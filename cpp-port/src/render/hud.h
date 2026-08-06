#pragma once

#include "../sim/car.h"
#include "../sim/race_state.h"
#include "vertex.h"

#include <utility>
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
// Phase 4e added the segmented TIRE/FUEL/CAR status strip -- the first HUD
// feature needing real quad geometry rather than just dbgText. `uiOut` is
// the frame's shared UI-overlay vertex list (Renderer submits it as a
// second, pixel-space bgfx view after this call returns) -- hud.cpp stays
// Renderer-independent, only appending vertex data, never touching bgfx
// view/state calls itself for this part.
//
// Phase 4f added the minimap. `minimapOutline`/`minimapBoundX`/
// `minimapBoundY` come from Renderer::setTrack()'s eager cache (see
// renderer.h) -- passed in rather than pulling Renderer itself in as a
// dependency, same rationale as `uiOut` above.
//
// Phase 4g added the leaderboard panel. `trackTotal` (Track::total()) is
// used only for the leaderboard's lapEst fallback (index.html:3947) --
// same "pass the one scalar needed, not the whole Track type" rationale
// as the minimap's own parameters.
//
// Roadmap Phase 0 (fixing invisible touch controls): `windowW`/`windowH`
// feed computeTouchRegions() (src/ui/touch_controls.h) so the steer/brake/
// gas/pit buttons can finally be drawn (touch_buttons.h) at the exact same
// pixel rects main.cpp's input handling already hit-tests against -- was
// input-recognized but never painted (touch_controls.h's own header
// comment used to say so plainly).
//
// G21 (NT2003 presentation plan) rearranged all of the above from one
// left-hand column into corner blocks, matching how the reference
// distributes its HUD: minimap top-left, POS/LAP top-right, LAP TIME/BEST
// bottom-left, and the left column reduced to live telemetry (spotter,
// flag, speed, gear/RPM, TIRE/FUEL/CAR bars) with the leaderboard beneath
// it. `windowW`/`windowH` now also place the two new panels, so they are no
// longer only a touch-region input. Values in the panels are drawn with
// ui_draw.h's seven-segment helpers rather than dbgText -- dbgText is
// locked to one fixed 8x16 cell, so a headline number drawn with it would
// be the same size as its own caption.
//
// Caller is expected to have called bgfx::setDebug(BGFX_DEBUG_TEXT) once
// at init and bgfx::dbgTextClear() once this frame before calling this.
void drawHud(const RaceState& state, const std::vector<Car>& cars, std::vector<PosColorVertex>& uiOut,
             const std::vector<std::pair<float, float>>& minimapOutline, float minimapBoundX,
             float minimapBoundY, double trackTotal, int windowW, int windowH);

// Phase 4g (PORT_PROGRESS.md): the same descending race-position sort key
// tick() uses to build S.order (race.cpp:339-343, index.html:4192's own
// `done ? finishT : prog` metric) -- recomputed here purely for display,
// not a sim decision, so this doesn't touch race.cpp/tick() at all.
// Reused by leaderboard.cpp (Phase 4g) and results.cpp (Phase 4h).
std::vector<const Car*> computeRaceOrder(const std::vector<Car>& cars);
