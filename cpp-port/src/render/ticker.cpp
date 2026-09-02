#include "ticker.h"

#include "color.h"
#include "fmt_time.h"
#include "hud_text.h"
#include "ui_draw.h"

#include <cmath>

namespace {

constexpr float kTextSize = 14.0f;

// Per-entry internal spacing, all in pixels at kTextSize.
constexpr float kChipW = 9.0f, kChipH = 11.0f;
constexpr float kRankGap = 6.0f;  // rank -> chip
constexpr float kChipGap = 5.0f;  // chip -> number
constexpr float kNumGap = 6.0f;   // number -> name
constexpr float kNameGap = 10.0f; // name -> lap time
constexpr float kEntryGap = 26.0f; // trailing space before the next entry

// Scroll rate. Slow enough to read a name at a glance while cornering, which
// is the only speed that matters -- a ticker the player has to track with
// their eyes is worse than no ticker, because it pulls attention off the
// track at exactly the moment the information is least useful.
constexpr float kScrollPxPerSec = 55.0f;

std::string rankText(int rank) { return "P" + std::to_string(rank); }
std::string numText(int carNum) { return "#" + std::to_string(carNum); }

// Width of one entry, excluding the trailing gap.
float entryWidth(const TickerEntry& e, float size) {
    return font::measure(rankText(e.rank), size) + kRankGap + kChipW + kChipGap +
           font::measure(numText(e.carNum), size) + kNumGap + font::measure(e.name, size) +
           kNameGap + font::measure(e.lapTime, size);
}

} // namespace

std::vector<TickerEntry> buildTickerEntries(const std::vector<const Car*>& order) {
    std::vector<TickerEntry> out;
    out.reserve(order.size());
    for (std::size_t i = 0; i < order.size(); ++i) {
        const Car* c = order[i];
        TickerEntry e;
        e.rank = (int)i + 1;
        e.carNum = c->num;
        e.name = c->name;
        // A car that is out or in the pits reports that instead of a lap
        // time. The leaderboard already makes this substitution for its own
        // tag column; doing the same here keeps one car from reading as
        // healthy in one place and retired in the other.
        e.lapTime = c->out ? "OUT" : c->pit > 0 ? "PIT" : fmtLapTime(c->lastLapT);
        e.col = c->col;
        e.isPlayer = c->isPlayer;
        out.push_back(e);
    }
    return out;
}

float tickerContentWidth(const std::vector<TickerEntry>& entries, float pixelSize) {
    float w = 0.0f;
    for (const TickerEntry& e : entries) w += entryWidth(e, pixelSize) + kEntryGap;
    return w;
}

void drawTicker(const TickerBox& box, const std::vector<TickerEntry>& entries, double simT,
                std::vector<PosColorVertex>& uiOut, std::vector<PosColorUvVertex>& textOut) {
    // The band goes down regardless, so the strip does not blink out of
    // existence in the pre-race frames before any car has a position.
    pushQuad(uiOut, box.x, box.y, box.w, box.h, packColor(Theme::kBlack, 0.66f));
    pushLineSegment(uiOut, box.x, box.y + box.h, box.x + box.w, box.y + box.h, 1.0f,
                    packColor(Theme::kGraycool, 0.4f));
    if (entries.empty()) return;

    const float content = tickerContentWidth(entries, kTextSize);
    if (content <= 0.0f) return;

    // Scroll offset, wrapped into one content period.
    //
    // Computed from simT each frame rather than accumulated per frame, so it
    // cannot drift with frame rate. The fmod itself is defensive rather than
    // load-bearing: removing it was tried as a deliberate mutation and changed
    // nothing visible, because the drawing is already periodic in x with this
    // exact period, so an unwrapped offset lands on the same image. It stays
    // because an unbounded float offset does eventually lose sub-pixel
    // precision, but the honest note is that no test covers that -- it would
    // take hours of continuous running to show.
    float offset = (float)std::fmod(simT * (double)kScrollPxPerSec, (double)content);
    if (offset < 0.0f) offset += content;

    const float baseline = box.y + box.h * 0.5f + font::ascent(kTextSize) * 0.42f;
    const float chipY = box.y + (box.h - kChipH) * 0.5f;
    const float right = box.x + box.w;

    // Repeat the list until the strip is covered. A single pass is narrower
    // than the screen whenever the field is small, so this cannot be one loop
    // over entries -- with four cars left running, one pass is a few hundred
    // pixels and the rest of the strip would sit empty.
    float x = box.x - offset;
    while (x < right) {
        for (const TickerEntry& e : entries) {
            const float w = entryWidth(e, kTextSize);
            if (x + w >= box.x && x <= right) {
                float pen = x;
                const uint32_t fg = e.isPlayer ? packColor(Theme::kYellow) : packColor(Theme::kWhite);

                hudtext::draw(textOut, pen, baseline, rankText(e.rank), kTextSize,
                              packColor(Theme::kGraycool));
                pen += font::measure(rankText(e.rank), kTextSize) + kRankGap;

                pushQuad(uiOut, pen, chipY, kChipW, kChipH,
                         packColor((float)e.col[0], (float)e.col[1], (float)e.col[2]));
                pen += kChipW + kChipGap;

                hudtext::draw(textOut, pen, baseline, numText(e.carNum), kTextSize, fg);
                pen += font::measure(numText(e.carNum), kTextSize) + kNumGap;

                hudtext::draw(textOut, pen, baseline, e.name, kTextSize, fg);
                pen += font::measure(e.name, kTextSize) + kNameGap;

                hudtext::draw(textOut, pen, baseline, e.lapTime, kTextSize,
                              packColor(Theme::kGraycool));
            }
            x += w + kEntryGap;
            if (x >= right) break;
        }
    }
}
