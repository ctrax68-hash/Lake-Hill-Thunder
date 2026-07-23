// Verifies Phase 6c's (PORT_PROGRESS.md) src/audio/mixer.{h,cpp} -- the
// audioTick()-equivalent parameter updates plus renderStereo()'s per-sample
// synthesis -- entirely bgfx/SDL2-free (audio_engine.h/.cpp, the only
// SDL2-touching layer, isn't exercised here; that's covered by the native
// headless SDL_AUDIODRIVER=dummy run instead, see PORT_PROGRESS.md).

#include "../src/audio/mixer.h"
#include "../src/sim/car.h"
#include "../src/sim/race_state.h"
#include "../src/sim/rng.h"

#include <cmath>
#include <cstdio>
#include <vector>

namespace {
int g_failures = 0;

void expectTrue(const char* label, bool cond) {
    if (!cond) {
        std::fprintf(stderr, "%s: FAILED\n", label);
        ++g_failures;
    }
}

constexpr double kSampleRate = 44100.0;

double rms(const std::vector<float>& interleaved) {
    double sumSq = 0.0;
    for (float s : interleaved) sumSq += (double)s * (double)s;
    return std::sqrt(sumSq / interleaved.size());
}

Car makePlayer() {
    Car c;
    c.isPlayer = true;
    return c;
}

// RMS of a-b, sample for sample -- isolates exactly what one extra tick()
// call changed, unconfounded by the ambient noise-buffer/oscillator phase
// naturally differing between two separately-measured time windows (which
// a plain "rms(before) vs rms(after)" comparison can't rule out, since a
// one-shot's peak contribution is small next to a driving engine's own
// noise-driven layers). `Mixer` has no owning pointers -- every member is
// a plain value type -- so copying it (via the implicit copy constructor)
// snapshots the exact internal DSP state and is a safe, correct way to
// diff "what if the trigger hadn't fired" against reality.
double diffRms(const std::vector<float>& a, const std::vector<float>& b) {
    double sumSq = 0.0;
    for (size_t i = 0; i < a.size(); ++i) {
        const double d = (double)a[i] - (double)b[i];
        sumSq += d * d;
    }
    return std::sqrt(sumSq / a.size());
}
} // namespace

