#pragma once

#include <bgfx/bgfx.h>

// Phase 5c (PORT_PROGRESS.md): a minimal Position+UV vertex format for the
// sky background's fullscreen textured quad -- the only textured geometry
// in this port so far. Phase 5e's texture infrastructure (crowd-tile atlas,
// stadium/livery textures) is expected to generalize/replace this with a
// fuller PosNormalUVColorVertex once it exists; until then this one-off
// keeps the sky's textured-quad path self-contained.
struct PosUvVertex {
    float x, y, z;
    float u, v;

    static bgfx::VertexLayout layout() {
        bgfx::VertexLayout l;
        l.begin()
            .add(bgfx::Attrib::Position, 3, bgfx::AttribType::Float)
            .add(bgfx::Attrib::TexCoord0, 2, bgfx::AttribType::Float)
            .end();
        return l;
    }
};
