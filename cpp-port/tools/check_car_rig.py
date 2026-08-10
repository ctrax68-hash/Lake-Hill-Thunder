# H1: a standing check on the generated car rig -- run it after any change to
# gen_car_rig.py.
#
#     python3 tools/check_car_rig.py
#
# Kept as a committed tool rather than a throwaway scratch program (the
# G12-G14 "decode it directly" precedent) because the two things it guards
# are invisible in a screenshot until they are badly wrong, and H2 is going
# to touch the same UVs again:
#   1. wheel-arch relief -- the CAR-F/CAR-L/CAR-M/CAR-N history is four
#      phases of fighting body geometry poking through the tire. The ring's
#      widest point sits near tire mid-height, so this has to be proven, not
#      assumed.
#   2. V-band alignment -- livery.cpp paints fixed V bands and the new ring
#      has to land them where the old 4-corner rule did.
import sys, math, os
_HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, _HERE)
os.chdir(_HERE)  # gen_car_rig.py writes its output relative to tools/
import gen_car_rig as R

ok = True
def check(cond, what):
    global ok
    print(("  ok   " if cond else "  FAIL ") + what)
    if not cond: ok = False

print("geometry")
check(len(R.RINGS) == 16, "16 stations")
check(all(len(r) == R.NK for r in R.RINGS), "every station has NK=%d ring points" % R.NK)
check(R.NK == 14, "ring is JS's 14 points")

# The ring is deliberately NOT z-monotonic -- RINGF goes 0.76 -> 0.96 ->
# 1.00 at the bottom, i.e. the rocker tucks in under the widest point, which
# is what a real car section does. What has to hold is that the UNWRAP has
# no fold-back, and v is by ring index, so that is the thing to assert.
check(all(R.car_v(k) > R.car_v(k + 1) for k in range(R.NK - 1)),
      "v strictly decreasing by ring index (unwrap cannot fold back)")

# --- wheel clearance -------------------------------------------------------
# Wheel: barrel of radius 0.35 centred at (axleX, 0.35, +-TRACK_HALF), axis Z,
# half-width 0.35*0.4 = 0.14.
#
# "Clear" does NOT mean "outside the barrel" -- the fender is supposed to
# wrap over and around the tire. It means: any body point standing in the
# tire's radial shadow must stay INBOARD of the tire's inner face (the body
# tucks in beside the wheel) or OUTBOARD of its outer face (the fender
# covers it). Anything strictly between the two faces is the tire poking
# through bodywork, which is the CAR-F/CAR-M bug.
WR, HW = R.WHEEL_RADIUS, R.WHEEL_RADIUS * 0.4
EPS = 1e-6
print("wheel clearance (no body point between a tire's inner and outer faces)")
worst = None
for axle_x in R._WHEEL_AXLE_X:
    for sgn in (1, -1):
        wz = sgn * R.TRACK_HALF
        inner, outer = abs(wz) - HW, abs(wz) + HW
        for i, r in enumerate(R.RINGS):
            for k, p in enumerate(r):
                x, y, z = p
                if (z > 0) != (sgn > 0): continue          # other side of the car
                if math.hypot(x - axle_x, y - WR) >= WR: continue  # not in the radial shadow
                az = abs(z)
                if inner + EPS < az < outer - EPS:
                    depth = min(az - inner, outer - az)
                    if worst is None or depth > worst[0]:
                        worst = (depth, i, k, p)
# The relief tapers with distance from the axle, so a station part-way along
# the notch range keeps a sliver inside the barrel. JS has the identical
# formula and constants, and every "fix" for it (hard-clamping, widening the
# range) trades this graze for a visible STEP in the rocker line between
# stations -- which is the CAR-L/CAR-M dent all over again. So bound it
# instead of demanding zero: it must be shallow, and confined to the low
# (hf<=0.30) rocker points that live behind the tire from every external
# viewpoint. Anything at fender height would be the real bug.
TOL = 0.08
check(worst is None or worst[0] <= TOL,
      "tire-barrel intrusion within tolerance (%.3f <= %.3f)%s"
      % (0.0 if worst is None else worst[0], TOL,
         "" if worst is None else " at station %d k=%d %s" % (worst[1], worst[2], worst[3])))
check(worst is None or R.RINGF[worst[2]][0] <= R._WHEEL_RELIEF_HF,
      "any intrusion is at rocker height (hf=%.2f <= %.2f), not fender height"
      % (0.0 if worst is None else R.RINGF[worst[2]][0], R._WHEEL_RELIEF_HF))
