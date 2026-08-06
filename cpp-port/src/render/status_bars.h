#pragma once

#include "../sim/car.h"
#include "vertex.h"

#include <vector>

// Phase 4e (PORT_PROGRESS.md): index.html:3999-4020's consolidated TIRE/
// FUEL/CAR status strip. Draws the three labels via bgfx::dbgTextPrintf()
// (same approach as the rest of hud.cpp) and each stat's segmented bar via
// ui_draw.h's pushSegBar(), appending the bar geometry into `uiOut` for
// Renderer's UI-overlay view submission (see renderer.cpp). All three
// source fields (Car::wear/fuel/dmg) already exist and are already
// correctly maintained by the ported physics core -- this is a pure
// rendering addition, no sim-core change.
//
// G21 (NT2003 presentation plan): `baseRow` is the dbgText row of the TIRE
// bar; FUEL and CAR follow on baseRow+1/+2. Was hardcoded to rows 8-10 back
// when hud.cpp owned one uninterrupted left-hand column; that column now
// starts lower (to clear the top-left minimap) and is shorter (LAP/POS and
// the lap times moved into the new corner panels), so the caller owns the
// placement.
void drawStatusBars(const Car& player, int baseRow, std::vector<PosColorVertex>& uiOut);
