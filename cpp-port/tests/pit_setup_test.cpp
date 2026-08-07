// Verifies pit_setup.{h,cpp}'s pure logic (region layout, clampPitSetup())
// -- same rationale as menu_test.cpp/touch_controls_test.cpp: real
// synthetic click delivery can't be reliably tested headlessly in this
// container, so this exercises the region-computation/clamping math the
// click handler (main.cpp) calls into.

#include "../src/ui/pit_setup.h"
#include "../src/ui/touch_controls.h" // pointInRect() -- shared with main.cpp's own click handling

#include <cstdio>

namespace {

bool ok = true;

void check(bool cond, const char* what) {
    if (!cond) {
        std::fprintf(stderr, "pit_setup_test: FAILED -- %s\n", what);
        ok = false;
    }
}

} // namespace

int main() {
    PitSetupRegions r = computePitSetupRegions();

    struct Named { const char* name; SDL_Rect rect; };
    const Named all[] = {
        {"wedgeMinus", r.wedgeMinus}, {"wedgePlus", r.wedgePlus},
        {"trackBarMinus", r.trackBarMinus}, {"trackBarPlus", r.trackBarPlus},
    };
    for (auto& n : all) {
        check(n.rect.w > 0 && n.rect.h > 0, "a region has non-positive size");
        check(n.rect.x >= 0 && n.rect.y >= 0, "a region has a negative position");
    }

    // Each row's minus button sits strictly left of its plus button, with
    // no overlap -- both rows share the same layout (drawPitSetup() prints
    // WEDGE and TRACK BAR with identical column offsets).
    check(r.wedgeMinus.x + r.wedgeMinus.w <= r.wedgePlus.x, "wedgeMinus overlaps wedgePlus");
    check(r.trackBarMinus.x + r.trackBarMinus.w <= r.trackBarPlus.x, "trackBarMinus overlaps trackBarPlus");
    check(r.wedgeMinus.x == r.trackBarMinus.x, "wedge and track-bar minus buttons aren't column-aligned");
    check(r.wedgePlus.x == r.trackBarPlus.x, "wedge and track-bar plus buttons aren't column-aligned");

    // The two rows are vertically distinct and in the order drawPitSetup()
    // prints them (WEDGE above TRACK BAR).
    check(r.wedgeMinus.y < r.trackBarMinus.y, "wedge row is not above track-bar row");
    check(r.wedgeMinus.y == r.wedgePlus.y, "wedge minus/plus aren't on the same row");
    check(r.trackBarMinus.y == r.trackBarPlus.y, "track-bar minus/plus aren't on the same row");

    // A point inside each rect actually hits it (sanity check on the shared
    // pointInRect() helper, same as menu_test.cpp's own).
    check(pointInRect(r.wedgeMinus.x + 1, r.wedgeMinus.y + 1, r.wedgeMinus), "point inside wedgeMinus missed");
    check(pointInRect(r.trackBarPlus.x + 1, r.trackBarPlus.y + 1, r.trackBarPlus),
          "point inside trackBarPlus missed");

    // clampPitSetup(): stays within [-1, 1], is a no-op inside the range,
    // and clamps (doesn't wrap) outside it -- repeated clicks at a limit
    // must stick, not drift past it or wrap around.
    check(clampPitSetup(0.0) == 0.0, "clampPitSetup(0) changed the value");
    check(clampPitSetup(0.5) == 0.5, "clampPitSetup(0.5) changed the value");
    check(clampPitSetup(-0.5) == -0.5, "clampPitSetup(-0.5) changed the value");
    check(clampPitSetup(1.0) == 1.0, "clampPitSetup(1.0) changed the value");
    check(clampPitSetup(1.05) == 1.0, "clampPitSetup(1.05) didn't clamp to 1.0");
    check(clampPitSetup(-1.05) == -1.0, "clampPitSetup(-1.05) didn't clamp to -1.0");
    check(clampPitSetup(5.0) == 1.0, "clampPitSetup(5.0) didn't clamp to 1.0");
    check(clampPitSetup(-5.0) == -1.0, "clampPitSetup(-5.0) didn't clamp to -1.0");

    // Repeated stepping from a value already at the limit stays exactly at
    // the limit (the actual click-handler pattern: newValue =
    // clampPitSetup(current + kPitSetupStep)).
    double atLimit = 1.0;
    atLimit = clampPitSetup(atLimit + kPitSetupStep);
    check(atLimit == 1.0, "stepping past +1.0 didn't stay clamped at 1.0");
    double atNegLimit = -1.0;
    atNegLimit = clampPitSetup(atNegLimit - kPitSetupStep);
    check(atNegLimit == -1.0, "stepping past -1.0 didn't stay clamped at -1.0");

    if (ok) {
        std::printf("pit_setup_test: region layout and clamping all correct.\n");
        return 0;
    }
    return 1;
}
