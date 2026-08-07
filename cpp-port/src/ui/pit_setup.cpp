#include "pit_setup.h"

#include "../render/color.h"
#include "../render/ui_draw.h"

#include <bgfx/bgfx.h>

#include <algorithm>
#include <cmath>

namespace {

// Same fixed 8x16 dbgText cell geometry every other UI module in this
// project derives its regions from (menu.cpp/status_bars.cpp's own
// comments make the same point).
constexpr float kCellW = 8.0f, kCellH = 16.0f;
constexpr int kCol = 46;         // left edge of the panel, in text columns
constexpr int kRowTitle = 13;
constexpr int kRowWedge = 15;
constexpr int kRowTrackBar = 17;
constexpr int kRowHint = 19;

// Button/track layout, in text columns relative to kCol -- labels are 11
// chars ("WEDGE:     "/"TRACK BAR: "), so the minus button starts right
// after them, then the track, then the plus button.
constexpr int kLabelCols = 11;
constexpr int kBtnCols = 3;
constexpr int kBarCols = 20;

constexpr uint8_t kBlack = 0, kWhite = 15, kYellow = 14, kGrey = 7;
constexpr uint8_t attr(uint8_t fg, uint8_t bg) { return (uint8_t)((bg << 4) | fg); }

SDL_Rect colRect(int col, int cols, int row) {
    SDL_Rect r;
    r.x = col * (int)kCellW;
    r.y = row * (int)kCellH;
    r.w = cols * (int)kCellW;
    r.h = (int)kCellH;
    return r;
}

// P4's own version of status_bars.cpp's drawBalanceBar() widget shape (a
// center-zero track -- steel background, light center notch, colored
// marker offset by a signed [-1,1] value) -- not reused directly since
// that one is file-local to status_bars.cpp and specific to the wear
// balance reading; this is the same idea applied to a player-adjustable
// setup value instead of a derived wear statistic.
void drawSetupTrack(int row, double value, std::vector<PosColorVertex>& uiOut) {
    const float barX = (float)((kCol + kLabelCols + kBtnCols) * (int)kCellW);
    const float barW = (float)(kBarCols * (int)kCellW);
    const float y = (float)row * kCellH + (kCellH - 10.0f) / 2.0f;
    pushQuad(uiOut, barX, y, barW, 10.0f, packColor(Theme::kSteel));
    constexpr float kNotchW = 2.0f;
    pushQuad(uiOut, barX + barW / 2.0f - kNotchW / 2.0f, y, kNotchW, 10.0f, packColor(Theme::kGraycool));

    const double v = std::max(-1.0, std::min(1.0, value));
    const float* markerRgb = std::fabs(v) < 0.05 ? Theme::kGraycool : (v > 0 ? Theme::kOrange : Theme::kBlue);
    constexpr float kMarkerW = 8.0f;
    const float markerX = barX + barW / 2.0f + (float)v * (barW / 2.0f - kMarkerW / 2.0f) - kMarkerW / 2.0f;
    pushQuad(uiOut, markerX, y - 2.0f, kMarkerW, 10.0f + 4.0f, packColor(markerRgb));
}

} // namespace

PitSetupRegions computePitSetupRegions() {
    PitSetupRegions r{};
    r.wedgeMinus = colRect(kCol + kLabelCols, kBtnCols, kRowWedge);
    r.wedgePlus = colRect(kCol + kLabelCols + kBtnCols + kBarCols, kBtnCols, kRowWedge);
    r.trackBarMinus = colRect(kCol + kLabelCols, kBtnCols, kRowTrackBar);
    r.trackBarPlus = colRect(kCol + kLabelCols + kBtnCols + kBarCols, kBtnCols, kRowTrackBar);
    return r;
}

double clampPitSetup(double value) {
    return std::max(-kPitSetupMax, std::min(kPitSetupMax, value));
}

void drawPitSetup(const Car& player, std::vector<PosColorVertex>& uiOut) {
    bgfx::dbgTextPrintf(kCol, kRowTitle, attr(kYellow, kBlack), "PIT ADJUSTMENTS");

    bgfx::dbgTextPrintf(kCol, kRowWedge, attr(kWhite, kBlack), "WEDGE:     ");
    bgfx::dbgTextPrintf(kCol + kLabelCols, kRowWedge, attr(kWhite, kBlack), "-");
    bgfx::dbgTextPrintf(kCol + kLabelCols + kBtnCols + kBarCols, kRowWedge, attr(kWhite, kBlack), "+");
    drawSetupTrack(kRowWedge, player.setupWedge, uiOut);

    bgfx::dbgTextPrintf(kCol, kRowTrackBar, attr(kWhite, kBlack), "TRACK BAR: ");
    bgfx::dbgTextPrintf(kCol + kLabelCols, kRowTrackBar, attr(kWhite, kBlack), "-");
    bgfx::dbgTextPrintf(kCol + kLabelCols + kBtnCols + kBarCols, kRowTrackBar, attr(kWhite, kBlack), "+");
    drawSetupTrack(kRowTrackBar, player.setupTrackBar, uiOut);

    bgfx::dbgTextPrintf(kCol, kRowHint, attr(kGrey, kBlack), "(auto-exits when service is done)");
}
