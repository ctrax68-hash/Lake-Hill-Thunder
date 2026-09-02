#pragma once

#include "font_atlas.h"
#include "vertex.h"

#include "../sim/car.h"

#include <string>
#include <vector>

// G27 (graphics pass): the broadcast ticker -- a strip across the top of the
// screen scrolling continuously through the whole field, one entry per car:
// position, colour chip, number, driver name, last lap time.
//
// WHY. It is the reference HUD's most distinctive element and the one piece
// of it this port had no equivalent of at all. The leaderboard shows the top
// five plus the player; in a twenty-car field that leaves fourteen drivers
// the player never sees. The ticker is how a broadcast covers the rest of the
// field without giving it permanent screen area, and it is the reason a race
// feels like a field rather than like six cars.
//
// It also could not have been built before G26. A scrolling strip needs to
// know how wide its content is in order to wrap it seamlessly, and dbgText's
// 8x16 grid could only place text at whole cells -- a strip stepping eight
// pixels at a time reads as stuttering, not scrolling.

// One entry's resolved content. Split from drawing so the layout -- which is
// where the wrap arithmetic lives, and the only part that can be wrong in a
// way a screenshot will not show -- is testable without a GPU.
struct TickerEntry {
    int rank;
    int carNum;
    std::string name;
    std::string lapTime; // already formatted, or the "--:--.--" placeholder
    Color3 col;
    bool isPlayer;
};

// Builds one entry per car, in race order. `order` is hud.h's
// computeRaceOrder() output, the same sort the leaderboard uses, so the two
// can never disagree about who is running where.
std::vector<TickerEntry> buildTickerEntries(const std::vector<const Car*>& order);

// Total width of one full pass through `entries` at `pixelSize`, including
// the separator space after each. This is the scroll period: offsetting by
// exactly this much reproduces the starting image, which is what makes the
// wrap invisible.
float tickerContentWidth(const std::vector<TickerEntry>& entries, float pixelSize);

struct TickerBox {
    float x, y, w, h;
};

// Draws the strip: a translucent band, then as many copies of the entry list
// as it takes to cover `box.w`, scrolled by `simT`.
//
// Entries are clipped by SKIPPING any that fall outside the box rather than
// by a scissor rect -- this view has no per-draw scissor set up, and one is
// not worth adding for a strip whose entries are individually cheap to test.
// An entry straddling the edge is dropped whole, so the strip appears to
// consume entries at its edges rather than slicing glyphs in half.
void drawTicker(const TickerBox& box, const std::vector<TickerEntry>& entries, double simT,
                std::vector<PosColorVertex>& uiOut, std::vector<PosColorUvVertex>& textOut);
