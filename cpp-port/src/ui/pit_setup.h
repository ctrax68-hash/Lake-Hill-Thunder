#pragma once

#include "../render/font_atlas.h"
#include "../render/vertex.h"
#include "../sim/car.h"

#include <SDL_rect.h>
#include <vector>

// P4 (NT2003 engine-feel plan, pit adjustments): the interactive pit-
// adjustment panel -- what turns P1's per-axle wear mechanic from
// something that just happens to the car into something the player
// actively tunes for, the same role NT2003's own pit screen plays. AI cars
// never see this (they auto-pit, unchanged, via step_car.cpp's existing
// c.pitT countdown); it only ever applies to the player, shown by the
// caller (main.cpp for input, renderer.cpp for drawing) whenever
// `player.isPlayer && player.pit == 2` (mid-service), layered on top of
// the ordinary HUD rather than being its own top-level RaceState::mode --
// mode stays "race" throughout a pit stop, unlike the menu/results screens
// this module's region-math pattern is otherwise copied from.
//
// Deliberately NOT gating pit exit on the player interacting with this
// panel: step_car.cpp's c.pitT countdown is completely untouched, so a
// headless run (no real player input -- see GAME-A's own documented
// limitation that the player only auto-drives under yellow) still
// completes every pit stop exactly as before. This panel is a pure bonus
// the player can use or ignore during the dwell, never a new way to get
// stuck in the pits.
//
// Scope note: the plan text also mentions "tire/fuel choices" alongside
// wedge/track bar. This pass implements wedge/track-bar only -- the
// mechanically load-bearing half, tying directly into P1's loose/tight
// axis and P2's carEff mechanism. Turning tire/fuel service into a real
// player CHOICE (vs. the existing always-full-service model this port
// already shares with the JS original) would need new partial-service/
// pit-duration mechanics disproportionate to this phase; left for a
// future pass, same as this project's other explicitly-logged scope cuts
// (PORT_PROGRESS.md's P4 entry).
struct PitSetupRegions {
    SDL_Rect wedgeMinus, wedgePlus, trackBarMinus, trackBarPlus;
};

// Fixed text-row layout (same "row-anchored SDL_Rect, sized to line up
// with what the draw function prints" pattern as menu.cpp's
// computeMenuRegions()) -- a centered panel over the track view, clear of
// every existing HUD element (leaderboard/bars top-left, minimap top-
// center, tachometer/buttons bottom).
PitSetupRegions computePitSetupRegions();

// Adjustment step size and clamp range, shared by the click handler
// (main.cpp) and the unit test so both stay bit-for-bit consistent with
// drawPitSetup()'s own displayed range -- matches Car::setupWedge/
// setupTrackBar's own documented [-1, 1] range (car.h).
constexpr double kPitSetupStep = 0.1;
constexpr double kPitSetupMax = 1.0;

// Clamps to [-kPitSetupMax, kPitSetupMax] -- repeated clicks at a limit are
// a no-op, not silently drifting past it.
double clampPitSetup(double value);

// G30: the geometry the label column has to satisfy, exposed so
// pit_setup_test can assert the fit instead of trusting a comment. The
// original 11-column width was the length of the dbgText string literals, and
// nothing noticed when proportional text outgrew it -- "TRACK BAR" rendered
// straight through the minus button. These let the test measure the real
// labels against the real column and fail if a future one does not fit.
extern const char* const kPitSetupLabels[2];
// Pixels available to a label before the minus button starts.
float pitSetupLabelWidthPx();

// Draws the panel (title, WEDGE/TRACK BAR center-zero tracks -- the same
// "steel background + center notch + signed marker" widget shape as
// status_bars.cpp's drawBalanceBar(), reused here rather than reinvented
// since it's exactly what a signed [-1,1] readout needs -- plus "-"/"+"
// buttons) via bgfx::dbgTextPrintf() + the Phase 4e UI-quad primitives,
// matching results.cpp's approach. `uiOut` is the frame's shared UI-
// overlay vertex list. Caller must have already called
// bgfx::dbgTextClear() this frame.
// G30: `textOut`, when non-null, draws this in the real UI font, with a panel
// behind it -- the dbgText version relied on the debug overlay always drawing
// on top of everything, which real text does not. Null falls back to dbgText.
void drawPitSetup(const Car& player, std::vector<PosColorVertex>& uiOut,
                  std::vector<PosColorUvVertex>* textOut = nullptr);
