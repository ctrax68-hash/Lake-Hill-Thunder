#include "fmt_time.h"

#include <cmath>
#include <cstdio>

std::string fmtLapTime(double t) {
    // index.html:3769: `if(!t||t<=0) return '--:--.--'` -- `!t` also
    // catches NaN (JS's `!NaN` is true); `!(t > 0)` is the C++ equivalent
    // that rejects zero, negative, and NaN alike.
    if (!(t > 0)) return "--:--.--";
    const long m = (long)std::floor(t / 60.0);
    const double s = t - (double)m * 60.0;
    char buf[32];
    std::snprintf(buf, sizeof(buf), "%ld:%s%.2f", m, s < 10.0 ? "0" : "", s);
    return std::string(buf);
}
