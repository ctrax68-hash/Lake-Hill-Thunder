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

    // M2: cycleTrack() must VISIT every track, not just return to its start.
    // The reported symptom was "only two tracks appearing, not 4" -- and a
    // cycle-length check alone would have passed happily on that, because
    // advancing by 2 through a 4-entry list still returns to where it began.
    // So assert coverage, which is the property that actually broke.
    {
        constexpr int kTracks = 4;
        bool seen[kTracks] = {false, false, false, false};
        int idx = 0;
        for (int i = 0; i < kTracks; ++i) {
            seen[idx] = true;
            idx = cycleTrack(idx, kTracks);
        }
        for (int i = 0; i < kTracks; ++i) {
            check(seen[i], "cycleTrack() never reaches some track -- part of the roster is unselectable");
        }
        check(idx == 0, "cycleTrack() is not a 4-cycle back to the first track");
        // Defensive normalization, matching cycleLaps()'s fallthrough spirit.
        check(cycleTrack(-1, kTracks) == 0, "cycleTrack() does not normalize a negative index");
        check(cycleTrack(99, kTracks) >= 0 && cycleTrack(99, kTracks) < kTracks,
              "cycleTrack() does not normalize an out-of-range index");
        check(cycleTrack(0, 0) == 0, "cycleTrack() does not survive an empty roster");
    }

    // M2: one tap must dispatch exactly one click.
    //
    // SDL synthesizes a mouse click from every touch, so a single tap arrives
    // as BOTH an SDL_FINGERDOWN and an SDL_MOUSEBUTTONDOWN carrying
    // which == SDL_TOUCH_MOUSEID. main.cpp dispatched from both, so every tap
    // counted twice: the track selector advanced by 2 (only 2 of 4 tracks
    // reachable), laps skipped 5 and 20, and QUALIFYING/SOUND/TILT/PIT/CAM --
    // all plain `!x` toggles -- flipped back to their original value and
    // looked dead. Only the idempotent controls (volume, start, held drive
    // buttons) appeared to work.
    //
    // Honest limit: main.cpp's event loop has no test harness (this file's own
    // header explains why event *delivery* can't be exercised headlessly), so
    // this pins the predicate main.cpp now filters on, plus the tap arithmetic
    // that predicate exists to fix -- not the wiring itself.
    check(isSyntheticTouchMouse(SDL_TOUCH_MOUSEID), "SDL's synthetic touch-mouse id is not recognized");
    check(!isSyntheticTouchMouse(0), "a real mouse is misfiltered as a synthetic touch-mouse");
    {
        // The two events one real tap produces, in the order SDL queues them.
        const Uint32 tapEventWhich[] = {SDL_TOUCH_MOUSEID}; // + one SDL_FINGERDOWN, always dispatched
        int dispatches = 1;                                 // the SDL_FINGERDOWN branch
        for (Uint32 which : tapEventWhich) {
            if (!isSyntheticTouchMouse(which)) ++dispatches;
        }
        check(dispatches == 1, "one tap does not dispatch exactly one click");

        // A real mouse click produces only the mouse event, and must still work.
        const Uint32 mouseWhich = 0;
        check(!isSyntheticTouchMouse(mouseWhich), "a desktop mouse click would be dropped");
    }

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
