#pragma once

#include <bgfx/bgfx.h>
#include <cstdint>

// H4 (NT2003 engine-feel plan): vertex format for camera-facing particle
// billboards -- position (already the final world-space quad corner, CPU-
// computed from the camera's own right/up basis each frame), UV (samples
// particle_texture.h's radial-gradient sprite), and a per-vertex color
// whose alpha carries the batch's fixed opacity (0.38 smoke / 0.9 spark,
// JS's own uAlpha uniforms) -- baked per-vertex instead of a uniform so
// both batches can share one shader/program.
struct PosUvColorVertex {
    float x, y, z;
    float u, v;
    uint32_t abgr;

    static bgfx::VertexLayout layout() {
        bgfx::VertexLayout l;
        l.begin()
            .add(bgfx::Attrib::Position, 3, bgfx::AttribType::Float)
            .add(bgfx::Attrib::TexCoord0, 2, bgfx::AttribType::Float)
            .add(bgfx::Attrib::Color0, 4, bgfx::AttribType::Uint8, true)
            .end();
        return l;
    }
};
