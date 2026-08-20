#pragma once

#include "../render/gear_rpm.h"
#include "constants.h"
#include "rng.h"
#include "track.h"

#include <array>
#include <deque>
#include <string>

// Port of the JS car constants / roster / makeCar() (index.html:397-503).

using Color3 = std::array<double, 3>;

// CAR (index.html:398)
struct CarConstants {
    double mass = 1500;

    // L18 (first post-launch feedback pass): "car acceleration sucks".
    // power/maxForce below were a byte-for-byte, never-retuned port of the
    // original JS game's own engine constants (index.html:457) -- shared
    // identically by the player and all 19 AI cars (no player-only throttle
    // ramp or traction-control exists anywhere in this codebase, unlike
    // steering's player/AI split). step_car.cpp's engine-force model:
    // engFRaw = min(maxForce, power/v) * modifiers, further clamped by the
    // traction budget engF = min(engFRaw, muEff*fz.rear) -- so raising these
    // constants can't produce an unrealistic wheelspin-free launch; the
    // clamp is a real physical safety net, not just a test guard.
    //
    // Old values (245000 W / 8200 N) worked out to 219 hp/ton, a=5.10 m/s^2
    // at 20 m/s, a=3.37 m/s^2 at 40 m/s -- and that 40 m/s figure is exactly
    // the power-limited, corner-exit-to-next-corner region on this oval,
    // which is what most likely read as "sucks" rather than 0-60 or top
    // speed in isolation. Real stock-car reference is ~435 hp/ton -- roughly
    // double what this model had.
    //
    // power alone can't fix the corner-exit weakness: below the power/
    // maxForce breakeven speed, engFRaw = min(maxForce, power/v) is governed
    // entirely by the flat maxForce cap, and corner-exit speeds on this oval
    // sit largely in/below that band -- so both constants move together.
    //
    // First attempt raised power 245000 -> 295000 W (+20%) and maxForce
    // 8200 -> 8700 N (+6%), reasoned from the traction budget alone (engF is
    // clamped by muEff*fz.rear, so nothing about the launch itself gets
    // unrealistic). drivability_test's 6-seed x 4-track solo guard caught
    // what the traction-budget arithmetic couldn't see: at those values,
    // Cedar Valley's worst-seed damage jumped 0.000 -> 1.000 (vs. baseline)
    // and Thunder Oval's 0.392 -> 0.934 -- not from any single cause, since
    // isolating power alone, maxForce alone, and combined with L17's looser
    // steering each independently pushed one track or another toward
    // worst-case damage, in a way that flipped almost discontinuously with
    // even a 4% change (a signature of this simulator's already-documented
    // chaos, not a smooth dose-response). Neither of those tracks failed the
    // test's own codified gate (dnfRate stayed under 0.40, earlyDnf stayed
    // 0), but a worst-case trajectory ending in a fully wrecked car is a real
    // player-facing regression even when the assertion doesn't catch it, so
    // I backed off rather than ship on a technicality.
    //
    // Settled on power 245000 -> 265000 W (+8%) and maxForce 8200 -> 8350 N
    // (+2%) -- the largest step that stayed CLEAN (not just passing) across
    // every track's worst-seed damage, combined with L17's steerYawAuthority
    // change:
    //
    //                    old       new
    //     hp/ton         219       237
    //     a @ 20 m/s     5.10      5.20 m/s^2
    //     a @ 40 m/s     3.37      3.70 m/s^2  (+10%, the flagged weak point)
    //     0-60 mph       5.22 s    5.12 s
    //     top speed      177.3     182.5 mph
    //
    // Deliberately NOT matched to real NASCAR's ~435 hp/ton, and smaller than
    // this file's own first attempt above. Two already-open, documented
    // issues explain why a bigger step keeps finding trouble on specific
    // tracks: L12 (cornerSpeed()/targetSpeed() already command entry speeds
    // up to 4.26x the tire's physical limit on some tracks -- faster
    // corner-exit raises how hard/often a car reaches its next
    // over-committed corner) and wall-contact damage (~0.105 dmg/sec at
    // racing speed, ~9.5s cumulative contact is a permanent DNF). This
    // leaves real "sucks" complaint headroom on the table on purpose --
    // revisit upward only after L12 and/or the wall-damage rate are actually
    // fixed, not by re-running this same drivability_test bisection with a
    // different seed set and hoping. torqueCurveMultiplier() (car.cpp) is
    // deliberately untouched -- it's a shape-only lever, tested separately,
    // not the fix for overall pace (see its own comment).
    double power = 265000;
    double maxForce = 8350;
    double brakeForce = 11500;
    double cdA = 0.32 * 2.2;
    double roll = 380;
    double mu = 1.0;
    double dfK = 0.00016;
    double len = 5.08;
    double wid = 2.0;

