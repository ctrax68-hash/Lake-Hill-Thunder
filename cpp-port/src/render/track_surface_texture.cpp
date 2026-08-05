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
    // Arithmetic done in uint32_t, not int: `x * 374761393` overflows a
    // signed 32-bit int for any x >= 6, which is undefined behaviour, and
    // GCC does diagnose it here ("iteration 2 invokes undefined behavior
    // [-Waggressive-loop-optimizations]") -- it happened to produce the
    // intended wraparound so far, but the compiler is free not to. Unsigned
    // overflow is well-defined wraparound, which is what this hash wants.
    // Pre-existing since G5a; fixed in G17 while reworking this file.
    uint32_t h = (uint32_t)x * 374761393u + (uint32_t)y * 668265263u + 374761397u;
    h = (h ^ (h >> 13)) * 1274126177u;
    h ^= (h >> 16);
    return (double)(h & 0x00FFFFFFu) / (double)0x01000000u;
}

} // namespace

std::vector<uint8_t> buildAsphaltPixels(double seamEvery) {
    constexpr int W = kAsphaltTextureSize, H = kAsphaltTextureSize;
    std::vector<uint8_t> pixels((size_t)W * H * 4);

    const std::array<double, 3> base{0.25, 0.25, 0.27};
    const std::array<double, 3> groove{0.15, 0.15, 0.16};  // tire-rubber buildup, racing groove
    const std::array<double, 3> apron{0.34, 0.32, 0.28};   // warm-tinted inner apron
    const std::array<double, 3> seamCol{0.13, 0.13, 0.14}; // tar-filled expansion joint
    const std::array<double, 3> lineY{0.88, 0.74, 0.10};   // apron boundary line (yellow)
    const std::array<double, 3> lineW{0.86, 0.86, 0.87};   // outer blend line (white)

    // G17 (NT2003 presentation plan): three groove bands rather than G5a's
    // single one. Real ovals wear in a distinct low line, a middle "hub"
    // line and (on worn/high-banked tracks) a high line, and the reference
    // footage reads as several concentric darker arcs rather than one
    // stripe -- three bands of decreasing strength reproduce that at a
    // glance. The primary band keeps G5a's exact centre/width so the
    // established racing line doesn't move.
    struct GrooveBand {
        double centre, halfWidth, strength;
    };
    constexpr std::array<GrooveBand, 3> kGrooves{{
        {0.42, 0.090, 1.00}, // primary (unchanged from G5a)
        {0.26, 0.055, 0.55}, // low line
        {0.60, 0.050, 0.40}, // high line
    }};
    constexpr double kApronWidth = 0.08;
    // Yellow apron boundary just outside the warm apron tint, and a white
    // blend line at the outer edge -- both present on essentially every
    // real oval and clearly visible in the reference footage.
    constexpr double kYellowV0 = 0.086, kYellowV1 = 0.112;
    constexpr double kWhiteV0 = 0.952, kWhiteV1 = 0.980;
    // One transverse expansion seam per tile, at the tile's U=0 edge. See
    // asphaltTileLength(): tile length IS `seamEvery`, so this lands seams
    // exactly `seamEvery` world units apart. Width is in U fraction, so it
    // scales with tile length and stays a roughly constant world width.
    const bool drawSeam = seamEvery > 0.0;
    const double seamHalfU = drawSeam ? std::min(0.03, 0.22 / seamEvery) : 0.0;

    for (int y = 0; y < H; ++y) {
        const double v = (double)y / (double)(H - 1);
        double grooveT = 0.0;
        for (const GrooveBand& g : kGrooves) {
            const double d = std::abs(v - g.centre);
            if (d < g.halfWidth) grooveT = std::max(grooveT, (1.0 - d / g.halfWidth) * g.strength);
        }
        const double apronT = v < kApronWidth ? 1.0 - v / kApronWidth : 0.0;
        const bool onYellow = v >= kYellowV0 && v < kYellowV1;
        const bool onWhite = v >= kWhiteV0 && v < kWhiteV1;
        for (int x = 0; x < W; ++x) {
            const double u = (double)x / (double)(W - 1);
            const double speckle = 0.85 + hashNoise(x, y) * 0.30;  // +-15% per-pixel brightness jitter
            std::array<double, 3> c{base[0] * speckle, base[1] * speckle, base[2] * speckle};
            for (int k = 0; k < 3; ++k) {
                c[k] = c[k] * (1.0 - grooveT) + groove[k] * grooveT;
                c[k] = c[k] * (1.0 - apronT) + apron[k] * apronT;
            }
            // Painted lines sit on top of groove/apron: real ones are
            // repainted over the worn surface, not worn through by it.
            if (onYellow)
                for (int k = 0; k < 3; ++k) c[k] = lineY[k] * (0.92 + speckle * 0.08);
            if (onWhite)
                for (int k = 0; k < 3; ++k) c[k] = lineW[k] * (0.92 + speckle * 0.08);
            // Seam last, and it crosses everything including the lines --
            // an expansion joint is cut through the whole surface. Wrapped
            // so the band straddles the tile edge seamlessly.
            if (drawSeam) {
                const double du = std::min(u, 1.0 - u);
                if (du < seamHalfU) {
                    const double t = 1.0 - du / seamHalfU;
                    for (int k = 0; k < 3; ++k) c[k] = c[k] * (1.0 - t) + seamCol[k] * t;
                }
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
