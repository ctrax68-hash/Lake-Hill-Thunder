#include "renderer.h"
#include "color.h"
#include "hud.h"
#include "shaders_embedded.h"
#include "track_surface.h"
#include "vertex.h"
#include "vertex_lit.h"
#include "../ui/menu.h"
#include "../ui/results.h"

#include "../sim/car.h"

#include <bx/math.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

namespace {

// Debug-only screenshot capture (see renderer.h's requestScreenshot()
// comment): bgfx hands back raw BGRA8 pixels, which this dumps verbatim
// alongside a tiny sidecar .meta text file (width height pitch yflip) so an
// external script (not part of the shipped app) can turn it into a real
// image format for a human/agent to actually look at. Every other CallbackI
// hook is a required pure virtual with no Phase-2-relevant behavior yet.
class ScreenshotCallback : public bgfx::CallbackI {
public:
    ~ScreenshotCallback() override = default;

    void fatal(const char* filePath, uint16_t line, bgfx::Fatal::Enum, const char* str) override {
        std::fprintf(stderr, "bgfx fatal (%s:%u): %s\n", filePath, line, str);
        std::abort();
    }
    void traceVargs(const char*, uint16_t, const char*, va_list) override {}
    void profilerBegin(const char*, uint32_t, const char*, uint16_t) override {}
    void profilerBeginLiteral(const char*, uint32_t, const char*, uint16_t) override {}
    void profilerEnd() override {}
    uint32_t cacheReadSize(uint64_t) override { return 0; }
    bool cacheRead(uint64_t, void*, uint32_t) override { return false; }
    void cacheWrite(uint64_t, const void*, uint32_t) override {}

    void screenShot(const char* filePath, uint32_t width, uint32_t height, uint32_t pitch,
                     bgfx::TextureFormat::Enum format, const void* data, uint32_t size,
                     bool yflip) override {
        const std::string path(filePath);
        if (FILE* f = std::fopen((path + ".rgba").c_str(), "wb")) {
            std::fwrite(data, 1, size, f);
            std::fclose(f);
        }
        if (FILE* meta = std::fopen((path + ".meta").c_str(), "w")) {
            std::fprintf(meta, "%u %u %u %d %d\n", width, height, pitch, (int)format, yflip ? 1 : 0);
            std::fclose(meta);
        }
    }

    void captureBegin(uint32_t, uint32_t, uint32_t, bgfx::TextureFormat::Enum, bool) override {}
    void captureEnd() override {}
    void captureFrame(const void*, uint32_t) override {}
};

} // namespace

bool Renderer::init(void* nativeDisplayHandle, void* nativeWindowHandle, int width, int height) {
    width_ = width;
    height_ = height;

    callback_ = new ScreenshotCallback();

    bgfx::PlatformData pd;
    pd.ndt = nativeDisplayHandle;
    pd.nwh = nativeWindowHandle;
    pd.context = nullptr;
    pd.backBuffer = nullptr;
    pd.backBufferDS = nullptr;

    bgfx::Init initInfo;
    initInfo.type = bgfx::RendererType::Count; // auto-select
    initInfo.platformData = pd;
    initInfo.resolution.width = (uint32_t)width;
    initInfo.resolution.height = (uint32_t)height;
    initInfo.resolution.reset = BGFX_RESET_VSYNC;
    initInfo.callback = callback_;

    if (!bgfx::init(initInfo)) {
        return false;
    }

    // Phase 4a (PORT_PROGRESS.md): enables bgfx's built-in debug-text
    // overlay, used by hud.cpp's drawHud() as a stand-in for a custom font
    // atlas -- this port has no other text-rendering capability yet.
    bgfx::setDebug(BGFX_DEBUG_TEXT);

    layout_.begin()
        .add(bgfx::Attrib::Position, 3, bgfx::AttribType::Float)
        .add(bgfx::Attrib::Color0, 4, bgfx::AttribType::Uint8, true)
        .end();

    bgfx::RendererType::Enum rendererType = bgfx::getRendererType();
    bgfx::ShaderHandle vsh = bgfx::createEmbeddedShader(s_embeddedShaders, rendererType, "vs_flat");
    bgfx::ShaderHandle fsh = bgfx::createEmbeddedShader(s_embeddedShaders, rendererType, "fs_flat");
    program_ = bgfx::createProgram(vsh, fsh, true /* destroy shaders when program is destroyed */);

    // Phase 5a (PORT_PROGRESS.md): lit program for world-space geometry.
    litLayout_ = PosNormalColorVertex::layout();
    bgfx::ShaderHandle vshLit = bgfx::createEmbeddedShader(s_embeddedShaders, rendererType, "vs_lit");
    bgfx::ShaderHandle fshLit = bgfx::createEmbeddedShader(s_embeddedShaders, rendererType, "fs_lit");
    litProgram_ = bgfx::createProgram(vshLit, fshLit, true);
    uSunDir_ = bgfx::createUniform("u_sunDir", bgfx::UniformType::Vec4);
    uSunColor_ = bgfx::createUniform("u_sunColor", bgfx::UniformType::Vec4);
    uHemiSky_ = bgfx::createUniform("u_hemiSky", bgfx::UniformType::Vec4);
    uHemiGround_ = bgfx::createUniform("u_hemiGround", bgfx::UniformType::Vec4);

    return bgfx::isValid(program_) && bgfx::isValid(litProgram_);
}

