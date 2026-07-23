#pragma once

#include <cstdint>

// Hoisted out of renderer.cpp's anonymous namespace (Phase 4e,
// PORT_PROGRESS.md) so UI-overlay geometry (ui_draw.cpp and everything
// built on it) can share the exact same vertex layout as the world-space
// track ribbon and car boxes -- both are drawn with the same flat-color
// shader/program, just under different bgfx views/transforms (see
// renderer.cpp's two-view renderFrame()).
struct PosColorVertex {
    float x, y, z;
    uint32_t abgr;
};
