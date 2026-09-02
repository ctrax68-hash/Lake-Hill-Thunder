#include "hud.h"
#include "color.h"
#include "fmt_time.h"
#include "gauge_cluster.h"
#include "hud_text.h"
#include "gear_rpm.h"
#include "leaderboard.h"
#include "minimap.h"
#include "proximity.h"
#include "status_bars.h"
#include "ticker.h"
#include "touch_buttons.h"
#include "ui_draw.h"

#include <algorithm>
#include <cstddef>
#include <string>

namespace {

// G27: the 16px grid the dbgText era imposed, kept as a plain pixel pitch.
// The HUD's panel placement is expressed in multiples of it and several
// modules measure against each other through it (the leaderboard's height
// against the status strip, for one), so it stays -- but nothing is snapped
// to it any more, and no label is addressed by row.
constexpr float kRowH = 16.0f;

// G27: the broadcast ticker owns the top strip, so every panel that used to
// start at the very top clears it. Must match touch_controls.cpp's own
// kTickerH, which pushes the CAM button down by the same amount -- see its
// comment for why that constant is duplicated rather than shared.
constexpr float kTickerH = 24.0f;

// Inner padding between a panel's top bevel and its first caption.
constexpr float kPanelPadTop = 6.0f;

// G27: the ordinal suffix for a race position, set as a superscript beside
// the big numeral -- the reference's signature HUD element ("15th"). English
// ordinals are irregular in exactly two ways (11/12/13 take "th" despite
// ending in 1/2/3, everything else follows the last digit), and getting that
// wrong is the kind of thing nobody notices until the player is running 12th.
const char* ordinalSuffix(int n) {
    const int lastTwo = n % 100;
    if (lastTwo >= 11 && lastTwo <= 13) return "th";
    switch (n % 10) {
        case 1: return "st";
        case 2: return "nd";
        case 3: return "rd";
        default: return "th";
    }
}

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
             std::vector<PosColorUvVertex>& textOut,
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
    const MinimapBox minimapBox = {8.0f, kTickerH + 6.0f, 170.0f, 100.0f};
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
    constexpr float kColX = 8.0f;
    constexpr float kFlagY = kTickerH + 6.0f + 100.0f + 12.0f; // clears the minimap plate
    constexpr float kBarsY = kFlagY + 24.0f;

    // Phase 6d (PORT_PROGRESS.md): a minimal spotter caption -- index.html:
    // 4040-4046's `spotOn = S.spotT>0 && S.spotTxt` gate, ported verbatim.
    // Not JS's own fading/merged DRAFT-chip presentation (that's a real
    // canvas-drawn UI element this port's dbgText-only HUD has no
    // equivalent of), just enough to make Phase 6b's spotter-message
    // trigger logic visually verifiable (this sandbox has no audio
    // hardware to listen for the matching spotterBlip() with).
    // G27: promoted from a line of terminal text in the left column to the
    // reference's treatment -- large outlined type across the middle of the
    // screen. A spotter call is the game shouting at the player mid-corner;
    // printed small in a corner it was routinely missed, which is most of why
    // it never felt like a spotter. Outlined rather than shadowed because it
    // lands anywhere on the scene (see hud_text.h), and placed above centre so
    // it never covers the car.
    if (state.spotT > 0 && !state.spotTxt.empty()) {
        hudtext::drawOutlined(textOut, (float)windowW * 0.5f, (float)windowH * 0.34f, state.spotTxt,
                              34.0f, packColor(Theme::kWhite));
    }

    // Flag state, as a real chip: dbgText could only invert one cell's
    // background per glyph, so this was a colour-attribute run with trailing
    // spaces padding it out ("GREEN  "). It is a quad now, sized to the word.
    const bool yellow = state.flag == "yellow";
    {
        const char* flagTxt = yellow ? "CAUTION" : "GREEN";
        const float w = font::measure(flagTxt, hudtext::kBody) + 16.0f;
        pushQuad(uiOut, kColX, kFlagY, w, 18.0f,
                 packColor(yellow ? Theme::kYellow : Theme::kGreen, 0.9f));
        font::pushText(textOut, kColX + 8.0f, kFlagY + 13.0f, flagTxt, hudtext::kBody,
                       packColor(Theme::kBlack));
    }

