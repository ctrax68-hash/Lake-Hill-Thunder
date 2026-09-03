#include "menu.h"

#include "../render/color.h"
#include "../render/hud_text.h"
#include "../render/ui_draw.h"

#include <bgfx/bgfx.h>

#include <algorithm>
#include <cstdio>

// G24 (NT2003 presentation plan): the reference's menu is a stack of thick,
// beveled bars (one bold/highlighted) over a dark backdrop with a checkered
// accent -- this was previously a bare column of 16px dbgText rows over
// whatever blurry 3D vignette happened to render behind it. Reproduces the
// LAYOUT/STYLE only, with this project's own fictional title/roster (no
// real logo, game title, or livery) -- see PORT_PROGRESS.md's G24 entry.

namespace {

// bgfx's built-in debug-text overlay is a fixed 8x16-cell monospace grid
// (same fact hud.cpp's own comment documents) -- these constants translate
// text row/column positions into the pixel rects computeMenuRegions()
// returns, so the clickable area always lines up with what drawMenu()
// prints, without hand-duplicating pixel numbers in two places.
constexpr float kCellW = 8.0f, kCellH = 16.0f;
constexpr int kCol = 1; // all menu rows start at column 1, matching hud.cpp

constexpr int kRowTitle = 1;

// G24: each row is now a 3-text-row-tall (48px) beveled bar instead of a
// single 16px dbgText line, with a blank row's worth of gap between bars --
// matching the reference's visibly thick plates. Row order is intentionally
// UNCHANGED from before this pass (track/laps/qual/sound/tilt/volume/start,
// Start last) -- menu_test.cpp hard-asserts Start stays the bottommost bar,
// and reordering to put Start at the top (as the reference's "Quick Race"
// sits) would need that test rewritten and wasm_verify.js's click
// coordinates re-derived for no functional benefit, so Start instead gets
// the reference's highlighted *treatment* (bigger fill contrast, warm
// accent) while keeping its position.
constexpr int kBarRowsTall = 3; // 48px
constexpr int kBarRowStep = 4;  // 3 rows of bar + 1 row of gap
constexpr int kFirstBarRow = 3;

constexpr int kRowTrack = kFirstBarRow;
constexpr int kRowLaps = kRowTrack + kBarRowStep;
constexpr int kRowQual = kRowLaps + kBarRowStep;
constexpr int kRowSound = kRowQual + kBarRowStep;
constexpr int kRowTilt = kRowSound + kBarRowStep;
constexpr int kRowVolume = kRowTilt + kBarRowStep;
constexpr int kRowStart = kRowVolume + kBarRowStep;

// Generous click width in character cells -- covers the full label+value
// text with room to spare, not a precise glyph-bounding hit test (matching
// touch_controls.h's own "reasonable first pass" precedent for fixed
// regions rather than pixel-exact ones).
constexpr int kRowColsWide = 40;
constexpr int kStartColsWide = 24;

SDL_Rect rowRect(int row, int cols) {
    SDL_Rect r;
    r.x = (int)(kCol * kCellW);
    r.y = (int)(row * kCellH);
    r.w = (int)(cols * kCellW);
    r.h = (int)(kBarRowsTall * kCellH);
    return r;
}

// dbgTextPrintf's _attr byte, same VGA text-mode palette as hud.cpp.
constexpr uint8_t kBlack = 0, kGreen = 2, kYellow = 14, kWhite = 15, kGrey = 7;

// L13 (NT2003/2004 fidelity pass): the running build's identity, on screen.
// This exists because several rounds of real, verified gameplay fixes
// appeared to change nothing for the player, and neither of us could tell
// whether a given build had actually reached the browser -- the service
// worker had been serving a permanently stale cached copy (see web/sw.js.in's
// header). A build stamp makes "is this the new build?" answerable at a
// glance instead of by inference, which is worth far more than the two lines
// it costs. LHT_BUILD_STAMP is defined by CMakeLists.txt; the fallback keeps
// non-CMake/IDE builds compiling.
//
// Hoisted to file scope by G26, which added a second use inside drawMenu()
// (the font-rendered stamp) above the point this used to be defined at.
#ifndef LHT_BUILD_STAMP
#define LHT_BUILD_STAMP "dev"
#endif

constexpr uint8_t attr(uint8_t fg, uint8_t bg) {
    return (uint8_t)((bg << 4) | fg);
}

// Fills a beveled bar behind row `row` (same pixel rect rowRect() returns)
// and prints `label` centered on the bar's middle text-row -- reuses
// pushBevelPanel() exactly as status_bars.cpp's drawOneBar() already does,
// just for a whole labeled row instead of a segmented value bar.
// G30: G26 gave the menu TITLE a real font and left every row label in
// dbgText, which is the wrong half -- the title is read once, the rows are
// what a player actually operates.
//
// `label` and `value` are separate so the value can start at a fixed column.
// The dbgText version padded them into one monospace string ("TRACK:      ")
// which lines up only in a fixed-width font; in a proportional one the values
// came out ragged down the column. `text` (the pre-padded form) is still what
// the dbgText fallback prints, so that path is unchanged.
constexpr float kBarValueX = 150.0f; // clears the longest label at kBarTextSize
constexpr float kBarTextSize = 17.0f;

void drawBar(int row, int cols, uint8_t fg, const float fillRgb[3], const float lightRgb[3],
             const float darkRgb[3], const char* text, std::vector<PosColorVertex>& uiOut,
             std::vector<PosColorUvVertex>* textOut, const char* label = nullptr,
             const char* value = nullptr) {
    const SDL_Rect r = rowRect(row, cols);
    pushBevelPanel(uiOut, (float)r.x, (float)r.y, (float)r.w, (float)r.h, packColor(fillRgb),
                   packColor(lightRgb), packColor(darkRgb));
    // `text == nullptr` means "panel only" -- the caller is drawing its own
    // label (the Start button centres its own). Without this, passing a null
    // textOut to get just the plate fell through to the dbgText branch and
    // drew the terminal-font string underneath.
    if (!text) return;
    if (!textOut) {
        bgfx::dbgTextPrintf(kCol, row + 1, attr(fg, kBlack), "%s", text);
        return;
    }
    // Genuinely centred in the bar, rather than snapped to the middle text row
    // of three.
    const float baseline = (float)r.y + (float)r.h * 0.5f + kBarTextSize * 0.36f;
    const uint32_t col = fg == kGrey ? packColor(Theme::kGraycool) : packColor(Theme::kWhite);
    if (label && value) {
        hudtext::draw(*textOut, (float)r.x + 12.0f, baseline, label, kBarTextSize, col);
        hudtext::draw(*textOut, (float)r.x + kBarValueX, baseline, value, kBarTextSize,
                      packColor(Theme::kWhite));
    } else {
        hudtext::draw(*textOut, (float)r.x + 12.0f, baseline, text, kBarTextSize, col);
    }
}

// G24: a simple checkered accent band -- no general checkerboard primitive
// exists in ui_draw.h (atlas_texture.cpp's checker logic is a private,
// texture-bake-time-only path, not callable here), so this is a one-off
// helper local to the menu, written with the same "pure pixel-space vector
// math" contract ui_draw.h's own functions follow. Axis-aligned cells only
// (pushQuad() has no rotation), not the reference's diagonal stripe -- a
// reasonable first pass, not attempted here as a true diagonal tessellation.
void pushCheckerBand(std::vector<PosColorVertex>& out, float x, float y, float w, float h, int cols,
                     uint32_t colorA, uint32_t colorB) {
    if (cols <= 0) return;
    const float cellW = w / (float)cols;
    for (int i = 0; i < cols; ++i) {
        pushQuad(out, x + (float)i * cellW, y, cellW, h, (i % 2 == 0) ? colorA : colorB);
    }
}

} // namespace

