#include "renderer.h"
#include "atlas_texture.h"
#include "car_rig_data.h"
#include "color.h"
#include "env_presets.h"
#include "hud.h"
#include "livery.h"
#include "particle_texture.h"
#include "shaders_embedded.h"
#include "sky_texture.h"
#include "hill_silhouette.h"
#include "pylon_mesh.h"
#include "stadium_mesh.h"
#include "track_surface.h"
#include "track_surface_texture.h"
#include "ui_draw.h" // G23: the mirror's own bevelled frame
#include "vertex.h"
#include "vertex_lit.h"
#include "vertex_particle.h"
#include "vertex_textured.h"
#include "vertex_uv.h"
#include "../ui/menu.h"
#include "../ui/results.h"

#include "../sim/car.h"

#include <bx/math.h>

#ifdef __EMSCRIPTEN__
#include <emscripten.h>
#endif

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

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
    void traceVargs(const char* filePath, uint16_t line, const char* format, va_list argList) override {
        char buf[512];
        std::vsnprintf(buf, sizeof(buf), format, argList);
        std::fprintf(stderr, "bgfx trace (%s:%u): %s", filePath, line, buf);
    }
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

#ifdef __EMSCRIPTEN__
// Diagnostic-only: unlike main.cpp's LHT_* env-var flags (which never reach
// a real browser tab -- std::getenv() has nothing to read from in a page
// loaded over HTTP), this reads the page's own URL directly, so a real
// on-device tester can opt in via ?skipCars=1. Added to conclusively test
// whether SkinnedMesh::draw()'s indexed glDrawElementsInstanced (the only
// code path in this game capable of an indexed draw) is the source of a
// WebKit-only GL_INVALID_OPERATION abort seen after Start Race, without
// needing to patch the vendored bgfx submodule (any such edit would be
// silently discarded by CI's next `git submodule update`).
EM_JS(int, lhtSkipCarsFlag, (), {
    return /[?&]skipCars=1(&|$)/.test(location.search) ? 1 : 0;
});
#endif

bool skipCarDraws() {
#ifdef __EMSCRIPTEN__
    static const bool skip = lhtSkipCarsFlag() != 0;
#else
    static const bool skip = std::getenv("LHT_SKIP_CARS") != nullptr;
#endif
    return skip;
}

#ifdef __EMSCRIPTEN__
// Diagnostic-only, same rationale/pattern as ?skipCars=1 above: added to
// bisect a "3D scene renders literally flipped/inverted" report seen only on
// iPhone/iPad Safari (WebKit), never reproduced in this sandbox's headless
// Chromium -- since the HUD (a separate view, drawn straight to the real
// backbuffer, never routed through sceneFb_) reportedly still renders
// correctly, the leading suspect is the sky/world -> offscreen sceneFb_ ->
// bloom/grade/tonemap sampling round-trip (Phase 5h, PORT_PROGRESS.md)
// behaving differently under Safari's own WebGL2 implementation than under
// Chromium/ANGLE's -- a real, driver-level divergence an Explore agent found
// no code-level explanation for (bgfx hardcodes originBottomLeft=true for
// the whole GL/GLES/WebGL family, identically compiled into the one wasm
// binary both browsers run; see PORT_PROGRESS.md for the full writeup).
// ?skipPostFx=1 makes the sky/world views render straight to the backbuffer
// and skips the bloom/grade/tonemap views entirely, so a real-device retest
// can confirm (or rule out) that round-trip as the flip's actual source
// before any blind fix is attempted.
EM_JS(int, lhtSkipPostFxFlag, (), {
    return /[?&]skipPostFx=1(&|$)/.test(location.search) ? 1 : 0;
});
#endif

bool skipPostFx() {
#ifdef __EMSCRIPTEN__
    static const bool skip = lhtSkipPostFxFlag() != 0;
#else
    static const bool skip = std::getenv("LHT_SKIP_POSTFX") != nullptr;
#endif
    return skip;
}

// Builds a full RGBA8 box-filter mip chain (level 0 through 1x1) from a
// square, power-of-two base image, concatenated in the order
// bgfx::createTexture2D(_hasMips=true, ...) expects. Written by hand rather
// than reused from bimg's own imageRgba8Downsample2x2 -- this codebase's
// other texture-import code (texture_import.h's own comment) already opted
// to bypass bimg's public API for a prior format-support reason, and this
// is a small, self-contained, dependency-free alternative.
std::vector<uint8_t> buildRgba8MipChain(const std::vector<uint8_t>& level0, int size) {
    std::vector<uint8_t> chain = level0;
    std::vector<uint8_t> prev = level0;
    int prevSize = size;
    while (prevSize > 1) {
        const int nextSize = prevSize / 2;
        std::vector<uint8_t> next((size_t)nextSize * nextSize * 4);
        for (int y = 0; y < nextSize; ++y) {
            for (int x = 0; x < nextSize; ++x) {
                const int sx = x * 2, sy = y * 2;
                for (int ch = 0; ch < 4; ++ch) {
                    const int a = prev[((size_t)(sy) * prevSize + sx) * 4 + ch];
                    const int b = prev[((size_t)(sy) * prevSize + sx + 1) * 4 + ch];
                    const int c = prev[((size_t)(sy + 1) * prevSize + sx) * 4 + ch];
                    const int d = prev[((size_t)(sy + 1) * prevSize + sx + 1) * 4 + ch];
                    next[((size_t)y * nextSize + x) * 4 + ch] = (uint8_t)((a + b + c + d + 2) / 4);
                }
            }
        }
        chain.insert(chain.end(), next.begin(), next.end());
        prev = std::move(next);
        prevSize = nextSize;
    }
    return chain;
}

// Step 3 (PORT_PROGRESS.md, physics-driven car rig animation): the car
// rig's model matrix, built by hand (not bx::mtx*) for the same reason
// wheel_animation.cpp avoids it -- a quick read of bx's mtxMul left its
// row/column-major multiply-order convention unclear, and this project has
// already paid once (Step 2's uvec4/vec4 attribute mismatch) for trusting
// an unverified convention. mat4RotateY follows the same right-handed
// cyclic-axis convention as wheel_animation.cpp's mat4RotateZ (X->Y->Z->X):
// rotateY(theta) maps X' = X*cos+Z*sin, Z' = -X*sin+Z*cos, so matching the
// flat quad's own world-space rotation (px = wx + lx*cos(ch) - ly*sin(ch),
// py = wy + lx*sin(ch) + ly*cos(ch), with the rig's local X = nose-tail and
// local Z = left-right, same axes the quad's lx/ly already used) requires
// calling this with -ch, not ch.
using Mat4f = std::array<float, 16>;

Mat4f mat4Identity() {
    Mat4f m{};
    m[0] = m[5] = m[10] = m[15] = 1.0f;
    return m;
}

Mat4f mat4Translate(float x, float y, float z) {
    Mat4f m = mat4Identity();
    m[12] = x;
    m[13] = y;
    m[14] = z;
    return m;
}

Mat4f mat4RotateY(float angle) {
    Mat4f m = mat4Identity();
    const float c = std::cos(angle), s = std::sin(angle);
    m[0] = c;
    m[2] = -s;
    m[8] = s;
    m[10] = c;
    return m;
}

Mat4f mat4Mul(const Mat4f& a, const Mat4f& b) {
    Mat4f r{};
    for (int col = 0; col < 4; ++col) {
        for (int row = 0; row < 4; ++row) {
            float sum = 0.0f;
            for (int k = 0; k < 4; ++k) sum += a[k * 4 + row] * b[col * 4 + k];
            r[col * 4 + row] = sum;
        }
    }
    return r;
}

// H3 (NT2003 engine-feel plan): JS's carBasis()/carShadowMat()
// (index.html:3239-3274), reduced to just the tangent-plane basis the
// contact-shadow decal needs. Deliberately NOT plugged into the car
// BODY's own model matrix -- that stays the pre-existing flat
// mat4Mul(mat4Translate(...), mat4RotateY(-ch)) below, untouched by this
// phase (see PORT_PROGRESS.md's H3 entry for why that divergence from JS
// is fine to leave as-is). The shadow has no such slack: a flat quad
// under a car that doesn't tilt would still visibly float/clip through
// the asphalt on every banked corner, since the BANKED SURFACE itself
// (unlike this port's car body) is real 3D geometry. So the shadow gets
// its own basis, built from the same surfaceUp() the chase camera already
// leans against (renderFrame()'s upBlend), combined with the car's own
// heading for the forward axis -- exactly the pairing already established
// at this file's chase-camera site above (surfaceUp(track, s) + cos/sin
// of the car's own pose.hdg).
Vec3 vec3Cross(const Vec3& a, const Vec3& b) {
    return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x};
}

Vec3 vec3Normalize(const Vec3& v) {
    const double len = std::sqrt(v.x * v.x + v.y * v.y + v.z * v.z);
    if (len < 1e-9) return {0.0, 1.0, 0.0};
    return {v.x / len, v.y / len, v.z / len};
}

struct ShadowBasis {
    Vec3 right, fw, up;
};

// fw: the car's heading vector re-projected into the plane perpendicular
// to `up` (JS's `v3norm(cross(up, cross(f,up)))` double-cross projection,
// sign-corrected to keep pointing the same way as the raw heading -- an
// exact port of index.html:3247-3248; `c.pitch` is dropped, not
// approximated, since this port's Car has no pitch field). right/up follow
// from there exactly as carBasis() derives them; `upF` is algebraically
// identical to `up` (cross(cross(fw,up),fw) == up for orthonormal fw/up),
// so unlike JS this doesn't recompute a redundant second copy of it.
ShadowBasis carShadowBasis(const Track& track, double s, double heading) {
    const Vec3 up = surfaceUp(track, s);
    const Vec3 f{std::cos(heading), 0.0, std::sin(heading)};
    Vec3 fw = vec3Normalize(vec3Cross(up, vec3Cross(f, up)));
    if (fw.x * f.x + fw.z * f.z < 0.0) fw = {-fw.x, -fw.y, -fw.z};
    const Vec3 right = vec3Normalize(vec3Cross(fw, up));
    return {right, fw, up};
}

// carShadowMat() (index.html:3267-3274): footprint 2.3 (right) x 4.6
// (forward), lifted 0.02 along the surface normal to dodge z-fighting with
// the ribbon underneath. `base` is the same pos3() point the caller already
// computed for the car's own carY (or the pace car's), passed in rather
// than recomputed. Columns follow this file's own column-major convention
// (mat4Translate/mat4RotateY above): col0=right*W, col1=fw*L, col2=up,
// col3=position -- a direct adaptation of JS's m4fromBasis(right, fw, upF,
// pos), which uses the same column layout.
Mat4f carShadowModelMat(const Track& track, double s, double heading, const Vec3& base) {
    const ShadowBasis b = carShadowBasis(track, s, heading);
    constexpr double kShadowW = 2.3, kShadowL = 4.6, kShadowLift = 0.02;
    Mat4f m{};
    m[0] = (float)(b.right.x * kShadowW);
    m[1] = (float)(b.right.y * kShadowW);
    m[2] = (float)(b.right.z * kShadowW);
    m[3] = 0.0f;
    m[4] = (float)(b.fw.x * kShadowL);
    m[5] = (float)(b.fw.y * kShadowL);
    m[6] = (float)(b.fw.z * kShadowL);
    m[7] = 0.0f;
    m[8] = (float)b.up.x;
    m[9] = (float)b.up.y;
    m[10] = (float)b.up.z;
    m[11] = 0.0f;
    m[12] = (float)(base.x + b.up.x * kShadowLift);
    m[13] = (float)(base.y + b.up.y * kShadowLift);
    m[14] = (float)(base.z + b.up.z * kShadowLift);
    m[15] = 1.0f;
    return m;
}

// H3: the shadow decal's own static geometry -- a unit-radius (0.5, matching
// THREE.PlaneGeometry(1,1)'s extent) disc built once and stretched into the
// W x L footprint by carShadowModelMat()'s own column magnitudes, exactly
// as JS scales its basis vectors before calling m4fromBasis() rather than
// scaling the geometry -- so a non-uniform right/fw scale distorts the disc
// into the same ellipse JS's stretched plane produces. Two-ring alpha
// falloff (0.5 center, 0.22 at 65% radius, 0 at the rim) mirrors JS's canvas
// radial-gradient stops (index.html:3928) as vertex-color alpha instead of a
// texture lookup: vs_flat/fs_flat (program_) has no UV/sampler at all, only
// a_color0 passthrough, so a soft edge has to come from interpolated vertex
// color across the fan rather than a sampled gradient.
std::vector<PosColorVertex> buildShadowDiscVertices(int segments) {
    constexpr float kMidR = 0.5f * 0.65f, kOuterR = 0.5f;
    // Same 0.5/0.22/0 alpha stops as JS's canvas radial gradient
    // (index.html:3928's 'rgba(0,0,0,0.5)'/'0.22'/'0'), black rgb.
    const uint32_t kCenterColor = packColor(0.0f, 0.0f, 0.0f, 0.5f);
    const uint32_t kMidColor = packColor(0.0f, 0.0f, 0.0f, 0.22f);
    const uint32_t kRimColor = packColor(0.0f, 0.0f, 0.0f, 0.0f);
    const float twoPi = 2.0f * (float)M_PI;
    std::vector<PosColorVertex> out;
    out.reserve((size_t)segments * 9);
    for (int i = 0; i < segments; ++i) {
        const float a0 = (float)i / (float)segments * twoPi;
        const float a1 = (float)(i + 1) / (float)segments * twoPi;
        const float c0 = std::cos(a0), s0 = std::sin(a0), c1 = std::cos(a1), s1 = std::sin(a1);
        // Center -> mid-ring fan.
        out.push_back({0.0f, 0.0f, 0.0f, kCenterColor});
        out.push_back({kMidR * c0, kMidR * s0, 0.0f, kMidColor});
        out.push_back({kMidR * c1, kMidR * s1, 0.0f, kMidColor});
        // Mid-ring -> outer-ring (rim) strip, as two triangles.
        out.push_back({kMidR * c0, kMidR * s0, 0.0f, kMidColor});
        out.push_back({kOuterR * c0, kOuterR * s0, 0.0f, kRimColor});
        out.push_back({kOuterR * c1, kOuterR * s1, 0.0f, kRimColor});
        out.push_back({kMidR * c0, kMidR * s0, 0.0f, kMidColor});
        out.push_back({kOuterR * c1, kOuterR * s1, 0.0f, kRimColor});
        out.push_back({kMidR * c1, kMidR * s1, 0.0f, kMidColor});
    }
    return out;
}

