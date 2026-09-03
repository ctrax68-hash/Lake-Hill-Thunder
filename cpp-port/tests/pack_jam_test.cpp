// N6: the field-jam harness.
//
// WHY THIS FILE EXISTS. Milltown Bullring ends its races with 2 of 20 cars
// moving. The field collapses from ~31 m/s to 1-6 m/s within about 100 s and
// never recovers. That has been reproduced for several rounds and NOT fixed:
// one attempt (a closing-rate blocker rule) was measured and came out worse,
// and the round that found it wrote down, correctly, that the next step was an
// instrument rather than another tuning guess. This is that instrument.
//
// WHAT IT MEASURES, AND WHY THOSE THINGS. `drivability_test` already reports
// DNF counts, which say a race went badly but nothing about how. A jam is a
// dynamic, so the useful readings are rates and mechanisms:
//
//   movingFrac   -- the headline symptom.
//   yieldFrac    -- the fraction of the field commanding brake because of the
//                   car ahead, i.e. how much of the pack is yielding at once.
//                   This is the suspected mechanism: the rule that makes one
//                   car lift is the same rule that, applied to everybody
//                   simultaneously, collapses the pack.
//   packLen      -- how much of the lap the field occupies. Distinguishes one
//                   solid ball from several clusters, which need different
//                   fixes.
//   contacts     -- overlapping pairs, the thing that actually costs damage.
//
// Reported per track, because the failure is track-shaped: it is worst on the
// shortest lap, which is exactly where a fixed following distance eats the
// largest fraction of the available room.

#include "../src/sim/race.h"
#include "../src/sim/tracks_data.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

namespace {
int g_failures = 0;

void expect(bool cond, const char* what) {
    if (!cond) {
        std::fprintf(stderr, "FAIL: %s\n", what);
        ++g_failures;
    }
}

struct JamReport {
    double movingFrac = 0;   // cars with v > 5 m/s, averaged over the window
    double meanV = 0;        // mean speed of running cars, averaged
    double worstMovingFrac = 1.0; // the low-water mark
    double packLen = 0;      // metres of lap occupied, at the end
    int contacts = 0;        // overlapping pairs, at the end
    int running = 0;         // not out/done, at the end
    // The property N6 actually fixed: after the field bunches, can it get
    // going again? Before the collision fix the answer was no, ever -- escape
    // from a pile-up was arithmetically impossible, so the first bunch was
    // permanent. This is the best moving fraction reached after the pack has
    // had a chance to tangle, which distinguishes "recovered" from "never
    // jammed" only in that both are healthy.
    double recoveredFrac = 0.0;
};

// Signed along-track gap, wrapped to [-total/2, +total/2].
double wrapDs(double ds, double total) {
    if (ds < -total / 2) ds += total;
    if (ds > total / 2) ds -= total;
    return ds;
}

JamReport runField(int trackIdx, double seconds, unsigned seed) {
    const Track track(TRACKS[trackIdx]);
    Mulberry32 rng(seed), rngR(seed * 7919u + 13u);
    RaceState state;
    PaceCar pace;
    std::vector<Car> cars;
    std::vector<Car*> finishOrder;
    gridStart(track, rng, state, pace, cars, finishOrder, nullptr);
    state.mode = "race";
    state.flag = "green";
    // Long enough that the field cannot simply run out of laps before the jam
    // has a chance to form; the collapse is reported to happen inside 100 s.
    state.laps = 200;
    state.finishLaps = state.laps;

    PlayerInput input; // no human: every car is AI, which is what jams
    JamReport r;

    const int steps = (int)(seconds / DT);
    int samples = 0;
    // Skip the first few seconds: the field starts bunched on the grid by
    // construction, and counting that as a jam would flatter any fix that
    // merely delays one.
    const int warmup = (int)(12.0 / DT);

    for (int i = 0; i < steps; ++i) {
        tick(state, cars, pace, track, rngR, input, finishOrder);
        if (i < warmup) continue;

        int moving = 0, alive = 0;
        double sumV = 0;
        for (const Car& c : cars) {
            if (c.out || c.done) continue;
            ++alive;
            sumV += c.v;
            if (c.v > 5.0) ++moving;
        }
        if (alive > 0) {
            const double frac = (double)moving / alive;
            r.movingFrac += frac;
            r.meanV += sumV / alive;
            r.worstMovingFrac = std::min(r.worstMovingFrac, frac);
            // Only count recovery from 30 s on, by which point the grid pack
            // has either dispersed or jammed.
            if (state.t > 30.0) r.recoveredFrac = std::max(r.recoveredFrac, frac);
            ++samples;
        }
    }
    if (samples > 0) {
        r.movingFrac /= samples;
        r.meanV /= samples;
    }

    // End-state geometry.
    std::vector<double> ss;
    for (const Car& c : cars) {
        if (c.out || c.done) continue;
        ++r.running;
        ss.push_back(c.s);
    }
    for (size_t a = 0; a < cars.size(); ++a) {
        for (size_t b = a + 1; b < cars.size(); ++b) {
            if (cars[a].out || cars[b].out || cars[a].done || cars[b].done) continue;
            const double ds = std::fabs(wrapDs(cars[b].s - cars[a].s, track.total()));
            if (ds < 5.0 && std::fabs(cars[a].lat - cars[b].lat) < 2.0) ++r.contacts;
        }
    }
    if (ss.size() > 1) {
        std::sort(ss.begin(), ss.end());
        // Largest gap between consecutive cars around the lap; the pack
        // occupies everything else.
        double biggestGap = ss.front() + track.total() - ss.back();
        for (size_t i = 1; i < ss.size(); ++i) biggestGap = std::max(biggestGap, ss[i] - ss[i - 1]);
        r.packLen = track.total() - biggestGap;
    }
    return r;
}
} // namespace

