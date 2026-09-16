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

# T1: THE ROOF MUST NOT BE ONE FLAT QUAD.
#
# Until T1 the ring's two topmost points were the roof edges with nothing
# between them, so the quad joining them spanned 1.110 m -- 61% of the car's
# full width -- carrying a single normal. That is the largest surface the
# Chase camera sees on the car ahead and it could not take a highlight. It was
# invisible to every check here because every check asked about the ring's
# PROFILE and none asked how far apart two adjacent points were.
_widest_top = max(abs(R.RINGF[k][1] - R.RINGF[k + 1][1])
                  for k in range(R.NK - 1) if R.RINGF[k][0] >= 0.95 and R.RINGF[k + 1][0] >= 0.95)
check(_widest_top <= 0.40,
      "widest single quad across the roof spans %.3f of half-width (<= 0.40; it was 1.22 when the roof was one flat strip)"
      % _widest_top)
_crown = max(hf for (hf, _w) in R.RINGF) - R.RINGF[R.K_ROOF_EDGE][0]
check(_crown > 0.0,
      "roof crowns above its own edge (crown reaches hf %.3f vs edge %.3f)"
      % (max(hf for (hf, _w) in R.RINGF), R.RINGF[R.K_ROOF_EDGE][0]))
# The crown sits PAST the roof edge in the point order, so the roof edge must
# still be the thing the livery's V anchor is pinned to -- if a future edit
# lets the role slide onto a crown point, the number panel silently loses its
# plateau. That is exactly the failure the role-name lookup exists to stop.
check(abs(R.RINGV[R.K_ROOF_EDGE] - 0.588) < 1e-9,
      "roof-edge role still carries the painted V anchor 0.588 (got %.4f)" % R.RINGV[R.K_ROOF_EDGE])
