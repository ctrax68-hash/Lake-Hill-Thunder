// Lake Hill Thunder C++ port -- Phase 2 minimal desktop renderer, extended
// in Phase 7 with a WebAssembly/browser build target (see PORT_PROGRESS.md).
// Opens an SDL2 window (native: real X11 window; web: the page's own
// <canvas>, see web/shell.html), drives a real race through the verified
// Phase 1 sim (gridStart()/tick()), and renders it with src/render/Renderer:
// a flat-shaded track ribbon + car boxes from a static top-down camera
// (press C to toggle a placeholder chase-camera view). Keyboard, touch/mouse
// (src/ui/touch_controls.h), and tilt (src/platform/tilt_input.h, toggled
// with T -- silently unavailable on web, SDL2 has no Emscripten sensor
// backend) all drive the player car; AI cars run the real stepCar() AI
// branch. Portrait windows show a blocked/black frame instead
// (src/ui/orientation.h), standing in for the CSS rotate prompt.

#include <SDL.h>

#ifdef __EMSCRIPTEN__
#include <emscripten.h>
#else
#include <SDL_syswm.h>
#endif

#include "../audio/audio_engine.h"
#include "../render/hud.h"
#include "../render/renderer.h"
#include "../sim/car.h"
#include "../sim/race.h"
#include "../sim/rng.h"
#include "../sim/tracks_data.h"
#include "../ui/menu.h"
#include "../ui/orientation.h"
#include "../ui/results.h"
#include "../ui/touch_controls.h"
#include "tilt_input.h"

#include <cstdio>
#include <cstdlib>
#include <vector>

namespace {

// Everything the per-frame tick touches, bundled into one struct so it can
// be handed to Emscripten's callback-based main loop as a single opaque
// pointer. `emscripten_set_main_loop_arg(..., simulate_infinite_loop=1)`
// unwinds main()'s own stack immediately (via an internal exception) right
// after registering the callback -- see main()'s own comment -- so nothing
// the loop touches can live as a plain stack local in main() the way the
// native build's did before this phase existed. The native build reads/
// writes these exact same fields via `S.` now too -- behavior is unchanged,
// only where the variables live changed, so both targets share one code
// path end to end instead of two parallel loop implementations.
struct LoopState {
    SDL_Window* window = nullptr;
    Renderer renderer;
    AudioEngine audio; // Phase 6c (PORT_PROGRESS.md): audioTick() equivalent
    Track track;
    Mulberry32 rng;
    Mulberry32 rngR;
    RaceState state;
    PaceCar pace;
    std::vector<Car> cars;
    PlayerInput input;
    std::vector<Car*> finishOrder;
    TiltInput tiltInput;
    TouchRegions touchRegions;
    MenuSelection menu; // Phase 4b (PORT_PROGRESS.md): live menu selections
    bool touchLeft = false, touchRight = false, touchGas = false, touchBrake = false;
    bool portrait = false;
    bool running = true;
    bool chaseCam = false;
    int frame = 0;
    double simAcc = 0.0;
    Uint64 perfFreq = 0;
    Uint64 last = 0;
    int width = 1280, height = 720;
    // Native default stays 600 (unchanged from before this phase) so
    // existing headless verification behavior isn't disturbed. A real
    // browser session can't set LHT_MAX_FRAMES at all (no shell
    // environment reaches `std::getenv()` in a page loaded over HTTP), so
    // the web build needs a default that means "run for as long as the
    // tab is open," not "stop after ~10 seconds" -- this default IS the
    // real shipped behavior on web, not a debug convenience there.
#ifdef __EMSCRIPTEN__
    int maxFrames = 2000000000;
#else
    int maxFrames = 600;
#endif
    const char* screenshotPath = nullptr;
    int screenshotAtFrame = 0;