    // Tire-model upgrade (bicycle model + weight transfer + aero-as-force):
    // additive to the fields above, which stay exactly as they were and
    // still drive the unchanged longitudinal (engine/drag/roll/brake) model.
    // cornerCap()/cornerSpeed()/targetSpeed() below are ALSO unchanged --
    // they remain the AI's forward-looking corner-speed-planning heuristic,
    // verified bit-for-bit against JS by speed_model_test.cpp. These new
    // constants only feed the new per-tick execution physics in
    // step_car.cpp, a separate concern from AI planning.
    double wheelBase = 2.79;      // m, front-to-rear axle distance
    double weightDistF = 0.50;    // static front weight fraction
    double cgHeight = 0.50;       // m, effective CG height (longitudinal transfer)
    double aeroBalanceF = 0.45;   // fraction of aero downforce carried by the front axle
    double cf = 95000.0;          // N/rad, front axle cornering stiffness
    double cr = 105000.0;         // N/rad, rear axle cornering stiffness
    double iz = 2800.0;           // kg*m^2, yaw moment of inertia
    // Reported bug: "one simple touch of button... makes hard turn." Root
    // cause -- this constant, not the input smoothing below it in
    // step_car.cpp (that 0.22-per-tick blend and its unscaled-by-DT timing
    // are both exact, verified ports of index.html's own steerIn code, at
    // the same 50Hz tick rate; not the bug). maxSteerAngle has no JS
    // equivalent at all -- JS's kinematic model maps c.steer straight to
    // yaw rate with no steer-angle/tire-force concept in between -- it was
    // introduced fresh by the bicycle-model tire upgrade and, by that
    // upgrade's own admission (PORT_PROGRESS.md's "Step 1" regression-pass
    // entry), never tuned for feel afterward.
    //
    // H8 cut this 0.5 -> 0.12 and H9 0.12 -> 0.05, each time reasoning from
    // an isolated scenario (a tap; a held corner) and each time making the
    // real problem WORSE. L1 (NT2003/2004 fidelity pass) reverses the
    // direction entirely, to 0.26 rad (~15 degrees) -- the real-world figure,
    // since a NASCAR turns its front wheels roughly 15 degrees to get through
    // a corner. Two things forced the correction:
    //
    // 1. PLAYING the game instead of only unit-testing it. Holding gas and
    //    tapping the steer key gently (120ms taps, 280ms apart) put the car
    //    into the infield and then into a terminal-damage DNF on lap 1, 20th
    //    of 20. Three passes of narrow, passing tests never caught that the
    //    game was simply not drivable.
    // 2. A drivability harness (tests/tire_model_test.cpp's L1 checks, and
    //    scratchpad/drivability.cpp) that drives the real stepCar() with a
    //    look-ahead lane-holding controller on all four tracks and measures
    //    damage. Sweeping full lock across 0.05/0.12/0.26 at three different
    //    cornering stiffnesses, 0.26 won on EVERY track and every stiffness:
    //      lock=0.05: dmg 0.97/1.00/0.77/0.00, 2/3/2/2 laps (two write-offs)
    //      lock=0.26: dmg 0.28/0.40/0.35/0.00, 3/4/2/2 laps (no write-offs)
    //
    // The mechanism, which every previous pass had backwards: a small lock
    // does not make the car gentle, it makes the car UNABLE TO RECOVER. Once
    // an excursion starts, 2.9 degrees of lock cannot generate enough
    // corrective yaw to get back, so the car grinds down the wall and
    // accumulates damage to a write-off. That is exactly the lap-1 DNF above.
    // Tap harshness is a real but SEPARATE problem, and it is fixed at its
    // own source -- the player's digital-input ramp rate in step_car.cpp --
    // rather than by crippling the car's steering authority.
    //
    // Also tested and REJECTED, recorded so it isn't re-attempted: raising
    // the understeer gradient by softening the front axle (cf 95000 ->
    // 75000/53000) to cut the yaw gain. The closed-form case for it is
    // genuinely attractive -- cf/cr are near-identical today, so Kus ~= 0.0008
    // and the characteristic speed sqrt(L/Kus) lands at 60.9 m/s (~136 mph),
    // inside the racing band, meaning yaw gain RISES with speed (8.65 at
    // 30 m/s -> 10.92 at 60) when a real stock car should firm up. But
    // measured on the same harness it made drivability consistently worse
    // (at lock=0.26, dmg 0.28 -> 0.44 at cf=75000 -> 0.53 at cf=53000),
    // because cf also carries the yaw DAMPING term: softening the front axle
    // cut damping ~21% and the car became more oscillatory, which dominates
    // the gain benefit under a real (bang-bang, digital) player input. cf/cr
    // therefore stay exactly as they were.
    double maxSteerAngle = 0.26;  // rad, full-lock front steer angle (~15 deg)

