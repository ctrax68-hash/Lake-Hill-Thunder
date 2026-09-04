#!/usr/bin/env python3
"""V1: render the showcase car from N angles into one contact sheet.

WHY THIS EXISTS. Four rounds of car work (H1, I-series, J-series, K-series)
shipped without anyone being able to look at what they changed. The Showcase
camera was a fixed front-3/4 from the car's LEFT, so the right flank, the tail
and the exhaust side were never visible in any of the three camera modes, and
the J2/J3/J5 entries in PORT_PROGRESS.md record verification degenerating into
decoding the compiled glTF blob and reading livery pixels instead. Every one of
those rounds was followed by the user saying the car still looked wrong.

This is the control those rounds lacked: one command, one image, the whole car.

Usage:
    python3 tools/car_turntable.py [--out FILE] [--angles N] [--build DIR]

Requires a built native binary (cmake --build build --target lht_port) and
xvfb-run, and Pillow for the sheet assembly.
"""

import argparse
import os
import shutil
import struct
import subprocess
import sys
import tempfile

REPO = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))


def capture(binary, angle, outstem, env_extra=None):
    """Render one frame at a given showcase azimuth. Returns (w, h, rgba)."""
    env = dict(os.environ)
    env["LHT_SHOWCASE_ANGLE"] = str(angle)
    env["LHT_SCREENSHOT"] = outstem
    if env_extra:
        env.update(env_extra)
    # No LHT_FORCE_RACE: the default entry point is the menu, which is where
    # the Showcase camera lives.
    subprocess.run(
        ["xvfb-run", "-a", "-s", "-screen 0 1280x720x24", binary],
        env=env, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL, timeout=300,
    )
    meta, raw = outstem + ".meta", outstem + ".rgba"
    if not os.path.exists(meta):
        raise RuntimeError("no capture produced at angle %s" % angle)
    w, h = (int(v) for v in open(meta).read().split()[:2])
    with open(raw, "rb") as f:
        return w, h, f.read()


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--out", default=os.path.join(REPO, "car_turntable.png"))
    ap.add_argument("--angles", type=int, default=8)
    ap.add_argument("--build", default=os.path.join(REPO, "build"))
    ap.add_argument("--crop", default="360,140,1000,560",
                    help="left,top,right,bottom crop around the car, in pixels")
    args = ap.parse_args()

    from PIL import Image, ImageOps

    binary = os.path.join(args.build, "lht_port")
    if not os.path.exists(binary):
        sys.exit("no binary at %s -- run: cmake --build build --target lht_port" % binary)

    box = tuple(int(v) for v in args.crop.split(","))
    tmp = tempfile.mkdtemp(prefix="turntable")
    tiles = []
    try:
        for i in range(args.angles):
            angle = 360.0 * i / args.angles
            w, h, raw = capture(binary, angle, os.path.join(tmp, "a%d" % i))
            # Captures are stored bottom-up; flip to normal orientation.
            im = ImageOps.flip(Image.frombytes("RGBA", (w, h), raw).convert("RGB"))
            tiles.append((angle, im.crop(box)))
            print("  captured %5.1f deg" % angle)
    finally:
        shutil.rmtree(tmp, ignore_errors=True)

    tw, th = tiles[0][1].size
    cols = 4
    rows = (len(tiles) + cols - 1) // cols
    pad = 6
    sheet = Image.new("RGB", (cols * tw + (cols + 1) * pad,
                              rows * th + (rows + 1) * pad), (18, 18, 20))
    for idx, (_, im) in enumerate(tiles):
        x = pad + (idx % cols) * (tw + pad)
        y = pad + (idx // cols) * (th + pad)
        sheet.paste(im, (x, y))
    sheet.save(args.out)
    print("wrote %s  (%d angles, %dx%d per tile)" % (args.out, len(tiles), tw, th))


if __name__ == "__main__":
    main()
