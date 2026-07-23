#pragma once

#include <cstdint>

#include <bgfx/bgfx.h>

// Phase 5a (PORT_PROGRESS.md): the world-space vertex format once real 3D
// geometry (banked track ribbon, stadium/stands in 5d) needs per-vertex
// lighting. The existing `PosColorVertex`/flat shader (vertex.h) stays
// exactly as-is for the pixel-space UI overlay (view 1) -- only the
// world-space view (view 0) moves to this format.
struct PosNormalColorVertex {
    float x, y, z;
    float nx, ny, nz;
    uint32_t abgr;

    static bgfx::VertexLayout layout() {
        bgfx::VertexLayout l;
        l.begin()
            .add(bgfx::Attrib::Position, 3, bgfx::AttribType::Float)
            .add(bgfx::Attrib::Normal, 3, bgfx::AttribType::Float)
            .add(bgfx::Attrib::Color0, 4, bgfx::AttribType::Uint8, true)
            .end();
        return l;
    }
};