    // L8 (NT2003/2004 fidelity pass): input-curve exponent for the PLAYER's
    // steering -- `angle = maxSteerAngle * sign(steer) * |steer|^gamma`.
    //
    // L1 fixed how MUCH steering the car has. This fixes how FINELY the
    // player can ask for it, which is what "the car overreacts" actually
    // meant and what four passes missed by moving magnitudes around instead.
    // From this model's own steady-state relation r = v*delta/(wheelBase +
    // Kus*v^2), the steer angle each corner really needs at racing speed:
    //
    //     Big Sable (R=240m, 50 m/s): 1.11 deg   (7.5% of full lock)
    //     Cedar     (R=160m, 45 m/s): 1.54 deg  (10.4%)
    //     Thunder   (R=120m, 45 m/s): 2.06 deg  (13.8%)
    //     Milltown  (R=100m, 45 m/s): 2.47 deg  (16.6%)  <- tightest in game
    //
    // What a key press commanded LINEARLY at 45 m/s (lock 0.26, ramp 0.12):
    //
    //     20ms (one tick):  1.79 deg -> 138m radius
    //     40ms:             3.36 deg ->  74m
    //     160ms ("a tap"):  9.54 deg ->  26m radius -- a spin, not a corner
    //
    // A SINGLE 20ms tick -- the shortest press possible -- already commanded
    // 72% of the steering the tightest corner in the game needs, and by 40ms
    // it commanded more than any corner needs. The entire usable range was
    // the first ~17% of input travel; past that everything was oversteer.
    // That held at every maxSteerAngle ever tried (0.5/0.12/0.05/0.26), which
    // is exactly why none of them fixed it: it is a resolution problem, not a
    // magnitude one.
    //
    // A digital key has no analogue travel, so one LINEAR mapping cannot
    // serve both fine control near centre and full authority for catching a
    // slide. A curve serves both: at 2.5 a 160ms tap commands 4.89 deg (51m
    // radius -- a correction, not a spin) while a HELD key still reaches the
    // full 15 deg lock, leaving recovery authority untouched. A speed-based
    // cap was measured as the alternative and rejected: capping the top of
    // the range is precisely what costs recovery.
    //
    // Verified NOT to make wrecking worse, which is the thing that matters:
    // across 6 RNG seeds x 4 tracks the player's DNF rate is identical at
    // gamma 1.0 and 2.5 (15/24 both). An earlier single-seed run suggested
    // otherwise and was noise -- see tests/drivability_test.cpp's header.
    double steerCurveGamma = 2.5; // player-only; see step_car.cpp's use site

    // L15 (NT2003/2004 fidelity pass): maximum steady-state yaw rate, rad/s,
    // that FULL player input is allowed to command -- at any speed.
    //
    // Reported symptom: "car spins wildly but survives". L8's input curve
    // softened the centre of the travel, which does nothing at all for a
    // player who simply HOLDS the turn button, and holding it is what spins
    // the car. The arithmetic, from the same steady-state relation used
    // throughout: at 45 m/s the tightest corner in the game needs 2.47 deg of
    // steer, while full lock is 15 deg -- roughly 6x more than the track ever
    // asks for, commanding a 16m turning radius at 100mph. That is not a
    // hard turn, it is a spin, and no amount of curve shaping prevents it.
    //
    // The cap is expressed as yaw authority rather than as an angle because
    // the angle that means "a spin" changes with speed: delta_max =
    // steerYawAuthority * (wheelBase + Kus*v^2) / v, clamped to
    // maxSteerAngle. Inverting the steady-state relation, that makes full
    // input command exactly this yaw rate at every speed, so the car answers
    // the controls consistently instead of turning into a hair trigger as it
    // gains speed. At 0.8 rad/s that works out to ~4.4 deg of lock at 45 m/s
    // (still comfortably more than the 2.47 the tightest corner needs, and a
    // 56m radius rather than 16m), ~7.1 deg at 20 m/s, and the full 15 deg
    // below ~10 m/s -- so pit-lane manoeuvring and spin recovery keep every
    // degree of steering they have ever had.
    //
    // I rejected a speed-sensitive cap earlier in this work, on the grounds
    // that it "cost recovery authority and produced DNFs at every setting".
    // That verdict came from drivability_test's bang-bang autopilot, which
    // was later proven to be measuring its own incompetence rather than the
    // car (63% DNF against the game's own AI managing 17% on the identical
    // car, slot, traffic and seeds). The measurement was invalid, so the
    // rejection does not stand.
    //
    // L17 (first post-launch feedback pass): "steering is a tad stiff",
    // reported once this build was actually live (L16) instead of stuck on a
    // six-day-stale deploy. Not a request to undo the anti-spin fix above --
    // a mild overshoot in that same direction. steerCurveGamma (below) stays
    // untouched; it governs centre-feel resolution, which isn't what was
    // reported. steerYawAuthority is the newer, single-purpose, exactly-once
    // -tuned lever, and the right one to isolate per this file's own H8/H9
    // lesson about moving one variable at a time. Raised 0.8 -> 0.95:
    //
    //     10 m/s: 13.13 deg -> 14.90 deg (full mechanical lock)
    //     45 m/s: 4.39 deg  -> 5.22 deg  (~19% more lock at racing speed)
    //     60 m/s: 4.20 deg  -> 4.99 deg
    //
    // Milltown's tightest corner still only needs 2.47 deg at 45 m/s, so this
    // doesn't touch that margin. Held-key-to-corner ratio moves 1.77x ->
    // 2.10x, still well inside tire_model_test.cpp's 3.0x "not a spin" bound
    // (headroom extends to ~1.36 rad/s). If still stiff after a re-test, the
    // next step is 1.0-1.1 rad/s -- verify this step first, don't pre-move it.
    //
    // Noted while tuning L18 alongside this: solo-driver drivability_test
    // showed this change is NOT independent of engine-force tuning -- the
    // same power/maxForce values produced different worst-seed wall-contact
    // outcomes depending on whether steerYawAuthority was 0.8 or 0.95, on a
    // chaotic (not smooth) basis. Re-run drivability_test after touching
    // EITHER this or power/maxForce again, even if the other looks untouched.
    //
    // N1b (0.95 -> 0.55): L17 above moved this the WRONG WAY, and this entry
    // reverses it past its original value. Report: "sliding right, happens
    // with cars around or without them around". *Sliding*, and reproducible
    // SOLO -- which finally made it measurable.
    //
    // Driving the way a human actually does -- HOLDING the turn button through
    // the corner, not modulating it -- the sim's own per-axle wear counters
    // say plainly what is happening:
    //
    //                  wearFront  wearRear  ratio   yaw got/asked  corner v
    //     player          0.261     0.014   18.9x      40-53%      16.1 m/s
    //     AI              0.066     0.054    1.2x      54-71%      44   m/s
    //
    // The player's fronts slide NINETEEN times more than the rears. That is
    // not a steering-response problem, it is a saturated front axle: the car
    // plows, refuses to rotate, washes out to the outside ("sliding right" on
    // a left oval) and scrubs itself from 40 m/s down to 16.
    //
    // Cause: a held button asks for far more yaw than any corner here needs.
    // Milltown needs 0.40 rad/s, Thunder Oval 0.33, Cedar Valley 0.26, Big
    // Sable 0.32 -- against 0.95 on tap. Demand triple the grip the front
    // tires have and they simply let go. Crucially this is the SAME defect as
    // "steering is a tad stiff" from the L17 round: a saturated tire stops
    // responding to more steering, so the car feels unresponsive AND pushes
    // wide, at the same time and for the same reason. I read that report as
    // "not enough steering" and raised the cap. That was backwards.
    //
    // Why the earlier sweeps missed it: I swept this constant 0.95 -> 1.5 ->
    // 2.5 -> 99 (cap effectively off), saw no change, and concluded the cap
    // was irrelevant. I never swept it DOWNWARD, and every probe in that pass
    // used a MODULATING controller rather than a held button. Two mistakes,
    // both pointing away from the one constant that mattered.
    //
    // 0.55 is measured, not guessed. Held-button behaviour across all four
    // tracks, with the speed blend in step_car.cpp:
    //
    //     cap    front:rear wear   corner speed   drivability
    //     0.95        18.9x           16.1 m/s      0% DNF
    //     0.60         9.0x           20.9          0% DNF
    //     0.55         4.0x           23.8          0% DNF
    //     0.50          --             --          75% DNF  <- cliff
    //
    // 0.50 collapses drivability_test to 75% DNF: that is the recovery-
    // authority cliff L15 warned about in the abstract, now located exactly.
    // 0.55 sits one step above it and still leaves 1.4x margin over the
    // tightest corner's demand. Do not go below 0.55 without re-running that
    // sweep -- the failure is a cliff, not a slope.
    double steerYawAuthority = 0.55; // rad/s at full lock; player-only
    double brakeBiasFront = 0.62; // fraction of brake force applied at the front axle

