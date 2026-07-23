#include "audio_engine.h"

#include <cstdio>

void AudioEngine::sdlCallback(void* userdata, Uint8* stream, int len) {
    AudioEngine* self = static_cast<AudioEngine*>(userdata);
    // len is bytes; format is AUDIO_F32SYS stereo, so 8 bytes/frame.
    self->mixer_.renderStereo(reinterpret_cast<float*>(stream), len / 8);
}

void AudioEngine::init() {
    // Deliberately not requested at the top-level SDL_Init() call in
    // main.cpp: SDL_Init(SDL_INIT_AUDIO) hard-fails the whole process if no
    // default audio driver exists at all (confirmed this session -- this
    // sandbox has no audio hardware without SDL_AUDIODRIVER=dummy set),
    // unlike SDL_OpenAudioDevice() below, which fails gracefully. Doing the
    // subsystem init here, with its own non-fatal fallback, means a
    // completely audio-less environment still runs the whole game fine
    // (silently), matching JS's own try/catch(e){AC=null} resilience.
    if (SDL_WasInit(SDL_INIT_AUDIO) == 0) {
        if (SDL_InitSubSystem(SDL_INIT_AUDIO) != 0) {
            std::fprintf(stderr, "AudioEngine::init: SDL_InitSubSystem(SDL_INIT_AUDIO) failed: %s\n", SDL_GetError());
            ok_ = false;
            return;
        }
        subsystemInited_ = true;
    }

    SDL_AudioSpec desired{};
    desired.freq = 44100;
    desired.format = AUDIO_F32SYS;
    desired.channels = 2;
    desired.samples = 1024;
    desired.callback = &AudioEngine::sdlCallback;
    desired.userdata = this;

    SDL_AudioSpec obtained{};
    // No SDL_AUDIO_ALLOW_*_CHANGE flags: SDL guarantees the callback always
    // receives exactly the requested format/frequency/channel count,
    // performing any conversion its backend needs internally -- simpler
    // and more portable across this port's native/WASM targets than this
    // code branching on whatever the device actually negotiated.
    dev_ = SDL_OpenAudioDevice(nullptr, 0, &desired, &obtained, 0);
    if (dev_ == 0) {
        std::fprintf(stderr, "AudioEngine::init: SDL_OpenAudioDevice failed: %s\n", SDL_GetError());
        if (subsystemInited_) {
            SDL_QuitSubSystem(SDL_INIT_AUDIO);
            subsystemInited_ = false;
        }
        ok_ = false;
        return;
    }

    static Mulberry32 noiseRng(0xA0D10u); // cosmetic-only noise seed, see dsp.h's NoiseBuffer comment
    mixer_.init(static_cast<double>(obtained.freq), noiseRng);
    SDL_PauseAudioDevice(dev_, 0); // start playback
    ok_ = true;
}

void AudioEngine::shutdown() {
    if (dev_ != 0) {
        SDL_CloseAudioDevice(dev_);
        dev_ = 0;
    }
    if (subsystemInited_) {
        SDL_QuitSubSystem(SDL_INIT_AUDIO);
        subsystemInited_ = false;
    }
    ok_ = false;
}

void AudioEngine::tick(const RaceState& state, const std::vector<Car>& cars, bool sound, double volume01) {
    if (!ok_) return;
    // Mixer::tick() only writes ParamSmoother targets/scalar state, all of
    // which renderStereo() reads from the callback thread -- lock/unlock
    // around the write, SDL2's own documented pattern for safely mutating
    // audio-callback-shared state from the main thread.
    SDL_LockAudioDevice(dev_);
    mixer_.tick(state, cars, sound, volume01);
    SDL_UnlockAudioDevice(dev_);
}