    LoopState(int trackIdx, uint32_t rngSeed, uint32_t rngRSeed)
        : track(TRACKS[trackIdx]), rng(rngSeed), rngR(rngRSeed) {
        // Keep the menu's own trackIdx in sync with whatever this LoopState
        // was actually constructed with (argv[1]/default 0) -- otherwise
        // the menu's trackBtn would cycle relative to index 0 regardless of
        // which track is actually loaded/displayed at startup.
        menu.trackIdx = trackIdx;
    }
};

// Mirrors bP's JS click handler (index.html:4665-4669) exactly: an
// independent toggle on the player's own pitReq, guarded the same way, not
// threaded through PlayerInput/stepCar() at all -- the player has no other
// way to request a pit stop (tick()'s AI pit-strategy block deliberately
// skips the player, matching JS).
void togglePlayerPit(LoopState& S) {
    Car* player = nullptr;
    for (auto& c : S.cars) {
        if (c.isPlayer) { player = &c; break; }
    }
    if (player && S.state.mode == "race" && !player->done && player->pit == 0) {
        player->pitReq = !player->pitReq;
    }
}

// Mirrors JS's startRace() (index.html:4604-4614): gridStart() placement,
// plus the state resets startRace() makes explicitly alongside it. Most of
// these already match RaceState's freshly-constructed defaults (this port
// only ever runs gridStart() once per session so far, no menu-return/
// restart flow exists yet), but state.finishLaps = state.laps is the one
// that's no longer a coincidence now that the menu's laps row makes
// state.laps genuinely user-selectable (Phase 4b, PORT_PROGRESS.md) --
// written explicitly here, matching JS, rather than relying on both
// defaulting to 5.
void startRaceFromMenu(LoopState& S) {
    gridStart(S.track, S.rng, S.state, S.pace, S.cars, S.finishOrder, nullptr);
    S.state.oneToGo = false;
    S.state.pitsOpen = true;
    S.state.cautionMaxSlot = -1;
    S.state.finishLaps = S.state.laps;
    S.state.gwcState = "none";
    S.state.gwcAttempts = 0;
    S.state.gwcMarkLap = -1;
    S.state.spotT = 0;
    S.state.spotTxt.clear();
    S.state.spotState = "clear";
    S.state.togoMsg = false;
    S.state.fuelMsg = false;
    S.state.tireMsg = false;
    S.state.dmgMsg = false;
    S.state.mode = "pace";
    // The accumulator may hold an arbitrarily large backlog of real elapsed
    // time from however long the player sat on the menu (dt keeps
    // accumulating every frame regardless of mode, below) -- without this
    // reset, tick()'s `while (simAcc >= DT)` loop would try to instantly
    // replay that entire idle span the moment Start is pressed. JS has no
    // equivalent risk: its `acc` only accumulates inside the same
    // mode-gated block that calls tick() (index.html:4144-4166), so it
    // never builds up backlog while sitting in the menu in the first place.
    S.simAcc = 0.0;
}

// Debug-only: seeds a plausible post-race field state (a few finishers,
// one DNF, the rest still carrying a best lap) and jumps straight to
// mode=="done" -- lets headless verification (native LHT_FORCE_DONE at
// startup, or the SDLK_k debug hotkey below at any time, including via a
// real Playwright browser click/keypress against the WASM build) reach the
// Phase 4h results screen without waiting out a real multi-lap race, whose
// finish can be delayed indefinitely by green-white-checkered extensions
// after a caution (race.cpp's own gwcState machine). Idempotent (clears
// finishOrder/done/out first) so repeated invocations don't accumulate
// stale entries. Same seeded-state-not-a-physics-bypass philosophy as
// LHT_FORCE_RACE.
void seedForceDoneState(LoopState& S) {
    S.finishOrder.clear();
    for (size_t i = 0; i < S.cars.size(); ++i) {
        Car& c = S.cars[i];
        c.done = false;
        c.out = false;
        if (i < 3) {
            c.done = true;
            c.finishT = (double)i * 2.0;
            c.bestLapT = 40.0 + (double)i * 1.5;
            S.finishOrder.push_back(&c);
        } else if (i == 3) {
            c.out = true;
        } else {
            c.bestLapT = 45.0;
        }
    }
    S.state.mode = "done";
}

// Menu row click/tap handling (Phase 4b, PORT_PROGRESS.md), matching
// index.html's #trkTog/#lapTog/#qualTog/#sndTog/#tiltTog/#volSlider/
// #startBtn handlers (index.html:4650-4723) -- see menu.h's own comments
// for what's a real toggle (track/laps/tilt) vs. stored-but-inert UI
// parity (qual/sound/volume) and why.
void handleMenuClick(LoopState& S, int x, int y) {
    const MenuRegions r = computeMenuRegions();
    if (pointInRect(x, y, r.trackBtn)) {
        S.menu.trackIdx = (S.menu.trackIdx + 1) % (int)TRACKS.size();
        S.track = Track(TRACKS[S.menu.trackIdx]);
        S.renderer.setTrack(S.track);
    } else if (pointInRect(x, y, r.lapsBtn)) {
        S.state.laps = cycleLaps(S.state.laps);
    } else if (pointInRect(x, y, r.qualBtn)) {
        S.menu.qual = !S.menu.qual;
    } else if (pointInRect(x, y, r.soundBtn)) {
        S.menu.sound = !S.menu.sound;
    } else if (pointInRect(x, y, r.tiltBtn)) {
        S.state.tilt = !S.state.tilt;
    } else if (pointInRect(x, y, r.volumeBar)) {
        S.menu.volume = volumeFromClickX(r.volumeBar, x);
    } else if (pointInRect(x, y, r.startBtn)) {
        if (S.menu.qual) {
            // Honest, one-shot note rather than silently ignoring the
            // selection -- see MenuSelection::qual's comment for why.
            std::printf("Menu: qualifying isn't implemented in this port yet -- "
                        "starting a normal race instead.\n");
        }
        startRaceFromMenu(S);
    }
}

// Results screen click/tap handling (Phase 4h, PORT_PROGRESS.md), matching
// index.html's #againBtn handler (index.html:4692-4696): the only
// interactive element on the results screen is "BACK TO MENU", which just
// flips mode back to "menu" -- startRaceFromMenu() (above) already resets
// everything else a second gridStart() needs, now that its own
// finishOrder-clearing bugfix (race.h/.cpp) is in place.
void handleResultsClick(LoopState& S, int x, int y) {
    const std::vector<const Car*> order = computeRaceOrder(S.cars);
    const std::vector<const Car*> resultsOrder = buildResultsOrder(S.finishOrder, order);
    const ResultsRegions r = computeResultsRegions((int)resultsOrder.size());
    if (pointInRect(x, y, r.backBtn)) {
        S.state.mode = "menu";
    }
}

void mainLoopTick(void* argPtr) {
    LoopState& S = *static_cast<LoopState*>(argPtr);

    // Tire-model-upgrade regression pass (PORT_PROGRESS.md): see this flag's
    // two use sites below (fixed-step dt, and skipping the render call
    // entirely) for why it exists. Debug/regression-measurement only.
    static const bool headlessFast = std::getenv("LHT_HEADLESS_FAST") != nullptr;

    SDL_Event ev;
    while (SDL_PollEvent(&ev)) {
        if (ev.type == SDL_QUIT) S.running = false;
        if (ev.type == SDL_KEYDOWN) {
            if (ev.key.keysym.sym == SDLK_ESCAPE) S.running = false;
            if (ev.key.keysym.sym == SDLK_c && !ev.key.repeat) {
                S.chaseCam = !S.chaseCam;
                S.renderer.setCameraMode(S.chaseCam ? Renderer::CameraMode::Chase
                                                     : Renderer::CameraMode::TopDown);
            }
            // Debug-only: JS has no keyboard binding for pit at all (bP is
            // touch/click-only) -- desktop-testing convenience, same
            // rationale as LHT_FORCE_RACE/LHT_START_CHASE.
            if (ev.key.keysym.sym == SDLK_p && !ev.key.repeat) togglePlayerPit(S);
            // Debug-only: the real tilt-steer checkbox now exists (Phase 4b's
            // menu tiltBtn, below) and toggles this exact same field --
            // this keyboard shortcut just remains a desktop-testing
            // convenience that also works mid-race, matching JS's own
            // #tiltTog binding having no mode restriction either
            // (index.html:4703-4705).
            if (ev.key.keysym.sym == SDLK_t && !ev.key.repeat) S.state.tilt = !S.state.tilt;
            // Debug-only: jumps straight to the Phase 4h results screen
            // (seedForceDoneState()'s own comment above explains why) --
            // unlike LHT_FORCE_DONE (an env var, read once at startup and
            // unreachable from a page loaded over HTTP), this is real
            // keyboard input, deliverable via Playwright against the WASM
            // build too, so it's what tests/wasm_verify.js uses to reach
            // and click through the results screen without waiting out a
            // real race. Starts a race first if the menu hasn't yet (so
            // `S.cars` is non-empty to seed).
            if (ev.key.keysym.sym == SDLK_k && !ev.key.repeat) {
                if (S.cars.empty()) startRaceFromMenu(S);
                seedForceDoneState(S);
            }
        }
        if (ev.type == SDL_WINDOWEVENT && ev.window.event == SDL_WINDOWEVENT_RESIZED) {
            S.width = ev.window.data1;
            S.height = ev.window.data2;
            S.renderer.resize(S.width, S.height);
            S.touchRegions = computeTouchRegions(S.width, S.height);
            S.portrait = isPortrait(S.width, S.height);
        }
        // Mouse (desktop stand-in) and real touch, hit-tested against the
        // same regions -- bL/bR/bG/bB are press-and-hold (index.html's
        // pointerdown/up), bP is a single toggle on press (index.html's
        // click), matching bindBtn()'s and bP's own listener exactly.
        // Ignored entirely while portrait, matching the `#rotate` overlay
        // physically covering the on-screen buttons in JS. While
        // mode=="menu" (Phase 4b, PORT_PROGRESS.md), clicks/taps go to the
        // menu rows instead -- the drive controls don't exist to hit yet
        // (no cars until Start is pressed), matching JS's own menu overlay
        // covering the driving HUD entirely.
        if (!S.portrait && ev.type == SDL_MOUSEBUTTONDOWN && ev.button.button == SDL_BUTTON_LEFT) {
            const int x = ev.button.x, y = ev.button.y;
            if (S.state.mode == "menu") {
                handleMenuClick(S, x, y);
            } else if (S.state.mode == "done") {
                handleResultsClick(S, x, y);
            } else {
                if (pointInRect(x, y, S.touchRegions.bL)) S.touchLeft = true;
                if (pointInRect(x, y, S.touchRegions.bR)) S.touchRight = true;
                if (pointInRect(x, y, S.touchRegions.bG)) S.touchGas = true;
                if (pointInRect(x, y, S.touchRegions.bB)) S.touchBrake = true;
                if (pointInRect(x, y, S.touchRegions.bP)) togglePlayerPit(S);
            }
        }
        if (ev.type == SDL_MOUSEBUTTONUP && ev.button.button == SDL_BUTTON_LEFT) {
            S.touchLeft = S.touchRight = S.touchGas = S.touchBrake = false;
        }
        if (!S.portrait && ev.type == SDL_FINGERDOWN) {
            const int x = (int)(ev.tfinger.x * S.width), y = (int)(ev.tfinger.y * S.height);
            if (S.state.mode == "menu") {
                handleMenuClick(S, x, y);
            } else if (S.state.mode == "done") {
                handleResultsClick(S, x, y);
            } else {
                if (pointInRect(x, y, S.touchRegions.bL)) S.touchLeft = true;
                if (pointInRect(x, y, S.touchRegions.bR)) S.touchRight = true;
                if (pointInRect(x, y, S.touchRegions.bG)) S.touchGas = true;
                if (pointInRect(x, y, S.touchRegions.bB)) S.touchBrake = true;
                if (pointInRect(x, y, S.touchRegions.bP)) togglePlayerPit(S);
            }
        }
        if (ev.type == SDL_FINGERUP) {
            S.touchLeft = S.touchRight = S.touchGas = S.touchBrake = false;
        }
    }

    S.tiltInput.update();
    if (S.tiltInput.available()) S.state.tiltG = S.tiltInput.tiltG();

    const Uint8* keys = SDL_GetKeyboardState(nullptr);
    S.input.gas = keys[SDL_SCANCODE_UP] || keys[SDL_SCANCODE_W] || S.touchGas;
    S.input.brake = keys[SDL_SCANCODE_DOWN] || keys[SDL_SCANCODE_S] || S.touchBrake;
    S.input.left = keys[SDL_SCANCODE_LEFT] || keys[SDL_SCANCODE_A] || S.touchLeft;
    S.input.right = keys[SDL_SCANCODE_RIGHT] || keys[SDL_SCANCODE_D] || S.touchRight;

    const Uint64 now = SDL_GetPerformanceCounter();
    double dt;
    if (headlessFast) {
        // Real play/normal headless runs tie simulation progress to actual
        // elapsed wall-clock time (the whole point of this accumulator).
        // With rendering skipped, real time per loop iteration collapses to
        // near-zero, so that same accumulator would never cross DT and the
        // sim would never advance at all (confirmed: a 20000-frame run
        // finished in 0.3s with state.t stuck at 0.0). A fixed-step virtual
        // clock is also the more correct thing for a reproducible regression
        // run anyway, not just a workaround.
        dt = DT;
    } else {
        dt = (double)(now - S.last) / (double)S.perfFreq;
    }
    S.last = now;
    if (dt > 0.25) dt = 0.25; // clamp a stall (e.g. window drag) instead of a physics-time jump
    S.simAcc += dt;
    // Matches JS's own frame() gate exactly (index.html:4144): tick() only
    // runs in race/pace/qual/victory, never in menu/menuwait/done. Menu mode
    // has no cars yet (gridStart() hasn't run), so this isn't just a JS
    // parity nicety -- it also avoids running collide()/the caution
    // controller over an empty field.
    const bool simRunning = S.state.mode == "race" || S.state.mode == "pace" ||
                             S.state.mode == "qual" || S.state.mode == "victory";
    if (simRunning) {
        while (S.simAcc >= DT) {
            tick(S.state, S.cars, S.pace, S.track, S.rngR, S.input, S.finishOrder);
            S.simAcc -= DT;
        }
    }

    // Phase 6c (PORT_PROGRESS.md): audioTick() equivalent -- once per
    // rendered frame (index.html:4171, inside frame(ts) after the physics
    // tick loop, NOT once per physics tick), regardless of simRunning (JS's
    // own audioTick() has its own `if(!AC||!S.player) return;` guard, and
    // its `live` gate already zeroes every target when not actually racing
    // -- see mixer.cpp's own tick()).
    S.audio.tick(S.state, S.cars, S.menu.sound, S.menu.volume / 100.0);

    // headlessFast (declared at the top of this function): skips the render
    // call entirely -- screenshot/portrait/menu-art paths are irrelevant to
    // a metrics-only run, and rendering (even off-screen via EGL/xvfb) turned
    // out to dominate wall-clock cost by a wide margin versus the sim itself
    // (a 20000-frame race-to-completion run didn't finish in 5+ minutes with
    // rendering on).
    if (!headlessFast) {
        if (S.portrait) {
            S.renderer.renderBlockedFrame();
        } else if (S.state.mode == "menu") {
            S.renderer.renderFrame(S.state, S.cars, &S.menu, &S.track.name());
        } else if (S.state.mode == "done") {
            S.renderer.renderFrame(S.state, S.cars, nullptr, nullptr, &S.finishOrder);
        } else {
            S.renderer.renderFrame(S.state, S.cars);
        }
    }
    ++S.frame;

    if (S.screenshotPath && S.frame == S.screenshotAtFrame) {
        S.renderer.requestScreenshot(S.screenshotPath);
    }
    if (S.frame % 250 == 0) {
        const Car* player = nullptr;
        for (auto& c : S.cars) {
            if (c.isPlayer) { player = &c; break; }
        }
        std::printf("frame=%d t=%.1f mode=%s flag=%s player.lap=%d player.v=%.1f\n",
                    S.frame, S.state.t, S.state.mode.c_str(), S.state.flag.c_str(),
                    player ? player->lap : -99, player ? player->v : -1.0);
    }
    // Tire-model-upgrade regression pass (PORT_PROGRESS.md): a one-shot
    // per-race summary -- wreck count, and each car's finish/lap-time/DNF
    // outcome -- printed once the race actually reaches "done" (or, if it
    // doesn't finish within LHT_MAX_FRAMES, right before the run ends
    // anyway). Debug/regression-measurement only, gated behind the same
    // LHT_FORCE_RACE headless-verification path every other LHT_* debug
    // hook in this file already uses; no effect on real play.
    if (std::getenv("LHT_FORCE_RACE")) {
        static bool summaryPrinted = false;
        const bool aboutToStop = S.frame + 1 >= S.maxFrames;
        if (!summaryPrinted && (S.state.mode == "done" || aboutToStop)) {
            summaryPrinted = true;
            std::printf("RACE_SUMMARY wreckCount=%d frame=%d t=%.1f mode=%s\n",
                        S.state.wreckCount, S.frame, S.state.t, S.state.mode.c_str());
            for (auto& c : S.cars) {
                std::printf("RACE_SUMMARY car=%d num=%d lap=%d bestLapT=%.3f lastLapT=%.3f "
                            "finishT=%.3f out=%d done=%d dmg=%.3f\n",
                            c.idx, c.num, c.lap, c.bestLapT, c.lastLapT, c.finishT,
                            c.out ? 1 : 0, c.done ? 1 : 0, c.dmg);
            }
        }
    }

#ifdef __EMSCRIPTEN__
    // Safety valve, not the normal exit path: a real page load has no
    // LHT_MAX_FRAMES to hit (env vars don't exist in a browser) and no
    // SDL_QUIT event route, so this practically never fires during real
    // play. It exists so a debug/verification session (e.g. pressing
    // Escape) can still cleanly stop and tear down without a crash.
    if (!S.running || S.frame >= S.maxFrames) {
        emscripten_cancel_main_loop();
        S.tiltInput.shutdown();
        S.audio.shutdown();
        S.renderer.shutdown();
        SDL_DestroyWindow(S.window);
        SDL_Quit();
    }
#endif
}

} // namespace