    // Regression-pass fix: the AI's steerIn formulas (step_car.cpp) were
    // written against the old model, where c.steer mapped directly and
    // instantaneously to yaw rate (no persistent state). The bicycle model's
    // `r` has real inertia, so a target yaw rate now takes several ticks to
    // develop -- confirmed via the regression pass to cause wall contact
    // during ordinary pace-lap/race driving, not just chaotic starts, since
    // the AI had no way to tell it wasn't turning as fast as intended. This
    // gain scales a yaw-rate-error feedback term (desired minus actual `r`,
    // see step_car.cpp's steerIn sites) added on top of each branch's
    // existing feedforward steerIn, so the AI compensates for the lag
    // instead of assuming zero-latency turning.
    double yawCorrGain = 0.35;    // dimensionless per (rad/s) of yaw-rate error

    // Regression-pass fix: the vy/r bicycle-model integration diverges
    // numerically under this sim's fixed 0.02s explicit-Euler step at
    // highway speed with steerIn saturated at full lock for several
    // consecutive ticks (a stiff coupled-oscillator stability failure --
    // Cf/Cr/Iz/mass/v set the system's natural frequency, and the fixed
    // step violates the discretization's stability margin in that specific
    // regime even though the same equations are stable elsewhere). Fixed by
    // subdividing each tick into this many substeps inside
    // integrateYawDynamics() (car.cpp), recomputing slip angles/forces each
    // substep rather than integrating against one frozen force -- the
    // instability is driven by the tight alpha<->vy/r feedback loop itself,
    // so freezing forces would remove exactly the self-correction raising
    // the sampling rate is meant to restore. A yaw-damping term was tried
    // instead and rejected: it suppressed the divergence but crippled
    // legitimate cornering response fleet-wide.
    int yawSubsteps = 4;

    // Drivetrain upgrade: gearRpm() (src/render/gear_rpm.h) was previously
    // cosmetic-only (HUD readout + engine-audio pitch, src/audio/mixer.cpp).
    // Reused verbatim here (same gear breakpoints, no new gear ratios
    // invented) to shape the accel curve -- see torqueCurveMultiplier() --
    // and to time a brief power-interruption dip around each gear change.
    double shiftDipDur = 0.2;  // s, how long a gear change reduces engine force
    double shiftDipMag = 0.6;  // multiplier applied to engFRaw while shiftCd > 0

