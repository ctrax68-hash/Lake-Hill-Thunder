// Verifies Mulberry32 (src/sim/rng.h) against ground-truth output captured
// from the original JS `mulberry32` (index.html:229) run under Node for the
// 4 seeds this game actually uses. Regenerate the expected values with
// `node` against the JS source directly (not by hand) if this ever needs
// updating -- see PORT_PROGRESS.md Phase 1a notes for the exact script used.

#include "../src/sim/rng.h"

#include <cmath>
#include <cstdio>
#include <cstdlib>

namespace {

bool nearlyEqual(double a, double b) {
    return std::fabs(a - b) < 1e-15;
}

bool checkSeed(uint32_t seed, const double* expected, int count) {
    Mulberry32 rng(seed);
    bool ok = true;
    for (int i = 0; i < count; ++i) {
        double v = rng.next();
        if (!nearlyEqual(v, expected[i])) {
            std::fprintf(stderr, "seed %u sample %d: got %.17g expected %.17g\n",
                         seed, i, v, expected[i]);
            ok = false;
        }
    }
    return ok;
}

} // namespace

int main() {
    // clang-format off
    const double seed12345[] = {0.97972826776094735, 0.30675226449966431, 0.48420542152598500, 0.81793441250920296, 0.50942836934700608, 0.34747186047025025};
    const double seed999[]   = {0.96990582230500877, 0.63477940973825753, 0.30933190695941448, 0.77269398933276534, 0.45663421228528023, 0.57689674384891987};
    const double seed777[]   = {0.68637871788814664, 0.034451418323442340, 0.19238732964731753, 0.12300295010209084, 0.63296704227104783, 0.38216686481609941};
    const double seed4242[]  = {0.54670613352209330, 0.27860878920182586, 0.93123691715300083, 0.50722246640361845, 0.66877828026190400, 0.28787897294387221};
    // clang-format on

    bool ok = true;
    ok &= checkSeed(12345u, seed12345, 6);
    ok &= checkSeed(999u, seed999, 6);
    ok &= checkSeed(777u, seed777, 6);
    ok &= checkSeed(4242u, seed4242, 6);

    if (ok) {
        std::printf("rng_test: all 4 seeds match JS mulberry32 bit-for-bit.\n");
        return 0;
    }
    std::fprintf(stderr, "rng_test: MISMATCH -- Mulberry32 port diverges from JS.\n");
    return 1;
}
