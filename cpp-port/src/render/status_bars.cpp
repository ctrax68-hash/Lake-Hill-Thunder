#include "status_bars.h"
#include "color.h"
#include "ui_draw.h"

#include <bgfx/bgfx.h>

#include <cmath>

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

// P1 (NT2003 engine-feel plan, the loose/tight axis): a center-zero
// balance track rather than pushSegBar()'s left-to-right fill -- there is
// no "how full" reading here, only a signed position between two named
// extremes, so the discrete-segment bar this file uses everywhere else
// doesn't fit. `balance` is in [-1, 1]: negative = loose (rear worn more),
// positive = tight (front worn more), 0 = even. Single-letter "L"/"T"
// dbgText captions bracket the track rather than full words -- this HUD's
// own established abbreviation convention (TIRE/FUEL/CAR, seven-segment
// digits instead of a real font), and there's no room for more within the
// same column budget the other bars already fit their label in.
void drawBalanceBar(int row, double balance, std::vector<PosColorVertex>& uiOut) {
    bgfx::dbgTextPrintf(1, row, attr(kWhite, kBlack), "BAL");
    const int leftCol = (int)(kBarX / kCellW) - 1;
    const int rightCol = (int)((kBarX + kBarW) / kCellW);
    bgfx::dbgTextPrintf(leftCol, row, attr(kWhite, kBlack), "L");
    bgfx::dbgTextPrintf(rightCol, row, attr(kWhite, kBlack), "T");

    const float y = (float)row * kCellH + (kCellH - kBarH) / 2.0f;
    pushQuad(uiOut, kBarX, y, kBarW, kBarH, packColor(Theme::kSteel));
    constexpr float kNotchW = 2.0f;
    pushQuad(uiOut, kBarX + kBarW / 2.0f - kNotchW / 2.0f, y, kNotchW, kBarH, packColor(Theme::kGraycool));

    const double b = std::max(-1.0, std::min(1.0, balance));
    // Neutral near the center, warming toward each named extreme -- same
    // "the color itself tells you how urgent this is" idiom as the other
    // bars' green/orange/red thresholds.
    const float* markerRgb = std::fabs(b) < 0.15 ? Theme::kGraycool : (b > 0 ? Theme::kOrange : Theme::kBlue);
    constexpr float kMarkerW = 8.0f;
    const float markerX = kBarX + kBarW / 2.0f + (float)b * (kBarW / 2.0f - kMarkerW / 2.0f) - kMarkerW / 2.0f;
    pushQuad(uiOut, markerX, y - 2.0f, kMarkerW, kBarH + 4.0f, packColor(markerRgb));
}

} // namespace

void drawStatusBars(const Car& player, int baseRow, std::vector<PosColorVertex>& uiOut) {
    // index.html:4005-4009's exact color thresholds and dOK inversion (the
    // bars show "how much is left," not the raw wear/damage values).
    //
    // P1 (NT2003 engine-feel plan, the loose/tight axis): the single TIRE
    // bar is now two, one per axle (Car::wearFront/wearRear -- see car.h's
    // comment on why Car::wear itself stays a derived max() rather than
    // being read here directly), plus a new BAL row between them and FUEL
    // showing which way the car's handling balance has drifted. Every row
    // below baseRow+2 shifted down by two to make room -- hud.cpp's own
    // leaderboard-placement math already accounts for this (kRowBars+3 ->
    // kRowBars+5, see its own comment).
    const double wrFront = 1.0 - player.wearFront;
    const double wrRear = 1.0 - player.wearRear;
    const double dOK = 1.0 - player.dmg;
    const double balance = player.wearFront - player.wearRear;

    drawOneBar(baseRow, "TIR-F", wrFront,
               wrFront > 0.5 ? Theme::kYellow : wrFront > 0.25 ? Theme::kOrange : Theme::kRed, uiOut);
    drawOneBar(baseRow + 1, "TIR-R", wrRear,
               wrRear > 0.5 ? Theme::kYellow : wrRear > 0.25 ? Theme::kOrange : Theme::kRed, uiOut);
    drawBalanceBar(baseRow + 2, balance, uiOut);
    drawOneBar(baseRow + 3, "FUEL", player.fuel,
               player.fuel > 0.3 ? Theme::kBlue : player.fuel > 0.12 ? Theme::kOrange : Theme::kRed, uiOut);
    drawOneBar(baseRow + 4, "CAR ", dOK,
               dOK > 0.6 ? Theme::kBlue : dOK > 0.3 ? Theme::kOrange : Theme::kRed, uiOut);
}
