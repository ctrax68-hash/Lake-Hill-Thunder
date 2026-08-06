// Verifies proximity.{h,cpp}'s buildProximityList() -- the pure selection
// half of G22's spotter strip. Same split as minimap_test.cpp /
// leaderboard_test.cpp: the selection logic is testable without any
// rendering context, so it gets a real unit test even though drawProximity()
// itself needs bgfx for dbgTextPrintf().

#include "../src/render/proximity.h"

#include <cmath>
#include <cstdio>

namespace {

bool ok = true;

void check(bool cond, const char* what) {
    if (!cond) {
        std::fprintf(stderr, "proximity_test: FAILED -- %s\n", what);
        ok = false;
    }
}

constexpr double kTotal = 1000.0;

Car makeCarAt(int num, double s, double lat) {
    Car c;
    c.num = num;
    c.s = s;
    c.lat = lat;
    c.col = {1.0, 0.0, 0.0};
    return c;
}

} // namespace

int main() {
    // Cars outside the range are excluded; cars inside are kept.
    {
        std::vector<Car> cars = {makeCarAt(1, 500.0, 0.0),   // the player
                                  makeCarAt(2, 510.0, 2.0),   // 10 ahead  -> in
                                  makeCarAt(3, 480.0, -2.0),  // 20 behind -> in
                                  makeCarAt(4, 600.0, 0.0)};  // 100 ahead -> out
        const auto near = buildProximityList(cars, cars[0], kTotal, 30.0, 5);
        check(near.size() == 2, "only cars within sRange should be listed");
        bool has4 = false;
        for (auto& p : near) if (p.num == 4) has4 = true;
        check(!has4, "a car beyond sRange should be excluded");
    }

    // The player is never in their own proximity list.
    {
        std::vector<Car> cars = {makeCarAt(7, 100.0, 0.0)};
        check(buildProximityList(cars, cars[0], kTotal, 30.0, 5).empty(),
              "the player should not appear in their own list");
    }

    // Cars that are out or finished are skipped -- neither is someone you
    // can still hit.
    {
        std::vector<Car> cars = {makeCarAt(1, 500.0, 0.0), makeCarAt(2, 505.0, 1.0),
                                  makeCarAt(3, 495.0, 1.0)};
        cars[1].out = true;
        cars[2].done = true;
        check(buildProximityList(cars, cars[0], kTotal, 30.0, 5).empty(),
              "out/finished cars should be skipped");
    }

    // The along-track difference wraps: a car just across the start/finish
    // line reads as a few units ahead, not almost a full lap behind.
    {
        std::vector<Car> cars = {makeCarAt(1, 995.0, 0.0), makeCarAt(2, 5.0, 1.0)};
        const auto near = buildProximityList(cars, cars[0], kTotal, 30.0, 5);
        check(near.size() == 1, "a car across the start/finish line should still be near");
        check(near.size() == 1 && std::abs(near[0].dS - 10.0) < 1e-9,
              "dS should wrap to +10, not -990");
    }
    {
        // ... and symmetrically for a car just behind across the line.
        std::vector<Car> cars = {makeCarAt(1, 5.0, 0.0), makeCarAt(2, 995.0, 1.0)};
        const auto near = buildProximityList(cars, cars[0], kTotal, 30.0, 5);
        check(near.size() == 1 && std::abs(near[0].dS + 10.0) < 1e-9,
              "dS should wrap to -10 for a car just behind across the line");
    }

    // maxCars keeps the *nearest* cars, not an arbitrary subset.
    {
        std::vector<Car> cars = {makeCarAt(1, 500.0, 0.0)};
        cars.push_back(makeCarAt(2, 501.0, 0.0));  // 1 away  -> keep
        cars.push_back(makeCarAt(3, 528.0, 1.0));  // 28 away -> drop
        cars.push_back(makeCarAt(4, 498.0, -1.0)); // 2 away  -> keep
        const auto near = buildProximityList(cars, cars[0], kTotal, 30.0, 2);
        check(near.size() == 2, "maxCars should cap the list");
        bool has3 = false;
        for (auto& p : near) if (p.num == 3) has3 = true;
        check(!has3, "the cap should drop the furthest car, not the nearest");
    }

    // Display order is left-to-right by lateral offset, so a car on one side
    // of the player appears on that side of the strip.
    {
        std::vector<Car> cars = {makeCarAt(1, 500.0, 0.0)};
        cars.push_back(makeCarAt(2, 501.0, 3.0));
        cars.push_back(makeCarAt(3, 502.0, -3.0));
        cars.push_back(makeCarAt(4, 503.0, 0.5));
        const auto near = buildProximityList(cars, cars[0], kTotal, 30.0, 5);
        check(near.size() == 3, "all three neighbours should be listed");
        bool sorted = true;
        for (size_t i = 0; i + 1 < near.size(); ++i) {
            if (near[i].dLat > near[i + 1].dLat) sorted = false;
        }
        check(sorted, "the list should be sorted left-to-right by dLat");
        check(near.size() == 3 && near.front().num == 3 && near.back().num == 2,
              "the leftmost/rightmost cars should be at the ends");
    }

    // dLat/dS are relative to the player, not absolute track coordinates.
    {
        std::vector<Car> cars = {makeCarAt(1, 500.0, 4.0), makeCarAt(2, 512.0, 1.0)};
        const auto near = buildProximityList(cars, cars[0], kTotal, 30.0, 5);
        check(near.size() == 1 && std::abs(near[0].dLat + 3.0) < 1e-9,
              "dLat should be measured from the player's own lat");
        check(near.size() == 1 && std::abs(near[0].dS - 12.0) < 1e-9,
              "dS should be measured from the player's own s");
    }

    // Degenerate arguments produce an empty list rather than misbehaving.
    {
        std::vector<Car> cars = {makeCarAt(1, 500.0, 0.0), makeCarAt(2, 501.0, 0.0)};
        check(buildProximityList(cars, cars[0], kTotal, 30.0, 0).empty(), "maxCars=0 -> empty");
        check(buildProximityList(cars, cars[0], kTotal, 0.0, 5).empty(), "sRange=0 -> empty");
        check(buildProximityList(cars, cars[0], 0.0, 30.0, 5).empty(), "trackTotal=0 -> empty");
    }

    if (ok) {
        std::printf("proximity_test: selection, wrapping, capping and ordering all match.\n");
        return 0;
    }
    return 1;
}