void Renderer::shutdown() {
    if (bgfx::isValid(trackVb_)) bgfx::destroy(trackVb_);
    if (bgfx::isValid(program_)) bgfx::destroy(program_);
    if (bgfx::isValid(litProgram_)) bgfx::destroy(litProgram_);
    if (bgfx::isValid(uSunDir_)) bgfx::destroy(uSunDir_);
    if (bgfx::isValid(uSunColor_)) bgfx::destroy(uSunColor_);
    if (bgfx::isValid(uHemiSky_)) bgfx::destroy(uHemiSky_);
    if (bgfx::isValid(uHemiGround_)) bgfx::destroy(uHemiGround_);
    bgfx::shutdown();
    delete callback_;
    callback_ = nullptr;
}

void Renderer::setTrack(const Track& track) {
    track_ = &track;
    if (bgfx::isValid(trackVb_)) bgfx::destroy(trackVb_);

    const double halfW = track.halfW();
    const double total = track.total();
    const double step = 4.0;
    const int n = std::max(8, (int)std::ceil(total / step));

    // Phase 5a (PORT_PROGRESS.md): the ribbon is now a real banked 3D
    // surface (pos3()/surfH(), track_surface.h) instead of a flat Z=0 plane
    // -- banking raises the +lat (outside) edge, matching JS's own "3D
    // surface model (render only; physics stays planar)" (index.html:377).
    struct EdgePair {
        Vec3 inner, outer;
    };
    std::vector<EdgePair> pts(n);
    float minx = 1e9f, maxx = -1e9f, minz = 1e9f, maxz = -1e9f;
    for (int i = 0; i < n; ++i) {
        const double s = (double)i / n * total;
        const Vec3 inner = pos3(track, s, -halfW);
        const Vec3 outer = pos3(track, s, halfW);
        pts[i] = {inner, outer};
        minx = std::min({minx, (float)inner.x, (float)outer.x});
        maxx = std::max({maxx, (float)inner.x, (float)outer.x});
        minz = std::min({minz, (float)inner.z, (float)outer.z});
        maxz = std::max({maxz, (float)inner.z, (float)outer.z});
    }

    // Flat per-triangle normals (not smoothed vertex normals -- a
    // deliberate simplification, logged in PORT_PROGRESS.md; acceptable
    // since the ribbon is mostly planar within a segment). Forced to point
    // up (n.y >= 0) regardless of the triangle's winding, since this
    // renderer draws with no backface culling and winding order here isn't
    // otherwise meaningful.
    auto faceNormal = [](const Vec3& a, const Vec3& b, const Vec3& c) {
        const double ux = b.x - a.x, uy = b.y - a.y, uz = b.z - a.z;
        const double vx = c.x - a.x, vy = c.y - a.y, vz = c.z - a.z;
        double nx = uy * vz - uz * vy;
        double ny = uz * vx - ux * vz;
        double nz = ux * vy - uy * vx;
        const double len = std::sqrt(nx * nx + ny * ny + nz * nz);
        if (len > 1e-9) {
            nx /= len;
            ny /= len;
            nz /= len;
        }
        if (ny < 0) {
            nx = -nx;
            ny = -ny;
            nz = -nz;
        }
        return Vec3{nx, ny, nz};
    };

    const uint32_t asphalt = packColor(0.25f, 0.25f, 0.27f);
    std::vector<PosNormalColorVertex> verts;
    verts.reserve((size_t)n * 6);
    for (int i = 0; i < n; ++i) {
        const EdgePair& a = pts[i];
        const EdgePair& b = pts[(i + 1) % n];
        // Two triangles per ribbon segment: (inner_a, outer_a, outer_b) and
        // (inner_a, outer_b, inner_b). Triangle list, not a strip -- a
        // closed loop's wraparound is simpler to get right this way, and a
        // few hundred duplicated vertices costs nothing here.
        const Vec3 n1 = faceNormal(a.inner, a.outer, b.outer);
        const Vec3 n2 = faceNormal(a.inner, b.outer, b.inner);
        verts.push_back({(float)a.inner.x, (float)a.inner.y, (float)a.inner.z,
                          (float)n1.x, (float)n1.y, (float)n1.z, asphalt});
        verts.push_back({(float)a.outer.x, (float)a.outer.y, (float)a.outer.z,
                          (float)n1.x, (float)n1.y, (float)n1.z, asphalt});
        verts.push_back({(float)b.outer.x, (float)b.outer.y, (float)b.outer.z,
                          (float)n1.x, (float)n1.y, (float)n1.z, asphalt});
        verts.push_back({(float)a.inner.x, (float)a.inner.y, (float)a.inner.z,
                          (float)n2.x, (float)n2.y, (float)n2.z, asphalt});
        verts.push_back({(float)b.outer.x, (float)b.outer.y, (float)b.outer.z,
                          (float)n2.x, (float)n2.y, (float)n2.z, asphalt});
        verts.push_back({(float)b.inner.x, (float)b.inner.y, (float)b.inner.z,
                          (float)n2.x, (float)n2.y, (float)n2.z, asphalt});
    }

    trackVb_ = bgfx::createVertexBuffer(
        bgfx::copy(verts.data(), (uint32_t)(verts.size() * sizeof(PosNormalColorVertex))), litLayout_);
    trackVertexCount_ = (uint32_t)verts.size();

    // Frame the static top-down camera to this track's bounding box (x/z,
    // i.e. the same math x/y plane as before -- pos3()'s lateral cos(bank)
    // foreshortening is a few percent at most for this game's bank angles,
    // well inside the existing 10% margin). Actual aspect-correct fitting
    // to the window happens per-frame in renderFrame() since the window
    // size can change.
    topCx_ = (minx + maxx) / 2.0f;
    topCy_ = (minz + maxz) / 2.0f;
    topHalfW_ = std::max((maxx - minx) / 2.0f * 1.1f, 10.0f);
    topHalfH_ = std::max((maxz - minz) / 2.0f * 1.1f, 10.0f);

    // Phase 4f (PORT_PROGRESS.md): the minimap's outline, built eagerly
    // here (see this class's own header comment for why that's a
    // deliberate simplification over JS's lazy-cache approach) -- a
    // 141-point centerline sample, matching index.html:4059-4062 exactly
    // (not the inner/outer ribbon edges above; the minimap draws the
    // track's centerline, not its width).
    minimapOutline_.clear();
    minimapOutline_.reserve(141);
    float boundX = 1.0f, boundY = 1.0f;
    for (int i = 0; i <= 140; ++i) {
        PointResult p = track.pointAt((double)i / 140.0 * total);
        const float px = (float)p.x, py = (float)p.y;
        minimapOutline_.push_back({px, py});
        boundX = std::max(boundX, std::abs(px));
        boundY = std::max(boundY, std::abs(py));
    }
    minimapBoundX_ = boundX;
    minimapBoundY_ = boundY;
}