MenuRegions computeMenuRegions() {
    MenuRegions r{};
    r.trackBtn = rowRect(kRowTrack, kRowColsWide);
    r.lapsBtn = rowRect(kRowLaps, kRowColsWide);
    r.qualBtn = rowRect(kRowQual, kRowColsWide);
    r.soundBtn = rowRect(kRowSound, kRowColsWide);
    r.tiltBtn = rowRect(kRowTilt, kRowColsWide);
    r.volumeBar = rowRect(kRowVolume, kRowColsWide);
    r.startBtn = rowRect(kRowStart, kStartColsWide);
    return r;
}

int cycleLaps(int laps) {
    // index.html:4706-4709's exact cycle order.
    if (laps == 3) return 5;
    if (laps == 5) return 10;
    if (laps == 10) return 20;
    return 3; // covers 20 -> 3 and any unexpected starting value
}

int cycleTrack(int trackIdx, int trackCount) {
    if (trackCount <= 0) return 0;
    // Also normalizes a negative or out-of-range starting value rather than
    // propagating it, same defensive spirit as cycleLaps()'s fallthrough.
    const int cur = (trackIdx % trackCount + trackCount) % trackCount;
    return (cur + 1) % trackCount;
}

int volumeFromClickX(const SDL_Rect& bar, int clickX) {
    if (bar.w <= 0) return 0;
    double t = (double)(clickX - bar.x) / (double)bar.w;
    t = std::max(0.0, std::min(1.0, t));
    return (int)std::lround(t * 100.0);
}

