// Verifies cornerSpeed()/targetSpeed() (src/sim/car.{h,cpp}).
//
// HISTORY, AND WHY THIS FILE NO LONGER ASSERTS JS PARITY.
//
// This test used to pin cornerSpeed()/targetSpeed() bit-for-bit against ground
// truth captured from the original JS functions (index.html:629-649) under
// Node. Those assertions passed for this port's entire life and were still
// passing on the day the game froze solid.
//
// The formula they were guarding does not converge. cornerSpeed() grows `mu`
// by the aero term `dfK*v*v`, feeds that inflated mu into cornerCap(), whose
// denominator `max(0.25, 1 - mu*t)` PINS at 0.25 once mu*tan(bank) passes
// 0.75 -- after which cornerCap is linear in mu, mu is quadratic in v, and
// every iteration compounds the last. It terminates only because the loop runs
// exactly four times. Big Sable, banked 23 degrees, iterated to 325 m/s
// (727 mph) against a physical limit of 76.3 m/s: a 4.26x over-command. The
// old expected value on line 33 of this file was literally 320.82072783979334.
//
// Under JS's KINEMATIC model that was harmless -- there was no tire limit to
// violate and a separate yaw cap caught it. Against this port's bicycle tire
// model it is fatal, and it is shared by all 20 cars. So this file was
// asserting, precisely and confidently, faithfulness to a formula that is
// wrong for the physics this port actually runs. It is being changed on
// purpose, not re-baselined because it broke: **the new assertions check the
// property that matters (a planned speed the tires can actually deliver),
// not a fresh set of magic numbers copied out of the new implementation.**
// Numbers copied from an implementation only ever assert that the code does
// what it currently does.
//
// See PORT_PROGRESS.md's N2 entry for the measured effect.

#include "../src/sim/car.h"
#include "../src/sim/constants.h"
#include "../src/sim/tracks_data.h"

#include <cmath>
#include <cstdio>

namespace {
int g_failures = 0;

void check(bool cond, const char* what) {
    if (!cond) {
        std::fprintf(stderr, "FAIL: %s\n", what);
        ++g_failures;
    }
}

// The lateral acceleration a banked corner can actually deliver on tires of
// grip `mu` -- the standard banked-turn result, and the same construction
// cornerSpeed()'s own clamp and drivability_test's reference driver use.
double physicalLimit(const Track& track, double R, double s, double wear) {
    const double muTire = CAR.mu * (1 - 0.12 * wear);
    return std::sqrt(cornerCap(muTire, track.bankAt(s)) * R);
}
} // namespace

int main() {
    // Every track, every curved metre, every wear state: the plan must never
    // exceed what the tires can hold. Sweeping the real geometry rather than
    // spot-checking three hand-picked (R, s) pairs is the point -- the old
    // spot checks all passed while Big Sable was planning 727 mph two hundred
    // metres away from the sampled point.
    for (int t = 0; t < (int)TRACKS.size(); ++t) {
        const Track track(TRACKS[t]);
        const double total = track.total();
        double worstRatio = 0.0, worstS = 0.0, worstPlan = 0.0, worstLim = 0.0;

        for (double wear : {0.0, 0.3, 0.6, 0.9}) {
            for (double s = 0; s < total; s += 1.0) {
                const double curv = std::fabs(track.pointAt(s).curv);
                if (curv < 1e-6) continue; // straight
                const double R = 1.0 / curv;
                const double plan = cornerSpeed(track, R, s, wear);
                const double lim = physicalLimit(track, R, s, wear);

                check(std::isfinite(plan), "cornerSpeed returned a non-finite speed");
                check(plan > 0.0, "cornerSpeed returned a non-positive speed");

                const double ratio = plan / lim;
                if (ratio > worstRatio) {
                    worstRatio = ratio;
                    worstS = s;
                    worstPlan = plan;
                    worstLim = lim;
                }
            }
        }

        std::printf("%-20s worst planned/limit = %.3fx  (s=%.0f, %.1f vs %.1f m/s)\n",
                    TRACKS[t].name.c_str(), worstRatio, worstS, worstPlan, worstLim);

        // The headline assertion. 1.0 would be sitting exactly on the friction
        // circle with nothing left for bumps, traffic, wear or the yaw
        // transient, so the planner deliberately aims just inside it; this
        // bound simply refuses to let it plan a speed physics cannot deliver.
        check(worstRatio <= 1.0,
              "cornerSpeed plans a speed beyond the tires' friction limit -- any driver "
              "following it understeers into the wall");
        // And it must not have over-corrected into uselessly slow guidance.
        check(worstRatio > 0.5, "cornerSpeed is planning far below the limit -- the AI would crawl");
    }

    // targetSpeed() must stay bounded by the same physics, and must still
    // behave like a lookahead planner: slower into a corner than on a straight.
    const Track t0(TRACKS[0]);
    for (double s = 0; s < t0.total(); s += 25.0) {
        Car c{};
        c.s = s;
        c.v = 60;
        c.wear = 0.2;
        c.skill = 0.95;
        const double vT = targetSpeed(t0, c);
        check(std::isfinite(vT) && vT > 0.0, "targetSpeed returned a non-finite or non-positive speed");
        check(vT <= 95.0 + 1e-9, "targetSpeed exceeded its own 95 m/s ceiling");

        const double curv = std::fabs(t0.pointAt(s).curv);
        if (curv > 1e-6) {
            const double lim = physicalLimit(t0, 1.0 / curv, s, c.wear);
            // targetSpeed() brakes on APPROACH, so at the corner itself it may
            // legitimately still be carrying speed from the preceding straight;
            // what it must never do is plan multiples of the limit the way the
            // pre-fix version did (Big Sable: 95 flat, every sample).
            check(vT <= lim * 1.35,
                  "targetSpeed is still commanding well past the corner's physical limit");
        }
    }

    // Wear must make the planner more conservative, not less -- this held
    // before the fix and must keep holding.
    const Track t3(TRACKS[3]);
    double prev = 1e9;
    for (double wear : {0.0, 0.3, 0.6, 0.9}) {
        const double v = cornerSpeed(t3, 240, 600, wear);
        check(v < prev, "cornerSpeed does not decrease monotonically with tire wear");
        prev = v;
    }

    if (g_failures == 0) {
        std::printf("speed_model_test: planned corner speeds are physically achievable.\n");
        return 0;
    }
    std::fprintf(stderr, "speed_model_test: %d FAILURES.\n", g_failures);
    return 1;
}
