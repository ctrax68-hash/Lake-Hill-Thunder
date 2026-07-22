// Lake Hill Thunder C++ port -- Phase 2 minimal desktop renderer.
// Opens an SDL2 window, drives a real race through the verified Phase 1 sim
// (gridStart()/tick()), and renders it with src/render/Renderer: a
// flat-shaded track ribbon + car boxes from a static top-down camera (press
// C to toggle a placeholder chase-camera view). Keyboard controls the
// player car; AI cars run the real stepCar() AI branch.

#include <SDL.h>
#include <SDL_syswm.h>

#include "../render/renderer.h"
#include "../sim/car.h"
#include "../sim/race.h"
#include "../sim/rng.h"
#include "../sim/tracks_data.h"

#include <cstdio>
#include <cstdlib>
#include <vector>

int main(int argc, char** argv)
{
    const int trackIdx = argc > 1 ? std::atoi(argv[1]) : 0;

    if (SDL_Init(SDL_INIT_VIDEO) != 0) {
        std::fprintf(stderr, "SDL_Init failed: %s\n", SDL_GetError());
        return 1;
    }

    int width = 1280;
    int height = 720;

    SDL_Window* window = SDL_CreateWindow(
        "Lake Hill Thunder (C++ port -- Phase 2)",
        SDL_WINDOWPOS_UNDEFINED, SDL_WINDOWPOS_UNDEFINED,
        width, height,
        SDL_WINDOW_SHOWN | SDL_WINDOW_RESIZABLE
    );
    if (!window) {
        std::fprintf(stderr, "SDL_CreateWindow failed: %s\n", SDL_GetError());
        SDL_Quit();
        return 1;
    }

    SDL_SysWMinfo wmi;
    SDL_VERSION(&wmi.version);
    if (!SDL_GetWindowWMInfo(window, &wmi)) {
        std::fprintf(stderr, "SDL_GetWindowWMInfo failed: %s\n", SDL_GetError());
        SDL_DestroyWindow(window);
        SDL_Quit();
        return 1;
    }
    if (wmi.subsystem != SDL_SYSWM_X11) {
        // This SDL2 build only has the X11 video driver enabled (see
        // PORT_PROGRESS.md's Phase 0 notes) -- add a Wayland branch here if
        // that build option ever changes.
        std::fprintf(stderr, "Unsupported SDL_SYSWM subsystem: %d\n", (int)wmi.subsystem);
        SDL_DestroyWindow(window);
        SDL_Quit();
        return 1;
    }
    void* ndt = wmi.info.x11.display;
    void* nwh = reinterpret_cast<void*>(static_cast<uintptr_t>(wmi.info.x11.window));

    Renderer renderer;
    if (!renderer.init(ndt, nwh, width, height)) {
        std::fprintf(stderr, "Renderer::init failed\n");
        SDL_DestroyWindow(window);
        SDL_Quit();
        return 1;
    }

    Track track(TRACKS[trackIdx]);
    renderer.setTrack(track);
    renderer.setChaseTarget(0); // idx 0 is always the player, see car.h

    Mulberry32 rng(12345);
    Mulberry32 rngR(999);
    RaceState state;
    PaceCar pace;
    std::vector<Car> cars;
    gridStart(track, rng, state, pace, cars, nullptr);
    state.mode = "pace";
    PlayerInput input;
    std::vector<Car*> finishOrder;

    // Bounded by default so this stays scriptable/verifiable in a headless
    // run too (same idea as Phase 0's main.cpp) -- set LHT_MAX_FRAMES for an
    // actual interactive session (e.g. a very large number).
    int maxFrames = 600;
    if (const char* mf = std::getenv("LHT_MAX_FRAMES")) maxFrames = std::atoi(mf);
    const char* screenshotPath = std::getenv("LHT_SCREENSHOT");
    const int screenshotAtFrame = maxFrames > 20 ? maxFrames - 10 : maxFrames / 2;

    bool running = true;
    bool chaseCam = false;
    int frame = 0;
    double simAcc = 0.0;
    Uint64 perfFreq = SDL_GetPerformanceFrequency();
    Uint64 last = SDL_GetPerformanceCounter();

    while (running && frame < maxFrames) {
        SDL_Event ev;
        while (SDL_PollEvent(&ev)) {
            if (ev.type == SDL_QUIT) running = false;
            if (ev.type == SDL_KEYDOWN) {
                if (ev.key.keysym.sym == SDLK_ESCAPE) running = false;
                if (ev.key.keysym.sym == SDLK_c && !ev.key.repeat) {
                    chaseCam = !chaseCam;
                    renderer.setCameraMode(chaseCam ? Renderer::CameraMode::Chase
                                                     : Renderer::CameraMode::TopDown);
                }
            }
            if (ev.type == SDL_WINDOWEVENT && ev.window.event == SDL_WINDOWEVENT_RESIZED) {
                width = ev.window.data1;
                height = ev.window.data2;
                renderer.resize(width, height);
            }
        }

        const Uint8* keys = SDL_GetKeyboardState(nullptr);
        input.gas = keys[SDL_SCANCODE_UP] || keys[SDL_SCANCODE_W];
        input.brake = keys[SDL_SCANCODE_DOWN] || keys[SDL_SCANCODE_S];
        input.left = keys[SDL_SCANCODE_LEFT] || keys[SDL_SCANCODE_A];
        input.right = keys[SDL_SCANCODE_RIGHT] || keys[SDL_SCANCODE_D];

        const Uint64 now = SDL_GetPerformanceCounter();
        double dt = (double)(now - last) / (double)perfFreq;
        last = now;
        if (dt > 0.25) dt = 0.25; // clamp a stall (e.g. window drag) instead of a physics-time jump
        simAcc += dt;
        while (simAcc >= DT) {
            tick(state, cars, pace, track, rngR, input, finishOrder);
            simAcc -= DT;
        }

        renderer.renderFrame(cars);
        ++frame;

        if (screenshotPath && frame == screenshotAtFrame) {
            renderer.requestScreenshot(screenshotPath);
        }
        if (frame % 250 == 0) {
            const Car* player = nullptr;
            for (auto& c : cars) {
                if (c.isPlayer) { player = &c; break; }
            }
            std::printf("frame=%d t=%.1f mode=%s flag=%s player.lap=%d player.v=%.1f\n",
                        frame, state.t, state.mode.c_str(), state.flag.c_str(),
                        player ? player->lap : -99, player ? player->v : -1.0);
        }
    }

    std::printf("Rendered %d frames without crashing. Final: mode=%s t=%.1f\n",
                frame, state.mode.c_str(), state.t);

    renderer.shutdown();
    SDL_DestroyWindow(window);
    SDL_Quit();
    return 0;
}
