#include "renderer.h"
#include "atlas_texture.h"
#include "color.h"
#include "env_presets.h"
#include "hud.h"
#include "livery.h"
#include "shaders_embedded.h"
#include "sky_texture.h"
#include "hill_silhouette.h"
#include "pylon_mesh.h"
#include "stadium_mesh.h"
#include "track_surface.h"
#include "vertex.h"
#include "vertex_lit.h"
#include "vertex_textured.h"
#include "vertex_uv.h"
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

    return bgfx::isValid(program_) && bgfx::isValid(litProgram_) && bgfx::isValid(skyProgram_) &&
           bgfx::isValid(texturedLitProgram_) && bgfx::isValid(bloomBrightProgram_) &&
           bgfx::isValid(bloomBlurProgram_) && bgfx::isValid(gradeTonemapProgram_) && bgfx::isValid(sceneFb_);
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
}

void Renderer::shutdown() {
    if (bgfx::isValid(trackVb_)) bgfx::destroy(trackVb_);
    if (bgfx::isValid(groundVb_)) bgfx::destroy(groundVb_);
    if (bgfx::isValid(program_)) bgfx::destroy(program_);
    if (bgfx::isValid(litProgram_)) bgfx::destroy(litProgram_);
    if (bgfx::isValid(uSunDir_)) bgfx::destroy(uSunDir_);
    if (bgfx::isValid(uSunColor_)) bgfx::destroy(uSunColor_);
    if (bgfx::isValid(uHemiSky_)) bgfx::destroy(uHemiSky_);
    if (bgfx::isValid(uHemiGround_)) bgfx::destroy(uHemiGround_);
    if (bgfx::isValid(skyProgram_)) bgfx::destroy(skyProgram_);
    if (bgfx::isValid(uSkyTexColor_)) bgfx::destroy(uSkyTexColor_);
    if (bgfx::isValid(skyVb_)) bgfx::destroy(skyVb_);
    if (bgfx::isValid(skyTexture_)) bgfx::destroy(skyTexture_);
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
    if (bgfx::isValid(bloomBrightProgram_)) bgfx::destroy(bloomBrightProgram_);
    if (bgfx::isValid(bloomBlurProgram_)) bgfx::destroy(bloomBlurProgram_);
    if (bgfx::isValid(gradeTonemapProgram_)) bgfx::destroy(gradeTonemapProgram_);
    if (bgfx::isValid(uTexBloom_)) bgfx::destroy(uTexBloom_);
    if (bgfx::isValid(uBloomParams_)) bgfx::destroy(uBloomParams_);
    if (bgfx::isValid(uGradeParams1_)) bgfx::destroy(uGradeParams1_);
    if (bgfx::isValid(uGradeParams2_)) bgfx::destroy(uGradeParams2_);
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
        appendStand(buildStandMesh(track, seg0.s0 + seg0.len * 0.12, seg0.s0 + seg0.len * 0.88, st.standTier.front,
                                    st.crowdTiers, st.standDensity, st.standScale.tierD, st.standScale.tierH,
                                    st.crowdPalette, crowdUV, sceneryRng));
        appendStand(buildStandMesh(track, seg2.s0 + seg2.len * 0.12, seg2.s0 + seg2.len * 0.88, st.standTier.back,
                                    st.crowdTiers, st.standDensity, st.standScale.tierD, st.standScale.tierH,
                                    st.crowdPalette, crowdUV, sceneryRng));
        // Every track gets corner seating (index.html:2067-2075's own
        // comment on this): coverage fraction depends on standReach, not
        // whether it's "full" only.
        const double cornerCov = st.standReach == "full" ? 0.94 : 0.55;
        const double pad = (1.0 - cornerCov) / 2.0;
        appendStand(buildStandMesh(track, seg1.s0 + seg1.len * pad, seg1.s0 + seg1.len * (1 - pad),
                                    st.standTier.corner, st.crowdTiers, st.standDensity, st.standScale.tierD,
                                    st.standScale.tierH, st.crowdPalette, crowdUV, sceneryRng));
        appendStand(buildStandMesh(track, seg3.s0 + seg3.len * pad, seg3.s0 + seg3.len * (1 - pad),
                                    st.standTier.corner, st.crowdTiers, st.standDensity, st.standScale.tierD,
                                    st.standScale.tierH, st.crowdPalette, crowdUV, sceneryRng));
        // PIT_OUT/PIT_IN (index.html:1937): sized to hold both pit AI lanes
        // (moving lane at lat=-8.4, stall lane at lat=-10.5).
        append(buildPitRoadMesh(track, -7.2, -11.8));
        append(buildOuterWallMesh(track));
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
        atlasTexture_ = bgfx::createTexture2D((uint16_t)kAtlasSize, (uint16_t)kAtlasSize, false, 1,
                                               bgfx::TextureFormat::RGBA8, 0,
                                               bgfx::copy(atlasPixels.data(), (uint32_t)atlasPixels.size()));
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
    const bgfx::TextureHandle tex =
        bgfx::createTexture2D((uint16_t)kLiveryTextureSize, (uint16_t)kLiveryTextureSize, false, 1,
                               bgfx::TextureFormat::RGBA8, 0, bgfx::copy(pixels.data(), (uint32_t)pixels.size()));
    carTextures_[car.num] = tex;
    return tex;
}

