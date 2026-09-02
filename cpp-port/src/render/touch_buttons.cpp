#include "touch_buttons.h"
#include "color.h"
#include "hud_text.h"
#include "ui_draw.h"

namespace {

// G27: the button label size. Larger than the HUD's other captions on
// purpose -- these are the only text in the game a thumb has to find while
// the car is moving, and the reference's on-screen controls are likewise
// bigger than its readouts.
constexpr float kLabelSize = 18.0f;

// A ~2px border via two nested quads (outer = border color, inner = dark
// fill) -- cheaper than a real stroke and consistent with this renderer's
// existing flat-quad-only UI style (minimap.cpp/leaderboard.cpp draw boxes
// the same "bordered rect via two overlapping quads" way).
//
// G27: the label is now genuinely centred. Under dbgText it was snapped to
// the nearest 8x16 cell -- this comment used to say exact centring "isn't
// possible", which was true of that grid and is not true any more. Off-centre
// labels on the controls a thumb aims at while the car is moving is the one
// place in this HUD where the old approximation actually cost something.
void drawButton(const SDL_Rect& r, const float borderRgb[3], const char* label,
                std::vector<PosColorVertex>& uiOut, std::vector<PosColorUvVertex>& textOut) {
    constexpr float kBorder = 2.0f;
    pushQuad(uiOut, (float)r.x, (float)r.y, (float)r.w, (float)r.h, packColor(borderRgb, 0.9f));
    pushQuad(uiOut, (float)r.x + kBorder, (float)r.y + kBorder, (float)r.w - 2.0f * kBorder,
             (float)r.h - 2.0f * kBorder, packColor(Theme::kBlack, 0.55f));

    // Vertically centred on the cap height rather than on the full line box:
    // these labels are all caps and digits, so the descender space a line box
    // reserves would push everything visibly high in the button.
    const float cx = (float)r.x + (float)r.w / 2.0f;
    const float baseline = (float)r.y + (float)r.h / 2.0f + font::ascent(kLabelSize) * 0.5f;
    hudtext::drawCentered(textOut, cx, baseline, label, kLabelSize, packColor(Theme::kWhite));
}

} // namespace

void drawTouchButtons(const TouchRegions& regions, std::vector<PosColorVertex>& uiOut,
                      std::vector<PosColorUvVertex>& textOut) {
    // Border colors match index.html's own per-button CSS border-color
    // exactly (steer pair blue via the shared .ctl default, brake red,
    // gas yellow, pit orange) -- index.html:36-52.
    drawButton(regions.bL, Theme::kBlue, "<", uiOut, textOut);
    drawButton(regions.bR, Theme::kBlue, ">", uiOut, textOut);
    drawButton(regions.bB, Theme::kRed, "BRAKE", uiOut, textOut);
    drawButton(regions.bG, Theme::kYellow, "GAS", uiOut, textOut);
    drawButton(regions.bP, Theme::kOrange, "PIT", uiOut, textOut);

    // Camera-mode toggle: a neutral (non-drive-control) border color, since
    // it isn't one of JS's own steer/brake/gas/pit colors -- mirrors JS's
    // own CAM button being visually distinct from the drive controls too.
    drawButton(regions.bC, Theme::kGraycool, "CAM", uiOut, textOut);
}
