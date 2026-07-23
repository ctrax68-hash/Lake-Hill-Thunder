// Verifies src/audio/dsp.{h,cpp}'s pure DSP math (bgfx/SDL2-free, same
// category as track_surface_test.cpp/stadium_mesh_test.cpp): biquad
// frequency response (a lowpass/highpass/bandpass genuinely passes/rejects
// the frequencies they're supposed to, not just "doesn't crash"),
// oscillator waveform shape/period, noise-buffer statistics and looping,
// and the parameter smoother's exponential-convergence math.

#include "../src/audio/dsp.h"
#include "../src/sim/rng.h"

#include <algorithm>
#include <cmath>
#include <cstdio>

namespace {

int g_failures = 0;

void expectTrue(const char* label, bool cond) {
    if (!cond) {
        std::fprintf(stderr, "%s: FAILED\n", label);
        ++g_failures;
    }
}

void expectNear(const char* label, double got, double expected, double tol) {
    if (std::fabs(got - expected) > tol) {
        std::fprintf(stderr, "%s: got %.6f expected %.6f (tol %.6f)\n", label, got, expected, tol);
        ++g_failures;
    }
}

constexpr double kSampleRate = 44100.0;

// RMS output level of a biquad fed a steady sine at `freqHz`, after letting
// the filter settle for a warm-up period (so transient ring-up doesn't skew
// the measurement) -- the standard way to spot-check a filter's frequency
// response without needing a full FFT.
double biquadRmsAtFreq(Biquad& bq, double freqHz) {
    bq.reset();
    double phase = 0.0;
    const int warmup = 4000, measure = 4000;
    for (int i = 0; i < warmup; ++i) {
        const double x = std::sin(phase);
        phase += 2.0 * M_PI * freqHz / kSampleRate;
        bq.process(x);
    }
    double sumSq = 0.0;
    for (int i = 0; i < measure; ++i) {
        const double x = std::sin(phase);
        phase += 2.0 * M_PI * freqHz / kSampleRate;
        const double y = bq.process(x);
        sumSq += y * y;
    }
    return std::sqrt(sumSq / measure);
}

} // namespace

