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
        const AxleLoads fzStatic = axleLoads(c, 0.0, 0.0);
        const double alphaSat = c.mu * fzStatic.front / c.cf;

        const double fraction = alphaSat / c.maxSteerAngle;
        expect(fraction >= 1.0,
               "full digital-input lock no longer reaches the front axle's own saturation slip angle");

        auto tapSaturationTick = [&](double maxSteer) {
            double steer = 0.0;
            for (int tick = 1; tick <= 10; ++tick) {
                steer += (1.0 - steer) * 0.22; // step_car.cpp's isPlayer smoothing, steerIn=1 held
                if (steer * maxSteer >= alphaSat) return tick;
            }
            return 11; // never saturated within the window
        };

        expect(tapSaturationTick(0.5) == 1,
               "sanity check: the H8-era maxSteerAngle (0.5) reproduces the original reported bug -- "
               "a held tap saturates the front axle on the very first 20ms tick");
        expect(tapSaturationTick(c.maxSteerAngle) == 11,
               "the current maxSteerAngle never saturates the front axle from a tap at all");
    }

    // ---- maxSteerAngle, continued (H9): the OTHER half of the persistent
    // report -- not a tap at all, just holding the turn key through the
    // game's tightest real corner (Milltown Bullring, R=100m) at the AI's
    // own computed target speed for it, ordinary technique any player would
    // use. Drives the REAL stepCar() end-to-end (not a hand transcription --
    // this scenario is exactly what stepCar()'s isPlayer branch computes),
    // holding gas+left the whole way from just before the tightest point.
    // At the H8-era value (0.12 rad) this scratch-tested to the car running
    // ~11m off the racing line into the wall inside 2 seconds -- the "car
    // still off-axis" report, reproduced directly. Asserts the CURRENT
    // value keeps the car close to the racing line and takes no damage. ----
    {
        const Track track(TRACKS[1]); // MILLTOWN BULLRING
        const double cornerV = cornerSpeed(track, 100.0, 110.0, 0.0);

        Mulberry32 rng(12345u);
        Car car = makeCar(true, 0, track, rng);
        car.lap = 0;
        car.v = cornerV;
        car.s = 105.0; // just before the tightest point (s=110)
        PointResult p0 = track.pointAt(car.s);
        car.x = p0.x;
        car.y = p0.y;
        car.hdg = p0.hdg;
        car.vdir = p0.hdg;

        RaceState state;
        state.mode = "race";
        state.flag = "green";
        PaceCar pace;
        std::vector<Car> allCars{car};
        PlayerInput input;
        input.gas = true;
        input.left = true; // held full lock toward the corner

        double worstLat = 0;
        for (int i = 0; i < 100; ++i) { // 2s
            allCars[0] = car;
            stepCar(car, state, track, allCars, pace, input);
            if (std::fabs(car.lat) > std::fabs(worstLat)) worstLat = car.lat;
        }
        expect(std::fabs(worstLat) < track.halfW() * 0.9,
               "holding full lock through the tightest real corner stays close to the racing line, not off "
               "into the wall");
        expect(car.dmg == 0.0, "holding full lock through the tightest real corner takes no wall damage");
    }

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