    // Suspension upgrade: axleLoads() computes weight transfer
    // instantaneously every tick (no spring/damper dynamics). suspRate is a
    // first-order lag rate (1/s) applied to its output in step_car.cpp --
    // alpha = 1-exp(-suspRate*DT) gives ~100ms settling time, a stiff
    // race-car spring/damper feel, not a soft road-car one. Steady-state Fz
    // is unchanged; only the transient response smooths out, so this is
    // deliberately NOT folded into axleLoads() itself (preserves its
    // existing passing unit tests, which assert on its instantaneous value).
    double suspRate = 10.0;

    // P2 (NT2003 engine-feel plan, fuel as real mass): c.fuel is a [0,1]
    // fraction of tank remaining (car.h's own Car::fuel comment). step_car.cpp
    // builds a per-tick CarConstants copy each tick with mass/weightDistF
    // adjusted by these two constants, then uses THAT copy everywhere axle
    // loads, longitudinal acceleration and yaw dynamics read CAR -- no new
    // machinery, mass just stops being a constant.
    //
    // weightDistF shifts toward the FRONT (added, not subtracted) as the
    // tank fills -- verified empirically (a scratch closed-loop probe, same
    // technique as P1's own driver-feedback test) against the naive
    // "fuel cell is rear-mounted, so shift weight rearward" assumption
    // first, which turned out to produce the opposite of the intended feel.
    // integrateYawDynamics()'s Kus (understeer-gradient) formula pairs aR
    // (CG-to-rear distance, grows with weightDistF) with the FIXED front
    // stiffness cf, and aF with the fixed rear stiffness cr -- since cf/cr
    // don't scale with load in this linear-tire model, adding weight onto an
    // axle raises that SAME axle's own understeer contribution (it has more
    // weight to turn but no more stiffness to do it with), not the other
    // axle's. A rearward shift therefore measurably LOOSENED the car at
    // full tank in the probe (more rear friction-ellipse saturation, less
    // front) -- the reverse of "tight full tank, frees up as it burns" that
    // real stock-car fuel loads are known for. Shifting toward the front
    // instead reproduces the intended direction (see PORT_PROGRESS.md for
    // the actual probe numbers across four steer-feedback targets).
    double fuelMass = 120.0;        // kg, mass added by a completely full tank
    double fuelWeightShiftF = 0.02; // weightDistF shift (toward front) at a completely full tank

    // P4 (NT2003 engine-feel plan, pit adjustments): how far the player's
    // pit-adjustment panel (src/ui/pit_setup.h) can move each of
    // Car::setupWedge/setupTrackBar (each in [-1, 1]) at full deflection.
    // wedgeWeightDistRange reuses the exact same weightDistF lever P2's
    // fuelWeightShiftF already verified the sign of; trackBarStiffnessRange
    // moves ONLY the rear axle's cornering stiffness (`cr`), not `cf` -- a
    // real track bar is a rear-suspension component -- verified via the
    // same closed-loop probe technique as P2's fuel-shift discovery:
    // increasing cr monotonically reduces rear friction-limit hits across
    // four steer-feedback targets (see PORT_PROGRESS.md's P4 entry for the
    // actual numbers), i.e. genuinely tightens the car, matching real
    // "more track bar = tighter" setup convention.
    double wedgeWeightDistRange = 0.03;
    double trackBarStiffnessRange = 0.15;
};
inline const CarConstants CAR{};

double cornerCap(double mu, double bank); // defined in car.cpp (index.html:404)

// Tire-model upgrade: real per-axle vertical load, from static weight
// distribution + longitudinal weight transfer (from acceleration `a`) + aero
// downforce (from speed `v`, force-ized from the same `dfK` constant that
// used to be added straight into a scalar mu). `aeroEfficiency` carries over
// the old model's dirty-air/damage aero degradation (previously a multiplier
// on the mu-additive term only, not on base tire mu -- same causal effect,
// now expressed as reduced downforce/Fz instead). Clamped to a small
// positive floor so a friction-ellipse division never sees a zero/negative
// load.
struct AxleLoads { double front, rear; };
AxleLoads axleLoads(const CarConstants& c, double v, double a, double aeroEfficiency = 1.0);

// Tire-model upgrade: bicycle-model slip angles from body-frame lateral
// velocity `vy`, yaw rate `r`, forward speed `v` (caller-supplied, already
// floor-clamped away from zero), and front steer angle `steerAngle` (rad).
struct SlipAngles { double front, rear; };
SlipAngles slipAngles(const CarConstants& c, double vy, double r, double v, double steerAngle);

// Tire-model upgrade: one axle's lateral tire force -- linear cornering-
// stiffness region (`-stiffness * slipAngle`), clamped by a friction ellipse
// combining `mu*fz` with `fxFrac` (how much of that axle's longitudinal grip
// is already spent, in [-1,1]).
double axleLateralForce(double stiffness, double slipAngle, double mu, double fz, double fxFrac);

