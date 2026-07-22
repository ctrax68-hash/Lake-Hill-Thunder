#pragma once

// Test-only synthetic player driver, ported verbatim from the brain script
// tools/playtest.js and tests/determinism/dump_js_trace.js use to drive the
// player car headlessly. This is NOT part of the shipped game/sim -- it's a
// scripted, RNG-free stand-in for a human driver, used only so the
// determinism checks have SOME player input to feed stepCar()'s player
// branch. Must stay byte-for-bit identical to the JS version or the two
// sides will diverge for a reason that has nothing to do with the port
// itself.

#include "../src/sim/car.h"
#include "../src/sim/race_state.h"
#include "../src/sim/track.h"

#include <algorithm>
#include <cmath>

inline void driverBrain(const Car& c, const Track& track, RaceState& state, PlayerInput& input) {
    constexpr double PI = 3.14159265358979323846;
    auto wrapPi = [](double a) {
        while (a > PI) a -= 2 * PI;
        while (a < -PI) a += 2 * PI;
        return a;
    };

    const double LA = std::max(14.0, c.v * 0.6);
    PointResult pT = track.pointAt(c.s + LA);
    const double lane = -2.0;
    const double tx = pT.x - std::sin(pT.hdg) * lane, ty = pT.y + std::cos(pT.hdg) * lane;
    double dH = wrapPi(std::atan2(ty - c.y, tx - c.x) - c.hdg);
    const double curvFF = track.pointAt(c.s + std::max(6.0, c.v * 0.3)).curv;
    state.tiltG = std::max(-1.0, std::min(1.0, curvFF * c.v * 1.9 + dH * 2.2)) * 22;
    input.left = input.right = false;

    if (state.mode == "pace") {
        // Only relevant if this car has no gridAhead (front row); stepCar's
        // pace branch reads allCars[c.gridAhead] itself using the same
        // index, so this input is actually unused during pace phase either
        // way -- kept for exact parity with the JS brain script.
    } else {
        const double sA = c.s + c.v * 0.55;
        PointResult pA = track.pointAt(sA);
        input.gas = true;
        input.brake = false;
        if (std::abs(pA.curv) > 1e-6) {
            const double R = 1 / std::abs(pA.curv);
            const double mu = 1 + 0.00016 * c.v * c.v;
            const double tb = std::tan(track.bankAt(sA));
            const double cap = 9.81 * (mu + tb) / std::max(0.25, 1 - mu * tb);
            const double need = c.v * c.v / R;
            if (need > cap * 0.92) input.gas = false;
            if (need > cap * 1.18) input.brake = true;
        }
    }
}
