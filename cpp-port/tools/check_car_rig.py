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
# R2: the station list is now the hand-authored silhouette keyframes PLUS
# arch samples inserted either side of both axles, so a fixed count is the
# wrong assertion. Check the structure instead: every keyframe survived, the
# list is ordered, and there are enough samples to resolve the arch.
check(len(R.RINGS) == len(R.CHASSIS_STATIONS), "one ring per station")
check(len(R.CHASSIS_STATIONS) >= 24, "enough stations to resolve the arch (%d)" % len(R.CHASSIS_STATIONS))
_key_xs = [st[0] for st in R._KEY_STATIONS]
_all_xs = [st[0] for st in R.CHASSIS_STATIONS]
check(all(any(abs(kx - ax) < 1e-9 for ax in _all_xs) for kx in _key_xs),
      "every hand-authored keyframe station survives into the final list")
check(all(_all_xs[i] > _all_xs[i + 1] for i in range(len(_all_xs) - 1)),
      "stations are strictly ordered nose -> tail")

# R2: the arch is a real opening, not a pinch. At each axle the lip must rise
# clear of the tire's crown; a pinch leaves it at rocker height.
_TIRE_TOP = R.WHEEL_RADIUS * 2.0
for _wx in R._WHEEL_AXLE_X:
    _i = min(range(len(_all_xs)), key=lambda j: abs(_all_xs[j] - _wx))
    _lip_y = R.RINGS[_i][R.K_LIP][1]
    check(_lip_y > _TIRE_TOP,
          "axle x=%+.2f: arch lip y=%.3f clears the tire crown %.3f" % (_wx, _lip_y, _TIRE_TOP))
    check(abs(R.RINGS[_i][1][2]) <= R.ARCH_INNER_Z + 1e-6,
          "axle x=%+.2f: wheelhouse wall pulled to ARCH_INNER_Z (|z|=%.3f)" % (_wx, abs(R.RINGS[_i][1][2])))
check(R.ARCH_R >= R.WHEEL_RADIUS + R.SUSP_MAX_TRAVEL,
      "arch radius clears a fully compressed wheel (%.3f >= %.3f)"
      % (R.ARCH_R, R.WHEEL_RADIUS + R.SUSP_MAX_TRAVEL))
check(all(len(r) == R.NK for r in R.RINGS), "every station has NK=%d ring points" % R.NK)
# R1: assert the ring's STRUCTURE, not a magic count. A count check could
# never catch an asymmetric ring; these can, and they survive future point
# additions (the crease pairs were exactly such an addition).
check(R.NK % 2 == 0, "ring has an even point count (mirrored halves)")
check(all(abs(R.RINGF[k][0] - R.RINGF[R.NK - 1 - k][0]) < 1e-12 for k in range(R.NK)),
      "ring heights are mirror-symmetric")
check(all(abs(R.RINGF[k][1] + R.RINGF[R.NK - 1 - k][1]) < 1e-12 for k in range(R.NK)),
      "ring widths are mirror-symmetric (wf -> -wf)")
check(all(abs(R.RINGV[k] + R.RINGV[R.NK - 1 - k] - 1.0) < 1e-12 for k in range(R.NK)),
      "ring V is mirror-symmetric (v -> 1-v)")

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
# R2: TIGHTENED TO ZERO. The old bound was TOL = 0.08 with a long comment
# explaining why a sliver inside the tire barrel was unavoidable -- true of a
# relief that only pinches |z| at constant height, and the CAR-L/CAR-M/CAR-N
# history is all attempts to tune that graze away. A real arch has no graze:
# every lip point lies on a circle of radius ARCH_R = 0.45 about the axle,
# outside the tire's 0.35 barrel, so the intrusion is exactly none.
TOL = 0.0
check(worst is None,
      "tire-barrel intrusion is exactly zero%s"
      % ("" if worst is None else " -- %.3f at station %d k=%d %s" % (worst[0], worst[1], worst[2], worst[3])))
check(R.ARCH_INNER_Z <= abs(R.TRACK_HALF) - HW - 0.02,
      "wheelhouse wall (%.3f) stays inboard of the tire's inner face (%.3f) with margin"
      % (R.ARCH_INNER_Z, abs(R.TRACK_HALF) - HW))
