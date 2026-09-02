#include "atlas_texture.h"

#include "word_blit.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <string>

namespace {

std::array<double, 3> mixC(const std::array<double, 3>& a, const std::array<double, 3>& b, double f) {
    return {a[0] + (b[0] - a[0]) * f, a[1] + (b[1] - a[1]) * f, a[2] + (b[2] - a[2]) * f};
}

class Canvas {
public:
    explicit Canvas(int size) : size_(size), pixels_((size_t)size * size * 4, 0) {}

    // G28 added `alpha`. It defaults to opaque, so every existing caller is
    // unchanged; the catch fence is the one region that needs transparency,
    // because a fence you cannot see through is a wall.
    void fillRect(int x, int y, int w, int h, const std::array<double, 3>& color, double alpha = 1.0) {
        const int x0 = std::max(0, x), y0 = std::max(0, y);
        const int x1 = std::min(size_, x + w), y1 = std::min(size_, y + h);
        const uint8_t r = (uint8_t)std::lround(std::clamp(color[0], 0.0, 1.0) * 255.0);
        const uint8_t g = (uint8_t)std::lround(std::clamp(color[1], 0.0, 1.0) * 255.0);
        const uint8_t b = (uint8_t)std::lround(std::clamp(color[2], 0.0, 1.0) * 255.0);
        const uint8_t a = (uint8_t)std::lround(std::clamp(alpha, 0.0, 1.0) * 255.0);
        for (int py = y0; py < y1; ++py) {
            for (int px = x0; px < x1; ++px) {
                const size_t idx = ((size_t)py * size_ + (size_t)px) * 4;
                pixels_[idx] = r;
                pixels_[idx + 1] = g;
                pixels_[idx + 2] = b;
                pixels_[idx + 3] = a;
            }
        }
    }

    std::vector<uint8_t> take() { return std::move(pixels_); }