int main() {
    // ---- cold mixer (no tick() ever called): every ParamSmoother starts at
    // 0, so rendered output must be silent regardless of the oscillators
    // continuously running underneath. ----
    {
        Mulberry32 rng(1);
        Mixer m;
        m.init(kSampleRate, rng);
        std::vector<float> buf(2 * 2000);
        m.renderStereo(buf.data(), 2000);
        expectTrue("cold mixer (never ticked) is silent", rms(buf) < 1e-6);
    }

    // ---- driving engine becomes audible after enough samples for the
    // ParamSmoothers to converge, then falls back near-silent once sound is
    // turned off. ----
    {
        Mulberry32 rng(2);
        Mixer m;
        m.init(kSampleRate, rng);
        std::vector<Car> cars{makePlayer()};
        cars[0].v = 60;
        cars[0].thr = 1.0;
        RaceState state;
        state.mode = "race";
        state.flag = "green";

        m.tick(state, cars, /*sound=*/true, /*volume01=*/1.0);
        // Advance ~0.5s (well past every relevant time constant: busMaster
        // tau=0.05, engine gain tau=0.08) so the smoothers are converged
        // before measuring.
        std::vector<float> warmup(2 * 20000);
        m.renderStereo(warmup.data(), 20000);
        std::vector<float> steady(2 * 2000);
        m.renderStereo(steady.data(), 2000);
        expectTrue("driving engine is audible once converged", rms(steady) > 0.02);

        m.tick(state, cars, /*sound=*/false, /*volume01=*/1.0);
        std::vector<float> muteRamp(2 * 20000);
        m.renderStereo(muteRamp.data(), 20000);
        std::vector<float> muted(2 * 2000);
        m.renderStereo(muted.data(), 2000);
        expectTrue("turning sound off decays back to near-silence", rms(muted) < 0.01);
    }

    // ---- a hitFx rising edge (index.html:1431-1432) triggers an audible
    // one-shot thump on top of whatever else is playing. ----
    {
        Mulberry32 rng(3);
        Mixer m;
        m.init(kSampleRate, rng);
        std::vector<Car> cars{makePlayer()};
        RaceState state;
        state.mode = "race";
        state.flag = "green";

        // Warm up busMaster so it's not itself still ramping from 0 during
        // the comparison window below.
        for (int i = 0; i < 50; ++i) {
            m.tick(state, cars, true, 1.0);
            std::vector<float> discard(2 * 1024);
            m.renderStereo(discard.data(), 1024);
        }

        // Snapshot the exact converged state (see diffRms's own comment),
        // then diverge: one copy gets the hitFx rising edge, the other
        // doesn't. Render the full ~0.22s thump duration (index.html:1358)
        // from both and diff sample-for-sample.
        Mixer withThump = m;
        Mixer withoutThump = m;
        cars[0].hitFx = 0.5; // rising edge past lastHitFx_(0)+0.15
        withThump.tick(state, cars, true, 1.0);

        const int n = (int)(0.22 * kSampleRate);
        std::vector<float> a(2 * n), b(2 * n);
        withThump.renderStereo(a.data(), n);
        withoutThump.renderStereo(b.data(), n);
        // Both copies diverged from an identical snapshot and get retargeted
        // with identical tick() inputs except the new hitFx value, so this
        // diff isolates exactly (and only) the thump's own contribution --
        // not a noise-floor-limited measurement, so a modest threshold well
        // under the ~0.0046 this specific scenario actually measures is a
        // meaningful, non-flaky pass criterion.
        expectTrue("hitFx rising edge triggers an audible thump", diffRms(a, b) > 0.002);
    }

    // ---- a fresh spotter message (state.spotT rising, index.html:1459-
    // 1461) triggers an audible blip, using the same controlled-diff
    // technique as the hitFx thump check above. ----
    {
        Mulberry32 rng(4);
        Mixer m;
        m.init(kSampleRate, rng);
        std::vector<Car> cars{makePlayer()};
        RaceState state;
        state.mode = "race";
        state.flag = "green";
        for (int i = 0; i < 50; ++i) {
            m.tick(state, cars, true, 1.0);
            std::vector<float> discard(2 * 1024);
            m.renderStereo(discard.data(), 1024);
        }

        Mixer withBlip = m;
        Mixer withoutBlip = m;
        state.spotT = 2.2; // a fresh spotterSay() call, rising past lastSpotT_(0)
        withBlip.tick(state, cars, true, 1.0);

        const int n = (int)(0.09 * kSampleRate); // spotterBlip()'s own duration
        std::vector<float> a(2 * n), b(2 * n);
        withBlip.renderStereo(a.data(), n);
        withoutBlip.renderStereo(b.data(), n);
        // See the hitFx thump check's comment above -- same isolated-diff
        // reasoning; this scenario measures ~0.0027.
        expectTrue("a fresh spotter message triggers an audible blip", diffRms(a, b) > 0.001);
    }

    // ---- output always stays within [-1,1] under a "loud" scenario (many
    // nearby opponents + a blowout + a spotter message all at once). ----
    {
        Mulberry32 rng(5);
        Mixer m;
        m.init(kSampleRate, rng);
        std::vector<Car> cars(4, makePlayer());
        cars[0].isPlayer = true;
        for (size_t i = 1; i < cars.size(); ++i) {
            cars[i].isPlayer = false;
            cars[i].x = 5.0 * (double)i;
            cars[i].v = 55;
        }
        cars[0].v = 70;
        cars[0].thr = 1.0;
        cars[0].blown = true;
        RaceState state;
        state.mode = "race";
        state.flag = "yellow";
        state.greenT = 1.0;
        state.spotT = 2.2;

        m.tick(state, cars, true, 1.0);
        std::vector<float> loud(2 * 8000);
        m.renderStereo(loud.data(), 8000);
        bool allInRange = true;
        for (float s : loud) {
            if (!std::isfinite(s) || s < -1.0f || s > 1.0f) allInRange = false;
        }
        expectTrue("output stays within [-1,1] and finite under a loud mix", allInRange);
    }

    if (g_failures == 0) {
        std::printf("mixer_test: audioTick-equivalent parameter updates and renderStereo synthesis match "
                     "expectations.\n");
        return 0;
    }
    std::fprintf(stderr, "mixer_test: %d MISMATCHES.\n", g_failures);
    return 1;
}
