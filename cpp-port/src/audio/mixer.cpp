#include "mixer.h"

#include "../render/gear_rpm.h" // see gear_rpm.h's own comment on this cross-folder include

#include <algorithm>
#include <cmath>

namespace {
constexpr double kNoiseBufferSeconds = 0.5; // NZBUF (index.html:1304)
} // namespace

int Mixer::pickOneShotSlot() {
    for (int i = 0; i < kOneShotSlots; ++i) {
        if (!oneShots_[i].active) return i;
    }
    int best = 0;
    double bestFrac = -1.0;
    for (int i = 0; i < kOneShotSlots; ++i) {
        const double frac = oneShots_[i].dur > 0 ? oneShots_[i].t / oneShots_[i].dur : 1.0;
        if (frac > bestFrac) {
            bestFrac = frac;
            best = i;
        }
    }
    return best;
}

void Mixer::init(double sampleRate, Mulberry32& noiseRng) {
    sampleRate_ = sampleRate;
    noise_.generate(kNoiseBufferSeconds, sampleRate_, noiseRng);

    engFilter_.setLowpass(900.0, 1.0, sampleRate_);
    engNoiseFilter_.setBandpass(420.0, 0.7, sampleRate_);
    skidFilter_.setHighpass(1800.0, 1.0, sampleRate_);
    draftFilter_.setBandpass(1200.0, 0.5, sampleRate_);
    crowdFilter_.setBandpass(850.0, 0.4, sampleRate_);
    for (auto& v : pack_) v.filter.setLowpass(650.0, 1.0, sampleRate_);

    engFreqA_.init(sampleRate_, 0.03);
    engFreqB_.init(sampleRate_, 0.03);
    engFreqC_.init(sampleRate_, 0.03);
    engGain_.init(sampleRate_, 0.08);
    engNoiseGain_.init(sampleRate_, 0.1);
    skidGain_.init(sampleRate_, 0.05);
    draftGain_.init(sampleRate_, 0.15);
    crowdGain_.init(sampleRate_, 0.4);
    busMaster_.init(sampleRate_, 0.05);
    for (auto& v : pack_) {
        v.freq.init(sampleRate_, 0.05);
        v.gain.init(sampleRate_, 0.08);
        v.pan.init(sampleRate_, 0.1);
    }
}

void Mixer::triggerLowpassOneShot(double freqHz, double q, double gain, double dur) {
    OneShot& os = oneShots_[pickOneShotSlot()];
    os.active = true;
    os.pos = 0.0;
    os.t = 0.0;
    os.dur = dur;
    os.gainStart = gain;
    os.filter.reset();
    os.filter.setLowpass(freqHz, q, sampleRate_);
}

void Mixer::triggerBandpassOneShot(double freqHz, double q, double gain, double dur) {
    OneShot& os = oneShots_[pickOneShotSlot()];
    os.active = true;
    os.pos = 0.0;
    os.t = 0.0;
    os.dur = dur;
    os.gainStart = gain;
    os.filter.reset();
    os.filter.setBandpass(freqHz, q, sampleRate_);
}

