// Lake Hill Thunder C++ port -- Phase 0 scaffolding.
// Opens an SDL2 window, initializes bgfx against its native window handle,
// and clears the screen every frame. No sim/render logic lives here yet --
// that starts in Phase 1 (src/sim) and Phase 2 (src/render).

#include <SDL.h>
#include <SDL_syswm.h>
#include <bgfx/bgfx.h>

#include <cstdio>

int main(int argc, char** argv)
{
    (void)argc;
    (void)argv;

    if (SDL_Init(SDL_INIT_VIDEO) != 0) {
        std::fprintf(stderr, "SDL_Init failed: %s\n", SDL_GetError());
        return 1;
    }

    const int width = 1280;
    const int height = 720;

    SDL_Window* window = SDL_CreateWindow(
        "Lake Hill Thunder (C++ port -- Phase 0)",
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

    // This SDL2 build only has the X11 video driver enabled (Wayland was OFF
    // in the CMake configure output) -- Wayland support can be added back
    // here later if that changes.
    bgfx::PlatformData pd;
    if (wmi.subsystem == SDL_SYSWM_X11) {
        pd.ndt = wmi.info.x11.display;
        pd.nwh = reinterpret_cast<void*>(static_cast<uintptr_t>(wmi.info.x11.window));
    } else {
        std::fprintf(stderr, "Unsupported SDL_SYSWM subsystem: %d\n", (int)wmi.subsystem);
        SDL_DestroyWindow(window);
        SDL_Quit();
        return 1;
    }
    pd.context = nullptr;
    pd.backBuffer = nullptr;
    pd.backBufferDS = nullptr;

    bgfx::Init init;
    init.type = bgfx::RendererType::Count; // auto-select
    init.platformData = pd; // bgfx::init reads Init::platformData, not a prior setPlatformData() call
    init.resolution.width = (uint32_t)width;
    init.resolution.height = (uint32_t)height;
    init.resolution.reset = BGFX_RESET_VSYNC;

    if (!bgfx::init(init)) {
        std::fprintf(stderr, "bgfx::init failed\n");
        SDL_DestroyWindow(window);
        SDL_Quit();
        return 1;
    }

    const bgfx::ViewId kClearView = 0;
    bgfx::setViewClear(kClearView, BGFX_CLEAR_COLOR | BGFX_CLEAR_DEPTH, 0x303030ff, 1.0f, 0);
    bgfx::setViewRect(kClearView, 0, 0, bgfx::BackbufferRatio::Equal);

    bool running = true;
    int framesRendered = 0;
    // Phase 0 only needs to prove the pipeline is alive: run a bounded number
    // of frames instead of an indefinite loop, so this scaffolding is
    // scriptable/verifiable in a headless CI-like run too.
    const int maxFrames = 180;

    while (running && framesRendered < maxFrames) {
        SDL_Event ev;
        while (SDL_PollEvent(&ev)) {
            if (ev.type == SDL_QUIT) running = false;
            if (ev.type == SDL_KEYDOWN && ev.key.keysym.sym == SDLK_ESCAPE) running = false;
        }

        bgfx::touch(kClearView);
        bgfx::frame();
        ++framesRendered;
    }

    std::printf("Rendered %d frames without crashing.\n", framesRendered);

    bgfx::shutdown();
    SDL_DestroyWindow(window);
    SDL_Quit();
    return 0;
}
