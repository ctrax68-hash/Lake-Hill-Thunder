#include "hud.h"
#include "color.h"
#include "fmt_time.h"
#include "gauge_cluster.h"
#include "gear_rpm.h"
#include "leaderboard.h"
#include "minimap.h"
#include "proximity.h"
#include "status_bars.h"
#include "touch_buttons.h"
#include "ui_draw.h"

#include <bgfx/bgfx.h>

#include <algorithm>
#include <cstddef>
#include <string>

namespace {

// dbgTextPrintf's _attr byte: top 4 bits background, bottom 4 foreground,
// standard VGA 16-color text-mode palette (bgfx.h's own doc comment) --
// not the JS THEME's actual RGB values, since this palette is fixed.
constexpr uint8_t kBlack = 0, kGreen = 2, kYellow = 14, kWhite = 15;

constexpr uint8_t attr(uint8_t fg, uint8_t bg) {
    return (uint8_t)((bg << 4) | fg);
}

// G21 (NT2003 presentation plan): dbgText's fixed glyph cell. The HUD now
// mixes dbgText labels with pixel-space panel geometry, so every panel is
// placed on a multiple of kCellH and its labels addressed by row -- that
// way a label always lands inside the plate that frames it, at any window
// size, instead of drifting by up to a cell as the two coordinate systems
// disagree.
constexpr float kCellW = 8.0f, kCellH = 16.0f;

// The dbgText column whose left edge is nearest `x`.
int colAt(float x) { return (int)(x / kCellW); }

// Panels start this far above their first caption row. dbgText can only be
// addressed by whole cells, so without it a caption sits flush against the
// panel's own top bevel and reads as clipped.
constexpr float kPanelPadTop = 6.0f;

// Panel chrome. Deliberately translucent: this HUD sits over a live
// 3D scene (unlike JS's own opaque canvas panels), so an opaque plate
// would black out more of the track than the reference's do.
const uint32_t kPanelFill = packColor(Theme::kBlack, 0.62f);
const uint32_t kPanelLight = packColor(Theme::kGraycool, 0.5f);
const uint32_t kPanelDark = packColor(Theme::kSteel, 0.95f);
// Unlit LCD segments, painted as faint ghosts the way a real segmented
// readout shows its inactive bars.
const uint32_t kSegOff = packColor(Theme::kSteel, 0.55f);

// Draws a seven-segment value right-aligned so its last glyph ends at
// `rightX` -- readouts whose digit count changes (position, lap, lap time)
// stay pinned at one edge instead of growing off-center.
void segValueRightAligned(std::vector<PosColorVertex>& uiOut, float rightX, float y, float cellW,
                          float cellH, float gap, const std::string& text, const float onRgb[3]) {
    const float w = measureSevenSegText(cellW, gap, text);
    pushSevenSegText(uiOut, rightX - w, y, cellW, cellH, gap, text, packColor(onRgb), kSegOff);
}

} // namespace

std::vector<const Car*> computeRaceOrder(const std::vector<Car>& cars) {
    std::vector<const Car*> order;
    order.reserve(cars.size());
    for (auto& c : cars) order.push_back(&c);
    std::stable_sort(order.begin(), order.end(), [](const Car* a, const Car* b) {
        const double ra = a->done ? 1e6 - a->finishT : a->prog;
        const double rb = b->done ? 1e6 - b->finishT : b->prog;
        return ra > rb; // descending, matching race.cpp:339-343 exactly
    });
    return order;
}

