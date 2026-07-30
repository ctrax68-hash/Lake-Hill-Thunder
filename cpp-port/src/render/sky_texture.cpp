#include "sky_texture.h"

#include "../sim/rng.h"

#include <algorithm>
#include <cmath>

namespace {

struct Rgb {
    double r, g, b;
};

// The base gradient's 3 stops (index.html:3729-3737): 0 -> zenith,
// 0.82 -> horizon, 1.0 -> horizon lightened (a fake atmospheric-scattering
// brightening right at the horizon line).
Rgb sampleGradient(const std::array<double, 3>& zenith, const std::array<double, 3>& horizon, double t) {
    const double hazeR = std::min(1.0, horizon[0] * 1.12 + 18.0 / 255.0);
    const double hazeG = std::min(1.0, horizon[1] * 1.12 + 18.0 / 255.0);
    const double hazeB = std::min(1.0, horizon[2] * 1.12 + 18.0 / 255.0);
    if (t <= 0.82) {
        const double f = t / 0.82;
        return {zenith[0] + (horizon[0] - zenith[0]) * f, zenith[1] + (horizon[1] - zenith[1]) * f,
                zenith[2] + (horizon[2] - zenith[2]) * f};
    }
    const double f = (t - 0.82) / (1.0 - 0.82);
    return {horizon[0] + (hazeR - horizon[0]) * f, horizon[1] + (hazeG - horizon[1]) * f,
            horizon[2] + (hazeB - horizon[2]) * f};
}

} // namespace

std::vector<uint8_t> buildSkyPixels(const Sky& sky, const EnvPreset* sunPreset) {
    std::vector<uint8_t> pixels((size_t)kSkyTextureWidth * kSkyTextureHeight * 4, 0);

    // Source-over alpha blend into whatever's already in the buffer --
    // matches canvas's default composite operation, since the glow and
    // cloud passes below draw on top of the base gradient.
    auto blendPixel = [&](int x, int y, double r, double g, double b, double a) {
        const size_t idx = ((size_t)y * kSkyTextureWidth + (size_t)x) * 4;
        const double existR = pixels[idx] / 255.0, existG = pixels[idx + 1] / 255.0, existB = pixels[idx + 2] / 255.0;
        pixels[idx] = (uint8_t)std::lround(std::clamp(existR * (1 - a) + r * a, 0.0, 1.0) * 255.0);
        pixels[idx + 1] = (uint8_t)std::lround(std::clamp(existG * (1 - a) + g * a, 0.0, 1.0) * 255.0);
        pixels[idx + 2] = (uint8_t)std::lround(std::clamp(existB * (1 - a) + b * a, 0.0, 1.0) * 255.0);
        pixels[idx + 3] = 255;
    };

    // Base vertical gradient (index.html:3729-3738).
    for (int y = 0; y < kSkyTextureHeight; ++y) {
        const double t = (double)y / (kSkyTextureHeight - 1);
        const Rgb c = sampleGradient(sky.zenith, sky.horizon, t);
        for (int x = 0; x < kSkyTextureWidth; ++x) blendPixel(x, y, c.r, c.g, c.b, 1.0);
    }

    // Stylized sun-glow blob (index.html:3739-3748): vertical position from
    // elevation (90=zenith/top, 0=horizon, clamped to [0,85] as JS does),
    // radial falloff from alpha 0.22 at center to 0 at radius H*0.3.
    if (sunPreset) {
        const double el = std::max(0.0, std::min(85.0, sunPreset->elevationDeg));
        const double sunY = kSkyTextureHeight * (1.0 - el / 100.0);
        const double radius = kSkyTextureHeight * 0.3;
        for (int y = 0; y < kSkyTextureHeight; ++y) {
            for (int x = 0; x < kSkyTextureWidth; ++x) {
                const double dx = x - kSkyTextureWidth / 2.0, dy = y - sunY;
                const double d = std::sqrt(dx * dx + dy * dy);
                if (d >= radius) continue;
                const double a = 0.22 * (1.0 - d / radius);
                blendPixel(x, y, sunPreset->sunColor[0], sunPreset->sunColor[1], sunPreset->sunColor[2], a);
            }
        }
    }

    // G5c (NASCAR-Thunder gap-analysis plan, sky/cloud improvement):
    // replaced two flat horizontal streaks with a handful of soft
    // elliptical cloud-puff blobs, reusing the sun-glow blob's own radial-
    // falloff technique above (`a` scaled by `1 - d/radius`) instead of
    // inventing a new one -- still not real cloud silhouettes, but a much
    // less obviously-artificial "two flat bars" tell than before. Same
    // rng2 seed (mulberry32(777)) as JS's own scenery-only stream --
    // render-only data, doesn't affect gameplay determinism, same already-
    // established "safe to have its own local RNG instance" precedent
    // noted elsewhere in this port.
    Mulberry32 rng2(777);
    constexpr int kCloudCount = 5;
    for (int i = 0; i < kCloudCount; ++i) {
        const double cx = rng2.next() * kSkyTextureWidth;
        const double cy = kSkyTextureHeight * (0.08 + rng2.next() * 0.40);
        const double rx = kSkyTextureWidth * (0.18 + rng2.next() * 0.22);
        const double ry = rx * (0.25 + rng2.next() * 0.15);
        const double peakA = 0.08 + rng2.next() * 0.08;
        const int x0 = std::max(0, (int)std::floor(cx - rx));
        const int x1 = std::min(kSkyTextureWidth, (int)std::ceil(cx + rx));
        const int y0 = std::max(0, (int)std::floor(cy - ry));
        const int y1 = std::min(kSkyTextureHeight, (int)std::ceil(cy + ry));
        for (int y = y0; y < y1; ++y) {
            for (int x = x0; x < x1; ++x) {
                const double dx = (x - cx) / rx, dy = (y - cy) / ry;
                const double d = std::sqrt(dx * dx + dy * dy);
                if (d >= 1.0) continue;
                blendPixel(x, y, 1.0, 1.0, 1.0, peakA * (1.0 - d));
            }
        }
    }

    return pixels;
}