// Regression-pass fix (see CarConstants::yawSubsteps): integrates the
// bicycle-model vy/r/hdg state over one outer tick of length `dt`, split
// into `substeps` inner steps that each recompute slip angles and axle
// forces from the just-updated vy/r (not one frozen force for the whole
// tick) -- restores the discretization's stability margin at high speed
// under sustained full-lock steering. `fz`/`muEff`/`steerAngle`/
// `fxFracFront`/`fxFracRear` don't depend on vy/r and are computed once by
// the caller, outside this function, exactly as before this fix.
// `vDyn`/`vSafe` are also caller-supplied (both derived from `c.v`, which
// doesn't change across substeps within one tick).
// P1 (NT2003 engine-feel plan, the loose/tight axis): slipMagAvg used to be
// the ONLY signal wear() derived from -- |alpha.front|+|alpha.rear| summed
// together and averaged, throwing away which axle actually did the
// sliding. slipFrontAvg/slipRearAvg keep that same per-substep time-
// weighted-average convention but per axle, so step_car.cpp can grow
// Car::wearFront/wearRear independently instead of one shared scalar.
// slipMagAvg (= slipFrontAvg+slipRearAvg) and pastLimitAny (=
// pastLimitFront||pastLimitRear) are kept alongside the new per-axle
// fields rather than removed -- H4's slipFx tire-smoke signal and this
// file's own substep-convergence tests want the combined view, and
// recomputing it at every call site would just be the same sum done twice.
struct YawIntegrationResult {
    double vy, r;       // updated state, replaces c.vy/c.r
    double hdgDelta;      // sum of r_i*dtSub across substeps -- add to c.hdg
    double slipMagAvg;    // time-weighted average of |alpha.front|+|alpha.rear|
    double slipFrontAvg;  // time-weighted average of |alpha.front| alone
    double slipRearAvg;   // time-weighted average of |alpha.rear| alone
    bool pastLimitAny;    // true if any substep's force hit the friction ellipse (either axle)
    bool pastLimitFront;  // true if any substep's FRONT force hit the friction ellipse
    bool pastLimitRear;   // true if any substep's REAR force hit the friction ellipse
};
// P1: muEffFront/muEffRear replace the single shared muEff -- each axle's
// own current grip (CAR.mu degraded by that axle's own wear, see
// step_car.cpp) now genuinely bounds only that axle's friction ellipse,
// which is what lets an asymmetrically-worn car actually understeer/
// oversteer instead of just going uniformly slower everywhere.
YawIntegrationResult integrateYawDynamics(const CarConstants& c, double vy0, double r0, double vDyn,
                                           double vSafe, double steerAngle, const AxleLoads& fz,
                                           double muEffFront, double muEffRear, double fxFracFront,
                                           double fxFracRear, double dt, int substeps);

// Drivetrain upgrade: shapes engine force within gearRpm()'s reused
// breakpoint table -- peaks at 1.0 in the middle of each gear's rpm band,
// tapers down toward both edges (mimicking real torque falling off just
// after a shift and again near redline before the next one). The plateau
// deliberately covers most of the band so the band-averaged multiplier stays
// close to 1.0, preserving CarConstants::maxForce's existing tuning -- this
// reshapes the accel curve, it isn't meant to change overall pace.
double torqueCurveMultiplier(const GearRpm& g);

// Suspension upgrade: first-order lag (exponential smoothing) toward
// `target`, used to smooth axleLoads()'s instantaneous per-tick output into
// a value with spring/damper-like settling time instead of a discontinuous
// jump. Steady state (current == target held) is unaffected -- only the
// transient response changes.
double suspensionLag(double current, double target, double rate, double dt);

// CAR_PALETTE (index.html:413-423)
namespace CarPalette {
inline constexpr Color3 Red{0.7529, 0.0000, 0.0000};
inline constexpr Color3 Blue{0.0000, 0.2196, 0.9412};
inline constexpr Color3 Yellow{0.9686, 0.8314, 0.0000};
inline constexpr Color3 Black{0.1200, 0.1200, 0.1300};
inline constexpr Color3 White{0.9500, 0.9500, 0.9600};
inline constexpr Color3 Silver{0.7843, 0.7843, 0.7843};
inline constexpr Color3 Orange{1.0000, 0.4784, 0.0000};
inline constexpr Color3 Green{0.0392, 0.5608, 0.0000};
inline constexpr Color3 Purple{0.3529, 0.1725, 0.6275};
} // namespace CarPalette

// Per-driver authored livery (index.html:424-430). Rendering-only (consumed
// by paintLivery() in the JS original) but kept alongside the roster data
// since it's part of each ROSTER entry -- not used by Phase 1 physics/AI.
struct LiveryScheme {
    int stripe;
    int mask;
    int sponsor;
    Color3 acc2;
};

struct RosterEntry {
    std::string name;
    int num;
    Color3 col;
    LiveryScheme scheme;
};

// ROSTER (index.html:431-451): 19 AI drivers. FIELD = ROSTER.size() + 1 (player).
extern const std::array<RosterEntry, 19> ROSTER;
inline constexpr int FIELD = 19 + 1;

// {t, prog} rolling sample (index.html:1092-1093), Phase 4g's leaderboard
// live-gap calculation only (see gap_time.h's gapTimeAt()) -- never read by
// stepCar()'s driving/physics logic, confirmed by grepping every use site.
struct ProgSample {
    double t, prog;
};

