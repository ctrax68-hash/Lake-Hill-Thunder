#include "status_bars.h"
#include "color.h"
#include "ui_draw.h"

#include <bgfx/bgfx.h>

namespace {

// G21 (NT2003 presentation plan): the three rows are now placed relative to
// a caller-supplied base row rather than hardcoded at 8-10. hud.cpp's left
// text column moved down to clear the top-left minimap and lost four rows
// to the new corner panels, so a fixed row range no longer describes where
// these belong. Never rendered in the same frame as menu.cpp's rows
// (drawHud()/drawMenu() are mutually exclusive by RaceState::mode), so no
// cross-module row conflict either way.
constexpr float kCellW = 8.0f, kCellH = 16.0f;
constexpr float kBarX = 6.0f * kCellW; // clears the "TIRE "/"FUEL "/"CAR  " label
constexpr float kBarW = 200.0f;
constexpr float kBarH = 10.0f;
constexpr int kBarSegN = 6; // index.html:4016's drawSegBar(..., 6, ...)

constexpr uint8_t kWhite = 15, kBlack = 0; // dbgText VGA palette, matching hud.cpp's own constants

uint8_t attr(uint8_t fg, uint8_t bg) {
    return (uint8_t)((bg << 4) | fg);
}

void drawOneBar(int row, const char* label, double frac, const float* filledRgb,
                std::vector<PosColorVertex>& uiOut) {
    bgfx::dbgTextPrintf(1, row, attr(kWhite, kBlack), "%s", label);
    const float y = (float)row * kCellH + (kCellH - kBarH) / 2.0f;
    const uint32_t filledAbgr = packColor(filledRgb);
    const uint32_t emptyAbgr = packColor(Theme::kSteel); // index.html:3871's THEME.steel for unfilled segments
    pushSegBar(uiOut, kBarX, y, kBarW, kBarH, frac, kBarSegN, filledAbgr, emptyAbgr);
}

} // namespace

void drawStatusBars(const Car& player, int baseRow, std::vector<PosColorVertex>& uiOut) {
    // index.html:4005-4009's exact color thresholds and wr/dOK inversions
    // (the bars show "how much is left," not the raw wear/damage values).
    const double wr = 1.0 - player.wear;
    const double dOK = 1.0 - player.dmg;

    drawOneBar(baseRow, "TIRE", wr,
               wr > 0.5 ? Theme::kYellow : wr > 0.25 ? Theme::kOrange : Theme::kRed, uiOut);
    drawOneBar(baseRow + 1, "FUEL", player.fuel,
               player.fuel > 0.3 ? Theme::kBlue : player.fuel > 0.12 ? Theme::kOrange : Theme::kRed, uiOut);
    drawOneBar(baseRow + 2, "CAR ", dOK,
               dOK > 0.6 ? Theme::kBlue : dOK > 0.3 ? Theme::kOrange : Theme::kRed, uiOut);
}
