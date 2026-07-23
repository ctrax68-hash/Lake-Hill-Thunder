#pragma once

#include "../sim/car.h"
#include "vertex.h"

#include <string>
#include <vector>

// Phase 4g (PORT_PROGRESS.md): one row of the leaderboard panel, already
// resolved to exactly what should be displayed (tag precedence, gap-vs-
// name toggling, etc. all already applied) -- kept as plain data so
// buildLeaderboardRows() can be unit-tested independent of any dbgText/
// bgfx rendering.
struct LeaderboardRow {
    int rank; // 1-based race position (index.html's i+1)
    int carNum;
    std::string tag; // already truncated to 9 chars, matching JS's tag.slice(0,9)
    bool isPlayerRow;
    Color3 col;
    // True for the pinned player's row when they're outside the top N --
    // draws a thin divider above it (index.html:3951-3954).
    bool dividerBefore = false;
};

// Phase 4g (PORT_PROGRESS.md): builds the leaderboard's row list -- top-N
// (5) plus the player's own row pinned at the end if they're outside the
// top N (index.html:3826-3830's computeLayout() list-building +
// index.html:3946-3977's tag/gap resolution), given the race-position-
// sorted `order` (hud.h's computeRaceOrder()). `mode`/`flag`/`simT` drive
// the alternating gap/name broadcast toggle (index.html:3946's
// showGaps); `trackTotal` is Track::total(), used only for the lapEst
// fallback (index.html:3947) when the leader hasn't set a lap time yet.
// Pure logic, no bgfx dependency -- directly testable.
std::vector<LeaderboardRow> buildLeaderboardRows(const std::vector<const Car*>& order,
                                                  const std::string& mode, const std::string& flag,
                                                  double simT, double trackTotal);

struct LeaderboardBox {
    float x, y, w, h;
};

// Draws the header banner (CAUTION/LAKE HILL 400) and each row (rank +
// color chip + "#num tag") via bgfx::dbgTextPrintf() + ui_draw.h's
// pushQuad()/pushLineSegment(), matching hud.cpp's own dbgText approach
// and VGA-palette conventions. `flagYellow` selects the header banner
// text (index.html:3944).
void drawLeaderboard(const LeaderboardBox& box, const std::vector<LeaderboardRow>& rows, bool flagYellow,
                      std::vector<PosColorVertex>& uiOut);