MenuHeaderLayout computeMenuHeader(const std::string& title, const std::string& stamp) {
    MenuHeaderLayout h;

    // The header band is rows 0-1 (32px) -- the two rows above the checker
    // accent, unchanged, so none of computeMenuRegions()' click rects move.
    // 24px leaves the cap height inside that band with a little air; 34px
    // was tried first and overran both the band and the stamp, which is
    // what the first screenshot of this feature showed.
    h.titleSize = 24.0f;
    h.stampSize = 11.0f;
    h.titleX = kCol * kCellW;
    h.bandHeight = (float)(kRowTitle + 1) * kCellH;

    // Anchored to the BOTTOM of the band, not derived from the ascent.
    // Deriving it put the baseline at 38.5px, four pixels into the checker
    // accent that starts at 32, and the band sliced through the lettering.
    // Sitting the baseline just above the accent is also simply how a title
    // bar reads: text resting on the rule, not floating in the middle of it.
    // Neither string has a descender, so this tail clearance is enough.
    h.baseline = h.bandHeight - 5.0f;

    // The band is sized to its CONTENT, not to the bar column. The stamp is
    // a build hash plus a timestamp, so its width is not knowable at
    // authoring time -- deriving the width from the measured strings is what
    // makes an overlap impossible for any stamp, rather than merely unlikely
    // for today's. This is the whole reason a proportional font needs a
    // measure(); the dbgText version could only start at a whole 8px cell
    // and hope.
    const float stampW = font::measure(stamp, h.stampSize);
    const float gap = kCellW * 2.0f, pad = kCellW;
    h.bandWidth = std::max((float)((kRowColsWide + 2) * kCellW),
                           h.titleX + font::measure(title, h.titleSize) + gap + stampW + pad);
    h.stampX = h.bandWidth - pad - stampW;
    return h;
}

