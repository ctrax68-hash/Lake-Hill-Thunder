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

# --- nose/tail cap (K1, car visual fidelity plan part 3) --------------------
# The old cap fan's apex X was literally the station's own X -- a flat 2D
# disc, not a convex bumper fascia -- which nothing here ever checked,
# because nothing here ever computed or asserted an expected apex offset in
# the first place. These checks isolate cap vertices by index range
# (R.NOSE_CAP_RANGE/R.TAIL_CAP_RANGE, exposed by gen_car_rig.py for exactly
# this purpose -- caps sample ordinary body-livery UV, no distinguishing
# swatch the way a prop like the spoiler has, so an index range is the only
# reliable way to find them from outside that file).
print("nose/tail cap")
nose_positions = R.positions[R.NOSE_CAP_RANGE[0]:R.NOSE_CAP_RANGE[1]]
tail_positions = R.positions[R.TAIL_CAP_RANGE[0]:R.TAIL_CAP_RANGE[1]]
nose_normals = R.normals[R.NOSE_CAP_RANGE[0]:R.NOSE_CAP_RANGE[1]]
tail_normals = R.normals[R.TAIL_CAP_RANGE[0]:R.TAIL_CAP_RANGE[1]]

nose_xs = sorted({round(p[0], 9) for p in nose_positions})
check(len(nose_xs) > 1,
      "nose cap vertices do NOT all share one X (real 3D convexity, not a flat disc)")
nose_dx = max(nose_xs) - min(nose_xs) if len(nose_xs) > 1 else 0.0
check(0.02 < nose_dx < 0.15, "nose apex offset is bounded and plausible (%.4f)" % nose_dx)

# Tail deliberately untouched by K1 -- this is the scope-boundary regression
# guard: if a future edit "helpfully" applies the same forward-offset
# treatment to the tail without its own reported symptom and its own
# justification, this fails instead of silently drifting.
tail_xs = {round(p[0], 9) for p in tail_positions}
check(len(tail_xs) == 1,
      "tail cap vertices all share one X (deliberately untouched -- K1 is nose-only by design)")

all_cap_normals = nose_normals + tail_normals
check(all(all(c == c for c in n) for n in all_cap_normals), "no NaN in any cap normal")
check(all(abs(math.sqrt(sum(c * c for c in n)) - 1.0) < 1e-6 for n in all_cap_normals),
      "all cap normals unit length")
check(all(n[0] > 0 for n in nose_normals), "nose cap normals face outward (+X)")
check(all(n[0] < 0 for n in tail_normals), "tail cap normals face outward (-X)")

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

# --- spoiler endplates (J3, car visual fidelity plan part 2) ----------------
# Position-checked rather than count-checked: a vertex-count delta could
# pass even if the endplate landed in the wrong place, but exact corner
# coincidence with the blade's own front/rear corners is the actual point
# (no visible seam where the new surface meets the existing blade/riser).
print("spoiler endplates")
_EP_TOL = 1e-9
def _has_point(p):
    return any(all(abs(a - b) < _EP_TOL for a, b in zip(p, q)) for q in R.positions)
_ep_expected = []
for _epz in (R._spz, -R._spz):
    _ep_expected.extend([
        (R._sp_x0 - 0.05, R._sp_deckY, _epz),
        (R._sp_x1, R._sp_deckY, _epz),
        (R._sp_x1, R._sp_y1, _epz),
        (R._sp_x0, R._sp_y0, _epz),
    ])
_ep_missing = [p for p in _ep_expected if not _has_point(p)]
check(not _ep_missing, "all 8 endplate corner positions are present in the mesh")
check(all(_has_point((R._sp_x0, R._sp_y0, z)) for z in (R._spz, -R._spz)),
      "endplate's top-front corner is coincident with the blade's own front corner")
check(all(_has_point((R._sp_x1, R._sp_y1, z)) for z in (R._spz, -R._spz)),
      "endplate's top-back corner is coincident with the blade's own rear corner")

# --- front splitter (J4, car visual fidelity plan part 2) -------------------
# Identified by swatch + position: SW_SPOILER_DARK is shared with the
# spoiler's own underside/risers/endplates, but those all sit near the tail
# (x < 0); the splitter sits near the nose (x > 0), so a simple x-sign split
# can't cross-contaminate the two.
print("front splitter")
_spl_idx = [i for i, (uv, j, p) in enumerate(zip(R.uvs, R.joints0, R.positions))
            if uv == R.SW_SPOILER_DARK and j[0] == 0 and p[0] > 0]
check(len(_spl_idx) == 12, "splitter vertex count == 3 quads * 4 verts (got %d)" % len(_spl_idx))