    // N3: while c.pit is nonzero the pit branch in stepCar() owns the car --
    // it steers itself down pit road and is speed-limited to 22 m/s (49 mph).
    // Nothing on screen used to say so, so a player who had tapped PIT (which
    // sits just above BRAKE) experienced it as the car mysteriously refusing
    // to accelerate past 50 and pulling toward the infield, with no way to
    // tell whether the game was broken or they had done something. Reported
    // as exactly that. The state is now named, and so is the way out.
    {
        const char* pitTxt = nullptr;
        if (player->pit > 0) {
            pitTxt = player->pit == 2    ? "PIT STALL - SERVICING"
                     : player->dtPending ? "DRIVE-THROUGH PENALTY - 49 MPH"
                     : player->pit == 1  ? "PIT ROAD - 49 MPH  (PIT AGAIN TO CANCEL)"
                                         : "PIT ROAD - 49 MPH";
        } else if (player->pitReq) {
            // Armed but not yet committed -- stepCar() promotes this to c.pit
            // at the frontstretch. Surfacing it is what makes an accidental
            // tap recoverable BEFORE it costs anything.
            pitTxt = "PIT REQUESTED (PIT AGAIN TO CANCEL)";
        }
        if (pitTxt) {
            const float bx = kColX + 96.0f;
            const float w = font::measure(pitTxt, hudtext::kBody) + 16.0f;
            pushQuad(uiOut, bx, kFlagY, w, 18.0f, packColor(Theme::kOrange, 0.92f));
            font::pushText(textOut, bx + 8.0f, kFlagY + 13.0f, pitTxt, hudtext::kBody,
                           packColor(Theme::kBlack));
        }
    }

    // Phase 4e (PORT_PROGRESS.md): index.html:3999-4020's segmented TIRE/
    // FUEL/CAR status strip -- the first HUD feature needing real quad
    // geometry (drawStatusBars() appends into `uiOut`; Renderer submits it
    // as a separate UI-overlay view after this function returns).
    drawStatusBars(*player, kBarsY, uiOut, textOut);