void drawMenu(const MenuSelection& sel, int laps, bool tilt, const std::string& trackName,
              std::vector<PosColorVertex>& uiOut, std::vector<PosColorUvVertex>* textOut) {
    // Backdrop: a dark panel behind the whole bar stack, separating it from
    // the showcase car/track rendering behind it -- the reference's menu
    // sits over a glamour shot the same way. Sized to the bar column plus a
    // margin, not the full screen, since this module has no viewport size
    // to reason about (matches this file's existing "fixed pixel layout,
    // not viewport-adaptive" precedent).
    const float backdropW = (kRowColsWide + 2) * kCellW;
    const float backdropH = (float)(kRowStart + kBarRowsTall + 1) * kCellH;
    pushQuad(uiOut, 0.0f, 0.0f, backdropW, backdropH, packColor(Theme::kBlack, 0.55f));

    // G26: the title band can be wider than the bar column, since it is
    // sized to the measured title + build stamp (see below). Tracked here so
    // the checker accent spans the same width and the two read as one header.
    float headerWidth = backdropW;

    // G26 (graphics pass): the first text in this game drawn with a real
    // font. The title and build stamp are the demo on purpose -- they are
    // the two strings a player sees before touching anything, so "did the
    // font system land?" is answerable at a glance, the same reasoning that
    // put the build stamp on screen in the first place (L13, below).
    //
    // The dbgText path is kept as a fallback rather than deleted: textOut is
    // null whenever the atlas failed to decode, and a menu with no title at
    // all would be a far worse failure than a menu with an ugly one. G27
    // retires dbgText from the gameplay UI once every call site has moved.
    if (textOut) {
        const std::string title = "LAKE HILL THUNDER";
        const std::string stamp = std::string("build ") + LHT_BUILD_STAMP;
        const MenuHeaderLayout h = computeMenuHeader(title, stamp);

        pushQuad(uiOut, 0.0f, 0.0f, h.bandWidth, h.bandHeight, packColor(Theme::kBlack, 0.55f));
        font::pushTextShadowed(*textOut, h.titleX, h.baseline, title, h.titleSize,
                               packColor(Theme::kYellow), packColor(Theme::kBlack, 0.75f), 2.0f);
        font::pushTextShadowed(*textOut, h.stampX, h.baseline, stamp, h.stampSize,
                               packColor(Theme::kGraycool), packColor(Theme::kBlack, 0.75f), 1.0f);
        headerWidth = h.bandWidth;
    } else {
        bgfx::dbgTextPrintf(kCol, kRowTitle, attr(kYellow, kBlack), "LAKE HILL THUNDER");
        bgfx::dbgTextPrintf(kCol + 19, kRowTitle, attr(kGrey, kBlack), "build %s", LHT_BUILD_STAMP);
    }

    // Checkered accent band between the title and the first bar -- a
    // generic racing motif, not a reproduction of any real logo/lockup.
    const float checkerY = (kRowTitle + 1) * kCellH;
    pushCheckerBand(uiOut, 0.0f, checkerY, headerWidth, kCellH, 20, packColor(Theme::kWhite, 0.85f),
                    packColor(Theme::kSteel, 0.85f));

    drawBar(kRowTrack, kRowColsWide, kWhite, Theme::kSteel, Theme::kGraycool, Theme::kBlack,
            (std::string("TRACK:      ") + trackName).c_str(), uiOut, textOut, "TRACK:", trackName.c_str());

    char lapsText[32], lapsVal[8];
    std::snprintf(lapsText, sizeof(lapsText), "LAPS:       %-3d", laps);
    std::snprintf(lapsVal, sizeof(lapsVal), "%d", laps);
    drawBar(kRowLaps, kRowColsWide, kWhite, Theme::kSteel, Theme::kGraycool, Theme::kBlack, lapsText, uiOut,
            textOut, "LAPS:", lapsVal);

    // index.html's #qualTog exists, but this port doesn't have a real
    // qualifying flow yet (see MenuSelection::qual's own comment) -- grey
    // rather than white, a small honest visual cue that this row currently
    // has no gameplay effect.
    drawBar(kRowQual, kRowColsWide, kGrey, Theme::kSteel, Theme::kGraycool, Theme::kBlack,
            sel.qual ? "QUALIFYING: ON" : "QUALIFYING: OFF", uiOut, textOut, "QUALIFYING:",
            sel.qual ? "ON" : "OFF");
    drawBar(kRowSound, kRowColsWide, kGrey, Theme::kSteel, Theme::kGraycool, Theme::kBlack,
            sel.sound ? "SOUND:      ON" : "SOUND:      OFF", uiOut, textOut, "SOUND:",
            sel.sound ? "ON" : "OFF");
    drawBar(kRowTilt, kRowColsWide, kWhite, Theme::kSteel, Theme::kGraycool, Theme::kBlack,
            tilt ? "TILT STEER: ON" : "TILT STEER: OFF", uiOut, textOut, "TILT STEER:",
            tilt ? "ON" : "OFF");

    char volBar[21];
    const int filled = std::max(0, std::min(20, (sel.volume * 20) / 100));
    for (int i = 0; i < 20; ++i) volBar[i] = i < filled ? '#' : '-';
    volBar[20] = '\0';
    char volText[40], volVal[8];
    std::snprintf(volText, sizeof(volText), "VOLUME: [%s] %3d%%", volBar, sel.volume);
    std::snprintf(volVal, sizeof(volVal), "%d%%", sel.volume);
    drawBar(kRowVolume, kRowColsWide, kGrey, Theme::kSteel, Theme::kGraycool, Theme::kBlack, volText, uiOut,
            textOut, "VOLUME:", volVal);
    // G30: a real bar instead of "[####----]". The ASCII meter was a dbgText
    // workaround -- the only way to draw a level in a fixed-cell font -- and
    // ui_draw.h has had pushSegBar() for the HUD's status strip all along.
    // Drawn only on the font path; the dbgText fallback keeps its hashes,
    // since it has nothing better.
    if (textOut) {
        const SDL_Rect vb = rowRect(kRowVolume, kRowColsWide);
        const float barX = (float)vb.x + kBarValueX + 72.0f; // clears the "100%" reading
        const float barW = (float)vb.w - kBarValueX - 86.0f;
        if (barW > 20.0f) {
            pushSegBar(uiOut, barX, (float)vb.y + (float)vb.h * 0.5f - 6.0f, barW, 12.0f,
                       std::max(0.0, std::min(1.0, sel.volume / 100.0)), 10, packColor(Theme::kBlue),
                       packColor(Theme::kBlack, 0.55f));
        }
    }

    // Start stays the bottommost bar (see kRowStart's own comment) but gets
    // the reference's highlighted treatment: a bold red fill with a bright
    // accent edge instead of the other rows' plain steel plate.
    // Centred and sized to its own button. The other rows are left-aligned
    // labels; this one is a button, and its dbgText string carried decorative
    // ">>> <<<" arrows that only centred it by luck of the cell grid.
    if (textOut) {
        drawBar(kRowStart, kStartColsWide, kWhite, Theme::kRed, Theme::kOrange, Theme::kBlack,
                /*text=*/nullptr, uiOut, textOut);
        const SDL_Rect sb = rowRect(kRowStart, kStartColsWide);
        const char* startTxt = "START RACE";
        float size = 21.0f;
        const float avail = (float)sb.w - 24.0f;
        const float w = font::measure(startTxt, size);
        if (w > avail && w > 0.0f) size *= avail / w;
        hudtext::drawCentered(*textOut, (float)sb.x + sb.w * 0.5f,
                              (float)sb.y + sb.h * 0.5f + size * 0.36f, startTxt, size,
                              packColor(Theme::kWhite));
    } else {
        drawBar(kRowStart, kStartColsWide, kWhite, Theme::kRed, Theme::kOrange, Theme::kBlack,
                " >>> START RACE <<< ", uiOut, textOut);
    }
}
