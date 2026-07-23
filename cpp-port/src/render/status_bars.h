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
void drawStatusBars(const Car& player, std::vector<PosColorVertex>& uiOut);