int main() {
    // Biquad lowpass (cutoff 900Hz, matching the engine tone's own filter,
    // index.html:1299): a low frequency (100Hz) should pass through close to
    // unchanged (RMS near sin's own ~0.707), a high frequency (8000Hz, well
    // above cutoff) should be strongly attenuated.
    {
        Biquad lp;
        lp.setLowpass(900.0, 0.9, kSampleRate);
        const double low = biquadRmsAtFreq(lp, 100.0);
        const double high = biquadRmsAtFreq(lp, 8000.0);
        expectTrue("lowpass passes low frequencies (RMS > 0.5)", low > 0.5);
        expectTrue("lowpass rejects high frequencies (RMS < 0.1)", high < 0.1);
        expectTrue("lowpass attenuates high freq far more than low freq", high < low * 0.2);
    }

    // Biquad highpass (1800Hz, matching the skid filter, index.html:1314):
    // opposite shape from the lowpass check above.
    {
        Biquad hp;
        hp.setHighpass(1800.0, 0.9, kSampleRate);
        const double low = biquadRmsAtFreq(hp, 100.0);
        const double high = biquadRmsAtFreq(hp, 8000.0);
        expectTrue("highpass rejects low frequencies (RMS < 0.1)", low < 0.1);
        expectTrue("highpass passes high frequencies (RMS > 0.5)", high > 0.5);
    }

    // Biquad bandpass (420Hz center, Q=0.7, matching the engine intake-noise
    // filter, index.html:1307): the center frequency should pass much more
    // strongly than a frequency an octave-plus away on either side.
    {
        Biquad bp;
        bp.setBandpass(420.0, 0.7, kSampleRate);
        const double center = biquadRmsAtFreq(bp, 420.0);
        const double farLow = biquadRmsAtFreq(bp, 60.0);
        const double farHigh = biquadRmsAtFreq(bp, 6000.0);
        expectTrue("bandpass passes its own center frequency most strongly", center > farLow && center > farHigh);
        expectTrue("bandpass rejects a far-off low frequency", farLow < center * 0.3);
        expectTrue("bandpass rejects a far-off high frequency", farHigh < center * 0.3);
    }

    // Oscillator: sawtooth stays in [-1,1] and covers the full range each
    // period; square alternates between exactly -1 and +1 at 50% duty.
    {
        Oscillator saw(Waveform::Sawtooth);
        double mn = 1e9, mx = -1e9;
        for (int i = 0; i < 1000; ++i) {
            const double s = saw.process(100.0, kSampleRate);
            mn = std::min(mn, s);
            mx = std::max(mx, s);
            expectTrue("sawtooth sample within [-1,1]", s >= -1.0 - 1e-9 && s <= 1.0 + 1e-9);
        }
        expectTrue("sawtooth covers close to its full [-1,1] range over many periods", mx - mn > 1.9);
    }
    {
        Oscillator sq(Waveform::Square);
        bool sawPositive = false, sawNegative = false;
        for (int i = 0; i < 1000; ++i) {
            const double s = sq.process(100.0, kSampleRate);
            expectTrue("square sample is exactly +1 or -1", s == 1.0 || s == -1.0);
            if (s > 0) sawPositive = true;
            if (s < 0) sawNegative = true;
        }
        expectTrue("square wave visits both +1 and -1", sawPositive && sawNegative);
    }

    // NoiseBuffer: generated samples stay in range, looping wraps correctly,
    // and interpolated reads fall between their two neighboring samples.
    {
        Mulberry32 rng(777);
        NoiseBuffer nb;
        nb.generate(0.1, kSampleRate, rng);
        expectTrue("noise buffer has the expected sample count", nb.size() == (size_t)std::lround(0.1 * kSampleRate));
        bool allInRange = true;
        double sum = 0.0;
        for (size_t i = 0; i < nb.size(); ++i) {
            const double v = nb.sampleAt((double)i);
            if (v < -1.0 || v > 1.0) allInRange = false;
            sum += v;
        }
        expectTrue("all generated noise samples are within [-1,1]", allInRange);
        expectTrue("white noise mean is close to zero over enough samples", std::fabs(sum / nb.size()) < 0.05);

        // Looping: reading at size() should equal reading at 0 (wraps).
        expectNear("noise buffer wraps at its own length", nb.sampleAt((double)nb.size()), nb.sampleAt(0.0), 1e-9);

        // Interpolated read at a half-sample offset falls between its two
        // neighbors.
        const double a = nb.sampleAt(5.0), b = nb.sampleAt(6.0);
        const double mid = nb.sampleAt(5.5);
        const double lo = std::min(a, b), hi = std::max(a, b);
        expectTrue("interpolated read falls between its two neighboring samples", mid >= lo - 1e-9 && mid <= hi + 1e-9);
    }

    // ParamSmoother: matches AudioParam.setTargetAtTime()'s own exponential
    // convergence -- after exactly one time constant, the value should have
    // moved (1 - 1/e) ~= 63.2% of the way from its start to the target.
    {
        ParamSmoother sm;
        const double tau = 0.05;
        sm.init(kSampleRate, tau);
        sm.setImmediate(0.0);
        sm.setTarget(1.0);
        const int samplesPerTau = (int)std::lround(tau * kSampleRate);
        for (int i = 0; i < samplesPerTau; ++i) sm.next();
        expectNear("ParamSmoother reaches ~63% of target after one time constant", sm.value(), 1.0 - std::exp(-1.0),
                   0.01);
        for (int i = 0; i < samplesPerTau * 20; ++i) sm.next();
        expectNear("ParamSmoother converges close to target after many time constants", sm.value(), 1.0, 1e-6);
    }

    if (g_failures == 0) {
        std::printf("dsp_test: biquad response, oscillator shape, noise stats, and smoother convergence all match "
                     "expectations.\n");
        return 0;
    }
    std::fprintf(stderr, "dsp_test: %d MISMATCHES.\n", g_failures);
    return 1;
}