check(abs(R.TRACK_HALF) + HW <= max(st[1] for st in R.CHASSIS_STATIONS) + 1e-9,
      "tire outer face (%.3f) is inside the widest bodywork (%.3f)"
      % (abs(R.TRACK_HALF) + HW, max(st[1] for st in R.CHASSIS_STATIONS)))

# R2: the arch must be an OPENING, not a pinch, and must leave the upper body
# alone. These replace the old "relief pulls low points inboard" checks, which
# described a mechanism that no longer exists.
_axle_idx = [min(range(len(R.CHASSIS_STATIONS)),
                 key=lambda j: abs(R.CHASSIS_STATIONS[j][0] - wx))
             for wx in R._WHEEL_AXLE_X]
for si, name in zip(_axle_idx, ("front axle", "rear axle")):
    st = R.CHASSIS_STATIONS[si]
    # An opening: the lip must stand well proud of where the un-arched
    # section would have put it. A pinch leaves it at rocker height.
    lip_hf = R.RINGF[R.K_LIP][0]
    y_base = st[3] + (lip_hf / R.SHOULDER) * (st[2] - st[3])
    lip_y = R.RINGS[si][R.K_LIP][1]
    check(lip_y - y_base >= 0.30,
          "%s: arch lip stands %.3f above the un-arched section (>= 0.30 means an opening)"
          % (name, lip_y - y_base))
    # And it must never REACH the beltline -- that would be a fender dent.
    for k, (hf, wf) in enumerate(R.RINGF):
        if hf >= R.SHOULDER:
            check(abs(abs(R.RINGS[si][k][2]) - abs(wf * st[1])) < 1e-9,
                  "%s: arch leaves the beltline and roof untouched (k=%d)" % (name, k))
            break

# The arch must never LOWER any point -- it only ever lifts the section onto
# the lip. This is the assertion that catches a mis-tuned arch folding the ring.
_no_lowering = True
for si, st in enumerate(R.CHASSIS_STATIONS):
    for k, (hf, wf) in enumerate(R.RINGF):
        y_plain = st[3] + (hf / R.SHOULDER) * (st[2] - st[3]) if hf <= R.SHOULDER else None
        if y_plain is not None and R.RINGS[si][k][1] < y_plain - 1e-9:
            _no_lowering = False
check(_no_lowering, "the arch only ever lifts the section, never lowers it (no ring fold-back)")

# R2c: the wheelhouse wall must EASE back out to the body line toward the ends
# of the mouth. The first arch pulled it fully inboard at every station it
# touched, turning a 1.16 m stretch of the car's flank into a trench that the
# (black) tire vanished into. Assert the shape of the taper directly: full
# depth where the tire's radial shadow reaches, and materially shallower at
# the last station before the mouth closes.
_taper_ok, _full_ok = True, True
for _wx in R._WHEEL_AXLE_X:
    _in_mouth = [(si, st) for si, st in enumerate(R.CHASSIS_STATIONS)
                 if 1e-9 < abs(st[0] - _wx) < R.ARCH_X_MAX - 1e-9]
    for si, st in _in_mouth:
        _dx = abs(st[0] - _wx)
        _z = abs(R.RINGS[si][1][2])           # k=1: the wheelhouse wall
        _body_z = abs(R.RINGF[1][1]) * st[1]  # where it would sit un-arched
        if _dx <= R.ARCH_WALL_FULL and _z > R.ARCH_INNER_Z + 1e-6:
            _full_ok = False
    # The outermost station still inside the mouth must be closer to the body
    # line than to the wall, or the taper is not doing anything.
    if _in_mouth:
        si, st = max(_in_mouth, key=lambda p: abs(p[1][0] - _wx))
        _z = abs(R.RINGS[si][1][2])
        _body_z = abs(R.RINGF[1][1]) * st[1]
        if _z - R.ARCH_INNER_Z <= (_body_z - R.ARCH_INNER_Z) * 0.5:
            _taper_ok = False
check(_full_ok, "wheelhouse wall is at full depth everywhere the tire's radial shadow reaches")
check(_taper_ok, "wheelhouse wall eases back toward the body line at the mouth's ends (not a trench)")