// Car (the JS object shape built by makeCar(), index.html:453-503).
// replayHist/histTick are intentionally NOT ported here -- they're a
// replay-camera feasibility spike in the JS original (index.html:1096-
// 1108, "build + measure the cost... not wired to any playback UI yet"),
// never read by anything this port has ported so far. progHist WAS ported
// (Phase 4g) once the leaderboard's live-gap feature needed it -- see this
// file's own earlier history: it was originally deferred here alongside
// replayHist/histTick until "Phase 2+ needs them for parity with the live
// HUD," which Phase 4g's leaderboard now does.
struct Car {
    bool isPlayer = false;
    int idx = 0;
    std::string name;
    int num = 0;
    const LiveryScheme* scheme = nullptr; // null for the player, matching JS
    Color3 col{};

    double x = 0, y = 0, hdg = 0, vdir = 0, v = 0;
    double s = 0, lat = 0;
    int lap = -1;
    double prog = 0;
    bool done = false;
    double finishT = 0;
    std::deque<ProgSample> progHist; // see ProgSample's own comment above

    double steer = 0, thr = 0, brk = 0;

    // P1 (NT2003 engine-feel plan, the loose/tight axis): wear is now a
    // DERIVED value, `max(wearFront, wearRear)`, recomputed every tick right
    // after step_car.cpp updates the two real per-axle accumulators below --
    // every existing consumer (AI corner-speed planning's cornerSpeed()/
    // targetSpeed(), race.cpp's pit-strategy thresholds, status_bars.cpp's
    // CAR/FUEL-adjacent bars) wants "how worn is this car overall," and none
    // of them need to change now that there's a real axle split underneath.
    // wearFront/wearRear are the new authoritative state: the actual per-
    // axle grip loss step_car.cpp's bicycle-model friction ellipse now reads
    // (see integrateYawDynamics()'s muEffFront/muEffRear), grown from that
    // SAME axle's own slip (YawIntegrationResult::slipFrontAvg/slipRearAvg)
    // instead of a shared combined signal -- this is what makes a car that's
    // pushed into understeer wear its fronts specifically, tightening it
    // further, and a car driven loose wear its rears, loosening it further.
    double wear = 0, wearFront = 0, wearRear = 0, draftF = 0;
    bool dirty = false;

    // c.hitFx (index.html:1067,1227,4238): impact-severity accumulator,
    // decayed by the renderer/audio layer, not stepCar()/collide()
    // themselves (index.html:3150's `c.hitFx -= dt*2.5` lives in the
    // particle-emission code, index.html:1457-1458's audioTick() reads it
    // only to detect a rising edge past its own last-seen value). Cosmetic-
    // only, same category as slipFx (never read by driving/AI logic) --
    // unlike dmgCd/blown/spinRollCd above, this was originally left
    // unported (see step_car.cpp/race.cpp's own "intentionally not ported"
    // comments at its two accumulation sites), until Phase 6b's audio port
    // needed a real trigger signal for impact-thump/blowout-bang sounds.
    double hitFx = 0;

    // c.slipFx (index.html:1134): tire-smoke intensity, same
    // renderer-decayed/cosmetic-only category as hitFx above (JS's own
    // emitFX() decays both at index.html:3330/3341). JS sets this as a
    // binary 1 exactly when its old cornerCap() model saturated -- this
    // port's real bicycle-tire model (car.cpp's integrateYawDynamics())
    // computes actual per-substep slip magnitude instead of one binary
    // cap, so H4 (NT2003 engine-feel plan) boosts this continuously from
    // that (see step_car.cpp), with the friction-ellipse limit
    // (YawIntegrationResult::pastLimitAny) still guaranteeing full
    // intensity at the exact moment JS's old trigger would have fired.
    double slipFx = 0;

    double skill = 0, aggr = 0;
    double grooveBias = 0;

    int passSide = 0;
    double passT = 0;

    double lapStartT = 0, lastLapT = 0, bestLapT = 0;

    double pitch = 0;

    double spinT = 0;
    int spinDir = 1;
    double spinCd = 0;
    // spinRollCd/dmgCd/blown are dynamically added by JS (tick()/collide())
    // rather than being in makeCar()'s literal object -- e.g. `c.dmgCd||0`
    // reads as 0 until first set. They ARE physics-relevant (gate damage
    // timing and tire-blowout speed) so get real, zero-defaulted fields here
    // rather than being treated as cosmetic/HUD-only like hitFx/slipFx.
    double spinRollCd = 0;
    double dmgCd = 0;
    bool blown = false;

    // Tire-model-upgrade regression-pass fix, not a JS port field: gates the
    // wall-clamp's "fresh impact" response (see step_car.cpp) so a car
    // continuously embedded against the wall only gets its vy/r wiped once
    // per contact, not every single tick -- deliberately separate from
    // dmgCd, which stays suppressed during yellow flag (this must not be).
    double wallCd = 0;

    int pit = 0;
    double pitT = 0;
    bool pitReq = false;
    // Drive-through-penalty pending flag (index.html:698, 758): dynamically
    // added by JS's race-control penalty logic (not yet ported -- Phase 1g
    // territory), but the pit branch itself reads/clears it, so it's a real
    // field here now rather than deferred.
    bool dtPending = false;

    double fuel = 1, dmg = 0;