check(R.RINGV[R.NK // 2 - 1] > 0.5,
      "half-ring stops strictly above v=0.5 so its mirror is not a duplicate (got %.4f)"
      % R.RINGV[R.NK // 2 - 1])

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
# R3a: READ the role indices from the generator instead of restating them.
# Naming them here was the same latent trap as the index-based station lookups
# and the literal 0.68 rim radius -- a ring edit slides every one of them and
# the guards keep "passing" against something else. The ring grew 11 -> 16
# half-points and the beltline crease was removed in R3a; nothing below had to
# change, which is the point.
K_ROCKER, K_BELT_HI, K_ROOF_P = R.K_ROCKER, R.K_BELT, R.K_ROOF_EDGE
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
# T7: the tips are DOMES now, and the loft's last section ring sits one cap
# depth inside the car's true extent -- so the last ring no longer lands on the
# painted range's exact end. The cap fills that sliver (it samples its own
# station's u, this file's long-standing flat-swatch convention). What still
# has to hold is that the loft starts inside the paint and close to its edge.
check(0.02 <= us[0] <= 0.045, "nose section ring starts just inside the painted range (u=%.4f)" % us[0])
check(0.755 <= us[-1] <= 0.78, "tail section ring ends just inside the painted range (u=%.4f)" % us[-1])

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
def _key_station_x(role):
    """Scaled x of a named landmark station.

    T6: BY ROLE, not by x_js literal. The previous version took a coordinate,
    and re-authoring the silhouette against the reference photo moved every one
    of them at once -- it failed loudly, which was the point, but a name never
    needed updating in the first place. gen_car_rig.py owns the mapping.
    """
    x = R.station_x(role)
    for st in R.CHASSIS_STATIONS:
        if abs(st[0] - x) < 1e-9:
            return st[0]
    raise SystemExit("check_car_rig: station role %r resolves to x=%.4f, but no "
                     "station sits there" % (role, x))

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

# --- T23: the greenhouse's creases are IN THE EMITTED MESH ------------------
#
# Every check above reads R.RING_NRM, the smooth table, which by design still
# holds the averaged normal at a crease. So none of them can tell whether the
# crease reached the vertices bgfx is handed -- and a normal table that is
# built correctly and then not wired into emit_smooth_quad() would leave the
# car looking exactly as domed as before while all of the above stayed green.
# That is the failure this project has shipped repeatedly.
#
# This measures R.positions/R.normals, the arrays that become the glTF: at a
# crease station, two quads meeting at one ring point must carry MEASURABLY
# DIFFERENT normals for that same position; at a smooth station they must not.
# The angles are printed, so a pass states what it found rather than only that
# it found something.
print()
print("greenhouse creases (measured on the emitted vertices, not on RING_NRM)")


def _emitted_normal_spread(x_station, upper_only=True):
    """Largest angle between two emitted normals sharing one loft position."""
    by_pos = {}
    for idx, p in enumerate(R.positions):
        if abs(p[0] - x_station) > 1e-6:
            continue
        key = (round(p[1], 6), round(p[2], 6))
        by_pos.setdefault(key, []).append(R.normals[idx])
    # The upper section is the only place T23 creases; a ring point's height
    # stands in for its mirrored index here, because the emitted arrays carry
    # positions, not ring indices. K_BELT's own height at this station is the
    # boundary, computed from the station rather than assumed.
    st = [s for s in R.CHASSIS_STATIONS if abs(s[0] - x_station) < 1e-9][0]
    belt_hf = R._RING_HALF_F[R.K_BELT][0]
    belt_y = st[3] + (belt_hf / R.SHOULDER) * (st[2] - st[3])
    worst = 0.0
    for (y, _z), ns in by_pos.items():
        if upper_only and y < belt_y - 1e-6:
            continue
        for a in range(len(ns)):
            for b in range(a + 1, len(ns)):
                worst = max(worst, _turn(ns[a], ns[b]))
    return worst


for _role in R._CREASE_ROLES:
    _x = R.station_x(_role)
    _spread = _emitted_normal_spread(_x)
    check(_spread > 12.0,
          "%-10s is a real edge in the emitted mesh: %.1f deg between its two sides"
          % (_role, _spread))

# The control. roof_mid sits in the middle of the flat roof, where the surface
# genuinely does not bend, and it is NOT in _CREASE_ROLES -- so if this ever
# reports a spread, the crease selection has leaked past the roles it names.
_smooth_spread = _emitted_normal_spread(R.station_x("roof_mid"))
check(_smooth_spread < 1.0,
      "roof_mid stays smooth in the emitted mesh: %.2f deg (the control -- it is not a crease role)"
      % _smooth_spread)

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
# T7: THIS ASSERTION IS DELIBERATELY REVERSED, and the reason is worth stating
# because it used to guard the opposite property.
#
# K1 rounded the nose and left the tail flat on purpose, and this check existed
# so nobody "helpfully" rounded the tail without a reported symptom. There is
# now a reported symptom. A high-resolution CHASE frame -- the view the player
# looks at for an entire race, which no round before T6b had examined -- shows
# the rear reading as a flat billboard: a single 0.60 x 1.64 m plane with one
# normal and square corners. The scope boundary was right when it was set and
# is wrong now, so it moves rather than being quietly deleted.
tail_xs = {round(p[0], 9) for p in tail_positions}
check(len(tail_xs) > 1,
      "tail cap is a DOME, not a flat plane (%d distinct X layers)" % len(tail_xs))
_tail_depth = max(tail_xs) - min(tail_xs) if len(tail_xs) > 1 else 0.0
check(abs(_tail_depth - R.TAIL_CAP_DEPTH) < 1e-6,
      "tail dome stands exactly TAIL_CAP_DEPTH (%.3f) off its section ring" % _tail_depth)
# And the nose is deeper than the tail: a bumper fascia bulges, a tail panel
# only softens its corners. 0.06 on a 0.77 m face was the flat-disc figure.
check(R.NOSE_CAP_DEPTH > R.TAIL_CAP_DEPTH and R.NOSE_CAP_DEPTH >= 0.08,
      "nose dome (%.3f) is deeper than the tail dome (%.3f) and no longer a disc"
      % (R.NOSE_CAP_DEPTH, R.TAIL_CAP_DEPTH))

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
# T23: the four station literals moved with the greenhouse (cowl 0.585->0.80,
# A-pillar 0.03->0.205, C-pillar -0.78->-0.505, deck start -1.67->-1.56). This
# copy tracking them is the whole point of the section: if livery.cpp had been
# left on the old numbers, the painted windshield would now sit on the hood and
# these checks are what says so.
_uWS0, _uWS1 = _carU_raw(0.80), _carU_raw(0.205)
_uSG0 = _carU_raw(0.205) + _K_SEAM_W
_uRG0, _uRG1 = _carU_raw(-0.505), _carU_raw(-1.56) - _K_SEAM_W

_st6_u = R.car_u(_key_station_x("cowl"))    # cowl/windshield base
_st8_u = R.car_u(_key_station_x("roof_lead"))    # A-pillar top / roof leading edge
_st10_u = R.car_u(_key_station_x("roof_trail"))  # C-pillar top / roof trailing edge
_st12_u = R.car_u(_key_station_x("deck_start"))  # deck start

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
# T23d: 0.335/0.330 -> 0.405/0.190. The windshield and backlite rects used to
# span the whole beltline, painting glass over the A-pillars and the rear
# quarter panels; they are now the section's TOP plateau plus a little turn onto
# the pillar. So the interesting bound is no longer "inside the beltline" (which
# a narrower band satisfies trivially) but that the band still COVERS the roof
# plateau -- a windshield narrower than the roof it meets leaves a painted strip
# of body colour running down the middle of the glass.
_GV0, _GVH = 0.405, 0.190
_ROOF_LO, _ROOF_HI = R.RINGV[R.NK - 1 - R.K_ROOF_EDGE], R.RINGV[R.K_ROOF_EDGE]
check(_GV0 <= _ROOF_LO and _ROOF_HI <= _GV0 + _GVH,
      "glass V-span [%.3f,%.3f] covers the whole roof plateau [%.3f,%.3f]"
      % (_GV0, _GV0 + _GVH, _ROOF_LO, _ROOF_HI))
check((_ROOF_LO - _GV0) < 0.03 and ((_GV0 + _GVH) - _ROOF_HI) < 0.03,
      "glass V-span overhangs the plateau by only %.3f / %.3f -- it turns onto the pillar, it does not cover it"
      % (_ROOF_LO - _GV0, (_GV0 + _GVH) - _ROOF_HI))
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

# T19: THE SPLITTER MUST TOUCH THE CAR.
#
# It hung 0.030 m below the bodywork with nothing joining them: the lowest true
# body vertex near the nose was at y = 0.080 and the splitter's top face at
# 0.050, so from any side-on angle there was daylight between the two and it
# read as a detached plank floating under the car.
#
# Nothing could have caught that. Both pieces were individually correct -- the
# splitter protruded, cleared the ground, cleared the tire, and every existing
# clause passed. The defect was the RELATIONSHIP between two things each guard
# only looked at alone, which is the same shape as T11's buried wheels: a set of
# individually-satisfied constraints says nothing about the quantity they
# jointly determine.
#
# "True bodywork" excludes anything sampling a flat SW_* swatch point, because
# the splitter's own rear vertices sit behind HALF_LEN and a naive x filter
# counts them as body -- which is exactly the mistake the first measurement of
# this made, reporting a -0.020 m gap (an overlap) when the real figure was
# +0.030.
_SW_PTS = {R.SW_SPOILER_DARK, R.SW_SPOILER_BODY, R.SW_TREAD, R.SW_SIDEWALL,
           R.SW_TIRE_LETTER, R.SW_RIM, R.SW_MIRROR, R.SW_BEAD, R.SW_LUG}
_nose_body_y = [p[1] for p, uv in zip(R.positions, R.uvs)
                if uv not in _SW_PTS and p[0] > R.HALF_LEN - 0.30]
if _spl_idx and _nose_body_y:
    _spl_top = max(R.positions[i][1] for i in _spl_idx)
    _gap = min(_nose_body_y) - _spl_top
    # Touching or embedded is fine (this codebase's "overlap rather than gap"
    # idiom); a positive gap is open air and is the defect.
    check(_gap <= 1e-9,
          "splitter meets the bodywork above it (gap %.3f m, body bottom %.3f vs splitter top %.3f)"
          % (_gap, min(_nose_body_y), _spl_top))

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
# T11: was a third hand-copy of `WHEEL_RADIUS * 0.4`. The generator now names
# that quantity, so read it -- a checker carrying its own copy of the number it
# is checking cannot detect the one thing it exists to detect. This guard did
# fire when the width moved, which is the only reason it isn't still wrong.
_hw = R.WHEEL_HALF_WIDTH
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

# --- T23c: the two bright rings ---------------------------------------------
#
# The wheel's whole read in the reference comes from a bronze bead ring and a
# lug ring, and a swatch repaint alone cannot produce either -- they need their
# own annuli, the same argument I1's own comment makes for why a single-swatch
# fan can never show a rim/tire distinction. So the assertion is on the EMITTED
# vertices: each ring exists, on the outer cap only, bound to its own wheel's
# joint so it spins with the tire. A ring bound to chassis joint 0 would sit
# still while the wheel turned, which no still frame can show.
_bead_ok, _lug_ok, _ring_joint_ok, _ring_outer_ok = True, True, True, True
for _wi, (wx, wz) in enumerate(R.wheel_offsets):
    _joint = _wi + 1
    _outer_sign = 1 if wz > 0 else -1
    for _sw, _rout, _rin in ((R.SW_BEAD, R.WHEEL_R_BEAD_O * R.WHEEL_RADIUS,
                              R.WHEEL_R_BEAD * R.WHEEL_RADIUS),
                             (R.SW_LUG, R.WHEEL_R_BEAD * R.WHEEL_RADIUS * R.WHEEL_R_LUG_O,
                              R.WHEEL_R_BEAD * R.WHEEL_RADIUS * R.WHEEL_R_LUG_I)):
        # The |z - wz| filter is not decoration: both wheels on an axle share
        # wx, so a radius test in X-Y alone matches BOTH of them and every count
        # below comes out doubled. The first version of this check did exactly
        # that and failed against correct geometry.
        def _at(r):
            return [(p, j) for uv, j, p in zip(R.uvs, R.joints0, R.positions) if uv == _sw
                    and abs(p[2] - wz) < 0.3
                    and abs(math.hypot(p[0] - wx, p[1] - R.WHEEL_RADIUS) - r) < 1e-9]
        _v, _vi = _at(_rout), _at(_rin)
        _n = 16  # add_wheel()'s call-site `sides`
        if _sw == R.SW_BEAD and (len(_v) != _n or len(_vi) != _n):
            _bead_ok = False
        if _sw == R.SW_LUG and (len(_v) != _n or len(_vi) != _n):
            _lug_ok = False
        if any(j[0] != _joint for _p, j in _v + _vi):
            _ring_joint_ok = False
        # Outer cap only: every vertex must sit on the side of the hub that
        # faces away from the car.
        if any((p[2] - wz) * _outer_sign <= 0 for p, _j in _v + _vi):
            _ring_outer_ok = False
check(_bead_ok, "every wheel's outer cap carries a bead ring (16 vertices on each of its two radii)")
check(_lug_ok, "every wheel's outer cap carries a lug ring (16 vertices on each of its two radii)")
check(_ring_joint_ok, "every bead- and lug-ring vertex is bound to its own wheel's joint, never chassis joint 0")
check(_ring_outer_ok, "no bead- or lug-ring geometry on any wheel's inner (never-seen) cap")
check(R.WHEEL_R_BEAD_O - R.WHEEL_R_BEAD <= 0.06,
      "the bead ring stays a LINE (%.3f of the radius); wider and the tire wears a whitewall"
      % (R.WHEEL_R_BEAD_O - R.WHEEL_R_BEAD))
check(R.WHEEL_R_LUG_O < 1.0 and R.WHEEL_R_LUG_I > R._HUB_R_FRAC,
      "the lug ring sits on the rim face, outside the centre nut (%.2f-%.2f of R_INNER vs nut %.2f)"
      % (R.WHEEL_R_LUG_I, R.WHEEL_R_LUG_O, R._HUB_R_FRAC))
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

# --- T9: wheel triangle winding ---------------------------------------------
# 81-88% of every wheel's triangles were wound backwards relative to the
# normals add_wheel() hand-sets on them. add_wheel()'s own comment says winding
# "only needs to describe valid triangles, not a specific facing -- this
# renderer applies no backface culling anywhere", which is still true today
# (skinned_mesh.cpp:114 confirms it) -- so this was latent, not visible. It is
# fixed anyway: the day culling is turned on, 800 triangles would vanish from
# four wheels and the cause would be extremely hard to find from the symptom.
print("wheel winding")
_wind_bad = 0
for _t in range(0, len(R.indices), 3):
    _i0, _i1, _i2 = R.indices[_t], R.indices[_t + 1], R.indices[_t + 2]
    if R.joints0[_i0][0] == 0:
        continue
    _p0, _p1, _p2 = R.positions[_i0], R.positions[_i1], R.positions[_i2]
    if R._dot(R._cross(R._sub(_p1, _p0), R._sub(_p2, _p0)), R.normals[_i0]) < 0.0:
        _wind_bad += 1
check(_wind_bad == 0,
      "every wheel triangle's winding agrees with its own normal (%d backwards)" % _wind_bad)

# --- T8: the tail panel's UV island ------------------------------------------
print("tail panel UV island")
_ISL = (R.TAIL_UV_U0, R.TAIL_UV_V0, R.TAIL_UV_U1, R.TAIL_UV_V1)

# 1. No swatch sample point may fall inside it. The first attempt at this island
# sat at u 0.820-0.988 / v 0.380-0.620, which contains SW_MIRROR at
# (0.835, 0.5) exactly -- and, worse, the whole u > 0.80 column is painted as
# FULL-HEIGHT swatch bands, so the panel was simply buried. Margin, not just
# non-containment: swatch texels feed mip levels and a neighbour bleeding in
# turns a tire black into whatever the tail panel put next to it.
_SWATCHES = {n: getattr(R, n) for n in dir(R) if n.startswith("SW_")}
_MARGIN = 0.05
_bad = [(n, uv) for n, uv in _SWATCHES.items()
        if _ISL[0] - _MARGIN <= uv[0] <= _ISL[2] + _MARGIN
        and _ISL[1] - _MARGIN <= uv[1] <= _ISL[3] + _MARGIN]
check(not _bad,
      "no SW_* swatch sits within %.2f of the tail island%s"
      % (_MARGIN, "" if not _bad else " -- collides with " + ", ".join(n for n, _ in _bad)))

# 2. The island must actually be used: every tail-cap vertex inside it, and no
# body vertex inside it. Cheap, and it is the assertion that would have caught
# the stale car_rig_data.h -- the generated header only refreshes when
# gen_car_rig.py is RUN, so editing the island and rebuilding C++ alone left
# the mesh pointing at the old rectangle while the livery painted the new one.
_tail_uvs = [R.uvs[i] for i in range(*R.TAIL_CAP_RANGE)]
_inside = lambda uv: _ISL[0] - 1e-6 <= uv[0] <= _ISL[2] + 1e-6 and _ISL[1] - 1e-6 <= uv[1] <= _ISL[3] + 1e-6
check(all(_inside(uv) for uv in _tail_uvs),
      "every tail-cap vertex unwraps into the island (%d/%d)"
      % (sum(1 for uv in _tail_uvs if _inside(uv)), len(_tail_uvs)))
_body_in = sum(1 for i, uv in enumerate(R.uvs)
               if not (R.TAIL_CAP_RANGE[0] <= i < R.TAIL_CAP_RANGE[1]) and _inside(uv))
check(_body_in == 0, "no non-tail vertex samples the island (%d do)" % _body_in)

# 3. livery.cpp paints this rectangle from its own copy of the numbers, in a
# different language, and a silent drift paints the rear panel onto the tire
# swatches. Read the C++ and compare, the same way the glass-U checks compare
# against livery.cpp's own carU replica.
import re as _re
_liv = open(os.path.join(_HERE, "..", "src", "render", "livery.cpp")).read()
_m = _re.search(r"constexpr double TU0 = ([0-9.]+), TU1 = ([0-9.]+);\s*\n\s*constexpr double TV0 = ([0-9.]+), TV1 = ([0-9.]+);", _liv)
check(_m is not None, "livery.cpp still declares the tail island rectangle")
if _m:
    _cpp = tuple(float(g) for g in _m.groups())
    _py = (R.TAIL_UV_U0, R.TAIL_UV_U1, R.TAIL_UV_V0, R.TAIL_UV_V1)
    check(max(abs(a - b) for a, b in zip(_cpp, _py)) < 1e-9,
          "livery.cpp's tail island matches gen_car_rig.py's (cpp %s vs py %s)" % (_cpp, _py))

# T16: the nose island, guarded exactly as the tail's is above. Same three
# questions -- does it collide with a swatch, does the cap actually unwrap into
# it, and does anything else land in it -- because the nose has the same
# failure modes and one extra: the two islands now sit in the SAME reserved
# column, so an overlap between them would silently paint the grille onto the
# rear panel.
print()
print("nose fascia UV island")
_NISL = (R.NOSE_UV_U0, R.NOSE_UV_V0, R.NOSE_UV_U1, R.NOSE_UV_V1)

_nbad = [(n, uv) for n, uv in _SWATCHES.items()
         if _NISL[0] - _MARGIN <= uv[0] <= _NISL[2] + _MARGIN
         and _NISL[1] - _MARGIN <= uv[1] <= _NISL[3] + _MARGIN]
check(not _nbad,
      "no SW_* swatch sits within %.2f of the nose island%s"
      % (_MARGIN, "" if not _nbad else " -- collides with " + ", ".join(n for n, _ in _nbad)))

# The two islands must not overlap each other.
_overlap = not (_NISL[2] < _ISL[0] or _ISL[2] < _NISL[0] or
                _NISL[3] < _ISL[1] or _ISL[3] < _NISL[1])
check(not _overlap,
      "the nose and tail islands do not overlap (nose %s vs tail %s)" % (_NISL, _ISL))

_nose_uvs = [R.uvs[i] for i in range(*R.NOSE_CAP_RANGE)]
_ninside = lambda uv: (_NISL[0] - 1e-6 <= uv[0] <= _NISL[2] + 1e-6
                       and _NISL[1] - 1e-6 <= uv[1] <= _NISL[3] + 1e-6)
check(all(_ninside(uv) for uv in _nose_uvs),
      "every nose-cap vertex unwraps into the island (%d/%d)"
      % (sum(1 for uv in _nose_uvs if _ninside(uv)), len(_nose_uvs)))
_nbody_in = sum(1 for i, uv in enumerate(R.uvs)
                if not (R.NOSE_CAP_RANGE[0] <= i < R.NOSE_CAP_RANGE[1]) and _ninside(uv))
check(_nbody_in == 0, "no non-nose vertex samples the island (%d do)" % _nbody_in)

# And livery.cpp must be painting the same rectangle the mesh samples --
# the tail island's own history is why: the C++ and the generator each held
# their own copy of the numbers and a drift painted the panel onto the tire
# swatches.
_mn = _re.search(r"constexpr double NU0 = ([0-9.]+), NU1 = ([0-9.]+);\s*\n\s*"
                 r"constexpr double NV0 = ([0-9.]+), NV1 = ([0-9.]+);", _liv)
check(_mn is not None, "livery.cpp still declares the nose island rectangle")
if _mn:
    _ncpp = tuple(float(g) for g in _mn.groups())
    _npy = (R.NOSE_UV_U0, R.NOSE_UV_U1, R.NOSE_UV_V0, R.NOSE_UV_V1)
    check(max(abs(a - b) for a, b in zip(_ncpp, _npy)) < 1e-9,
          "livery.cpp's nose island matches gen_car_rig.py's (cpp %s vs py %s)" % (_ncpp, _npy))

# 4. T11: renderer.cpp carries its OWN copy of the wheel radius, as
# `constexpr double kWheelRadius`, with nothing but a trailing comment saying
# it "must match tools/gen_car_rig.py's WHEEL_RADIUS". It feeds
# computeWheelTransforms(), so a drift does not fail to build and does not
# look wrong in a still frame -- the wheels simply sit at the wrong ride
# height and spin at the wrong rate against the ground they are on. This
# round moved WHEEL_RADIUS and would have desynced it.
#
# Same technique as the tail-island check above: read the C++ and compare.
print()
print("cross-file constants")
_rend = open(os.path.join(_HERE, "..", "src", "render", "renderer.cpp")).read()
_mw = _re.search(r"constexpr double kWheelRadius = ([0-9.]+);", _rend)
check(_mw is not None, "renderer.cpp still declares kWheelRadius")
if _mw:
    check(abs(float(_mw.group(1)) - R.WHEEL_RADIUS) < 1e-9,
          "renderer.cpp's kWheelRadius matches gen_car_rig.py's WHEEL_RADIUS (cpp %s vs py %s)"
          % (_mw.group(1), R.WHEEL_RADIUS))

# 5. T13: livery.cpp bakes ambient occlusion into the wheel arches and the
# rocker, and it can only do that by naming where they are in UV space. Those
# five numbers are DERIVED from this generator -- the U spans are the footprint
# of the stations _arch_lip_y() actually carves, and the lip V is RINGV[K_LIP].
# Nothing about that survives a re-authored station table, and the failure is
# silent: the AO simply lands on the wrong part of the car and everything still
# builds and still renders. Recompute and compare.
print()
print("baked AO footprint")
_lip_hf = R.RINGF[R.K_LIP][0]


def _arch_u_span(axle_x):
    us = []
    for (x, half_w, belt_y, y_low, roof_y) in R.CHASSIS_STATIONS:
        y_base_lip = y_low + (_lip_hf / R.SHOULDER) * (belt_y - y_low)
        if R._arch_lip_y(x, axle_x, y_base_lip) > y_base_lip + 1e-9:
            us.append(R.car_u(x))
    return (min(us), max(us)) if us else None


_front = _arch_u_span(R._WHEEL_AXLE_X[0])
_rear = _arch_u_span(R._WHEEL_AXLE_X[1])
check(_front is not None and _rear is not None, "both axles carve an arch into the section")
if _front and _rear:
    _want = {
        "kArchFrontU0": _front[0], "kArchFrontU1": _front[1],
        "kArchRearU0": _rear[0], "kArchRearU1": _rear[1],
        "kArchLipV": R.RINGV[R.K_LIP],
    }
    for _name, _py in _want.items():
        _mm = _re.search(r"\b%s = ([0-9.]+)" % _name, _liv)
        check(_mm is not None, "livery.cpp declares %s" % _name)
        if _mm:
            # 1e-4: these are written to 4 decimal places on purpose -- a
            # texel at 2048 is 4.9e-4 wide, so agreeing to 1e-4 means the AO
            # band starts on the same texel the generator's arch does.
            check(abs(float(_mm.group(1)) - _py) < 1e-4,
                  "livery.cpp's %s matches the generated arch (cpp %s vs py %.4f)"
                  % (_name, _mm.group(1), _py))

    # And the centres those spans imply must land on the ACTUAL AXLES. This is
    # the assertion that would have caught the original defect: livery.cpp
    # painted its arch shadow at carU(+-1.395), inherited from the JS source,
    # and kept painting there after the axles moved to a real Cup car's short
    # front / long rear overhang. The shadow ended up ~0.16 m along the car
    # from the hole it represents, partly on the fender beside it.
    for _label, _axle, _span in (("front", R._WHEEL_AXLE_X[0], _front),
                                 ("rear", R._WHEEL_AXLE_X[1], _rear)):
        _mid = 0.5 * (_span[0] + _span[1])
        _axle_u = R.car_u(_axle)
        # 0.012 is a real tolerance, not a rubber stamp: the arch span is
        # sampled at discrete stations so its midpoint need not land exactly on
        # the axle, but the defect this catches was 0.031 and 0.036 out.
        check(abs(_mid - _axle_u) < 0.012,
              "%s arch span is centred on its axle (span mid %.4f vs axle u %.4f)"
              % (_label, _mid, _axle_u))

print("\nverts %d  tris %d" % (len(R.positions), len(R.indices) // 3))
print("check_car_rig: PASS" if ok else "check_car_rig: FAILURES ABOVE")
sys.exit(0 if ok else 1)
