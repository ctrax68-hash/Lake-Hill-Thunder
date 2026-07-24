#pragma once

// Step 2 (PORT_PROGRESS.md, glTF skinned-mesh import pipeline): vertex
// format for GPU-skinned meshes, alongside this port's existing vertex
// structs (vertex.h's PosColorVertex, vertex_lit.h's PosNormalColorVertex,
// vertex_textured.h's PosNormalUVVertex) -- same "plain struct + static
// layout()" shape as all of them. Adds joint indices (Indices attribute,
// 4x uint8) and joint weights (Weight attribute, 4x float) on top of
// PosNormalUVVertex's position/normal/uv, consumed by vs_skinned.sc to
// blend a weighted sum of bone matrices per vertex.
//
// Joint indices are plain uint8 (not normalized) -- glTF's JOINTS_0
// convention this port's mesh_import.h already follows -- so a rig can
// have up to 256 joints, comfortably more than any wheel/suspension rig
// this project needs.

#include <bgfx/bgfx.h>

struct PosNormalUVSkinnedVertex {
    float x, y, z;
    float nx, ny, nz;
    float u, v;
    uint8_t joints[4];
    float weights[4];

    static bgfx::VertexLayout layout() {
        bgfx::VertexLayout l;
        l.begin()
            .add(bgfx::Attrib::Position, 3, bgfx::AttribType::Float)
            .add(bgfx::Attrib::Normal, 3, bgfx::AttribType::Float)
            .add(bgfx::Attrib::TexCoord0, 2, bgfx::AttribType::Float)
            .add(bgfx::Attrib::Indices, 4, bgfx::AttribType::Uint8)
            .add(bgfx::Attrib::Weight, 4, bgfx::AttribType::Float)
            .end();
        return l;
    }
};
