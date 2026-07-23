#pragma once

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
    double power = 245000;
    double maxForce = 8200;
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
    double maxSteerAngle = 0.5;   // rad, full-lock front steer angle
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

    double wear = 0, draftF = 0;
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