check(abs((abs(R.TRACK_HALF) - HW) - R._WHEEL_INNER_Z) < 1e-6,
      "tire inner face (%.3f) == WHEEL_INNER_Z (%.3f), so the JS-tuned relief targets the real tire"
      % (abs(R.TRACK_HALF) - HW, R._WHEEL_INNER_Z))
check(abs(R.TRACK_HALF) + HW <= max(st[1] for st in R.CHASSIS_STATIONS) + 1e-9,
      "tire outer face (%.3f) is inside the widest bodywork (%.3f)"
      % (abs(R.TRACK_HALF) + HW, max(st[1] for st in R.CHASSIS_STATIONS)))

# The relief must actually be doing something at the axle stations. It
# TAPERS with distance from the axle (JS's `t = 1 - dx/RANGE`), so it only
# reaches WHEEL_INNER_Z exactly at dx=0 -- assert it gets close, not that it
# snaps.
for si, name in ((4, "front axle"), (11, "rear axle")):
    st = R.CHASSIS_STATIONS[si]
    relieved = [k for k, (hf, wf) in enumerate(R.RINGF)
                if hf <= R._WHEEL_RELIEF_HF and abs(wf * st[1]) > R._WHEEL_INNER_Z + 1e-9]
    worst_k = max(relieved, key=lambda k: abs(R.RINGS[si][k][2])) if relieved else None
    got = all(abs(R.RINGS[si][k][2]) < abs(R.RINGF[k][1] * st[1]) - 1e-9        # moved inboard
              and abs(R.RINGS[si][k][2]) <= R._WHEEL_INNER_Z + 0.05             # nearly all the way
              for k in relieved)
    check(len(relieved) > 0 and got,
          "%s station: all %d low ring points pulled to ~WHEEL_INNER_Z (worst |z|=%.3f vs target %.3f)"
          % (name, len(relieved), abs(R.RINGS[si][worst_k][2]) if worst_k is not None else -1,
             R._WHEEL_INNER_Z))
# ...and must NOT be touching anything above hf=0.30 (the old visible-dent bug).
untouched = True
for si in (4, 11):
    st = R.CHASSIS_STATIONS[si]
    for k, (hf, wf) in enumerate(R.RINGF):
        if hf > R._WHEEL_RELIEF_HF and abs(abs(R.RINGS[si][k][2]) - abs(wf * st[1])) > 1e-9:
            untouched = False
check(untouched, "relief touches nothing above hf=0.30 (no fender dent)")

# --- UV ---------------------------------------------------------------------
print("UV / livery band alignment")
anchors = [(0, 0.945, "+z rocker"), (4, 0.700, "+z beltline"),
           (9, 0.300, "-z beltline"), (13, 0.055, "-z rocker")]
for k, old_v, name in anchors:
    v = R.car_v(k)
    check(abs(v - old_v) < 0.035, "%s: v=%.3f vs old %.3f (delta %.3f)" % (name, v, old_v, abs(v - old_v)))
check(abs((R.car_v(6) + R.car_v(7)) / 2 - 0.5) < 1e-6, "roof edges straddle v=0.5")
check(all(R.car_v(k) > R.car_v(k + 1) for k in range(R.NK - 1)), "v strictly decreasing around the ring")

# livery.cpp's painted bands must still fall on real body surface.
bands = [("rocker/seam (near-black)", 0.948, 1.000), ("seam (near-black)", 0.000, 0.052),
         ("side glass +z", 0.590, 0.665), ("side glass -z", 0.335, 0.410),
         ("door number +z", 0.760, 0.770), ("door number -z", 0.230, 0.240)]
vmin, vmax = R.car_v(R.NK - 1), R.car_v(0)
for name, lo, hi in bands:
    covered = not (hi < vmin or lo > vmax)
    check(covered, "band %-24s [%.3f,%.3f] within ring V span [%.3f,%.3f]" % (name, lo, hi, vmin, vmax))

# U must still span the livery's paint range exactly at the tips.
us = [R.car_u(st[0]) for st in R.CHASSIS_STATIONS]
check(abs(us[0] - 0.02) < 1e-6, "nose station u == 0.02")
check(abs(us[-1] - 0.78) < 1e-6, "tail station u == 0.78")

# --- normals ----------------------------------------------------------------
print("normals")
check(all(abs(math.sqrt(sum(c * c for c in n)) - 1.0) < 1e-6
          for row in R.RING_NRM for n in row), "all ring normals unit length")