// G15 (NASCAR-Thunder gap-analysis plan): render-side pose interpolation,
// ported from JS's `lerpAng()`/`lerpS()`/`poseOf()` (index.html:3229-3238).
// `tick()` runs at a fixed DT=0.02 (50Hz) while the display refreshes at an
// uncorrelated rate (~60Hz); drawing each car's raw, tick-snapped x/y/hdg/s/
// lat every frame makes on-screen motion advance in an uneven 0/1/2-ticks-
// per-frame cadence instead of a constant amount, which reads as stutter --
// worst in a close chase-cam view. Blending the pre-tick pose (Car::px/py/
// phdg/ps/plat, stored every tick in race.cpp) against the post-tick pose by
// the accumulator's leftover fraction (`alpha`) removes that snap.
double lerpAngRad(double a, double b, double t) {
    double d = b - a;
    while (d > M_PI) d -= 2.0 * M_PI;
    while (d < -M_PI) d += 2.0 * M_PI;
    return a + d * t;
}

double lerpTrackS(double a, double b, double t, double trackTotal) {
    double d = b - a;
    if (d > trackTotal / 2.0) d -= trackTotal;
    if (d < -trackTotal / 2.0) d += trackTotal;
    double r = a + d * t;
    return std::fmod(std::fmod(r, trackTotal) + trackTotal, trackTotal);
}

struct InterpPose {
    double x, y, hdg, s, lat;
};

InterpPose interpolatedPose(const Car& c, double alpha, double trackTotal) {
    return InterpPose{
        c.px + (c.x - c.px) * alpha,
        c.py + (c.y - c.py) * alpha,
        lerpAngRad(c.phdg, c.hdg, alpha),
        lerpTrackS(c.ps, c.s, alpha, trackTotal),
        c.plat + (c.lat - c.plat) * alpha,
    };
}

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

    // H3 (NT2003 engine-feel plan): the contact-shadow disc, built once here
    // (needs layout_, just created above) and stretched per-car by
    // carShadowModelMat() every frame -- see renderer.h's shadowVb_ comment.
    {
        const auto shadowVerts = buildShadowDiscVertices(24);
        shadowVb_ = bgfx::createVertexBuffer(
            bgfx::copy(shadowVerts.data(), (uint32_t)(shadowVerts.size() * sizeof(PosColorVertex))), layout_);
        shadowVertexCount_ = (uint32_t)shadowVerts.size();
    }

    // Phase 5a (PORT_PROGRESS.md): lit program for world-space geometry.
    litLayout_ = PosNormalColorVertex::layout();
    bgfx::ShaderHandle vshLit = bgfx::createEmbeddedShader(s_embeddedShaders, rendererType, "vs_lit");
    bgfx::ShaderHandle fshLit = bgfx::createEmbeddedShader(s_embeddedShaders, rendererType, "fs_lit");
    litProgram_ = bgfx::createProgram(vshLit, fshLit, true);
    uSunDir_ = bgfx::createUniform("u_sunDir", bgfx::UniformType::Vec4);
    uSunColor_ = bgfx::createUniform("u_sunColor", bgfx::UniformType::Vec4);
    uHemiSky_ = bgfx::createUniform("u_hemiSky", bgfx::UniformType::Vec4);
    uHemiGround_ = bgfx::createUniform("u_hemiGround", bgfx::UniformType::Vec4);
    uCamPos_ = bgfx::createUniform("u_camPos", bgfx::UniformType::Vec4);

    // Phase 5c (PORT_PROGRESS.md): sky program for the fullscreen textured
    // background quad.
    skyLayout_ = PosUvVertex::layout();
    bgfx::ShaderHandle vshSky = bgfx::createEmbeddedShader(s_embeddedShaders, rendererType, "vs_sky");
    bgfx::ShaderHandle fshSky = bgfx::createEmbeddedShader(s_embeddedShaders, rendererType, "fs_sky");
    skyProgram_ = bgfx::createProgram(vshSky, fshSky, true);
    uSkyTexColor_ = bgfx::createUniform("s_texColor", bgfx::UniformType::Sampler);
    // Two triangles covering the full screen in NDC (-1..1); drawn with
    // identity view/proj, so these positions ARE clip space directly, no
    // transform needed at runtime beyond the shader's own (identity) MVP
    // multiply. v=0 at the top of the screen samples row 0 of the sky
    // pixel buffer (zenith); v=1 at the bottom samples the last row
    // (horizon/haze) -- matches buildSkyPixels()'s row-major top-to-bottom
    // layout.
    const PosUvVertex skyVerts[6] = {
        {-1, 1, 1, 0, 0}, {1, 1, 1, 1, 0}, {1, -1, 1, 1, 1},
        {-1, 1, 1, 0, 0}, {1, -1, 1, 1, 1}, {-1, -1, 1, 0, 1},
    };
    skyVb_ = bgfx::createVertexBuffer(bgfx::copy(skyVerts, sizeof(skyVerts)), skyLayout_);

    // Phase 5e (PORT_PROGRESS.md): textured-lit program for the crowd-
    // atlas-textured front stand tiers -- shares the sun/hemi lighting
    // uniforms above (bgfx uniforms are looked up by name per-draw, not
    // tied to one program, and already-set values persist across
    // submit() calls, confirmed empirically since Phase 5d's ground/
    // ribbon/stadium draws already share these same handles) and reuses
    // uSkyTexColor_ as its sampler uniform (same name/type, a different
    // texture bound per draw call -- textures, unlike named uniforms, are
    // submit-scoped, so binding a different one per draw is the norm).
    texturedLitLayout_ = PosNormalUVVertex::layout();
    bgfx::ShaderHandle vshTexturedLit = bgfx::createEmbeddedShader(s_embeddedShaders, rendererType, "vs_textured_lit");
    bgfx::ShaderHandle fshTexturedLit = bgfx::createEmbeddedShader(s_embeddedShaders, rendererType, "fs_textured_lit");
    texturedLitProgram_ = bgfx::createProgram(vshTexturedLit, fshTexturedLit, true);

    // G17 (NT2003 presentation plan): the asphalt texture moved from here
    // to setTrack(). G5a built it once at init because nothing about it
    // varied by track; it now bakes the track's own `Stadium::seamEvery`
    // expansion-seam spacing, so it has to be rebuilt whenever the track
    // changes. See setTrack()'s own comment at the new build site.

    // Phase 5h (PORT_PROGRESS.md): the bloom+grade+tonemap postprocess
    // chain's 3 programs, each pairing a *fresh* vs_sky shader handle (a
    // second createEmbeddedShader() call, not the same handle skyProgram_
    // already consumed -- createProgram(..., true) destroys its shaders
    // when the program is destroyed, so each program needs its own vs_sky
    // instance) with its own fragment shader.
    bgfx::ShaderHandle vshBloomBright = bgfx::createEmbeddedShader(s_embeddedShaders, rendererType, "vs_sky");
    bgfx::ShaderHandle fshBloomBright = bgfx::createEmbeddedShader(s_embeddedShaders, rendererType, "fs_bloom_bright");
    bloomBrightProgram_ = bgfx::createProgram(vshBloomBright, fshBloomBright, true);
    bgfx::ShaderHandle vshBloomBlur = bgfx::createEmbeddedShader(s_embeddedShaders, rendererType, "vs_sky");
    bgfx::ShaderHandle fshBloomBlur = bgfx::createEmbeddedShader(s_embeddedShaders, rendererType, "fs_bloom_blur");
    bloomBlurProgram_ = bgfx::createProgram(vshBloomBlur, fshBloomBlur, true);
    bgfx::ShaderHandle vshGradeTonemap = bgfx::createEmbeddedShader(s_embeddedShaders, rendererType, "vs_sky");
    bgfx::ShaderHandle fshGradeTonemap = bgfx::createEmbeddedShader(s_embeddedShaders, rendererType, "fs_grade_tonemap");
    gradeTonemapProgram_ = bgfx::createProgram(vshGradeTonemap, fshGradeTonemap, true);
    uTexBloom_ = bgfx::createUniform("s_texBloom", bgfx::UniformType::Sampler);
    uBloomParams_ = bgfx::createUniform("u_bloomParams", bgfx::UniformType::Vec4);
    uGradeParams1_ = bgfx::createUniform("u_gradeParams1", bgfx::UniformType::Vec4);
    uGradeParams2_ = bgfx::createUniform("u_gradeParams2", bgfx::UniformType::Vec4);
    createPostFxTargets(width, height);

    // Step 3 (PORT_PROGRESS.md, physics-driven car rig animation): parse
    // the embedded placeholder car rig once (car_rig_data.h -- a box
    // chassis + 4 box wheels, generated by tools/gen_car_rig.py, not a real
    // Unreal-authored asset) and cache its joint list/wheel-joint indices
    // for every frame's computeBonePalette() call. Uses a hardcoded strlen
    // rather than storing a separate length constant -- kCarRigGltfJson is
    // a NUL-terminated C string literal, same as every other embedded-text
    // asset in this port (shaders_embedded.h, etc.).
    const MeshImportResult rigImport = importGltfFromMemory(kCarRigGltfJson, std::strlen(kCarRigGltfJson));
    if (rigImport.ok) {
        carMesh_.create(rigImport.mesh);
        carRigJoints_ = rigImport.mesh.joints;
        for (size_t i = 0; i < carRigJoints_.size(); ++i) {
            if (carRigJoints_[i].name == "wheel_FL") carWheelJointIndex_[0] = (int)i;
            else if (carRigJoints_[i].name == "wheel_FR") carWheelJointIndex_[1] = (int)i;
            else if (carRigJoints_[i].name == "wheel_RL") carWheelJointIndex_[2] = (int)i;
            else if (carRigJoints_[i].name == "wheel_RR") carWheelJointIndex_[3] = (int)i;
        }
    } else {
        std::fprintf(stderr, "car rig import failed: %s\n", rigImport.error.c_str());
    }

    // H4 (NT2003 engine-feel plan): particle billboard shader/texture --
    // particle_texture.h's procedural radial-gradient sprite (same
    // "no runtime asset files" convention as livery/sky/atlas), uploaded
    // once here since it never varies per track or per car.
    particleLayout_ = PosUvColorVertex::layout();
    bgfx::ShaderHandle vshParticle = bgfx::createEmbeddedShader(s_embeddedShaders, rendererType, "vs_particle");
    bgfx::ShaderHandle fshParticle = bgfx::createEmbeddedShader(s_embeddedShaders, rendererType, "fs_particle");
    particleProgram_ = bgfx::createProgram(vshParticle, fshParticle, true);
    uParticleTexColor_ = bgfx::createUniform("s_texColor", bgfx::UniformType::Sampler);
    {
        const std::vector<uint8_t> spritePixels = buildParticleSpritePixels();
        particleTexture_ =
            bgfx::createTexture2D((uint16_t)kParticleTextureSize, (uint16_t)kParticleTextureSize, false, 1,
                                  bgfx::TextureFormat::RGBA8, 0,
                                  bgfx::copy(spritePixels.data(), (uint32_t)spritePixels.size()));
    }

    return bgfx::isValid(program_) && bgfx::isValid(litProgram_) && bgfx::isValid(skyProgram_) &&
           bgfx::isValid(texturedLitProgram_) && bgfx::isValid(bloomBrightProgram_) &&
           bgfx::isValid(bloomBlurProgram_) && bgfx::isValid(gradeTonemapProgram_) && bgfx::isValid(sceneFb_) &&
           bgfx::isValid(particleProgram_);
}

void Renderer::createPostFxTargets(int width, int height) {
    if (bgfx::isValid(sceneFb_)) bgfx::destroy(sceneFb_);
    if (bgfx::isValid(bloomBrightFb_)) bgfx::destroy(bloomBrightFb_);
    if (bgfx::isValid(bloomBlurFb_)) bgfx::destroy(bloomBlurFb_);

    // RGBA16F when the backend can use it as a render-target format (lets
    // lit values exceed 1.0 without clipping before the tonemap pass sees
    // them); RGBA8 otherwise. This port's lighting rarely produces values
    // far above 1.0 anyway (no real HDR light sources), so RGBA8's
    // precision loss here is a minor, honest fallback, not a real quality
    // cliff -- exactly the "RGBA16F if available, else RGBA8" scope the
    // plan called for.
    const bgfx::Caps* caps = bgfx::getCaps();
    const bool rgba16fOk =
        (caps->formats[bgfx::TextureFormat::RGBA16F] & BGFX_CAPS_FORMAT_TEXTURE_FRAMEBUFFER) != 0;
    const bgfx::TextureFormat::Enum sceneFormat = rgba16fOk ? bgfx::TextureFormat::RGBA16F : bgfx::TextureFormat::RGBA8;

    const uint64_t colorFlags = BGFX_TEXTURE_RT | BGFX_SAMPLER_U_CLAMP | BGFX_SAMPLER_V_CLAMP;
    const uint16_t w = (uint16_t)std::max(1, width), h = (uint16_t)std::max(1, height);
    const bgfx::TextureHandle sceneColor = bgfx::createTexture2D(w, h, false, 1, sceneFormat, colorFlags);
    const bgfx::TextureHandle sceneDepth =
        bgfx::createTexture2D(w, h, false, 1, bgfx::TextureFormat::D24S8, colorFlags);
    bgfx::TextureHandle sceneAttachments[2] = {sceneColor, sceneDepth};
    sceneFb_ = bgfx::createFrameBuffer(2, sceneAttachments, true);

    // Half-res bright-pass + blur targets -- sampling the full-res scene
    // texture at half-res destination coordinates lets the GPU's own
    // bilinear filtering do the downsample for free (no explicit downsample
    // shader pass needed).
    const uint16_t bw = (uint16_t)std::max(1, width / 2), bh = (uint16_t)std::max(1, height / 2);
    const bgfx::TextureHandle brightColor = bgfx::createTexture2D(bw, bh, false, 1, sceneFormat, colorFlags);
    bloomBrightFb_ = bgfx::createFrameBuffer(1, &brightColor, true);
    const bgfx::TextureHandle blurColor = bgfx::createTexture2D(bw, bh, false, 1, sceneFormat, colorFlags);
    bloomBlurFb_ = bgfx::createFrameBuffer(1, &blurColor, true);

    // G23 (NT2003 presentation plan): the rearview mirror's own target, same
    // color+depth recipe as sceneFb_ but at a fixed kMirrorW x kMirrorH --
    // it is a small HUD inset, so backbuffer resolution would be waste, and
    // a fixed size keeps the rear camera's aspect (and therefore its
    // framing) stable across window resizes. Recreated here with the rest
    // only because this is where framebuffers live; nothing about it
    // actually depends on `width`/`height`.
    if (bgfx::isValid(mirrorFb_)) bgfx::destroy(mirrorFb_);
    const bgfx::TextureHandle mirrorColor =
        bgfx::createTexture2D(kMirrorW, kMirrorH, false, 1, sceneFormat, colorFlags);
    const bgfx::TextureHandle mirrorDepth =
        bgfx::createTexture2D(kMirrorW, kMirrorH, false, 1, bgfx::TextureFormat::D24S8, colorFlags);
    bgfx::TextureHandle mirrorAttachments[2] = {mirrorColor, mirrorDepth};
    mirrorFb_ = bgfx::createFrameBuffer(2, mirrorAttachments, true);
}

