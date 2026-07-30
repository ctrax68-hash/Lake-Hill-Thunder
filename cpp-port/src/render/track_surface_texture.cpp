#include "track_surface_texture.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>

namespace {

// A deterministic per-pixel hash (no RNG dependency, no seed to thread
// through) -- same "closed-form analytic pattern" style atlas_texture.cpp's
// paintWallPattern()/paintFenceBand() already use instead of a stateful
// noise generator. Returns a value in [0,1).
double hashNoise(int x, int y) {
    uint32_t h = (uint32_t)(x * 374761393 + y * 668265263 + 374761397);
    h = (h ^ (h >> 13)) * 1274126177u;
    h ^= (h >> 16);
    return (double)(h & 0x00FFFFFFu) / (double)0x01000000u;
}

} // namespace

std::vector<uint8_t> buildAsphaltPixels() {
    constexpr int W = kAsphaltTextureSize, H = kAsphaltTextureSize;
    std::vector<uint8_t> pixels((size_t)W * H * 4);

    const std::array<double, 3> base{0.25, 0.25, 0.27};
    const std::array<double, 3> groove{0.15, 0.15, 0.16};  // tire-rubber buildup, racing groove
    const std::array<double, 3> apron{0.34, 0.32, 0.28};   // warm-tinted inner apron

    // Groove band centered a bit inside the midline (favors the low/inside
    // line real stock cars run), soft-edged via linear falloff rather than
    // a hard cutoff.
    constexpr double kGrooveCenter = 0.42, kGrooveHalfWidth = 0.09;
    constexpr double kApronWidth = 0.08;

    for (int y = 0; y < H; ++y) {
        const double v = (double)y / (double)(H - 1);
        const double grooveDist = std::abs(v - kGrooveCenter);
        const double grooveT = grooveDist < kGrooveHalfWidth ? 1.0 - grooveDist / kGrooveHalfWidth : 0.0;
        const double apronT = v < kApronWidth ? 1.0 - v / kApronWidth : 0.0;
        for (int x = 0; x < W; ++x) {
            const double speckle = 0.85 + hashNoise(x, y) * 0.30;  // +-15% per-pixel brightness jitter
            std::array<double, 3> c{base[0] * speckle, base[1] * speckle, base[2] * speckle};
            for (int k = 0; k < 3; ++k) {
                c[k] = c[k] * (1.0 - grooveT) + groove[k] * grooveT;
                c[k] = c[k] * (1.0 - apronT) + apron[k] * apronT;
            }
            const size_t idx = ((size_t)y * W + (size_t)x) * 4;
            pixels[idx + 0] = (uint8_t)std::lround(std::clamp(c[0], 0.0, 1.0) * 255.0);
            pixels[idx + 1] = (uint8_t)std::lround(std::clamp(c[1], 0.0, 1.0) * 255.0);
            pixels[idx + 2] = (uint8_t)std::lround(std::clamp(c[2], 0.0, 1.0) * 255.0);
            pixels[idx + 3] = 255;
        }
    }
    return pixels;
}
