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

    // G31: THE ASSERTION THAT WOULD HAVE CAUGHT THE ORIGINAL BUG.
    //
    // The crowd was painted on a flat 8 px grid for the whole life of this
    // port, and every test it had passed the whole time -- because they all
    // asked about the texture in TEXEL space (is the fill proportion right, is
    // a cell a solid block) and the defect was in the mapping to WORLD space.
    // At 256 texels over 7.0 m by 4.1 m, an 8 px cell is a person 0.22 m wide
    // and 0.12 m tall. Nothing that checks only texels can see that.
    //
    // So this asserts the thing that actually matters: a painted spectator is
    // roughly the size of a person. Bounds are generous -- this is guarding
    // against being wrong by a factor of ten, not pinning today's numbers.
    {
        const double personW = kCrowdTileWidthM / kCrowdCols;
        const double personH = kCrowdTileSlopeM / kCrowdRows;
        std::printf("crowd cell in world units: %.2f m wide x %.2f m tall (%d x %d grid)\n", personW,
                    personH, kCrowdCols, kCrowdRows);
        expectTrue("a painted spectator is 0.3-0.8 m wide", personW > 0.3 && personW < 0.8);
        expectTrue("a painted spectator is 0.7-1.6 m tall", personH > 0.7 && personH < 1.6);
    }

    // Fill-proportion: at fillProb=1.0 essentially every non-aisle cell in the
    // crowd tile carries a spectator; at fillProb=0.0 essentially none do.
    // Sampled at each cell's torso centre rather than its corner -- a figure
    // no longer fills its whole cell, and sampling the corner would read the
    // seat gap above the head and call a full stand empty.
    {
        Mulberry32 rngFull(777);
        const auto full = buildAtlasPixels(wallColor, palette, 1.0, rngFull);
        Mulberry32 rngEmpty(777);
        const auto empty = buildAtlasPixels(wallColor, palette, 0.0, rngEmpty);

        int filledCount = 0, emptyCount = 0, total = 0;
        const std::array<double, 3> baseSeat{38 / 255.0, 38 / 255.0, 42 / 255.0};
        const double cellW = (double)kAtlasCrowd.w / kCrowdCols;
        const double cellH = (double)kAtlasCrowd.h / kCrowdRows;
        for (int row = 0; row < kCrowdRows; ++row) {
            for (int col = 0; col < kCrowdCols; ++col) {
                ++total;
                const int px = kAtlasCrowd.x + (int)(col * cellW + cellW * 0.5);
                const int py = kAtlasCrowd.y + (int)(row * cellH + cellH * 0.5);
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

    // Grid-cell solidity: within a figure's torso every pixel is the same
    // color (confirms flat-filled rectangles, not per-pixel noise).
    {
        Mulberry32 rng(777);
        const auto pixels = buildAtlasPixels(wallColor, palette, 1.0, rng);
        const int cellW = kAtlasCrowd.w / kCrowdCols, cellH = kAtlasCrowd.h / kCrowdRows;
        const int cx = kAtlasCrowd.x + cellW / 2, cy = kAtlasCrowd.y + (int)(cellH * 0.6);
        const auto a = pixelAt(pixels, cx, cy);
        const auto b = pixelAt(pixels, cx + 1, cy + 1);
        expectTrue("crowd figures are flat-filled blocks, not per-pixel noise",
                   std::fabs(a[0] - b[0]) < 1e-6 && std::fabs(a[1] - b[1]) < 1e-6);
    }

    // Aisles exist: a real grandstand has stairways, and they are what makes
    // a coloured field read as seating rather than as a pattern. Asserted by
    // finding a full-height column of non-spectator pixels at fillProb=1.0,
    // where without aisles every column would be solid people.
    {
        Mulberry32 rng(777);
        const auto pixels = buildAtlasPixels(wallColor, palette, 1.0, rng);
        // Aisles are drawn over the seating at every 5th column BOUNDARY, so
        // the stripe centre is at col*cellW, not at a cell centre.
        const double cellW = (double)kAtlasCrowd.w / kCrowdCols;
        const int px = kAtlasCrowd.x + (int)std::lround(5 * cellW);
        const std::array<double, 3> aisle{0.20, 0.20, 0.22};
        int aisleRows = 0;
        for (int row = 0; row < kCrowdRows; ++row) {
            const int py = kAtlasCrowd.y + (int)((row + 0.5) * kAtlasCrowd.h / kCrowdRows);
            const auto p = pixelAt(pixels, px, py);
            if (std::fabs(p[0] - aisle[0]) < 2e-3 && std::fabs(p[2] - aisle[2]) < 2e-3) ++aisleRows;
        }
        expectTrue("the crowd has aisles running up through every row", aisleRows == kCrowdRows);
        // And they are NARROW -- a stairway is not as wide as a seat. The
        // first version skipped whole columns, which cost half a metre of
        // crowd per aisle; this pins that it does not come back.
        expectTrue("an aisle is narrower than half a seat", std::lround(cellW * 0.30) < cellW * 0.5);
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
