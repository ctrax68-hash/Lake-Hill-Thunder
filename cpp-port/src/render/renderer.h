#pragma once

// Phase 2 minimal renderer (PORT_PROGRESS.md): a track ribbon (static mesh,
// built once from Track::pointAt()/halfW()) + car boxes (rebuilt every frame
// from live Car state via a transient vertex buffer), viewed from a camera
// that's either a static top-down framing of the whole track or a chase
// camera following one car.
//
// Phase 5a added real 3D: the ribbon is now banked (Track::bankAt() via
// track_surface.h's pos3()/surfH()), lit (hemisphere+directional, see
// vs_lit.sc/fs_lit.sc), depth-tested, and the Chase camera is a real
// perspective view with banking lean and a surface-height clamp -- see
// renderFrame()'s CameraMode::Chase branch. Materials/textures/postprocessing
// are later Phase 5 sub-tasks (5e/5f/5h); the full alternate-camera-mode
// suite (helmet/tower/blimp/victory/pit/caution-cam) remains deliberately
// out of scope for Phase 5, deferred to a future session.

#include "../sim/car.h"
#include "../sim/race_state.h"
#include "../sim/track.h"
#include "../ui/menu.h"

#include <bgfx/bgfx.h>

#include <chrono>
#include <utility>
#include <vector>

class Renderer {
public:
    bool init(void* nativeDisplayHandle, void* nativeWindowHandle, int width, int height);
    void shutdown();

    // Builds the static track-ribbon mesh and computes the top-down camera
    // framing for it. Call once, after init() and before the first
    // renderFrame().
    void setTrack(const Track& track);

    // Phase 4f (PORT_PROGRESS.md): the minimap's cached track-outline
    // polyline + bounding half-extents, built once in setTrack() -- see
    // minimap.h's drawMinimap() for how these are consumed. Exposed so
    // hud.cpp (which stays Renderer-independent) can be handed this data
    // by renderFrame() rather than depending on Renderer itself.
    const std::vector<std::pair<float, float>>& minimapOutline() const { return minimapOutline_; }
    float minimapBoundX() const { return minimapBoundX_; }
    float minimapBoundY() const { return minimapBoundY_; }

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
    // the frame. Phase 4b (PORT_PROGRESS.md): while raceState.mode=="menu",
    // `cars` is expected to be empty (no grid built yet) and `menu`/
    // `menuTrackName` (both must be non-null in that case) drive a menu
    // overlay drawn in place of the HUD -- see menu.h's drawMenu(). Ignored
    // whenever mode != "menu" (may be null then).
    //
    // Phase 4h (PORT_PROGRESS.md): while raceState.mode=="done", track/car
    // submission is skipped entirely (opaque black clear, matching
    // renderBlockedFrame()'s precedent -- confirmed via JS's own CSS that
    // `#results`, unlike `#menu`, has no semi-transparent override) and
    // `finishOrder` (must be non-null then) drives the results screen drawn
    // in place of the HUD -- see results.h's drawResults(). Ignored whenever
    // mode != "done" (may be null then).
    void renderFrame(const RaceState& raceState, const std::vector<Car>& cars,
                      const MenuSelection* menu = nullptr, const std::string* menuTrackName = nullptr,
                      const std::vector<Car*>* finishOrder = nullptr);

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

    // Phase 4f (PORT_PROGRESS.md): the minimap's track-outline polyline,
    // built once here alongside the ribbon mesh rather than JS's lazy-
    // build-then-null-to-invalidate MMPTS pattern (index.html:4058) --
    // this port already has a clean "track changed" hook (this function),
    // so there's no need to replicate that workaround.
    std::vector<std::pair<float, float>> minimapOutline_;
    float minimapBoundX_ = 1.0f, minimapBoundY_ = 1.0f;

    // Chase-camera smoothing state (index.html:3451-3457's cam.pos/cam.look
    // self-init-then-exponentially-blend idiom). Phase 5a (PORT_PROGRESS.md)
    // upgraded this from a 2D (x,y) pair to a real 3D (eye + look) pair --
    // see renderFrame()'s CameraMode::Chase branch for the full writeup.
    // Renderer keeps its own clock for this (rather than taking a dt
    // parameter) so main.cpp's render call site doesn't need to know the
    // camera has frame-rate-dependent state.
    bool chaseInitialized_ = false;
    float chaseEyeX_ = 0, chaseEyeY_ = 0, chaseEyeZ_ = 0;
    float chaseLookX_ = 0, chaseLookY_ = 0, chaseLookZ_ = 0;
    std::chrono::steady_clock::time_point chaseLastTime_;

    bgfx::VertexLayout layout_;
    bgfx::ProgramHandle program_ = BGFX_INVALID_HANDLE;
    bgfx::VertexBufferHandle trackVb_ = BGFX_INVALID_HANDLE;
    uint32_t trackVertexCount_ = 0;

    // Phase 5a (PORT_PROGRESS.md): the lit shader/vertex layout for
    // world-space geometry (the banked track ribbon here; stadium/stands
    // join it in Phase 5d). The pixel-space UI overlay (view 1) keeps using
    // `layout_`/`program_` (flat, unlit) unchanged.
    bgfx::VertexLayout litLayout_;
    bgfx::ProgramHandle litProgram_ = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle uSunDir_ = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle uSunColor_ = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle uHemiSky_ = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle uHemiGround_ = BGFX_INVALID_HANDLE;

    // Phase 5b (PORT_PROGRESS.md): the resolved ENV_PRESETS values for the
    // current track (env_presets.h), computed once in setTrack() rather
    // than per frame -- these are per-track, not per-frame, data. Replaces
    // Phase 5a's hardcoded 'noon-grass' constants in renderFrame().
    float sunDir_[4] = {0, 1, 0, 0};
    float sunColor_[4] = {0, 0, 0, 0};
    float hemiSky_[4] = {0, 0, 0, 0};
    float hemiGround_[4] = {0, 0, 0, 0};

    // Phase 5b: a large flat ground plane colored by theme.grass -- the
    // first real use of per-track color data, and something for the new
    // lighting to shade besides the ribbon itself.
    bgfx::VertexBufferHandle groundVb_ = BGFX_INVALID_HANDLE;
    uint32_t groundVertexCount_ = 0;

    // Phase 5d (PORT_PROGRESS.md): one combined static buffer for stands
    // (all 4 zones) + pit road + the outer wall, built once per setTrack()
    // alongside the ribbon/ground (stadium_mesh.h). Flat colors only --
    // crowd/wall/fence textures are Phase 5e's job.
    bgfx::VertexBufferHandle stadiumVb_ = BGFX_INVALID_HANDLE;
    uint32_t stadiumVertexCount_ = 0;

    // Phase 5c (PORT_PROGRESS.md): the sky background -- a fullscreen
    // textured quad (own unlit program/vertex layout/static NDC-space
    // vertex buffer, built once in init()) sampling a per-track texture
    // (sky_texture.h's buildSkyPixels(), rebuilt once per setTrack()).
    // Drawn in its own view (id 0, lower than the world view) so it
    // renders behind everything else regardless of submission order.
    bgfx::VertexLayout skyLayout_;
    bgfx::ProgramHandle skyProgram_ = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle uSkyTexColor_ = BGFX_INVALID_HANDLE;
    bgfx::VertexBufferHandle skyVb_ = BGFX_INVALID_HANDLE;
    bgfx::TextureHandle skyTexture_ = BGFX_INVALID_HANDLE;

    bgfx::CallbackI* callback_ = nullptr;
};
