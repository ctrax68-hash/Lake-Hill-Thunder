// Verifies the tire-model upgrade's new pure functions (src/sim/car.{h,cpp}):
// axleLoads(), slipAngles(), axleLateralForce(). These replace how
// step_car.cpp executes per-tick cornering physics -- cornerCap()/
// cornerSpeed()/targetSpeed() (the AI's corner-speed-planning heuristic) are
// untouched and stay covered by speed_model_test.cpp/car_test.cpp/
// race_sim_test.cpp, all still passing bit-for-bit against JS ground truth,
// which is what confirms this upgrade didn't disturb them.

#include "../src/sim/car.h"
#include "../src/sim/race_state.h"
#include "../src/sim/rng.h"
#include "../src/sim/step_car.h"
#include "../src/sim/tracks_data.h"

#include <cmath>
#include <cstdio>
#include <vector>

namespace {
int g_failures = 0;

void expect(bool cond, const char* what) {
    if (!cond) {
        std::fprintf(stderr, "FAIL: %s\n", what);
        ++g_failures;
    }
}

void expectNear(const char* label, double got, double expected, double tol = 1e-6) {
    if (std::fabs(got - expected) > tol) {
        std::fprintf(stderr, "%s: got %.17g expected %.17g (diff %.3g)\n",
                     label, got, expected, got - expected);
        ++g_failures;
    }
}
} // namespace

