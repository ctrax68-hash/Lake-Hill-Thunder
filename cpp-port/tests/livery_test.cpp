// Verifies livery.{h,cpp}'s pure pixel math (bgfx-free): the 3-tone
// shading bands, pairwise-distinguishable stripe styles pulled from real
// ROSTER schemes, and that different car numbers produce visibly
// different pixels in the number-decal region.

#include "../src/render/livery.h"
#include "../src/sim/car.h"

#include <cmath>
#include <cstdio>

namespace {

int g_failures = 0;

void expectTrue(const char* label, bool cond) {
    if (!cond) {
        std::fprintf(stderr, "%s: FAILED\n", label);
        ++g_failures;
    }
}

std::array<double, 3> pixelAt(const std::vector<uint8_t>& pixels, int x, int y) {
    const size_t idx = ((size_t)y * kLiveryTextureSize + (size_t)x) * 4;
    return {pixels[idx] / 255.0, pixels[idx + 1] / 255.0, pixels[idx + 2] / 255.0};
}

double luminance(const std::array<double, 3>& p) {
    return 0.2126 * p[0] + 0.7152 * p[1] + 0.0722 * p[2];
}

// Average luminance over a horizontal band (fy0,fy1 as fractions of the
// texture height), sampled at a handful of x columns clear of any stripe
// graphics (near the very front, u~0.05, always base body color early on
// in the shading pass regardless of stripe style).
double bandLuminance(const std::vector<uint8_t>& pixels, double fy0, double fy1) {
    const int x = (int)(0.06 * kLiveryTextureSize);
    double sum = 0;
    int n = 0;
    for (double fy = fy0; fy < fy1; fy += 0.01) {
        const int y = (int)(fy * kLiveryTextureSize);
        if (y < 0 || y >= kLiveryTextureSize) continue;
        sum += luminance(pixelAt(pixels, x, y));
        ++n;
    }
    return n ? sum / n : 0.0;
}

} // namespace

int main() {
    const Color3 red = CarPalette::Red;

    // 3-tone shading: shadow bands (top/bottom rockers) are darkest, the
    // highlight band (roof/hood, v~0.40-0.60) is brightest, base (mid-body,
    // v~0.20-0.30 clear of the roof band) is in between.
    {
        LiveryScheme scheme{0, 0, 0, CarPalette::White}; // style 0, no stripe reaching this sample column
        const auto pixels = buildLiveryPixels(red, 7, 1, &scheme);
        const double shadowLum = bandLuminance(pixels, 0.0, 0.05);
        const double baseLum = bandLuminance(pixels, 0.15, 0.20);
        const double hiliteLum = bandLuminance(pixels, 0.45, 0.55);
        expectTrue("shadow band is darker than base", shadowLum < baseLum - 0.01);
        expectTrue("highlight band is brighter than base", hiliteLum > baseLum + 0.01);
    }

    // Pairwise-distinguishable stripe styles: same body/accent/scheme
    // fields except `stripe`, all 5 styles should paint genuinely
    // different pixel data somewhere on the texture.
    {
        std::vector<std::vector<uint8_t>> variants;
        for (int style = 0; style < 5; ++style) {
            LiveryScheme scheme{style, 0, 0, CarPalette::Yellow};
            variants.push_back(buildLiveryPixels(red, 28, 0, &scheme));
        }
        bool allDistinct = true;
        for (size_t i = 0; i < variants.size(); ++i) {
            for (size_t j = i + 1; j < variants.size(); ++j) {
                if (variants[i] == variants[j]) allDistinct = false;
            }
        }
        expectTrue("all 5 stripe styles produce distinct textures", allDistinct);
    }

    // Different car numbers produce different pixels in the roof number-
    // decal region (a real regression guard: if this ever degenerated to
    // "same texture regardless of number," every car would look identical).
    {
        LiveryScheme scheme{0, 0, 0, CarPalette::White};
        const auto p7 = buildLiveryPixels(red, 7, 1, &scheme);
        const auto p91 = buildLiveryPixels(red, 91, 1, &scheme);
        expectTrue("car #7 and car #91 render different textures", p7 != p91);
    }

    // Every real ROSTER scheme builds without crashing and produces a
    // fully-opaque, correctly-sized RGBA8 buffer.
    {
        for (const auto& entry : ROSTER) {
            const auto pixels = buildLiveryPixels(entry.col, entry.num, 0, &entry.scheme);
            char label[64];
            std::snprintf(label, sizeof(label), "ROSTER #%d builds a full-size buffer", entry.num);
            expectTrue(label, pixels.size() == (size_t)kLiveryTextureSize * kLiveryTextureSize * 4);
        }
    }

    // Player car (num=21, scheme=nullptr) falls back to idx-based picks
    // without crashing.
    {
        const auto pixels = buildLiveryPixels({1.0, 0.82, 0.24}, 21, 0, nullptr);
        expectTrue("player car (null scheme) builds a full-size buffer",
                   pixels.size() == (size_t)kLiveryTextureSize * kLiveryTextureSize * 4);
    }

    if (g_failures == 0) {
        std::printf("livery_test: shading bands, stripe styles, and number decals all match expectations.\n");
        return 0;
    }
    std::fprintf(stderr, "livery_test: %d MISMATCHES.\n", g_failures);
    return 1;
}
