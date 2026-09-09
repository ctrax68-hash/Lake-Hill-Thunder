#!/usr/bin/env python3
"""Measure the generated car against real Gen-4 Cup proportions.

    python3 tools/car_proportions.py

WHY THIS EXISTS. Every car round in this project has been judged by looking at
a render and deciding whether it "looks right", and that judgement has been
wrong repeatedly -- including three times in a single session, twice on the
same question. `check_car_rig.py` guards the mesh against being BROKEN (folds,
tire intrusion, UV drift); nothing has ever checked whether it is the right
SHAPE. This does.

The numbers below are the loss function for that question. Each one is
measured off `gen_car_rig.py`'s own station table, so there is no rendering,
no camera, and no image processing between the mesh and the number.

TARGET PROVENANCE, stated per row, because it decides how hard to chase one:

  spec  -- published Gen-4 Cup dimensions. Hard targets; a miss is a bug.
  photo -- read off the user-supplied reference profile (a Gen-4 in near-side
           view) using a landmark grid. The car in that photo is rotated
           perhaps 30 degrees off pure profile, so ABSOLUTE lengths from it are
           unreliable. RATIOS taken along one axis are not, and only ratios are
           used here. Tolerances are correspondingly loose.

A row marked `photo` with a wide tolerance is not a weak assertion, it is an
honest one: it says "this proportion is clearly wrong" without pretending to
know it to the millimetre.
"""

import math
import os
import sys

_HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, _HERE)
os.chdir(_HERE)  # gen_car_rig.py writes its output relative to tools/
import gen_car_rig as R  # noqa: E402

rows = []
worst = []


def measure(name, value, target, tol, source, note=""):
    """Record one proportion. `tol` is a fraction of the target."""
    dev = (value - target) / target if target else 0.0
    ok = abs(dev) <= tol
    rows.append((ok, name, value, target, dev, tol, source, note))
    if not ok:
        worst.append((abs(dev), name, value, target, dev, note))


ST = R.CHASSIS_STATIONS
HALF_LEN = R.HALF_LEN
LENGTH = 2.0 * HALF_LEN
AXLE_F, AXLE_R = R._WHEEL_AXLE_X[0], R._WHEEL_AXLE_X[1]
WHEELBASE = AXLE_F - AXLE_R
ROOF = max(st[4] for st in ST)
MAX_HALF_W = max(st[1] for st in ST)


def station(role):
    """The hand-authored keyframe carrying this ROLE.

    By name, never by x_js literal: T6 moved every landmark coordinate in the
    table at once, and the first version of this file addressed them by value
    and simply stopped resolving. Names survive a re-authoring; coordinates do
    not, and a checker that silently measures the wrong station is worse than
    one that fails.
    """
    x = R.station_x(role)
    for st in ST:
        if abs(st[0] - x) < 1e-9:
            return st
    raise SystemExit("car_proportions: role %r resolves to x=%.4f, no station there" % (role, x))


# --- overall box: published Gen-4 Cup dimensions --------------------------
# T7: measured over the real geometry, not 2*HALF_LEN. The published 200 in
# Cup length is taken OVER THE BUMPERS, so comparing it against the loft alone
# was measuring the wrong thing -- and once the tips became domes standing
# proud of the last section ring, 2*HALF_LEN would have quietly under-reported
# by the two cap depths. min/max over every chassis vertex cannot drift.
# Bumper to bumper, using the cap vertex ranges gen_car_rig exposes. NOT
# min/max over every chassis vertex: the front splitter's lip reaches
# HALF_LEN + 0.18 and the spoiler blade HALF_LEN + 0.06, so that measured 5.32
# and would have had me shaving a body that was already the right length. A
# quoted Cup length is over the bumpers; aero appendages are not in it.
_nose_x = [R.positions[i][0] for i in range(*R.NOSE_CAP_RANGE)]
_tail_x = [R.positions[i][0] for i in range(*R.TAIL_CAP_RANGE)]
TRUE_LENGTH = max(_nose_x) - min(_tail_x)
measure("overall length over bumpers (m)", TRUE_LENGTH, 5.08, 0.02, "spec")
measure("overall width (m)", 2.0 * MAX_HALF_W, 1.842, 0.03, "spec",
        "72.5 in mandated body width")
