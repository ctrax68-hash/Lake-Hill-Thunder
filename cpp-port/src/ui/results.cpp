#include "results.h"
#include "../render/color.h"
#include "../render/fmt_time.h"
#include "../render/ui_draw.h"

#include <bgfx/bgfx.h>

namespace {

constexpr int kCellW = 8, kCellH = 16;
constexpr int kCol = 1; // matches hud.cpp/menu.cpp/leaderboard.cpp's own column

constexpr int kRowTitle = 1;
constexpr int kRowListStart = 3;

// dbgTextPrintf's _attr byte, same VGA text-mode palette as every other
// dbgText-based screen in this port.
constexpr uint8_t kBlack = 0, kYellow = 14, kWhite = 15;

uint8_t attr(uint8_t fg, uint8_t bg) {
    return (uint8_t)((bg << 4) | fg);
}

SDL_Rect rowRect(int row, int cols) {
    SDL_Rect r;
    r.x = kCol * kCellW;
    r.y = row * kCellH;
    r.w = cols * kCellW;
    r.h = kCellH;
    return r;
}

} // namespace

ResultsRegions computeResultsRegions(int numRows) {
    ResultsRegions r{};
    // One blank row of spacing between the ranked list and the button,
    // same visual gap `menu.cpp`'s own fixed rows use between groups.
    const int backRow = kRowListStart + numRows + 1;
    r.backBtn = rowRect(backRow, 24);
    return r;
}

std::vector<const Car*> buildResultsOrder(const std::vector<Car*>& finishOrder,
                                           const std::vector<const Car*>& order) {
    std::vector<const Car*> result;
    result.reserve(order.size());
    for (Car* c : finishOrder) result.push_back(c);
    for (const Car* c : order) {
        if (!c->done && !c->out) result.push_back(c);
    }
    for (const Car* c : order) {
        if (!c->done && c->out) result.push_back(c);
    }
    return result;
}

void drawResults(const std::vector<const Car*>& resultsOrder, std::vector<PosColorVertex>& uiOut) {
    // index.html:4129-4130: order[0]===S.player -> win banner, else the
    // generic "RACE COMPLETE" title.
    const bool win = !resultsOrder.empty() && resultsOrder[0]->isPlayer;
    bgfx::dbgTextPrintf(kCol, kRowTitle, attr(kBlack, kYellow), "%s",
                        win ? "YOU WIN THE LAKE HILL 400!" : "RACE COMPLETE");

    for (size_t i = 0; i < resultsOrder.size(); ++i) {
        const Car* c = resultsOrder[i];
        const int row = kRowListStart + (int)i;
        const uint8_t textAttr = c->isPlayer ? attr(kBlack, kYellow) : attr(kWhite, kBlack);

        bgfx::dbgTextPrintf(kCol, row, textAttr, "%2d", (int)i + 1);

        const float chipX = (float)(kCol + 3) * kCellW;
        const float chipY = (float)row * kCellH + 3.0f;
        pushQuad(uiOut, chipX, chipY, 8.0f, 10.0f, packColor((float)c->col[0], (float)c->col[1], (float)c->col[2]));

        // index.html:4123: `!c.done && c.out` -> DNF; c.done wins even if
        // c.out is also set (a car that legitimately finished is never a
        // DNF, matching stepCar()'s own finish-check guard).
        const std::string result = (!c->done && c->out) ? "DNF" : fmtLapTime(c->bestLapT);
        bgfx::dbgTextPrintf(kCol + 6, row, textAttr, "#%-3d %-16s %s", c->num, c->name.c_str(), result.c_str());
    }

    const ResultsRegions r = computeResultsRegions((int)resultsOrder.size());
    const int backRow = r.backBtn.y / kCellH;
    bgfx::dbgTextPrintf(kCol, backRow, attr(kBlack, kYellow), " >>> BACK TO MENU <<< ");
}
