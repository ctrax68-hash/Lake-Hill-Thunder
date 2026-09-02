#include "proximity.h"
#include "color.h"
#include "hud_text.h"
#include "ui_draw.h"

#include <algorithm>
#include <cmath>

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

void drawProximity(const ProximityBox& box, const std::vector<ProximityCar>& near,
                   std::vector<PosColorVertex>& uiOut, std::vector<PosColorUvVertex>& textOut) {
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
    // Cars ahead sit on the upper line, cars behind on the lower, so the strip
    // carries the along-track relationship as well as the lateral one. G27
    // places both from the panel rect: the two lines used to be dbgText rows,
    // which meant the chips had to be positioned off that same text grid to
    // stay aligned with their numbers. With the number free to sit at any
    // pixel, the chip and its label are simply centred on one line together.
    constexpr float kChipW = 10.0f, kChipH = 14.0f;
    constexpr float kLineH = 18.0f;
    const float lineTop = box.y + (box.h - kLineH * 2.0f) / 2.0f;
    const float slotW = box.w / (float)near.size();
    for (std::size_t i = 0; i < near.size(); ++i) {
        const ProximityCar& p = near[i];
        const bool ahead = p.dS >= 0.0;
        const float rowY = lineTop + (ahead ? 0.0f : kLineH);
        const float slotX = box.x + slotW * (float)i;
        const float chipX = slotX + 6.0f;
        pushQuad(uiOut, chipX, rowY + (kLineH - kChipH) / 2.0f, kChipW, kChipH,
                 packColor((float)p.col[0], (float)p.col[1], (float)p.col[2]));
        hudtext::draw(textOut, chipX + kChipW + 4.0f, rowY + 13.0f, std::to_string(p.num),
                      hudtext::kBody, packColor(Theme::kWhite));
    }
}
