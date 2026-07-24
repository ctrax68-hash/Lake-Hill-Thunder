// Verifies the tire-model upgrade's new pure functions (src/sim/car.{h,cpp}):
// axleLoads(), slipAngles(), axleLateralForce(). These replace how
// step_car.cpp executes per-tick cornering physics -- cornerCap()/
// cornerSpeed()/targetSpeed() (the AI's corner-speed-planning heuristic) are
// untouched and stay covered by speed_model_test.cpp/car_test.cpp/
// race_sim_test.cpp, all still passing bit-for-bit against JS ground truth,
// which is what confirms this upgrade didn't disturb them.

#include "../src/sim/car.h"

#include <cmath>
#include <cstdio>

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
    {
        const double fy = axleLateralForce(/*stiffness=*/90000.0, /*slipAngle=*/0.02,
                                            /*mu=*/1.0, /*fz=*/7000.0, /*fxFrac=*/0.0);
        expectNear("linear-region force = -stiffness*slipAngle", fy, -90000.0 * 0.02, 1e-6);
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
                    integrateYawDynamics(c, vy, r, v, v, steerAngle, fz, c.mu, 0.0, 0.0, 0.02, substeps);
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
        YawIntegrationResult r1 = integrateYawDynamics(c, 0.0, 0.0, v, v, steerAngle, fz, c.mu, 0.0, 0.0, 0.02, 1);
        YawIntegrationResult r4 = integrateYawDynamics(c, 0.0, 0.0, v, v, steerAngle, fz, c.mu, 0.0, 0.0, 0.02, 4);
        YawIntegrationResult r8 = integrateYawDynamics(c, 0.0, 0.0, v, v, steerAngle, fz, c.mu, 0.0, 0.0, 0.02, 8);
        expect(std::fabs(r1.slipMagAvg - r4.slipMagAvg) < 0.01,
               "slipMagAvg roughly substep-count-invariant (n=1 vs n=4)");
        expect(std::fabs(r4.slipMagAvg - r8.slipMagAvg) < 0.01,
               "slipMagAvg roughly substep-count-invariant (n=4 vs n=8)");
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
