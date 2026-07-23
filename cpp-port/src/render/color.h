#pragma once

#include <algorithm>
#include <cstdint>

// bgfx's Uint8x4 Color0 attribute is read back as a single little-endian
// uint32 in R,G,B,A byte order -- i.e. 0xAABBGGRR when written out as a hex
// literal (same convention bgfx's own examples use for `m_abgr`). Hoisted
// out of renderer.cpp's anonymous namespace (Phase 4e, PORT_PROGRESS.md)
// so UI-overlay-emitting code (ui_draw.cpp, seg_bar.cpp, minimap.cpp,
// leaderboard.cpp, results.cpp) can build vertex colors without depending
// on renderer.cpp internals.
inline uint32_t packColor(float r, float g, float b, float a = 1.0f) {
    auto b8 = [](float v) -> uint32_t {
        return (uint32_t)(std::clamp(v, 0.0f, 1.0f) * 255.0f + 0.5f);
    };
    return (b8(a) << 24) | (b8(b) << 16) | (b8(g) << 8) | b8(r);
}

// Phase 4e (PORT_PROGRESS.md): HUD chrome colors, matching index.html's
// THEME table (index.html:3773-3776) exactly. NOT the same table as
// CarPalette's livery colors (src/sim/car.h) -- checked: CarPalette::Orange
// happens to equal THEME.orange, but CarPalette::Red/Blue do NOT equal
// THEME.red/blue, a coincidence, not a shared concept, so a separate table
// here is correct rather than duplicative.
namespace Theme {
constexpr float kBlack[3] = {0x00 / 255.0f, 0x00 / 255.0f, 0x00 / 255.0f};
constexpr float kWhite[3] = {0xff / 255.0f, 0xff / 255.0f, 0xff / 255.0f};
constexpr float kYellow[3] = {0xF7 / 255.0f, 0xD4 / 255.0f, 0x00 / 255.0f};
constexpr float kBlue[3] = {0x1A / 255.0f, 0x4F / 255.0f, 0xFF / 255.0f};
constexpr float kSteel[3] = {0x2A / 255.0f, 0x2A / 255.0f, 0x2A / 255.0f};
constexpr float kOrange[3] = {0xFF / 255.0f, 0x7A / 255.0f, 0x00 / 255.0f};
constexpr float kRed[3] = {0xD6 / 255.0f, 0x28 / 255.0f, 0x28 / 255.0f};
constexpr float kGraycool[3] = {0xC8 / 255.0f, 0xC8 / 255.0f, 0xC8 / 255.0f};
} // namespace Theme

inline uint32_t packColor(const float rgb[3], float a = 1.0f) {
    return packColor(rgb[0], rgb[1], rgb[2], a);
}
