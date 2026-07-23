#pragma once

#include "../render/vertex.h"
#include "../sim/car.h"

#include <SDL_rect.h>
#include <vector>

// Phase 4h (PORT_PROGRESS.md): the results screen (index.html:186-189's
// `#results` DOM overlay, populated by showResults()/index.html:4115-4131),
// adapted to this port's bgfx-debug-text-plus-UI-quad rendering (same
// approach as hud.cpp/leaderboard.cpp) and menu.cpp's region-hit-testing
// pattern, for the single "BACK TO MENU" button.
//
// Unlike the menu's "draw on top of the still-rendering track" approach,
// the results screen fully replaces the scene -- confirmed from JS's own
// CSS: `.overlay` is opaque black, `#menu` explicitly overrides to
// semi-transparent, `#results` has no such override, so it stays opaque.
// Renderer::renderFrame() skips track/car submission entirely when
// mode=="done", matching renderBlockedFrame()'s existing opaque-clear
// precedent more than the menu's.

struct ResultsRegions {
    SDL_Rect backBtn;
};

// The back button sits directly below the ranked list, so its row position
// depends on how many cars are in `resultsOrder` -- mirrors
// leaderboard.cpp's own variable-height `LeaderboardBox`, since a results
// list can have up to FIELD (20) rows, unlike menu.cpp's fixed row set.
ResultsRegions computeResultsRegions(int numRows);

// Builds index.html:4119-4121's exact 3-bucket finish order:
// `S.finishOrder.concat(S.order.filter(!done && !out)).concat(S.order.filter(!done && out))`
// -- finishers in crossing order, then still-racing cars (a caution/DNF
// interrupted the race before every car finished), then DNFs classified
// last. `finishOrder` mirrors race.h's own `std::vector<Car*>` (appended to
// by tick() in crossing order); `order` must already be race-position-
// sorted (hud.h's computeRaceOrder() output) for the still-racing/DNF
// buckets' internal order to match JS's own `S.order` traversal.
std::vector<const Car*> buildResultsOrder(const std::vector<Car*>& finishOrder,
                                           const std::vector<const Car*>& order);

// Draws the results screen (title, ranked rows -- rank/color chip/car
// number/name/best-lap-or-DNF -- and the "BACK TO MENU" row) via
// bgfx::dbgTextPrintf() + the Phase 4e UI-quad primitives (color chip),
// matching hud.cpp/leaderboard.cpp's approach. `uiOut` is the frame's
// shared UI-overlay vertex list (Renderer submits it as a second,
// pixel-space bgfx view after this call returns). Caller must have already
// called bgfx::dbgTextClear() this frame.
void drawResults(const std::vector<const Car*>& resultsOrder, std::vector<PosColorVertex>& uiOut);
