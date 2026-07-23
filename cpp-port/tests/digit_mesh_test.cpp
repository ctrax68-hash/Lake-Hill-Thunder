// Verifies digit_mesh.{h,cpp}'s pure geometry (bgfx-free, same category as
// stadium_mesh_test.cpp): each digit emits exactly one quad (6 verts) per
// lit 7-segment bit, out-of-range digits emit nothing, and multi-digit
// numbers sum their digits' own vertex counts.

#include "../src/render/digit_mesh.h"

#include <cstdio>
#include <vector>

namespace {

int g_failures = 0;

void expectTrue(const char* label, bool cond) {
    if (!cond) {
        std::fprintf(stderr, "%s: FAILED\n", label);
        ++g_failures;
    }
}

// Mirrors digit_mesh.cpp's own private kSegBits table (index.html:2175-2176)
// so this test can compute each digit's expected segment count independently
// rather than just re-running the same lookup back at the source.
constexpr int kSegBits[10] = {0b1110111, 0b0100100, 0b1011101, 0b1101101, 0b0101110,
                              0b1101011, 0b1111011, 0b0100101, 0b1111111, 0b1101111};

int popcount(int x) {
    int c = 0;
    while (x) {
        c += x & 1;
        x >>= 1;
    }
    return c;
}

} // namespace

int main() {
    const PutFn identity = [](double x, double y) { return Vec3{x, y, 0.0}; };

    for (int n = 0; n <= 9; ++n) {
        std::vector<MeshVertex> out;
        addDigitQuads(out, n, 0, 0, 1.0, 1.0, identity, {1, 1, 1});
        char label[64];
        std::snprintf(label, sizeof(label), "digit %d emits one quad per lit segment", n);
        expectTrue(label, out.size() == (size_t)popcount(kSegBits[n]) * 6);
    }

    {
        std::vector<MeshVertex> out;
        addDigitQuads(out, 10, 0, 0, 1, 1, identity, {1, 1, 1});
        expectTrue("out-of-range digit (10) emits nothing", out.empty());
        std::vector<MeshVertex> negOut;
        addDigitQuads(negOut, -1, 0, 0, 1, 1, identity, {1, 1, 1});
        expectTrue("out-of-range digit (-1) emits nothing", negOut.empty());
    }

    // addNumber(): total vertex count is the sum of each digit's own
    // segment count (91 -> digit 9 + digit 1).
    {
        std::vector<MeshVertex> out;
        addNumber(out, 91, identity, 2.0, 1.0, {1, 1, 1});
        const size_t expected = (size_t)(popcount(kSegBits[9]) + popcount(kSegBits[1])) * 6;
        expectTrue("addNumber(91) vertex count matches sum of both digits' segments", out.size() == expected);
    }
    {
        std::vector<MeshVertex> out;
        addNumber(out, 7, identity, 2.0, 1.0, {1, 1, 1});
        expectTrue("addNumber(7) (single digit) vertex count matches", out.size() == (size_t)popcount(kSegBits[7]) * 6);
    }

    if (g_failures == 0) {
        std::printf("digit_mesh_test: digit-segment and multi-digit layout all match expectations.\n");
        return 0;
    }
    std::fprintf(stderr, "digit_mesh_test: %d MISMATCHES.\n", g_failures);
    return 1;
}
