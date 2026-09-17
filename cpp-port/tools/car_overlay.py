#!/usr/bin/env python3
"""Overlay the generated car's silhouette lines on a reference side view, and
print how far each station is from the traced reference -- in metres.

    python3 tools/car_overlay.py REF.jpg --rear X,Y --front X,Y [--out overlay.png]

WHY THIS EXISTS. The user's instruction, verbatim: "add more lines and keep
going until our cars match the exact outline". car_proportions.py measures a
handful of ratios; this measures the WHOLE profile, line by line, against the
photograph the car is being matched to, and it is the loss function for every
station-table edit from here on.

REGISTRATION. Two wheel centres in image pixels (refined here to the darkest
disc near each guess) fix scale, translation and the frame's tilt, so the
reference's own perspective is the only error left. The tire radius the photo
implies at that scale is printed rather than assumed: a car-select frame is not
an orthographic drawing and the number says how far from one it is.

TRACING. The reference silhouette is extracted per image column: the top line
is the first body-coloured pixel from the top, the bottom line the last one,
and the beltline the first body pixel under the first window run. "Body
coloured" is a per-reference predicate (the #41 is red and white on a grey-blue
backdrop); pass --trace to load a hand-corrected JSON instead.

LINES DRAWN, ours in solid colour over the photo:
    cyan     top silhouette (hood / windshield / roof / backlite / deck)
    green    bottom silhouette (rocker, air dam, valance)
    magenta  beltline (ring point K_BELT)
    orange   roof edge / drip rail (K_ROOF_EDGE)
    yellow   wheels, at WHEEL_RADIUS
and the traced reference in the same colours, dotted, so a mismatch is two
lines of one colour that do not coincide.
"""

import argparse
import json
import math
import os
import sys

import numpy as np
from PIL import Image, ImageDraw

_HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, _HERE)
os.chdir(_HERE)
import gen_car_rig as R  # noqa: E402


def refine_wheel(a, cx0, cy0, win=14):
    """Darkest disc near the guess; radius measured by walking out until the
    rubber ends, not assumed."""
    best = None
    for rad in (30, 34, 38, 42):
        ys, xs = np.ogrid[-rad:rad + 1, -rad:rad + 1]
        m = (xs * xs + ys * ys) <= rad * rad
        for cx in range(cx0 - win, cx0 + win + 1, 2):
            for cy in range(cy0 - win, cy0 + win + 1, 2):
                v = a[cy - rad:cy + rad + 1, cx - rad:cx + rad + 1].mean(axis=2)[m].mean()
                if best is None or v < best[0]:
                    best = (v, cx, cy)
    _v, cx, cy = best
    # tire radius: radial luminance profile over the UPPER half of the wheel
    # (the lower half sits in the car's own shadow, which is as dark as
    # rubber and made the first version walk out to the search cap), first
    # radius at which the mean leaves the rubber for good.
    lum = a.mean(axis=2)
    angs = [math.radians(t) for t in range(-160, -19, 10)]
    r = 18
    while r < 70:
        v = np.mean([lum[int(round(cy + math.sin(t) * r)), int(round(cx + math.cos(t) * r))] for t in angs])
        if v > 0.33:
            break
        r += 1
    return cx, cy, r


