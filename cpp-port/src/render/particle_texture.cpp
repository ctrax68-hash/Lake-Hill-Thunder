#include "particle_texture.h"

#include <algorithm>
#include <cmath>

std::vector<uint8_t> buildParticleSpritePixels() {
    std::vector<uint8_t> pixels((size_t)kParticleTextureSize * kParticleTextureSize * 4, 0);
    const double cx = kParticleTextureSize / 2.0, cy = kParticleTextureSize / 2.0;
    const double radius = kParticleTextureSize / 2.0;
    for (int y = 0; y < kParticleTextureSize; ++y) {
        for (int x = 0; x < kParticleTextureSize; ++x) {
            const double dx = (x + 0.5) - cx, dy = (y + 0.5) - cy;
            const double t = std::clamp(std::sqrt(dx * dx + dy * dy) / radius, 0.0, 1.0);
            // Two linear segments matching canvas's own interpolation
            // between 3 stops: 1.0 @ t=0, 0.55 @ t=0.5, 0.0 @ t=1.0.
            const double a = t <= 0.5 ? 1.0 + (0.55 - 1.0) * (t / 0.5) : 0.55 + (0.0 - 0.55) * ((t - 0.5) / 0.5);
            const size_t idx = ((size_t)y * kParticleTextureSize + (size_t)x) * 4;
            pixels[idx] = 255;
            pixels[idx + 1] = 255;
            pixels[idx + 2] = 255;
            pixels[idx + 3] = (uint8_t)std::lround(std::clamp(a, 0.0, 1.0) * 255.0);
        }
    }
    return pixels;
}
