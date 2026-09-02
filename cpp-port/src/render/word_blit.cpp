#include "word_blit.h"

#include "font_atlas.h"
#include "font_atlas_data.h"

#include <algorithm>
#include <cmath>

namespace wordblit {
namespace {

// The decoded glyph atlas, decoded once. Track load calls this a few dozen
// times per board; re-decoding a 19 KB PNG each time would be silly, and the
// image is immutable for the life of the process.
const font::AtlasImage& atlas() {
    static const font::AtlasImage img = font::decodeAtlas();
    return img;
}

const fontatlas::Glyph* glyphFor(char ch) {
    const int code = (int)(unsigned char)ch;
    if (code < fontatlas::kFirstChar || code > fontatlas::kLastChar) return nullptr;
    return &fontatlas::kGlyphs[code - fontatlas::kFirstChar];
}

double scaleFor(double pixelSize) { return pixelSize / (double)fontatlas::kBakedPixelSize; }

// Bilinear coverage sample from the atlas, in atlas pixel space. Bilinear
// rather than nearest because these wordmarks are drawn at a fraction of the
// 48px bake size -- point-sampling a downscale drops whole stems of a letter,
// which at board sizes turns "APEX" into something that is not a word.
double sampleCoverage(const font::AtlasImage& img, double ax, double ay) {
    const double cx = std::clamp(ax, 0.0, (double)img.width - 1.0);
    const double cy = std::clamp(ay, 0.0, (double)img.height - 1.0);
    const int x0 = (int)cx, y0 = (int)cy;
    const int x1 = std::min(x0 + 1, img.width - 1), y1 = std::min(y0 + 1, img.height - 1);
    const double fx = cx - x0, fy = cy - y0;
    auto at = [&](int x, int y) {
        return (double)img.rgba8[((size_t)y * img.width + (size_t)x) * 4] / 255.0;
    };
    const double top = at(x0, y0) + (at(x1, y0) - at(x0, y0)) * fx;
    const double bot = at(x0, y1) + (at(x1, y1) - at(x0, y1)) * fx;
    return top + (bot - top) * fy;
}

void blendPixel(const Target& dst, int px, int py, const std::array<double, 3>& color, double a) {
    if (a <= 0.002 || px < 0 || py < 0 || px >= dst.size || py >= dst.size) return;
    const double k = std::clamp(a, 0.0, 1.0);
    const size_t idx = ((size_t)py * dst.size + (size_t)px) * 4;
    for (int ch = 0; ch < 3; ++ch) {
        const double src = std::clamp(color[ch], 0.0, 1.0) * 255.0;
        const double old = (double)dst.pixels[idx + ch];
        dst.pixels[idx + ch] = (uint8_t)std::lround(old + (src - old) * k);
    }
    dst.pixels[idx + 3] = 255;
}

} // namespace

double measureWord(const std::string& text, double pixelSize) {
    const double s = scaleFor(pixelSize);
    double pen = 0.0;
    for (char ch : text) {
        if (const fontatlas::Glyph* g = glyphFor(ch)) pen += (double)g->advance * s;
    }
    return pen;
}

bool drawWord(const Target& dst, double x, double y, const std::string& text, double pixelSize,
              const std::array<double, 3>& color) {
    const font::AtlasImage& img = atlas();
    if (!img.ok || dst.pixels == nullptr || dst.size <= 0) return false;

    const double s = scaleFor(pixelSize);
    double pen = x;

    for (char ch : text) {
        const fontatlas::Glyph* g = glyphFor(ch);
        if (!g) continue;
        if (g->w > 0 && g->h > 0) {
            // Destination rect for this glyph, then for each destination pixel
            // sample back into the atlas. Iterating the destination (rather
            // than the source) is what keeps a downscale from leaving holes:
            // every output pixel gets a value, instead of several source
            // pixels fighting over one output pixel and most being dropped.
            const double dx0 = pen + (double)g->xoff * s;
            const double dy0 = y + (double)g->yoff * s;
            const int px0 = (int)std::floor(dx0), py0 = (int)std::floor(dy0);
            const int px1 = (int)std::ceil(dx0 + (double)g->w * s);
            const int py1 = (int)std::ceil(dy0 + (double)g->h * s);

            for (int py = py0; py < py1; ++py) {
                for (int px = px0; px < px1; ++px) {
                    // +0.5 samples the pixel centre, not its corner.
                    const double u = ((double)px + 0.5 - dx0) / s;
                    const double v = ((double)py + 0.5 - dy0) / s;
                    if (u < 0.0 || v < 0.0 || u > (double)g->w || v > (double)g->h) continue;
                    blendPixel(dst, px, py, color,
                               sampleCoverage(img, (double)g->x + u, (double)g->y + v));
                }
            }
        }
        pen += (double)g->advance * s;
    }
    return true;
}

bool drawWordCentered(const Target& dst, int rx, int ry, int rw, int rh, const std::string& text,
                      double maxPixelSize, const std::array<double, 3>& color) {
    if (text.empty() || rw <= 0 || rh <= 0) return false;

    // Shrink to fit rather than clip. These names are authored data, and a
    // board reading "HAMMERHEA" is worse than one reading slightly small --
    // and it is the kind of thing that only shows up on the one board with
    // the longest name, on the one track nobody screenshotted.
    double size = maxPixelSize;
    const double pad = (double)rw * 0.08;
    const double avail = (double)rw - pad * 2.0;
    const double w = measureWord(text, size);
    if (w > avail && w > 0.0) size *= avail / w;

    const double finalW = measureWord(text, size);
    const double x = (double)rx + ((double)rw - finalW) * 0.5;
    // Centre on the cap height rather than the full line box: these are all
    // caps, so the descender space a line box reserves would sit the word
    // visibly high on its board.
    const double y = (double)ry + (double)rh * 0.5 + font::ascent((float)size) * 0.38;
    return drawWord(dst, x, y, text, size, color);
}

} // namespace wordblit
