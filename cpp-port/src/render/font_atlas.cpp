#include "font_atlas.h"

#include "font_atlas_data.h"
#include "texture_import.h"

#include <algorithm>

namespace font {
namespace {

// Returns nullptr for anything outside the baked range, so unexpected bytes
// (a stray high-bit character from a data file, say) are skipped rather than
// indexing off the end of the table. The spotter strings were forced to ASCII
// for exactly this class of reason -- see race.cpp's L4 entry -- but a text
// renderer should not be the thing that crashes when that slips.
const fontatlas::Glyph* glyphFor(char c) {
    const int code = (int)(unsigned char)c;
    if (code < fontatlas::kFirstChar || code > fontatlas::kLastChar) return nullptr;
    return &fontatlas::kGlyphs[code - fontatlas::kFirstChar];
}

float scaleFor(float pixelSize) {
    return pixelSize / (float)fontatlas::kBakedPixelSize;
}

} // namespace

int bakedPixelSize() { return fontatlas::kBakedPixelSize; }

float ascent(float pixelSize) { return (float)fontatlas::kAscent * scaleFor(pixelSize); }

float measure(const std::string& text, float pixelSize) {
    const float s = scaleFor(pixelSize);
    float pen = 0.0f;
    for (char c : text) {
        if (const fontatlas::Glyph* g = glyphFor(c)) pen += g->advance * s;
    }
    return pen;
}

void pushText(std::vector<PosColorUvVertex>& out, float x, float y, const std::string& text,
              float pixelSize, uint32_t abgr) {
    const float s = scaleFor(pixelSize);
    const float invW = 1.0f / (float)fontatlas::kAtlasWidth;
    const float invH = 1.0f / (float)fontatlas::kAtlasHeight;
    float pen = x;

    for (char c : text) {
        const fontatlas::Glyph* g = glyphFor(c);
        if (!g) continue;
        if (g->w > 0 && g->h > 0) {
            const float x0 = pen + (float)g->xoff * s;
            const float y0 = y + (float)g->yoff * s; // yoff is relative to the baseline
            const float x1 = x0 + (float)g->w * s;
            const float y1 = y0 + (float)g->h * s;

            // Half-texel inset: the atlas packs glyphs with a 2px gutter, but
            // sampling exactly on the boundary can still pick up a neighbour
            // once the quad is scaled and the sampler filters.
            const float u0 = ((float)g->x + 0.5f) * invW;
            const float v0 = ((float)g->y + 0.5f) * invH;
            const float u1 = ((float)(g->x + g->w) - 0.5f) * invW;
            const float v1 = ((float)(g->y + g->h) - 0.5f) * invH;

            // Two triangles, same winding as ui_draw.cpp's pushQuad so the
            // whole UI buffer can be drawn with one state.
            out.push_back({x0, y0, 0.0f, abgr, u0, v0});
            out.push_back({x1, y0, 0.0f, abgr, u1, v0});
            out.push_back({x1, y1, 0.0f, abgr, u1, v1});
            out.push_back({x0, y0, 0.0f, abgr, u0, v0});
            out.push_back({x1, y1, 0.0f, abgr, u1, v1});
            out.push_back({x0, y1, 0.0f, abgr, u0, v1});
        }
        pen += g->advance * s;
    }
}

void pushTextShadowed(std::vector<PosColorUvVertex>& out, float x, float y, const std::string& text,
                      float pixelSize, uint32_t abgr, uint32_t shadowAbgr, float shadowOffset) {
    pushText(out, x + shadowOffset, y + shadowOffset, text, pixelSize, shadowAbgr);
    pushText(out, x, y, text, pixelSize, abgr);
}

AtlasImage decodeAtlas() {
    AtlasImage img;
    const DecodedImage dec = decodeImageRGBA8(fontatlas::kAtlasPng, (size_t)fontatlas::kAtlasPngSize);
    if (!dec.ok) return img;
    img.ok = true;
    img.width = dec.width;
    img.height = dec.height;
    img.rgba8 = dec.rgba8;
    return img;
}

} // namespace font
