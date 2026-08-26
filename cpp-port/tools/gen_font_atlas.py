#!/usr/bin/env python3
"""G26: bakes the UI font atlas into src/render/font_atlas_data.h.

WHY THIS EXISTS
---------------
Every piece of text in this game -- HUD, menu, results, pit setup, touch
button labels -- is drawn with bgfx's built-in debug-text overlay: a fixed
8x16 monospace cell grid with a 16-colour VGA attribute byte. It cannot be
scaled, positioned at sub-cell offsets, or given a per-pixel colour. The
seven-segment digit rasteriser in ui_draw.h exists *only* as a workaround for
that (and handles just `0-9 : . - /`), and sponsor wordmarks, pit-stall signs
and pylon text have all been deferred repeatedly for the same missing piece.

The reference footage this is being matched against uses a proportional bold
sans with drop shadows at several sizes. None of that is reachable from
dbgText, so the font atlas is the gate on the whole UI ask.

WHY OFFLINE, AND WHY PNG
------------------------
There is no font rasteriser in the shipping build (no freetype, no stb_truetype
wired up) and adding one to a WASM target to do work that never changes would be
silly. So this bakes once, offline, and commits the result -- the same
generated-asset pattern as tools/gen_car_rig.py -> car_rig_data.h.

The atlas is embedded as PNG bytes rather than raw pixels because the raw
alpha buffer is 256 KB, which as a C array would be well over a megabyte of
source. PNG-compressed it is a fraction of that, and the decoder is already
here: texture_import.h's decodeImage() (stb_image), used today for glTF
base-colour textures.

Run:  python3 tools/gen_font_atlas.py
"""

import os
import sys
import zlib
import struct

try:
    from PIL import Image, ImageDraw, ImageFont
except ImportError:
    sys.exit("Pillow required: pip install pillow")

# DejaVu Sans Bold: metrically generous, genuinely bold at small sizes (the
# ticker text in the reference is small and still reads), and permissively
# licensed (Bitstream Vera derivative). Ships with essentially every Linux
# image, so the bake is reproducible.
FONT_PATH = "/usr/share/fonts/truetype/dejavu/DejaVuSans-Bold.ttf"

# 48 px is chosen so the largest thing the HUD needs -- the reference's big
# position numeral, roughly 60 px tall on a phone -- is only modestly
# upscaled, while everything else scales DOWN through the mip chain, which is
# where bilinear filtering behaves well. Baking larger would cost atlas area
# for glyphs nothing ever draws at that size.
FONT_SIZE = 48
PAD = 2  # transparent gutter so bilinear sampling cannot bleed neighbours in

FIRST_CHAR = 32   # space
LAST_CHAR = 126   # ~

OUT_HEADER = os.path.join(os.path.dirname(__file__), "..", "src", "render", "font_atlas_data.h")


def main():
    if not os.path.exists(FONT_PATH):
        sys.exit(f"font not found: {FONT_PATH}")
    font = ImageFont.truetype(FONT_PATH, FONT_SIZE)
    ascent, descent = font.getmetrics()
    line_height = ascent + descent

    chars = [chr(c) for c in range(FIRST_CHAR, LAST_CHAR + 1)]

    # Measure every glyph first so the atlas can be packed by shelves rather
    # than a fixed grid -- a grid sized for 'W' wastes most of its area on 'i'.
    glyphs = []
    for ch in chars:
        bbox = font.getbbox(ch)  # (x0, y0, x1, y1) relative to the text origin
        adv = font.getlength(ch)
        if bbox is None:
            bbox = (0, 0, 0, 0)
        x0, y0, x1, y1 = bbox
        w, h = max(0, x1 - x0), max(0, y1 - y0)
        glyphs.append({"ch": ch, "w": w, "h": h, "bx": x0, "by": y0, "adv": adv})

    # Shelf-pack, tallest first, into a power-of-two width.
    ATLAS_W = 512
    order = sorted(range(len(glyphs)), key=lambda i: -glyphs[i]["h"])
    x, y, shelf_h = PAD, PAD, 0
    for i in order:
        g = glyphs[i]
        gw, gh = g["w"] + PAD, g["h"] + PAD
        if x + gw > ATLAS_W - PAD:
            x = PAD
            y += shelf_h
            shelf_h = 0
        g["x"], g["y"] = x, y
        x += gw
        shelf_h = max(shelf_h, gh)
    atlas_h = y + shelf_h + PAD
    ATLAS_H = 1
    while ATLAS_H < atlas_h:
        ATLAS_H *= 2

    # Render. Single channel: the glyph coverage is an alpha mask, and the UI
    # tints it per-vertex, so storing RGB would be three redundant copies.
    img = Image.new("L", (ATLAS_W, ATLAS_H), 0)
    draw = ImageDraw.Draw(img)
    for g in glyphs:
        if g["w"] > 0 and g["h"] > 0:
            # Draw at an offset that cancels the glyph's own bbox origin, so
            # the rendered ink lands exactly at (x, y) in the atlas.
            draw.text((g["x"] - g["bx"], g["y"] - g["by"]), g["ch"], font=font, fill=255)

    png = encode_png_gray(img)

    with open(os.path.normpath(OUT_HEADER), "w") as f:
        f.write(header_text(glyphs, ATLAS_W, ATLAS_H, ascent, descent, line_height, png))

    print(f"atlas {ATLAS_W}x{ATLAS_H}, {len(glyphs)} glyphs, "
          f"png {len(png)} bytes -> {os.path.normpath(OUT_HEADER)}")