def trace(a, x0, x1, y0, y1, body):
    """Per-column top / bottom / belt of the reference body."""
    top, bot, belt = {}, {}, {}
    win = (a.max(axis=2) < 0.45) & ~body
    for x in range(x0, x1):
        col = body[y0:y1, x]
        ys = np.where(col)[0]
        if len(ys) == 0:
            continue
        top[x] = y0 + int(ys[0])
        bot[x] = y0 + int(ys[-1])
        # belt: first body pixel after the first window run below the top
        w = np.where(win[y0 + ys[0]:y1, x])[0]
        if len(w):
            after = y0 + ys[0] + w[0]
            cand = [y for y in (y0 + ys) if y > after + 4]
            if cand:
                belt[x] = int(cand[0])
    return top, bot, belt


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("ref")
    ap.add_argument("--rear", required=True)
    ap.add_argument("--front", required=True)
    ap.add_argument("--xrange", default="470,1010", help="image columns to trace")
    ap.add_argument("--yrange", default="45,240")
    ap.add_argument("--belt-x", default="560,720", help="columns where the belt is readable")
    ap.add_argument("--no-refine", action="store_true",
                    help="use the wheel centres exactly as given (the darkest-disc refine is "
                         "pulled downward by the car's own shadow on a lit car-select frame)")
    ap.add_argument("--tire-r", type=float, default=0.0, help="tire radius in px, if known")
    ap.add_argument("--trace", default=None,
                    help="JSON with hand-read image points {top:[[x,y],..], bot:[..], belt:[..]}; "
                         "replaces the colour-predicate trace, which is not trustworthy on a "
                         "car-select frame (dark lower body, bright backdrop streaks)")
    ap.add_argument("--raw-heights", action="store_true",
                    help="compare against the photo's heights as read. By default reference "
                         "heights are scaled by (rig tire radius / photo tire radius), so the "
                         "photo's wheel becomes ours and every other height follows: that is "
                         "the only consistent way to compare a body whose wheels must stay in "
                         "its arches.")
    ap.add_argument("--out", default=None)
    ap.add_argument("--json", default=None, help="write the trace + residuals here")
    args = ap.parse_args()

    im = Image.open(args.ref).convert("RGB")
    a = np.asarray(im, float) / 255.0
    r, g, b = a[..., 0], a[..., 1], a[..., 2]
    body = ((r > 0.45) & (r > g * 1.6) & (r > b * 1.5)) | (a.min(axis=2) > 0.70)

    rx0, ry0 = (int(v) for v in args.rear.split(","))
    fx0, fy0 = (int(v) for v in args.front.split(","))
    if args.no_refine:
        RX, RY, FX, FY = rx0, ry0, fx0, fy0
        RR = FR = args.tire_r if args.tire_r > 0 else refine_wheel(a, rx0, ry0)[2]
    else:
        RX, RY, RR = refine_wheel(a, rx0, ry0)
        FX, FY, FR = refine_wheel(a, fx0, fy0)
        if args.tire_r > 0:
            RR = FR = args.tire_r

    AF, AR = R._WHEEL_AXLE_X
    wb_px = math.hypot(FX - RX, FY - RY)
    scale = wb_px / (AF - AR)
    # Image basis: e_x runs rear -> front along the car (whichever way the
    # car faces in the frame), e_y is its perpendicular chosen to point UP in
    # the image. The first version rotated "up" by the same angle as the
    # forward vector, which is right for a car facing +x and turns the world
    # upside down for one facing -x (the #21 registered at 177.6 degrees).
    ex = ((FX - RX) / wb_px, (FY - RY) / wb_px)
    ey = (ex[1], -ex[0])
    if ey[1] > 0:
        ey = (-ey[0], -ey[1])
    tilt = math.degrees(math.atan2(-ey[0], -ey[1]))  # lean of "up" from vertical
    tire_m = 0.5 * (RR + FR) / scale
    print("registration: rear (%d,%d) r=%d  front (%d,%d) r=%d" % (RX, RY, RR, FX, FY, FR))
    print("registration: scale %.1f px/m, tilt %.1f deg, photo tire radius %.3f m (rig %.3f)"
          % (scale, tilt, tire_m, R.WHEEL_RADIUS))

    def proj(x, y):
        dx = (x - AR) * scale
        dy = (y - R.WHEEL_RADIUS) * scale
        return (RX + ex[0] * dx + ey[0] * dy, RY + ex[1] * dx + ey[1] * dy)

    hscale = 1.0 if args.raw_heights else (R.WHEEL_RADIUS / tire_m)
    print("heights: reference scaled by %.3f (tire-normalised)" % hscale)

    def unproj(px, py):
        vx, vy = px - RX, py - RY
        dx = vx * ex[0] + vy * ex[1]
        dy = vx * ey[0] + vy * ey[1]
        # Heights are measured from the ground (the hub sits one tire radius
        # up), then scaled so the photo's tire matches ours.
        return (AR + dx / scale, (R.WHEEL_RADIUS + dy / scale) * hscale)

    x0, x1 = (int(v) for v in args.xrange.split(","))
    y0, y1 = (int(v) for v in args.yrange.split(","))
    if args.trace:
        tr = json.load(open(args.trace))
        top = {int(x): int(y) for x, y in tr.get("top", [])}
        bot = {int(x): int(y) for x, y in tr.get("bot", [])}
        belt = {int(x): int(y) for x, y in tr.get("belt", [])}
    else:
        top, bot, belt = trace(a, x0, x1, y0, y1, body)
        bx0, bx1 = (int(v) for v in args.belt_x.split(","))
        belt = {x: y for x, y in belt.items() if bx0 <= x < bx1}

    # reference lines in car coordinates, as x -> y samples
    def to_car(d):
        pts = [unproj(x, y) for x, y in d.items()]
        return sorted(pts, key=lambda p: -p[0])

    ref_top, ref_bot, ref_belt = to_car(top), to_car(bot), to_car(belt)

    def ref_at(pts, x):
        if not pts or x > pts[0][0] or x < pts[-1][0]:
            return None
        for i in range(len(pts) - 1):
            if pts[i][0] >= x >= pts[i + 1][0]:
                t = (pts[i][0] - x) / max(pts[i][0] - pts[i + 1][0], 1e-9)
                return pts[i][1] + (pts[i + 1][1] - pts[i][1]) * t
        return None

    # our lines
    ours_top, ours_bot, ours_belt, ours_roof = [], [], [], []
    for i, st in enumerate(R.CHASSIS_STATIONS):
        ring = R.RINGS[i]
        ours_top.append((st[0], max(p[1] for p in ring)))
        ours_bot.append((st[0], min(p[1] for p in ring)))
        ours_belt.append((st[0], ring[R.K_BELT][1]))
        ours_roof.append((st[0], ring[R.K_ROOF_EDGE][1]))
    for rng in (R.NOSE_CAP_RANGE, R.TAIL_CAP_RANGE):
        bins = {}
        for i in range(*rng):
            p = R.positions[i]
            k = round(p[0], 3)
            e = bins.setdefault(k, [p[1], p[1]])
            e[0] = max(e[0], p[1])
            e[1] = min(e[1], p[1])
        for k, (hi, lo) in bins.items():
            ours_top.append((k, hi))
            ours_bot.append((k, lo))
    for L in (ours_top, ours_bot):
        L.sort(key=lambda t: -t[0])

    # residual table, per hand-authored station
    print()
    print("  %-12s %8s   %7s %7s %7s   %7s %7s %7s   %7s %7s %7s" % (
        "station", "x", "top", "ref", "d", "belt", "ref", "d", "bot", "ref", "d"))
    roles = {round(R.station_x(k), 4): k for k in R.STATION_ROLES}
    worst = []
    for i, st in enumerate(R.CHASSIS_STATIONS):
        role = roles.get(round(st[0], 4), "")
        # Look BOTH silhouette lines up by x. ours_bot is sorted and carries the
        # cap bins as well as the stations, so indexing it by the station index
        # -- which the first version did -- read a cap point at the nose and a
        # neighbouring station everywhere else. Every `bot` residual printed was
        # misaligned, and the nose's +0.314 was the cap dome, not the air dam.
        t = ours_top[[o[0] for o in ours_top].index(st[0])][1]
        bo = ours_bot[[o[0] for o in ours_bot].index(st[0])][1]
        bl = ours_belt[i][1]
        rt, rb, rbo = ref_at(ref_top, st[0]), ref_at(ref_belt, st[0]), ref_at(ref_bot, st[0])
        f = lambda v: "%7.3f" % v if v is not None else "      -"
        d = lambda o, rr: ("%+7.3f" % (o - rr)) if rr is not None else "      -"
        print("  %-12s %8.3f   %s %s %s   %s %s %s   %s %s %s" % (
            role, st[0], f(t), f(rt), d(t, rt), f(bl), f(rb), d(bl, rb), f(bo), f(rbo), d(bo, rbo)))
        for name, o, rr in (("top", t, rt), ("belt", bl, rb), ("bot", bo, rbo)):
            if rr is not None:
                worst.append((abs(o - rr), name, role or "%.2f" % st[0], o - rr))
    worst.sort(reverse=True)
    print()
    print("worst residuals:")
    for mag, name, role, dv in worst[:8]:
        print("  %-5s %-12s %+.3f m" % (name, role, dv))

    if args.out:
        out = im.resize((im.width * 2, im.height * 2), Image.LANCZOS)
        d = ImageDraw.Draw(out)
        P = lambda x, y: tuple(v * 2 for v in proj(x, y))
        for pts, col in ((top, (0, 255, 255)), (bot, (0, 255, 0)), (belt, (255, 0, 255))):
            for x, y in pts.items():
                if args.trace:
                    d.ellipse([x * 2 - 3, y * 2 - 3, x * 2 + 3, y * 2 + 3], outline=col, width=2)
                elif x % 3 == 0:
                    d.point((x * 2, y * 2), fill=col)
        d.line([P(x, y) for x, y in ours_top], fill=(0, 255, 255), width=3)
        d.line([P(x, y) for x, y in ours_bot], fill=(0, 255, 0), width=3)
        d.line([P(x, y) for x, y in ours_belt], fill=(255, 0, 255), width=2)
        d.line([P(x, y) for x, y in ours_roof if y > 0.95], fill=(255, 160, 0), width=2)
        for ax in (AF, AR):
            cx, cy = P(ax, R.WHEEL_RADIUS)
            rr = R.WHEEL_RADIUS * scale * 2
            d.ellipse([cx - rr, cy - rr, cx + rr, cy + rr], outline=(255, 255, 0), width=3)
        out.save(args.out)
        print("wrote", args.out)
    if args.json:
        json.dump({"scale": scale, "tilt_deg": tilt, "tire_m": tire_m,
                   "ref_top": ref_top, "ref_bot": ref_bot, "ref_belt": ref_belt}, open(args.json, "w"))


if __name__ == "__main__":
    main()
