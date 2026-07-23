#pragma once

#include "dsp.h"
#include "../sim/car.h"
#include "../sim/race_state.h"
#include "../sim/rng.h"

#include <vector>

// Phase 6c (PORT_PROGRESS.md): the actual voice graph + audioTick()
// equivalent, built on Phase 6a's DSP primitives. Ports JS's whole audio
// section (index.html:1284-1461) as a graph of plain C++ objects instead
// of a Web Audio node graph -- every node JS creates once in audioInit()
// and leaves connected for the session becomes a member here instead
// (Biquad/Oscillator/ParamSmoother instances, or a read position into the
// one shared NoiseBuffer), and every `.connect()` edge becomes an explicit
// per-sample sum in renderStereo() -- fully bgfx/SDL2-free (only the
// audio_engine.h/.cpp layer above this one touches SDL2), so this stays
// unit-testable pure logic like the rest of this port's audio math.
//
// Two methods split JS's own two different "rates" apart cleanly:
//  - tick() runs once per sim frame (mirrors audioTick(), index.html:1393-
//    1461, called once per requestAnimationFrame) and only sets TARGET
//    values on this mixer's ParamSmoothers/oscillator frequencies from the
//    current RaceState/Car data -- it never touches a sample.
//  - renderStereo() runs from the real-time audio callback (audio_engine.cpp)
//    at the device's actual sample rate, synthesizing however many frames
//    the callback asks for by calling every ParamSmoother.next()/Biquad::
//    process()/Oscillator::process() once per output sample -- exactly
//    matching how a real AudioParam continues smoothing every sample
//    between JS's own once-per-frame setTargetAtTime() calls.
class Mixer {
public:
    // Generates the shared 0.5s noise buffer (JS's NZBUF, index.html:1304-
    // 1305) and sets every filter's fixed frequency/Q -- everything that
    // doesn't change after this point, matching JS's own audioInit() doing
    // all of this once up front.
    void init(double sampleRate, Mulberry32& noiseRng);

    // audioTick() (index.html:1393-1461). `sound`/`volume01` are
    // MenuSelection::sound/(volume/100.0) -- JS's S.sound/S.volume.
    void tick(const RaceState& state, const std::vector<Car>& cars, bool sound, double volume01);

    // Fills `numFrames` interleaved stereo float samples (L,R,L,R,...) into
    // `out` (must hold at least numFrames*2 floats). Called only from the
    // audio callback thread.
    void renderStereo(float* out, int numFrames);

private:
    double sampleRate_ = 44100.0;
    NoiseBuffer noise_;

    // ---- engine (player) voice (index.html:1289-1303, 1420-1428) ----
    Oscillator engOscA_{Waveform::Sawtooth};
    Oscillator engOscB_{Waveform::Square};
    Oscillator engOscC_{Waveform::Sawtooth};
    ParamSmoother engFreqA_, engFreqB_, engFreqC_;
    Biquad engFilter_; // lowpass 900Hz Q=1 (index.html:1299)
    double engNoisePos_ = 0.0;
    Biquad engNoiseFilter_; // bandpass 420Hz Q=0.7 (index.html:1307)
    ParamSmoother engNoiseGain_;
    ParamSmoother engGain_;

    // ---- skid (index.html:1313-1319, 1436-1437) ----
    double skidPos_ = 0.0;
    Biquad skidFilter_; // highpass 1800Hz Q=1
    ParamSmoother skidGain_;
    double skid_ = 0.0; // JS's own module-level `skid` decay accumulator

    // ---- drafting wind (index.html:1331-1336, 1458-1459) ----
    double draftPos_ = 0.0;
    Biquad draftFilter_; // bandpass 1200Hz Q=0.5
    ParamSmoother draftGain_;

    // ---- crowd bed (index.html:1338-1344, 1450-1454) ----
    double crowdPos_ = 0.0;
    Biquad crowdFilter_; // bandpass 850Hz Q=0.4
    ParamSmoother crowdGain_;

    // ---- opponent pack voices (index.html:1320-1330, 1439-1449) ----
    struct PackVoice {
        Oscillator osc{Waveform::Sawtooth};
        Biquad filter; // lowpass 650Hz Q=1, one independent instance per voice
        ParamSmoother freq, gain, pan;
    };
    static constexpr int kPackVoices = 3; // JS's own PACK_VOICES
    PackVoice pack_[kPackVoices];

    ParamSmoother busMaster_;

    // ---- one-shots: thump/bang/spotter-blip (index.html:1347-1387) ----
    // JS creates a brand-new AudioBufferSourceNode/filter/gain per call, so
    // concurrent one-shots are unbounded there; a small fixed pool is a
    // deliberate, documented simplification -- real overlap beyond this
    // many simultaneous impacts/blips is not a scenario this game produces.
    struct OneShot {
        bool active = false;
        double pos = 0.0;
        Biquad filter;
        double t = 0.0;
        double dur = 0.0;
        double gainStart = 0.0;
    };
    static constexpr int kOneShotSlots = 8;
    OneShot oneShots_[kOneShotSlots];
    // Finds a free one-shot slot, or (if every slot is busy -- not expected
    // in normal play) reclaims whichever slot is closest to finishing, so a
    // rare overlap steals the least-noticeable voice rather than a fixed index.
    int pickOneShotSlot();
    void triggerLowpassOneShot(double freqHz, double q, double gain, double dur);
    void triggerBandpassOneShot(double freqHz, double q, double gain, double dur);

    // Edge-detection state (index.html:1349,1459-1461): audioTick() doesn't
    // fire a thump/bang/blip directly at the accumulation site -- it
    // compares against the last-seen value every tick and fires on a
    // rising edge, so this mixer mirrors that with its own "last seen"
    // members rather than needing a callback hook into race.cpp's tick().
    double lastHitFx_ = 0.0;
    bool lastBlown_ = false;
    double lastSpotT_ = 0.0; // spotterSay() resets state.spotT to 2.2; a rise
                              // above whatever it last was is a fresh message
};