void drawHud(const RaceState& state, const std::vector<Car>& cars, std::vector<PosColorVertex>& uiOut,
             const std::vector<std::pair<float, float>>& minimapOutline, float minimapBoundX,
             float minimapBoundY, double trackTotal, int windowW, int windowH) {
    if (state.mode == "menu" || state.mode == "menuwait") return; // index.html:3931

    const Car* player = nullptr;
    for (auto& c : cars) {
        if (c.isPlayer) { player = &c; break; }
    }
    if (!player) return;

    // Race position: the same descending sort key tick() uses to build
    // S.order (race.cpp:339-343, index.html:4192's own `done ? finishT :
    // prog` metric) -- recomputed here purely for display, not a sim
    // decision, so this doesn't touch race.cpp/tick() at all.
    auto rank = [](const Car& c) { return c.done ? 1e6 - c.finishT : c.prog; };
    int pos = 1;
    const double playerRank = rank(*player);
    for (auto& c : cars) {
        if (&c == player) continue;
        if (rank(c) > playerRank) ++pos;
    }

    // index.html:3985-3987: S.finishLaps (not S.laps) is the denominator,
    // since it's the effective final lap and grows during a
    // green-white-checkered extension.
    const int lapNo = player->done
        ? state.finishLaps
        : std::min(state.finishLaps, std::max(1, player->lap < 1 ? 1 : player->lap + 1));

    // ---- G21: top-left minimap ------------------------------------------
    //
    // Phase 4f (PORT_PROGRESS.md): index.html:4059-4101's minimap. G21 moved
    // it from the bottom of the left-hand cascade up into the top-left
    // corner, where the reference keeps its track map, and put a beveled
    // plate behind it (drawMinimap() draws only a thin outline, deliberately
    // -- see its own comment -- so the plate goes down first and its content
    // paints over it; the UI list is one depth-test-free draw call, so
    // painter order is append order).
    const MinimapBox minimapBox = {8.0f, 8.0f, 170.0f, 100.0f};
    pushBevelPanel(uiOut, minimapBox.x, minimapBox.y, minimapBox.w, minimapBox.h, kPanelFill, kPanelLight,
                   kPanelDark);
    drawMinimap(minimapBox, minimapOutline, minimapBoundX, minimapBoundY, cars, state.t, uiOut);

    // ---- G21: left telemetry column -------------------------------------
    //
    // Starts at row 7 (y=112px) so it clears the minimap plate above
    // (8..108px). LAP/POS and the lap times used to live here; G21 moved
    // them into the corner panels. G22 then took SPD and GEAR/RPM out too --
    // the gauge cluster below shows both, and the reference does not print
    // the same number twice -- leaving this column as the state readouts the
    // gauge has no dial for: spotter, flag, and the three status bars.
    constexpr int kRowSpotter = 7, kRowFlag = 8, kRowBars = 9;

    // Phase 6d (PORT_PROGRESS.md): a minimal spotter caption -- index.html:
    // 4040-4046's `spotOn = S.spotT>0 && S.spotTxt` gate, ported verbatim.
    // Not JS's own fading/merged DRAFT-chip presentation (that's a real
    // canvas-drawn UI element this port's dbgText-only HUD has no
    // equivalent of), just enough to make Phase 6b's spotter-message
    // trigger logic visually verifiable (this sandbox has no audio
    // hardware to listen for the matching spotterBlip() with).
    if (state.spotT > 0 && !state.spotTxt.empty()) {
        bgfx::dbgTextPrintf(1, kRowSpotter, attr(kWhite, kBlack), "%s", state.spotTxt.c_str());
    }

    const bool yellow = state.flag == "yellow";
    bgfx::dbgTextPrintf(1, kRowFlag, attr(yellow ? kBlack : kWhite, yellow ? kYellow : kGreen),
                        yellow ? "CAUTION" : "GREEN  ");

    // Phase 4e (PORT_PROGRESS.md): index.html:3999-4020's segmented TIRE/
    // FUEL/CAR status strip -- the first HUD feature needing real quad
    // geometry (drawStatusBars() appends into `uiOut`; Renderer submits it
    // as a separate UI-overlay view after this function returns).
    drawStatusBars(*player, kRowBars, uiOut);

    // ---- G21: bottom-left panel geometry --------------------------------
    //
    // Resolved before the leaderboard is drawn, because on a short window
    // the two share the left edge and the leaderboard is what has to give.
    // The caption row is snapped to the dbgText grid so both captions land
    // inside the plate; the plate then starts kPanelPadTop above that row,
    // and clears the steer buttons below it (computeTouchRegions() puts
    // bL/bR at windowH-90px).
    constexpr float kLapPanelW = 232.0f, kLapPanelH = 96.0f;
    const int lapPanelRow = std::max(0, (int)(((float)windowH - 102.0f - kLapPanelH) / kCellH));
    const float lapPanelY = (float)lapPanelRow * kCellH;
    const float lapPanelTop = lapPanelY - kPanelPadTop;

    // Phase 4g (PORT_PROGRESS.md): index.html:3939-3978's leaderboard panel
    // (rank, color chip, name/tag, live gap), placed directly below the
    // status bars above (which end at row 13 / y=224px).
    //
    // G21: trimmed to the rows that actually fit above the lap-time panel.
    // At 1280x720 there is room for the full list, but a short window (an
    // 860x480 browser tab, say) has the two overlapping otherwise. Rows are
    // dropped off the bottom of the top-N block, never the pinned player row
    // -- that row is the one the player is actually looking for, and it is
    // also what carries the divider that makes the list readable.
    const std::vector<const Car*> order = computeRaceOrder(cars);
    std::vector<LeaderboardRow> lbRows =
        buildLeaderboardRows(order, state.mode, state.flag, state.t, trackTotal);
    const float lbY = (float)(kRowBars + 3) * kCellH + 8.0f;
    const int lbMaxRows = (int)((lapPanelTop - 8.0f - lbY - kCellH) / kCellH);
    if (lbMaxRows <= 0) {
        lbRows.clear();
    } else if ((int)lbRows.size() > lbMaxRows) {
        if (lbRows.back().dividerBefore) { // the pinned player row
            const LeaderboardRow pinned = lbRows.back();
            lbRows.resize((size_t)lbMaxRows - 1);
            lbRows.push_back(pinned);
        } else {
            lbRows.resize((size_t)lbMaxRows);
        }
    }
    if (!lbRows.empty()) {
        const LeaderboardBox lbBox = {8.0f, lbY, 248.0f, 16.0f + 16.0f * (float)lbRows.size()};
        drawLeaderboard(lbBox, lbRows, state.flag == "yellow", uiOut);
    }

    // ---- G21: top-right POS / LAP panel ---------------------------------
    //
    // Row 4 (y=64px) clears the CAM button above it (computeTouchRegions()
    // puts bC at y=14..54). Values are seven-segment (ui_draw.h) rather than
    // dbgText: the reference's corner readouts are segmented displays, and
    // dbgText can't be scaled off its fixed 8x16 cell, so a headline number
    // drawn with it would read the same size as its own caption.
    {
        constexpr float kPanelW = 216.0f, kPanelH = 64.0f;
        constexpr int kPanelRow = 4;
        const float px = (float)windowW - 8.0f - kPanelW;
        const float py = (float)kPanelRow * kCellH;
        pushBevelPanel(uiOut, px, py - kPanelPadTop, kPanelW, kPanelH + kPanelPadTop, kPanelFill, kPanelLight,
                       kPanelDark);

        bgfx::dbgTextPrintf(colAt(px + 12.0f), kPanelRow, attr(kWhite, kBlack), "POS");
        bgfx::dbgTextPrintf(colAt(px + 112.0f), kPanelRow, attr(kWhite, kBlack), "LAP");

        const float valueY = py + 20.0f;
        segValueRightAligned(uiOut, px + 92.0f, valueY, 16.0f, 30.0f, 4.0f, std::to_string(pos),
                             Theme::kWhite);
        segValueRightAligned(uiOut, px + kPanelW - 12.0f, valueY, 12.0f, 30.0f, 3.0f,
                             std::to_string(lapNo) + "/" + std::to_string(state.finishLaps),
                             yellow ? Theme::kYellow : Theme::kWhite);
    }

    // ---- G21: bottom-left LAP TIME / BEST panel --------------------------
    //
    // Phase 4c (PORT_PROGRESS.md): index.html:3990-3996's LAST/BEST lap time
    // strip, ported via fmtLapTime() (fmt_time.h -- a direct port of JS's
    // fmtT()). Car::lastLapT/bestLapT are already set correctly by
    // step_car.cpp (Phase 1's ported physics), so this is purely a rendering
    // addition. G21 moved it out of the left dbgText column into its own
    // plate above the bottom-left steer buttons (bL/bR start at
    // windowH-90px), where the reference puts its timing block, and switched
    // the values to seven-segment. fmtLapTime()'s two outputs -- "1:23.45"
    // and the "--:--.--" placeholder -- are both wholly expressible by
    // pushSevenSegText() ('0'-'9', ':', '.', '-').
    {
        constexpr float px = 8.0f;
        pushBevelPanel(uiOut, px, lapPanelTop, kLapPanelW, kLapPanelH + kPanelPadTop, kPanelFill, kPanelLight,
                       kPanelDark);

        bgfx::dbgTextPrintf(colAt(px + 8.0f), lapPanelRow, attr(kWhite, kBlack), "LAP TIME");
        segValueRightAligned(uiOut, px + kLapPanelW - 12.0f, lapPanelY + 18.0f, 13.0f, 22.0f, 3.0f,
                             fmtLapTime(player->lastLapT), Theme::kWhite);

        bgfx::dbgTextPrintf(colAt(px + 8.0f), lapPanelRow + 3, attr(kWhite, kBlack), "BEST");
        segValueRightAligned(uiOut, px + kLapPanelW - 12.0f, lapPanelY + 66.0f, 13.0f, 22.0f, 3.0f,
                             fmtLapTime(player->bestLapT), Theme::kYellow);
    }

    // ---- G22: bottom gauge cluster and proximity strip -------------------
    //
    // The bottom edge between the two touch-control groups is the only band
    // wide enough for the reference's gauge cluster: the steer pair ends at
    // x=200 (computeTouchRegions()), and the brake/gas/pit group starts at
    // windowW-200. The cluster takes the right of that band -- as close as
    // this layout can get to the reference's bottom-right tachometer without
    // sitting under the throttle button -- and the proximity strip takes
    // what is left to its own left, centred in the gap.
    //
    // gearRpm() is a pure function of Car::v (gear_rpm.h), not stored physics
    // state, so the dial cannot disagree with the car it reports on.
    {
        constexpr float kBandEdge = 208.0f; // touch-button groups + 8px clearance
        constexpr float kGaugeW = 260.0f, kGaugeH = 136.0f;
        const GearRpm gr = gearRpm(player->v);

        const int gaugeRow = std::max(0, (int)(((float)windowH - 16.0f - kGaugeH + kPanelPadTop) / kCellH));
        const float gaugeTop = (float)gaugeRow * kCellH - kPanelPadTop;
        const float gaugeX = (float)windowW - kBandEdge - kGaugeW;

        // Everything here needs the band to actually be wide enough to hold
        // it; on a narrow window there is no room between the two button
        // groups and the whole cluster is dropped rather than drawn over
        // them.
        if (gaugeX >= kBandEdge) {
            const GaugeBox gaugeBox = {gaugeX, gaugeTop, kGaugeW, kGaugeH};
            drawGaugeCluster(gaugeBox, gaugeRow, gr.rpm, gr.gear, player->v, player->draftF, uiOut);

            // Proximity strip: centred in what is left of the band, capped so
            // it stays a compact strip on a wide window instead of stretching
            // the chips apart.
            constexpr float kProxMaxW = 320.0f, kProxH = 44.0f;
            const float gapL = kBandEdge, gapR = gaugeX - 12.0f;
            const float proxW = std::min(kProxMaxW, gapR - gapL);
            if (proxW >= 120.0f) {
                const int proxRow = std::max(0, (int)(((float)windowH - 16.0f - kProxH + kPanelPadTop) / kCellH));
                const ProximityBox proxBox = {gapL + (gapR - gapL - proxW) / 2.0f,
                                              (float)proxRow * kCellH - kPanelPadTop, proxW, kProxH};
                // A chip plus a two-digit number needs ~56px of slot to stay
                // clear of its neighbour, so the number of cars shown follows
                // the width actually available rather than being fixed at 5
                // and running together on a narrow window.
                const std::size_t maxChips =
                    (std::size_t)std::clamp((int)(proxW / 56.0f), 2, 5);
                const std::vector<ProximityCar> nearby =
                    buildProximityList(cars, *player, trackTotal, /*sRange=*/30.0, maxChips);
                drawProximity(proxBox, proxRow, nearby, uiOut);
            }
        }
    }

    // Roadmap Phase 0: steer/brake/gas/pit buttons, finally visible --
    // computeTouchRegions() is the exact same region math main.cpp already
    // hit-tests pointer/touch input against, so this can't drift out of
    // sync with what's actually clickable.
    drawTouchButtons(computeTouchRegions(windowW, windowH), uiOut);
}
