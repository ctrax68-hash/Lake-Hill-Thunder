#include "atlas_texture.h"

#include <algorithm>
#include <cmath>

namespace {

std::array<double, 3> mixC(const std::array<double, 3>& a, const std::array<double, 3>& b, double f) {
    return {a[0] + (b[0] - a[0]) * f, a[1] + (b[1] - a[1]) * f, a[2] + (b[2] - a[2]) * f};
}

class Canvas {
public:
    explicit Canvas(int size) : size_(size), pixels_((size_t)size * size * 4, 0) {}

    void fillRect(int x, int y, int w, int h, const std::array<double, 3>& color) {
        const int x0 = std::max(0, x), y0 = std::max(0, y);
        const int x1 = std::min(size_, x + w), y1 = std::min(size_, y + h);
        const uint8_t r = (uint8_t)std::lround(std::clamp(color[0], 0.0, 1.0) * 255.0);
        const uint8_t g = (uint8_t)std::lround(std::clamp(color[1], 0.0, 1.0) * 255.0);
        const uint8_t b = (uint8_t)std::lround(std::clamp(color[2], 0.0, 1.0) * 255.0);
        for (int py = y0; py < y1; ++py) {
            for (int px = x0; px < x1; ++px) {
                const size_t idx = ((size_t)py * size_ + (size_t)px) * 4;
                pixels_[idx] = r;
                pixels_[idx + 1] = g;
                pixels_[idx + 2] = b;
                pixels_[idx + 3] = 255;
            }
        }
    }

    std::vector<uint8_t> take() { return std::move(pixels_); }

private:
    int size_;
    std::vector<uint8_t> pixels_;
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
void paintFenceBand(Canvas& c, const AtlasRegion& r) {
    const std::array<double, 3> base{118 / 255.0, 120 / 255.0, 124 / 255.0};
    const std::array<double, 3> line{205 / 255.0, 207 / 255.0, 210 / 255.0};
    c.fillRect(r.x, r.y, r.w, r.h, base);
    const double cell = r.h * 0.34;
    const double halfW = std::max(1.0, r.h * 0.05) / 2.0;
    for (int py = r.y; py < r.y + r.h; ++py) {
        for (int px = r.x; px < r.x + r.w; ++px) {
            const double lx = px - r.x, ly = py - r.y;
            const double d1 = std::fmod(ly - lx, cell), d1n = d1 < 0 ? d1 + cell : d1;
            const double d2 = std::fmod(ly + lx, cell), d2n = d2 < 0 ? d2 + cell : d2;
            const bool onLine1 = d1n < halfW || d1n > cell - halfW;
            const bool onLine2 = d2n < halfW || d2n > cell - halfW;
            if (onLine1 || onLine2) c.fillRect(px, py, 1, 1, line);
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
    for (int py = r.y; py < r.y + r.h; py += cell) {
        for (int px = r.x; px < r.x + r.w; px += cell) {
            if (rng.next() > fillProb) continue;
            const auto& col = palette[(size_t)(rng.next() * palette.size())];
            const double m = 0.7 + rng.next() * 0.4;
            c.fillRect(px, py, cell - 1, cell - 1, {col[0] * m, col[1] * m, col[2] * m});
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

// paintSponsorTiles(): simplified -- flat alternating light/dark panels
// with a border, no sponsor-name text (this header's own simplification
// note #3 explains why: JS's drawWord() is a full bitmap-font renderer,
// out of scope here).
void paintSponsorTiles(Canvas& c, const AtlasRegion& r, int count) {
    const double tw = (double)r.w / count;
    for (int i = 0; i < count; ++i) {
        const int tx = r.x + (int)std::lround(i * tw);
        const bool light = (i % 2 == 0);
        const std::array<double, 3> fill = light ? std::array<double, 3>{232 / 255.0, 232 / 255.0, 236 / 255.0}
                                                  : std::array<double, 3>{18 / 255.0, 18 / 255.0, 22 / 255.0};
        const std::array<double, 3> border{10 / 255.0, 10 / 255.0, 12 / 255.0};
        c.fillRect(tx, r.y, (int)std::lround(tw), r.h, fill);
        const int bw = 2;
        c.fillRect(tx, r.y, (int)std::lround(tw), bw, border);
        c.fillRect(tx, r.y + r.h - bw, (int)std::lround(tw), bw, border);
        c.fillRect(tx, r.y, bw, r.h, border);
        c.fillRect(tx + (int)std::lround(tw) - bw, r.y, bw, r.h, border);
    }
}

} // namespace

std::array<double, 4> atlasUV(const AtlasRegion& r) {
    constexpr double kInset = 4.0;
    return {(r.x + kInset) / kAtlasSize, (r.y + kInset) / kAtlasSize, (r.x + r.w - kInset) / kAtlasSize,
            (r.y + r.h - kInset) / kAtlasSize};
}

std::array<double, 4> atlasSponsorUV(int i) {
    const double tw = (double)kAtlasSponsor.w / kAtlasSponsorCount;
    const double x0 = kAtlasSponsor.x + i * tw;
    constexpr double kInset = 2.0;
    return {(x0 + kInset) / kAtlasSize, (kAtlasSponsor.y + kInset) / kAtlasSize, (x0 + tw - kInset) / kAtlasSize,
            (kAtlasSponsor.y + kAtlasSponsor.h - kInset) / kAtlasSize};
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
