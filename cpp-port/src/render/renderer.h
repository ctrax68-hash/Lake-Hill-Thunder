#pragma once

// Phase 2 minimal renderer (PORT_PROGRESS.md): flat-shaded track ribbon
// (static mesh, built once from Track::pointAt()/halfW()) + car boxes
// (rebuilt every frame from live Car state via a transient vertex buffer),
// viewed from a camera that's either a static top-down framing of the whole
// track or a chase camera following one car. No lighting/materials/
// postprocessing yet -- that's Phase 5.

#include "../sim/car.h"
#include "../sim/track.h"

#include <bgfx/bgfx.h>

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
    void setCameraMode(CameraMode mode) { cameraMode_ = mode; }

    // If cameraMode() == Chase, follows the car at `chaseCarIdx` (matched
    // against Car::idx, same convention as everywhere else in this port).
    void setChaseTarget(int chaseCarIdx) { chaseCarIdx_ = chaseCarIdx; }

    // Draws the track ribbon + one box per car, then submits the frame.
    void renderFrame(const std::vector<Car>& cars);

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

    // Top-down framing, computed once in setTrack() from the ribbon's
    // bounding box.
    float topCx_ = 0, topCy_ = 0, topHalfW_ = 50, topHalfH_ = 50;

    bgfx::VertexLayout layout_;
    bgfx::ProgramHandle program_ = BGFX_INVALID_HANDLE;
    bgfx::VertexBufferHandle trackVb_ = BGFX_INVALID_HANDLE;
    uint32_t trackVertexCount_ = 0;

    bgfx::CallbackI* callback_ = nullptr;
};
