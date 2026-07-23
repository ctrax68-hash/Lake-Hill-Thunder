// Verifies leaderboard.{h,cpp}'s buildLeaderboardRows() against hand-
// computed expectations: top-N + pinned-player-row list building, tag
// precedence (out > pit > spinT > gap-or-name), and the live-gap
// interpolation via gapTimeAt(). Pure logic, no bgfx dependency for this
// function (drawLeaderboard() itself is the only bgfx-touching part of
// this file, not exercised here).

#include "../src/render/leaderboard.h"

#include <cstdio>
#include <cstring>

namespace {

bool ok = true;

void check(bool cond, const char* what) {
    if (!cond) {
        std::fprintf(stderr, "leaderboard_test: FAILED -- %s\n", what);
        ok = false;
    }
}

void checkEq(const std::string& got, const char* want, const char* what) {
    if (got != want) {
        std::fprintf(stderr, "leaderboard_test: FAILED -- %s (got \"%s\", want \"%s\")\n",
                      what, got.c_str(), want);
        ok = false;
    }
}

Car makeCar(bool isPlayer, const char* name, int num, double prog) {
    Car c;
    c.isPlayer = isPlayer;
    c.name = name;
    c.num = num;
    c.prog = prog;
    return c;
}

} // namespace

int main() {
    // Scenario A: 8 cars, already descending by prog (8.0 down to 1.0),
    // player at index 2 (within the top 5) -> no pinning, list is exactly
    // [0,1,2,3,4].
    {
        std::vector<Car> cars;
        for (int i = 0; i < 8; ++i) cars.push_back(makeCar(i == 2, "DRIVER X", 10 + i, 8.0 - i));
        std::vector<const Car*> order;
        for (auto& c : cars) order.push_back(&c);

        const auto rows = buildLeaderboardRows(order, "menu", "green", 0.0, 480.0);
        check(rows.size() == 5, "top-5-only list should have exactly 5 rows when player is inside top 5");
        bool anyDivider = false;
        for (auto& r : rows) if (r.dividerBefore) anyDivider = true;
        check(!anyDivider, "no divider should appear when the player isn't pinned");
        check(rows[2].isPlayerRow && rows[2].rank == 3, "row index 2 should be the player, rank 3");
        checkEq(rows[2].tag, "YOU", "player's own row should read YOU when not showing gaps");
    }

    // Scenario B: player at index 6 (outside top 5) -> pinned as a 6th
    // row, divider only on that last row.
    {
        std::vector<Car> cars;
        for (int i = 0; i < 8; ++i) cars.push_back(makeCar(i == 6, "DRIVER X", 10 + i, 8.0 - i));
        std::vector<const Car*> order;
        for (auto& c : cars) order.push_back(&c);

        const auto rows = buildLeaderboardRows(order, "menu", "green", 0.0, 480.0);
        check(rows.size() == 6, "pinned player should add a 6th row");
        check(!rows[4].dividerBefore, "no divider before the last top-5 row");
        check(rows[5].dividerBefore, "divider should appear before the pinned player row");
        check(rows[5].isPlayerRow && rows[5].rank == 7, "pinned row should be the player, rank 7");
    }

    // Tag precedence: out > pit > spinT, regardless of showGaps.
    {
        std::vector<Car> cars;
        cars.push_back(makeCar(false, "LEAD CAR", 1, 5.0));
        cars.push_back(makeCar(false, "OUT CAR", 2, 4.0));
        cars[1].out = true;
        cars.push_back(makeCar(false, "PIT CAR", 3, 3.0));
        cars[2].pit = 1;
        cars.push_back(makeCar(false, "WRECK CAR", 4, 2.0));
        cars[3].spinT = 1.0;
        cars.push_back(makeCar(true, "PLAYER", 5, 1.0));
        std::vector<const Car*> order;
        for (auto& c : cars) order.push_back(&c);

        // showGaps true: mode=race, flag=green, simT=5 -> floor(5/5)=1, odd.
        const auto rows = buildLeaderboardRows(order, "race", "green", 5.0, 480.0);
        check(rows.size() == 5, "5 cars, all within top 5");
        checkEq(rows[1].tag, "OUT", "out should override even while showGaps is true");
        checkEq(rows[2].tag, "PIT", "pit should override even while showGaps is true");
        checkEq(rows[3].tag, "WRECK", "spinT should override even while showGaps is true");
    }

    // showGaps toggling: same field, but mode != "race" -> no gap/leader
    // resolution, names/YOU stay as-is.
    {
        std::vector<Car> cars;
        cars.push_back(makeCar(false, "A B", 1, 5.0));
        cars.push_back(makeCar(false, "C D", 2, 4.0));
        std::vector<const Car*> order;
        for (auto& c : cars) order.push_back(&c);

        const auto rowsMenu = buildLeaderboardRows(order, "menu", "green", 5.0, 480.0);
        checkEq(rowsMenu[0].tag, "B", "leader's tag should be their surname, not LEADER, while mode!=race");
        checkEq(rowsMenu[1].tag, "D", "non-leader's tag should be their surname while showGaps is false");
    }

    // Leader tag + gap-fallback (no progHist yet): showGaps true, leader
    // gets "LEADER"; a trailing car 0.5 prog behind with no progHist
    // history falls back to dp*lapEst (index.html:3947's fallback path).
    // lastLapT==0 for the leader here, so lapEst = trackTotal/48 = 10.0
    // (480/48), giving gap = 0.5*10.0 = 5.0 -> "+5.0".
    {
        std::vector<Car> cars;
        cars.push_back(makeCar(false, "A B", 1, 5.0));
        cars.push_back(makeCar(false, "C D", 2, 4.5));
        std::vector<const Car*> order;
        for (auto& c : cars) order.push_back(&c);

        const auto rows = buildLeaderboardRows(order, "race", "green", 5.0, 480.0);
        checkEq(rows[0].tag, "LEADER", "leader's tag should be LEADER while showGaps is true");
        checkEq(rows[1].tag, "+5.0", "gap fallback should be dp*lapEst with no progHist history");
    }

    // Real gapTimeAt() interpolation: give the ahead car 2 progHist
    // samples spanning its prog through the trailing car's current prog.
    // Hand-computed: hist={ {t=0,prog=5.0}, {t=1,prog=6.0} }, target
    // prog=5.5 -> interpolated t = 0 + (5.5-5.0)*(1-0)/(6.0-5.0) = 0.5.
    // gap = simT - shiftT = 5.0 - 0.5 = 4.5 -> "+4.5".
    {
        std::vector<Car> cars;
        Car ahead = makeCar(false, "A B", 1, 6.0);
        ahead.progHist.push_back({0.0, 5.0});
        ahead.progHist.push_back({1.0, 6.0});
        cars.push_back(ahead);
        cars.push_back(makeCar(false, "C D", 2, 5.5)); // dp = 6.0-5.5 = 0.5 < 1
        std::vector<const Car*> order;
        for (auto& c : cars) order.push_back(&c);

        const auto rows = buildLeaderboardRows(order, "race", "green", 5.0, 480.0);
        checkEq(rows[1].tag, "+4.5", "gapTimeAt() interpolation should produce +4.5");
    }

    // A full lap behind (dp>=1): "-N LAP", not a decimal gap at all.
    {
        std::vector<Car> cars;
        cars.push_back(makeCar(false, "A B", 1, 6.0));
        cars.push_back(makeCar(false, "C D", 2, 4.5)); // dp = 1.5
        std::vector<const Car*> order;
        for (auto& c : cars) order.push_back(&c);

        const auto rows = buildLeaderboardRows(order, "race", "green", 5.0, 480.0);
        checkEq(rows[1].tag, "-1 LAP", "a full lap down should read -1 LAP, not a decimal gap");
    }

    // Empty order should be a safe no-op.
    {
        const auto rows = buildLeaderboardRows({}, "race", "green", 5.0, 480.0);
        check(rows.empty(), "empty order should produce no rows");
    }

    if (ok) {
        std::printf("leaderboard_test: row-building, tag precedence, and gap math match expectations.\n");
        return 0;
    }
    return 1;
}
