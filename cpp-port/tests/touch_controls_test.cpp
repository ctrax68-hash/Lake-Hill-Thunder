// Verifies computeTouchRegions()'s region math (src/ui/touch_controls.{h,cpp})
// against the JS original's --ctl* CSS layout (index.html:19-20,46-51,
// 194-198): bL/bR bottom-left steer pair, bB/bG bottom-right brake/gas pair,
// bP stacked above bB. No SDL2/bgfx window dependency -- this only exercises
// the region-computation/hit-test math, not actual event delivery (which
// can't be reliably tested headlessly, see PORT_PROGRESS.md's Phase 2/3
// session notes on synthetic-input flakiness under Xvfb).

#include "../src/ui/touch_controls.h"

#include <cstdio>
#include <initializer_list>

namespace {

bool ok = true;

void check(bool cond, const char* what) {
    if (!cond) {
        std::fprintf(stderr, "touch_controls_test: FAILED -- %s\n", what);
        ok = false;
    }
}

} // namespace

int main() {
    const int w = 1280, h = 720;
    TouchRegions r = computeTouchRegions(w, h);

    // All five regions must be fully inside the window.
    for (auto& rect : {r.bL, r.bR, r.bB, r.bG, r.bP}) {
        check(rect.x >= 0 && rect.y >= 0 && rect.x + rect.w <= w && rect.y + rect.h <= h,
              "a region falls outside the window bounds");
        check(rect.w > 0 && rect.h > 0, "a region has non-positive size");
    }

    // bL sits left of bR, both anchored to the same bottom row (index.html:46-47).
    check(r.bL.x < r.bR.x, "bL is not left of bR");
    check(r.bL.y == r.bR.y, "bL/bR are not on the same row");

    // bB sits left of bG (bG is the rightmost control), sharing the same
    // bottom edge (not top -- bG is taller than bB, index.html:48,51).
    check(r.bB.x < r.bG.x, "bB is not left of bG");
    check(r.bB.y + r.bB.h == r.bG.y + r.bG.h, "bB/bG do not share the same bottom edge");

    // bP sits directly above bB, same x, with a gap in between (index.html:194-197).
    check(r.bP.x == r.bB.x, "bP is not aligned with bB's x");
    check(r.bP.y + r.bP.h < r.bB.y, "bP does not sit above bB with a gap");

    // The steer pair (bottom-left) and the gas/brake pair (bottom-right)
    // must not overlap for a reasonably wide window.
    check(r.bL.x + r.bL.w < r.bB.x, "left steer pair overlaps the right pedal pair");

    // Sample points: each region's center hits itself and nothing else.
    struct Named { const char* name; SDL_Rect rect; };
    const Named all[] = {{"bL", r.bL}, {"bR", r.bR}, {"bB", r.bB}, {"bG", r.bG}, {"bP", r.bP}};
    for (auto& n : all) {
        const int cx = n.rect.x + n.rect.w / 2, cy = n.rect.y + n.rect.h / 2;
        int hits = 0;
        for (auto& o : all) {
            if (pointInRect(cx, cy, o.rect)) ++hits;
        }
        check(hits == 1, "a region's own center point hits more than one region");
    }

    // A point clearly outside every region (top-left corner) hits nothing.
    for (auto& n : all) {
        check(!pointInRect(0, 0, n.rect), "the window's top-left corner unexpectedly hit a region");
    }

    if (ok) {
        std::printf("touch_controls_test: region layout matches expectations.\n");
        return 0;
    }
    return 1;
}
