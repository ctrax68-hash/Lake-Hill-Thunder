#include "gear_rpm.h"

#include <algorithm>
#include <array>
#include <cstddef>

namespace {
// index.html:1392's GEAR_BREAKS.
constexpr std::array<double, 4> kGearBreaks = {14, 26, 40, 70};
} // namespace

GearRpm gearRpm(double v) {
    double lo = 0, hi = kGearBreaks[0];
    int gear = 1;
    for (size_t g = 0; g < kGearBreaks.size(); ++g) {
        // index.html:1396: the last breakpoint's branch is also taken
        // unconditionally once g reaches the last index, so speeds beyond
        // the final breakpoint (70) still land in top gear rather than
        // falling off the end of the loop.
        if (v <= kGearBreaks[g] || g == kGearBreaks.size() - 1) {
            hi = kGearBreaks[g];
            lo = g > 0 ? kGearBreaks[g - 1] : 0;
            gear = (int)g + 1;
            break;
        }
    }
    const double rpm = 0.25 + 0.75 * std::min(1.0, (v - lo) / std::max(1.0, hi - lo));
    return {gear, rpm};
}