void Mixer::tick(const RaceState& state, const std::vector<Car>& cars, bool sound, double volume01) {
    const Car* player = nullptr;
    for (auto& c : cars) {
        if (c.isPlayer) {
            player = &c;
            break;
        }
    }
    if (!player) return;

    // index.html:1394-1397
    const double rpm = gearRpm(player->v).rpm;
    const double f = 55.0 + rpm * 165.0 + player->draftF * 8.0;
    engFreqA_.setTarget(f);
    engFreqB_.setTarget(f * 0.5);
    engFreqC_.setTarget(f * 1.013);

    // index.html:1398-1404
    const bool live = (state.mode == "race" || state.mode == "pace" || state.mode == "qual" ||
                        state.mode == "victory") &&
                       sound;
    busMaster_.setTarget(sound ? volume01 : 0.0);
    const double tgt = live ? (0.05 + 0.10 * (player->thr * 0.7 + rpm * 0.3)) : 0.0;
    engGain_.setTarget(tgt);
    engNoiseGain_.setTarget(live ? 0.25 * player->thr : 0.0);
    skid_ = std::max(0.0, skid_ - 0.08);
    skidGain_.setTarget(sound ? skid_ * 0.06 : 0.0);

    // index.html:1405-1421: N nearest opponents, closest-first.
    struct Near {
        const Car* car;
        double d;
    };
    std::vector<Near> nearest;
    if (live) {
        for (auto& o : cars) {
            if (&o == player) continue;
            const double d = std::hypot(o.x - player->x, o.y - player->y);
            if (d < 42.0) nearest.push_back({&o, d});
        }
        std::sort(nearest.begin(), nearest.end(), [](const Near& a, const Near& b) { return a.d < b.d; });
    }
    for (int i = 0; i < kPackVoices; ++i) {
        PackVoice& voice = pack_[i];
        if (i < static_cast<int>(nearest.size())) {
            const Car& o = *nearest[i].car;
            const double rpm2 = gearRpm(o.v).rpm;
            voice.freq.setTarget(52.0 + rpm2 * 160.0);
            voice.gain.setTarget((1.0 - nearest[i].d / 42.0) * 0.05);
            const double dx = o.x - player->x, dy = o.y - player->y;
            const double side = -std::sin(player->hdg) * dx + std::cos(player->hdg) * dy;
            voice.pan.setTarget(std::clamp(side / 6.0, -1.0, 1.0));
        } else {
            voice.gain.setTarget(0.0);
        }
    }

    // index.html:1422-1428: crowd swell + close-battle rise.
    double crowd = 0.004;
    if (state.greenT > 0) crowd = 0.030;
    else if (state.flag == "yellow") crowd = 0.014;
    else if (state.mode == "victory") crowd = 0.034;
    if (!nearest.empty() && nearest[0].d < 12.0) crowd = std::max(crowd, 0.020);
    crowdGain_.setTarget(sound && state.mode != "menu" ? crowd : 0.0);

    // index.html:1429-1430
    draftGain_.setTarget(live ? player->draftF * 0.035 * std::min(1.0, player->v / 50.0) : 0.0);

    // index.html:1431-1434: impact thumps + blowout bang (player-relative).
    // JS's playThump()/playBang()/spotterBlip() each independently check
    // `if(!AC || !S.sound) return;` (index.html:1350,1361,1372) -- the
    // `sound` check below matches that per-call gate (the edge-detection
    // itself isn't gated in JS, only the actual sound-producing call is).
    if (player->hitFx > lastHitFx_ + 0.15) {
        // playThump(sev) (index.html:1349-1358): lowpass 200Hz, gain
        // min(0.28, 0.10+sev*0.18), exponential decay to ~0.001 over 0.22s.
        if (sound) triggerLowpassOneShot(200.0, 1.0, std::min(0.28, 0.10 + player->hitFx * 0.18), 0.22);
    }
    lastHitFx_ = player->hitFx;
    if (player->blown && !lastBlown_) {
        // playBang() (index.html:1360-1369): bandpass 320Hz Q=0.8, gain 0.3,
        // decay over 0.35s.
        if (sound) triggerBandpassOneShot(320.0, 0.8, 0.3, 0.35);
    }
    lastBlown_ = player->blown;

    // Edge-detects a fresh spotterSay() call the same way JS's own
    // audioTick() edge-detects hitFx/blown above -- see mixer.h's own
    // comment on why this port can't call the blip synchronously from
    // race.cpp's spotterSay() helper.
    if (state.spotT > lastSpotT_) {
        // spotterBlip() (index.html:1371-1380): bandpass 1450Hz Q=2.2, gain
        // 0.06, decay over 0.09s.
        if (sound) triggerBandpassOneShot(1450.0, 2.2, 0.06, 0.09);
    }
    lastSpotT_ = state.spotT;
}

