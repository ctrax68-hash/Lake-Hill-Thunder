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

#include "mesh_import.h"
#include "skinned_mesh.h"
#include "wheel_animation.h"

#include "../sim/car.h"
#include "../sim/race_state.h"
#include "../sim/track.h"
#include "../ui/menu.h"

#include <array>
#include <bgfx/bgfx.h>

#include <chrono>
#include <unordered_map>
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

    // Phase 5e (PORT_PROGRESS.md): the crowd-tile atlas (atlas_texture.h)
    // and the front-tier stand seats textured with it -- a separate static
    // buffer/program from stadiumVb_'s flat-colored geometry (risers +
    // upper-tier seats + pit road + wall all stay on the flat path).
    bgfx::VertexLayout texturedLitLayout_;
    bgfx::ProgramHandle texturedLitProgram_ = BGFX_INVALID_HANDLE;
    bgfx::TextureHandle atlasTexture_ = BGFX_INVALID_HANDLE;
    bgfx::VertexBufferHandle stadiumTexturedVb_ = BGFX_INVALID_HANDLE;
    uint32_t stadiumTexturedVertexCount_ = 0;

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

    // Phase 5f (PORT_PROGRESS.md): per-car livery textures (livery.h),
    // cached by car number -- mirrors JS's own CARBUFS cache. Built lazily
    // the first time each car number is drawn; NOT cleared by setTrack()
    // (a car's number/livery doesn't depend on which track it's racing
    // at), only by shutdown().
    std::unordered_map<int, bgfx::TextureHandle> carTextures_;
    bgfx::TextureHandle getOrBuildCarTexture(const Car& car);

    // Step 3 (PORT_PROGRESS.md, physics-driven car rig animation): replaces
    // the flat textured car quad above with a real skinned rig -- one
    // shared SkinnedMesh (car_rig_data.h's placeholder box-chassis-plus-4-
    // wheels rig, parsed once in init()), drawn once per car with a
    // per-car bone-matrix palette (wheel_animation.h, driven by that car's
    // live Car::v/fzFront/fzRear) and that car's own livery texture
    // (getOrBuildCarTexture(), unchanged).
    SkinnedMesh carMesh_;
    std::vector<ImportedJoint> carRigJoints_; // cached from the rig import, read every frame
    // wheel_FL/FR/RL/RR's index into carRigJoints_, resolved once in init()
    // by joint name; -1 for any wheel the rig doesn't have (defensive, not
    // expected to ever be needed for the shipped rig).
    std::array<int, 4> carWheelJointIndex_{-1, -1, -1, -1};
    // Wheel spin is an integral of angular velocity -- real per-car render
    // state that must persist across frames, keyed by car number like
    // carTextures_ above (a car's identity, not which track it's racing).
    std::unordered_map<int, WheelAnimState> carWheelAnim_;
    // Wall-clock delta for wheel-spin integration -- renderFrame() may be
    // called at a different cadence than the sim's own fixed DT tick (see
    // main.cpp's accumulator loop), so this needs its own real elapsed-time
    // clock, same idea as chaseLastTime_ above but updated unconditionally
    // every frame (wheels keep spinning in TopDown camera mode too).
    bool wheelAnimInitialized_ = false;
    std::chrono::steady_clock::time_point wheelAnimLastTime_;

    // Phase 5h (PORT_PROGRESS.md): the bloom+grade+tonemap postprocess
    // chain. The sky/world views now render into `sceneFb_` (an offscreen
    // color+depth target) instead of the backbuffer; `bloomBrightFb_`/
    // `bloomBlurFb_` are a half-resolution bright-pass + fixed-radius-blur
    // pair; the final grade+tonemap pass composites both back onto the
    // real backbuffer, and the UI view (unchanged) draws on top of that,
    // same as before -- see renderFrame()'s own comments for the full view
    // chain. `sceneFb_`'s color format is RGBA16F when the backend supports
    // it as a render target, RGBA8 otherwise (chosen once in
    // createPostFxTargets()) -- letting fs_lit.sc/fs_textured_lit.sc write
    // lit values above 1.0 for this pass's ACES curve to roll off, instead
    // of Phase 5a-5g's earlier hard `min(..., 1.0)` clamp.
    bgfx::FrameBufferHandle sceneFb_ = BGFX_INVALID_HANDLE;
    bgfx::FrameBufferHandle bloomBrightFb_ = BGFX_INVALID_HANDLE;
    bgfx::FrameBufferHandle bloomBlurFb_ = BGFX_INVALID_HANDLE;
    bgfx::ProgramHandle bloomBrightProgram_ = BGFX_INVALID_HANDLE;
    bgfx::ProgramHandle bloomBlurProgram_ = BGFX_INVALID_HANDLE;
    bgfx::ProgramHandle gradeTonemapProgram_ = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle uTexBloom_ = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle uBloomParams_ = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle uGradeParams1_ = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle uGradeParams2_ = BGFX_INVALID_HANDLE;
    // (Re)creates sceneFb_/bloomBrightFb_/bloomBlurFb_ at the given size --
    // called from init() and resize() (bgfx framebuffers, unlike the real
    // backbuffer, don't auto-resize on bgfx::reset()).
    void createPostFxTargets(int width, int height);

    bgfx::CallbackI* callback_ = nullptr;
};
