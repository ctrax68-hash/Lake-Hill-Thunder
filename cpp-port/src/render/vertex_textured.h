#pragma once

#include <bgfx/bgfx.h>

// Phase 5e (PORT_PROGRESS.md): world-space geometry that samples a texture
// but still responds to the same hemisphere+directional lighting as
// PosNormalColorVertex's flat-colored path (vs_lit.sc/fs_lit.sc) -- used
// for the crowd-tile-textured front stand tiers. No per-vertex color: the
// atlas texture supplies the base color directly (fs_textured_lit.sc
// multiplies the sampled texel by the lighting amount, mirroring fs_lit.sc's
// `v_color0.rgb * lightAmt` but with a texel in place of the vertex color).
struct PosNormalUVVertex {
    float x, y, z;
    float nx, ny, nz;
    float u, v;

    static bgfx::VertexLayout layout() {
        bgfx::VertexLayout l;
        l.begin()
            .add(bgfx::Attrib::Position, 3, bgfx::AttribType::Float)
            .add(bgfx::Attrib::Normal, 3, bgfx::AttribType::Float)
            .add(bgfx::Attrib::TexCoord0, 2, bgfx::AttribType::Float)
            .end();
        return l;
    }
};