void Mixer::renderStereo(float* out, int numFrames) {
    const size_t noiseLen = noise_.size();
    for (int i = 0; i < numFrames; ++i) {
        // ---- engine ----
        const double sawA = engOscA_.process(engFreqA_.next(), sampleRate_);
        const double sqB = engOscB_.process(engFreqB_.next(), sampleRate_);
        const double sawC = engOscC_.process(engFreqC_.next(), sampleRate_);
        const double engMix = sawA + 0.35 * sqB + 0.55 * sawC; // gB=0.35, gC=0.55 (index.html:1293-1294)
        const double engFiltered = engFilter_.process(engMix);
        const double engNoiseSample = noise_.sampleAt(engNoisePos_);
        engNoisePos_ += 1.0;
        if (noiseLen > 0 && engNoisePos_ >= static_cast<double>(noiseLen)) engNoisePos_ -= static_cast<double>(noiseLen);
        const double engNoiseFiltered = engNoiseFilter_.process(engNoiseSample);
        const double engineOut = (engFiltered + engNoiseFiltered * engNoiseGain_.next()) * engGain_.next();

        // ---- skid ----
        const double skidSample = noise_.sampleAt(skidPos_);
        skidPos_ += 1.0;
        if (noiseLen > 0 && skidPos_ >= static_cast<double>(noiseLen)) skidPos_ -= static_cast<double>(noiseLen);
        const double skidOut = skidFilter_.process(skidSample) * skidGain_.next();

        // ---- draft wind ----
        const double draftSample = noise_.sampleAt(draftPos_);
        draftPos_ += 1.0;
        if (noiseLen > 0 && draftPos_ >= static_cast<double>(noiseLen)) draftPos_ -= static_cast<double>(noiseLen);
        const double draftOut = draftFilter_.process(draftSample) * draftGain_.next();

        // ---- crowd bed (playbackRate=0.35, index.html:1341) ----
        const double crowdSample = noise_.sampleAt(crowdPos_);
        crowdPos_ += 0.35;
        if (noiseLen > 0 && crowdPos_ >= static_cast<double>(noiseLen)) crowdPos_ -= static_cast<double>(noiseLen);
        const double crowdOut = crowdFilter_.process(crowdSample) * crowdGain_.next();

        // ---- opponent pack voices (equal-power pan) ----
        double oppL = 0.0, oppR = 0.0;
        for (auto& v : pack_) {
            const double osc = v.osc.process(v.freq.next(), sampleRate_);
            const double filtered = v.filter.process(osc);
            const double g = v.gain.next();
            const double pan = v.pan.next();
            // Approximate StereoPannerNode's equal-power law (not a byte-
            // exact port of its exact per-side formula, which differs
            // slightly for pan<0 vs pan>0 -- functionally equivalent
            // constant-power panning, logged as a legitimate simplification).
            const double angle = (pan + 1.0) * 0.25 * M_PI; // 0..pi/2
            oppL += filtered * g * std::cos(angle);
            oppR += filtered * g * std::sin(angle);
        }

        // ---- one-shots (thump/bang/spotter-blip) ----
        double oneShotOut = 0.0;
        for (auto& os : oneShots_) {
            if (!os.active) continue;
            const double sample = noise_.sampleAt(os.pos);
            os.pos += 1.0;
            if (noiseLen > 0 && os.pos >= static_cast<double>(noiseLen)) os.pos -= static_cast<double>(noiseLen);
            const double filtered = os.filter.process(sample);
            // Matches AudioParam.exponentialRampToValueAtTime(0.001, dur)'s
            // own curve: gainStart * (0.001/gainStart)^(t/dur).
            const double frac = os.t / os.dur;
            const double envelope = os.gainStart * std::pow(0.001 / os.gainStart, frac);
            oneShotOut += filtered * envelope;
            os.t += 1.0 / sampleRate_;
            if (os.t >= os.dur) os.active = false;
        }

        const double mono = engineOut + skidOut + draftOut + crowdOut + oneShotOut;
        const double master = busMaster_.next();
        const double left = std::clamp((mono + oppL) * master, -1.0, 1.0);
        const double right = std::clamp((mono + oppR) * master, -1.0, 1.0);
        out[2 * i] = static_cast<float>(left);
        out[2 * i + 1] = static_cast<float>(right);
    }
}