    // G28: raw access so word_blit.cpp can rasterise glyph coverage straight
    // into these bytes. Canvas otherwise deals only in flat rects, and adding
    // a text primitive to it would drag the font into every caller of this
    // file; handing out the buffer keeps the glyph code in one place.
    wordblit::Target target() { return {pixels_.data(), size_}; }

private:
    int size_;
    std::vector<uint8_t> pixels_;
};

// G28: the trackside brand names.
//
// ALL INVENTED. This project's standing rule is that no real sponsor, series
// or manufacturer name appears anywhere in the game. The reference's boards
// are a dense ribbon of wordmarks, and what they contribute is that the world
// does not read as empty -- invented names do that job completely, which is
// why the rule costs nothing here. "LAKE HILL" is the track's own name, the
// one wordmark that is not a notional sponsor.
constexpr std::array<const char*, kAtlasSponsorCount> kSponsorNames = {
    "APEX FUEL", "IRONCLAD", "VOLTARC",  "NORTHGATE",
    "REDSHIFT",  "SUMMIT",   "HAMMERHEAD", "LAKE HILL",
};

// paintWallPattern() (index.html:2927-2940): a 45-degree diamond
// checkerboard. Computed analytically per-pixel (this header's own
// simplification note #2) rather than simulating canvas
// translate+rotate+fillRect: rotate each pixel's tile-local offset by
// -45 degrees, then checkerboard the resulting axis-aligned cell indices.
void paintWallPattern(Canvas& c, const AtlasRegion& r, const std::array<double, 3>& wallColor) {
    c.fillRect(r.x, r.y, r.w, r.h, wallColor);
    const auto dark = mixC(wallColor, {0, 0, 0}, 0.35);
    const double cell = r.h * 0.28;
    const double cx = r.x + r.w / 2.0, cy = r.y + r.h / 2.0;
    const double ca = std::cos(-M_PI / 4), sa = std::sin(-M_PI / 4);
    for (int py = r.y; py < r.y + r.h; ++py) {
        for (int px = r.x; px < r.x + r.w; ++px) {
            const double lx = px - cx, ly = py - cy;
            const double rx = lx * ca - ly * sa, ry = lx * sa + ly * ca;
            const long cxCell = (long)std::floor(rx / cell), cyCell = (long)std::floor(ry / cell);
            if (((cxCell + cyCell) % 2 + 2) % 2 == 0) c.fillRect(px, py, 1, 1, dark);
        }
    }
}

// paintFenceBand() (index.html:2941-2952): a crosshatch of two diagonal
// stripe families. Computed analytically (note #2) as two mod-cell
// distance checks instead of stroking literal diagonal line segments.
// G28: the fence is TRANSPARENT between its wires now.
//
// It was painted as an opaque grey field with lighter crosshatch lines on
// top, which was fine while the fence was a 1 m strip along the top of the
// wall -- at that size it reads as a band of texture and occludes nothing.
// It stops being fine the moment the fence is its real height: a 5-6 m opaque
// panel running the whole lap paints a grey sheet straight over the crowd,
// which is worse than having no fence at all. The reference's fence is
// something you see the stands *through*, and that is most of what makes it
// read as a fence rather than as a wall.
//
// So: the gaps get alpha 0 and only the wires are opaque. The mesh is drawn
// in its own alpha-blended pass (renderer.cpp) rather than with the opaque
// world.
void paintFenceBand(Canvas& c, const AtlasRegion& r) {
    const std::array<double, 3> line{205 / 255.0, 207 / 255.0, 210 / 255.0};
    c.fillRect(r.x, r.y, r.w, r.h, line, 0.0);
    // G28: a much finer mesh. The cell was r.h*0.34 -- three diamonds over
    // the whole band -- which was invisible while the fence was a 1 m strip
    // and became absurd the moment it was its real height: diamonds nearly
    // two metres across, reading as a chain-link pattern painted on a wall
    // rather than as wire. At r.h*0.06 the band carries ~16 diamonds, which
    // over a 5.5 m fence is roughly a 34 cm cell -- coarser than real wire,
    // deliberately, because a true 5 cm mesh aliases into grey mush at any
    // distance and costs the fence its shape.
    const double cell = r.h * 0.06;
    const double halfW = std::max(1.0, cell * 0.16);
    for (int py = r.y; py < r.y + r.h; ++py) {
        for (int px = r.x; px < r.x + r.w; ++px) {
            const double lx = px - r.x, ly = py - r.y;
            const double d1 = std::fmod(ly - lx, cell), d1n = d1 < 0 ? d1 + cell : d1;
            const double d2 = std::fmod(ly + lx, cell), d2n = d2 < 0 ? d2 + cell : d2;
            const bool onLine1 = d1n < halfW || d1n > cell - halfW;
            const bool onLine2 = d2n < halfW || d2n > cell - halfW;
            if (onLine1 || onLine2) c.fillRect(px, py, 1, 1, line, 1.0);
        }
    }
}

// paintCrowdTile() (index.html:2953-2965): exact port -- an 8px grid,
// empty-seat base color, per-cell random fill (gated by crowdFill) from
// the track's own palette, with a brightness jitter multiplier.
void paintCrowdTile(Canvas& c, const AtlasRegion& r, const std::array<std::array<double, 3>, 6>& palette,
                     double fillProb, Mulberry32& rng) {
    c.fillRect(r.x, r.y, r.w, r.h, {38 / 255.0, 38 / 255.0, 42 / 255.0});
    constexpr int cell = 8;
    // G5b (NASCAR-Thunder gap-analysis plan, crowd variety): a small
    // neutral-toned "head" rect on top of each filled cell's own jersey-
    // colored torso fill -- still 2 fillRect() calls per cell (no
    // resolution/perf change), just enough to break up the single-flat-
    // color-per-cell look without a texture-budget increase.
    const std::array<double, 3> skin{0.55, 0.45, 0.38};
    for (int py = r.y; py < r.y + r.h; py += cell) {
        for (int px = r.x; px < r.x + r.w; px += cell) {
            if (rng.next() > fillProb) continue;
            const auto& col = palette[(size_t)(rng.next() * palette.size())];
            const double m = 0.7 + rng.next() * 0.4;
            c.fillRect(px, py, cell - 1, cell - 1, {col[0] * m, col[1] * m, col[2] * m});
            const double headM = 0.85 + rng.next() * 0.3;
            c.fillRect(px + 2, py, cell - 4, 3, {skin[0] * headM, skin[1] * headM, skin[2] * headM});
        }
    }
}

// paintCrewTile() (index.html:2966-2983): exact port -- 3 simplified
// crew-figure silhouettes (torso/legs/helmet/visor blocks) on a pit-
// concrete background. Coordinates scaled from JS's 96x64 tile to this
// port's kAtlasCrew region size.
void paintCrewTile(Canvas& c, const AtlasRegion& r) {
    c.fillRect(r.x, r.y, r.w, r.h, {133 / 255.0, 130 / 255.0, 120 / 255.0});
    const double sx = r.w / 96.0, sy = r.h / 64.0;
    const std::array<double, 3> suits[3] = {
        {46 / 255.0, 48 / 255.0, 58 / 255.0}, {120 / 255.0, 28 / 255.0, 26 / 255.0}, {40 / 255.0, 40 / 255.0, 44 / 255.0}};
    const double fw = r.w / 3.0;
    for (int i = 0; i < 3; ++i) {
        const double cxm = r.x + i * fw + fw / 2;
        auto scaledRect = [&](double x0, double y0, double w, double h, const std::array<double, 3>& color) {
            c.fillRect((int)std::lround(cxm + x0 * sx), (int)std::lround(r.y + y0 * sy), (int)std::lround(w * sx),
                       (int)std::lround(h * sy), color);
        };
        scaledRect(-6, 18, 12, 26, suits[i]);
        scaledRect(-6, 44, 5, 18, suits[i]);
        scaledRect(1, 44, 5, 18, suits[i]);
        scaledRect(-4, 6, 8, 10, {230 / 255.0, 230 / 255.0, 235 / 255.0});
        scaledRect(-4, 9, 8, 4, {20 / 255.0, 22 / 255.0, 28 / 255.0});
    }
}

// paintSponsorTiles(): eight boards, each carrying an invented wordmark.
//
// G28 changed the tile SHAPE, which is what made the text possible. The tiles
// used to be sliced vertically -- eight columns of 32x256 -- and a sponsor
// panel is 8 m long by 0.8 m tall, with u along its length. That maps 32 atlas
// pixels across 8 m and 256 down 0.8 m: an ~80:1 anisotropic stretch, on which
// any lettering smears into stripes. Sliced horizontally instead, each tile is
// 256x32 and lands on the board at very nearly square pixels.
//
// (The old vertical slicing was never wrong for what it drew -- flat
// alternating light/dark panels look the same either way -- which is exactly
// why the shape problem was invisible until something with structure was
// painted into it.)
void paintSponsorTiles(Canvas& c, const AtlasRegion& r, int count) {
    const double th = (double)r.h / count;
    for (int i = 0; i < count; ++i) {
        const int ty = r.y + (int)std::lround(i * th);
        const int h = (int)std::lround(th);
        const bool light = (i % 2 == 0);
        const std::array<double, 3> fill = light ? std::array<double, 3>{232 / 255.0, 232 / 255.0, 236 / 255.0}
                                                  : std::array<double, 3>{18 / 255.0, 18 / 255.0, 22 / 255.0};
        const std::array<double, 3> border{10 / 255.0, 10 / 255.0, 12 / 255.0};
        c.fillRect(r.x, ty, r.w, h, fill);
        const int bw = 2;
        c.fillRect(r.x, ty, r.w, bw, border);
        c.fillRect(r.x, ty + h - bw, r.w, bw, border);
        c.fillRect(r.x, ty, bw, h, border);
        c.fillRect(r.x + r.w - bw, ty, bw, h, border);

        // Ink contrasts with the board it sits on, so both halves of the
        // alternation stay readable at distance.
        const std::array<double, 3> ink = light ? std::array<double, 3>{16 / 255.0, 18 / 255.0, 24 / 255.0}
                                                : std::array<double, 3>{236 / 255.0, 236 / 255.0, 240 / 255.0};
        wordblit::drawWordCentered(c.target(), r.x + bw, ty + bw, r.w - bw * 2, h - bw * 2,
                                   kSponsorNames[i % (int)kSponsorNames.size()], th * 0.68, ink);
    }
}

} // namespace