int main() {
    std::printf("%-22s %8s %8s %8s %8s %8s %8s %6s\n", "track", "moving", "worst", "recovrd", "meanV",
                "packLen", "contact", "run");
    for (int i = 0; i < 4; ++i) {
        // Two seeds. This simulator is chaotic and single-seed differences of
        // a car or two are noise -- a point the N4/N5 round had to make the
        // hard way. Only effects large enough to show on both are treated as
        // real.
        for (unsigned seed : {12345u, 777u}) {
            const JamReport r = runField(i, 240.0, seed);
            std::printf("%-18s s%-3u %7.2f %8.2f %8.2f %8.2f %8.0f %8d %6d\n", TRACKS[i].name.c_str(),
                        seed % 1000, r.movingFrac, r.worstMovingFrac, r.recoveredFrac, r.meanV, r.packLen,
                        r.contacts, r.running);

            // THE GUARD, stated as what N6 actually fixed rather than as an
            // aspiration.
            //
            // A field bunches -- that is racing. What it must not do is bunch
            // ONCE and stay welded for the rest of the race, which is what the
            // un-rate-limited collision penalty guaranteed. So the assertion
            // is recovery: at some point after the grid pack disperses, the
            // field must be racing again.
            //
            // Deliberately NOT "the field is always moving". That would be the
            // behaviour worth having, and this build does not have it -- see
            // PORT_PROGRESS's N6 entry for the residual, wear-driven late-race
            // collapse that remains. Asserting it here would give a permanently
            // red test that says nothing about regressions, which is worse than
            // a guard that pins the ground actually gained.
            const std::string tag = TRACKS[i].name + " recovers from bunching instead of welding solid";
            expect(r.recoveredFrac > 0.85, tag.c_str());

            // A ratchet on the average, set below every figure this build
            // measures (worst is 0.52) and above the pre-fix worst (0.17), so
            // it catches a real regression without re-encoding today's noise.
            const std::string tag2 = TRACKS[i].name + " field does not regress to a permanent crawl";
            expect(r.movingFrac > 0.40, tag2.c_str());
        }
    }

    std::printf(g_failures == 0 ? "pack_jam_test: PASS\n" : "pack_jam_test: FAILURES ABOVE\n");
    return g_failures == 0 ? 0 : 1;
}
