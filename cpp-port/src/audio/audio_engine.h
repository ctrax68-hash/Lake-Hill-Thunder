#pragma once

#include "mixer.h"
#include "../sim/car.h"
#include "../sim/race_state.h"

#include <SDL.h>

#include <vector>

// Phase 6c (PORT_PROGRESS.md): the only SDL2-touching piece of the audio
// port -- everything else (dsp.h, mixer.h) is pure math. Owns one SDL2
// audio device and the Mixer that fills it.
//
// Unlike JS's own audioInit() (index.html:1284: `if(AC || !S.sound) return;`
// -- lazily creates the AudioContext only once sound is first needed, and
// never if it starts muted), this engine always opens the device at
// startup regardless of the initial sound/volume selection: mute is just
// the Mixer's busMaster gain smoothing to 0 from then on (see mixer.cpp's
// tick()). This is a deliberate simplification -- it avoids needing to
// lazily open/close an SDL audio device in response to a live menu
// toggle, at the cost of a session with sound off from the very start
// still spinning up (silently) an audio device and callback thread.
class AudioEngine {
public:
    // Opens the default SDL2 audio device (stereo float, whatever
    // frequency/buffer size the platform actually gives back -- see the
    // .cpp for why format/rate aren't forced). Leaves this engine inert
    // (every other method becomes a no-op) if no audio device is available
    // at all, mirroring JS's own try/catch(e){AC=null} fallback -- SDL2
    // itself reports this rather than throwing, so this is checked instead
    // of caught.
    void init();
    void shutdown();

    // Call once per rendered frame (mirrors audioTick(), index.html:1393 --
    // called once per requestAnimationFrame, NOT once per physics tick).
    // `sound`/`volume01` are MenuSelection::sound/(volume/100.0).
    void tick(const RaceState& state, const std::vector<Car>& cars, bool sound, double volume01);

private:
    bool ok_ = false;
    bool subsystemInited_ = false; // did this instance call SDL_InitSubSystem(SDL_INIT_AUDIO)?
    SDL_AudioDeviceID dev_ = 0;
    Mixer mixer_;

    static void sdlCallback(void* userdata, Uint8* stream, int len);
};
