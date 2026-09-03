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
void expect(bool cond, const char* what) {
    if (!cond) {
        std::fprintf(stderr, "FAIL: %s\n", what);
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
        std::vector<Car*> finishOrder;
        gridStart(track, rng, state, pace, cars, finishOrder, nullptr);

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
    //
    // N6/L10: these numbers are a DELIBERATE DIVERGENCE from JS, not a
    // re-baseline to hide a failure. Recorded here in the same style as L12's
    // change to speed_model_test, which faced the identical situation.
    //
    // JS dissipated a fixed fraction of the scalar speed DIFFERENCE on every
    // tick two cars overlapped. That made sustained contact a permanent
    // velocity sink and, because the penalty scaled with the difference, it
    // bit hardest on the car trying to escape -- roughly 12 m/s^2 of loss
    // against 5 m/s^2 of drive, so escaping a pile-up was arithmetically
    // impossible. Measured consequence: on Milltown the whole field ended up
    // in a 35 m stretch of an 848 m lap at ~0 m/s, permanently.
    //
    // The port now removes the CLOSING component along the contact normal, and
    // splits it between hitter and victim by role rather than by array index.
    // For this test's head-on case (40 m/s into 30 m/s, closing at 10) that
    // means the hitter sheds 6*0.45 = 2.7 and the victim 6*0.20 = 1.2, where
    // JS shed 1.9 and 0.3. A genuine 10 m/s rear-ending costing more than
    // 2 m/s between the two cars is the more defensible number on its own
    // terms, quite apart from the jam it fixes.
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
        expectNear("collide[0].v", cars[0].v, 37.3); // JS: 38.1 -- see above
        expectNear("collide[0].hdg", cars[0].hdg, 0.0);
        expectNear("collide[1].x", cars[1].x, 2.55);
        expectNear("collide[1].y", cars[1].y, 0.0);
        expectNear("collide[1].v", cars[1].v, 28.8); // JS: 29.7 -- see above
        expectNear("collide[1].hdg", cars[1].hdg, 0.0);
        // Phase 6b (PORT_PROGRESS.md): collide()'s hitter.hitFx accumulation
        // (index.html:1227, `hitter.hitFx += cv2*0.04`) -- cv2 = |37.3-28.8|
        // = 8.5, so cars[0] (the hitter: a.v>b.v) gains 8.5*0.04 = 0.34;
        // cars[1] (the victim) gets no hitFx from this site. (The 8.5 is 8.4
        // under JS's own speed loss; see the divergence note above.)
        expectNear("collide[0].hitFx", cars[0].hitFx, 0.34);
        expectNear("collide[1].hitFx", cars[1].hitFx, 0.0);
    }

    // ---- collide(): a SEPARATING pair pays nothing (N6) ----
    //
    // The single property the field-jam fix turns on, and the one JS got
    // wrong. Two overlapping cars moving APART must lose no speed: penalising
    // them is what made a pile-up inescapable, because pulling away from your
    // neighbour is exactly what raises the old speed-difference penalty.
    {
        std::vector<Car> cars(2);
        cars[0].x = 0; cars[0].y = 0; cars[0].hdg = 0;
        cars[1].x = 1.5; cars[1].y = 0; cars[1].hdg = 0;
        // b is ahead of a and pulling away: they overlap, but are separating.
        cars[0].v = 20; cars[1].v = 30;
        RaceState state;
        state.mode = "race";
        state.flag = "green";
        Mulberry32 rngR(999);
        collide(cars, state, track, rngR);
        expectNear("separating pair: trailing car keeps its speed", cars[0].v, 20.0);
        expectNear("separating pair: leading car keeps its speed", cars[1].v, 30.0);
    }

    // ---- collide(): P3 (NT2003 engine-feel plan, damage that pulls) --
    // dmgPull must come out SIGNED and OPPOSITE for the two cars in a
    // one-sided (laterally offset) impact -- the previous block's fixture
    // has cars[1] directly ahead of cars[0] (dy=0), which is degenerate for
    // this: both cars' local-left component of the push direction is
    // exactly zero there, so this needs its own laterally-offset fixture.
    // cars[1] sits up-and-to-the-right of cars[0] (both hdg=0, facing +x,
    // so +y is each car's own "left"): the repulsion push shoves cars[0]
    // down-and-left (toward its own RIGHT, i.e. negative pull) and cars[1]
    // up-and-right (toward its own LEFT, i.e. positive pull) -- opposite
    // signs, matching two cars pushed apart from each other. ----
    {
        std::vector<Car> cars(2);
        cars[0].x = 0;
        cars[0].y = 0;
        cars[0].v = 40;
        cars[0].hdg = 0;
        cars[1].x = 2.0;
        cars[1].y = 1.0;
        cars[1].v = 30;
        cars[1].hdg = 0;
        RaceState state;
        state.mode = "race";
        state.flag = "green";
        Mulberry32 rngR(999);
        collide(cars, state, track, rngR);
        // cv2 = |38.1-29.7| = 8.4 (same closing speed as the previous
        // fixture -- unaffected by the lateral offset), so cars[0] (hitter,
        // a.v>b.v) gets hitterDelta = min(0.08, 8.4*0.003) = 0.0252, and
        // cars[1] (victim) gets victimDelta = min(0.05, 8.4*0.002) = 0.0168.
        expectNear("collide P3 [0].dmgPull (hitter, pulled right)", cars[0].dmgPull, -0.0255, 1e-4);
        expectNear("collide P3 [1].dmgPull (victim, pulled left)", cars[1].dmgPull, 0.0170, 1e-4);
        expect(cars[0].dmgPull < 0.0 && cars[1].dmgPull > 0.0,
               "collide() gives the two cars in a one-sided impact OPPOSITE-signed pulls");
    }

    // ---- gridStart() clears finishOrder (Phase 4h bugfix, PORT_PROGRESS.md)
    // -- JS's own gridStart() does `S.finishOrder=[]` (index.html:590); this
    // port's version hadn't, which was harmless while gridStart() only ever
    // ran once per process but would dangle a Car* the instant a second race
    // (restart) starts, since `cars` is cleared and repopulated too. ----
    {
        Mulberry32 rng(12345);
        RaceState state;
        PaceCar pace;
        std::vector<Car> cars;
        std::vector<Car*> finishOrder;
        gridStart(track, rng, state, pace, cars, finishOrder, nullptr);

        // Simulate a finished race leaving stale entries behind.
        Car fake;
        finishOrder.push_back(&fake);
        finishOrder.push_back(&fake);
        if (finishOrder.size() != 2) {
            std::fprintf(stderr, "gridStart finishOrder setup: expected 2 entries before restart\n");
            ++g_failures;
        }

        // A second gridStart() (restart) must clear the stale pointers.
        gridStart(track, rng, state, pace, cars, finishOrder, nullptr);
        if (!finishOrder.empty()) {
            std::fprintf(stderr, "gridStart finishOrder: expected empty after a second gridStart(), got %zu\n",
                         finishOrder.size());
            ++g_failures;
        }
    }

    if (g_failures == 0) {
        std::printf("race_sim_test: gridStart/stepPace/updateAero match JS; collide matches the documented N6/L10 divergence.\n");
        return 0;
    }
    std::fprintf(stderr, "race_sim_test: %d MISMATCHES.\n", g_failures);
    return 1;
}