# --- UV ---------------------------------------------------------------------
print("UV / livery band alignment")
# R1: anchored by ROLE, not by literal index, so adding crease pairs cannot
# silently slide a band. These indices are named once here and in RINGF.
K_ROCKER, K_BELT_HI, K_ROOF_P = 0, 7, 10
K_ROOF_N = R.NK - 1 - K_ROOF_P
check(R.RINGV[K_ROCKER] >= 0.948,
      "+z rocker v=%.3f sits inside livery's black rocker band [0.948,1.0]" % R.RINGV[K_ROCKER])
check(abs(R.RINGV[K_BELT_HI] - 0.677) < 0.005,
      "+z beltline v=%.3f lands on livery's beltline seam (0.677)" % R.RINGV[K_BELT_HI])
check(abs((R.RINGV[K_ROOF_P] + R.RINGV[K_ROOF_N]) / 2 - 0.5) < 1e-9, "roof edges straddle v=0.5")

# THE ASSERTION THAT WOULD HAVE CAUGHT THE ROOF-NUMBER BUG.
#
# livery.cpp paints the roof number panel across v [0.420, 0.580] -- 0.160
# wide. The old 14-point ring's roof plateau spanned v [0.464, 0.536], only
# 0.072, so the number overflowed onto the drip rail and down the tumblehome
# for the whole life of that mesh. Nothing checked it, because every V check
# only ever asked whether a band fell somewhere on the ring at all.
_ROOF_PANEL = (0.420, 0.580)
check(R.RINGV[K_ROOF_N] <= _ROOF_PANEL[0] and R.RINGV[K_ROOF_P] >= _ROOF_PANEL[1],
      "roof number panel [%.3f,%.3f] fits INSIDE the roof plateau [%.3f,%.3f]"
      % (_ROOF_PANEL[0], _ROOF_PANEL[1], R.RINGV[K_ROOF_N], R.RINGV[K_ROOF_P]))
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

# R2: stations must be addressed BY X, never by index. _build_stations()
# interleaves auto-generated arch samples with the hand-authored silhouette
# rows, so every literal index in this file went stale the moment the arch
# landed -- four glass-U guards started failing against geometry that was
# fine, because index 6 was no longer the cowl. Look the row up by the x_js
# value the table itself is written in, and fail loudly if it is not there.
def _key_station_x(x_js):
    x = x_js * (R.HALF_LEN / 2.51)
    for st in R.CHASSIS_STATIONS:
        if abs(st[0] - x) < 1e-9:
            return st[0]
    raise SystemExit("check_car_rig: no station at x_js=%.3f -- the silhouette "
                     "table moved and this file was not updated with it" % x_js)

# Spans touched by a wheel arch. The arch opening's leading and trailing edges
# are where the lip circle meets the fender at a VERTICAL tangent (dx ==
# ARCH_R), which is a genuine crease on a real car -- the same reasoning the
# around-ring check has always applied to the lip itself. Scoped rather than
# bounded loosely, so the smooth-shell guard keeps its teeth everywhere else.
_ARCH_ST = set(i for i, st in enumerate(R.CHASSIS_STATIONS)
               if any(abs(st[0] - wx) <= R.ARCH_X_MAX + 1e-9 for wx in R._WHEEL_AXLE_X))
_ARCH_SPAN = set()
for _i in _ARCH_ST:
    _ARCH_SPAN.add(_i)
    _ARCH_SPAN.add(_i - 1)

# LENGTHWISE smoothness is the metric that actually decides whether the body
# reads as a curved shell or as a stack of plates: the old mesh gave every
# station-to-station quad one flat normal, so this was a hard facet edge at
# every one of the 15 spans. It is the thing "looks like folded cardboard"
# was describing.
worst_len = max(_turn(R.RING_NRM[i][k], R.RING_NRM[i + 1][k])
                for k in range(R.NK) for i in range(len(R.RINGS) - 1)
                if i not in _ARCH_SPAN)
check(worst_len < 60.0, "lengthwise normal turn peaks at %.1f deg away from the arches (notchback header/backlite are deliberate creases)" % worst_len)
worst_len_arch = max(_turn(R.RING_NRM[i][k], R.RING_NRM[i + 1][k])
                     for k in range(R.NK) for i in range(len(R.RINGS) - 1)
                     if i in _ARCH_SPAN)
