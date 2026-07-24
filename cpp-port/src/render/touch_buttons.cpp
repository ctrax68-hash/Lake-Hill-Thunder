#include "touch_buttons.h"
#include "color.h"
#include "ui_draw.h"

#include <bgfx/bgfx.h>

#include <cstring>

namespace {

constexpr float kCellW = 8.0f, kCellH = 16.0f; // dbgText's fixed glyph cell, matching hud.cpp's own constants
constexpr uint8_t kWhite = 15, kBlack = 0;      // dbgText VGA palette

uint8_t attr(uint8_t fg, uint8_t bg) {
    return (uint8_t)((bg << 4) | fg);
}

// A ~2px border via two nested quads (outer = border color, inner = dark
// fill) -- cheaper than a real stroke and consistent with this renderer's
// existing flat-quad-only UI style (minimap.cpp/leaderboard.cpp draw boxes
// the same "bordered rect via two overlapping quads" way). The label is
// centered on the region's own character cell as closely as dbgText's fixed
// 8x16 grid allows -- exact sub-cell centering isn't possible, but landing
// within a cell or two of center reads fine at these button sizes.
void drawButton(const SDL_Rect& r, const float borderRgb[3], const char* label,
                std::vector<PosColorVertex>& uiOut) {
    constexpr float kBorder = 2.0f;
    pushQuad(uiOut, (float)r.x, (float)r.y, (float)r.w, (float)r.h, packColor(borderRgb, 0.9f));
    pushQuad(uiOut, (float)r.x + kBorder, (float)r.y + kBorder, (float)r.w - 2.0f * kBorder,
             (float)r.h - 2.0f * kBorder, packColor(Theme::kBlack, 0.55f));

    const int len = (int)std::strlen(label);
    const int col = (int)((r.x + r.w / 2.0f) / kCellW - len / 2.0f + 0.5f);
    const int row = (int)((r.y + r.h / 2.0f) / kCellH - 0.5f + 0.5f);
    bgfx::dbgTextPrintf(col, row, attr(kWhite, kBlack), "%s", label);
}

} // namespace

void drawTouchButtons(const TouchRegions& regions, std::vector<PosColorVertex>& uiOut) {
    // Border colors match index.html's own per-button CSS border-color
    // exactly (steer pair blue via the shared .ctl default, brake red,
    // gas yellow, pit orange) -- index.html:36-52.
    drawButton(regions.bL, Theme::kBlue, "<", uiOut);
    drawButton(regions.bR, Theme::kBlue, ">", uiOut);
    drawButton(regions.bB, Theme::kRed, "BRAKE", uiOut);
    drawButton(regions.bG, Theme::kYellow, "GAS", uiOut);
    drawButton(regions.bP, Theme::kOrange, "PIT", uiOut);

    // Camera-mode toggle: a neutral (non-drive-control) border color, since
    // it isn't one of JS's own steer/brake/gas/pit colors -- mirrors JS's
    // own CAM button being visually distinct from the drive controls too.
    drawButton(regions.bC, Theme::kGraycool, "CAM", uiOut);
}
