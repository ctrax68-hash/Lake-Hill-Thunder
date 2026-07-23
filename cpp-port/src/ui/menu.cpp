#include "menu.h"

#include <bgfx/bgfx.h>

#include <algorithm>
#include <cstdio>

namespace {

// bgfx's built-in debug-text overlay is a fixed 8x16-cell monospace grid
// (same fact hud.cpp's own comment documents) -- these constants translate
// text row/column positions into the pixel rects computeMenuRegions()
// returns, so the clickable area always lines up with what drawMenu()
// prints, without hand-duplicating pixel numbers in two places.
constexpr int kCellW = 8, kCellH = 16;
constexpr int kCol = 1; // all menu rows start at column 1, matching hud.cpp

constexpr int kRowTitle = 1;
constexpr int kRowTrack = 3;
constexpr int kRowLaps = 4;
constexpr int kRowQual = 5;
constexpr int kRowSound = 6;
constexpr int kRowTilt = 7;
constexpr int kRowVolume = 9;
constexpr int kRowStart = 11;

// Generous click width in character cells -- covers the full label+value
// text with room to spare, not a precise glyph-bounding hit test (matching
// touch_controls.h's own "reasonable first pass" precedent for fixed
// regions rather than pixel-exact ones).
constexpr int kRowColsWide = 40;
constexpr int kStartColsWide = 24;

SDL_Rect rowRect(int row, int cols) {
    SDL_Rect r;
    r.x = kCol * kCellW;
    r.y = row * kCellH;
    r.w = cols * kCellW;
    r.h = kCellH;
    return r;
}

// dbgTextPrintf's _attr byte, same VGA text-mode palette as hud.cpp.
constexpr uint8_t kBlack = 0, kGreen = 2, kYellow = 14, kWhite = 15, kGrey = 7;

constexpr uint8_t attr(uint8_t fg, uint8_t bg) {
    return (uint8_t)((bg << 4) | fg);
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

int volumeFromClickX(const SDL_Rect& bar, int clickX) {
    if (bar.w <= 0) return 0;
    double t = (double)(clickX - bar.x) / (double)bar.w;
    t = std::max(0.0, std::min(1.0, t));
    return (int)std::lround(t * 100.0);
}

void drawMenu(const MenuSelection& sel, int laps, bool tilt, const std::string& trackName) {
    bgfx::dbgTextPrintf(kCol, kRowTitle, attr(kYellow, kBlack), "LAKE HILL THUNDER");

    bgfx::dbgTextPrintf(kCol, kRowTrack, attr(kWhite, kBlack), "TRACK:      %-20s", trackName.c_str());
    bgfx::dbgTextPrintf(kCol, kRowLaps, attr(kWhite, kBlack), "LAPS:       %-3d", laps);
    // index.html's #qualTog exists, but this port doesn't have a real
    // qualifying flow yet (see MenuSelection::qual's own comment) -- grey
    // rather than white, a small honest visual cue that this row currently
    // has no gameplay effect.
    bgfx::dbgTextPrintf(kCol, kRowQual, attr(kGrey, kBlack), "QUALIFYING: %-3s", sel.qual ? "ON" : "OFF");
    bgfx::dbgTextPrintf(kCol, kRowSound, attr(kGrey, kBlack), "SOUND:      %-3s", sel.sound ? "ON" : "OFF");
    bgfx::dbgTextPrintf(kCol, kRowTilt, attr(kWhite, kBlack), "TILT STEER: %-3s", tilt ? "ON" : "OFF");

    char bar[21];
    const int filled = std::max(0, std::min(20, (sel.volume * 20) / 100));
    for (int i = 0; i < 20; ++i) bar[i] = i < filled ? '#' : '-';
    bar[20] = '\0';
    bgfx::dbgTextPrintf(kCol, kRowVolume, attr(kGrey, kBlack), "VOLUME: [%s] %3d%%", bar, sel.volume);

    bgfx::dbgTextPrintf(kCol, kRowStart, attr(kBlack, kGreen), " >>> START RACE <<< ");
}
