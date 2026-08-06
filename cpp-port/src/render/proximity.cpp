#include "proximity.h"
#include "color.h"
#include "ui_draw.h"

#include <bgfx/bgfx.h>

#include <algorithm>
#include <cmath>

namespace {

constexpr float kCellW = 8.0f, kCellH = 16.0f; // dbgText's fixed glyph cell
constexpr uint8_t kWhite = 15, kBlack = 0;     // dbgText VGA palette, matching hud.cpp

uint8_t attr(uint8_t fg, uint8_t bg) {
    return (uint8_t)((bg << 4) | fg);
}

} // namespace

std::vector<ProximityCar> buildProximityList(const std::vector<Car>& cars, const Car& player,
                                              double trackTotal, double sRange, std::size_t maxCars) {
    std::vector<ProximityCar> out;
    if (maxCars == 0 || sRange <= 0.0 || trackTotal <= 0.0) return out;

    for (const Car& c : cars) {
        if (&c == &player) continue;
        if (c.out || c.done) continue;

        // Wrap to the shorter way around, so a car a few metres up the road
        // but across the start/finish line reads as just ahead rather than
        // almost a full lap behind.
        double ds = c.s - player.s;
        ds -= trackTotal * std::round(ds / trackTotal);
        if (std::abs(ds) > sRange) continue;

        out.push_back({c.num, c.col, c.lat - player.lat, ds});
    }

    // Nearest first, so the cap keeps the cars that actually matter.
    std::stable_sort(out.begin(), out.end(), [](const ProximityCar& a, const ProximityCar& b) {
        return std::abs(a.dS) < std::abs(b.dS);
    });
    if (out.size() > maxCars) out.resize(maxCars);

    // Then left-to-right by lateral offset for display: a car on one side of
    // the player should appear on that side of the strip.
    std::stable_sort(out.begin(), out.end(),
                     [](const ProximityCar& a, const ProximityCar& b) { return a.dLat < b.dLat; });
    return out;
}

void drawProximity(const ProximityBox& box, int captionRow, const std::vector<ProximityCar>& near,
                    std::vector<PosColorVertex>& uiOut) {
    pushBevelPanel(uiOut, box.x, box.y, box.w, box.h, packColor(Theme::kBlack, 0.62f),
                   packColor(Theme::kGraycool, 0.5f), packColor(Theme::kSteel, 0.95f));

    // The player's own lane, so the chips either side of it read as
    // "inside me" / "outside me" rather than as an unanchored list.
    const float midX = box.x + box.w / 2.0f;
    pushQuad(uiOut, midX - 1.0f, box.y + 4.0f, 2.0f, box.h - 8.0f, packColor(Theme::kYellow, 0.55f));

    if (near.empty()) return;

    // One slot per car, laid out evenly. Each slot is a colour chip plus the
    // car number printed beside it (leaderboard.cpp's idiom -- a number
    // painted into the chip would vanish against half the liveries).
    //
    // Cars ahead print on `captionRow`, cars behind on the row below, so the
    // strip carries the along-track relationship as well as the lateral one.
    // Chip geometry is placed from the dbgText row rather than from the
    // panel rect, so a chip is always vertically centred on the number it
    // belongs to -- the two coordinate systems agree by construction.
    constexpr float kChipW = 10.0f, kChipH = 14.0f;
    const float slotW = box.w / (float)near.size();
    for (std::size_t i = 0; i < near.size(); ++i) {
        const ProximityCar& p = near[i];
        const bool ahead = p.dS >= 0.0;
        const int row = captionRow + (ahead ? 0 : 1);
        const float slotX = box.x + slotW * (float)i;
        const float chipX = slotX + 6.0f;
        const float chipY = (float)row * kCellH + (kCellH - kChipH) / 2.0f;
        pushQuad(uiOut, chipX, chipY, kChipW, kChipH,
                 packColor((float)p.col[0], (float)p.col[1], (float)p.col[2]));
        bgfx::dbgTextPrintf((int)((chipX + kChipW + 3.0f) / kCellW), row, attr(kWhite, kBlack), "%d", p.num);
    }
}
