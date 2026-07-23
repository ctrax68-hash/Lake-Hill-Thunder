#include "hud.h"
#include "fmt_time.h"
#include "gear_rpm.h"
#include "leaderboard.h"
#include "minimap.h"
#include "status_bars.h"

#include <bgfx/bgfx.h>

#include <algorithm>

namespace {

// dbgTextPrintf's _attr byte: top 4 bits background, bottom 4 foreground,
// standard VGA 16-color text-mode palette (bgfx.h's own doc comment) --
// not the JS THEME's actual RGB values, since this palette is fixed.
constexpr uint8_t kBlack = 0, kGreen = 2, kYellow = 14, kWhite = 15;

constexpr uint8_t attr(uint8_t fg, uint8_t bg) {
    return (uint8_t)((bg << 4) | fg);
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
             float minimapBoundY, double trackTotal) {
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
    bgfx::dbgTextPrintf(1, 1, attr(kWhite, kBlack), "LAP %d / %d", lapNo, state.finishLaps);
    bgfx::dbgTextPrintf(1, 2, attr(kWhite, kBlack), "POS %d / %d", pos, (int)cars.size());

    const bool yellow = state.flag == "yellow";
    bgfx::dbgTextPrintf(1, 3, attr(yellow ? kBlack : kWhite, yellow ? kYellow : kGreen),
                        yellow ? "CAUTION" : "GREEN  ");

    bgfx::dbgTextPrintf(1, 4, attr(kWhite, kBlack), "SPD %3d", (int)player->v);

    // Phase 4c (PORT_PROGRESS.md): index.html:3990-3996's LAST/BEST lap
    // time strip, ported via fmtLapTime() (fmt_time.h -- a direct port of
    // JS's fmtT()). Car::lastLapT/bestLapT are already set correctly by
    // step_car.cpp (Phase 1's ported physics), so this is purely a
    // rendering addition.
    bgfx::dbgTextPrintf(1, 5, attr(kWhite, kBlack), "LAST %s", fmtLapTime(player->lastLapT).c_str());
    bgfx::dbgTextPrintf(1, 6, attr(kWhite, kBlack), "BEST %s", fmtLapTime(player->bestLapT).c_str());

    // Phase 4d (PORT_PROGRESS.md): index.html:3800-3821's drawSpeedModule()
    // gear+RPM readout, via the ported gearRpm() (gear_rpm.h) -- gear/RPM
    // aren't real physics state, just a display-time function of Car::v.
    const GearRpm gr = gearRpm(player->v);
    bgfx::dbgTextPrintf(1, 7, attr(kWhite, kBlack), "GEAR %d  RPM %3d%%", gr.gear, (int)(gr.rpm * 100));

    // Phase 6d (PORT_PROGRESS.md): a minimal spotter caption -- index.html:
    // 4040-4046's `spotOn = S.spotT>0 && S.spotTxt` gate, ported verbatim.
    // Not JS's own fading/merged DRAFT-chip presentation (that's a real
    // canvas-drawn UI element this port's dbgText-only HUD has no
    // equivalent of), just enough to make Phase 6b's spotter-message
    // trigger logic visually verifiable (this sandbox has no audio
    // hardware to listen for the matching spotterBlip() with). Row 0 --
    // every other row this function/status_bars.cpp/leaderboard.cpp use is
    // already spoken for (rows 1-7 above, 8-10 by drawStatusBars(), and the
    // leaderboard/minimap boxes cascade in pixel space starting at y=190px
    // right below that), so this is the one still-free row, directly above
    // the LAP/POS readout.
    if (state.spotT > 0 && !state.spotTxt.empty()) {
        bgfx::dbgTextPrintf(1, 0, attr(kWhite, kBlack), "%s", state.spotTxt.c_str());
    }

    // Phase 4e (PORT_PROGRESS.md): index.html:3999-4020's segmented TIRE/
    // FUEL/CAR status strip -- the first HUD feature needing real quad
    // geometry (drawStatusBars() appends into `uiOut`; Renderer submits it
    // as a separate UI-overlay view after this function returns).
    drawStatusBars(*player, uiOut);

    // Phase 4g (PORT_PROGRESS.md): index.html:3939-3978's leaderboard panel
    // (rank, color chip, name/tag, live gap), placed directly below the
    // status bars above (rows 1-10 end at y=176px) -- matching JS's own
    // "minimap cascades below the leaderboard" ordering (computeLayout(),
    // index.html:3822-3823), now that the leaderboard actually exists.
    const std::vector<const Car*> order = computeRaceOrder(cars);
    const std::vector<LeaderboardRow> lbRows =
        buildLeaderboardRows(order, state.mode, state.flag, state.t, trackTotal);
    const LeaderboardBox lbBox = {8.0f, 190.0f, 248.0f, 16.0f + 16.0f * (float)lbRows.size()};
    drawLeaderboard(lbBox, lbRows, state.flag == "yellow", uiOut);

    // Phase 4f (PORT_PROGRESS.md): index.html:4059-4101's minimap, now
    // cascaded below the leaderboard above, matching JS's own layout
    // ordering exactly (it previously sat directly under the status bars,
    // before this phase's leaderboard existed).
    const float minimapY = lbBox.y + lbBox.h + 8.0f;
    const MinimapBox minimapBox = {8.0f, minimapY, 180.0f, 110.0f};
    drawMinimap(minimapBox, minimapOutline, minimapBoundX, minimapBoundY, cars, state.t, uiOut);
}
