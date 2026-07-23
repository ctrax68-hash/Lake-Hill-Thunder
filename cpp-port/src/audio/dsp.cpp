#include "dsp.h"

#include <cmath>

namespace {
constexpr double kTwoPi = 6.283185307179586;
}

void Biquad::setLowpass(double freqHz, double q, double sampleRate) {
    const double w0 = kTwoPi * freqHz / sampleRate;
    const double cosw0 = std::cos(w0);
    const double alpha = std::sin(w0) / (2.0 * q);
    const double a0 = 1.0 + alpha;
    b0_ = ((1.0 - cosw0) / 2.0) / a0;
    b1_ = (1.0 - cosw0) / a0;
    b2_ = ((1.0 - cosw0) / 2.0) / a0;
    a1_ = (-2.0 * cosw0) / a0;
    a2_ = (1.0 - alpha) / a0;
}

void Biquad::setHighpass(double freqHz, double q, double sampleRate) {
    const double w0 = kTwoPi * freqHz / sampleRate;
    const double cosw0 = std::cos(w0);
    const double alpha = std::sin(w0) / (2.0 * q);
    const double a0 = 1.0 + alpha;
    b0_ = ((1.0 + cosw0) / 2.0) / a0;
    b1_ = (-(1.0 + cosw0)) / a0;
    b2_ = ((1.0 + cosw0) / 2.0) / a0;
    a1_ = (-2.0 * cosw0) / a0;
    a2_ = (1.0 - alpha) / a0;
}

void Biquad::setBandpass(double freqHz, double q, double sampleRate) {
    // Constant 0dB peak-gain variant -- what the Web Audio spec mandates for
    // BiquadFilterNode's 'bandpass' type (see this file's own header comment).
    const double w0 = kTwoPi * freqHz / sampleRate;
    const double cosw0 = std::cos(w0);
    const double alpha = std::sin(w0) / (2.0 * q);
    const double a0 = 1.0 + alpha;
    b0_ = alpha / a0;
    b1_ = 0.0;
    b2_ = (-alpha) / a0;
    a1_ = (-2.0 * cosw0) / a0;
    a2_ = (1.0 - alpha) / a0;
}

double Biquad::process(double x) {
    const double y = b0_ * x + b1_ * x1_ + b2_ * x2_ - a1_ * y1_ - a2_ * y2_;
    x2_ = x1_;
    x1_ = x;
    y2_ = y1_;
    y1_ = y;
    return y;
}

void Biquad::reset() {
    x1_ = x2_ = y1_ = y2_ = 0.0;
}

double Oscillator::process(double freqHz, double sampleRate) {
    double sample;
    switch (type_) {
        case Waveform::Sawtooth:
            sample = 2.0 * phase_ - 1.0;
            break;
        case Waveform::Square:
        default:
            sample = phase_ < 0.5 ? 1.0 : -1.0;
            break;
    }
    phase_ += freqHz / sampleRate;
    phase_ -= std::floor(phase_);
    return sample;
}

void NoiseBuffer::generate(double durationSec, double sampleRate, Mulberry32& rng) {
    const size_t n = (size_t)std::lround(durationSec * sampleRate);
    samples_.resize(n);
    for (size_t i = 0; i < n; ++i) {
        samples_[i] = (float)(rng.next() * 2.0 - 1.0);
    }
}

double NoiseBuffer::sampleAt(double positionInSamples) const {
    const size_t n = samples_.size();
    if (n == 0) return 0.0;
    double p = std::fmod(positionInSamples, (double)n);
    if (p < 0) p += (double)n;
    const size_t i0 = (size_t)p;
    const size_t i1 = (i0 + 1) % n;
    const double frac = p - (double)i0;
    return samples_[i0] * (1.0 - frac) + samples_[i1] * frac;
}

void ParamSmoother::init(double sampleRate, double timeConstantSeconds) {
    coeff_ = timeConstantSeconds > 0.0 ? (1.0 - std::exp(-1.0 / (timeConstantSeconds * sampleRate))) : 1.0;
}