# The arch mouth is a crease, but it is not a fold. R2's first cut peaked at
# 154 deg here because the lip (ARCH_CY + ARCH_R = 0.80) reached the front
# axle's beltY (also 0.80) and flattened the entire flank into a horizontal
# shelf. That is the number this bound exists to catch.
check(worst_len_arch < 95.0,
      "arch-mouth lengthwise turn peaks at %.1f deg -- a lip crease, not a folded flank" % worst_len_arch)

# AROUND the ring, real creases are expected and wanted -- a car has a rocker
# line and a wheel-arch lip. The arch is at the two axle stations, where the
# relief tucks the low points inboard while the fender above stays flared;
# that crease is the arch, not an artifact. Everywhere else should be gentle.
worst_ring_other = max(_turn(R.RING_NRM[i][k], R.RING_NRM[i][k + 1])
                       for i in range(len(R.RINGS)) if i not in _ARCH_ST
                       for k in range(R.NK - 1))
worst_ring_axle = max(_turn(R.RING_NRM[i][k], R.RING_NRM[i][k + 1])
                      for i in sorted(_ARCH_ST) for k in range(R.NK - 1))
check(worst_ring_other < 95.0,
      "around-ring turn peaks at %.1f deg (crease pairs and the hood/deck shoulder are deliberate)" % worst_ring_other)
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

# --- glass UV alignment vs real station geometry (K2, plan part 3) ---------
# Before K2, nothing here checked ANY glass rect's U-bound against real
# station geometry -- only that fixed V-bands fell somewhere within the
# ring's full V-range, a much weaker guarantee. That gap is exactly why the
# rear-glass rect shipped painted 1.875x too wide (bleeding onto the trunk
# decklid) without ever failing a check. Same loose-cross-file-sync
# convention this file already uses for the SW_* swatches and the `bands`
# list above: a small commented copy of livery.cpp's own post-K2 glass
# constants (livery.cpp:422,435,443-444), checked against the real station
# table via car_u()/car_v() rather than against each other.
print("glass UV alignment vs real station geometry")

# livery.cpp's own carU() formula, replicated here (same convention
# tests/livery_test.cpp already uses for its own inlined copy of it) --
# it takes raw JS-scale x, a different domain than this file's own car_u()
# below (which takes THIS rig's already-scaled station x). The two are
# equal at corresponding points by construction -- that's the entire
# reason _station() scales every station x by HALF_LEN/2.51 -- but the raw
# and scaled values are not interchangeable inputs to each other's
# function, which is exactly the bug this section's first draft had
# (comparing car_u(0.68) [wrong domain] against the real station value and
# getting a false failure).
def _carU_raw(x):
    return 0.02 + (2.51 - x) / 5.02 * 0.76

_K_SEAM_W = 0.0035  # livery.cpp's own kSeamW, copied here for the same reason
_uWS0, _uWS1 = _carU_raw(0.80), _carU_raw(0.35)
_uSG0 = _carU_raw(0.35) + _K_SEAM_W
_uRG0, _uRG1 = _carU_raw(-0.95), _carU_raw(-1.40) - _K_SEAM_W

_st6_u = R.car_u(_key_station_x(0.80))    # cowl/windshield base
_st8_u = R.car_u(_key_station_x(0.35))    # A-pillar top / roof leading edge
_st10_u = R.car_u(_key_station_x(-0.95))  # C-pillar top / roof trailing edge
_st12_u = R.car_u(_key_station_x(-1.40))  # rear axle / deck start

check(abs(_uWS0 - _st6_u) < 1e-9, "windshield uWS0 exactly matches the cowl station (regression guard)")
check(abs(_uWS1 - _st8_u) < 1e-9, "windshield uWS1 exactly matches the A-pillar station (regression guard)")
check(abs(_uSG0 - (_st8_u + _K_SEAM_W)) < 1e-9,
      "side-glass uSG0 anchors to the A-pillar station + seam margin (K2 fix)")
check(abs(_uRG0 - _st10_u) < 1e-9, "rear-glass uRG0 exactly matches the C-pillar station (regression guard)")
# The actual bug-catcher: fails against the pre-K2 uRG1 (carU(-1.75), which
# overshoots station 11 by far more than 0.01), passes after the fix, and
# would catch a future regression that widens it back out.
check(0 < _st12_u - _uRG1 <= 0.02,
      "rear-glass uRG1 sits strictly inside the real rear-axle station, with a small bounded margin (%.4f)"
      % (_st12_u - _uRG1))

