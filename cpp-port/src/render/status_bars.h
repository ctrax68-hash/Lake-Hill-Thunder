#pragma once

#include "../sim/car.h"
#include "font_atlas.h"
#include "vertex.h"

#include <vector>

// Phase 4e (PORT_PROGRESS.md): index.html:3999-4020's consolidated TIRE/
// FUEL/CAR status strip. Draws the labels via bgfx::dbgTextPrintf() (same
// approach as the rest of hud.cpp) and each stat's segmented bar via
// ui_draw.h's pushSegBar(), appending the bar geometry into `uiOut` for
// Renderer's UI-overlay view submission (see renderer.cpp).
//
// G21 (NT2003 presentation plan): `baseRow` is the dbgText row of the first
// bar; the rest follow below it. Was hardcoded to rows 8-10 back when
// hud.cpp owned one uninterrupted left-hand column; that column now starts
// lower (to clear the top-left minimap) and is shorter (LAP/POS and the lap
// times moved into the new corner panels), so the caller owns the
// placement.
//
// P1 (NT2003 engine-feel plan, the loose/tight axis): now draws FIVE rows,
// not three -- TIR-F/TIR-R (Car::wearFront/wearRear), a new BAL loose<->
// tight indicator, then FUEL/CAR unchanged. Callers that reserved exactly 3
// rows below `baseRow` (this port has one: hud.cpp's leaderboard-placement
// math) need 5 now.
// G27: `topY` is the pixel top of the first bar row, replacing the dbgText
// `baseRow` this used to take, and the labels go into `textOut` in the real
// UI font. The row pitch stays 16px -- hud.cpp measures its leaderboard
// placement against the bottom of this strip, so the spacing is load-bearing.
void drawStatusBars(const Car& player, float topY, std::vector<PosColorVertex>& uiOut,
                    std::vector<PosColorUvVertex>& textOut);