int main(int argc, char** argv)
{
    const int trackIdx = argc > 1 ? std::atoi(argv[1]) : 0;

    // Confirmed this session (PORT_PROGRESS.md's Phase 7 notes): SDL2 has
    // no Emscripten sensor backend at all (not even a dummy one), unlike
    // its video/audio/joystick backends which are all confirmed present --
    // so SDL_INIT_SENSOR is only requested on the native build, rather than
    // assuming SDL_Init() tolerates a subsystem flag with nothing behind it.
    // SDL_INIT_AUDIO is deliberately NOT requested here (Phase 6c,
    // PORT_PROGRESS.md): unlike SDL_OpenAudioDevice() failing gracefully
    // later, SDL_Init(SDL_INIT_AUDIO) itself hard-fails the whole process
    // if no default audio driver exists at all -- confirmed this session,
    // this exact sandbox has no audio hardware without SDL_AUDIODRIVER=
    // dummy set, so requesting it here would break every existing native
    // run that doesn't set that env var. AudioEngine::init() instead calls
    // SDL_InitSubSystem(SDL_INIT_AUDIO) itself and handles that failure
    // non-fatally, exactly like its own SDL_OpenAudioDevice fallback.
    Uint32 sdlInitFlags = SDL_INIT_VIDEO;
#ifndef __EMSCRIPTEN__
    sdlInitFlags |= SDL_INIT_SENSOR;
#endif
    if (SDL_Init(sdlInitFlags) != 0) {
        std::fprintf(stderr, "SDL_Init failed: %s\n", SDL_GetError());
        return 1;
    }

    // A function-local static, not a plain stack local: its address is
    // fixed for the rest of the program's lifetime from the moment this
    // line first runs, which both targets rely on -- Renderer::setTrack()
    // stores a raw `const Track*` internally (see renderer.h's own "not
    // owned, caller's Track must outlive this Renderer" comment), and the
    // Emscripten path unwinds main()'s own stack frame immediately below,
    // so a plain local would leave that pointer dangling right away.
    static LoopState S(trackIdx, 12345u, 999u);

    // Debug-only: lets a headless run start already-portrait to exercise
    // Phase 3c's rotate-block path, or override the window size generally
    // -- same rationale as LHT_FORCE_RACE/LHT_START_CHASE below. No-ops on
    // web (no environment to read), which is fine: a real page's size
    // comes from the canvas/viewport, not a launch-time override.
    if (const char* w = std::getenv("LHT_WINDOW_W")) S.width = std::atoi(w);
    if (const char* h = std::getenv("LHT_WINDOW_H")) S.height = std::atoi(h);

    S.window = SDL_CreateWindow(
        "Lake Hill Thunder (C++ port)",
        SDL_WINDOWPOS_UNDEFINED, SDL_WINDOWPOS_UNDEFINED,
        S.width, S.height,
        SDL_WINDOW_SHOWN | SDL_WINDOW_RESIZABLE
    );
    if (!S.window) {
        std::fprintf(stderr, "SDL_CreateWindow failed: %s\n", SDL_GetError());
        SDL_Quit();
        return 1;
    }

    void* ndt;
    void* nwh;
#ifdef __EMSCRIPTEN__
    // bgfx's HTML5/Emscripten GL backend (bgfx/src/glcontext_html5.cpp)
    // reads PlatformData::nwh back out as a `const char*` CSS selector
    // string, not a native window handle -- there is no OS-level window/
    // display handle concept in a browser at all. "#canvas" must match
    // web/shell.html's <canvas id="canvas">.
    ndt = nullptr;
    nwh = (void*)"#canvas";
#else
    SDL_SysWMinfo wmi;
    SDL_VERSION(&wmi.version);
    if (!SDL_GetWindowWMInfo(S.window, &wmi)) {
        std::fprintf(stderr, "SDL_GetWindowWMInfo failed: %s\n", SDL_GetError());
        SDL_DestroyWindow(S.window);
        SDL_Quit();
        return 1;
    }
    if (wmi.subsystem != SDL_SYSWM_X11) {
        // This SDL2 build only has the X11 video driver enabled (see
        // PORT_PROGRESS.md's Phase 0 notes) -- add a Wayland branch here if
        // that build option ever changes.
        std::fprintf(stderr, "Unsupported SDL_SYSWM subsystem: %d\n", (int)wmi.subsystem);
        SDL_DestroyWindow(S.window);
        SDL_Quit();
        return 1;
    }
    ndt = wmi.info.x11.display;
    nwh = reinterpret_cast<void*>(static_cast<uintptr_t>(wmi.info.x11.window));
#endif

    if (!S.renderer.init(ndt, nwh, S.width, S.height)) {
        std::fprintf(stderr, "Renderer::init failed\n");
        SDL_DestroyWindow(S.window);
        SDL_Quit();
        return 1;
    }

    // Phase 6c (PORT_PROGRESS.md): not fatal if this fails (no audio device
    // available) -- see AudioEngine::init()'s own comment for the fallback.
    S.audio.init();

    S.renderer.setTrack(S.track);
    S.renderer.setChaseTarget(0); // idx 0 is always the player, see car.h
    // Debug-only: force chase mode on at startup instead of waiting for a
    // live C keypress -- for scripted headless screenshot verification,
    // same rationale as LHT_FORCE_RACE below.
    if (std::getenv("LHT_START_CHASE")) S.renderer.setCameraMode(Renderer::CameraMode::Chase);

    // Phase 4b (PORT_PROGRESS.md): the real entry point is now the menu
    // screen -- RaceState's own default mode ("menu", race_state.h) is left
    // untouched here, and gridStart() only runs once Start is actually
    // pressed (mainLoopTick()'s handleMenuClick() -> startRaceFromMenu()).
    //
    // Debug-only: LHT_FORCE_RACE remains a scripted-verification bypass,
    // matching its pre-menu behavior exactly -- skip straight past both the
    // menu AND the ~28-sim-second pace phase (during which every car,
    // including the player, is formation-driven -- keyboard input is
    // ignored, matching the JS original), landing already in a running
    // green-flag race so headless screenshot verification doesn't need to
    // simulate a mouse click just to see gameplay. Same seeded-starting-
    // state philosophy as the --force flag used throughout this port's
    // determinism tests, not a physics bypass.
    if (std::getenv("LHT_FORCE_RACE")) {
        startRaceFromMenu(S);
        // Tire-model-upgrade regression pass: LHT_NATURAL_START skips only
        // the mode/flag override above, leaving startRaceFromMenu()'s normal
        // "pace" mode in place so the ~28-sim-second pace-lap-to-green-flag
        // transition runs for real -- used to verify the actually-played
        // race flow, as opposed to LHT_FORCE_RACE's own instant-bunched-
        // green-flag bypass (a much harsher, non-representative stress
        // condition on its own).
        if (!std::getenv("LHT_NATURAL_START")) {
            S.state.mode = "race";
            S.state.flag = "green";
        }
    }

    // Debug-only: see seedForceDoneState()'s own comment above.
    if (std::getenv("LHT_FORCE_DONE")) {
        startRaceFromMenu(S);
        seedForceDoneState(S);
    }

    // Debug-only: Phase 6b's spotter-message conditions are otherwise
    // timing/RNG-gated (laps-to-go, a nearby AI car, a tire blowout) --
    // this seeds state.spotTxt/spotT directly, the same "seeded state, not
    // a physics bypass" philosophy as LHT_FORCE_RACE/LHT_FORCE_DONE, purely
    // so headless screenshot verification can reliably show Phase 6d's HUD
    // caption without waiting on a real trigger to happen to fire.
    if (std::getenv("LHT_FORCE_SPOTTER")) {
        S.state.spotTxt = "INSIDE!";
        S.state.spotT = 2.2;
    }

    // Phase 3b (PORT_PROGRESS.md): tilt-steer via SDL_Sensor, feeding
    // state.tilt/state.tiltG the same way JS's `deviceorientation` listener
    // feeds S.tilt/S.tiltG (index.html:1260-1264) -- stepCar()'s player
    // branch (step_car.cpp:216) already reads these fields exactly as
    // ported since Phase 1, so no sim-core change is needed here, only
    // populating them from real hardware. init() gracefully reports
    // unavailable when no accelerometer exists -- true of this dev
    // container's native build, and unconditionally true of the web build
    // too (no Emscripten sensor backend at all, see the SDL_Init comment
    // above), so tilt-steer is a silent no-op there. Not a bug to fix.
    S.tiltInput.init();

    // Phase 3a (PORT_PROGRESS.md): touch/click input regions matching the JS
    // original's bL/bR/bB/bG/bP on-screen buttons (index.html:1235-1246,
    // 4664-4669). No visible button is drawn yet -- that's Phase 4's job --
    // this is input recognition only.
    S.touchRegions = computeTouchRegions(S.width, S.height);

    // Phase 3c (PORT_PROGRESS.md): stand-in for the CSS `@media (orientation:
    // portrait)` rotate prompt (index.html:140-147,203). The sim keeps
    // ticking regardless of orientation, same as JS -- the CSS overlay
    // never pauses the game loop, it just visually and (via its z-index
    // covering the whole viewport) physically blocks touches to the
    // controls underneath. Keyboard is deliberately left unaffected in
    // mainLoopTick(), matching JS.
    S.portrait = isPortrait(S.width, S.height);

    // Bounded by default (native) so this stays scriptable/verifiable in a
    // headless run too -- set LHT_MAX_FRAMES for an actual interactive
    // native session (e.g. a very large number). See LoopState's own
    // comment for why the web build's default differs.
    if (const char* mf = std::getenv("LHT_MAX_FRAMES")) S.maxFrames = std::atoi(mf);
    S.screenshotPath = std::getenv("LHT_SCREENSHOT");
    S.screenshotAtFrame = S.maxFrames > 20 ? S.maxFrames - 10 : S.maxFrames / 2;

    S.perfFreq = SDL_GetPerformanceFrequency();
    S.last = SDL_GetPerformanceCounter();

#ifdef __EMSCRIPTEN__
    // simulate_infinite_loop=1: this call never returns to the caller in
    // the normal C++ sense -- it unwinds main()'s stack immediately (via an
    // internal exception) right after registering the callback, then the
    // browser drives mainLoopTick() once per requestAnimationFrame from
    // here on (the `0` fps argument means "let the browser's rAF cadence
    // drive it" rather than a fixed rate). Matches bgfx's own vendored
    // HTML5 example harness (entry.cpp's BX_PLATFORM_EMSCRIPTEN branch,
    // which also uses emscripten_set_main_loop() rather than -sASYNCIFY --
    // see PORT_PROGRESS.md's Phase 7 notes for why that was the right call
    // here too, given bgfx's heavy function-pointer/vtable dispatch).
    emscripten_set_main_loop_arg(&mainLoopTick, &S, 0, 1);
    return 0;
#else
    while (S.running && S.frame < S.maxFrames) {
        mainLoopTick(&S);
    }

    std::printf("Rendered %d frames without crashing. Final: mode=%s t=%.1f\n",
                S.frame, S.state.mode.c_str(), S.state.t);

    S.tiltInput.shutdown();
    S.audio.shutdown();
    S.renderer.shutdown();
    SDL_DestroyWindow(S.window);
    SDL_Quit();
    return 0;
#endif
}
