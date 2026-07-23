#include "minimap.h"
#include "color.h"
#include "ui_draw.h"

#include <algorithm>
#include <cmath>

void drawMinimap(const MinimapBox& box, const std::vector<std::pair<float, float>>& outline,
                  float boundX, float boundY, const std::vector<Car>& cars, double simT,
                  std::vector<PosColorVertex>& uiOut) {
    if (outline.empty()) return;

    // index.html:4067's panel() border -- an unfilled outline here (this
    // port's HUD sits over the still-rendering track/cars the same way
    // JS's own semi-transparent panel does, so a filled rect would just
    // hide what's underneath it unnecessarily).
    const uint32_t whiteAbgr = packColor(Theme::kWhite);
    {
        const std::vector<std::pair<float, float>> rectPts = {
            {box.x, box.y}, {box.x + box.w, box.y}, {box.x + box.w, box.y + box.h}, {box.x, box.y + box.h}};
        pushPolyline(uiOut, rectPts, 1.0f, whiteAbgr, true);
    }

    // index.html:4068's scale-to-fit + centered origin.
    const float sc = std::min((box.w - 18.0f) / 2.0f / boundX, (box.h - 14.0f) / 2.0f / boundY);
    const float ox = box.x + box.w / 2.0f, oy = box.y + box.h / 2.0f;

    std::vector<std::pair<float, float>> trackPts;
    trackPts.reserve(outline.size());
    for (auto& p : outline) trackPts.push_back({ox + p.first * sc, oy + p.second * sc});
    pushPolyline(uiOut, trackPts, 2.0f, packColor(Theme::kGraycool), true);

    const uint32_t yellowAbgr = packColor(Theme::kYellow);

    for (auto& c : cars) {
        const float X = ox + (float)c.x * sc, Y = oy + (float)c.y * sc;

        // index.html:4073-4079's pulsing trouble ring.
        if (c.spinT > 0 || c.blown || c.dmg > 0.6) {
            const double pulse = 0.5 + 0.5 * std::sin(simT * 6.0);
            const float alpha = (float)(0.35 + 0.35 * pulse);
            const float radius = (float)((c.isPlayer ? 3.4 : 2.4) + 2.5 + pulse * 1.5);
            pushRingOutline(uiOut, X, Y, radius, 1.5f, packColor(Theme::kRed, alpha));
        }

        if (c.isPlayer) {
            // index.html:4080-4092's directional wedge -- heading is
            // already in the same world-space convention as the track
            // outline above, so no extra rotation is needed here, only the
            // wedge's own fixed pixel size (not scaled by `sc`, matching
            // JS exactly).
            const float hx = (float)std::cos(c.hdg), hy = (float)std::sin(c.hdg);
            const float px = -hy, py = hx;
            constexpr float tipR = 5.5f, backR = 3.0f, halfW = 2.4f;
            pushTriangle(uiOut, X + hx * tipR, Y + hy * tipR, X - hx * backR + px * halfW,
                         Y - hy * backR + py * halfW, X - hx * backR - px * halfW, Y - hy * backR - py * halfW,
                         yellowAbgr);
        } else {
            const uint32_t carAbgr = packColor((float)c.col[0], (float)c.col[1], (float)c.col[2]);
            pushFilledCircle(uiOut, X, Y, 2.4f, carAbgr);
        }
    }
}
