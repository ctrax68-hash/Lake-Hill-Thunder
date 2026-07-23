// Verifies atlas_texture.{h,cpp}'s pure pixel math (bgfx-free): a
// statistical fill-proportion check on the crowd tile at a fixed seed, the
// 8px grid-cell boundary (a whole cell is one solid color, confirming the
// grid pitch), and that the fixed region layout doesn't overlap itself.

#include "../src/render/atlas_texture.h"

#include <cmath>
#include <cstdio>
#include <set>

namespace {

int g_failures = 0;

void expectTrue(const char* label, bool cond) {
    if (!cond) {
        std::fprintf(stderr, "%s: FAILED\n", label);
        ++g_failures;
    }
}

std::array<double, 3> pixelAt(const std::vector<uint8_t>& pixels, int x, int y) {
    const size_t idx = ((size_t)y * kAtlasSize + (size_t)x) * 4;
    return {pixels[idx] / 255.0, pixels[idx + 1] / 255.0, pixels[idx + 2] / 255.0};
}

bool regionsOverlap(const AtlasRegion& a, const AtlasRegion& b) {
    return a.x < b.x + b.w && b.x < a.x + a.w && a.y < b.y + b.h && b.y < a.y + a.h;
}

} // namespace

int main() {
    const std::array<std::array<double, 3>, 6> palette = {{{0.75, 0.2, 0.2},
                                                             {0.2, 0.35, 0.75},
                                                             {0.85, 0.8, 0.25},
                                                             {0.8, 0.8, 0.82},
                                                             {0.25, 0.6, 0.3},
                                                             {0.5, 0.3, 0.6}}};
    const std::array<double, 3> wallColor{1.0, 0.267, 0.0};

    // Fill-proportion: at fillProb=1.0, essentially every 8px cell in the
    // crowd tile should differ from the empty-seat base color (38,38,42);
    // at fillProb=0.0, essentially none should.
    {
        Mulberry32 rngFull(777);
        const auto full = buildAtlasPixels(wallColor, palette, 1.0, rngFull);
        Mulberry32 rngEmpty(777);
        const auto empty = buildAtlasPixels(wallColor, palette, 0.0, rngEmpty);

        int filledCount = 0, emptyCount = 0, total = 0;
        const std::array<double, 3> baseSeat{38 / 255.0, 38 / 255.0, 42 / 255.0};
        for (int py = kAtlasCrowd.y; py < kAtlasCrowd.y + kAtlasCrowd.h; py += 8) {
            for (int px = kAtlasCrowd.x; px < kAtlasCrowd.x + kAtlasCrowd.w; px += 8) {
                ++total;
                const auto pFull = pixelAt(full, px, py);
                const auto pEmpty = pixelAt(empty, px, py);
                if (std::fabs(pFull[0] - baseSeat[0]) > 1e-3 || std::fabs(pFull[1] - baseSeat[1]) > 1e-3)
                    ++filledCount;
                if (std::fabs(pEmpty[0] - baseSeat[0]) < 1e-3 && std::fabs(pEmpty[1] - baseSeat[1]) < 1e-3)
                    ++emptyCount;
            }
        }
        expectTrue("fillProb=1.0 fills nearly every cell", filledCount > total * 9 / 10);
        expectTrue("fillProb=0.0 leaves nearly every cell empty", emptyCount > total * 9 / 10);
    }

    // 8px grid-cell boundary: within one cell, every pixel is the same
    // solid color (confirms the fill grid's pitch, not an anti-aliased or
    // per-pixel-random pattern).
    {
        Mulberry32 rng(777);
        const auto pixels = buildAtlasPixels(wallColor, palette, 1.0, rng);
        const int cx = kAtlasCrowd.x + 16, cy = kAtlasCrowd.y + 16; // a cell's top-left corner
        const auto corner = pixelAt(pixels, cx, cy);
        const auto inside = pixelAt(pixels, cx + 3, cy + 3);
        expectTrue("crowd cell is a solid 8px block, not per-pixel noise",
                   std::fabs(corner[0] - inside[0]) < 1e-6 && std::fabs(corner[1] - inside[1]) < 1e-6);
    }

    // Region non-overlap: the fixed layout's 5 regions never overlap.
    {
        const AtlasRegion regions[] = {kAtlasWall, kAtlasFence, kAtlasCrowd, kAtlasSponsor, kAtlasCrew};
        bool anyOverlap = false;
        for (size_t i = 0; i < 5; ++i)
            for (size_t j = i + 1; j < 5; ++j)
                if (regionsOverlap(regions[i], regions[j])) anyOverlap = true;
        expectTrue("atlas regions don't overlap", !anyOverlap);
        for (const auto& r : regions) {
            expectTrue("region fits within the atlas bounds",
                       r.x >= 0 && r.y >= 0 && r.x + r.w <= kAtlasSize && r.y + r.h <= kAtlasSize);
        }
    }

    if (g_failures == 0) {
        std::printf("atlas_texture_test: fill statistics, grid pitch, and region layout all match expectations.\n");
        return 0;
    }
    std::fprintf(stderr, "atlas_texture_test: %d MISMATCHES.\n", g_failures);
    return 1;
}