measure("roof height (m)", ROOF, 1.295, 0.03, "spec", "51 in")
measure("wheelbase (m)", WHEELBASE, 2.794, 0.01, "spec", "110 in")
measure("track width (m)", 2.0 * R.TRACK_HALF, 1.537, 0.03, "spec", "60.5 in")
measure("tire diameter (m)", 2.0 * R.WHEEL_RADIUS, 0.72, 0.05, "spec")

# --- where the wheels sit IN the body ------------------------------------
# The single clearest silhouette error the reference photo shows. Measured on
# the grid overlay: front overhang ~90 px, rear ~133 px against a 264 px
# wheelbase. A stock car's rear deck is long and its nose is short; ours is
# symmetric, which is what a generic car shape looks like, not a Cup car.
front_oh = HALF_LEN - AXLE_F
rear_oh = HALF_LEN + AXLE_R
measure("front overhang / length", front_oh / LENGTH, 0.182, 0.12, "photo",
        "90 px of a 505 px car")
measure("rear overhang / length", rear_oh / LENGTH, 0.269, 0.12, "photo",
        "133 px of a 505 px car")
measure("rear overhang / front overhang", rear_oh / max(front_oh, 1e-9), 1.48, 0.15,
        "photo", "THE nose-too-long / tail-too-short test")

# --- greenhouse placement -------------------------------------------------
# Also read off the grid: the cowl sits ~97 px behind the front axle on a
# 264 px wheelbase. A cab-forward greenhouse is the other half of the same
# silhouette error as the overhangs above.
cowl = station("cowl")[0]
roof_lead = station("roof_lead")[0]
roof_trail = station("roof_trail")[0]
deck = station("rear_axle")[0]
measure("cowl behind front axle / wheelbase", (AXLE_F - cowl) / WHEELBASE, 0.367, 0.15,
        "photo", "97 px of 264 px")
measure("flat roof length / wheelbase", (roof_lead - roof_trail) / WHEELBASE, 0.292, 0.20,
        "photo", "77 px of 264 px")

# --- profile heights ------------------------------------------------------
# Vertical readings off the same grid, scaled by the known 1.295 m roof.
nose_top = station("nose")[4]
belt_cabin = station("roof_mid")[2]
deck_top = station("deck_flat")[4]
measure("nose height / roof height", nose_top / ROOF, 0.551, 0.10, "photo",
        "a Cup car's nose is LOW")
measure("beltline / roof height", belt_cabin / ROOF, 0.703, 0.08, "photo")
measure("deck height / roof height", deck_top / ROOF, 0.724, 0.10, "photo")

# --- glass rake -----------------------------------------------------------
ws_rise = station("roof_lead")[4] - station("cowl")[4]
ws_run = station("cowl")[0] - station("roof_lead")[0]
bl_rise = station("roof_trail")[4] - station("deck_start")[4]
bl_run = station("deck_start")[0] - station("roof_trail")[0]
measure("windshield rake (deg from horizontal)", math.degrees(math.atan2(ws_rise, ws_run)),
        34.0, 0.15, "photo")
# Measured off the reference with SEPARATE horizontal and vertical scales: the
# car there is rotated off pure profile so x is foreshortened ~1.17x, and one
# combined scale gives the wrong angle. Windshield 34.1, backlite 20.9 -- the
# backlite is SHALLOWER, which is what makes the deck read long. R2b changed
# these to 32/30 on the opposite belief and made the silhouette worse.
measure("backlite rake (deg from horizontal)", math.degrees(math.atan2(bl_rise, -bl_run)),
        21.0, 0.25, "photo", "SHALLOWER than the windshield -- R2b had this backwards")

# --- report ---------------------------------------------------------------
print("car_proportions -- generated mesh vs real Gen-4 Cup")
print()
print("  %-42s %9s %9s %8s  %s" % ("proportion", "actual", "target", "dev", "src"))
for ok, name, value, target, dev, tol, source, note in rows:
    print("  %s %-40s %9.3f %9.3f %+7.1f%%  %s%s"
          % ("ok  " if ok else "OFF ", name, value, target, dev * 100.0, source,
             ("  -- " + note) if note and not ok else ""))

print()
if not worst:
    print("car_proportions: every measured proportion is within tolerance.")
    sys.exit(0)

worst.sort(reverse=True)
print("WORST DEVIATIONS -- fix from the top:")
for i, (mag, name, value, target, dev, note) in enumerate(worst[:5], 1):
    print("  %d. %-44s %.3f vs %.3f  (%+.1f%%)" % (i, name, value, target, dev * 100.0))
    if note:
        print("     %s" % note)
sys.exit(1)