# Outward: dot with the outward direction from the section's mid axis.
inward = 0
for i, row in enumerate(R.RING_NRM):
    mid = (R.CHASSIS_STATIONS[i][2] + R.CHASSIS_STATIONS[i][3]) / 2.0
    for k, n in enumerate(row):
        p = R.RINGS[i][k]
        if n[1] * (p[1] - mid) + n[2] * p[2] < -1e-9: inward += 1
check(inward == 0, "every ring normal faces outward")
def _turn(a, b):
    d = max(-1.0, min(1.0, sum(x * y for x, y in zip(a, b))))
    return math.degrees(math.acos(d))

# LENGTHWISE smoothness is the metric that actually decides whether the body
# reads as a curved shell or as a stack of plates: the old mesh gave every
# station-to-station quad one flat normal, so this was a hard facet edge at
# every one of the 15 spans. It is the thing "looks like folded cardboard"
# was describing.
worst_len = max(_turn(R.RING_NRM[i][k], R.RING_NRM[i + 1][k])
                for k in range(R.NK) for i in range(len(R.RINGS) - 1))
check(worst_len < 45.0, "lengthwise normal turn peaks at %.1f deg (was a hard facet at every span)" % worst_len)

# AROUND the ring, real creases are expected and wanted -- a car has a rocker
# line and a wheel-arch lip. The arch is at the two axle stations, where the
# relief tucks the low points inboard while the fender above stays flared;
# that crease is the arch, not an artifact. Everywhere else should be gentle.
AXLE_ST = (4, 11)
worst_ring_other = max(_turn(R.RING_NRM[i][k], R.RING_NRM[i][k + 1])
                       for i in range(len(R.RINGS)) if i not in AXLE_ST
                       for k in range(R.NK - 1))
worst_ring_axle = max(_turn(R.RING_NRM[i][k], R.RING_NRM[i][k + 1])
                      for i in AXLE_ST for k in range(R.NK - 1))
check(worst_ring_other < 70.0,
      "around-ring turn away from the axles peaks at %.1f deg" % worst_ring_other)
print("  note  wheel-arch crease at the axle stations: %.1f deg (expected -- a car has an arch lip)"
      % worst_ring_axle)

# --- exhaust pipes (J2, car visual fidelity plan part 2) --------------------
# Identified by swatch + joint rather than a hand-picked x/z box: SW_SIDEWALL
# is also used by every wheel's own tire end-cap annulus, but those are
# jointed 1-4 (wheel_animation.cpp's per-wheel bones) while the exhaust is
# chassis-bound (joint 0, same as the spoiler/mirrors) -- so this filter
# can't accidentally pick up wheel geometry no matter where either sits.
print("exhaust pipes")
exh_idx = [i for i, (uv, j) in enumerate(zip(R.uvs, R.joints0))
           if uv == R.SW_SIDEWALL and j[0] == 0]
check(len(exh_idx) == 2 * R._EXH_SEGS * 6,
      "exhaust vertex count == 2 pipes * %d segs * 6 verts (got %d)" % (R._EXH_SEGS, len(exh_idx)))
check(all(R.positions[i][2] < 0 for i in exh_idx),
      "every exhaust vertex is on the right side (z < 0, this port's mirrored-vs-JS convention)")
check(all(R.positions[i][1] > 0 for i in exh_idx),
      "every exhaust vertex clears the ground plane (y > 0)")

# Clearance from the rear tire's cylindrical volume -- same radial-shadow
# test the wheel-clearance section above uses, recomputed locally (not
# reused from that loop's own inner/outer, which are left holding whichever
# axle/side its loop last iterated, not necessarily the rear-right pair
# needed here).
_rear_axle_x = R._WHEEL_AXLE_X[1]  # -WHEELBASE/2, matches wheel_offsets' RL/RR row
_inner_r, _outer_r = R.TRACK_HALF - HW, R.TRACK_HALF + HW
_worst_exh = None
for i in exh_idx:
    x, y, z = R.positions[i]
    if math.hypot(x - _rear_axle_x, y - WR) < WR and _inner_r + EPS < abs(z) < _outer_r - EPS:
        depth = min(abs(z) - _inner_r, _outer_r - abs(z))
        if _worst_exh is None or depth > _worst_exh:
            _worst_exh = depth
check(_worst_exh is None, "exhaust geometry clears the rear tire's cylindrical volume%s"
      % ("" if _worst_exh is None else " (worst overlap %.3f)" % _worst_exh))

print("\nverts %d  tris %d" % (len(R.positions), len(R.indices) // 3))
print("check_car_rig: PASS" if ok else "check_car_rig: FAILURES ABOVE")
sys.exit(0 if ok else 1)
