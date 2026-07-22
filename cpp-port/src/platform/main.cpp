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

#include "../render/renderer.h"
#include "../sim/car.h"
#include "../sim/race.h"
#include "../sim/rng.h"
#include "../sim/tracks_data.h"
#include "../ui/orientation.h"
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
        : track(TRACKS[trackIdx]), rng(rngSeed), rngR(rngRSeed) {}
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

void mainLoopTick(void* argPtr) {
    LoopState& S = *static_cast<LoopState*>(argPtr);

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
            // Debug-only: JS's tilt-steer mode is toggled from a menu
            // checkbox (index.html:4704) that doesn't exist yet in this
            // port (Phase 4's "UI overlay" job) -- desktop-testing
            // convenience standing in for that checkbox.
            if (ev.key.keysym.sym == SDLK_t && !ev.key.repeat) S.state.tilt = !S.state.tilt;
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
        // physically covering the on-screen buttons in JS.
        if (!S.portrait && ev.type == SDL_MOUSEBUTTONDOWN && ev.button.button == SDL_BUTTON_LEFT) {
            const int x = ev.button.x, y = ev.button.y;
            if (pointInRect(x, y, S.touchRegions.bL)) S.touchLeft = true;
            if (pointInRect(x, y, S.touchRegions.bR)) S.touchRight = true;
            if (pointInRect(x, y, S.touchRegions.bG)) S.touchGas = true;
            if (pointInRect(x, y, S.touchRegions.bB)) S.touchBrake = true;
            if (pointInRect(x, y, S.touchRegions.bP)) togglePlayerPit(S);
        }
        if (ev.type == SDL_MOUSEBUTTONUP && ev.button.button == SDL_BUTTON_LEFT) {
            S.touchLeft = S.touchRight = S.touchGas = S.touchBrake = false;
        }
        if (!S.portrait && ev.type == SDL_FINGERDOWN) {
            const int x = (int)(ev.tfinger.x * S.width), y = (int)(ev.tfinger.y * S.height);
            if (pointInRect(x, y, S.touchRegions.bL)) S.touchLeft = true;
            if (pointInRect(x, y, S.touchRegions.bR)) S.touchRight = true;
            if (pointInRect(x, y, S.touchRegions.bG)) S.touchGas = true;
            if (pointInRect(x, y, S.touchRegions.bB)) S.touchBrake = true;
            if (pointInRect(x, y, S.touchRegions.bP)) togglePlayerPit(S);
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
    double dt = (double)(now - S.last) / (double)S.perfFreq;
    S.last = now;
    if (dt > 0.25) dt = 0.25; // clamp a stall (e.g. window drag) instead of a physics-time jump
    S.simAcc += dt;
    while (S.simAcc >= DT) {
        tick(S.state, S.cars, S.pace, S.track, S.rngR, S.input, S.finishOrder);
        S.simAcc -= DT;
    }

    if (S.portrait) {
        S.renderer.renderBlockedFrame();
    } else {
        S.renderer.renderFrame(S.state, S.cars);
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

#ifdef __EMSCRIPTEN__
    // Safety valve, not the normal exit path: a real page load has no
    // LHT_MAX_FRAMES to hit (env vars don't exist in a browser) and no
    // SDL_QUIT event route, so this practically never fires during real
    // play. It exists so a debug/verification session (e.g. pressing
    // Escape) can still cleanly stop and tear down without a crash.
    if (!S.running || S.frame >= S.maxFrames) {
        emscripten_cancel_main_loop();
        S.tiltInput.shutdown();
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

    S.renderer.setTrack(S.track);
    S.renderer.setChaseTarget(0); // idx 0 is always the player, see car.h
    // Debug-only: force chase mode on at startup instead of waiting for a
    // live C keypress -- for scripted headless screenshot verification,
    // same rationale as LHT_FORCE_RACE below.
    if (std::getenv("LHT_START_CHASE")) S.renderer.setCameraMode(Renderer::CameraMode::Chase);

    gridStart(S.track, S.rng, S.state, S.pace, S.cars, nullptr);
    S.state.mode = "pace";
    // Debug-only: skip straight past the ~28-sim-second pace phase (during
    // which every car, including the player, is formation-driven --
    // keyboard input is ignored, matching the JS original) so keyboard
    // responsiveness can actually be tested without waiting real-time for
    // green flag. Same seeded-starting-state philosophy as the --force flag
    // used throughout this port's determinism tests, not a physics bypass.
    if (std::getenv("LHT_FORCE_RACE")) {
        S.state.mode = "race";
        S.state.flag = "green";
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
    S.renderer.shutdown();
    SDL_DestroyWindow(S.window);
    SDL_Quit();
    return 0;
#endif
}
