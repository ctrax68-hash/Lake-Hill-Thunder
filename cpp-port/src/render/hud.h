#pragma once

#include "../sim/car.h"
#include "../sim/race_state.h"

#include <vector>

// Phase 4a (PORT_PROGRESS.md): a first, functional-only slice of JS's much
// larger drawHUD() (index.html:3927-4058) -- lap counter, player race
// position, flag state, and speed, drawn via bgfx's built-in debug-text
// overlay (bgfx::dbgTextPrintf(), a fixed 8x16-cell monospace VGA-style
// text buffer) rather than a custom font atlas, since this port has no
// other text-rendering capability yet and bgfx already ships one.
//
// Phase 4c added the LAST/BEST lap time strip (Car::lastLapT/bestLapT,
// already correctly maintained by the ported physics core -- this was a
// pure rendering addition, no sim-core change). Phase 4d added the
// GEAR/RPM readout (gear_rpm.h's gearRpm(), a pure function of Car::v --
// not real stored physics state either).
//
// Still NOT a port of JS's full HUD information surface: no per-driver
// leaderboard panel (names, car-color chips, live gaps), no minimap
// (player wedge, trouble-pulse rings), no segmented tire/fuel/car bars.
// Those are left for the remaining Phase 4 sub-tasks -- see
// PORT_PROGRESS.md.
//
// Caller is expected to have called bgfx::setDebug(BGFX_DEBUG_TEXT) once
// at init and bgfx::dbgTextClear() once this frame before calling this.
void drawHud(const RaceState& state, const std::vector<Car>& cars);
