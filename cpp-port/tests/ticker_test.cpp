// G27 (graphics pass): exercises the broadcast ticker's entry building and
// its scroll/wrap arithmetic. bgfx-free, same hand-rolled expect pattern as
// particles_test.cpp / font_atlas_test.cpp.
//
// WHY THE WRAP IS THE POINT. Everything else about this strip is visible in a
// screenshot the moment it is wrong. The wrap is not: a ticker whose scroll
// period disagrees with its content width by a few pixels looks perfect in
// any single frame and jumps once per cycle -- which on a twenty-car field at
// 55 px/s is roughly once a minute, exactly the interval at which nobody can
// reproduce it on demand. So it is pinned as an identity: the strip drawn at
// t and at t + one period must be the same image, vertex for vertex.

#include "../src/render/ticker.h"
#include "../src/render/hud.h"

#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

namespace {
int g_failures = 0;

void expect(bool cond, const char* what) {
    if (!cond) {
        std::fprintf(stderr, "FAIL: %s\n", what);
        ++g_failures;
    }
}

Car makeCar(int num, const char* name, double prog, bool isPlayer = false) {
    Car c;
    c.num = num;
    c.name = name;
    c.prog = prog;
    c.isPlayer = isPlayer;
    c.lastLapT = 0.0;
    c.col = {0.5, 0.5, 0.5};
    return c;
}

// The two geometry lists a draw produces, flattened for comparison.
struct Drawn {
    std::vector<PosColorVertex> quads;
    std::vector<PosColorUvVertex> text;
};

Drawn drawAt(const TickerBox& box, const std::vector<TickerEntry>& entries, double t) {
    Drawn d;
    drawTicker(box, entries, t, d.quads, d.text);
    return d;
}

bool sameText(const std::vector<PosColorUvVertex>& a, const std::vector<PosColorUvVertex>& b) {
    if (a.size() != b.size()) return false;
    for (size_t i = 0; i < a.size(); ++i) {
        // A float period cannot land on an exact bit pattern, so positions are
        // compared with a sub-pixel tolerance rather than for equality.
        if (std::fabs(a[i].x - b[i].x) > 0.05f || std::fabs(a[i].y - b[i].y) > 0.05f) return false;
        if (a[i].abgr != b[i].abgr) return false;
        if (std::fabs(a[i].u - b[i].u) > 1e-6f || std::fabs(a[i].v - b[i].v) > 1e-6f) return false;
    }
    return true;
}
} // namespace

