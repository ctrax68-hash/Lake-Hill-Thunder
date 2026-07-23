// Verifies fmt_time.{h,cpp}'s fmtLapTime() against hand-computed values,
// matching JS's fmtT() (index.html:3769-3771) exactly. Pure formatting
// logic, zero bgfx dependency -- no live rendering context needed.

#include "../src/render/fmt_time.h"

#include <cmath>
#include <cstdio>
#include <string>

namespace {

bool ok = true;

void checkEq(const std::string& got, const char* want, const char* what) {
    if (got != want) {
        std::fprintf(stderr, "fmt_time_test: FAILED -- %s (got \"%s\", want \"%s\")\n",
                      what, got.c_str(), want);
        ok = false;
    }
}

} // namespace

int main() {
    // Placeholder cases: zero, negative, NaN.
    checkEq(fmtLapTime(0.0), "--:--.--", "t=0 should be placeholder");
    checkEq(fmtLapTime(-5.0), "--:--.--", "negative t should be placeholder");
    checkEq(fmtLapTime(std::nan("")), "--:--.--", "NaN should be placeholder");

    // Sub-10-second seconds component gets zero-padded (index.html:3771's
    // `s<10?'0':''`), sub-minute overall (m=0).
    checkEq(fmtLapTime(7.5), "0:07.50", "sub-10s seconds should be zero-padded");

    // No zero-pad once seconds >= 10.
    checkEq(fmtLapTime(45.25), "0:45.25", "seconds >=10 should not be zero-padded");

    // Minutes component present once t >= 60.
    checkEq(fmtLapTime(75.0), "1:15.00", "75s should be 1:15.00");
    checkEq(fmtLapTime(125.5), "2:05.50", "125.5s should be 2:05.50 (zero-padded seconds)");

    if (ok) {
        std::printf("fmt_time_test: all formatted values match expectations.\n");
        return 0;
    }
    return 1;
}
