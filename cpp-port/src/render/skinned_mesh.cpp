#include "skinned_mesh.h"

#include "shaders_embedded.h"
#include "texture_import.h"
#include "vertex_skinned.h"

#include <algorithm>
#include <cstdint>

namespace {

bgfx::ProgramHandle g_program = BGFX_INVALID_HANDLE;
bgfx::UniformHandle g_uBoneMatrices = BGFX_INVALID_HANDLE;
bgfx::UniformHandle g_sTexColor = BGFX_INVALID_HANDLE;

void ensureSharedResources() {
    if (bgfx::isValid(g_program)) return;
    const bgfx::RendererType::Enum rendererType = bgfx::getRendererType();
    bgfx::ShaderHandle vsh = bgfx::createEmbeddedShader(s_embeddedShaders, rendererType, "vs_skinned");
    bgfx::ShaderHandle fsh = bgfx::createEmbeddedShader(s_embeddedShaders, rendererType, "fs_textured_lit");
    g_program = bgfx::createProgram(vsh, fsh, true);
    g_uBoneMatrices = bgfx::createUniform("u_boneMatrices", bgfx::UniformType::Mat4, SkinnedMesh::kMaxBones);
    g_sTexColor = bgfx::createUniform("s_texColor", bgfx::UniformType::Sampler);
}

} // namespace

bool SkinnedMesh::create(const ImportedMesh& mesh) {
    if (mesh.vertices.empty() || mesh.indices.empty()) return false;
    ensureSharedResources();

    std::vector<PosNormalUVSkinnedVertex> verts(mesh.vertices.size());
    for (size_t i = 0; i < mesh.vertices.size(); ++i) {
        const ImportedVertex& src = mesh.vertices[i];
        PosNormalUVSkinnedVertex& v = verts[i];
        v.x = (float)src.px;
        v.y = (float)src.py;
        v.z = (float)src.pz;
        v.nx = (float)src.nx;
        v.ny = (float)src.ny;
        v.nz = (float)src.nz;
        v.u = (float)src.u;
        v.v = (float)src.v;
        for (int k = 0; k < 4; ++k) {
            v.joints[k] = (uint8_t)std::min<uint16_t>(src.joints[k], 255);
            v.weights[k] = (float)src.weights[k];
        }
    }
    static const bgfx::VertexLayout layout = PosNormalUVSkinnedVertex::layout();
    vb_ = bgfx::createVertexBuffer(
        bgfx::copy(verts.data(), (uint32_t)(verts.size() * sizeof(PosNormalUVSkinnedVertex))), layout);

    const bool use32BitIndices = mesh.vertices.size() > 0xFFFF;
    if (use32BitIndices) {
        ib_ = bgfx::createIndexBuffer(bgfx::copy(mesh.indices.data(), (uint32_t)(mesh.indices.size() * sizeof(uint32_t))),
                                       BGFX_BUFFER_INDEX32);
    } else {
        std::vector<uint16_t> idx16(mesh.indices.size());
        for (size_t i = 0; i < mesh.indices.size(); ++i) idx16[i] = (uint16_t)mesh.indices[i];
        ib_ = bgfx::createIndexBuffer(bgfx::copy(idx16.data(), (uint32_t)(idx16.size() * sizeof(uint16_t))));
    }
    indexCount_ = (uint32_t)mesh.indices.size();

    // Texture: decode the embedded base color image if present, otherwise
    // upload a solid 1x1 texture of baseColorFactor -- either way the
    // fragment shader (fs_textured_lit.sc) always has something to sample.
    if (!mesh.baseColorImageBytes.empty()) {
        DecodedImage img = decodeImageRGBA8(mesh.baseColorImageBytes.data(), mesh.baseColorImageBytes.size());
        if (img.ok) {
            texture_ = bgfx::createTexture2D((uint16_t)img.width, (uint16_t)img.height, false, 1,
                                              bgfx::TextureFormat::RGBA8, 0,
                                              bgfx::copy(img.rgba8.data(), (uint32_t)img.rgba8.size()));
        }
    }
    if (!bgfx::isValid(texture_)) {
        const uint8_t pixel[4] = {
            (uint8_t)std::clamp(mesh.baseColorFactor[0] * 255.0, 0.0, 255.0),
            (uint8_t)std::clamp(mesh.baseColorFactor[1] * 255.0, 0.0, 255.0),
            (uint8_t)std::clamp(mesh.baseColorFactor[2] * 255.0, 0.0, 255.0),
            (uint8_t)std::clamp(mesh.baseColorFactor[3] * 255.0, 0.0, 255.0),
        };
        texture_ = bgfx::createTexture2D(1, 1, false, 1, bgfx::TextureFormat::RGBA8, 0, bgfx::copy(pixel, 4));
    }

    return bgfx::isValid(vb_) && bgfx::isValid(ib_) && bgfx::isValid(texture_);
}

void SkinnedMesh::destroy() {
    if (bgfx::isValid(vb_)) bgfx::destroy(vb_);
    if (bgfx::isValid(ib_)) bgfx::destroy(ib_);
    if (bgfx::isValid(texture_)) bgfx::destroy(texture_);
    vb_ = BGFX_INVALID_HANDLE;
    ib_ = BGFX_INVALID_HANDLE;
    texture_ = BGFX_INVALID_HANDLE;
    indexCount_ = 0;
}

void SkinnedMesh::setBoneMatrices(const float* matrices4x4ColMajor, int jointCount) {
    ensureSharedResources();
    const int n = std::min(jointCount, kMaxBones);
    if (n > 0) bgfx::setUniform(g_uBoneMatrices, matrices4x4ColMajor, (uint16_t)n);
}

void SkinnedMesh::draw(uint16_t view, const float* modelMatrix4x4ColMajor, bgfx::TextureHandle textureOverride) const {
    if (!isValid()) return;
    bgfx::setTransform(modelMatrix4x4ColMajor);
    bgfx::setVertexBuffer(0, vb_);
    bgfx::setIndexBuffer(ib_, 0, indexCount_);
    bgfx::setTexture(0, g_sTexColor, bgfx::isValid(textureOverride) ? textureOverride : texture_);
    // No BGFX_STATE_CULL_* -- matches renderer.cpp's own lit/textured-lit
    // draw state (see its renderFrame(), which applies no backface culling
    // anywhere either), rather than guessing this pipeline's winding-order
    // convention.
    bgfx::setState(BGFX_STATE_WRITE_RGB | BGFX_STATE_WRITE_A | BGFX_STATE_WRITE_Z | BGFX_STATE_DEPTH_TEST_LESS);
    bgfx::submit(view, g_program);
}