void Renderer::shutdown() {
    carMesh_.destroy();
    if (bgfx::isValid(trackVb_)) bgfx::destroy(trackVb_);
    if (bgfx::isValid(groundVb_)) bgfx::destroy(groundVb_);
    if (bgfx::isValid(program_)) bgfx::destroy(program_);
    if (bgfx::isValid(litProgram_)) bgfx::destroy(litProgram_);
    if (bgfx::isValid(uSunDir_)) bgfx::destroy(uSunDir_);
    if (bgfx::isValid(uSunColor_)) bgfx::destroy(uSunColor_);
    if (bgfx::isValid(uHemiSky_)) bgfx::destroy(uHemiSky_);
    if (bgfx::isValid(uHemiGround_)) bgfx::destroy(uHemiGround_);
    if (bgfx::isValid(uCamPos_)) bgfx::destroy(uCamPos_);
    if (bgfx::isValid(skyProgram_)) bgfx::destroy(skyProgram_);
    if (bgfx::isValid(uSkyTexColor_)) bgfx::destroy(uSkyTexColor_);
    if (bgfx::isValid(skyVb_)) bgfx::destroy(skyVb_);
    if (bgfx::isValid(shadowVb_)) bgfx::destroy(shadowVb_);
    if (bgfx::isValid(skyTexture_)) bgfx::destroy(skyTexture_);
    if (bgfx::isValid(asphaltTexture_)) bgfx::destroy(asphaltTexture_);
    if (bgfx::isValid(stadiumVb_)) bgfx::destroy(stadiumVb_);
    if (bgfx::isValid(texturedLitProgram_)) bgfx::destroy(texturedLitProgram_);
    if (bgfx::isValid(atlasTexture_)) bgfx::destroy(atlasTexture_);
    if (bgfx::isValid(stadiumTexturedVb_)) bgfx::destroy(stadiumTexturedVb_);
    for (auto& [num, tex] : carTextures_) {
        if (bgfx::isValid(tex)) bgfx::destroy(tex);
    }
    carTextures_.clear();
    if (bgfx::isValid(sceneFb_)) bgfx::destroy(sceneFb_);
    if (bgfx::isValid(bloomBrightFb_)) bgfx::destroy(bloomBrightFb_);
    if (bgfx::isValid(bloomBlurFb_)) bgfx::destroy(bloomBlurFb_);
    if (bgfx::isValid(mirrorFb_)) bgfx::destroy(mirrorFb_); // G23
    if (bgfx::isValid(bloomBrightProgram_)) bgfx::destroy(bloomBrightProgram_);
    if (bgfx::isValid(bloomBlurProgram_)) bgfx::destroy(bloomBlurProgram_);
    if (bgfx::isValid(gradeTonemapProgram_)) bgfx::destroy(gradeTonemapProgram_);
    if (bgfx::isValid(uTexBloom_)) bgfx::destroy(uTexBloom_);
    if (bgfx::isValid(uBloomParams_)) bgfx::destroy(uBloomParams_);
    if (bgfx::isValid(uGradeParams1_)) bgfx::destroy(uGradeParams1_);
    if (bgfx::isValid(uGradeParams2_)) bgfx::destroy(uGradeParams2_);
    if (bgfx::isValid(particleProgram_)) bgfx::destroy(particleProgram_);
    if (bgfx::isValid(uParticleTexColor_)) bgfx::destroy(uParticleTexColor_);
    if (bgfx::isValid(particleTexture_)) bgfx::destroy(particleTexture_);
    bgfx::shutdown();
    delete callback_;
    callback_ = nullptr;
}

