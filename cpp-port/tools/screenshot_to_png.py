#!/usr/bin/env python3
"""Convert an LHT_SCREENSHOT capture (.rgba + .meta) into a PNG.

    python3 tools/screenshot_to_png.py shot.png [more.png ...]

Pass the path you gave LHT_SCREENSHOT; this reads `<path>.rgba` and
`<path>.meta` beside it and writes `<path>`.

WHY THIS EXISTS, AND WHY IT READS THE FORMAT FIELD.

Every screenshot this session was converted by hand-rolled one-off Python that
assumed the capture was BGRA, because renderer.cpp's ScreenshotCallback comment
said "bgfx hands back raw BGRA8 pixels". It does not: the sidecar records the
real bgfx TextureFormat, and it is 71 = RGBA8. So R and B were swapped in every
image, and an entire round was spent diagnosing a "sky bug" -- all four tracks
author a blue zenith and all four appeared to render orange-brown, which is
exactly what a blue sky looks like with its red and blue channels exchanged.
There was never a bug. The instrument was wrong.

That is the fifth time in this project that the thing which looked broken was
the measuring apparatus rather than the subject, and the first four are written
up in PORT_PROGRESS.md. The fix is not "be more careful" -- it is to stop
re-deriving the conversion at each call site and read what the sidecar says.

The .meta is five whitespace-separated fields:

    width height pitch format yflip

`format` is a bgfx::TextureFormat::Enum value (70 = BGRA8, 71 = RGBA8);
`pitch` is in BYTES and may exceed width*4, so rows must be strided; `yflip`
means row 0 is the BOTTOM of the image.

A self-check is built in: --verify renders the channel order decision against a
known-colour pixel and reports it, so the assumption is testable rather than
assumed.
"""

import os
import sys

try:
    from PIL import Image
except ImportError:
    sys.exit("screenshot_to_png: Pillow is required (pip install pillow)")

# bgfx::TextureFormat::Enum -- only the two 32-bit orders a backbuffer capture
# can plausibly use. Anything else is a real surprise and should stop, not be
# guessed at.
_BGRA8 = 70
_RGBA8 = 71


def convert(path):
    meta_path = path + ".meta"
    raw_path = path + ".rgba"
    if not os.path.exists(meta_path) or not os.path.exists(raw_path):
        raise SystemExit("screenshot_to_png: expected %s and %s" % (meta_path, raw_path))

    fields = open(meta_path).read().split()
    if len(fields) != 5:
        raise SystemExit("screenshot_to_png: %s has %d fields, expected 5 "
                         "(width height pitch format yflip)" % (meta_path, len(fields)))
    width, height, pitch, fmt, yflip = (int(f) for f in fields)

    if fmt not in (_BGRA8, _RGBA8):
        raise SystemExit("screenshot_to_png: unhandled bgfx TextureFormat %d in %s -- "
                         "check bgfx.h rather than guessing a channel order" % (fmt, meta_path))

    raw = open(raw_path, "rb").read()
    need = pitch * height
    if len(raw) < need:
        raise SystemExit("screenshot_to_png: %s is %d bytes, need %d (pitch %d x height %d)"
                         % (raw_path, len(raw), need, pitch, height))

    # Read at the real pitch, then crop -- pitch is bytes per row and is not
    # required to equal width*4.
    img = Image.frombytes("RGBA", (pitch // 4, height), raw[:need]).crop((0, 0, width, height))
    if fmt == _BGRA8:
        b, g, r, a = img.split()
        img = Image.merge("RGB", (r, g, b))
    else:
        img = img.convert("RGB")
    if yflip:
        img = img.transpose(Image.FLIP_TOP_BOTTOM)
    img.save(path)
    return img, fmt


def main(argv):
    verify = "--verify" in argv
    paths = [a for a in argv[1:] if not a.startswith("--")]
    if not paths:
        raise SystemExit(__doc__)
    for path in paths:
        img, fmt = convert(path)
        name = {_BGRA8: "BGRA8", _RGBA8: "RGBA8"}[fmt]
        print("%s  %dx%d  %s" % (path, img.width, img.height, name))
        if verify:
            # The menu title is drawn kYellow (see ui/menu.cpp). Yellow is the
            # ideal probe: it is asymmetric in R and B, so a swap turns it cyan
            # and the error is unmissable rather than subtle.
            px = img.load()
            best = None
            for y in range(8, min(30, img.height)):
                for x in range(10, min(300, img.width)):
                    c = px[x, y]
                    if best is None or sum(c) > sum(best):
                        best = c
            if best:
                verdict = "yellow (channel order OK)" if best[0] > best[2] + 60 else \
                          "NOT yellow -- channel order is probably wrong"
                print("    brightest title pixel %s -> %s" % (best, verdict))


if __name__ == "__main__":
    main(sys.argv)