std::array<double, 4> atlasUV(const AtlasRegion& r) {
    constexpr double kInset = 4.0;
    return {(r.x + kInset) / kAtlasSize, (r.y + kInset) / kAtlasSize, (r.x + r.w - kInset) / kAtlasSize,
            (r.y + r.h - kInset) / kAtlasSize};
}

std::array<double, 4> atlasSponsorUV(int i) {
    // G28: tiles are horizontal bands now, not vertical columns -- see
    // paintSponsorTiles() for why the old shape made lettering impossible.
    const double th = (double)kAtlasSponsor.h / kAtlasSponsorCount;
    const double y0 = kAtlasSponsor.y + i * th;
    constexpr double kInset = 2.0;
    return {(kAtlasSponsor.x + kInset) / kAtlasSize, (y0 + kInset) / kAtlasSize,
            (kAtlasSponsor.x + kAtlasSponsor.w - kInset) / kAtlasSize, (y0 + th - kInset) / kAtlasSize};
}

std::vector<uint8_t> buildAtlasPixels(const std::array<double, 3>& wallColor,
                                       const std::array<std::array<double, 3>, 6>& palette, double crowdFill,
                                       Mulberry32& rng) {
    Canvas c(kAtlasSize);
    c.fillRect(0, 0, kAtlasSize, kAtlasSize, {60 / 255.0, 60 / 255.0, 64 / 255.0});
    paintWallPattern(c, kAtlasWall, wallColor);
    paintFenceBand(c, kAtlasFence);
    paintCrowdTile(c, kAtlasCrowd, palette, crowdFill, rng);
    paintCrewTile(c, kAtlasCrew);
    paintSponsorTiles(c, kAtlasSponsor, kAtlasSponsorCount);
    return c.take();
}