int main() {
    std::vector<Car> cars;
    cars.push_back(makeCar(7, "D. MCCREADY", 300.0));
    cars.push_back(makeCar(21, "YOU", 200.0, /*isPlayer=*/true));
    cars.push_back(makeCar(44, "R. LAFONTAIN", 100.0));
    cars.push_back(makeCar(3, "T. VANCE", 50.0));

    // ---- buildTickerEntries ----
    {
        const std::vector<const Car*> order = computeRaceOrder(cars);
        const std::vector<TickerEntry> e = buildTickerEntries(order);
        expect(e.size() == cars.size(), "ticker carries one entry per car");
        expect(e[0].rank == 1 && e[1].rank == 2 && e[2].rank == 3 && e[3].rank == 4,
               "ticker ranks are 1-based and sequential");
        expect(e[0].carNum == 7, "ticker is ordered by race position, leader first");
        expect(e[3].carNum == 3, "ticker's last entry is the car running last");

        int players = 0;
        for (const TickerEntry& t : e) {
            if (t.isPlayer) ++players;
        }
        expect(players == 1, "exactly one ticker entry is flagged as the player");
        for (const TickerEntry& t : e) expect(!t.lapTime.empty(), "every entry has a lap-time field");
    }

    // ---- retired and pitting cars report their state, not a lap time ----
    {
        std::vector<Car> f = cars;
        f[0].out = true;
        f[2].pit = 1;
        const std::vector<const Car*> order = computeRaceOrder(f);
        const std::vector<TickerEntry> e = buildTickerEntries(order);
        bool sawOut = false, sawPit = false;
        for (const TickerEntry& t : e) {
            if (t.carNum == 7) sawOut = t.lapTime == "OUT";
            if (t.carNum == 44) sawPit = t.lapTime == "PIT";
        }
        expect(sawOut, "a retired car reads OUT in the ticker");
        expect(sawPit, "a pitting car reads PIT in the ticker");
    }

    // ---- tickerContentWidth ----
    {
        const std::vector<const Car*> order = computeRaceOrder(cars);
        const std::vector<TickerEntry> e = buildTickerEntries(order);

        expect(tickerContentWidth({}, 14.0f) == 0.0f, "an empty field has zero content width");
        const float w = tickerContentWidth(e, 14.0f);
        expect(w > 0.0f, "a populated field has a positive content width");

        // Additive: dropping an entry must shorten the strip, or the wrap
        // period stops matching what is actually drawn.
        std::vector<TickerEntry> fewer = e;
        fewer.pop_back();
        expect(tickerContentWidth(fewer, 14.0f) < w, "removing an entry shortens the content");

        expect(tickerContentWidth(e, 28.0f) > w, "content width grows with text size");
    }

    // ---- the strip is drawn, and covers its box ----
    {
        const std::vector<const Car*> order = computeRaceOrder(cars);
        const std::vector<TickerEntry> e = buildTickerEntries(order);
        const TickerBox box = {0.0f, 0.0f, 1280.0f, 24.0f};

        const Drawn d = drawAt(box, e, 0.0);
        expect(!d.quads.empty(), "ticker draws its band");
        expect(!d.text.empty(), "ticker draws entry text");

        // With only four cars, one pass is far narrower than a 1280px strip --
        // the list has to repeat or most of the band sits empty. This is the
        // case a single loop over entries gets wrong.
        const float content = tickerContentWidth(e, 14.0f);
        expect(content < box.w, "test premise: one pass is narrower than the strip");
        float rightmost = 0.0f;
        for (const PosColorUvVertex& v : d.text) rightmost = std::max(rightmost, v.x);
        expect(rightmost > content, "ticker repeats its list to cover a strip wider than one pass");

        // An empty field still draws the band, so the strip does not blink out
        // before the grid is formed.
        const Drawn empty = drawAt(box, {}, 0.0);
        expect(!empty.quads.empty(), "ticker draws its band even with no entries");
        expect(empty.text.empty(), "an empty ticker draws no text");
    }

    // ---- THE WRAP ----
    //
    // First attempt at this asserted that the image at t and at t + content/rate
    // match. That test passed on a deliberately broken tickerContentWidth,
    // because it derived the period FROM the function under test: with the
    // content wrong, both the code's wrap and the test's expected period were
    // wrong by the same amount, and the two agreed with each other perfectly.
    // A test whose expected value comes from the thing it is testing checks
    // only self-consistency.
    //
    // The independent property is spatial, not temporal: within a SINGLE
    // frame, the drawn strip must repeat every tickerContentWidth pixels. That
    // ties the advertised content width to the distance the drawing actually
    // advances, with nothing in common between the two but the pixels. If they
    // disagree, the strip visibly jumps once per wrap -- roughly once a minute
    // on a full field, which is exactly the interval nobody can reproduce on
    // demand.
    {
        const std::vector<const Car*> order = computeRaceOrder(cars);
        const std::vector<TickerEntry> e = buildTickerEntries(order);
        // Wide enough to hold several passes, so there is a next copy to find.
        const TickerBox box = {0.0f, 0.0f, 4000.0f, 24.0f};
        const float content = tickerContentWidth(e, 14.0f);
        const Drawn d = drawAt(box, e, 0.0);
        expect(!d.text.empty(), "wide ticker draws text");

        // Every glyph that has room for a copy one content-width to its right
        // must have one: same glyph, same colour, same line, exactly that far
        // along.
        int checked = 0, matched = 0;
        for (const PosColorUvVertex& v : d.text) {
            if (v.x + content > box.w - 200.0f) continue; // no room for the copy
            ++checked;
            for (const PosColorUvVertex& w : d.text) {
                if (w.abgr != v.abgr) continue;
                if (std::fabs(w.u - v.u) > 1e-6f || std::fabs(w.v - v.v) > 1e-6f) continue;
                if (std::fabs(w.y - v.y) > 0.05f) continue;
                if (std::fabs(w.x - (v.x + content)) < 0.05f) {
                    ++matched;
                    break;
                }
            }
        }
        expect(checked > 0, "test premise: the strip is wide enough to contain a repeat");
        expect(matched == checked, "the drawn strip does not repeat every tickerContentWidth pixels");
    }

    // ---- the strip scrolls, and the scroll is periodic ----
    {
        const std::vector<const Car*> order = computeRaceOrder(cars);
        const std::vector<TickerEntry> e = buildTickerEntries(order);
        const TickerBox box = {0.0f, 0.0f, 1280.0f, 24.0f};
        const float content = tickerContentWidth(e, 14.0f);

        const Drawn a0 = drawAt(box, e, 0.0);
        const Drawn a1 = drawAt(box, e, 1.0);
        expect(!sameText(a0.text, a1.text), "the ticker actually scrolls");

        float rate = 0.0f;
        if (!a0.text.empty() && a0.text.size() == a1.text.size()) rate = a0.text[0].x - a1.text[0].x;
        expect(rate > 0.0f, "the ticker scrolls leftward at a measurable rate");

        // Given the spatial repeat is pinned above, this now says something
        // real: advancing by one content width returns to the same image.
        if (rate > 0.0f) {
            const double period = (double)content / (double)rate;
            for (double t : {0.0, 3.7, 11.25}) {
                expect(sameText(drawAt(box, e, t).text, drawAt(box, e, t + period).text),
                       "the ticker does not repeat after one content width");
            }
        }
    }

    std::printf(g_failures == 0 ? "ticker_test: PASS\n" : "ticker_test: FAILURES ABOVE\n");
    return g_failures == 0 ? 0 : 1;
}
