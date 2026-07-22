#pragma once

#include "constants.h"
#include "rng.h"
#include "track.h"

#include <array>
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
};
inline const CarConstants CAR{};

double cornerCap(double mu, double bank); // defined in car.cpp (index.html:404)

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

// Car (the JS object shape built by makeCar(), index.html:453-503).
// progHist/replayHist/histTick are intentionally NOT ported here -- they're
// HUD telemetry / replay-camera bookkeeping only (added well after the
// original physics core), never read by stepCar()'s driving/physics logic.
// If Phase 2+ needs them for parity with the live HUD, add them then.
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

    double steer = 0, thr = 0, brk = 0;

    double wear = 0, draftF = 0;
    bool dirty = false;

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

    int pit = 0;
    double pitT = 0;
    bool pitReq = false;

    double fuel = 1, dmg = 0;
    bool out = false;

    int cautionSlot = -1;
    int gridSlot = -1;
    double gridLane = 0;

    // Set later, in gridStart() (Phase 1g) -- not by makeCar() itself.
    int gridAhead = -1;
    double px = 0, py = 0, phdg = 0, ps = 0, plat = 0;
};

// makeCar() (index.html:453-503). `track` supplies TRACK.pointAt(0) for the
// initial spawn position; `rng` is the shared gameplay RNG stream (seed
// 12345) -- must be called exactly 3 times per AI car (skill, aggr,
// grooveBias), zero times for the player, in that order, to stay
// bit-for-bit consistent with the JS original's call sequence.
Car makeCar(bool isPlayer, int idx, const Track& track, Mulberry32& rng);

// AI target-speed model (index.html:629-649). Takes the active Track
// explicitly rather than closing over a single global instance like the JS
// original does -- a direct parameter-passing adaptation, not a behavior
// change; the math and iteration order are unchanged.
double cornerSpeed(const Track& track, double R, double s, double wear);
double targetSpeed(const Track& track, const Car& car);