def encode_png_gray(img):
    """Minimal 8-bit greyscale PNG. Pillow's own save() would work, but going
    through a BytesIO and re-reading is no simpler and this keeps the exact
    bytes that land in the header explicit and deterministic."""
    w, h = img.size
    raw = img.tobytes()
    scan = b"".join(b"\x00" + raw[r * w:(r + 1) * w] for r in range(h))

    def chunk(tag, data):
        c = struct.pack(">I", len(data)) + tag + data
        return c + struct.pack(">I", zlib.crc32(tag + data) & 0xFFFFFFFF)

    ihdr = struct.pack(">IIBBBBB", w, h, 8, 0, 0, 0, 0)  # 8-bit, colour type 0 = grey
    return (b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", ihdr)
            + chunk(b"IDAT", zlib.compress(scan, 9)) + chunk(b"IEND", b""))


def header_text(glyphs, aw, ah, ascent, descent, line_h, png):
    by_char = {g["ch"]: g for g in glyphs}
    rows = []
    for c in range(FIRST_CHAR, LAST_CHAR + 1):
        g = by_char[chr(c)]
        # yoff is measured from the text baseline (negative = above it), which
        # is what a renderer wants when it positions by baseline, not by cell.
        rows.append("    {{{:4d},{:4d},{:4d},{:4d},{:5d},{:5d},{:7.2f}}}, // {}".format(
            g["x"], g["y"], g["w"], g["h"], g["bx"], g["by"] - ascent, g["adv"],
            repr(g["ch"])))
    body = "\n".join(rows)

    # Wrap by BYTES, not by characters -- chunking the joined string at a
    # fixed column split tokens in half ("0x0" / "x00" across the newline),
    # which is a compile error, not a cosmetic one.
    PER_LINE = 16
    wrapped = []
    for i in range(0, len(png), PER_LINE):
        wrapped.append("    " + ",".join("0x%02x" % b for b in png[i:i + PER_LINE]) + ",")
    blob = "\n".join(wrapped)

    return f'''#pragma once

// GENERATED by tools/gen_font_atlas.py -- do not edit by hand.
// Source font: DejaVu Sans Bold, {FONT_SIZE}px. Regenerate after changing
// either the font or the size; see that script for why this is baked offline
// and embedded as PNG rather than raw pixels.

#include <cstdint>

namespace fontatlas {{

struct Glyph {{
    int16_t x, y, w, h;   // position/size in the atlas, pixels
    int16_t xoff;         // left bearing from the pen position
    int16_t yoff;         // top edge relative to the BASELINE (negative = above)
    float advance;        // pen advance, pixels at the baked size
}};

inline constexpr int kFirstChar = {FIRST_CHAR};
inline constexpr int kLastChar = {LAST_CHAR};
inline constexpr int kAtlasWidth = {aw};
inline constexpr int kAtlasHeight = {ah};
inline constexpr int kBakedPixelSize = {FONT_SIZE};
inline constexpr int kAscent = {ascent};
inline constexpr int kDescent = {descent};
inline constexpr int kLineHeight = {line_h};

// Indexed by (codepoint - kFirstChar).
inline constexpr Glyph kGlyphs[] = {{
{body}
}};
inline constexpr int kGlyphCount = (int)(sizeof(kGlyphs) / sizeof(kGlyphs[0]));

// 8-bit greyscale PNG of the atlas; decode with texture_import.h's
// decodeImageRGBA8(). Greyscale because a glyph is coverage only -- the UI
// tints it per-vertex, so RGB would be three identical copies of the mask.
inline constexpr uint8_t kAtlasPng[] = {{
{blob}
}};
inline constexpr int kAtlasPngSize = (int)(sizeof(kAtlasPng) / sizeof(kAtlasPng[0]));

}} // namespace fontatlas
'''


if __name__ == "__main__":
    main()
