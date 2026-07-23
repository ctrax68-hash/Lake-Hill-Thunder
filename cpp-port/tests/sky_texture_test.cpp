// Verifies sky_texture.h/.cpp's pure pixel-buffer math (bgfx-free, same
// category as atlas/livery generators will be in later sub-phases): the
// base gradient's known stops, and a detectable brightness bump near the
// sun-glow's position when a preset is supplied.

#include "../src/render/sky_texture.h"

#include <cmath>
#include <cstdio>

namespace {

int g_failures = 0;

struct Px {
    double r, g, b;
};

Px pixelAt(const std::vector<uint8_t>& pixels, int x, int y) {
    const size_t idx = ((size_t)y * kSkyTextureWidth + (size_t)x) * 4;
    return {pixels[idx] / 255.0, pixels[idx + 1] / 255.0, pixels[idx + 2] / 255.0};
}

double luminance(const Px& p) {
    return 0.2126 * p.r + 0.7152 * p.g + 0.0722 * p.b;
}

void expectNear(const char* label, double got, double expected, double tol) {
    if (std::fabs(got - expected) > tol) {
        std::fprintf(stderr, "%s: got %.4f expected %.4f (tol %.4f)\n", label, got, expected, tol);
        ++g_failures;
    }
}

void expectTrue(const char* label, bool cond) {
    if (!cond) {
        std::fprintf(stderr, "%s: FAILED\n", label);
        ++g_failures;
    }
}

} // namespace

int main() {
    const Sky sky{{0.78, 0.86, 0.94}, {0.20, 0.45, 0.85}, "none"}; // Thunder Oval's real sky data

    // No sun preset: pure gradient, top row == zenith, 0.82 row == horizon.
    {
        const std::vector<uint8_t> pixels = buildSkyPixels(sky, nullptr);
        expectTrue("pixel buffer sized WxHx4", pixels.size() == (size_t)kSkyTextureWidth * kSkyTextureHeight * 4);

        const Px top = pixelAt(pixels, kSkyTextureWidth / 2, 0);
        expectNear("top row r == zenith.r", top.r, sky.zenith[0], 1.0 / 255.0 + 1e-6);
        expectNear("top row g == zenith.g", top.g, sky.zenith[1], 1.0 / 255.0 + 1e-6);
        expectNear("top row b == zenith.b", top.b, sky.zenith[2], 1.0 / 255.0 + 1e-6);

        const int horizonRow = (int)std::lround(0.82 * (kSkyTextureHeight - 1));
        const Px horiz = pixelAt(pixels, kSkyTextureWidth / 2, horizonRow);
        expectNear("0.82 row r == horizon.r", horiz.r, sky.horizon[0], 2.0 / 255.0 + 1e-6);
        expectNear("0.82 row g == horizon.g", horiz.g, sky.horizon[1], 2.0 / 255.0 + 1e-6);
        expectNear("0.82 row b == horizon.b", horiz.b, sky.horizon[2], 2.0 / 255.0 + 1e-6);

        // Bottom row is a lightened haze band -- strictly brighter than the
        // horizon color itself (both channels below 255, so clamping never
        // masks the +18/255 offset for this particular sky.horizon).
        const Px bottom = pixelAt(pixels, kSkyTextureWidth / 2, kSkyTextureHeight - 1);
        expectTrue("bottom row is brighter than horizon (haze lightening)",
                   luminance(bottom) > luminance({sky.horizon[0], sky.horizon[1], sky.horizon[2]}) + 0.01);
    }

    // With a sun preset: a detectable brightness bump right at the glow's
    // computed position, versus the same row far from the glow center.
    {
        const EnvPreset preset{35, 55, {1.0, 0.9569, 0.8784}, 3.2, {0, 0, 0}, {0, 0, 0}, 1.0};
        const std::vector<uint8_t> pixels = buildSkyPixels(sky, &preset);
        const double sunY = kSkyTextureHeight * (1.0 - 55.0 / 100.0);
        const int row = (int)std::lround(sunY);
        const Px center = pixelAt(pixels, kSkyTextureWidth / 2, row);
        const Px edge = pixelAt(pixels, 0, row); // same row, far from the glow's horizontal center
        expectTrue("sun glow brightens its center vs. the same row's edge",
                   luminance(center) > luminance(edge) + 0.02);
    }

    if (g_failures == 0) {
        std::printf("sky_texture_test: gradient stops and sun-glow bump both match expectations.\n");
        return 0;
    }
    std::fprintf(stderr, "sky_texture_test: %d MISMATCHES.\n", g_failures);
    return 1;
}
