#pragma once

// Phase 2 minimal renderer (PORT_PROGRESS.md): flat-shaded track ribbon
// (static mesh, built once from Track::pointAt()/halfW()) + car boxes
// (rebuilt every frame from live Car state via a transient vertex buffer),
// viewed from a camera that's either a static top-down framing of the whole
// track or a chase camera following one car. No lighting/materials/
// postprocessing yet -- that's Phase 5.

#include "../sim/car.h"
#include "../sim/race_state.h"
#include "../sim/track.h"

#include <bgfx/bgfx.h>

#include <chrono>
#include <vector>

class Renderer {
public:
    bool init(void* nativeDisplayHandle, void* nativeWindowHandle, int width, int height);
    void shutdown();

    // Builds the static track-ribbon mesh and computes the top-down camera
    // framing for it. Call once, after init() and before the first
    // renderFrame().
    void setTrack(const Track& track);

    void resize(int width, int height);

    enum class CameraMode { TopDown, Chase };
    // Re-entering Chase always hard-snaps on the next renderFrame() rather
    // than smoothing in from wherever the camera last was (same "hard cut,
    // not glide" idiom the JS uses for mode-transition cuts, e.g.
    // startRace()/startQualifying()/the caution-restart cut noted in
    // race.cpp's cautionController() comments).
    void setCameraMode(CameraMode mode) {
        if (mode == CameraMode::Chase && cameraMode_ != CameraMode::Chase) chaseInitialized_ = false;
        cameraMode_ = mode;
    }

    // If cameraMode() == Chase, follows the car at `chaseCarIdx` (matched
    // against Car::idx, same convention as everywhere else in this port).
    void setChaseTarget(int chaseCarIdx) { chaseCarIdx_ = chaseCarIdx; }

    // Draws the track ribbon + one box per car + HUD text, then submits
    // the frame.
    void renderFrame(const RaceState& raceState, const std::vector<Car>& cars);

    // Phase 3c (PORT_PROGRESS.md): stand-in for the CSS `#rotate` prompt
    // (index.html:140-147,203) shown whenever the viewport is portrait --
    // this port has no text/icon rendering yet (that's Phase 4's job), so
    // this just clears to black and submits an otherwise-empty frame
    // instead of drawing the track/cars, which is the same practical
    // effect from a player's perspective (the game is fully hidden) even
    // though it lacks the actual "ROTATE YOUR PHONE" message and spinning
    // phone icon graphic.
    void renderBlockedFrame();

    // Debug-only: requests bgfx capture the next presented frame to
    // `path` (BMP via bgfx's own writer -- see renderer.cpp's ScreenshotCb).
    // Not part of the shipped app; used by this session's own verification
    // (see PORT_PROGRESS.md's Phase 2 notes) and any future headless
    // regression tooling that wants a rendered-frame check, same idea as
    // tools/playtest.js's screenshots on the JS side.
    void requestScreenshot(const char* path);

private:
    int width_ = 0, height_ = 0;
    CameraMode cameraMode_ = CameraMode::TopDown;
    int chaseCarIdx_ = 0;

    // Set by setTrack(); needed by the chase camera's corner-lookahead bias
    // (Track::pointAt() ahead of the chased car). Not owned -- caller's
    // Track must outlive this Renderer, true for main.cpp's usage.
    const Track* track_ = nullptr;

    // Top-down framing, computed once in setTrack() from the ribbon's
    // bounding box.
    float topCx_ = 0, topCy_ = 0, topHalfW_ = 50, topHalfH_ = 50;

    // Chase-camera smoothing state (index.html:3451-3457's cam.pos/cam.look
    // self-init-then-exponentially-blend idiom, adapted to 2D -- see
    // renderFrame()'s CameraMode::Chase branch for the full writeup of what
    // this is and isn't a port of). Renderer keeps its own clock for this
    // (rather than taking a dt parameter) so main.cpp's render call site
    // doesn't need to know the camera has frame-rate-dependent state.
    bool chaseInitialized_ = false;
    float chaseCx_ = 0, chaseCy_ = 0;
    std::chrono::steady_clock::time_point chaseLastTime_;

    bgfx::VertexLayout layout_;
    bgfx::ProgramHandle program_ = BGFX_INVALID_HANDLE;
    bgfx::VertexBufferHandle trackVb_ = BGFX_INVALID_HANDLE;
    uint32_t trackVertexCount_ = 0;

    bgfx::CallbackI* callback_ = nullptr;
};