# Ground clearance -- the single biggest risk flagged for this phase before
# any code was written. ">0.02", not just ">0": the plan's own reasoning is
# that this rig has no dynamic ride-height/pitch at the chassis level (only
# per-wheel suspension travel), so a static margin here is a real buffer,
# not a nominal one that a future feature could silently eat into.
_spl_min_y = min(R.positions[i][1] for i in _spl_idx) if _spl_idx else None
check(_spl_min_y is not None and _spl_min_y > 0.02,
      "splitter clears the ground plane with a real margin (min y=%.3f > 0.02)"
      % (-1 if _spl_min_y is None else _spl_min_y))

# Regression guard that the geometry actually protrudes past the nose
# rather than being accidentally recessed under the nose cap.
check(any(R.positions[i][0] > R.HALF_LEN for i in _spl_idx),
      "splitter actually protrudes past the nose tip (some x > HALF_LEN=%.3f)" % R.HALF_LEN)

# Clearance from the front tire's cylindrical volume -- the splitter's wide
# z-span (0.90 of the nose's own halfWidth) makes this worth checking
# explicitly, same reasoning J2's exhaust check above already applied to
# the rear tire (_inner_r/_outer_r are that same check's own values --
# TRACK_HALF/HW are identical for the front and rear axles, so they're
# valid here unchanged, not recomputed).
_front_axle_x = R._WHEEL_AXLE_X[0]  # WHEELBASE/2, matches wheel_offsets' FL/FR row
_worst_spl = None
for i in _spl_idx:
    x, y, z = R.positions[i]
    if math.hypot(x - _front_axle_x, y - WR) < WR and _inner_r + EPS < abs(z) < _outer_r - EPS:
        depth = min(abs(z) - _inner_r, _outer_r - abs(z))
        if _worst_spl is None or depth > _worst_spl:
            _worst_spl = depth
check(_worst_spl is None, "splitter clears the front tire's cylindrical volume%s"
      % ("" if _worst_spl is None else " (worst overlap %.3f)" % _worst_spl))

# --- wheel hub center-lock nut (J5, car visual fidelity plan part 2) --------
# Self-contained: recomputes each wheel's own expected hub-nut geometry
# exactly the way add_wheel() does (same R_INNER/half_width formulas from
# the call site, same _HUB_R_FRAC/_HUB_SIDES/_HUB_Z_OFFSET_FRAC constants
# gen_car_rig.py exposes at module level for exactly this reason), then
# checks for its exact presence/absence -- more precise than inferring
# from radius bands alone, since the hub-nut's own centre point and the
# pre-existing hub-disc fan's centre point share the same (x,y) and would
# otherwise be ambiguous without also checking z.
print("wheel hub center-lock nut")
_HR = R.WHEEL_RADIUS * 0.68 * R._HUB_R_FRAC  # R_INNER * _HUB_R_FRAC (R_INNER = radius*0.68 in add_wheel())
_hw = R.WHEEL_RADIUS * 0.4  # half_width, matching the add_wheel() call site's own WHEEL_RADIUS*0.4
_hub_ring_ok = True
_hub_joint_ok = True
_hub_outer_only_ok = True
for _wi, (wx, wz) in enumerate(R.wheel_offsets):
    _joint = _wi + 1
    _cx, _cy, _cz = wx, R.WHEEL_RADIUS, wz
    _outer_sign = 1 if _cz > 0 else -1
    for _side_sign in (1, -1):
        _z = _cz + _side_sign * _hw
        _hub_z = _z + _side_sign * (_hw * R._HUB_Z_OFFSET_FRAC)
        _found = [i for i, (uv, j, p) in enumerate(zip(R.uvs, R.joints0, R.positions))
                  if uv == R.SW_RIM and j[0] == _joint
                  and abs(p[2] - _hub_z) < 1e-9
                  and abs(math.hypot(p[0] - _cx, p[1] - _cy) - _HR) < 1e-9]
        if _side_sign == _outer_sign:
            if len(_found) != R._HUB_SIDES: _hub_ring_ok = False
            if any(R.joints0[i][0] != _joint for i in _found): _hub_joint_ok = False
        else:
            if len(_found) != 0: _hub_outer_only_ok = False
check(_hub_ring_ok, "every wheel's outer cap carries exactly %d hub-nut ring vertices" % R._HUB_SIDES)
check(_hub_joint_ok,
      "every hub-nut vertex is bound to its own wheel's joint (1-4), never chassis joint 0")
check(_hub_outer_only_ok, "no hub-nut geometry on any wheel's inner (never-seen) cap")
check(0 < _HR < R.WHEEL_RADIUS * 0.68,
      "hub-nut radius (%.4f) stays strictly inside the hub disc's own R_INNER (%.4f)"
      % (_HR, R.WHEEL_RADIUS * 0.68))
check(_hw * R._HUB_Z_OFFSET_FRAC < 0.3 * _hw,
      "hub-nut z-embed offset stays small relative to half_width")

print("\nverts %d  tris %d" % (len(R.positions), len(R.indices) // 3))
print("check_car_rig: PASS" if ok else "check_car_rig: FAILURES ABOVE")
sys.exit(0 if ok else 1)
