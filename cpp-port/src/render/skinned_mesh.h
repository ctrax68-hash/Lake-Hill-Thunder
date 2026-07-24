#pragma once

// Step 2 (PORT_PROGRESS.md, glTF skinned-mesh import pipeline): GPU-side
// consumer of mesh_import.h's ImportedMesh -- uploads vertex/index/texture
// buffers and draws with GPU linear-blend skinning (vs_skinned.sc +
// fs_textured_lit.sc). bgfx-calling, deliberately NOT unit-tested: this
// port's established convention (stadium_mesh.cpp/atlas_texture.cpp/etc.)
// is a bgfx-free pure-logic builder feeding a thin bgfx-calling consumer,
// where only the pure-logic half gets a ctest -- the GPU half is verified
// via a headless xvfb-run screenshot instead, same as every other
// bgfx-touching module in this project (renderer.cpp itself is the
// canonical example: no test target anywhere instantiates real bgfx
// buffers/programs/draws).
//
// The shared shader program/uniform/sampler are function-local statics,
// created once on first use (guarded by bgfx::isValid(), same pattern
// renderer.cpp uses for its own per-feature programs) rather than fields
// on Renderer -- this keeps the class self-contained for this pass's own
// verification without needing to touch renderer.cpp/main.cpp's render
// loop, which is Step 3's job (replacing the flat car quad). Step 3 can
// freely move this ownership into Renderer's init()/shutdown() once it
// wires SkinnedMesh in for real, without changing SkinnedMesh's public API.

#include "mesh_import.h"

#include <bgfx/bgfx.h>

class SkinnedMesh {
public:
    static constexpr int kMaxBones = 32; // matches vs_skinned.sc's MAX_BONES

    // Uploads `mesh`'s vertex/index data and a texture (decoded from
    // mesh.baseColorImageBytes via texture_import.h if present, otherwise a
    // solid 1x1 texture of mesh.baseColorFactor). Lazily creates the shared
    // program/uniforms on first call across all instances. Returns false
    // (leaving the instance in its default, isValid()==false state) if
    // `mesh` has no vertices/indices.
    bool create(const ImportedMesh& mesh);

    // Releases this instance's own GPU resources (vertex/index/texture
    // buffers). Safe to call on an already-destroyed or never-created
    // instance. Does NOT release the shared program/uniforms -- those
    // outlive every instance, matching every other bgfx resource in this
    // port that's created once in Renderer::init() and destroyed once in
    // Renderer::shutdown().
    void destroy();

    bool isValid() const { return bgfx::isValid(vb_); }

    // Uploads this frame's bone matrix palette -- one column-major 4x4
    // matrix per joint, in the same order as the ImportedMesh::joints this
    // instance was created from. `jointCount` must be <= kMaxBones; bones
    // beyond `jointCount` are left as whatever the uniform last held (every
    // vertex's joint indices/weights only ever reference valid joints from
    // its own mesh, so stale trailing entries are never actually sampled).
    static void setBoneMatrices(const float* matrices4x4ColMajor, int jointCount);

    // Draws with the given world transform (column-major 4x4) into `view`.
    // `textureOverride`, if valid, is bound instead of this instance's own
    // texture_ -- Step 3's per-car livery use case: one shared SkinnedMesh
    // (the car rig's geometry, uploaded once) drawn once per car, each with
    // a different already-built livery texture (Renderer::getOrBuildCarTexture()).
    void draw(uint16_t view, const float* modelMatrix4x4ColMajor,
              bgfx::TextureHandle textureOverride = BGFX_INVALID_HANDLE) const;

private:
    bgfx::VertexBufferHandle vb_ = BGFX_INVALID_HANDLE;
    bgfx::IndexBufferHandle ib_ = BGFX_INVALID_HANDLE;
    bgfx::TextureHandle texture_ = BGFX_INVALID_HANDLE;
    uint32_t indexCount_ = 0;
};