void Renderer::setTrack(const Track& track) {
    track_ = &track;
    if (bgfx::isValid(trackVb_)) bgfx::destroy(trackVb_);
    if (bgfx::isValid(groundVb_)) bgfx::destroy(groundVb_);

    // Phase 5b (PORT_PROGRESS.md): resolve this track's ENV_PRESETS entry
    // (env_presets.h, a port of index.html:3520-3530's applyEnvPreset())
    // once here rather than per frame -- lighting is per-track data, not
    // per-frame. Replaces Phase 5a's hardcoded 'noon-grass' constants.
    {
        const EnvPreset& preset = resolveEnvPreset(track.stadium().env.preset);
        const Vec3d dir = envSunDirection(preset);
        sunDir_[0] = (float)dir.x;
        sunDir_[1] = (float)dir.y;
        sunDir_[2] = (float)dir.z;
        sunDir_[3] = 0.0f;
        sunColor_[0] = (float)(preset.sunColor[0] * preset.sunIntensity);
        sunColor_[1] = (float)(preset.sunColor[1] * preset.sunIntensity);
        sunColor_[2] = (float)(preset.sunColor[2] * preset.sunIntensity);
        sunColor_[3] = 0.0f;
        hemiSky_[0] = (float)(preset.hemiSky[0] * preset.hemiIntensity);
        hemiSky_[1] = (float)(preset.hemiSky[1] * preset.hemiIntensity);
        hemiSky_[2] = (float)(preset.hemiSky[2] * preset.hemiIntensity);
        hemiSky_[3] = 0.0f;
        hemiGround_[0] = (float)(preset.hemiGround[0] * preset.hemiIntensity);
        hemiGround_[1] = (float)(preset.hemiGround[1] * preset.hemiIntensity);
        hemiGround_[2] = (float)(preset.hemiGround[2] * preset.hemiIntensity);
        hemiGround_[3] = 0.0f;

        // Phase 5c (PORT_PROGRESS.md): rebuild the sky background texture
        // for this track (sky_texture.h's buildSkyPixels(), a port of
        // buildSkyTexture(), index.html:3724-3766) -- per-track data, not
        // per-frame, same rationale as the lighting uniforms just above.
        // Cedar Valley's `sky.silhouette=='hills'` hill silhouette is
        // deferred to Phase 5g (grouped with the other track-specific
        // special case, Big Sable's jumbotron/pylon) -- this sub-phase
        // only does the gradient+glow+clouds backdrop every track gets.
        if (bgfx::isValid(skyTexture_)) bgfx::destroy(skyTexture_);
        const std::vector<uint8_t> skyPixels = buildSkyPixels(track.stadium().sky, &preset);
        skyTexture_ = bgfx::createTexture2D((uint16_t)kSkyTextureWidth, (uint16_t)kSkyTextureHeight, false, 1,
                                             bgfx::TextureFormat::RGBA8, 0,
                                             bgfx::copy(skyPixels.data(), (uint32_t)skyPixels.size()));
    }

    // G17 (NT2003 presentation plan): rebuilt per track, not once at init
    // as G5a did -- it now bakes this track's own `Stadium::seamEvery`
    // expansion-seam spacing (a field authored per-track from the start but
    // never read by anything until now). hasMips=true from the start, same
    // "aliases on real mobile GPU without a mip chain" lesson already
    // learned once for the crowd atlas (the per-pixel speckle noise here is
    // exactly that kind of high-frequency content).
    {
        if (bgfx::isValid(asphaltTexture_)) bgfx::destroy(asphaltTexture_);
        const std::vector<uint8_t> asphaltPixels = buildAsphaltPixels(track.stadium().seamEvery);
        const std::vector<uint8_t> asphaltMips = buildRgba8MipChain(asphaltPixels, kAsphaltTextureSize);
        asphaltTexture_ = bgfx::createTexture2D((uint16_t)kAsphaltTextureSize, (uint16_t)kAsphaltTextureSize, true, 1,
                                                 bgfx::TextureFormat::RGBA8, 0,
                                                 bgfx::copy(asphaltMips.data(), (uint32_t)asphaltMips.size()));
    }

    const double halfW = track.halfW();
    const double total = track.total();
    const double step = 4.0;
    const int n = std::max(8, (int)std::ceil(total / step));

    // Phase 5a (PORT_PROGRESS.md): the ribbon is now a real banked 3D
    // surface (pos3()/surfH(), track_surface.h) instead of a flat Z=0 plane
    // -- banking raises the +lat (outside) edge, matching JS's own "3D
    // surface model (render only; physics stays planar)" (index.html:377).
    // G17 (NT2003 presentation plan) -- BUGFIX, the outer shoulder. The
    // ribbon spans lat -halfW..+halfW (-6..+6), but the outer wall stands at
    // wallLat() == halfW + 6 == 12 and the stands start at 18, so lat 6..12
    // had **no geometry at all** on any track. surfH() keeps rising across
    // that span (it clamps lat to wallLat, not to halfW), so the banked
    // ribbon edge ends several units *above* the only thing actually drawn
    // out there -- the flat grass plane at y = -0.05 -- leaving a visible
    // hole with the wall floating past it. On Milltown (14 deg bank) the
    // ribbon edge sits at y=3.01 and the wall base at y=4.49 over a 6-unit
    // lateral gap. Extending each slice out to wallLat closes it and gives
    // the wall a base to meet, using the same pos3() the ribbon already
    // uses so the shoulder follows the banking exactly.
    struct EdgePair {
        Vec3 inner, outer, shoulder;
    };
    std::vector<EdgePair> pts(n);
    float minx = 1e9f, maxx = -1e9f, minz = 1e9f, maxz = -1e9f;
    const double shoulderLat = wallLat(track);
    for (int i = 0; i < n; ++i) {
        const double s = (double)i / n * total;
        const Vec3 inner = pos3(track, s, -halfW);
        const Vec3 outer = pos3(track, s, halfW);
        const Vec3 shoulder = pos3(track, s, shoulderLat);
        pts[i] = {inner, outer, shoulder};
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

    // G5a (NASCAR-Thunder gap-analysis plan, track surface texture): the
    // ribbon used to get one hardcoded flat vertex color (the old `asphalt`
    // constant here) -- now it's UV'd into the tileable asphalt texture
    // (track_surface_texture.h) instead, via texturedLitLayout_/
    // texturedLitProgram_ (already existed for the crowd-atlas stand
    // seats). U wraps `s / kAsphaltTileLength` into [0,1) IN SOFTWARE
    // (std::fmod, not relying on hardware texture wrap) since this port
    // never needs the GPU to see a U outside [0,1) -- V is the lateral
    // fraction across the ribbon's own fixed width (0 = inner edge, 1 =
    // outer/wall edge), matching where the asphalt texture's groove/apron
    // bands are painted.
    // G17: tile length is now the track's own `Stadium::seamEvery` (see
    // asphaltTileLength()), so one expansion seam per tile lands seams at
    // real per-track spacing instead of every hardcoded 8 units.
    const double asphaltTile = asphaltTileLength(track.stadium().seamEvery);
    auto wrapU = [asphaltTile](double s) {
        double t = std::fmod(s / asphaltTile, 1.0);
        return t < 0.0 ? t + 1.0 : t;
    };
    std::vector<PosNormalUVVertex> verts;
    verts.reserve((size_t)n * 12);
    for (int i = 0; i < n; ++i) {
        const EdgePair& a = pts[i];
        const EdgePair& b = pts[(i + 1) % n];
        const double sa = (double)i / n * total;
        const double sb = (double)(i + 1) / n * total;
        const double ua = wrapU(sa), ub = wrapU(sb);
        constexpr double vInner = 0.0, vOuter = 1.0;
        // The shoulder re-samples the texture over a band that is clear of
        // every painted feature, so it reads as plain asphalt running out to
        // the wall while still getting real speckle variation across its
        // 6-unit width. The clean window is V 0.68..0.94: the high groove
        // tops out at 0.65 and the white outer line starts at 0.952. A
        // first pass ramped 1.0 -> 0.60 and visibly **re-crossed the white
        // line**, painting a second stripe out on the shoulder (caught in
        // the first WASM screenshot). The small V discontinuity at
        // lat=halfW (racing surface ends at 1.0, shoulder starts at 0.94)
        // is invisible because both sides are plain speckle there -- and it
        // is the correct real-world read anyway: the white line is painted
        // at the outer edge of the racing surface, with bare apron beyond.
        constexpr double vShoulderIn = 0.94, vShoulderOut = 0.68;
        // Two triangles per ribbon segment: (inner_a, outer_a, outer_b) and
        // (inner_a, outer_b, inner_b). Triangle list, not a strip -- a
        // closed loop's wraparound is simpler to get right this way, and a
        // few hundred duplicated vertices costs nothing here.
        const Vec3 n1 = faceNormal(a.inner, a.outer, b.outer);
        const Vec3 n2 = faceNormal(a.inner, b.outer, b.inner);
        verts.push_back({(float)a.inner.x, (float)a.inner.y, (float)a.inner.z,
                          (float)n1.x, (float)n1.y, (float)n1.z, (float)ua, (float)vInner});
        verts.push_back({(float)a.outer.x, (float)a.outer.y, (float)a.outer.z,
                          (float)n1.x, (float)n1.y, (float)n1.z, (float)ua, (float)vOuter});
        verts.push_back({(float)b.outer.x, (float)b.outer.y, (float)b.outer.z,
                          (float)n1.x, (float)n1.y, (float)n1.z, (float)ub, (float)vOuter});
        verts.push_back({(float)a.inner.x, (float)a.inner.y, (float)a.inner.z,
                          (float)n2.x, (float)n2.y, (float)n2.z, (float)ua, (float)vInner});
        verts.push_back({(float)b.outer.x, (float)b.outer.y, (float)b.outer.z,
                          (float)n2.x, (float)n2.y, (float)n2.z, (float)ub, (float)vOuter});
        verts.push_back({(float)b.inner.x, (float)b.inner.y, (float)b.inner.z,
                          (float)n2.x, (float)n2.y, (float)n2.z, (float)ub, (float)vInner});
        // Shoulder: outer ribbon edge -> wall base, same two-triangle split.
        const Vec3 n3 = faceNormal(a.outer, a.shoulder, b.shoulder);
        const Vec3 n4 = faceNormal(a.outer, b.shoulder, b.outer);
        verts.push_back({(float)a.outer.x, (float)a.outer.y, (float)a.outer.z,
                          (float)n3.x, (float)n3.y, (float)n3.z, (float)ua, (float)vShoulderIn});
        verts.push_back({(float)a.shoulder.x, (float)a.shoulder.y, (float)a.shoulder.z,
                          (float)n3.x, (float)n3.y, (float)n3.z, (float)ua, (float)vShoulderOut});
        verts.push_back({(float)b.shoulder.x, (float)b.shoulder.y, (float)b.shoulder.z,
                          (float)n3.x, (float)n3.y, (float)n3.z, (float)ub, (float)vShoulderOut});
        verts.push_back({(float)a.outer.x, (float)a.outer.y, (float)a.outer.z,
                          (float)n4.x, (float)n4.y, (float)n4.z, (float)ua, (float)vShoulderIn});
        verts.push_back({(float)b.shoulder.x, (float)b.shoulder.y, (float)b.shoulder.z,
                          (float)n4.x, (float)n4.y, (float)n4.z, (float)ub, (float)vShoulderOut});
        verts.push_back({(float)b.outer.x, (float)b.outer.y, (float)b.outer.z,
                          (float)n4.x, (float)n4.y, (float)n4.z, (float)ub, (float)vShoulderIn});
    }

    trackVb_ = bgfx::createVertexBuffer(
        bgfx::copy(verts.data(), (uint32_t)(verts.size() * sizeof(PosNormalUVVertex))), texturedLitLayout_);
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

    // Phase 5b (PORT_PROGRESS.md): a large flat ground plane colored by
    // this track's theme.grass (index.html's per-track `theme:{grass:...}}`)
    // -- the first real use of per-track color data, and something for the
    // new lighting model to shade besides the ribbon itself. Sized well
    // past the track's own extent (6x the top-down framing half-size) so
    // it fills the frame in both TopDown and Chase views; sits a hair below
    // the ribbon's own lowest point (apron height, always >= 0.02, see
    // surfH()) to avoid z-fighting at the ribbon's edges.
    {
        const auto& grass = track.theme().grass;
        const uint32_t grassColor = packColor((float)grass[0], (float)grass[1], (float)grass[2]);
        const float half = std::max(topHalfW_, topHalfH_) * 3.0f;
        const float gx0 = topCx_ - half, gx1 = topCx_ + half;
        const float gz0 = topCy_ - half, gz1 = topCy_ + half;
        const float gy = -0.05f;
        const PosNormalColorVertex v00{gx0, gy, gz0, 0, 1, 0, grassColor};
        const PosNormalColorVertex v10{gx1, gy, gz0, 0, 1, 0, grassColor};
        const PosNormalColorVertex v11{gx1, gy, gz1, 0, 1, 0, grassColor};
        const PosNormalColorVertex v01{gx0, gy, gz1, 0, 1, 0, grassColor};
        const PosNormalColorVertex groundVerts[6] = {v00, v10, v11, v00, v11, v01};
        groundVb_ = bgfx::createVertexBuffer(bgfx::copy(groundVerts, sizeof(groundVerts)), litLayout_);
        groundVertexCount_ = 6;
    }

    // Phase 5d/5e (PORT_PROGRESS.md): stands (front/back/corner x2) + pit
    // road + the outer wall, combined into one static flat-colored buffer,
    // plus a second static buffer for the front-tier crowd-textured stand
    // seats (Phase 5e). One shared Mulberry32(777) scenery-RNG stream
    // across all 4 stand calls, matching JS's own rng2 consumption order
    // (front, back, corner, corner) -- cosmetic only, see stadium_mesh.h
    // for why this doesn't affect gameplay determinism. The atlas texture
    // gets its own independent Mulberry32(777) stream (paintCrowdTile()'s
    // own rng2 use in JS runs at a different, unrelated call site --
    // there's no cross-feature visual consistency requirement to preserve
    // here, only "safe to diverge" scenery randomness).
    if (bgfx::isValid(stadiumVb_)) bgfx::destroy(stadiumVb_);
    if (bgfx::isValid(stadiumTexturedVb_)) bgfx::destroy(stadiumTexturedVb_);
    if (bgfx::isValid(atlasTexture_)) bgfx::destroy(atlasTexture_);
    {
        const Stadium& st = track.stadium();
        Mulberry32 sceneryRng(777);
        std::vector<MeshVertex> mesh;
        std::vector<MeshVertex> texturedMesh;
        auto append = [&](std::vector<MeshVertex>&& v) {
            mesh.insert(mesh.end(), v.begin(), v.end());
        };
        // G5b (NASCAR-Thunder gap-analysis plan): buildOuterWallMesh() and
        // buildSponsorPanelsMesh() now carry real atlas UVs (wall-diamond/
        // sponsor-panel regions) instead of a flat color, so their output
        // goes into texturedMesh instead of the flat mesh -- same
        // destination `appendStand()`'s own `.textured` output already uses.
        auto appendTextured = [&](std::vector<MeshVertex>&& v) {
            texturedMesh.insert(texturedMesh.end(), v.begin(), v.end());
        };
        auto appendStand = [&](StandMeshResult&& r) {
            mesh.insert(mesh.end(), r.flat.begin(), r.flat.end());
            texturedMesh.insert(texturedMesh.end(), r.textured.begin(), r.textured.end());
        };
        const std::array<double, 4> crowdUV = atlasUV(kAtlasCrowd);
        const Seg& seg0 = track.segs()[0];
        const Seg& seg1 = track.segs()[1];
        const Seg& seg2 = track.segs()[2];
        const Seg& seg3 = track.segs()[3];
        // Phase 5g (PORT_PROGRESS.md): Cedar Valley's hill silhouette
        // (`sky.silhouette=='hills'`) -- a no-op empty mesh for every other
        // track. Called here, ahead of the stand builds, mirroring JS's own
        // buildWorld() call order (index.html:2056, before the grandstand
        // block) even though this port's shared scenery-RNG stream doesn't
        // require matching call order (see stadium_mesh.h's own "safe to
        // diverge" precedent).
        if (st.sky.silhouette == "hills") append(buildHillSilhouette(sceneryRng));
        // G9 (NASCAR-Thunder gap-analysis plan, stadium bug fix): straight
        // margin shrunk 0.12/0.88 -> 0.03/0.97 and cornerCov (below) raised
        // -- the old values left a real, unflagged bare-dirt gap at every
        // straight->corner transition (worst on partial-reach tracks,
        // where the two margins compounded to ~22.5% of the corner arc
        // left uncovered at each end). A little margin is kept rather than
        // closed to zero so it still reads as an intentional access point,
        // not a hole.
        appendStand(buildStandMesh(track, seg0.s0 + seg0.len * 0.03, seg0.s0 + seg0.len * 0.97, st.standTier.front,
                                    st.crowdTiers, st.standDensity, st.standScale.tierD, st.standScale.tierH,
                                    st.crowdPalette, crowdUV, sceneryRng));
        // G14 (NASCAR-Thunder gap-analysis plan): a suite/press-box tower
        // behind the front-tier stand -- same standTier.front/tierD/tierH
        // values as the call directly above, so the tower's position
        // always derives from the actual stand geometry it sits behind.
        append(buildSuiteTowerMesh(track, st.standTier.front, st.standScale.tierD, st.standScale.tierH));
        appendStand(buildStandMesh(track, seg2.s0 + seg2.len * 0.03, seg2.s0 + seg2.len * 0.97, st.standTier.back,
                                    st.crowdTiers, st.standDensity, st.standScale.tierD, st.standScale.tierH,
                                    st.crowdPalette, crowdUV, sceneryRng));
        // Every track gets corner seating (index.html:2067-2075's own
        // comment on this): coverage fraction depends on standReach, not
        // whether it's "full" only.
        const double cornerCov = st.standReach == "full" ? 0.97 : 0.85;
        const double pad = (1.0 - cornerCov) / 2.0;
        appendStand(buildStandMesh(track, seg1.s0 + seg1.len * pad, seg1.s0 + seg1.len * (1 - pad),
                                    st.standTier.corner, st.crowdTiers, st.standDensity, st.standScale.tierD,
                                    st.standScale.tierH, st.crowdPalette, crowdUV, sceneryRng));
        appendStand(buildStandMesh(track, seg3.s0 + seg3.len * pad, seg3.s0 + seg3.len * (1 - pad),
                                    st.standTier.corner, st.crowdTiers, st.standDensity, st.standScale.tierD,
                                    st.standScale.tierH, st.crowdPalette, crowdUV, sceneryRng));
        // G18 (NT2003 presentation plan): cap each stand zone with a roof
        // line (sponsor band + press-box fascia + roof cap). Same zone
        // extents and same tier/tierD/tierH values as the matching
        // buildStandMesh() call above each one, so the roof always lands on
        // top of the stand it belongs to.
        append(buildStandRoofMesh(track, seg0.s0 + seg0.len * 0.03, seg0.s0 + seg0.len * 0.97, st.standTier.front,
                                   st.standScale.tierD, st.standScale.tierH, track.theme().wall));
        append(buildStandRoofMesh(track, seg2.s0 + seg2.len * 0.03, seg2.s0 + seg2.len * 0.97, st.standTier.back,
                                   st.standScale.tierD, st.standScale.tierH, track.theme().wall));
        append(buildStandRoofMesh(track, seg1.s0 + seg1.len * pad, seg1.s0 + seg1.len * (1 - pad),
                                   st.standTier.corner, st.standScale.tierD, st.standScale.tierH,
                                   track.theme().wall));
        append(buildStandRoofMesh(track, seg3.s0 + seg3.len * pad, seg3.s0 + seg3.len * (1 - pad),
                                   st.standTier.corner, st.standScale.tierD, st.standScale.tierH,
                                   track.theme().wall));
        // PIT_OUT/PIT_IN (index.html:1937): sized to hold both pit AI lanes
        // (moving lane at lat=-8.4, stall lane at lat=-10.5).
        append(buildPitRoadMesh(track, -7.2, -11.8));
        // G19 (NT2003 presentation plan): wire the previously-dead crew
        // atlas region into real billboards, one per pit stall.
        appendTextured(buildPitCrewMesh(track));
        appendTextured(buildOuterWallMesh(track));
        // G10 (NASCAR-Thunder gap-analysis plan): wire the previously-dead
        // catch-fence atlas region into real geometry.
        appendTextured(buildCatchFenceMesh(track, st.fenceHeight));
        // G11 (NASCAR-Thunder gap-analysis plan): thread the previously-
        // dead sponsorDensity field into panel spacing.
        appendTextured(buildSponsorPanelsMesh(track, st.sponsorDensity));
        // G12 (NASCAR-Thunder gap-analysis plan): turn-number signage at
        // each corner apex, previously bare of any signage.
        append(buildTurnSignageMesh(track));
        // G17 (NT2003 presentation plan): resurfacing patches, wiring the
        // previously-dead Stadium::patches. Appended before the start/finish
        // stripe so the checker always wins wherever the two overlap.
        append(buildSurfacePatchesMesh(track, st.patches, sceneryRng));
        // G5a (NASCAR-Thunder gap-analysis plan, track surface texture):
        // the checkered start/finish stripe, flat vertex-colored (see this
        // function's own comment in stadium_mesh.cpp for why).
        append(buildStartFinishMesh(track));
        // G13 (NASCAR-Thunder gap-analysis plan): a flag stand beside the
        // start/finish line.
        append(buildFlagStandMesh(track));
        // Phase 5g (PORT_PROGRESS.md): Big Sable's scoring pylon + jumbotron
        // (`stadium.pylon`/`stadium.jumbotron`) -- both no-op empty meshes on
        // every other track, so safe to call unconditionally.
        append(buildPylonMesh(track));
        append(buildJumbotronMesh(track));

        std::vector<PosNormalColorVertex> stadiumVerts;
        stadiumVerts.reserve(mesh.size());
        for (const MeshVertex& v : mesh) {
            stadiumVerts.push_back({(float)v.x, (float)v.y, (float)v.z, (float)v.nx, (float)v.ny, (float)v.nz,
                                     packColor((float)v.color[0], (float)v.color[1], (float)v.color[2])});
        }
        stadiumVb_ = bgfx::createVertexBuffer(
            bgfx::copy(stadiumVerts.data(), (uint32_t)(stadiumVerts.size() * sizeof(PosNormalColorVertex))),
            litLayout_);
        stadiumVertexCount_ = (uint32_t)stadiumVerts.size();

        std::vector<PosNormalUVVertex> texturedVerts;
        texturedVerts.reserve(texturedMesh.size());
        for (const MeshVertex& v : texturedMesh) {
            texturedVerts.push_back({(float)v.x, (float)v.y, (float)v.z, (float)v.nx, (float)v.ny, (float)v.nz,
                                      (float)v.u, (float)v.v});
        }
        if (!texturedVerts.empty()) {
            stadiumTexturedVb_ = bgfx::createVertexBuffer(
                bgfx::copy(texturedVerts.data(), (uint32_t)(texturedVerts.size() * sizeof(PosNormalUVVertex))),
                texturedLitLayout_);
        }
        stadiumTexturedVertexCount_ = (uint32_t)texturedVerts.size();

        Mulberry32 atlasRng(777);
        const std::vector<uint8_t> atlasPixels =
            buildAtlasPixels(track.theme().wall, st.crowdPalette, st.crowdFill, atlasRng);
        // hasMips=true (was false): the crowd-tile region (paintCrowdTile(),
        // atlas_texture.cpp) is per-8px-cell random noise -- exactly the
        // kind of high-frequency content that aliases badly under
        // minification without a mip chain, reading as a big patch of
        // TV-static-like flicker on real devices rather than subtle
        // background crowd texture. Built by hand (buildRgba8MipChain())
        // rather than requesting bgfx/bimg auto-generate it, since
        // createTexture2D's hasMips flag only documents "texture already
        // contains a full mip chain in _mem" -- it doesn't generate one.
        const std::vector<uint8_t> atlasMips = buildRgba8MipChain(atlasPixels, kAtlasSize);
        atlasTexture_ = bgfx::createTexture2D((uint16_t)kAtlasSize, (uint16_t)kAtlasSize, true, 1,
                                               bgfx::TextureFormat::RGBA8, 0,
                                               bgfx::copy(atlasMips.data(), (uint32_t)atlasMips.size()));
    }

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
    // Phase 5h (PORT_PROGRESS.md): bgfx::reset() resizes the real backbuffer
    // automatically, but the offscreen postfx targets are ordinary
    // fixed-size framebuffers -- they need an explicit rebuild here or
    // they'd stay stuck at the old resolution (and eventually mismatch the
    // world view's viewport rect entirely).
    createPostFxTargets(width, height);
}

void Renderer::requestScreenshot(const char* path) {
    bgfx::requestScreenShot(BGFX_INVALID_HANDLE, path);
}

void Renderer::renderBlockedFrame() {
    const bgfx::ViewId kView = 0;
    // Phase 5h (PORT_PROGRESS.md): view 0 is renderFrame()'s kSkyView, which
    // that function points at the offscreen sceneFb_ whenever the sky
    // paints -- a view's frame buffer assignment persists across frames
    // until changed, so this MUST be reset to the real backbuffer
    // explicitly, or a renderBlockedFrame() call right after a renderFrame()
    // call would silently render this black screen into sceneFb_ instead
    // (never reaching the display).
    bgfx::setViewFrameBuffer(kView, BGFX_INVALID_HANDLE);
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

bgfx::TextureHandle Renderer::getOrBuildCarTexture(const Car& car) {
    auto it = carTextures_.find(car.num);
    if (it != carTextures_.end()) return it->second;
    const std::vector<uint8_t> pixels = buildLiveryPixels(car.col, car.num, car.idx, car.scheme);
    // G1b (NASCAR-Thunder gap-analysis plan, car UV/livery fix): mips now
    // that side panels actually get sampled at oblique angles (real UVs
    // instead of one flat texel) -- same "aliases on real mobile GPU
    // without a mip chain" lesson already learned once for the crowd atlas.
    const std::vector<uint8_t> liveryMips = buildRgba8MipChain(pixels, kLiveryTextureSize);
    const bgfx::TextureHandle tex =
        bgfx::createTexture2D((uint16_t)kLiveryTextureSize, (uint16_t)kLiveryTextureSize, true, 1,
                               bgfx::TextureFormat::RGBA8, 0, bgfx::copy(liveryMips.data(), (uint32_t)liveryMips.size()));
    carTextures_[car.num] = tex;
    return tex;
}

// G20 (NT2003 presentation plan): the pace car's own livery. Cached in the
// same map as the field, under a key no real car can take -- `Car::num` is
// a 1-2 digit race number throughout (livery.cpp's drawNumber() only
// renders 1-2 digits at all), so a negative key can never collide. Built
// through the same buildLiveryPixels() path as everything else rather than
// a special-cased texture, so it picks up every G16 detail (lamp clusters,
// number panels, sun strip) for free. livery.h's own note said "No
// pace-car variant: this port has no pace-car visual yet ... so there is
// nothing to build one for" -- that stopped being true here.
bgfx::TextureHandle Renderer::getOrBuildPaceTexture() {
    constexpr int kPaceKey = -1;
    auto it = carTextures_.find(kPaceKey);
    if (it != carTextures_.end()) return it->second;
    // Safety-car look: near-white body, and stripe/mask styles picked
    // explicitly rather than left to the idx-derived default so it reads
    // distinct from any car in the field.
    const LiveryScheme paceScheme{0, 0, 0, CarPalette::Yellow};
    const std::vector<uint8_t> pixels =
        buildLiveryPixels(Color3{0.93, 0.93, 0.95}, 0, 0, &paceScheme, /*paceLightBar=*/true);
    const std::vector<uint8_t> liveryMips = buildRgba8MipChain(pixels, kLiveryTextureSize);
    const bgfx::TextureHandle tex =
        bgfx::createTexture2D((uint16_t)kLiveryTextureSize, (uint16_t)kLiveryTextureSize, true, 1,
                               bgfx::TextureFormat::RGBA8, 0, bgfx::copy(liveryMips.data(), (uint32_t)liveryMips.size()));
    carTextures_[kPaceKey] = tex;
    return tex;
}

// G23 (NT2003 presentation plan): see renderer.h for why the car draws are
// resolved into plain data once per frame instead of being recomputed per
// view (wall-clock wheel state must advance exactly once; bone-matrix
// uploads must stay 1:1 with draws).
struct Renderer::WorldDrawList {
    struct Item {
        Mat4f model;
        std::vector<float> bones;
        int boneCount = 0;
        bgfx::TextureHandle texture = BGFX_INVALID_HANDLE;
    };
    std::vector<Item> cars;
    // H3: one model matrix per car's contact-shadow decal (carShadowModelMat()),
    // parallel to `cars` above but kept separate -- the shadow disc is a
    // plain static VB with no bone palette/texture, so it doesn't need
    // Item's other fields.
    std::vector<Mat4f> shadows;
    // H4 (NT2003 engine-feel plan): the frame's live particle list (tire
    // smoke/sparks/engine smoke), owned by main.cpp -- not a copy. Each
    // view (main, mirror) builds its OWN billboard quads from this same
    // shared list inside submitWorld(), since a billboard's screen-facing
    // orientation depends on that view's own camera, unlike the shadow/car
    // matrices above which are camera-independent world transforms.
    const std::vector<Particle>* particles = nullptr;
};

void Renderer::submitWorld(bgfx::ViewId viewId, const float view[16], const float proj[16],
                            const float eye[3], const WorldDrawList& draws) {
    bgfx::setViewTransform(viewId, view, proj);
    const float camPos[4] = {eye[0], eye[1], eye[2], 0.0f};
    bgfx::setUniform(uCamPos_, camPos);

    // Phase 5b (PORT_PROGRESS.md): sun/hemisphere lighting uniforms, real
    // per-track data resolved once in setTrack() (env_presets.h). Sun
    // direction is TOWARD the sun (matches fs_lit.sc's `dot(n, u_sunDir)`).
    // Set here rather than once per frame so each view's draws carry the
    // values alongside that view's own camera position.
    bgfx::setUniform(uSunDir_, sunDir_);
    bgfx::setUniform(uSunColor_, sunColor_);
    bgfx::setUniform(uHemiSky_, hemiSky_);
    bgfx::setUniform(uHemiGround_, hemiGround_);

    float identity[16];
    bx::mtxIdentity(identity);

    // Phase 5a (PORT_PROGRESS.md): real depth testing, now that world-space
    // geometry has genuine 3D extent (banked ribbon, stadium/stands) --
    // layering previously relied purely on submission order.
    const uint64_t state =
        BGFX_STATE_WRITE_RGB | BGFX_STATE_WRITE_A | BGFX_STATE_WRITE_Z | BGFX_STATE_DEPTH_TEST_LESS;

    // Phase 5b: the ground plane draws first (painter's-algorithm ordering,
    // though depth testing makes the exact order moot) so the ribbon and cars
    // are never occluded by it.
    if (groundVertexCount_ > 0) {
        bgfx::setTransform(identity);
        bgfx::setVertexBuffer(0, groundVb_, 0, groundVertexCount_);
        bgfx::setState(state);
        bgfx::submit(viewId, litProgram_);
    }

    // G5a: the ribbon is UV'd into asphaltTexture_ (rebuilt per track in
    // setTrack()) instead of a flat vertex color -- same
    // texturedLitProgram_/uSkyTexColor_ pattern the crowd-atlas stand seats
    // use below, just a different texture bound.
    if (trackVertexCount_ > 0 && bgfx::isValid(asphaltTexture_)) {
        bgfx::setTransform(identity);
        bgfx::setVertexBuffer(0, trackVb_, 0, trackVertexCount_);
        bgfx::setTexture(0, uSkyTexColor_, asphaltTexture_);
        bgfx::setState(state);
        bgfx::submit(viewId, texturedLitProgram_);
    }

    // Phase 5d: stands + pit road + outer wall, drawn after the ribbon.
    if (stadiumVertexCount_ > 0) {
        bgfx::setTransform(identity);
        bgfx::setVertexBuffer(0, stadiumVb_, 0, stadiumVertexCount_);
        bgfx::setState(state);
        bgfx::submit(viewId, litProgram_);
    }

    // Phase 5e: front-tier stand seats, crowd-atlas textured -- same lit
    // state as everything else, just a different program/layout/texture.
    if (stadiumTexturedVertexCount_ > 0 && bgfx::isValid(atlasTexture_)) {
        bgfx::setTransform(identity);
        bgfx::setVertexBuffer(0, stadiumTexturedVb_, 0, stadiumTexturedVertexCount_);
        bgfx::setTexture(0, uSkyTexColor_, atlasTexture_);
        bgfx::setState(state);
        bgfx::submit(viewId, texturedLitProgram_);
    }

    // H3 (NT2003 engine-feel plan): contact shadows, drawn before the opaque
    // car bodies (matches carShadowMat()'s renderOrder=-1 intent -- sit on
    // the track surface, with the cars on top of them) and after every
    // opaque world layer above, so they blend over real ground/asphalt/
    // stadium color rather than the view's raw clear color. Alpha-blended,
    // depth-tested but NOT depth-writing (JS's `depthWrite:false`) -- so a
    // shadow whose footprint is occluded by, say, the stadium wall still
    // gets correctly hidden, without punching a hole in the depth buffer
    // for whatever draws after it.
    if (shadowVertexCount_ > 0 && !draws.shadows.empty()) {
        const uint64_t shadowState =
            BGFX_STATE_WRITE_RGB | BGFX_STATE_WRITE_A | BGFX_STATE_DEPTH_TEST_LESS | BGFX_STATE_BLEND_ALPHA;
        for (const auto& model : draws.shadows) {
            bgfx::setTransform(model.data());
            bgfx::setVertexBuffer(0, shadowVb_, 0, shadowVertexCount_);
            bgfx::setState(shadowState);
            bgfx::submit(viewId, program_);
        }
    }

    // Cars (and, when it is on track, the pace car) from the prebuilt list.
    // setBoneMatrices() is re-uploaded immediately before each draw() so the
    // pairing bgfx's checked build requires holds for every view.
    for (const auto& item : draws.cars) {
        SkinnedMesh::setBoneMatrices(item.bones.data(), item.boneCount);
        carMesh_.draw(viewId, item.model.data(), item.texture);
    }

    // H4 (NT2003 engine-feel plan): particle billboards, drawn last so
    // smoke/sparks read on top of the opaque cars they trail behind rather
    // than being occluded by them. Camera-facing: each quad's corners are
    // built here, per view, from THIS view's own right/up basis --
    // extracted directly from `view`'s rows (view[0],view[4],view[8] =
    // world-space camera right; view[1],view[5],view[9] = world-space
    // camera up, for any right-handed lookAt matrix, which both the main
    // and mirror cameras are -- see H3's own camera-handedness comment for
    // why that's guaranteed here) rather than needing a new parameter,
    // since a billboard's orientation is inherently per-camera, unlike the
    // shadow/car matrices above which are camera-independent world
    // transforms.
    if (draws.particles && !draws.particles->empty() && bgfx::isValid(particleTexture_)) {
        const float rightX = view[0], rightY = view[4], rightZ = view[8];
        const float upX = view[1], upY = view[5], upZ = view[9];

        std::vector<PosUvColorVertex> smokeVerts, sparkVerts;
        for (const Particle& p : *draws.particles) {
            // JS's own per-frame size formula (index.html:3433-3436): grows
            // gently over life for smoke (size>=0.1), then both kinds
            // shrink out over roughly the last third of their life. `p.size`
            // itself (the spawn-time half-width) never changes -- only this
            // per-frame derived `s` does.
            const double g = 1.0 - p.age / p.life;
            double s = p.size;
            if (p.size >= 0.1) s *= 1.0 + p.age * 1.3;
            s *= std::min(1.0, g * 3.0);
            if (s <= 0.0) continue;

            const bool spark = p.size < 0.15;
            const float batchAlpha = spark ? 0.9f : 0.38f; // JS's own uAlpha uniforms
            const uint32_t col = packColor((float)p.col[0], (float)p.col[1], (float)p.col[2], batchAlpha);
            const float cx = (float)p.x, cy = (float)p.y, cz = (float)p.z, sf = (float)s;
            const float rx = rightX * sf, ry = rightY * sf, rz = rightZ * sf;
            const float ux = upX * sf, uy = upY * sf, uz = upZ * sf;

            std::vector<PosUvColorVertex>& out = spark ? sparkVerts : smokeVerts;
            out.push_back({cx - rx - ux, cy - ry - uy, cz - rz - uz, 0.0f, 0.0f, col});
            out.push_back({cx + rx - ux, cy + ry - uy, cz + rz - uz, 1.0f, 0.0f, col});
            out.push_back({cx + rx + ux, cy + ry + uy, cz + rz + uz, 1.0f, 1.0f, col});
            out.push_back({cx - rx - ux, cy - ry - uy, cz - rz - uz, 0.0f, 0.0f, col});
            out.push_back({cx + rx + ux, cy + ry + uy, cz + rz + uz, 1.0f, 1.0f, col});
            out.push_back({cx - rx + ux, cy - ry + uy, cz - rz + uz, 0.0f, 1.0f, col});
        }

        // Depth-tested (a particle genuinely behind the stadium wall/track
        // geometry should be hidden) but not depth-writing (overlapping
        // particles blend with each other rather than occluding). Two
        // blend states -- premultiplied "normal" for smoke, additive for
        // sparks -- sharing one shader; see fs_particle.sc's own comment
        // for why premultiplied output is what makes that work.
        const uint64_t baseState = BGFX_STATE_WRITE_RGB | BGFX_STATE_WRITE_A | BGFX_STATE_DEPTH_TEST_LESS;
        auto drawBatch = [&](std::vector<PosUvColorVertex>& verts, uint64_t blend) {
            const uint32_t count = (uint32_t)verts.size();
            if (count == 0 || bgfx::getAvailTransientVertexBuffer(count, particleLayout_) < count) return;
            bgfx::TransientVertexBuffer tvb;
            bgfx::allocTransientVertexBuffer(&tvb, count, particleLayout_);
            std::memcpy(tvb.data, verts.data(), count * sizeof(PosUvColorVertex));
            bgfx::setTransform(identity);
            bgfx::setVertexBuffer(0, &tvb, 0, count);
            bgfx::setTexture(0, uParticleTexColor_, particleTexture_);
            bgfx::setState(baseState | blend);
            bgfx::submit(viewId, particleProgram_);
        };
        drawBatch(smokeVerts, BGFX_STATE_BLEND_NORMAL);
        drawBatch(sparkVerts, BGFX_STATE_BLEND_ADD);
    }
}

void Renderer::renderFrame(const RaceState& raceState, const std::vector<Car>& cars, double renderAlpha,
                            const PaceCar* pace, const MenuSelection* menu, const std::string* menuTrackName,
                            const std::vector<Car*>* finishOrder, const std::vector<Particle>* particles) {
    // Phase 5c (PORT_PROGRESS.md): a new view (id 0, numerically below the
    // world view) for the sky background -- bgfx renders views in ascending
    // ID order regardless of submission order, so this MUST be a lower ID
    // than kView for the sky to end up behind the ribbon/cars. Shifted the
    // world/UI views up by one (0->1, 1->2) to make room; renderBlockedFrame()
    // is a separate, mutually-exclusive screen and keeps its own view 0
    // unchanged.
    const bgfx::ViewId kSkyView = 0;
    const bgfx::ViewId kView = 1;
    // G23 (NT2003 presentation plan): the rearview mirror renders the world a
    // second time into mirrorFb_. bgfx executes views in ascending ID order
    // regardless of submission order, so this must sit ABOVE the main world
    // view (its content is the same frame's) and BELOW the UI view that
    // samples the result -- a mirror numbered above its consumer would show
    // the previous frame. Bloom/blur/grade and UI all shifted up one to make
    // room (2-4 -> 3-5, 5 -> 6).
    const bgfx::ViewId kMirrorView = 2;
    // Set by the mirror block below; read by the UI view's composite.
    bool mirrorReady = false;
    // Phase 4h (PORT_PROGRESS.md): the results screen fully replaces the
    // scene (opaque black clear, no track/car geometry underneath) rather
    // than drawing on top of the still-rendering track like the menu does
    // -- confirmed via JS's own CSS that `#results`, unlike `#menu`, has no
    // semi-transparent override.
    const bool showResults = raceState.mode == "done";
    // Diagnostic-only (see skipPostFx()'s own comment): when set, sky/world
    // render straight to the real backbuffer and the bloom/grade/tonemap
    // block below is skipped entirely, bypassing the offscreen sceneFb_
    // round-trip altogether.
    const bool bypassPostFx = skipPostFx();
    // Sequential: draw calls execute in submission order, not bgfx's default
    // sort-by-key order -- this Phase 2 scene has no depth buffer (see the
    // BGFX_STATE_* flags below), so "track first, cars on top" relies
    // entirely on submission order, same idea as 2D painter's-algorithm
    // layering.
    bgfx::setViewMode(kView, bgfx::ViewMode::Sequential);
    bgfx::setViewRect(kView, 0, 0, (uint16_t)width_, (uint16_t)height_);
    // Phase 5h (PORT_PROGRESS.md): sky+world now render into the offscreen
    // sceneFb_ instead of the real backbuffer -- the bloom/grade/tonemap
    // chain below reads this as its input and writes the graded result to
    // the actual backbuffer. Set explicitly every frame (bgfx view state,
    // unlike the backbuffer itself, persists across frames until changed).
    if (bypassPostFx) {
        bgfx::setViewFrameBuffer(kView, BGFX_INVALID_HANDLE);
    } else {
        bgfx::setViewFrameBuffer(kView, sceneFb_);
    }
    // Phase 5c (PORT_PROGRESS.md): when the sky view already painted this
    // frame's color buffer, the world view must NOT clear color -- a
    // view's clear touches its ENTIRE viewport regardless of what that
    // view goes on to draw, so a color clear here would immediately wipe
    // out the sky everywhere the ribbon/ground/cars don't cover (this was
    // a real bug hit and fixed this sub-phase: the sky was invisible
    // behind the ground/track horizon until this clear was narrowed to
    // depth-only). Results screen and the "no sky built yet" fallback both
    // still want their own full color clear.
    const bool skyPaintedThisFrame = !showResults && bgfx::isValid(skyTexture_);
    bgfx::setViewClear(kView, skyPaintedThisFrame ? BGFX_CLEAR_DEPTH : (BGFX_CLEAR_COLOR | BGFX_CLEAR_DEPTH),
                        showResults ? 0x000000ff : 0x1a2e1aff, 1.0f, 0);

    const bool homogeneousDepth = bgfx::getCaps()->homogeneousDepth;
    float identity[16];
    bx::mtxIdentity(identity);

    // Phase H5 (PORT_PROGRESS.md): the chased car's speed/draft, read by the
    // postfx composite block further down (out of the Chase-branch `car`
    // pointer's scope by then, and outside the `!showResults` block it's
    // set in) to drive vignette-tightening and radial blur. Stays 0 in
    // TopDown/results/menu -- there's no "speed sensation" to sell from an
    // external overview or a non-race screen, so the effect is off there.
    float chaseSpeedFx_ = 0.0f, chaseDraftFx_ = 0.0f;

    // Sky draws only when the world does (skipped during the results
    // screen, same as the ribbon/cars below it).
    if (skyPaintedThisFrame) {
        if (bypassPostFx) {
            bgfx::setViewFrameBuffer(kSkyView, BGFX_INVALID_HANDLE);
        } else {
            bgfx::setViewFrameBuffer(kSkyView, sceneFb_);
        }
        bgfx::setViewRect(kSkyView, 0, 0, (uint16_t)width_, (uint16_t)height_);
        bgfx::setViewClear(kSkyView, BGFX_CLEAR_NONE);
        float skyIdentity[16];
        bx::mtxIdentity(skyIdentity);
        bgfx::setViewTransform(kSkyView, skyIdentity, skyIdentity);
        bgfx::setTransform(skyIdentity);
        bgfx::setVertexBuffer(0, skyVb_, 0, 6);
        bgfx::setTexture(0, uSkyTexColor_, skyTexture_);
        // No depth write/test -- this is a flat backdrop drawn once, behind
        // everything, in its own lower-numbered view; it doesn't need to
        // participate in the world's depth buffer at all.
        bgfx::setState(BGFX_STATE_WRITE_RGB | BGFX_STATE_WRITE_A);
        bgfx::submit(kSkyView, skyProgram_);
    }

    if (showResults) {
        // Nothing else draws to the world view this frame -- same "force
        // the clear with no geometry submitted" precedent as
        // renderBlockedFrame()'s own bgfx::touch(kView) call.
        bgfx::touch(kView);
    } else {
    // G23: the camera branches below fill these; submitWorld() then consumes
    // them, once for the main view and again for the mirror.
    float view[16], proj[16];
    float eyePos[3] = {0.0f, 0.0f, 0.0f};
    // The chased car's interpolated pose, kept for the mirror camera to
    // build its rear view from. Null in TopDown mode, or before setTrack().
    const Car* mirrorCar = nullptr;
    InterpPose mirrorPose{};
    Vec3 mirrorUp{0, 1, 0};

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

            // G15: the chased car's interpolated pose (see this file's
            // interpolatedPose() comment) -- matches JS's chase cam reading
            // from carModelMat(c), itself built from poseOf(c)
            // (index.html:3725-3727), rather than the raw tick-snapped Car.
            const InterpPose chasePose = interpolatedPose(*car, renderAlpha, track_->total());
            mirrorCar = car;    // G23: the rear camera builds from this same pose
            mirrorPose = chasePose;

            const Vec3 base = pos3(*track_, chasePose.s, chasePose.lat);
            const double th = chasePose.hdg;
            const double fwx = std::cos(th), fwz = std::sin(th); // fw.y == 0
            const Vec3 up = surfaceUp(*track_, chasePose.s);

            const double dist = 6.9 + car->v * 0.020, hgt = 2.55; // index.html:3438
            double targetEyeX = base.x - fwx * dist + up.x * hgt;
            double targetEyeY = base.y + up.y * hgt;
            double targetEyeZ = base.z - fwz * dist + up.z * hgt;
            double targetLookX = base.x + fwx * 8.0;
            double targetLookY = base.y + 0.9;
            double targetLookZ = base.z + fwz * 8.0;

            // Corner lookahead: bias the look point into the upcoming
            // corner (index.html:3446-3449's pAh/lookLat, same constants).
            PointResult pAh = track_->pointAt(chasePose.s + std::max(20.0, car->v * 0.7));
            const double lookLat = std::max(-2.5, std::min(2.5, pAh.curv * 450.0));
            const double nx = -std::sin(pAh.hdg), ny = std::cos(pAh.hdg);
            targetLookX += nx * lookLat;
            targetLookZ += ny * lookLat;

            // Phase H5 (PORT_PROGRESS.md): speed-driven FOV push, JS's
            // fovTgt formula verbatim (index.html:4058-4066) -- draftF adds
            // up to 4 more degrees on top of the speed term, same as JS.
            const double fovTgt = 58.0 + std::min(14.0, car->v * 0.14) + std::min(1.0, car->draftF) * 4.0;
            chaseSpeedFx_ = (float)car->v;
            chaseDraftFx_ = (float)car->draftF;

            if (!chaseInitialized_) {
                chaseEyeX_ = (float)targetEyeX;
                chaseEyeY_ = (float)targetEyeY;
                chaseEyeZ_ = (float)targetEyeZ;
                chaseLookX_ = (float)targetLookX;
                chaseLookY_ = (float)targetLookY;
                chaseLookZ_ = (float)targetLookZ;
                chaseFov_ = (float)fovTgt;
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
                // JS smooths FOV with an implicit-60fps `*0.08` per-frame
                // multiplier (index.html:4066); converted to real-dt here
                // the same way as the eye/look rates above.
                const double kFov = 1.0 - std::exp(-dtSec * 5.0);
                chaseFov_ += (float)((fovTgt - chaseFov_) * kFov);
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
            // G15: interpolated `s` again (see interpolatedPose()'s comment
            // above) -- this is a separate `car && track_` block from the
            // one above, so chasePose isn't in scope here.
            const Vec3 up = surfaceUp(*track_, interpolatedPose(*car, renderAlpha, track_->total()).s);
            upBlend = {up.x * 0.45, 1.0, up.z * 0.45};
            const double len = std::sqrt(upBlend.x * upBlend.x + upBlend.y * upBlend.y + upBlend.z * upBlend.z);
            if (len > 1e-9) {
                upBlend.x /= len;
                upBlend.y /= len;
                upBlend.z /= len;
            }
        }

        mirrorUp = upBlend; // G23: the mirror leans with the same banked up-vector

        const bx::Vec3 eye = {chaseEyeX_, chaseEyeY_, chaseEyeZ_};
        const bx::Vec3 at = {chaseLookX_, chaseLookY_, chaseLookZ_};
        const bx::Vec3 up = {(float)upBlend.x, (float)upBlend.y, (float)upBlend.z};
        // Right-handed explicitly: bx::mtxLookAt defaults to Handedness::Left,
        // whose `right = cross(up, at-eye)` is the NEGATION of the standard
        // right-handed `right = cross(forward, up)` this project's whole
        // world-space convention already assumes (track_surface.cpp's
        // pos3()/surfaceUp(), and mat4RotateY(-ch)'s own derivation comment
        // above, both verified right-handed). Left the camera's screen-X
        // axis mirrored relative to every other piece of world geometry --
        // a real, user-reported bug ("going around the track backwards",
        // steering feeling inverted), not a sim bug: the simulation's own
        // left-hand-turn convention and player steering sign are both
        // independently verified correct against the original JS (see
        // PORT_PROGRESS.md). mtxProj below must use the same handedness --
        // it isn't just an X-mirror, LH/RH also flips which view-space Z
        // sign the projection matrix expects.
        bx::mtxLookAt(view, eye, at, up, bx::Handedness::Right);
        const float aspect = (height_ > 0) ? (float)width_ / (float)height_ : 1.0f;
        // Base FOV (vertical) was a fixed 60, matching JS's own
        // `new THREE.PerspectiveCamera(60, 1, 0.5, 1500)` exactly. Phase H5
        // (PORT_PROGRESS.md) replaces the constant with chaseFov_, a smoothed
        // speed-driven push computed above (JS's own fovTgt formula,
        // index.html:4058-4066) -- 58 at rest, rising to as much as 76 at
        // speed with a full draft. near 0.5, far 1500 unchanged. bx's
        // `mtxProj(fovy, aspect, ...)` overload takes `_fovy` in DEGREES
        // and converts internally (`toRad(_fovy)` inside math.cpp) -- do
        // NOT pre-convert here, that double-converts and produces a
        // near-zero effective FOV (a real bug hit and fixed this session:
        // it manifested as the entire frame being covered by whatever
        // geometry was nearest the camera, with ~75x too much magnification).
        //
        // Holding vertical FOV fixed at chaseFov_ while deriving horizontal
        // FOV from `aspect` (the standard convention) works fine for
        // desktop-ish aspects, but a real phone in landscape with the
        // browser chrome eating vertical space can reach an aspect as
        // extreme as ~2.7:1 -- at that ratio 60 vertical implies a ~115
        // horizontal FOV, a near-fisheye view where nearby ground/wall
        // geometry looms huge and fills most of the frame while distant
        // things (the track ahead, other cars) shrink toward the center --
        // exactly the "huge close-up color bands, no visible cars, doesn't
        // look 3D" complaint a real device reported. Cap horizontal FOV at
        // kMaxFovXDeg by shrinking vertical FOV instead once `aspect`
        // exceeds the point where the base vertical FOV would already blow
        // past it; narrower/normal aspects are completely unaffected.
        const float kBaseFovYDeg = chaseFov_;
        constexpr float kMaxFovXDeg = 100.0f;
        float fovYDeg = kBaseFovYDeg;
        const float halfFovYBase = bx::toRad(kBaseFovYDeg) * 0.5f;
        const float fovXAtBaseDeg = bx::toDeg(bx::atan(bx::tan(halfFovYBase) * aspect)) * 2.0f;
        if (fovXAtBaseDeg > kMaxFovXDeg) {
            const float halfFovXCap = bx::toRad(kMaxFovXDeg) * 0.5f;
            fovYDeg = bx::toDeg(bx::atan(bx::tan(halfFovXCap) / aspect)) * 2.0f;
        }
        bx::mtxProj(proj, fovYDeg, aspect, 0.5f, 1500.0f, homogeneousDepth, bx::Handedness::Right);
        eyePos[0] = eye.x;
        eyePos[1] = eye.y;
        eyePos[2] = eye.z;
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
        const bx::Vec3 eye = {topCx_, 200.0f, topCy_};
        const bx::Vec3 at = {topCx_, 0.0f, topCy_};
        const bx::Vec3 up = {0.0f, 0.0f, -1.0f};
        // Right-handed explicitly -- see the Chase branch's own comment
        // above for why (matches this project's world-space convention).
        bx::mtxLookAt(view, eye, at, up, bx::Handedness::Right);
        bx::mtxOrtho(proj, -halfW, halfW, -halfH, halfH, 0.1f, 1000.0f, 0.0f, homogeneousDepth,
                     bx::Handedness::Right);
        eyePos[0] = eye.x;
        eyePos[1] = eye.y;
        eyePos[2] = eye.z;
    }

    // G23: the world's own geometry (ground, ribbon, stadium, crowd) is
    // submitted by submitWorld() below, once per view. The car draws are
    // resolved into `draws` first, because the loop that builds them advances
    // wall-clock wheel state -- see WorldDrawList's comment in renderer.h.
    WorldDrawList draws;
    draws.particles = particles; // H4: shared read-only across every view

    // Step 3 (PORT_PROGRESS.md, physics-driven car rig animation): the flat
    // textured quad that used to draw here is replaced by car_rig_data.h's
    // placeholder rig (box chassis + 4 box wheels, imported once in
    // init()), animated every frame from each car's own live
    // Car::v/fzFront/fzRear (wheel_animation.h) rather than a baked
    // animation clip. Livery still applies via getOrBuildCarTexture(),
    // unchanged, bound per-draw via SkinnedMesh::draw()'s textureOverride.
    // Model matrix: mat4Mul(mat4Translate(wx, carY, wy), mat4RotateY(-ch))
    // reproduces the old quad's exact world-space rotation (px = wx +
    // lx*cos(ch) - ly*sin(ch), py = wy + lx*sin(ch) + ly*cos(ch), for the
    // rig's local X = nose-tail / local Z = left-right, the same axes the
    // quad's lx/ly used) -- see the mat4RotateY comment above for the
    // derivation. Built with this file's own hand-rolled mat4* helpers
    // rather than bx::mtx*, same reasoning as wheel_animation.cpp.
    if (!cars.empty() && track_ && carMesh_.isValid()) {
        const auto now = std::chrono::steady_clock::now();
        double dt = 0.0;
        if (wheelAnimInitialized_) dt = std::chrono::duration<double>(now - wheelAnimLastTime_).count();
        wheelAnimLastTime_ = now;
        wheelAnimInitialized_ = true;
        dt = std::clamp(dt, 0.0, 0.1); // guard against a huge dt after a pause/hitch

        // Real per-axle rest load (static split, mass*G*weightDistF), the
        // baseline computeWheelTransforms() measures live fzFront/fzRear
        // against to get a suspension offset -- 0 at rest, displaced under
        // real weight transfer/aero load.
        const double restLoadFront = CAR.mass * G * CAR.weightDistF * 0.5;
        const double restLoadRear = CAR.mass * G * (1.0 - CAR.weightDistF) * 0.5;
        constexpr double kWheelRadius = 0.35; // must match tools/gen_car_rig.py's WHEEL_RADIUS
        constexpr double kLoadToTravel = 0.0001;
        constexpr double kMaxTravel = 0.08;

        for (auto& c : cars) {
            // G15: draw each car at its interpolated pose (see
            // interpolatedPose()'s comment above), not the raw tick-snapped
            // x/y/hdg/s/lat -- this is the fix for the reported chassis
            // jitter/stutter (wheel-spin/suspension animation below is
            // already driven by real wall-clock dt and is unaffected).
            const InterpPose pose = interpolatedPose(c, renderAlpha, track_->total());
            const float ch = (float)pose.hdg;
            const float wx = (float)pose.x, wy = (float)pose.y;
            const Vec3 carPos = pos3(*track_, pose.s, pose.lat);
            const float carY = (float)carPos.y;

            WheelAnimState& animState = carWheelAnim_[c.num];
            const WheelAnimInputs animIn{c.v, c.fzFront, c.fzRear};
            const auto wheelTransforms = computeWheelTransforms(animState, animIn, kWheelRadius, restLoadFront,
                                                                 restLoadRear, kLoadToTravel, kMaxTravel, dt);
            const auto bonePalette = computeBonePalette(carRigJoints_, wheelTransforms, carWheelJointIndex_);
            std::vector<float> boneFloats(bonePalette.size() * 16);
            for (size_t i = 0; i < bonePalette.size(); ++i) {
                for (int k = 0; k < 16; ++k) boneFloats[i * 16 + k] = (float)bonePalette[i][k];
            }
            const Mat4f model = mat4Mul(mat4Translate(wx, carY, wy), mat4RotateY(-ch));
            if (!skipCarDraws()) {
                // G23: queued rather than drawn here, so submitWorld() can
                // issue the setBoneMatrices()/draw() pair -- which bgfx's
                // checked build requires to be 1:1 ("Uniform ... was already
                // set for this draw call") -- once per view. The gate still
                // skips the bone upload along with the draw, for the same
                // reason it always did: uploading without a matching submit
                // stacks uniform writes with nothing to flush them, which is
                // a bug in the diagnostic gate rather than evidence about the
                // crash it exists to isolate.
                draws.cars.push_back({model, std::move(boneFloats), (int)bonePalette.size(),
                                      getOrBuildCarTexture(c)});
                // H3: the car's own contact shadow, built from the real
                // banked surfaceUp() rather than this car-body matrix above
                // (which never tilts with banking) -- see carShadowBasis()'s
                // own comment for why that's the correct call even though
                // the body it sits under stays flat.
                draws.shadows.push_back(carShadowModelMat(*track_, pose.s, pose.hdg, carPos));
            }
        }

        // G20 (NT2003 presentation plan) -- BUGFIX. The pace car is fully
        // simulated (gridStart() seeds it, stepPace() drives it, tick() runs
        // it every step) but was **never drawn**: renderFrame() didn't take
        // it and `src/render/` contained no reference to PaceCar at all. The
        // field followed an invisible car through every pace lap and every
        // caution -- and the grid-formation shot is one of the most
        // recognisable images in the reference footage. Drawn here through
        // the same skinned-mesh path as the field, so it gets the same rig,
        // wheel animation and G15 pose interpolation.
        //
        // Only while it's actually on track: once stepPace() parks it, JS
        // hides it too (index.html's own pace-car visibility gate).
        if (pace && pace->state != "parked" && !skipCarDraws()) {
            // PaceCar carries the same x/y/hdg/s/lat as Car, so
            // interpolatedPose()'s math applies verbatim -- but it takes a
            // Car, so blend inline rather than widening that helper's
            // signature for one call site.
            const double a = std::clamp(renderAlpha, 0.0, 1.0);
            const double pHdg = lerpAngRad(pace->phdg, pace->hdg, a);
            const double pX = pace->px + (pace->x - pace->px) * a;
            const double pY = pace->py + (pace->y - pace->py) * a;
            const double pS = lerpTrackS(pace->ps, pace->s, a, track_->total());
            const double pLat = pace->plat + (pace->lat - pace->plat) * a;

            WheelAnimState& paceAnim = carWheelAnim_[-1];
            const WheelAnimInputs paceIn{pace->v, restLoadFront, restLoadRear};
            const auto paceWheels = computeWheelTransforms(paceAnim, paceIn, kWheelRadius, restLoadFront,
                                                            restLoadRear, kLoadToTravel, kMaxTravel, dt);
            const auto paceBones = computeBonePalette(carRigJoints_, paceWheels, carWheelJointIndex_);
            std::vector<float> paceBoneFloats(paceBones.size() * 16);
            for (size_t i = 0; i < paceBones.size(); ++i) {
                for (int k = 0; k < 16; ++k) paceBoneFloats[i * 16 + k] = (float)paceBones[i][k];
            }
            const Vec3 pacePos = pos3(*track_, pS, pLat);
            const Mat4f paceModel =
                mat4Mul(mat4Translate((float)pX, (float)pacePos.y, (float)pY), mat4RotateY(-(float)pHdg));
            draws.cars.push_back({paceModel, std::move(paceBoneFloats), (int)paceBones.size(),
                                  getOrBuildPaceTexture()});
            // H3: JS gives the pace car a shadow too (index.html:4098).
            draws.shadows.push_back(carShadowModelMat(*track_, pS, pHdg, pacePos));
        }
    }

    // G23: the main view. Everything above only *computed* -- this is where
    // the world is actually submitted.
    submitWorld(kView, view, proj, eyePos, draws);

    // ---- G23: rearview mirror -------------------------------------------
    //
    // A second pass over the same world with a rear-facing camera, into
    // mirrorFb_, composited into the HUD further down. View 2 so it runs
    // after the main world view (1) but well before the UI view that samples
    // it -- bgfx executes views in ascending ID order regardless of
    // submission order, so a mirror view numbered above its consumer would
    // sample the *previous* frame's contents.
    //
    // Only in Chase mode: a top-down overview has no meaningful "behind".
    // (mirrorReady is declared alongside the view IDs above.)
    if (cameraMode_ == CameraMode::Chase && mirrorCar && track_ && bgfx::isValid(mirrorFb_)) {
        const Vec3 base = pos3(*track_, mirrorPose.s, mirrorPose.lat);
        const double th = mirrorPose.hdg;
        const double fwx = std::cos(th), fwz = std::sin(th); // fw.y == 0

        // Just above and slightly behind the roofline, looking back down the
        // track. Set back far enough that the player's own car is behind the
        // near plane rather than filling the mirror with its own tail.
        constexpr double kMirrorBack = 3.2, kMirrorHigh = 1.5, kMirrorRange = 60.0;
        const bx::Vec3 eye = {(float)(base.x - fwx * kMirrorBack + mirrorUp.x * kMirrorHigh),
                              (float)(base.y + mirrorUp.y * kMirrorHigh),
                              (float)(base.z - fwz * kMirrorBack + mirrorUp.z * kMirrorHigh)};
        const bx::Vec3 at = {(float)(eye.x - fwx * kMirrorRange), (float)(eye.y - 0.6),
                             (float)(eye.z - fwz * kMirrorRange)};
        const bx::Vec3 up = {(float)mirrorUp.x, (float)mirrorUp.y, (float)mirrorUp.z};

        float mirrorView[16], mirrorProj[16];
        // Same handedness as the main camera for both calls -- see the Chase
        // branch's own comment: LH/RH is not merely an X mirror, it also
        // flips which view-space Z sign the projection expects, so mixing
        // them silently produces a scene that is inside-out rather than
        // obviously broken.
        bx::mtxLookAt(mirrorView, eye, at, up, bx::Handedness::Right);
        const float mirrorAspect = (float)kMirrorW / (float)kMirrorH;
        // 34 vertical over a 2.67:1 target works out to ~79 horizontal --
        // the wide, shallow letterbox a real mirror gives, and wide enough
        // to show a car drawing alongside rather than only dead astern.
        bx::mtxProj(mirrorProj, 34.0f, mirrorAspect, 0.5f, 1500.0f, homogeneousDepth,
                    bx::Handedness::Right);

        bgfx::setViewFrameBuffer(kMirrorView, mirrorFb_);
        bgfx::setViewRect(kMirrorView, 0, 0, (uint16_t)kMirrorW, (uint16_t)kMirrorH);
        // Sequential so the sky backdrop below stays behind the world: it
        // writes no depth, so without submission-order execution bgfx's
        // draw-call sort could put it over the geometry instead.
        bgfx::setViewMode(kMirrorView, bgfx::ViewMode::Sequential);
        bgfx::setViewClear(kMirrorView, BGFX_CLEAR_COLOR | BGFX_CLEAR_DEPTH, 0x6a90b4ff, 1.0f, 0);
        if (skyPaintedThisFrame) {
            // The same fullscreen backdrop the main view uses -- identity
            // view/proj, so it fills the mirror target exactly as it fills
            // the screen. Submitted into the mirror's own view rather than
            // getting a second sky view of its own.
            float skyIdentity[16];
            bx::mtxIdentity(skyIdentity);
            bgfx::setViewTransform(kMirrorView, skyIdentity, skyIdentity);
            bgfx::setTransform(skyIdentity);
            bgfx::setVertexBuffer(0, skyVb_, 0, 6);
            bgfx::setTexture(0, uSkyTexColor_, skyTexture_);
            bgfx::setState(BGFX_STATE_WRITE_RGB | BGFX_STATE_WRITE_A);
            bgfx::submit(kMirrorView, skyProgram_);
        }
        const float mirrorEye[3] = {eye.x, eye.y, eye.z};
        submitWorld(kMirrorView, mirrorView, mirrorProj, mirrorEye, draws);
        mirrorReady = true;
    }
    } // !showResults

    // Phase 5h (PORT_PROGRESS.md): bloom + grade/tonemap postprocess chain.
    // sky/world (views 0/1, set up above) already rendered into sceneFb_
    // instead of the backbuffer; these 3 views composite that back onto
    // the REAL backbuffer through a bright-pass + fixed-radius blur +
    // final grade/tonemap fullscreen pass (reusing skyVb_'s fullscreen NDC
    // quad and uSkyTexColor_'s "generic single-sampler" convention, same as
    // every other fullscreen-quad draw in this renderer). Runs every frame
    // regardless of mode -- results/menu included, matching JS's own
    // EffectComposer, which has no "skip postfx this mode" branch either; a
    // black-cleared results scene simply stays black (plus a barely-visible
    // corner vignette) after the chain, no different in effect from before.
    // Skipped entirely under ?skipPostFx=1 -- sky/world already wrote
    // straight to the backbuffer above, so this chain would otherwise
    // overwrite that with a stale/empty sceneFb_ read.
    if (!bypassPostFx) {
        const bgfx::ViewId kBloomBrightView = 3;
        const bgfx::ViewId kBloomBlurView = 4;
        const bgfx::ViewId kGradeTonemapView = 5;
        const bgfx::TextureHandle sceneColorTex = bgfx::getTexture(sceneFb_);
        const uint16_t bloomW = (uint16_t)std::max(1, width_ / 2);
        const uint16_t bloomH = (uint16_t)std::max(1, height_ / 2);

        // Bright-pass: threshold=0.85 (JS's own UnrealBloomPass threshold,
        // index.html:1563), sampled from the full-res scene into a half-res
        // target -- the GPU's own bilinear filtering does the downsample.
        bgfx::setViewFrameBuffer(kBloomBrightView, bloomBrightFb_);
        bgfx::setViewRect(kBloomBrightView, 0, 0, bloomW, bloomH);
        bgfx::setViewClear(kBloomBrightView, BGFX_CLEAR_NONE);
        bgfx::setViewTransform(kBloomBrightView, identity, identity);
        {
            const float bloomParams[4] = {0.85f, 0.0f, 0.0f, 0.0f};
            bgfx::setUniform(uBloomParams_, bloomParams);
            bgfx::setTransform(identity);
            bgfx::setVertexBuffer(0, skyVb_, 0, 6);
            bgfx::setTexture(0, uSkyTexColor_, sceneColorTex);
            bgfx::setState(BGFX_STATE_WRITE_RGB | BGFX_STATE_WRITE_A);
            bgfx::submit(kBloomBrightView, bloomBrightProgram_);
        }

        // Fixed-radius blur (fs_bloom_blur.sc's own comment): a 3x3
        // binomial tap at 1.5 texels of the half-res bright buffer.
        bgfx::setViewFrameBuffer(kBloomBlurView, bloomBlurFb_);
        bgfx::setViewRect(kBloomBlurView, 0, 0, bloomW, bloomH);
        bgfx::setViewClear(kBloomBlurView, BGFX_CLEAR_NONE);
        bgfx::setViewTransform(kBloomBlurView, identity, identity);
        {
            const float bloomParams[4] = {0.0f, 1.0f / (float)bloomW, 1.0f / (float)bloomH, 1.5f};
            bgfx::setUniform(uBloomParams_, bloomParams);
            bgfx::setTransform(identity);
            bgfx::setVertexBuffer(0, skyVb_, 0, 6);
            bgfx::setTexture(0, uSkyTexColor_, bgfx::getTexture(bloomBrightFb_));
            bgfx::setState(BGFX_STATE_WRITE_RGB | BGFX_STATE_WRITE_A);
            bgfx::submit(kBloomBlurView, bloomBlurProgram_);
        }

        // Grade + tonemap: additive bloom combine, JS's own GradeShader math
        // (index.html:1533-1551: lift/gain/gamma, saturation, vignette),
        // then an ACES filmic curve applied LAST (matching JS's
        // RenderPass->Bloom->Grade->OutputPass ordering). Writes to the
        // REAL backbuffer (explicit BGFX_INVALID_HANDLE -- a view's own
        // frame buffer persists across frames until changed, so this must
        // be set every frame now that other views use custom targets).
        bgfx::setViewFrameBuffer(kGradeTonemapView, BGFX_INVALID_HANDLE);
        bgfx::setViewRect(kGradeTonemapView, 0, 0, (uint16_t)width_, (uint16_t)height_);
        bgfx::setViewClear(kGradeTonemapView, BGFX_CLEAR_NONE);
        bgfx::setViewTransform(kGradeTonemapView, identity, identity);
        {
            // bloomStrength/gain/lift/gamma, then saturation/vignetteInner/
            // vignetteOuter -- JS's own GradeShader defaults (index.html:
            // 1534-1535) and UnrealBloomPass strength (index.html:1563).
            //
            // Phase H5 (PORT_PROGRESS.md): vignetteInner/Outer tighten and a
            // new radial-blur strength (w) rises with chaseSpeedFx_/
            // chaseDraftFx_. JS's own vignette is static (0.55/0.95, never
            // varies) and it has no blur at all -- this is a deliberate
            // addition beyond straight porting, using the same v=100
            // saturation point as JS's own FOV-push formula above so all
            // three speed effects agree on what "fast" means.
            const float speedT = std::max(0.0f, std::min(1.0f, chaseSpeedFx_ / 100.0f + std::min(1.0f, chaseDraftFx_) * 0.15f));
            const float gradeParams1[4] = {0.32f, 1.04f, 0.0f, 0.94f};
            // 0.05 cap: the shader's per-tap offset is (screen-space distance
            // from center) * blurAmt * t, and corner pixels already sit
            // ~0.71 UV from center -- an early 0.35 cap produced a wildly
            // overdone warp even at modest speed (~0.11 offset, ~110px at
            // 960 wide) when hand-checked via headless screenshot. 0.05
            // keeps the corner offset under ~0.035 UV (~35px) at max speed,
            // a felt hint of speed rather than visible image distortion.
            const float gradeParams2[4] = {1.10f, 0.55f - 0.15f * speedT, 0.95f - 0.10f * speedT, 0.05f * speedT};
            bgfx::setUniform(uGradeParams1_, gradeParams1);
            bgfx::setUniform(uGradeParams2_, gradeParams2);
            bgfx::setTransform(identity);
            bgfx::setVertexBuffer(0, skyVb_, 0, 6);
            bgfx::setTexture(0, uSkyTexColor_, sceneColorTex);
            bgfx::setTexture(1, uTexBloom_, bgfx::getTexture(bloomBlurFb_));
            bgfx::setState(BGFX_STATE_WRITE_RGB | BGFX_STATE_WRITE_A);
            bgfx::submit(kGradeTonemapView, gradeTonemapProgram_);
        }
    }

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
                track_ ? track_->total() : 0.0, width_, height_);
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
    // views in ascending ID order by default, so this draws after the sky
    // (view 0), world (view 1), and Phase 5h's bloom/grade/tonemap chain
    // (views 2-4) -- deliberately renumbered up from 2 to 5 so it stays the
    // LAST view every frame, drawing straight onto the already-graded real
    // backbuffer, untouched by bloom/grade/tonemap (matching JS, whose HUD
    // is a separate DOM/CSS overlay entirely outside its EffectComposer
    // chain). bgfx's own debug-text overlay always draws on top of every
    // view regardless of ID (confirmed empirically in every screenshot this
    // project has taken -- unaffected by any of this view's renumbering),
    // so the resulting layering is sky -> world -> bloom/grade/tonemap ->
    // UI quads -> HUD/menu text, which is what's wanted (numbers legible
    // over bars, not chips over text). Skipped entirely when empty (e.g.
    // mode=="menu", where drawHud() early-returns before adding anything)
    // -- same "nothing submitted this frame -> nothing drawn" precedent the
    // world view's own `if (!cars.empty())` guard above relies on.
    if (!uiVerts.empty() || mirrorReady) {
        const bgfx::ViewId kUiView = 6;
        bgfx::setViewFrameBuffer(kUiView, BGFX_INVALID_HANDLE);
        bgfx::setViewRect(kUiView, 0, 0, (uint16_t)width_, (uint16_t)height_);
        // G23: Sequential, so the mirror composite below stays UNDER the UI
        // quads. This view now submits two different programs (skyProgram_
        // for the textured mirror, program_ for the flat UI geometry), and
        // bgfx's default sort key ordering makes no promise about which of
        // two different programs runs first -- the frame that borders the
        // mirror is drawn from uiVerts and must land on top of it.
        bgfx::setViewMode(kUiView, bgfx::ViewMode::Sequential);
        float uiViewMtx[16], uiProj[16];
        bx::mtxIdentity(uiViewMtx);
        // Top-left origin, y-down, matching dbgText/SDL mouse coordinates --
        // bottom=height, top=0 is the standard ortho y-flip trick.
        bx::mtxOrtho(uiProj, 0.0f, (float)width_, (float)height_, 0.0f, -1.0f, 1.0f, 0.0f,
                     homogeneousDepth);
        bgfx::setViewTransform(kUiView, uiViewMtx, uiProj);

        // ---- G23: mirror composite ---------------------------------------
        //
        // The textured-UI path the plan called for, and the reason it exists:
        // a PosUvVertex quad in this same pixel-space ortho view, drawn with
        // the already-built skyProgram_ (vs_sky.sc does a real MVP transform,
        // fs_sky.sc is a plain texture2D), so a render-target texture can be
        // composited into the HUD with no new shader, program or layout.
        //
        // Bypasses bloom/grade/tonemap by construction -- it lands on the
        // already-graded backbuffer -- so it reads flatter than the main
        // view. Accepted rather than running a second post-FX chain for a
        // 256x96 inset on a mobile target.
        if (mirrorReady && bgfx::isValid(mirrorFb_)) {
            const float mw = std::clamp((float)width_ * 0.20f, 176.0f, 288.0f);
            const float mh = mw * ((float)kMirrorH / (float)kMirrorW);
            const float mx = ((float)width_ - mw) * 0.5f, my = 8.0f;
            // The mirror flip, for free: reversed U on the composite quad, so
            // a car passing on one side appears on the side a driver looking
            // in a real mirror would see it.
            constexpr float uLeft = 1.0f, uRight = 0.0f;
            // Render-target textures are V-flipped on backends whose texture
            // origin is bottom-left (GL/GLES, which is what the WASM build
            // runs on) relative to the top-left pixel space this view uses.
            const bool originBottomLeft = bgfx::getCaps()->originBottomLeft;
            const float vTop = originBottomLeft ? 1.0f : 0.0f;
            const float vBot = originBottomLeft ? 0.0f : 1.0f;
            const PosUvVertex quad[6] = {
                {mx, my, 0.0f, uLeft, vTop},          {mx + mw, my, 0.0f, uRight, vTop},
                {mx + mw, my + mh, 0.0f, uRight, vBot}, {mx, my, 0.0f, uLeft, vTop},
                {mx + mw, my + mh, 0.0f, uRight, vBot}, {mx, my + mh, 0.0f, uLeft, vBot},
            };
            if (bgfx::getAvailTransientVertexBuffer(6, skyLayout_) >= 6) {
                bgfx::TransientVertexBuffer mtvb;
                bgfx::allocTransientVertexBuffer(&mtvb, 6, skyLayout_);
                std::memcpy(mtvb.data, quad, sizeof(quad));
                bgfx::setTransform(identity);
                bgfx::setVertexBuffer(0, &mtvb, 0, 6);
                bgfx::setTexture(0, uSkyTexColor_, bgfx::getTexture(mirrorFb_));
                bgfx::setState(BGFX_STATE_WRITE_RGB | BGFX_STATE_WRITE_A);
                bgfx::submit(kUiView, skyProgram_);
            }
            // Bevelled frame, appended after the composite so it draws over
            // the mirror's own edges -- same chrome as the HUD's other
            // plates, built from the same ui_draw helper.
            constexpr float kFrame = 3.0f;
            pushBevelPanel(uiVerts, mx - kFrame, my - kFrame, mw + kFrame * 2.0f, kFrame,
                            packColor(Theme::kSteel, 0.95f), packColor(Theme::kGraycool, 0.6f),
                            packColor(Theme::kSteel, 0.95f), 1.0f);
            pushBevelPanel(uiVerts, mx - kFrame, my + mh, mw + kFrame * 2.0f, kFrame,
                            packColor(Theme::kSteel, 0.95f), packColor(Theme::kGraycool, 0.6f),
                            packColor(Theme::kSteel, 0.95f), 1.0f);
            pushQuad(uiVerts, mx - kFrame, my, kFrame, mh, packColor(Theme::kSteel, 0.95f));
            pushQuad(uiVerts, mx + mw, my, kFrame, mh, packColor(Theme::kSteel, 0.95f));
        }

        const uint32_t uiVertCount = (uint32_t)uiVerts.size();
        if (uiVertCount > 0 && bgfx::getAvailTransientVertexBuffer(uiVertCount, layout_) >= uiVertCount) {
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