    // P3 (NT2003 engine-feel plan, damage that pulls): a persistent, SIGNED
    // handling bias from bent bodywork, in roughly [-1, 1] -- replaces JS's
    // fixed-direction "bent bodywork pulls gently toward the wall" nudge
    // (index.html:986, `steerIn += c.dmg*0.02`, applied only to the player
    // and always the same sign regardless of which side actually got hit)
    // with a real accumulated value set AT the moment of each impact
    // (step_car.cpp's wall-clamp site, race.cpp's collide()), signed by
    // which side of the CAR (not the world) the hit came from, so it
    // survives a heading change the same way a real bent fender would.
    // step_car.cpp's shared physics tail adds this into every branch's
    // steerIn (not just the player's) -- an AI car visibly fighting a
    // one-sided pull is exactly the "you can still drive it, but you're
    // fighting it" state the plan describes -- and also shaves front-axle
    // grip proportional to |dmgPull|, alongside wearFront's own P1 penalty.
    // Nudged back toward zero (not reset) on a pit repair, matching dmg's
    // own partial-repair semantics.
    double dmgPull = 0;

    // P4 (NT2003 engine-feel plan, pit adjustments): persistent, PLAYER-ONLY
    // chassis-tuning knobs, in [-1, 1], set via the interactive pit-
    // adjustment panel (src/ui/pit_setup.h) during a stop. Unlike wear/
    // fuel/dmg, these do NOT reset at pit service -- a real wedge/track-bar
    // change stays until the player dials it back, not until the next tank
    // of fuel. AI cars never touch these (stay at the neutral default 0 for
    // their whole race -- they have no interactive panel to set them with).
    // step_car.cpp's per-tick carEff copy (P2's own mechanism) applies
    // setupWedge to weightDistF (same lever P2 already verified -- positive
    // = toward front = tighter, per CarConstants::fuelWeightShiftF's own
    // comment on why "toward front" is the verified-correct sign in this
    // bicycle model) and setupTrackBar to the rear axle's own cornering
    // stiffness (CarConstants::cr) -- a genuinely different lever from
    // weightDistF (a track bar is a rear-suspension component in a real
    // stock car, so only cr moves, not cf), verified via the same
    // closed-loop probe technique as P2/P3: increasing cr measurably
    // tightens the car (fewer rear friction-limit hits at a fixed
    // aggressive steer-feedback target, monotonic across four rTarget
    // values), matching real "more track bar = tighter" setup convention.
    double setupWedge = 0, setupTrackBar = 0;

    bool out = false;

    int cautionSlot = -1;
    int gridSlot = -1;
    double gridLane = 0;

    // Set later, in gridStart() (Phase 1g) -- not by makeCar() itself.
    int gridAhead = -1;
    double px = 0, py = 0, phdg = 0, ps = 0, plat = 0;

    // Tire-model upgrade: real integrated bicycle-model dynamic state. `vy`
    // (lateral velocity, car-body frame) and `r` (yaw rate) replace the old
    // model's instantaneous-recompute-every-frame `yaw` local -- they now
    // carry real inertia between ticks. `fzFront`/`fzRear`/`slipRatio` are
    // read-only outputs (never fed back into the physics themselves) that
    // Step 3's wheel/suspension animation consumes.
    double vy = 0, r = 0;
    double fzFront = 0, fzRear = 0;
    double slipRatio = 0; // driven (rear) axle longitudinal slip fraction, [-1,1]

    // Regression-pass fix: last tick's longitudinal acceleration, reused to
    // estimate this tick's axle loads *before* engine force is finalized (see
    // step_car.cpp's traction-budget comment) -- breaks what would otherwise
    // be a circular dependency between engine force and weight transfer.
    double aPrev = 0;

    // Drivetrain upgrade: tracks the last tick's gearRpm(c.v).gear so
    // step_car.cpp can detect a gear change and apply a brief force-reduction
    // dip (CarConstants::shiftDipDur/shiftDipMag). Defaults to 1, not 0 --
    // gearRpm(0).gear is already 1, so a real "unset" sentinel of 0 would
    // spuriously read as a gear change (and trigger a dip) on every car's
    // very first tick.
    int prevGear = 1;
    double shiftCd = 0; // s remaining in the current shift's force-reduction dip

    // Suspension upgrade: axleLoads()'s per-axle Fz output, lagged (see
    // suspensionLag()) to give it spring/damper-like settling time instead of
    // jumping discontinuously every tick. Zero-defaulted like fzFront/fzRear
    // above -- first real value arrives on the first stepCar() tick.
    double fzFrontLag = 0, fzRearLag = 0;
};

// makeCar() (index.html:453-503). `track` supplies TRACK.pointAt(0) for the
// initial spawn position; `rng` is the shared gameplay RNG stream (seed
// 12345) -- must be called exactly 3 times per AI car (skill, aggr,
// grooveBias), zero times for the player, in that order, to stay
// bit-for-bit consistent with the JS original's call sequence.
Car makeCar(bool isPlayer, int idx, const Track& track, Mulberry32& rng);

// pitStallS() (index.html:682-685): each car's assigned pit-stall center on
// the frontstretch.
double pitStallS(const Track& track, int idx);

// AI target-speed model (index.html:629-649). Takes the active Track
// explicitly rather than closing over a single global instance like the JS
// original does -- a direct parameter-passing adaptation, not a behavior
// change; the math and iteration order are unchanged.
double cornerSpeed(const Track& track, double R, double s, double wear);
double targetSpeed(const Track& track, const Car& car);
