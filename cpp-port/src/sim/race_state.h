#pragma once

#include <string>

// Port of the JS race state object S (index.html:506-520) and the pace-car
// global PACE (index.html:562-563). Only the physics/AI-relevant subset of S
// is ported for Phase 1 -- fields that are purely UI/audio/render state
// (S.sound, S.volume, S.camMode, S.shakeT) are intentionally left out, same
// rationale as Car's progHist/replayHist. `cars`/`player`/`order`/
// `finishOrder` are NOT stored here -- they're managed as an explicit
// std::vector<Car>& passed around instead of embedded in this struct, since
// C++ doesn't need JS's single-global-object convenience and explicit
// parameters make ownership clearer.
//
// Correction from this file's first version: S.tilt/S.tiltG were originally
// filed under "UI-only, skip" alongside sound/volume/camMode/shakeT. That
// was wrong -- they're the tilt-steering INPUT SIGNAL, read directly by
// stepCar()'s player branch (index.html:862) to compute steerIn. That's a
// physics input, not a render concern, so they're real fields here.
struct RaceState {
    std::string mode = "menu"; // 'menu'|'pace'|'race'|'qual'|'menuwait'|'victory'|'done'
    double t = 0;
    int laps = 5;

    std::string flag = "green"; // 'green'|'yellow'
    double yellowT = 0;
    int cautionUntilLap = -1;
    double greenLockT = 0;
    double sinceGreenT = 0;

    bool oneToGo = false;
    bool pitsOpen = true;
    int cautionMaxSlot = -1;

    int finishLaps = 5;
    std::string gwcState = "none"; // 'none'|'watch'|'clean1'|'white'
    int gwcAttempts = 0;
    int gwcMarkLap = -1;

    bool tilt = false;  // tilt-steer input mode toggle
    double tiltG = 0;   // tilt-steer signal, [-22, 22]-ish (see stepCar's player branch)

    // Set by gridStart() (index.html:591), not part of S's initial literal.
    double paceV = 0;
    double greenT = 0;
};

// PACE (index.html:562-563). px/py/phdg/ps/plat (render-interpolation only,
// same category as Car's own px/py/phdg/ps/plat) are not ported.
struct PaceCar {
    double s = 0, lat = 0, v = 0, hdg = 0, x = 0, y = 0;
    std::string state = "lead"; // 'lead'|'peel'|'parked'
};

// input (index.html:1234): the player's raw control state. A real UI wires
// these from touch/keyboard events (Phase 3); here it's just data the
// player branch of stepCar() reads.
struct PlayerInput {
    bool left = false, right = false, gas = false, brake = false;
};
