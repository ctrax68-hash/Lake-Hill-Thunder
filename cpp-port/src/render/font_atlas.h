#pragma once

// G26 (graphics pass): the first real text renderer in this port.
//
// Everything drawn as text until now went through bgfx's built-in debug-text
// overlay -- a fixed 8x16 monospace cell grid with a 16-colour VGA attribute
// byte, no sub-cell positioning, no scaling, no per-pixel colour. That is why
// ui_draw.h has a seven-segment digit rasteriser at all (it exists purely so
// the HUD could show a large number, and it only knows `0-9 : . - /`), and why
// sponsor wordmarks, pit-stall signage and pylon text have each been deferred
// with the same note about needing a font first.
//
// SPLIT. This header is pure layout: glyph lookup, measurement, and emitting
// textured quads into a vertex buffer. It never touches bgfx, matching how
// atlas_texture.h / sky_texture.h / livery.h all produce pixels and let
// renderer.cpp own the GPU upload. The atlas image itself is baked offline by
// tools/gen_font_atlas.py into font_atlas_data.h.

#include "vertex.h"

#include <cstdint>
#include <string>
#include <vector>

// Text geometry needs a UV per vertex, which PosColorVertex has no room for.
// Kept as its own type (and its own program) rather than widening
// PosColorVertex, because every existing UI primitive and all the world
// geometry share that layout -- adding two floats to it would grow every
// vertex in the game to buy something only text uses.
struct PosColorUvVertex {
    float x, y, z;
    uint32_t abgr;
    float u, v;
};

namespace font {

// Pixel height the atlas was baked at. Requesting this size draws 1:1.
int bakedPixelSize();

// Distance from baseline to the top of a line, scaled to `pixelSize`. Useful
// for positioning a block of text by its top edge rather than its baseline.
float ascent(float pixelSize);

// Advance width of `text` at `pixelSize`, in pixels. Does not include the
// left bearing of the first glyph or any trailing whitespace trimming --
// it is the pen distance, which is what centring and right-alignment want.
float measure(const std::string& text, float pixelSize);

// Emits `text` into `out` as one textured quad per visible glyph.
//
// (x, y) is the pen start: x is the left edge, y is the BASELINE. Callers
// that would rather think in terms of a top edge should pass
// `y + ascent(pixelSize)`.
//
// Glyphs with no ink (space) emit no geometry but still advance the pen.
void pushText(std::vector<PosColorUvVertex>& out, float x, float y, const std::string& text,
              float pixelSize, uint32_t abgr);

// Convenience: the same text drawn twice, once offset and darkened, once on
// top. The reference HUD drop-shadows everything it puts over the world, and
// without it light text vanishes against a pale sky or a white wall.
void pushTextShadowed(std::vector<PosColorUvVertex>& out, float x, float y, const std::string& text,
                      float pixelSize, uint32_t abgr, uint32_t shadowAbgr, float shadowOffset);

// The decoded atlas as RGBA8, for renderer.cpp to upload. The baked PNG is
// 8-bit greyscale coverage; this expands it to RGBA with the coverage in all
// four channels, so the sampler can be read as either luminance or alpha
// without a swizzle the GLES2/WebGL path may not support.
struct AtlasImage {
    bool ok = false;
    int width = 0, height = 0;
    std::vector<uint8_t> rgba8;
};
AtlasImage decodeAtlas();

} // namespace font
