#include "status_bars.h"
#include "color.h"
#include "hud_text.h"
#include "ui_draw.h"

#include <cmath>

namespace {

// G21 (NT2003 presentation plan): the three rows are now placed relative to
// a caller-supplied base row rather than hardcoded at 8-10. hud.cpp's left
// text column moved down to clear the top-left minimap and lost four rows
// to the new corner panels, so a fixed row range no longer describes where
// these belong. Never rendered in the same frame as menu.cpp's rows
// (drawHud()/drawMenu() are mutually exclusive by RaceState::mode), so no
// cross-module row conflict either way.
// G27: rows are pixel offsets now, not dbgText cells. kRowH keeps the 16px
// pitch the dbgText grid imposed, because hud.cpp's leaderboard placement is
// measured against the bottom of this strip -- changing the spacing here
// would silently move that too.
constexpr float kRowH = 16.0f;
constexpr float kLabelX = 8.0f;
constexpr float kBarX = 56.0f; // clears the widest label ("TIR-F") at kCaption
constexpr float kBarW = 200.0f;
constexpr float kBarH = 10.0f;
constexpr int kBarSegN = 6; // index.html:4016's drawSegBar(..., 6, ...)

void drawOneBar(float rowY, const char* label, double frac, const float* filledRgb,
                std::vector<PosColorVertex>& uiOut, std::vector<PosColorUvVertex>& textOut) {
    hudtext::draw(textOut, kLabelX, rowY + 12.0f, label, hudtext::kCaption,
                  packColor(Theme::kWhite));
    const float y = rowY + (kRowH - kBarH) / 2.0f;
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
// Single-letter captions bracket the track rather than full words -- this
// HUD's own established abbreviation convention, and there is no room for
// more within the same label budget the other bars fit into. (Written when
// this was dbgText and there was no real font to set them in; G27 gave it
// one, but the abbreviation is still the right call at this size.)
void drawBalanceBar(float rowY, double balance, std::vector<PosColorVertex>& uiOut,
                    std::vector<PosColorUvVertex>& textOut) {
    const float baseline = rowY + 12.0f;
    hudtext::draw(textOut, kLabelX, baseline, "BAL", hudtext::kCaption, packColor(Theme::kWhite));
    // L and T bracket the track. With a real font these can finally be placed
    // against the bar's actual pixel edges instead of the nearest 8px cell.
    hudtext::drawRight(textOut, kBarX - 4.0f, baseline, "L", hudtext::kCaption,
                       packColor(Theme::kGraycool));
    hudtext::draw(textOut, kBarX + kBarW + 4.0f, baseline, "T", hudtext::kCaption,
                  packColor(Theme::kGraycool));

    const float y = rowY + (kRowH - kBarH) / 2.0f;
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

void drawStatusBars(const Car& player, float topY, std::vector<PosColorVertex>& uiOut,
                    std::vector<PosColorUvVertex>& textOut) {
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

    drawOneBar(topY, "TIR-F", wrFront,
               wrFront > 0.5 ? Theme::kYellow : wrFront > 0.25 ? Theme::kOrange : Theme::kRed, uiOut,
               textOut);
    drawOneBar(topY + kRowH, "TIR-R", wrRear,
               wrRear > 0.5 ? Theme::kYellow : wrRear > 0.25 ? Theme::kOrange : Theme::kRed, uiOut,
               textOut);
    drawBalanceBar(topY + kRowH * 2.0f, balance, uiOut, textOut);
    drawOneBar(topY + kRowH * 3.0f, "FUEL", player.fuel,
               player.fuel > 0.3 ? Theme::kBlue : player.fuel > 0.12 ? Theme::kOrange : Theme::kRed,
               uiOut, textOut);
    drawOneBar(topY + kRowH * 4.0f, "CAR", dOK,
               dOK > 0.6 ? Theme::kBlue : dOK > 0.3 ? Theme::kOrange : Theme::kRed, uiOut, textOut);
}