void Renderer::renderFrame(const RaceState& raceState, const std::vector<Car>& cars,
                            const MenuSelection* menu, const std::string* menuTrackName,
                            const std::vector<Car*>* finishOrder) {
    // Phase 5c (PORT_PROGRESS.md): a new view (id 0, numerically below the
    // world view) for the sky background -- bgfx renders views in ascending
    // ID order regardless of submission order, so this MUST be a lower ID
    // than kView for the sky to end up behind the ribbon/cars. Shifted the
    // world/UI views up by one (0->1, 1->2) to make room; renderBlockedFrame()
    // is a separate, mutually-exclusive screen and keeps its own view 0
    // unchanged.
    const bgfx::ViewId kSkyView = 0;
    const bgfx::ViewId kView = 1;
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
    // Phase 5h (PORT_PROGRESS.md): sky+world now render into the offscreen
    // sceneFb_ instead of the real backbuffer -- the bloom/grade/tonemap
    // chain below reads this as its input and writes the graded result to
    // the actual backbuffer. Set explicitly every frame (bgfx view state,
    // unlike the backbuffer itself, persists across frames until changed).
    bgfx::setViewFrameBuffer(kView, sceneFb_);
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

    // Sky draws only when the world does (skipped during the results
    // screen, same as the ribbon/cars below it).
    if (skyPaintedThisFrame) {
        bgfx::setViewFrameBuffer(kSkyView, sceneFb_);
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
    // Phase 5b (PORT_PROGRESS.md): sun/hemisphere lighting uniforms, now
    // real per-track data resolved once in setTrack() (env_presets.h)
    // instead of Phase 5a's hardcoded 'noon-grass' constants. Sun direction
    // is TOWARD the sun (matches fs_lit.sc's `dot(n, u_sunDir)`).
    bgfx::setUniform(uSunDir_, sunDir_);
    bgfx::setUniform(uSunColor_, sunColor_);
    bgfx::setUniform(uHemiSky_, hemiSky_);
    bgfx::setUniform(uHemiGround_, hemiGround_);

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

    // Phase 5b (PORT_PROGRESS.md): the ground plane draws first (painter's-
    // algorithm ordering, though depth testing makes the exact order moot)
    // so the ribbon and cars are never occluded by it.
    if (groundVertexCount_ > 0) {
        bgfx::setTransform(identity);
        bgfx::setVertexBuffer(0, groundVb_, 0, groundVertexCount_);
        bgfx::setState(state);
        bgfx::submit(kView, litProgram_);
    }

    if (trackVertexCount_ > 0) {
        bgfx::setTransform(identity);
        bgfx::setVertexBuffer(0, trackVb_, 0, trackVertexCount_);
        bgfx::setState(state);
        bgfx::submit(kView, litProgram_);
    }

    // Phase 5d (PORT_PROGRESS.md): stands + pit road + outer wall, drawn
    // after the ribbon (real per-triangle lighting from riser/seat slope
    // angles is the first geometry in this port to actually vary with
    // normal direction beyond the ribbon's own banking).
    if (stadiumVertexCount_ > 0) {
        bgfx::setTransform(identity);
        bgfx::setVertexBuffer(0, stadiumVb_, 0, stadiumVertexCount_);
        bgfx::setState(state);
        bgfx::submit(kView, litProgram_);
    }

    // Phase 5e (PORT_PROGRESS.md): front-tier stand seats, crowd-atlas
    // textured -- same lit state as everything else in this view, just a
    // different program/vertex layout/bound texture.
    if (stadiumTexturedVertexCount_ > 0 && bgfx::isValid(atlasTexture_)) {
        bgfx::setTransform(identity);
        bgfx::setVertexBuffer(0, stadiumTexturedVb_, 0, stadiumTexturedVertexCount_);
        bgfx::setTexture(0, uSkyTexColor_, atlasTexture_);
        bgfx::setState(state);
        bgfx::submit(kView, texturedLitProgram_);
    }

    // Phase 5f (PORT_PROGRESS.md): each car now samples its own livery
    // texture (livery.h) instead of a flat vertex color -- one draw call
    // per car (a shared transient buffer no longer works once every car
    // can bind a different texture). UV mapping is a "logged adaptation"
    // (livery.h's own header comment): JS's carU()/carV() map onto a full
    // 3D lofted body this port doesn't have; here the quad's long axis
    // (nose-tail) maps to carU()'s own linear formula and the short axis
    // (left-right) maps to a narrow band straddling the roof/number-decal
    // region (v=0.30-0.70) -- roughly what a camera looking straight down
    // would actually see, not the full wraparound side profile.
    if (!cars.empty() && track_) {
        const float hl = (float)(CAR.len / 2.0);
        const float hw = (float)(CAR.wid / 2.0);
        for (auto& c : cars) {
            const float ch = (float)c.hdg;
            const float cs = std::cos(ch), sn = std::sin(ch);
            const float wx = (float)c.x, wy = (float)c.y;
            const Vec3 carPos = pos3(*track_, c.s, c.lat);
            const Vec3 carUp = surfaceUp(*track_, c.s);
            const float carY = (float)carPos.y;
            const float lx[4] = {hl, hl, -hl, -hl};
            const float ly[4] = {hw, -hw, -hw, hw};
            float px[4], py[4], uu[4], vv[4];
            for (int k = 0; k < 4; ++k) {
                px[k] = wx + lx[k] * cs - ly[k] * sn;
                py[k] = wy + lx[k] * sn + ly[k] * cs;
                uu[k] = 0.02f + (hl - lx[k]) / (2.0f * hl) * 0.76f; // carU(), nose->0.02, tail->0.78
                vv[k] = 0.5f + (ly[k] / hw) * 0.20f;                // roof-straddling band
            }
            const float nx = (float)carUp.x, ny = (float)carUp.y, nz = (float)carUp.z;
            if (bgfx::getAvailTransientVertexBuffer(6, texturedLitLayout_) < 6) continue;
            bgfx::TransientVertexBuffer tvb;
            bgfx::allocTransientVertexBuffer(&tvb, 6, texturedLitLayout_);
            auto* vertex = (PosNormalUVVertex*)tvb.data;
            vertex[0] = {px[0], carY, py[0], nx, ny, nz, uu[0], vv[0]};
            vertex[1] = {px[1], carY, py[1], nx, ny, nz, uu[1], vv[1]};
            vertex[2] = {px[2], carY, py[2], nx, ny, nz, uu[2], vv[2]};
            vertex[3] = {px[0], carY, py[0], nx, ny, nz, uu[0], vv[0]};
            vertex[4] = {px[2], carY, py[2], nx, ny, nz, uu[2], vv[2]};
            vertex[5] = {px[3], carY, py[3], nx, ny, nz, uu[3], vv[3]};

            const bgfx::TextureHandle carTex = getOrBuildCarTexture(c);
            bgfx::setTransform(identity);
            bgfx::setVertexBuffer(0, &tvb, 0, 6);
            bgfx::setTexture(0, uSkyTexColor_, carTex);
            bgfx::setState(state);
            bgfx::submit(kView, texturedLitProgram_);
        }
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
    {
        const bgfx::ViewId kBloomBrightView = 2;
        const bgfx::ViewId kBloomBlurView = 3;
        const bgfx::ViewId kGradeTonemapView = 4;
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
            const float gradeParams1[4] = {0.32f, 1.04f, 0.0f, 0.94f};
            const float gradeParams2[4] = {1.10f, 0.55f, 0.95f, 0.0f};
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
    if (!uiVerts.empty()) {
        const bgfx::ViewId kUiView = 5;
        bgfx::setViewFrameBuffer(kUiView, BGFX_INVALID_HANDLE);
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
