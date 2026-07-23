#include "renderer.h"
#include "hud.h"
#include "shaders_embedded.h"
#include "../ui/menu.h"

#include "../sim/car.h"

#include <bx/math.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <string>

namespace {

struct PosColorVertex {
    float x, y, z;
    uint32_t abgr;
};

// bgfx's Uint8x4 Color0 attribute is read back as a single little-endian
// uint32 in R,G,B,A byte order -- i.e. 0xAABBGGRR when written out as a hex
// literal (same convention bgfx's own examples use for `m_abgr`).
uint32_t packColor(float r, float g, float b, float a = 1.0f) {
    auto b8 = [](float v) -> uint32_t {
        return (uint32_t)(std::clamp(v, 0.0f, 1.0f) * 255.0f + 0.5f);
    };
    return (b8(a) << 24) | (b8(b) << 16) | (b8(g) << 8) | b8(r);
}

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

    return bgfx::isValid(program_);
}

void Renderer::shutdown() {
    if (bgfx::isValid(trackVb_)) bgfx::destroy(trackVb_);
    if (bgfx::isValid(program_)) bgfx::destroy(program_);
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

    struct EdgePair {
        float ix, iy, ox, oy;
    };
    std::vector<EdgePair> pts(n);
    float minx = 1e9f, maxx = -1e9f, miny = 1e9f, maxy = -1e9f;
    for (int i = 0; i < n; ++i) {
        const double s = (double)i / n * total;
        PointResult p = track.pointAt(s);
        const double nx = -std::sin(p.hdg), ny = std::cos(p.hdg);
        // Inner edge is lat=-halfW, outer edge is lat=+halfW -- same
        // (p.x + nx*lat, p.y + ny*lat) convention used everywhere else in
        // this port (see e.g. stepPace()'s pace.x/pace.y).
        const float ix = (float)(p.x - nx * halfW), iy = (float)(p.y - ny * halfW);
        const float ox = (float)(p.x + nx * halfW), oy = (float)(p.y + ny * halfW);
        pts[i] = {ix, iy, ox, oy};
        minx = std::min({minx, ix, ox});
        maxx = std::max({maxx, ix, ox});
        miny = std::min({miny, iy, oy});
        maxy = std::max({maxy, iy, oy});
    }

    const uint32_t asphalt = packColor(0.25f, 0.25f, 0.27f);
    std::vector<PosColorVertex> verts;
    verts.reserve((size_t)n * 6);
    for (int i = 0; i < n; ++i) {
        const EdgePair& a = pts[i];
        const EdgePair& b = pts[(i + 1) % n];
        // Two triangles per ribbon segment: (inner_a, outer_a, outer_b) and
        // (inner_a, outer_b, inner_b). Triangle list, not a strip -- a
        // closed loop's wraparound is simpler to get right this way, and a
        // few hundred duplicated vertices costs nothing here.
        verts.push_back({a.ix, a.iy, 0.0f, asphalt});
        verts.push_back({a.ox, a.oy, 0.0f, asphalt});
        verts.push_back({b.ox, b.oy, 0.0f, asphalt});
        verts.push_back({a.ix, a.iy, 0.0f, asphalt});
        verts.push_back({b.ox, b.oy, 0.0f, asphalt});
        verts.push_back({b.ix, b.iy, 0.0f, asphalt});
    }

    trackVb_ = bgfx::createVertexBuffer(
        bgfx::copy(verts.data(), (uint32_t)(verts.size() * sizeof(PosColorVertex))), layout_);
    trackVertexCount_ = (uint32_t)verts.size();

    // Frame the static top-down camera to this track's bounding box, with a
    // 10% margin. Actual aspect-correct fitting to the window happens per-
    // frame in renderFrame() since the window size can change.
    topCx_ = (minx + maxx) / 2.0f;
    topCy_ = (miny + maxy) / 2.0f;
    topHalfW_ = std::max((maxx - minx) / 2.0f * 1.1f, 10.0f);
    topHalfH_ = std::max((maxy - miny) / 2.0f * 1.1f, 10.0f);
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
                            const MenuSelection* menu, const std::string* menuTrackName) {
    const bgfx::ViewId kView = 0;
    // Sequential: draw calls execute in submission order, not bgfx's default
    // sort-by-key order -- this Phase 2 scene has no depth buffer (see the
    // BGFX_STATE_* flags below), so "track first, cars on top" relies
    // entirely on submission order, same idea as 2D painter's-algorithm
    // layering.
    bgfx::setViewMode(kView, bgfx::ViewMode::Sequential);
    bgfx::setViewRect(kView, 0, 0, (uint16_t)width_, (uint16_t)height_);
    bgfx::setViewClear(kView, BGFX_CLEAR_COLOR | BGFX_CLEAR_DEPTH, 0x1a2e1aff, 1.0f, 0);

    float cx = topCx_, cy = topCy_;
    float halfW = topHalfW_, halfH = topHalfH_;
    if (cameraMode_ == CameraMode::Chase) {
        const Car* car = nullptr;
        for (auto& c : cars) {
            if (c.idx == chaseCarIdx_) {
                car = &c;
                break;
            }
        }
        // 2D-adapted analogue of the JS default chase camera
        // (index.html:3399-3457): forward lookahead + corner-lookahead bias
        // on the look target, both exponentially smoothed toward every
        // frame. Deliberately NOT ported: banking lean (upBlend), surface-
        // height clamping, and the victory/pit-stop/tower/helmet/blimp/menu/
        // caution-montage alternate camera branches (index.html:3293-3437)
        // -- all of those need real 3D track/car geometry (elevation,
        // banking mesh, a car model matrix) that doesn't exist until
        // Phase 5. This is a flat top-down view with a tighter zoom and
        // smoothed follow, not a 3rd-person chase cam.
        if (car && track_) {
            const auto now = std::chrono::steady_clock::now();
            double dtSec = 0.0;
            if (chaseInitialized_) {
                dtSec = std::chrono::duration<double>(now - chaseLastTime_).count();
                if (dtSec > 0.05) dtSec = 0.05; // matches JS's stall clamp (index.html:3452)
            }
            chaseLastTime_ = now;

            const double fwx = std::cos(car->hdg), fwy = std::sin(car->hdg);
            const double lookAheadDist = 10.0 + car->v * 0.35;
            double targetCx = car->x + fwx * lookAheadDist;
            double targetCy = car->y + fwy * lookAheadDist;

            // Corner-lookahead bias (index.html:3446-3449's pAh/lookLat,
            // same constants -- this is a world-space offset, not a
            // screen-space one, so it doesn't need rescaling for this
            // camera's tighter zoom).
            PointResult pAh = track_->pointAt(car->s + std::max(20.0, car->v * 0.7));
            const double lookLat = std::max(-2.5, std::min(2.5, pAh.curv * 450.0));
            const double nx = -std::sin(pAh.hdg), ny = std::cos(pAh.hdg);
            targetCx += nx * lookLat;
            targetCy += ny * lookLat;

            if (!chaseInitialized_) {
                chaseCx_ = (float)targetCx;
                chaseCy_ = (float)targetCy;
                chaseInitialized_ = true;
            } else {
                const double k = 1.0 - std::exp(-dtSec * 11.0); // index.html:3453
                chaseCx_ += (float)((targetCx - chaseCx_) * k);
                chaseCy_ += (float)((targetCy - chaseCy_) * k);
            }
        }
        cx = chaseCx_;
        cy = chaseCy_;
        halfW = 50.0f;
        halfH = 50.0f;
    }

    const float winAspect = (height_ > 0) ? (float)width_ / (float)height_ : 1.0f;
    const float boxAspect = halfW / halfH;
    if (boxAspect > winAspect) {
        halfH = halfW / winAspect;
    } else {
        halfW = halfH * winAspect;
    }

    float view[16], proj[16];
    const bx::Vec3 eye = {cx, cy, 200.0f};
    const bx::Vec3 at = {cx, cy, 0.0f};
    const bx::Vec3 up = {0.0f, 1.0f, 0.0f};
    bx::mtxLookAt(view, eye, at, up);
    const bool homogeneousDepth = bgfx::getCaps()->homogeneousDepth;
    bx::mtxOrtho(proj, -halfW, halfW, -halfH, halfH, 0.1f, 1000.0f, 0.0f, homogeneousDepth);
    bgfx::setViewTransform(kView, view, proj);

    float identity[16];
    bx::mtxIdentity(identity);

    const uint64_t state = BGFX_STATE_WRITE_RGB | BGFX_STATE_WRITE_A;

    if (trackVertexCount_ > 0) {
        bgfx::setTransform(identity);
        bgfx::setVertexBuffer(0, trackVb_, 0, trackVertexCount_);
        bgfx::setState(state);
        bgfx::submit(kView, program_);
    }

    if (!cars.empty()) {
        const uint32_t maxVerts = (uint32_t)cars.size() * 6; // 2 triangles/car, triangle list
        if (bgfx::getAvailTransientVertexBuffer(maxVerts, layout_) >= maxVerts) {
            bgfx::TransientVertexBuffer tvb;
            bgfx::allocTransientVertexBuffer(&tvb, maxVerts, layout_);
            auto* vertex = (PosColorVertex*)tvb.data;
            uint32_t vIdx = 0;
            const float hl = (float)(CAR.len / 2.0);
            const float hw = (float)(CAR.wid / 2.0);
            for (auto& c : cars) {
                const float ch = (float)c.hdg;
                const float cs = std::cos(ch), sn = std::sin(ch);
                const float wx = (float)c.x, wy = (float)c.y;
                const uint32_t col = packColor((float)c.col[0], (float)c.col[1], (float)c.col[2]);
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
                vertex[vIdx++] = {px[0], py[0], 0.0f, col};
                vertex[vIdx++] = {px[1], py[1], 0.0f, col};
                vertex[vIdx++] = {px[2], py[2], 0.0f, col};
                vertex[vIdx++] = {px[0], py[0], 0.0f, col};
                vertex[vIdx++] = {px[2], py[2], 0.0f, col};
                vertex[vIdx++] = {px[3], py[3], 0.0f, col};
            }
            bgfx::setTransform(identity);
            bgfx::setVertexBuffer(0, &tvb, 0, vIdx);
            bgfx::setState(state);
            bgfx::submit(kView, program_);
        }
    }

    // Phase 4a (PORT_PROGRESS.md): first functional slice of drawHUD()
    // (index.html:3927), drawn via bgfx's debug-text overlay -- see
    // hud.cpp for exactly what's ported vs. deferred.
    bgfx::dbgTextClear();
    drawHud(raceState, cars);
    // Phase 4b (PORT_PROGRESS.md): drawHud() itself already early-returns
    // for mode=="menu" (hud.cpp:21), so both can unconditionally run here
    // without stepping on each other's dbgText rows.
    if (raceState.mode == "menu" && menu && menuTrackName) {
        drawMenu(*menu, raceState.laps, raceState.tilt, *menuTrackName);
    }

    bgfx::frame();
}
