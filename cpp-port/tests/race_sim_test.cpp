// Verifies gridStart()/stepPace()/updateAero()/collide() (src/sim/race.{h,cpp})
// against ground truth captured from the original JS functions, run under
// Node. See PORT_PROGRESS.md's Phase 1f/1g notes for the exact script.
// (stepCar()/tick() themselves are verified separately, against a real
// multi-second JS trace, via tests/determinism_pace_check.cpp -- that one
// needs an externally generated fixture so isn't a plain ctest target.)

#include "../src/sim/car.h"
#include "../src/sim/race.h"
#include "../src/sim/rng.h"
#include "../src/sim/tracks_data.h"

#include <cmath>
#include <cstdio>

namespace {
int g_failures = 0;

void expectNear(const char* label, double got, double expected, double tol = 1e-6) {
    if (std::fabs(got - expected) > tol) {
        std::fprintf(stderr, "%s: got %.17g expected %.17g (diff %.3g)\n",
                     label, got, expected, got - expected);
        ++g_failures;
    }
}
void expectEq(const char* label, int got, int expected) {
    if (got != expected) {
        std::fprintf(stderr, "%s: got %d expected %d\n", label, got, expected);
        ++g_failures;
    }
}
void expectEqStr(const char* label, const std::string& got, const std::string& expected) {
    if (got != expected) {
        std::fprintf(stderr, "%s: got '%s' expected '%s'\n", label, got.c_str(), expected.c_str());
        ++g_failures;
    }
}
} // namespace

int main() {
    Track track(TRACKS[0]);

    // ---- gridStart() ----
    {
        Mulberry32 rng(12345);
        RaceState state;
        PaceCar pace;
        std::vector<Car> cars;
        gridStart(track, rng, state, pace, cars, nullptr);

        expectEq("cars.size()", static_cast<int>(cars.size()), FIELD);

        struct Expect {
            int arrIdx, idx;
            double x, y, hdg, s, lat, gridLane;
            int gridAhead, gridSlot;
        };
        // clang-format off
        Expect exps[] = {
            {0, 1, -149.04189148369937, 135.18023540655392, -0.051042569183660028, 885.60435433374209, -2.6, -2.6, -1, 0},
            {1, 2, -148.77658536125040, 140.37346298306076, -0.051042569183660028, 885.60435433374209, 2.6, 2.6, -1, 1},
            {2, 3, -160.02756520323305, 135.74145989634982, -0.051042569183660028, 874.60435433374209, -2.6, -2.6, 0, 2},
            {19, 0, -247.61767570723518, 132.92996484834674, -5.9127993049346745, 786.60435433374209, 2.6, 2.6, 17, 19},
        };
        // clang-format on
        for (auto& e : exps) {
            const Car& c = cars[e.arrIdx];
            char label[64];
            std::snprintf(label, sizeof(label), "gridStart car[%d].idx", e.arrIdx);
            expectEq(label, c.idx, e.idx);
            std::snprintf(label, sizeof(label), "gridStart car[%d].x", e.arrIdx);
            expectNear(label, c.x, e.x);
            std::snprintf(label, sizeof(label), "gridStart car[%d].y", e.arrIdx);
            expectNear(label, c.y, e.y);
            std::snprintf(label, sizeof(label), "gridStart car[%d].hdg", e.arrIdx);
            expectNear(label, c.hdg, e.hdg);
            std::snprintf(label, sizeof(label), "gridStart car[%d].s", e.arrIdx);
            expectNear(label, c.s, e.s);
            std::snprintf(label, sizeof(label), "gridStart car[%d].lat", e.arrIdx);
            expectNear(label, c.lat, e.lat);
            std::snprintf(label, sizeof(label), "gridStart car[%d].gridLane", e.arrIdx);
            expectNear(label, c.gridLane, e.gridLane);
            std::snprintf(label, sizeof(label), "gridStart car[%d].gridAhead", e.arrIdx);
            expectEq(label, c.gridAhead, e.gridAhead);
            std::snprintf(label, sizeof(label), "gridStart car[%d].gridSlot", e.arrIdx);
            expectEq(label, c.gridSlot, e.gridSlot);
        }
        expectNear("PACE.s", pace.s, 903.60435433374209);
        expectNear("PACE.hdg", pace.hdg, -0.051042569183660028);
        expectNear("PACE.x", pace.x, -130.93268142687430);
        expectNear("PACE.y", pace.y, 136.85848184786855);
        expectEqStr("PACE.state", pace.state, "lead");

        // ---- stepPace() ----
        state.mode = "pace";
        for (int i = 0; i < 5; ++i) stepPace(pace, state, track);
        expectEqStr("stepPace after 5 ticks: state", pace.state, "lead");
        expectNear("stepPace after 5 ticks: s", pace.s, 906.70435433374212);
        expectNear("stepPace after 5 ticks: lat", pace.lat, 0.0);
        expectNear("stepPace after 5 ticks: v", pace.v, 31.0);

        for (int i = 0; i < 500; ++i) stepPace(pace, state, track);
        expectEqStr("stepPace after 505 ticks: state", pace.state, "lead");
        expectNear("stepPace after 505 ticks: s", pace.s, 1216.7043543337090);
        expectNear("stepPace after 505 ticks: lat", pace.lat, 0.0);
        expectNear("stepPace after 505 ticks: v", pace.v, 31.0);
    }

    // ---- updateAero() ----
    {
        std::vector<Car> cars(3);
        cars[0].s = 100;
        cars[0].lat = -2.6;
        cars[1].s = 105;
        cars[1].lat = -2.6;
        cars[2].s = 200;
        cars[2].lat = 3.0;
        updateAero(cars, track);
        expectNear("aero[0].draftF", cars[0].draftF, 1.0);
        if (cars[0].dirty) {
            std::fprintf(stderr, "aero[0].dirty: expected false, got true\n");
            ++g_failures;
        }
        expectNear("aero[1].draftF", cars[1].draftF, 0.0);
        expectNear("aero[2].draftF", cars[2].draftF, 0.0);
    }

    // ---- collide() ----
    {
        std::vector<Car> cars(2);
        cars[0].x = 0;
        cars[0].y = 0;
        cars[0].v = 40;
        cars[0].hdg = 0;
        cars[1].x = 1.5;
        cars[1].y = 0;
        cars[1].v = 30;
        cars[1].hdg = 0;
        RaceState state;
        state.mode = "race";
        state.flag = "green";
        Mulberry32 rngR(999);
        collide(cars, state, track, rngR);
        expectNear("collide[0].x", cars[0].x, -1.05);
        expectNear("collide[0].y", cars[0].y, 0.0);
        expectNear("collide[0].v", cars[0].v, 38.1);
        expectNear("collide[0].hdg", cars[0].hdg, 0.0);
        expectNear("collide[1].x", cars[1].x, 2.55);
        expectNear("collide[1].y", cars[1].y, 0.0);
        expectNear("collide[1].v", cars[1].v, 29.7);
        expectNear("collide[1].hdg", cars[1].hdg, 0.0);
    }

    if (g_failures == 0) {
        std::printf("race_sim_test: gridStart/stepPace/updateAero/collide all match JS.\n");
        return 0;
    }
    std::fprintf(stderr, "race_sim_test: %d MISMATCHES.\n", g_failures);
    return 1;
}
