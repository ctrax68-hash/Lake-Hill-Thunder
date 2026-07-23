#pragma once

#include "../sim/rng.h"

#include <cstdint>
#include <vector>

// Phase 6a (PORT_PROGRESS.md): pure-math DSP primitives standing in for the
// Web Audio nodes JS's audio section builds directly (index.html:1285-1461:
// AudioContext.createBiquadFilter()/createOscillator()/createBufferSource()/
// createGain()). SDL2 gives raw sample buffers to fill, not a node graph, so
// this port re-implements the handful of DSP building blocks Web Audio would
// otherwise provide, rather than the whole graph -- each one is a direct,
// well-known formula (RBJ Audio EQ Cookbook biquads, a phase-accumulator
// oscillator, a fixed white-noise buffer, an exponential parameter smoother
// matching AudioParam.setTargetAtTime() exactly), not an approximation.
// Deliberately bgfx/SDL2-free, same "pure logic isolated from the platform
// layer" split this project already uses for render/sim (track_surface.h,
// stadium_mesh.h, etc.) -- lets these get real, fast unit tests
// (tests/dsp_test.cpp) instead of only being verifiable by ear.

// Biquad (RBJ Audio EQ Cookbook, https://www.w3.org/TR/audio-eq-cookbook/):
// lowpass/highpass/bandpass, each a direct transcription of the cookbook's
// own formulas -- the same ones a browser's BiquadFilterNode implements
// (bandpass here is the cookbook's "constant 0dB peak gain" variant, which
// is what the Web Audio spec itself mandates for BiquadFilterNode's
// 'bandpass' type). Direct Form I: y[n] = b0*x[n]+b1*x[n-1]+b2*x[n-2]
// - a1*y[n-1] - a2*y[n-2], with the b/a coefficients pre-normalized so a0
// is implicitly 1.
class Biquad {
public:
    void setLowpass(double freqHz, double q, double sampleRate);
    void setHighpass(double freqHz, double q, double sampleRate);
    void setBandpass(double freqHz, double q, double sampleRate);

    double process(double x);
    void reset();

private:
    double b0_ = 1, b1_ = 0, b2_ = 0, a1_ = 0, a2_ = 0;
    double x1_ = 0, x2_ = 0, y1_ = 0, y2_ = 0;
};

// A phase-accumulator oscillator producing the two naive (non-band-limited)
// waveforms JS's engine/opponent-voice oscillators use (`oscA.type =
// 'sawtooth'`, `oscB.type = 'square'`, index.html:1296-1297). Naive, not
// alias-free additive synthesis (unlike a real browser's OscillatorNode) --
// a deliberate simplification: at this game's audio pitch ranges (engine
// drone tops out around a few hundred Hz) the extra harmonics an unfiltered
// naive oscillator adds land inside this project's own lowpass-filtered
// engine tone anyway, so the audible difference is negligible against the
// cost of a proper band-limited (e.g. PolyBLEP) implementation.
enum class Waveform { Sawtooth, Square };

class Oscillator {
public:
    explicit Oscillator(Waveform type) : type_(type) {}

    // Advances the internal phase by one sample at `freqHz`/`sampleRate`
    // and returns the new sample, in [-1, 1].
    double process(double freqHz, double sampleRate);

    void reset() { phase_ = 0.0; }

private:
    Waveform type_;
    double phase_ = 0.0; // 0..1, wraps
};

// A fixed-length white-noise buffer standing in for JS's `NZBUF`
// (index.html:1304-1305: `AC.createBuffer(1, AC.sampleRate*0.5, ...)`,
// filled with `Math.random()*2-1`), reused across every noise-driven voice
// (engine intake noise, skid, draft wind, crowd bed, thump/bang/spotter-blip
// one-shots) exactly like JS reuses one NZBUF everywhere. Seeded via the
// caller's own Mulberry32 rather than a true RNG -- cosmetic-only content,
// same "safe to diverge" precedent this project already established for
// scenery randomness (sky clouds, crowd-tile fill, stand density) -- not a
// determinism risk since audio never feeds back into sim state.
class NoiseBuffer {
public:
    // Fills `durationSec` seconds of white noise in [-1, 1] at `sampleRate`.
    void generate(double durationSec, double sampleRate, Mulberry32& rng);

    size_t size() const { return samples_.size(); }

    // Linear-interpolated read at a fractional sample position, wrapping
    // (modulo `size()`) so a looping playback head never needs its own
    // bounds-checking -- matches JS's `AudioBufferSourceNode.loop = true`.
    double sampleAt(double positionInSamples) const;

private:
    std::vector<float> samples_;
};

// Exponential parameter smoother, matching Web Audio's
// `AudioParam.setTargetAtTime(target, startTime, timeConstant)` exactly:
// discretized at the sample rate, `value(t+dt) = value(t) + (target -
// value(t)) * (1 - exp(-dt/timeConstant))`. JS's whole audio section drives
// every live parameter (oscillator frequency, gain, filter cutoff isn't
// automated here but gain/frequency are) through this exact primitive
// rather than snapping instantly, which is what keeps engine pitch/volume
// changes from zippering -- reused identically here.
class ParamSmoother {
public:
    // `timeConstantSeconds` matches JS's own per-parameter constants (e.g.
    // 0.03 for oscillator frequency, 0.08 for engine gain, 0.4 for the crowd
    // bed) -- see mixer.cpp's own call sites for which JS line each value
    // was transcribed from.
    void init(double sampleRate, double timeConstantSeconds);

    void setTarget(double target) { target_ = target; }
    // Snaps value and target together with no ramp -- used once at voice
    // creation (JS's own gain.gain.value = 0 initial literals), not during
    // normal operation.
    void setImmediate(double value) {
        value_ = value;
        target_ = value;
    }

    double next() {
        value_ += (target_ - value_) * coeff_;
        return value_;
    }
    double value() const { return value_; }

private:
    double coeff_ = 1.0;
    double value_ = 0.0;
    double target_ = 0.0;
};