    // ---- G21: bottom-left panel geometry --------------------------------
    //
    // Resolved before the leaderboard is drawn, because on a short window
    // the two share the left edge and the leaderboard is what has to give.
    // Clears the steer buttons below it (computeTouchRegions() puts bL/bR at
    // windowH-90px). G27 dropped the row snapping: the captions are placed
    // inside the plate directly now, so the plate no longer has to sit on a
    // 16px boundary for them to line up with it.
    constexpr float kLapPanelW = 232.0f, kLapPanelH = 96.0f;
    const float lapPanelY = std::max(0.0f, (float)windowH - 102.0f - kLapPanelH);
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
    // P1 (NT2003 engine-feel plan, the loose/tight axis): +5, not +3 --
    // drawStatusBars() now draws TIR-F/TIR-R/BAL/FUEL/CAR (5 rows) instead
    // of TIRE/FUEL/CAR (3), see status_bars.h's own comment.
    // Below the five status-bar rows, which start at kBarsY on a 16px pitch.
    const float lbY = kBarsY + kRowH * 5.0f + 8.0f;
    const int lbMaxRows = (int)((lapPanelTop - 8.0f - lbY - kRowH) / kRowH);
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
        const LeaderboardBox lbBox = {8.0f, lbY, 248.0f, kRowH * (1.0f + (float)lbRows.size())};
        drawLeaderboard(lbBox, lbRows, state.flag == "yellow", uiOut, textOut);
    }

    // ---- G21: top-right POS / LAP panel ---------------------------------
    //
    // y=64px clears the CAM button above it (computeTouchRegions() puts bC at
    // y=14..54).
    //
    // G27: the position is now the reference's headline element -- a large
    // numeral with its ordinal set small and raised beside it ("15th"), then
    // "OF 20" underneath. This is the single most-read number on the screen
    // and it was previously a seven-segment glyph run the same size as the lap
    // counter next to it, with a "POS" caption to explain which was which. An
    // ordinal needs letters, which is precisely what the old digit rasteriser
    // could not draw -- ui_draw.h's pushSevenSegText() knows `0-9 : . - /` and
    // nothing else, so this shape of readout was unreachable until G26.
    //
    // The lap counter keeps its seven-segment treatment: it genuinely reads as
    // a mechanical counter, and it is the one place a segmented display is
    // right rather than a workaround.
    {
        constexpr float kPanelW = 216.0f, kPanelH = 72.0f;
        const float px = (float)windowW - 8.0f - kPanelW;
        const float py = kTickerH + 6.0f + 46.0f + 18.0f;
        pushBevelPanel(uiOut, px, py - kPanelPadTop, kPanelW, kPanelH + kPanelPadTop, kPanelFill, kPanelLight,
                       kPanelDark);

        // Position: numeral + raised ordinal, laid out as one unit so the
        // suffix always hugs the number no matter how wide the number is.
        constexpr float kPosSize = 44.0f, kOrdSize = 18.0f;
        const std::string posTxt = std::to_string(pos);
        const float posBaseline = py + 34.0f;
        hudtext::draw(textOut, px + 14.0f, posBaseline, posTxt, kPosSize, packColor(Theme::kWhite));
        hudtext::draw(textOut, px + 14.0f + font::measure(posTxt, kPosSize) + 2.0f,
                      posBaseline - kPosSize * 0.42f, ordinalSuffix(pos), kOrdSize,
                      packColor(Theme::kYellow));
        hudtext::draw(textOut, px + 14.0f, py + 56.0f, "OF " + std::to_string((int)cars.size()),
                      hudtext::kCaption, packColor(Theme::kGraycool));

        hudtext::caption(textOut, px + 112.0f, py - 2.0f, "LAP");
        segValueRightAligned(uiOut, px + kPanelW - 12.0f, py + 20.0f, 12.0f, 28.0f, 3.0f,
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

        hudtext::caption(textOut, px + 8.0f, lapPanelY - 2.0f, "LAP TIME");
        segValueRightAligned(uiOut, px + kLapPanelW - 12.0f, lapPanelY + 18.0f, 13.0f, 22.0f, 3.0f,
                             fmtLapTime(player->lastLapT), Theme::kWhite);

        hudtext::caption(textOut, px + 8.0f, lapPanelY + 46.0f, "BEST");
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

        const float gaugeTop = std::max(0.0f, (float)windowH - 16.0f - kGaugeH);
        const float gaugeX = (float)windowW - kBandEdge - kGaugeW;

        // Everything here needs the band to actually be wide enough to hold
        // it; on a narrow window there is no room between the two button
        // groups and the whole cluster is dropped rather than drawn over
        // them.
        if (gaugeX >= kBandEdge) {
            const GaugeBox gaugeBox = {gaugeX, gaugeTop, kGaugeW, kGaugeH};
            drawGaugeCluster(gaugeBox, gr.rpm, gr.gear, player->v, player->draftF, uiOut, textOut);

            // Proximity strip: centred in what is left of the band, capped so
            // it stays a compact strip on a wide window instead of stretching
            // the chips apart.
            constexpr float kProxMaxW = 320.0f, kProxH = 44.0f;
            const float gapL = kBandEdge, gapR = gaugeX - 12.0f;
            const float proxW = std::min(kProxMaxW, gapR - gapL);
            if (proxW >= 120.0f) {
                const ProximityBox proxBox = {gapL + (gapR - gapL - proxW) / 2.0f,
                                              std::max(0.0f, (float)windowH - 16.0f - kProxH), proxW,
                                              kProxH};
                // A chip plus a two-digit number needs ~56px of slot to stay
                // clear of its neighbour, so the number of cars shown follows
                // the width actually available rather than being fixed at 5
                // and running together on a narrow window.
                const std::size_t maxChips =
                    (std::size_t)std::clamp((int)(proxW / 56.0f), 2, 5);
                const std::vector<ProximityCar> nearby =
                    buildProximityList(cars, *player, trackTotal, /*sRange=*/30.0, maxChips);
                drawProximity(proxBox, nearby, uiOut, textOut);
            }
        }
    }

    // ---- G27: the broadcast ticker ---------------------------------------
    //
    // Drawn last of the readouts but occupying the top strip, so it reads as
    // a broadcast overlay laid over the game rather than as another panel in
    // the HUD's own cascade. `order` is the leaderboard's own race-order sort,
    // reused rather than recomputed -- the two must never disagree about who
    // is running where.
    drawTicker({0.0f, 0.0f, (float)windowW, kTickerH}, buildTickerEntries(order), state.t, uiOut,
               textOut);

    // Roadmap Phase 0: steer/brake/gas/pit buttons, finally visible --
    // computeTouchRegions() is the exact same region math main.cpp already
    // hit-tests pointer/touch input against, so this can't drift out of
    // sync with what's actually clickable.
    drawTouchButtons(computeTouchRegions(windowW, windowH), uiOut, textOut);
}
