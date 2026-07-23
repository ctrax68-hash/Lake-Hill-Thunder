#!/usr/bin/env python3
"""One-off generator for Phase 7b's PWA icon assets (PORT_PROGRESS.md).

Not part of the CMake build -- run manually whenever the icon design needs
to change, then commit the resulting PNGs directly (fixed branding art, not
build output). Requires Pillow.

Design: a checkered-flag motif in a yellow frame. Not an arbitrary choice --
this project's own JS side (index.html) already established a checkered
flag as its race-state icon language (its "unified state banner + flag icon
routine" UI work) and a black/white/yellow palette (--c-black:#000000,
--c-white:#ffffff, --c-yellow:#F7D400, see index.html's THEME/CSS custom
properties), so this reuses that existing visual identity rather than
inventing a new one. Sharp corners throughout (no rounding), matching this
project's own established "sharp corners" design-token convention.
"""

import os

from PIL import Image, ImageDraw

ICONS_DIR = os.path.join(os.path.dirname(os.path.abspath(__file__)), "icons")

BLACK = (0, 0, 0, 255)
WHITE = (255, 255, 255, 255)
YELLOW = (247, 212, 0, 255)  # #F7D400, index.html's --c-yellow


def draw_icon(size: int) -> Image.Image:
    img = Image.new("RGBA", (size, size), BLACK)
    draw = ImageDraw.Draw(img)

    # Yellow frame -- a flat border strip, no rounding, echoing the sharp-
    # cornered panel style used throughout this project's own UI work.
    frame = max(2, round(size * 0.035))
    draw.rectangle([0, 0, size - 1, size - 1], outline=YELLOW, width=frame)

    # Checkerboard field inset within the frame.
    inset = frame * 2
    board_x0, board_y0 = inset, inset
    board_x1, board_y1 = size - inset, size - inset
    board_size = board_x1 - board_x0
    cells = 6
    cell = board_size / cells
    for row in range(cells):
        for col in range(cells):
            if (row + col) % 2 == 0:
                x0 = board_x0 + col * cell
                y0 = board_y0 + row * cell
                draw.rectangle([x0, y0, x0 + cell, y0 + cell], fill=WHITE)
            # else: leave black (background already black)

    return img


def main():
    os.makedirs(ICONS_DIR, exist_ok=True)
    targets = [
        ("icon-192.png", 192),
        ("icon-512.png", 512),
        ("apple-touch-icon.png", 180),
    ]
    for filename, size in targets:
        img = draw_icon(size)
        out_path = os.path.join(ICONS_DIR, filename)
        img.save(out_path)
        print(f"wrote {out_path} ({size}x{size})")


if __name__ == "__main__":
    main()