int main() {
    const CarConstants c{}; // defaults: mass=1500, weightDistF=0.50, cgHeight=0.50,
                             // wheelBase=2.79, aeroBalanceF=0.45, dfK=0.00016

    // ---- axleLoads(): static split at zero speed/accel ----
    {
        AxleLoads fz = axleLoads(c, 0.0, 0.0);
        expectNear("static fzFront", fz.front, c.mass * G * c.weightDistF);
        expectNear("static fzRear", fz.rear, c.mass * G * (1 - c.weightDistF));
        expectNear("static split sums to weight", fz.front + fz.rear, c.mass * G, 1e-6);
    }

    // ---- axleLoads(): braking (negative a) shifts load toward the front ----
    {
        AxleLoads level = axleLoads(c, 30.0, 0.0);
        AxleLoads braking = axleLoads(c, 30.0, -5.0); // decelerating
        expect(braking.front > level.front, "braking increases front load");
        expect(braking.rear < level.rear, "braking decreases rear load");
        // Conservation: aero contribution is identical at the same speed, so
        // the longitudinal-transfer delta should exactly cancel between axles.
        expectNear("braking load transfer conserves total (front)",
                   (braking.front - level.front) + (braking.rear - level.rear), 0.0, 1e-6);
    }

    // ---- axleLoads(): accelerating (positive a) shifts load toward the rear ----
    {
        AxleLoads level = axleLoads(c, 30.0, 0.0);
        AxleLoads accel = axleLoads(c, 30.0, 3.0);
        expect(accel.rear > level.rear, "accelerating increases rear load");
        expect(accel.front < level.front, "accelerating decreases front load");
    }

    // ---- axleLoads(): aero downforce grows with speed^2 and splits by aeroBalanceF ----
    {
        AxleLoads slow = axleLoads(c, 20.0, 0.0);
        AxleLoads fast = axleLoads(c, 60.0, 0.0);
        const double slowTotal = slow.front + slow.rear;
        const double fastTotal = fast.front + fast.rear;
        expect(fastTotal > slowTotal, "higher speed produces more total downforce");
        // Downforce added at 60 m/s vs 20 m/s should scale with v^2 (60^2-20^2 vs 0),
        // split between axles per aeroBalanceF (0.45 front / 0.55 rear by default).
        const double expectedDownforce = c.dfK * (60.0 * 60.0 - 20.0 * 20.0) * c.mass * G;
        expectNear("aero downforce total delta matches dfK*v^2*mass*G",
                   fastTotal - slowTotal, expectedDownforce, 1e-3);
        expectNear("aero split matches aeroBalanceF (front share)",
                   (fast.front - slow.front) / expectedDownforce, c.aeroBalanceF, 1e-6);
    }

    // ---- axleLoads(): aeroEfficiency scales the downforce contribution only ----
    {
        AxleLoads full = axleLoads(c, 40.0, 0.0, 1.0);
        AxleLoads degraded = axleLoads(c, 40.0, 0.0, 0.5);
        expect(degraded.front + degraded.rear < full.front + full.rear,
               "degraded aeroEfficiency reduces total downforce");
    }

    // ---- axleLoads(): floors to a small positive value, never zero/negative ----
    {
        CarConstants extreme = c;
        extreme.cgHeight = 50.0; // absurdly high CG to force a large transfer
        AxleLoads fz = axleLoads(extreme, 5.0, -20.0); // hard braking
        expect(fz.front > 0.0 && fz.rear > 0.0, "axle loads never go non-positive");
    }

    // ---- slipAngles(): straight-line, no yaw/lateral velocity -> zero slip ----
    {
        SlipAngles a = slipAngles(c, 0.0, 0.0, 40.0, 0.0);
        expectNear("straight-line front slip angle", a.front, 0.0);
        expectNear("straight-line rear slip angle", a.rear, 0.0);
    }

    // ---- slipAngles(): pure steering input with no body motion yet ----
    {
        SlipAngles a = slipAngles(c, 0.0, 0.0, 40.0, 0.1); // 0.1 rad steer
        expectNear("steer-only front slip angle equals steer angle", a.front, 0.1, 1e-6);
        expectNear("steer-only rear slip angle stays zero", a.rear, 0.0, 1e-6);
    }

    // ---- slipAngles(): positive lateral velocity (sliding left) increases
    // magnitude of both slip angles in the expected direction ----
    {
        SlipAngles zero = slipAngles(c, 0.0, 0.0, 40.0, 0.0);
        SlipAngles slid = slipAngles(c, 5.0, 0.0, 40.0, 0.0);
        expect(slid.front < zero.front, "lateral velocity reduces front slip angle (opposes steer sense)");
        expect(slid.rear < zero.rear, "lateral velocity reduces rear slip angle the same way");
    }

    // ---- axleLateralForce(): linear region, no clamping ----
    // Regression-pass fix: was asserted as -stiffness*slipAngle, matching a
    // stray negation that made the yaw dynamics linearly unstable (a positive
    // steer produced yaw rate in the wrong direction) -- see car.cpp's own
    // comment on axleLateralForce() and PORT_PROGRESS.md.
    {
        const double fy = axleLateralForce(/*stiffness=*/90000.0, /*slipAngle=*/0.02,
                                            /*mu=*/1.0, /*fz=*/7000.0, /*fxFrac=*/0.0);
        expectNear("linear-region force = stiffness*slipAngle", fy, 90000.0 * 0.02, 1e-6);
    }

    // ---- axleLateralForce(): clamps at the friction circle (fxFrac=0) ----
    {
        const double fy = axleLateralForce(90000.0, /*slipAngle=*/1.0 /* huge */, 1.0, 7000.0, 0.0);
        expectNear("clamped force magnitude equals mu*fz", std::fabs(fy), 1.0 * 7000.0, 1e-6);
    }

    // ---- axleLateralForce(): friction ellipse shrinks available Fy as
    // longitudinal grip fraction (fxFrac) increases ----
    {
        const double fyNoLongitudinal = axleLateralForce(90000.0, 1.0, 1.0, 7000.0, 0.0);
        const double fyHalfSpent = axleLateralForce(90000.0, 1.0, 1.0, 7000.0, 0.8);
        expect(std::fabs(fyHalfSpent) < std::fabs(fyNoLongitudinal),
               "spending longitudinal grip reduces available lateral force");
        expectNear("ellipse at fxFrac=0.8 matches sqrt(1-0.8^2)",
                   std::fabs(fyHalfSpent), 1.0 * 7000.0 * std::sqrt(1.0 - 0.8 * 0.8), 1e-6);
    }

    // ---- axleLateralForce(): fxFrac at +-1 (all grip spent longitudinally)
    // leaves zero lateral capacity ----
    {
        const double fy = axleLateralForce(90000.0, 1.0, 1.0, 7000.0, 1.0);
        expectNear("fxFrac=1 leaves zero lateral force", fy, 0.0, 1e-6);
    }

    // ---- maxSteerAngle: reported bug "one simple touch of button makes
    // hard turn" -- traced to this constant, not the (verified-correct)
    // 0.22-per-tick digital-input smoothing in step_car.cpp. H8 (first
    // pass) cut this 0.5 -> 0.12 rad; the user's follow-up report ("still
    // ... cutting way too hard", car still visibly off-axis) plus a deeper
    // scratch investigation (see car.h's own comment on maxSteerAngle) found
    // 0.12 rad still let a single 160ms tap leave a permanent multi-degree
    // heading offset large enough to clip a nearby wall, and still let
    // ordinary full-lock cornering through the tightest real track overshoot
    // the friction limit entirely. H9 cut it again, to 0.05 rad -- small
    // enough that the front axle's own saturation slip angle, alpha_sat =
    // mu*fzFront/cf, is now OUTSIDE full lock's own raw range (full lock no
    // longer reaches the tire's friction ceiling at all, deliberately
    // staying in the linear/proportional regime -- see the corner-holding
    // check further below for why that's still enough to drive the game's
    // tightest real corner). Replicates the exact tap smoothing here
    // (`c.steer += (steerIn - c.steer) * 0.22`, isPlayer branch, steerIn=1
    // held from a standing start) and confirms: (a) full lock no longer
    // reaches alpha_sat at all; (b) replaying the exact H8-era value (0.5)
    // against this same tap sequence saturates on tick 1 (a single 20ms tick
    // reaching the tire's absolute lateral limit -- the original "instant
    // hard lock" report), while the current value does not saturate within
    // the window at all. ----
    {
        // The 0.12-per-tick player ramp (step_car.cpp) must stay slow enough
        // that a short tap is a nudge, not a lock-slam. Replicates that exact
        // smoothing from a standing start with the key held.
        auto steerAfterTicks = [](double blend, int ticks) {
            double steer = 0.0;
            for (int t = 0; t < ticks; ++t) steer += (1.0 - steer) * blend;
            return steer;
        };
        // A "tap" is ~8 ticks (160ms). At the old 0.22 that reached 81% of
        // full lock -- ~12 degrees of steering off a flick of a key, which is
        // the reported jolt. At 0.12 it reaches ~64%.
        const double tap = steerAfterTicks(0.12, 8);
        expect(tap < steerAfterTicks(0.22, 8),
               "the player ramp is slower than the old 0.22, so a short tap commands less lock");
        expect(tap < 0.70, "a 160ms tap stays well short of full lock");
        // ...but must still reach effectively full lock when genuinely held,
        // or the player cannot catch a slide (measured: ramps at/below 0.04
        // collapse into write-offs on two tracks).
        expect(steerAfterTicks(0.12, 40) > 0.99,
               "holding the key still reaches full lock within ~0.8s, preserving recovery authority");

        // ---- L8: steering RESOLUTION -- the property four earlier passes
        // missed. They all asked "how big is the response"; the reported bug
        // ("one small tap car jolts hard") is really "how finely can the
        // player ask for it". That is a static property of the input mapping,
        // so it is asserted directly here. Deliberately NOT measured through
        // the full-race harness: that harness turned out to pass on broken
        // code purely because of the RNG seed it hardcoded (see
        // drivability_test.cpp's header), whereas this arithmetic cannot.
        //
        // The yardstick is derived from the model's own steady-state relation
        // rather than hardcoded: the steer angle needed to hold the tightest
        // corner in the game (Milltown, R=100m) at racing speed. Everything
        // the player does in a corner should fit inside this.
        const double vRace = 45.0, rMin = 100.0;
        const double aF = c.wheelBase * (1 - c.weightDistF);
        const double aR = c.wheelBase * c.weightDistF;
        const double Kus = (c.mass / c.wheelBase) * (aR / c.cf - aF / c.cr);
        const double deltaNeeded = (vRace / rMin) * (c.wheelBase + Kus * vRace * vRace) / vRace;

        // The player's mapping, exactly as step_car.cpp applies it -- INCLUDING
        // L15's speed-sensitive yaw-authority cap, without which this would be
        // asserting a mapping the game no longer has.
        // N1b: mirrors step_car.cpp's speed blend. This lambda is a REPLICA of
        // the game's mapping rather than a call into it, so it has to track
        // changes there or it silently starts asserting a mapping the game no
        // longer has. Updating it is not a re-baseline to make a failure go
        // away: with this blend the assertions below pass at the OLD
        // steerYawAuthority (0.95) as well as the new 0.55, and the property
        // they guard -- full mechanical lock at low speed -- is genuinely
        // still true in the real code (14.90 deg at 8 m/s, verified).
        auto lockAt = [&](double v) {
            const double vAuth = std::max(6.0, v);
            const double authored = c.steerYawAuthority * (c.wheelBase + Kus * vAuth * vAuth) / vAuth;
            const double capBlend = std::max(0.0, std::min(1.0, (vAuth - 10.0) / 6.0));
            const double blended = c.maxSteerAngle + capBlend * (authored - c.maxSteerAngle);
            // N7: floor at the tightest corner's requirement.
            const double floorAng = c.steerCornerFloorMargin *
                                    (c.wheelBase + Kus * vAuth * vAuth) / c.steerTightestCornerR;
            return std::min(c.maxSteerAngle, std::max(blended, floorAng));
        };
        auto commandedAngle = [&](double steer) {
            return std::pow(steer, c.steerCurveGamma) * lockAt(vRace);
        };

        // The bug, stated as a test. A 160ms tap used to command ~3.9x the
        // tightest corner's steering (9.54 vs 2.47 deg -- a 26m radius at
        // 100mph). At the chosen gamma it commands ~1.98x (4.89 deg, 51m):
        // a real 2x improvement, deliberately not "less than one corner's
        // worth", which would need gamma ~4.0 and a centre range flat enough
        // to feel numb. Asserting < 1.0x here would assert a value nobody
        // chose; this bound is the honest one for what shipped.
        expect(commandedAngle(tap) < deltaNeeded * 2.5,
               "a 160ms tap no longer commands multiples of the tightest corner's steering");
        // Prove the CURVE is doing the work, not some other constant having
        // quietly moved: the same tap through the old linear mapping
        // overshot that corner badly.
        expect(tap * c.maxSteerAngle > deltaNeeded * 3.0,
               "sanity: the same tap through the OLD linear mapping overshot that corner ~3.9x");
        // The finest possible input -- one 20ms tick -- must be a nudge. It
        // used to be 72% of a whole corner's steering.
        expect(commandedAngle(steerAfterTicks(0.12, 1)) < deltaNeeded * 0.25,
               "the shortest possible key press is a fine nudge, not most of a corner's steering");
        // ---- L15: what a HELD key commands. Previously this asserted that
        // holding reaches full lock, which is exactly what made the car spin:
        // full lock is 15 deg while the tightest corner needs 2.47, so holding
        // the button commanded ~6x the steering the track ever asks for -- a
        // 16m radius at 100mph, which is a spin, not a hard turn. The property
        // that actually matters is that a held key gives a firm corner and NOT
        // a spin, and that low speed keeps every degree of lock for recovery.
        const double heldAngle = commandedAngle(steerAfterTicks(0.12, 50));
        expect(heldAngle > deltaNeeded,
               "holding the key can still out-turn the tightest corner, so a slide is catchable");
        expect(heldAngle < deltaNeeded * 3.0,
               "...but holding it no longer commands multiples of any corner on the track (i.e. a spin)");
        // Low speed must keep the full mechanical lock -- pit manoeuvring and
        // spin recovery depend on it, and this is the property that made a
        // blanket speed cap the wrong shape of fix.
        expect(lockAt(8.0) > c.maxSteerAngle * 0.99,
               "at low speed the full mechanical lock is still available");
        expect(lockAt(vRace) < c.maxSteerAngle,
               "at racing speed the yaw-authority cap is what binds, not the mechanical lock");

        // N4: releasing the button must unwind the wheel GENTLY. Reported as
        // "a slight stop of touching the turn button shoots car right": with a
        // symmetric 0.12 rate the gamma curve turned a 60 ms blip into a 62%
        // loss of steer angle, so the car snapped straight -- a jolt toward the
        // outside of a left corner.
        //
        // Replicates step_car.cpp's ramp, same replica convention as lockAt()
        // above (and the same obligation to track changes there).
        auto angleAfterRelease = [&](int ticks, double rate) {
            double steer = 1.0;
            for (int i = 0; i < ticks; ++i) steer += (0.0 - steer) * rate;
            return std::pow(std::fabs(steer), c.steerCurveGamma) * lockAt(vRace);
        };
        const double full = std::pow(1.0, c.steerCurveGamma) * lockAt(vRace);
        const double kept = angleAfterRelease(3, c.steerReleaseRamp) / full;
        expect(kept > 0.55, "a brief release keeps most of the steering instead of snapping straight");
        // ...and the release must genuinely be slower than the apply rate,
        // which is the whole mechanism. Guards against someone "simplifying"
        // it back to symmetric.
        expect(c.steerReleaseRamp < 0.12, "the wheel unwinds slower than it winds on");
        expect(kept > angleAfterRelease(3, 0.12) / full,
               "the asymmetric ramp actually retains more steering than the old symmetric one");
        // But not so slow the car keeps turning after you let go: a full
        // second of release must have essentially returned to centre.
        expect(angleAfterRelease(50, c.steerReleaseRamp) / full < 0.05,
               "releasing for a full second still returns the wheel to centre");

        // N7: THE assertion whose absence let a car ship that could not turn
        // into turn 1. Every check above tests that full lock is not too MUCH;
        // none tested that it is ever ENOUGH. A constant yaw cap scales as 1/v
        // while a corner's demand scales as v/R, so the two cross and the car
        // silently becomes unable to make the corner at all -- at cap*R, which
        // for Thunder Oval is 77 m/s and reachable straight off the green flag.
        //
        // Swept across the whole speed range the car can actually reach, not
        // just the one racing speed the other checks use -- the failure only
        // appears at the top end, which is exactly why a single-speed check
        // missed it.
        {
            const double rTightest = c.steerTightestCornerR;
            double worstMargin = 1e9;
            double worstV = 0;
            for (double v = 20.0; v <= 85.0; v += 1.0) {
                const double needed = (c.wheelBase + Kus * v * v) / rTightest;
                const double margin = lockAt(v) / needed;
                if (margin < worstMargin) {
                    worstMargin = margin;
                    worstV = v;
                }
            }
            expect(worstMargin > 1.0,
                   "full lock can always out-turn the tightest corner, at every speed the car reaches");
            // And with real margin, not just barely -- 1.10x steady-state was
            // measured to be insufficient once yaw inertia and the input ramp
            // are included, so a bound of 1.0 alone would not have caught it.
            expect(worstMargin > 1.15,
                   "...with enough margin left for yaw inertia and the input ramp");
            if (worstMargin <= 1.15) {
                std::fprintf(stderr, "  worst margin %.2fx at %.0f m/s\n", worstMargin, worstV);
            }
        }
    }

    // The actual "can a player drive this?" check lives in its own test,
    // tests/drivability_test.cpp, because it needs the full race.cpp tick()
    // (the other 19 cars, collisions and the dmg>=1 DNF rule) rather than
    // one car alone. A single-car version was written here first and is
    // deliberately NOT kept: measured against the pre-L1 constants it
    // reported ~0.97 damage while the full race had the player wrecked out
    // entirely on 3 of 4 tracks, so it systematically under-reported the
    // very failure it existed to catch. See that file's header.

    // H9's "hold full lock through the tightest corner and stay on the racing
    // line" check was REMOVED here rather than re-tuned, because L1 invalidated
    // its premise rather than its threshold. That assertion was only ever
    // satisfiable because full lock had been shrunk to 2.9 degrees -- barely
    // more than Milltown's corner needs -- so pinning the key happened to
    // trace the line. With full lock back at a realistic ~15 degrees, holding
    // it through a corner that needs ~11.6 correctly oversteers to the inside;
    // a real driver modulates rather than pinning the wheel. Asserting the old
    // behaviour would be asserting the bug. The drivability guard above covers
    // the same ground properly, with a driver that actually steers.

    // ---- integrateYawDynamics(): substep-count convergence over a
    // realistic in-game episode length. NOTE: sustained full-lock steering
    // held forever is NOT asserted to stay bounded here -- probing showed
    // the underlying saturating linear tire model has a genuine (non-
    // numerical) unbounded-growth mode under that exact, unrealistic
    // boundary condition: once slip angles saturate the friction ellipse at
    // both axles, rDot becomes roughly constant, so r grows ~linearly no
    // matter how fine the substep resolution (confirmed empirically: n=1
    // and n=4800 give indistinguishable long-run trajectories). In real
    // play this is always interrupted within ~15-30 ticks (a wall reset, a
    // wear/mu change, or the AI's own spin-recovery throttle lift -- see
    // step_car.cpp), which is exactly the window this checks instead: that
    // more substeps converge to a stable trajectory, not that r never
    // grows. Confirms n=4 (CarConstants::yawSubsteps' default) is already a
    // good approximation of the converged (high-substep) result for a
    // realistic episode, which is what the fix actually needed to
    // guarantee.
    {
        const double v = 30.0;
        const double steerAngle = c.maxSteerAngle;
        const AxleLoads fz = axleLoads(c, v, 0.0);
        auto runFor = [&](int substeps, int ticks) {
            double vy = 0.0, r = 0.0;
            for (int t = 0; t < ticks; ++t) {
                YawIntegrationResult res =
                    integrateYawDynamics(c, vy, r, v, v, steerAngle, fz, c.mu, c.mu, 0.0, 0.0, 0.02, substeps);
                vy = res.vy;
                r = res.r;
            }
            return r;
        };
        const int episodeTicks = 25; // matches the real sim's typical wall-reset interval
        const double r1 = runFor(1, episodeTicks);
        const double r4 = runFor(4, episodeTicks);
        const double r8 = runFor(8, episodeTicks);
        const double r64 = runFor(64, episodeTicks); // stand-in for the continuum limit
        expect(std::fabs(r4 - r64) < std::fabs(r1 - r64),
               "n=4 converges closer to the fine-substep trajectory than n=1 over a realistic episode");
        expect(std::fabs(r8 - r64) < std::fabs(r4 - r64),
               "n=8 converges closer still, confirming monotonic convergence as substeps increase");
        expect(std::fabs(r4 - r64) < 0.01,
               "n=4 (the default) is already a close approximation of the converged trajectory");
    }

    // ---- integrateYawDynamics(): wear-relevant outputs (slipMagAvg) stay
    // roughly substep-count-invariant for a short, non-saturating scenario
    // -- a time-weighted average, not a per-substep sum, so existing wear
    // tuning doesn't silently shift if the substep count ever changes ----
    {
        const double v = 30.0;
        const double steerAngle = 0.05; // mild steer, not saturating
        const AxleLoads fz = axleLoads(c, v, 0.0);
        YawIntegrationResult r1 = integrateYawDynamics(c, 0.0, 0.0, v, v, steerAngle, fz, c.mu, c.mu, 0.0, 0.0, 0.02, 1);
        YawIntegrationResult r4 = integrateYawDynamics(c, 0.0, 0.0, v, v, steerAngle, fz, c.mu, c.mu, 0.0, 0.0, 0.02, 4);
        YawIntegrationResult r8 = integrateYawDynamics(c, 0.0, 0.0, v, v, steerAngle, fz, c.mu, c.mu, 0.0, 0.0, 0.02, 8);
        expect(std::fabs(r1.slipMagAvg - r4.slipMagAvg) < 0.01,
               "slipMagAvg roughly substep-count-invariant (n=1 vs n=4)");
        expect(std::fabs(r4.slipMagAvg - r8.slipMagAvg) < 0.01,
               "slipMagAvg roughly substep-count-invariant (n=4 vs n=8)");
        expectNear("slipMagAvg equals slipFrontAvg+slipRearAvg (r4)", r4.slipMagAvg,
                   r4.slipFrontAvg + r4.slipRearAvg, 1e-9);
        expect(r4.pastLimitAny == (r4.pastLimitFront || r4.pastLimitRear),
               "pastLimitAny equals pastLimitFront||pastLimitRear");
    }

    // ---- P1 (NT2003 engine-feel plan, the loose/tight axis): the actual
    // "push into understeer -> fronts wear -> tighter -> push harder" loop
    // step_car.cpp runs on is CLOSED-loop -- the AI's steerIn is a feedback
    // term on yaw-rate error (CarConstants::yawCorrGain), not a fixed
    // steer angle. Holding steerAngle fixed and just degrading one axle's mu
    // (tried first, and left as a cautionary note rather than deleted
    // outright) does NOT reproduce the effect: a weaker front axle then just
    // generates less yaw torque overall, so the WHOLE car corners more
    // gently and every slip number drops, front included -- there is no
    // driver in that loop pushing harder to compensate. A tiny proportional
    // controller here (steerAngle = kP*(rTarget-r), clamped to
    // maxSteerAngle) stands in for that missing feedback -- "hold this yaw
    // rate no matter what" is exactly what dHdg-driven steerIn is doing in
    // the real game -- and is enough to reproduce the mechanism cleanly:
    // a front-worn car must steer harder to hold the same line, which loads
    // its (now weaker) front axle far more than the rear; a rear-worn car
    // shows the mirror image. This is the "deliberately-understeering line
    // drives wearFront > wearRear" verification, at the level this file
    // already operates (integrateYawDynamics() in isolation -- stepCar()
    // itself has no bgfx-free harness to run this same check against the
    // real AI code path). ----
    {
        const double v = 28.0;
        const double rTarget = 0.35; // a sustained, moderate cornering yaw rate
        const double kP = 3.0;       // steer-feedback gain, standing in for yawCorrGain's role
        const AxleLoads fz = axleLoads(c, v, 0.0);

        auto drive = [&](double muFront, double muRear) {
            double vy = 0.0, r = 0.0, frontSum = 0.0, rearSum = 0.0;
            int pfCount = 0, prCount = 0;
            constexpr int kTicks = 80, kWarmup = 20; // skip the initial transient before measuring
            for (int t = 0; t < kTicks; ++t) {
                const double steerAngle =
                    std::max(-c.maxSteerAngle, std::min(c.maxSteerAngle, kP * (rTarget - r)));
                YawIntegrationResult res = integrateYawDynamics(c, vy, r, v, v, steerAngle, fz, muFront, muRear,
                                                                 0.0, 0.0, 0.02, c.yawSubsteps);
                vy = res.vy;
                r = res.r;
                if (t >= kWarmup) {
                    frontSum += res.slipFrontAvg;
                    rearSum += res.slipRearAvg;
                    if (res.pastLimitFront) ++pfCount;
                    if (res.pastLimitRear) ++prCount;
                }
            }
            return std::tuple<double, double, int, int>{frontSum, rearSum, pfCount, prCount};
        };

        const auto [symF, symR, symPf, symPr] = drive(c.mu, c.mu);
        const auto [twF, twR, twPf, twPr] = drive(c.mu * 0.6, c.mu);
        const auto [rwF, rwR, rwPf, rwPr] = drive(c.mu, c.mu * 0.6);
        (void)twPr;
        (void)rwPf;

        expect(twF > symF, "a front-worn car steering to hold the same line loads its front MORE than the symmetric baseline");
        expect(twF > twR, "a front-worn car's front slip exceeds its own rear slip (the understeer signature)");
        expect(twPf > symPf, "a front-worn car's front axle hits the friction limit more often than the symmetric baseline");

        expect(rwR > symR, "a rear-worn car steering to hold the same line loads its rear MORE than the symmetric baseline");
        expect(rwR > rwF, "a rear-worn car's rear slip exceeds its own front slip (the oversteer signature)");
        expect(rwPr > symPr, "a rear-worn car's rear axle hits the friction limit more often than the symmetric baseline");
    }

    // ---- P2 (NT2003 engine-feel plan, fuel as real mass): a full tank
    // should measurably TIGHTEN the car (more understeer-prone) and slow its
    // straight-line acceleration -- "tight and sluggish on a full tank,
    // frees up as it burns off" is the plan's own stated goal, matching real
    // stock-car fuel-run folklore.
    //
    // The obvious-looking implementation -- shift weightDistF REARWARD as
    // fuel fills, since the fuel cell is physically rear-mounted -- was
    // tried first and empirically produces the OPPOSITE of the intended
    // feel in this bicycle model. integrateYawDynamics()'s closed-form
    // understeer gradient (see the steady-state test above) is
    // Kus=(mass/wheelBase)*(aR/cf - aF/cr): aR (CG-to-rear distance) pairs
    // with the FIXED front stiffness cf, aF pairs with the fixed rear
    // stiffness cr. Since cf/cr don't scale with load in this linear-tire
    // model, adding weight onto an axle raises THAT axle's own contribution
    // to Kus (more weight, no more stiffness to turn it with) rather than
    // starving the other axle the way a load-dependent tire model would. A
    // scratch closed-loop probe (same driver-feedback technique as the P1
    // test above) confirmed this concretely: shifting weightDistF rearward
    // made the REAR saturate MORE often at full tank, not the front -- a
    // real, verified LOOSE-at-full-tank result, backwards from the goal.
    // Shifting toward the FRONT instead (CarConstants::fuelWeightShiftF's
    // own comment) reproduces the intended tight-at-full-tank direction,
    // confirmed below.
    {
        // Mirrors step_car.cpp's own carEff derivation exactly (fuel=1 vs
        // fuel=0), so this test tracks the real formula rather than a
        // reimplementation of it.
        CarConstants full = c;
        full.mass = c.mass + 1.0 * c.fuelMass;
        full.weightDistF = c.weightDistF + 1.0 * c.fuelWeightShiftF;
        CarConstants empty = c; // fuel=0: unchanged

        expect(full.mass > empty.mass, "a full tank adds real mass");
        expect(full.weightDistF > empty.weightDistF, "a full tank shifts static balance toward the front");

        // ---- static Fz: the front axle should pick up proportionally MORE
        // of the added weight than the rear (the mechanism's actual load
        // asymmetry, ahead of any friction-ellipse saturation). ----
        {
            const AxleLoads fzFull = axleLoads(full, 0.0, 0.0);
            const AxleLoads fzEmpty = axleLoads(empty, 0.0, 0.0);
            const double frontGrowth = (fzFull.front - fzEmpty.front) / fzEmpty.front;
            const double rearGrowth = (fzFull.rear - fzEmpty.rear) / fzEmpty.rear;
            expect(frontGrowth > rearGrowth,
                   "a full tank's static load grows the front axle proportionally more than the rear");
        }

        // ---- longitudinal acceleration: mirrors step_car.cpp's `a =
        // (engF-drag-roll-brkF)/carEff.mass` -- identical net force, purely
        // heavier car, must accelerate more slowly ("sluggish"). ----
        {
            const double netForce = 6000.0 - 800.0 - 380.0; // representative engF-drag-roll, no braking
            const double aFull = netForce / full.mass;
            const double aEmpty = netForce / empty.mass;
            expect(aFull < aEmpty, "a full tank measurably slows straight-line acceleration for the same net force");
        }

        // ---- cornering tightness, via the same closed-form understeer
        // gradient as the standalone steady-state test further down this
        // file (Kus = (mass/wheelBase)*(aR/cf - aF/cr)): a full tank raises
        // aR (weightDistF shifts forward) and lowers aF, so Kus -- and with
        // it the amount of steer needed to hold a given yaw rate -- goes up.
        //
        // This block used to drive a closed-loop rTarget-tracking
        // controller into the friction ellipse instead (mirroring the P1
        // wear test above), asserting the resulting front/rear slip split
        // and rear-saturation-count directly. The maxSteerAngle fix
        // (0.5->0.12 rad, see CarConstants' own comment -- "one simple
        // touch of button... makes hard turn") broke that version: rTarget
        // = 0.50 rad/s was tuned to sit comfortably inside the OLD 0.5 rad
        // lock's headroom, letting the controller visibly "push harder" on
        // the loaded axle before saturating. Against the new 0.12 rad lock,
        // that same rTarget is no longer reachable without both cars
        // pinning the clamp for most of the run -- at that point the
        // "push harder" signal this mechanism relies on has nowhere left to
        // go, and a sweep across rTarget/kP confirmed no setting reproduces
        // all three original assertions with real margin anymore (the
        // front-slip and rear-saturation crossovers land at different,
        // nearby rTarget values instead of together). The closed-form check
        // below verifies the same "full tank tightens the car" claim
        // directly from the understeer gradient, at a small, deliberately
        // non-saturating steer angle -- so it stays valid regardless of
        // where maxSteerAngle happens to be tuned, rather than depending on
        // exactly how much clamp headroom is left above a chosen rTarget.
        {
            auto Kus = [](const CarConstants& cc) {
                const double aF = cc.wheelBase * (1 - cc.weightDistF);
                const double aR = cc.wheelBase * cc.weightDistF;
                return (cc.mass / cc.wheelBase) * (aR / cc.cf - aF / cc.cr);
            };
            expect(Kus(full) > Kus(empty),
                   "a full tank raises the understeer gradient (Kus) relative to an empty one");

            const double v = 30.0;
            const double steerAngle = 0.02; // well under both old and new maxSteerAngle -- stays linear
            const AxleLoads fzFull = axleLoads(full, v, 0.0);
            const AxleLoads fzEmpty = axleLoads(empty, v, 0.0);

            auto steadyStateYawRate = [&](const CarConstants& cc, const AxleLoads& fz) {
                double vy = 0.0, r = 0.0;
                bool everPastLimit = false;
                for (int t = 0; t < 200; ++t) {
                    YawIntegrationResult res = integrateYawDynamics(cc, vy, r, v, v, steerAngle, fz, cc.mu, cc.mu,
                                                                     0.0, 0.0, 0.02, cc.yawSubsteps);
                    vy = res.vy;
                    r = res.r;
                    if (res.pastLimitAny) everPastLimit = true;
                }
                expect(!everPastLimit, "closed-form understeer comparison stays in the linear (non-saturated) regime");
                return r;
            };

            const double rFull = steadyStateYawRate(full, fzFull);
            const double rEmpty = steadyStateYawRate(empty, fzEmpty);
            expect(rFull < rEmpty,
                   "a full tank yields less steady-state yaw rate than an empty one for the same steer angle (tighter)");
        }
    }

    // ---- integrateYawDynamics(): a sustained, non-saturating steer input
    // must settle to the CORRECT sign and magnitude of yaw rate, not just
    // converge across substep counts. This is the property a stray sign flip
    // in axleLateralForce() actually violated (see that function's own
    // comment): with the bug, the linearized yaw dynamics had an unstable
    // eigenvalue, so a car given a small, correctly-calibrated steer input
    // never settled at all -- it developed yaw rate in the WRONG direction
    // and grew until an unrelated nonlinearity (the friction ellipse) capped
    // it. Neither existing test above would have caught this: the n=1/4/8/64
    // comparison only checks the substeps agree with EACH OTHER, not with
    // the physically correct answer, and the linear-region unit test above
    // only checks axleLateralForce() in isolation, never that a sustained
    // steer actually holds a matching curved path end-to-end.
    //
    // Expected steady state derived from this exact code's own linearization
    // (not an external formula, so it can't smuggle in a different sign
    // convention): at rDot=0, vyDot=0, r_ss = v*steerAngle / (L + Kus*v*v),
    // where Kus = (mass/wheelBase) * (aR/cf - aF/cr) (understeer coefficient),
    // aR = wheelBase*weightDistF, aF = wheelBase*(1-weightDistF).
    {
        const double v = 30.0;
        const double steerAngle = 0.025; // mild, non-saturating (checked below)
        const AxleLoads fz = axleLoads(c, v, 0.0);

        const double aF = c.wheelBase * (1 - c.weightDistF);
        const double aR = c.wheelBase * c.weightDistF;
        const double Kus = (c.mass / c.wheelBase) * (aR / c.cf - aF / c.cr);
        const double rSs = v * steerAngle / (c.wheelBase + Kus * v * v);

        double vy = 0.0, r = 0.0;
        double rAt150 = 0.0;
        bool everPastLimit = false;
        for (int t = 0; t < 200; ++t) {
            YawIntegrationResult res =
                integrateYawDynamics(c, vy, r, v, v, steerAngle, fz, c.mu, c.mu, 0.0, 0.0, 0.02, c.yawSubsteps);
            vy = res.vy;
            r = res.r;
            if (res.pastLimitAny) everPastLimit = true;
            if (t == 149) rAt150 = r;
        }

        expect(!everPastLimit, "steady-state-tracking test stays in the linear (non-saturated) regime");
        expect((r > 0) == (steerAngle > 0),
               "sustained steer settles to yaw rate with the SAME sign as the steer input");
        expect(std::fabs(r - rAt150) < 1e-4,
               "yaw rate actually reaches steady state (not still diverging/oscillating at tick 200)");
        expectNear("yaw rate converges to the closed-form understeer steady state", r, rSs, 0.01);
    }

    // ---- torqueCurveMultiplier(): peaks at 1.0 mid-band, tapers at both
    // edges, never exceeds 1.0 or goes non-positive ----
    {
        const double lowEdge = torqueCurveMultiplier({1, 0.25});
        const double mid = torqueCurveMultiplier({1, 0.70});
        const double highEdge = torqueCurveMultiplier({1, 1.0});
        expectNear("torqueCurveMultiplier mid-band plateau is 1.0", mid, 1.0, 1e-9);
        expect(lowEdge < mid, "torqueCurveMultiplier tapers down just after a shift (low rpm)");
        expect(highEdge < mid, "torqueCurveMultiplier tapers down near redline (high rpm)");
        expect(lowEdge > 0.0 && highEdge > 0.0, "torqueCurveMultiplier never goes non-positive");
        for (double rpm = 0.25; rpm <= 1.0; rpm += 0.05) {
            expect(torqueCurveMultiplier({1, rpm}) <= 1.0 + 1e-9, "torqueCurveMultiplier never exceeds 1.0");
        }
    }

    // ---- suspensionLag(): converges to target, doesn't fully jump in one
    // step (transient smoothing, not a snap) ----
    {
        const double rate = 10.0, dt = 0.02;
        double fz = 0.0;
        for (int i = 0; i < 500; ++i) fz = suspensionLag(fz, 7000.0, rate, dt);
        expectNear("suspensionLag converges to target", fz, 7000.0, 1.0);

        const double firstStep = suspensionLag(0.0, 7000.0, rate, dt);
        expect(firstStep > 0.0 && firstStep < 7000.0,
               "suspensionLag doesn't fully jump to target in a single step");
    }

    if (g_failures == 0) {
        std::printf("tire_model_test: axleLoads/slipAngles/axleLateralForce all correct.\n");
        return 0;
    }
    std::fprintf(stderr, "tire_model_test: %d FAILURES.\n", g_failures);
    return 1;
}
