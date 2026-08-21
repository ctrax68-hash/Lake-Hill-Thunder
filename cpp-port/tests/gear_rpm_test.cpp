// Verifies gear_rpm.{h,cpp}'s gearRpm() against hand-computed values
// straddling each of JS's GEAR_BREAKS=[14,26,40,70] breakpoints
// (index.html:1392-1400), matching JS's arithmetic exactly.

#include "../src/render/gear_rpm.h"

#include <cmath>
#include <cstdio>

namespace {

bool ok = true;

void checkGearRpm(double v, int wantGear, double wantRpm, const char* what) {
    const GearRpm r = gearRpm(v);
    if (r.gear != wantGear) {
        std::fprintf(stderr, "gear_rpm_test: FAILED -- %s (gear: got %d, want %d)\n",
                      what, r.gear, wantGear);
        ok = false;
    }
    if (std::fabs(r.rpm - wantRpm) > 1e-9) {
        std::fprintf(stderr, "gear_rpm_test: FAILED -- %s (rpm: got %.9f, want %.9f)\n",
                      what, r.rpm, wantRpm);
        ok = false;
    }
}

} // namespace

int main() {
    checkGearRpm(0.0, 1, 0.25, "v=0 (bottom of gear 1)");
    checkGearRpm(14.0, 1, 1.0, "v=14 (top of gear 1, at the first breakpoint)");
    checkGearRpm(15.0, 2, 0.25 + 0.75 * (1.0 / 12.0), "v=15 (just into gear 2)");
    checkGearRpm(26.0, 2, 1.0, "v=26 (top of gear 2, at the second breakpoint)");
    checkGearRpm(27.0, 3, 0.25 + 0.75 * (1.0 / 14.0), "v=27 (just into gear 3)");
    checkGearRpm(40.0, 3, 1.0, "v=40 (top of gear 3, at the third breakpoint)");
    checkGearRpm(41.0, 4, 0.25 + 0.75 * (1.0 / 30.0), "v=41 (just into gear 4)");
    checkGearRpm(70.0, 4, 1.0, "v=70 (top of gear 4, at the fourth breakpoint)");
    // Beyond the last breakpoint: still gear 4, rpm clamped at 1.0 rather
    // than exceeding it (index.html:1396's `g===GEAR_BREAKS.length-1`
    // fallback keeps every speed past 70 in the same top-gear bracket).
    checkGearRpm(100.0, 4, 1.0, "v=100 (beyond the last breakpoint, still gear 4/clamped rpm)");

    // N5: gearBreakSpeed() is the single source of truth step_car.cpp's shift
    // hysteresis reads, instead of keeping its own copy of the breakpoints.
    // Assert it agrees with gearRpm()'s own boundaries -- if the table ever
    // moves, this fails rather than letting the two silently disagree.
    struct { int gear; double want; } kEdges[] = {{1, 14.0}, {2, 26.0}, {3, 40.0}, {4, 70.0}};
    for (const auto& e : kEdges) {
        if (gearBreakSpeed(e.gear) != e.want) {
            std::printf("gear_rpm_test: FAILED -- gearBreakSpeed(%d) = %.1f, expected %.1f\n",
                        e.gear, gearBreakSpeed(e.gear), e.want);
            ok = false;
        }
        // The boundary speed must actually be the top of that gear, and a hair
        // past it must be the next one -- which is the property the hysteresis
        // band is centred on.
        if (gearRpm(e.want).gear != e.gear) {
            std::printf("gear_rpm_test: FAILED -- v=%.1f is not the top of gear %d\n", e.want, e.gear);
            ok = false;
        }
        if (e.gear < 4 && gearRpm(e.want + 0.01).gear != e.gear + 1) {
            std::printf("gear_rpm_test: FAILED -- just past %.1f is not gear %d\n", e.want, e.gear + 1);
            ok = false;
        }
    }
    // Out-of-range indices clamp rather than reading off the end of the table.
    if (gearBreakSpeed(0) != 14.0 || gearBreakSpeed(99) != 70.0) {
        std::printf("gear_rpm_test: FAILED -- gearBreakSpeed() does not clamp out-of-range gears\n");
        ok = false;
    }

    if (ok) {
        std::printf("gear_rpm_test: all gear/RPM values match expectations.\n");
        return 0;
    }
    return 1;
}
