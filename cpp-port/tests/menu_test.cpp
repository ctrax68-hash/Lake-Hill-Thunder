// Verifies menu.{h,cpp}'s pure logic (region layout, cycleLaps(),
// volumeFromClickX()) -- no SDL2/bgfx window dependency, same rationale as
// touch_controls_test.cpp: actual synthetic mouse/tap event *delivery*
// can't be reliably tested headlessly in this container (three genuinely
// different xdotool/XTEST/XSendEvent attempts in Phase 2e/3b's own session
// notes never registered against a real SDL window here), so this only
// exercises the region-computation/hit-test/value math that
// handleMenuClick() (main.cpp) calls into.

#include "../src/ui/menu.h"
#include "../src/ui/touch_controls.h" // pointInRect() -- shared with main.cpp's own handleMenuClick()

#include <cstdio>
#include <initializer_list>

namespace {

bool ok = true;

void check(bool cond, const char* what) {
    if (!cond) {
        std::fprintf(stderr, "menu_test: FAILED -- %s\n", what);
        ok = false;
    }
}

} // namespace

int main() {
    MenuRegions r = computeMenuRegions();

    struct Named { const char* name; SDL_Rect rect; };
    const Named all[] = {
        {"trackBtn", r.trackBtn}, {"lapsBtn", r.lapsBtn}, {"qualBtn", r.qualBtn},
        {"soundBtn", r.soundBtn}, {"tiltBtn", r.tiltBtn}, {"volumeBar", r.volumeBar},
        {"startBtn", r.startBtn},
    };

    // Every region has positive size and sits at a non-negative position
    // (top-left anchored fixed layout -- see menu.h's own comment on why
    // this doesn't take a window size, unlike computeTouchRegions()).
    for (auto& n : all) {
        check(n.rect.w > 0 && n.rect.h > 0, "a region has non-positive size");
        check(n.rect.x >= 0 && n.rect.y >= 0, "a region has a negative position");
    }

    // Rows must be in strictly increasing y order, one per control, in the
    // same top-to-bottom order drawMenu() prints them.
    check(r.trackBtn.y < r.lapsBtn.y, "trackBtn is not above lapsBtn");
    check(r.lapsBtn.y < r.qualBtn.y, "lapsBtn is not above qualBtn");
    check(r.qualBtn.y < r.soundBtn.y, "qualBtn is not above soundBtn");
    check(r.soundBtn.y < r.tiltBtn.y, "soundBtn is not above tiltBtn");
    check(r.tiltBtn.y < r.volumeBar.y, "tiltBtn is not above volumeBar");
    check(r.volumeBar.y < r.startBtn.y, "volumeBar is not above startBtn");

    // No two rows overlap vertically.
    for (size_t i = 0; i < 7; ++i) {
        for (size_t j = i + 1; j < 7; ++j) {
            const SDL_Rect& a = all[i].rect;
            const SDL_Rect& b = all[j].rect;
            const bool overlap = a.y < b.y + b.h && b.y < a.y + a.h;
            check(!overlap, "two menu rows overlap vertically");
        }
    }

    // Each region's own center point hits itself and nothing else.
    for (auto& n : all) {
        const int cx = n.rect.x + n.rect.w / 2, cy = n.rect.y + n.rect.h / 2;
        int hits = 0;
        for (auto& o : all) {
            if (pointInRect(cx, cy, o.rect)) ++hits;
        }
        check(hits == 1, "a region's own center point hits more than one region");
    }

    // cycleLaps(): exact JS order (index.html:4706-4709), and it's a cycle
    // (following it 4 times from any start returns to that start).
    check(cycleLaps(3) == 5, "cycleLaps(3) != 5");
    check(cycleLaps(5) == 10, "cycleLaps(5) != 10");
    check(cycleLaps(10) == 20, "cycleLaps(10) != 20");
    check(cycleLaps(20) == 3, "cycleLaps(20) != 3");
    int laps = 3;
    for (int i = 0; i < 4; ++i) laps = cycleLaps(laps);
    check(laps == 3, "cycleLaps() is not a 4-cycle back to 3");

    // volumeFromClickX(): left edge -> 0, right edge -> 100, midpoint -> ~50,
    // clamped outside the bar in either direction.
    const SDL_Rect bar = {100, 0, 200, 16};
    check(volumeFromClickX(bar, 100) == 0, "volumeFromClickX at left edge != 0");
    check(volumeFromClickX(bar, 300) == 100, "volumeFromClickX at right edge != 100");
    const int mid = volumeFromClickX(bar, 200);
    check(mid >= 45 && mid <= 55, "volumeFromClickX at midpoint is not ~50");
    check(volumeFromClickX(bar, 0) == 0, "volumeFromClickX left of the bar is not clamped to 0");
    check(volumeFromClickX(bar, 1000) == 100, "volumeFromClickX right of the bar is not clamped to 100");

    if (ok) {
        std::printf("menu_test: region layout and value math match expectations.\n");
        return 0;
    }
    return 1;
}
