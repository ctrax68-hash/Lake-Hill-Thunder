#include "results.h"
#include "../render/color.h"
#include "../render/hud_text.h"
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

void drawResults(const std::vector<const Car*>& resultsOrder, std::vector<PosColorVertex>& uiOut,
                 std::vector<PosColorUvVertex>* textOut) {
    // index.html:4129-4130: order[0]===S.player -> win banner, else the
    // generic "RACE COMPLETE" title.
    const bool win = !resultsOrder.empty() && resultsOrder[0]->isPlayer;
    const char* title = win ? "YOU WIN THE LAKE HILL 400!" : "RACE COMPLETE";

    // G30: the last screen still drawn in the terminal font. Same treatment
    // the HUD got in G27 -- real banners as quads rather than per-glyph
    // background attributes, and columns right-aligned so a proportional font
    // does not leave the list ragged. The dbgText path stays as the fallback
    // for a failed atlas decode, exactly as drawMenu()'s does.
    if (!textOut) {
        bgfx::dbgTextPrintf(kCol, kRowTitle, attr(kBlack, kYellow), "%s", title);
        for (size_t i = 0; i < resultsOrder.size(); ++i) {
            const Car* c = resultsOrder[i];
            const int row = kRowListStart + (int)i;
            const uint8_t ta = c->isPlayer ? attr(kBlack, kYellow) : attr(kWhite, kBlack);
            bgfx::dbgTextPrintf(kCol, row, ta, "%2d", (int)i + 1);
            const std::string res = (!c->done && c->out) ? "DNF" : fmtLapTime(c->bestLapT);
            bgfx::dbgTextPrintf(kCol + 6, row, ta, "#%-3d %-16s %s", c->num, c->name.c_str(), res.c_str());
        }
        const ResultsRegions rr = computeResultsRegions((int)resultsOrder.size());
        bgfx::dbgTextPrintf(kCol, rr.backBtn.y / kCellH, attr(kBlack, kYellow), " >>> BACK TO MENU <<< ");
        return;
    }

    constexpr float kListW = 430.0f;
    const float x0 = kCol * kCellW;

    // Title banner.
    {
        const float ty = kRowTitle * kCellH;
        const float w = font::measure(title, hudtext::kValue) + 24.0f;
        pushQuad(uiOut, x0, ty, std::max(w, kListW), 34.0f, packColor(Theme::kYellow, 0.94f));
        font::pushText(*textOut, x0 + 12.0f, ty + 25.0f, title, hudtext::kValue, packColor(Theme::kBlack));
    }

    for (size_t i = 0; i < resultsOrder.size(); ++i) {
        const Car* c = resultsOrder[i];
        const float rowY = (float)(kRowListStart + (int)i) * kCellH;
        const float baseline = rowY + 12.0f;
        if (c->isPlayer) pushQuad(uiOut, x0, rowY, kListW, kCellH, packColor(Theme::kYellow, 0.88f));
        const uint32_t fg = c->isPlayer ? packColor(Theme::kBlack) : packColor(Theme::kWhite);

        hudtext::drawRight(*textOut, x0 + 26.0f, baseline, std::to_string(i + 1), hudtext::kBody, fg);
        pushQuad(uiOut, x0 + 34.0f, rowY + 3.0f, 9.0f, 10.0f,
                 packColor((float)c->col[0], (float)c->col[1], (float)c->col[2]));
        hudtext::draw(*textOut, x0 + 50.0f, baseline, "#" + std::to_string(c->num), hudtext::kBody, fg);
        hudtext::draw(*textOut, x0 + 96.0f, baseline, c->name, hudtext::kBody, fg);
        // index.html:4123: `!c.done && c.out` -> DNF; c.done wins even if
        // c.out is also set (a car that legitimately finished is never a
        // DNF, matching stepCar()'s own finish-check guard).
        const std::string result = (!c->done && c->out) ? "DNF" : fmtLapTime(c->bestLapT);
        hudtext::drawRight(*textOut, x0 + kListW - 10.0f, baseline, result, hudtext::kBody, fg);
    }

    const ResultsRegions r = computeResultsRegions((int)resultsOrder.size());
    pushQuad(uiOut, (float)r.backBtn.x, (float)r.backBtn.y, (float)r.backBtn.w, (float)r.backBtn.h,
             packColor(Theme::kYellow, 0.94f));
    // Sized to fit the button rather than assumed to: computeResultsRegions()
    // measures the button in dbgText cells, so its width is not chosen with
    // this font in mind and the label overflowed it at kBody.
    {
        const char* back = "BACK TO MENU";
        float size = hudtext::kBody;
        const float avail = (float)r.backBtn.w - 16.0f;
        const float w = font::measure(back, size);
        if (w > avail && w > 0.0f) size *= avail / w;
        hudtext::drawCentered(*textOut, (float)r.backBtn.x + r.backBtn.w * 0.5f,
                              (float)r.backBtn.y + r.backBtn.h * 0.5f + size * 0.36f, back, size,
                              packColor(Theme::kBlack));
    }
}
