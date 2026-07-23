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

    if (ok) {
        std::printf("gear_rpm_test: all gear/RPM values match expectations.\n");
        return 0;
    }
    return 1;
}
