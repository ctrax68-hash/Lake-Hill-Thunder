#pragma once

#include "../ui/touch_controls.h"
#include "vertex.h"

#include <vector>

// Roadmap Phase 0 (fixing invisible touch controls): touch_controls.h's
// input-region math (bL/bR/bB/bG/bP) has been functional since Phase 3a, but
// its own header comment says plainly "there is no visible on-screen button
// drawn yet" -- nothing ever painted those regions, so a touch/mouse player
// had no visual cue where to tap. Kept in src/render/ (not src/ui/, where
// touch_controls.h itself lives) to mirror status_bars.cpp/minimap.cpp/
// leaderboard.cpp's own placement: this is pure quad+dbgText drawing, same
// "isolate pixel-space drawing from region math" split touch_controls.h's
// own header comment already called for.
//
// Appends into the same frame-scoped `uiOut` vertex list every other HUD
// sub-module uses (hud.h's own doc comment) -- Renderer submits it as the
// pixel-space UI-overlay view after drawHud() returns, so this needs no
// bgfx dependency of its own beyond dbgTextPrintf for the labels.
void drawTouchButtons(const TouchRegions& regions, std::vector<PosColorVertex>& uiOut);