# GV0/GVH deliberately unchanged this phase (see livery.cpp's own K2
# comment) -- documents the real target span is still satisfied today,
# groundwork for a future tightening rather than an assertion of a fix.
# R1: the real beltline is now read from the ROLE index, not the literal k=4
# and k=9 the 14-point ring happened to put it at.
_GV0, _GVH = 0.335, 0.330
_BELT_LO, _BELT_HI = R.RINGV[R.NK - 1 - K_BELT_HI], R.RINGV[K_BELT_HI]
check(_BELT_LO <= _GV0 and _GV0 + _GVH <= _BELT_HI,
      "livery glass V-span [%.3f,%.3f] stays within the real beltline [%.3f,%.3f]"
      % (_GV0, _GV0 + _GVH, _BELT_LO, _BELT_HI))

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
# R2b: read the rim-face radius from the generator's own constant instead of
# repeating the literal 0.68. That literal was a silent trap -- when the rim
# was cut back to a real 15-inch wheel every assertion here would still have
# "passed" while describing geometry that no longer existed.
_R_BEAD = R.WHEEL_RADIUS * R.WHEEL_R_BEAD
_HR = _R_BEAD * R._HUB_R_FRAC
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
        # R2b: the rim face is dished inboard, and the nut sits proud of THAT.
        _z_rim = _z - _side_sign * (_hw * R.WHEEL_Z_DISH)
        _hub_z = _z_rim + _side_sign * (_hw * R._HUB_Z_OFFSET_FRAC)
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
check(0 < _HR < _R_BEAD,
      "hub-nut radius (%.4f) stays strictly inside the rim face's own R_INNER (%.4f)"
      % (_HR, _R_BEAD))
# R2b: the defect the turntable caught -- the rim face reached 0.68 of the
# radius, so the black sidewall was a 0.22-wide ring and every wheel rendered
# as a pale disc. Assert the tire, not the rim, is the dominant band.
_sidewall = R.WHEEL_R_SHOULDER - R.WHEEL_R_BEAD
check(_sidewall >= 0.35,
      "sidewall band is %.2f of the radius (>= 0.35; the I1 wheel that read as a pale disc had 0.32)"
      % _sidewall)
check(R.WHEEL_R_BEAD < 0.60,
      "rim face (%.2f of the radius) stays inside a real 15-in-wheel-in-a-28-in-tire proportion"
      % R.WHEEL_R_BEAD)
# The spoke gaps must actually be a DIFFERENT swatch from the spokes, or the
# face is a flat disc again with extra triangles. Count both on one outer cap.
_fx, _fz = R.wheel_offsets[0]
_f_rim = sum(1 for uv, j, p in zip(R.uvs, R.joints0, R.positions)
             if j[0] == 1 and uv == R.SW_RIM
             and abs(math.hypot(p[0] - _fx, p[1] - R.WHEEL_RADIUS) - _R_BEAD) < 1e-9)
_f_gap = sum(1 for uv, j, p in zip(R.uvs, R.joints0, R.positions)
             if j[0] == 1 and uv == R.SW_TREAD
             and abs(math.hypot(p[0] - _fx, p[1] - R.WHEEL_RADIUS) - _R_BEAD) < 1e-9)
_exp = R.WHEEL_RIM_SECTORS  # sectors/2 wedges * 2 rim verts each, per class
check(_f_rim == _exp and _f_gap == _exp,
      "rim face is %d spoke wedges alternating with %d dark gaps (got %d metal / %d dark rim verts)"
      % (R.WHEEL_SPOKES, R.WHEEL_SPOKES, _f_rim, _f_gap))
check(_hw * R._HUB_Z_OFFSET_FRAC < 0.3 * _hw,
      "hub-nut z-embed offset stays small relative to half_width")

print("\nverts %d  tris %d" % (len(R.positions), len(R.indices) // 3))
print("check_car_rig: PASS" if ok else "check_car_rig: FAILURES ABOVE")
sys.exit(0 if ok else 1)
