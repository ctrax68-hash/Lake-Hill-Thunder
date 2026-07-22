#include "hud.h"

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

void drawHud(const RaceState& state, const std::vector<Car>& cars) {
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
}