void Renderer::resize(int width, int height) {
    width_ = width;
    height_ = height;
    bgfx::reset((uint32_t)width, (uint32_t)height, BGFX_RESET_VSYNC);
}

void Renderer::requestScreenshot(const char* path) {
    bgfx::requestScreenShot(BGFX_INVALID_HANDLE, path);
}

void Renderer::renderBlockedFrame() {
    const bgfx::ViewId kView = 0;
    bgfx::setViewRect(kView, 0, 0, (uint16_t)width_, (uint16_t)height_);
    // index.html:144's #rotate background is var(--c-black) -- opaque
    // black, no depth buffer needed since nothing else draws this frame.
    bgfx::setViewClear(kView, BGFX_CLEAR_COLOR | BGFX_CLEAR_DEPTH, 0x000000ff, 1.0f, 0);
    // The #rotate overlay's z-index covers the HUD too (index.html:203) --
    // clear rather than leave a stale frame's text buffer showing through.
    bgfx::dbgTextClear();
    bgfx::touch(kView);
    bgfx::frame();
}

void Renderer::renderFrame(const RaceState& raceState, const std::vector<Car>& cars,
                            const MenuSelection* menu, const std::string* menuTrackName,
                            const std::vector<Car*>* finishOrder) {
    const bgfx::ViewId kView = 0;
    // Phase 4h (PORT_PROGRESS.md): the results screen fully replaces the
    // scene (opaque black clear, no track/car geometry underneath) rather
    // than drawing on top of the still-rendering track like the menu does
    // -- confirmed via JS's own CSS that `#results`, unlike `#menu`, has no
    // semi-transparent override.
    const bool showResults = raceState.mode == "done";
    // Sequential: draw calls execute in submission order, not bgfx's default
    // sort-by-key order -- this Phase 2 scene has no depth buffer (see the
    // BGFX_STATE_* flags below), so "track first, cars on top" relies
    // entirely on submission order, same idea as 2D painter's-algorithm
    // layering.
    bgfx::setViewMode(kView, bgfx::ViewMode::Sequential);
    bgfx::setViewRect(kView, 0, 0, (uint16_t)width_, (uint16_t)height_);
    bgfx::setViewClear(kView, BGFX_CLEAR_COLOR | BGFX_CLEAR_DEPTH,
                        showResults ? 0x000000ff : 0x1a2e1aff, 1.0f, 0);

    const bool homogeneousDepth = bgfx::getCaps()->homogeneousDepth;
    float identity[16];
    bx::mtxIdentity(identity);

    if (showResults) {
        // Nothing else draws to view 0 this frame -- same "force the clear
        // with no geometry submitted" precedent as renderBlockedFrame()'s
        // own bgfx::touch(kView) call.
        bgfx::touch(kView);
    } else {
    // Phase 5a (PORT_PROGRESS.md): sun/hemisphere lighting uniforms --
    // hardcoded to the 'noon-grass' ENV_PRESETS entry's values for now
    // (index.html:3490-3491); Phase 5b makes these real per-track data
    // selected in setTrack(). Sun direction is TOWARD the sun (matches
    // fs_lit.sc's `dot(n, u_sunDir)`), from azimuth=35deg/elevation=55deg.
    {
        constexpr double az = 35.0 * 3.14159265358979323846 / 180.0;
        constexpr double el = 55.0 * 3.14159265358979323846 / 180.0;
        const float sunDir[4] = {(float)(std::cos(az) * std::cos(el)), (float)std::sin(el),
                                  (float)(std::sin(az) * std::cos(el)), 0.0f};
        const float sunColor[4] = {1.0f * 3.2f, 0.9569f * 3.2f, 0.8784f * 3.2f, 0.0f};
        const float hemiSky[4] = {0.7490f * 1.1f, 0.8392f * 1.1f, 1.0f * 1.1f, 0.0f};
        const float hemiGround[4] = {0.2f * 1.1f, 0.1843f * 1.1f, 0.1569f * 1.1f, 0.0f};
        bgfx::setUniform(uSunDir_, sunDir);
        bgfx::setUniform(uSunColor_, sunColor);
        bgfx::setUniform(uHemiSky_, hemiSky);
        bgfx::setUniform(uHemiGround_, hemiGround);
    }

    // Phase 5a (PORT_PROGRESS.md): real depth testing, now that world-space
    // geometry has genuine 3D extent (banked ribbon, and stadium/stands
    // starting Phase 5d) -- previously layering relied purely on
    // submission order (no BGFX_STATE_WRITE_Z/DEPTH_TEST at all).
    const uint64_t state =
        BGFX_STATE_WRITE_RGB | BGFX_STATE_WRITE_A | BGFX_STATE_WRITE_Z | BGFX_STATE_DEPTH_TEST_LESS;

    if (cameraMode_ == CameraMode::Chase) {
        const Car* car = nullptr;
        for (auto& c : cars) {
            if (c.idx == chaseCarIdx_) {
                car = &c;
                break;
            }
        }
        // Phase 5a (PORT_PROGRESS.md): a real 3D perspective port of JS's
        // default chase camera (index.html:3399,3438-3469) -- forward
        // lookahead + corner-lookahead bias on the look target, banking
        // lean (upBlend), and a surface-height clamp so the camera never
        // dips below the (banked) track surface. Deliberately still NOT
        // ported: the victory-orbit/pit-stop/tower/helmet/blimp/menu-
        // establishing/caution-montage alternate camera branches
        // (index.html:3293-3437) -- out of scope for this Phase 5 pass,
        // deferred to a future session (not on Phase 5's own checklist).
        if (car && track_) {
            const auto now = std::chrono::steady_clock::now();
            double dtSec = 0.0;
            if (chaseInitialized_) {
                dtSec = std::chrono::duration<double>(now - chaseLastTime_).count();
                if (dtSec > 0.05) dtSec = 0.05; // matches JS's stall clamp (index.html:3452)
            }
            chaseLastTime_ = now;

            const Vec3 base = pos3(*track_, car->s, car->lat);
            const double th = car->hdg;
            const double fwx = std::cos(th), fwz = std::sin(th); // fw.y == 0
            const Vec3 up = surfaceUp(*track_, car->s);

            const double dist = 6.9 + car->v * 0.020, hgt = 2.55; // index.html:3438
            double targetEyeX = base.x - fwx * dist + up.x * hgt;
            double targetEyeY = base.y + up.y * hgt;
            double targetEyeZ = base.z - fwz * dist + up.z * hgt;
            double targetLookX = base.x + fwx * 8.0;
            double targetLookY = base.y + 0.9;
            double targetLookZ = base.z + fwz * 8.0;

            // Corner lookahead: bias the look point into the upcoming
            // corner (index.html:3446-3449's pAh/lookLat, same constants).
            PointResult pAh = track_->pointAt(car->s + std::max(20.0, car->v * 0.7));
            const double lookLat = std::max(-2.5, std::min(2.5, pAh.curv * 450.0));
            const double nx = -std::sin(pAh.hdg), ny = std::cos(pAh.hdg);
            targetLookX += nx * lookLat;
            targetLookZ += ny * lookLat;

            if (!chaseInitialized_) {
                chaseEyeX_ = (float)targetEyeX;
                chaseEyeY_ = (float)targetEyeY;
                chaseEyeZ_ = (float)targetEyeZ;
                chaseLookX_ = (float)targetLookX;
                chaseLookY_ = (float)targetLookY;
                chaseLookZ_ = (float)targetLookZ;
                chaseInitialized_ = true;
            } else {
                // Two-rate smoothing (index.html:3453): position settles
                // slower (k) than the look target (k2), matching JS exactly
                // -- this port's earlier 2D chase camera used one shared
                // rate for both; fixing that here is a fidelity correction.
                const double k = 1.0 - std::exp(-dtSec * 11.0);
                const double k2 = 1.0 - std::exp(-dtSec * 22.0);
                chaseEyeX_ += (float)((targetEyeX - chaseEyeX_) * k);
                chaseEyeY_ += (float)((targetEyeY - chaseEyeY_) * k);
                chaseEyeZ_ += (float)((targetEyeZ - chaseEyeZ_) * k);
                chaseLookX_ += (float)((targetLookX - chaseLookX_) * k2);
                chaseLookY_ += (float)((targetLookY - chaseLookY_) * k2);
                chaseLookZ_ += (float)((targetLookZ - chaseLookZ_) * k2);
            }

            // Keep the camera above the local (banked) surface
            // (index.html:3459-3461).
            ProjectResult pr = track_->project(chaseEyeX_, chaseEyeZ_);
            const double clampedLat = std::max(apronIn(*track_), std::min(wallLat(*track_), pr.lat));
            const float minY = (float)(surfH(*track_, pr.s, clampedLat) + 1.4);
            if (chaseEyeY_ < minY) chaseEyeY_ = minY;
        }

        // Camera up blends world-up with surface bank for the NT2003 lean
        // (index.html:3463): only the horizontal (x/z) lean components are
        // dampened by 0.45 -- the vertical component is left at a flat 1.0,
        // not blended, then the whole vector is renormalized. Recomputed
        // fresh from the car's live (unsmoothed) position every frame,
        // matching JS (upBlend isn't itself smoothed, only cam.pos/look are).
        Vec3 upBlend{0, 1, 0};
        if (car && track_) {
            const Vec3 up = surfaceUp(*track_, car->s);
            upBlend = {up.x * 0.45, 1.0, up.z * 0.45};
            const double len = std::sqrt(upBlend.x * upBlend.x + upBlend.y * upBlend.y + upBlend.z * upBlend.z);
            if (len > 1e-9) {
                upBlend.x /= len;
                upBlend.y /= len;
                upBlend.z /= len;
            }
        }

        float view[16], proj[16];
        const bx::Vec3 eye = {chaseEyeX_, chaseEyeY_, chaseEyeZ_};
        const bx::Vec3 at = {chaseLookX_, chaseLookY_, chaseLookZ_};
        const bx::Vec3 up = {(float)upBlend.x, (float)upBlend.y, (float)upBlend.z};
        bx::mtxLookAt(view, eye, at, up);
        const float aspect = (height_ > 0) ? (float)width_ / (float)height_ : 1.0f;
        // FOV 60, near 0.5, far 1500 -- matches JS's own
        // `new THREE.PerspectiveCamera(60, 1, 0.5, 1500)` exactly. bx's
        // `mtxProj(fovy, aspect, ...)` overload takes `_fovy` in DEGREES
        // and converts internally (`toRad(_fovy)` inside math.cpp) -- do
        // NOT pre-convert here, that double-converts and produces a
        // near-zero effective FOV (a real bug hit and fixed this session:
        // it manifested as the entire frame being covered by whatever
        // geometry was nearest the camera, with ~75x too much magnification).
        bx::mtxProj(proj, 60.0f, aspect, 0.5f, 1500.0f, homogeneousDepth);
        bgfx::setViewTransform(kView, view, proj);
    } else {
        // TopDown: unchanged framing/purpose (a static overview of the
        // whole track), still orthographic -- now looking straight down
        // the real height (Y) axis instead of a 2D placeholder Z axis,
        // since the ribbon mesh itself is genuinely 3D as of Phase 5a.
        float halfW = topHalfW_, halfH = topHalfH_;
        const float winAspect = (height_ > 0) ? (float)width_ / (float)height_ : 1.0f;
        const float boxAspect = halfW / halfH;
        if (boxAspect > winAspect) {
            halfH = halfW / winAspect;
        } else {
            halfW = halfH * winAspect;
        }
        float view[16], proj[16];
        const bx::Vec3 eye = {topCx_, 200.0f, topCy_};
        const bx::Vec3 at = {topCx_, 0.0f, topCy_};
        const bx::Vec3 up = {0.0f, 0.0f, -1.0f};
        bx::mtxLookAt(view, eye, at, up);
        bx::mtxOrtho(proj, -halfW, halfW, -halfH, halfH, 0.1f, 1000.0f, 0.0f, homogeneousDepth);
        bgfx::setViewTransform(kView, view, proj);
    }

    if (trackVertexCount_ > 0) {
        bgfx::setTransform(identity);
        bgfx::setVertexBuffer(0, trackVb_, 0, trackVertexCount_);
        bgfx::setState(state);
        bgfx::submit(kView, litProgram_);
    }

    if (!cars.empty() && track_) {
        const uint32_t maxVerts = (uint32_t)cars.size() * 6; // 2 triangles/car, triangle list
        if (bgfx::getAvailTransientVertexBuffer(maxVerts, litLayout_) >= maxVerts) {
            bgfx::TransientVertexBuffer tvb;
            bgfx::allocTransientVertexBuffer(&tvb, maxVerts, litLayout_);
            auto* vertex = (PosNormalColorVertex*)tvb.data;
            uint32_t vIdx = 0;
            const float hl = (float)(CAR.len / 2.0);
            const float hw = (float)(CAR.wid / 2.0);
            for (auto& c : cars) {
                const float ch = (float)c.hdg;
                const float cs = std::cos(ch), sn = std::sin(ch);
                const float wx = (float)c.x, wy = (float)c.y;
                const uint32_t col = packColor((float)c.col[0], (float)c.col[1], (float)c.col[2]);
                // Phase 5a (PORT_PROGRESS.md): the quad now sits at its
                // pos3()-derived height on the (possibly banked) surface
                // instead of a flat world Z, with a surfaceUp()-derived
                // normal so it shades correctly -- but the quad's own
                // shape/corners stay flat and horizontal (no 3D car loft;
                // that's out of scope for all of Phase 5, not just this
                // sub-phase).
                const Vec3 carPos = pos3(*track_, c.s, c.lat);
                const Vec3 carUp = surfaceUp(*track_, c.s);
                const float carY = (float)carPos.y;
                // Local-space corners, rotated by heading then translated to
                // world position -- baked directly into vertex positions
                // (world space) rather than via a per-draw model matrix,
                // since this transient buffer is rebuilt from scratch every
                // frame anyway.
                const float lx[4] = {hl, hl, -hl, -hl};
                const float ly[4] = {hw, -hw, -hw, hw};
                float px[4], py[4];
                for (int k = 0; k < 4; ++k) {
                    px[k] = wx + lx[k] * cs - ly[k] * sn;
                    py[k] = wy + lx[k] * sn + ly[k] * cs;
                }
                const float nx = (float)carUp.x, ny = (float)carUp.y, nz = (float)carUp.z;
                vertex[vIdx++] = {px[0], carY, py[0], nx, ny, nz, col};
                vertex[vIdx++] = {px[1], carY, py[1], nx, ny, nz, col};
                vertex[vIdx++] = {px[2], carY, py[2], nx, ny, nz, col};
                vertex[vIdx++] = {px[0], carY, py[0], nx, ny, nz, col};
                vertex[vIdx++] = {px[2], carY, py[2], nx, ny, nz, col};
                vertex[vIdx++] = {px[3], carY, py[3], nx, ny, nz, col};
            }
            bgfx::setTransform(identity);
            bgfx::setVertexBuffer(0, &tvb, 0, vIdx);
            bgfx::setState(state);
            bgfx::submit(kView, litProgram_);
        }
    }
    } // !showResults

    // Phase 4a (PORT_PROGRESS.md): first functional slice of drawHUD()
    // (index.html:3927), drawn via bgfx's debug-text overlay -- see
    // hud.cpp for exactly what's ported vs. deferred.
    bgfx::dbgTextClear();
    std::vector<PosColorVertex> uiVerts;
    if (showResults && finishOrder) {
        // Phase 4h (PORT_PROGRESS.md): results replaces the HUD entirely,
        // same "mode-exclusive branches" precedent as the menu branch below.
        const std::vector<const Car*> order = computeRaceOrder(cars);
        const std::vector<const Car*> resultsOrder = buildResultsOrder(*finishOrder, order);
        drawResults(resultsOrder, uiVerts);
    } else {
        drawHud(raceState, cars, uiVerts, minimapOutline_, minimapBoundX_, minimapBoundY_,
                track_ ? track_->total() : 0.0);
        // Phase 4b (PORT_PROGRESS.md): drawHud() itself already early-returns
        // for mode=="menu" (hud.cpp:21), so both can unconditionally run here
        // without stepping on each other's dbgText rows.
        if (raceState.mode == "menu" && menu && menuTrackName) {
            drawMenu(*menu, raceState.laps, raceState.tilt, *menuTrackName);
        }
    }

    // Phase 4e (PORT_PROGRESS.md): a second, pixel-space orthographic view
    // for 2D UI-overlay geometry (segmented bars today; minimap/leaderboard/
    // results chips in later Phase 4 sub-tasks), reusing the exact same
    // flat-color vertex layout/shader/program as the world-space geometry
    // above -- just a different view transform, no new shader. bgfx submits
    // views in ascending ID order by default, so this draws after view 0's
    // track/cars; bgfx's own debug-text overlay always draws on top of
    // every view regardless of ID (confirmed empirically in every
    // screenshot this project has taken), so the resulting layering is
    // world geometry -> UI quads -> HUD/menu text, which is what's wanted
    // (numbers legible over bars, not chips over text). Skipped entirely
    // when empty (e.g. mode=="menu", where drawHud() early-returns before
    // adding anything) -- same "nothing submitted this frame -> nothing
    // drawn" precedent view 0's own `if (!cars.empty())` guard above relies
    // on.
    if (!uiVerts.empty()) {
        const bgfx::ViewId kUiView = 1;
        bgfx::setViewRect(kUiView, 0, 0, (uint16_t)width_, (uint16_t)height_);
        float uiViewMtx[16], uiProj[16];
        bx::mtxIdentity(uiViewMtx);
        // Top-left origin, y-down, matching dbgText/SDL mouse coordinates --
        // bottom=height, top=0 is the standard ortho y-flip trick.
        bx::mtxOrtho(uiProj, 0.0f, (float)width_, (float)height_, 0.0f, -1.0f, 1.0f, 0.0f,
                     homogeneousDepth);
        bgfx::setViewTransform(kUiView, uiViewMtx, uiProj);

        const uint32_t uiVertCount = (uint32_t)uiVerts.size();
        if (bgfx::getAvailTransientVertexBuffer(uiVertCount, layout_) >= uiVertCount) {
            bgfx::TransientVertexBuffer tvb;
            bgfx::allocTransientVertexBuffer(&tvb, uiVertCount, layout_);
            std::memcpy(tvb.data, uiVerts.data(), uiVertCount * sizeof(PosColorVertex));
            bgfx::setTransform(identity);
            bgfx::setVertexBuffer(0, &tvb, 0, uiVertCount);
            // Alpha blending isn't needed by the opaque segmented bars yet,
            // but is by the minimap's pulsing trouble ring (Phase 4f) --
            // enabled now so that addition doesn't need a second state
            // variant later.
            bgfx::setState(BGFX_STATE_WRITE_RGB | BGFX_STATE_WRITE_A | BGFX_STATE_BLEND_ALPHA);
            bgfx::submit(kUiView, program_);
        }
    }

    bgfx::frame();
}
