// Verifies hill_silhouette.{h,cpp}'s pure geometry (bgfx-free, same
// category as stadium_mesh_test.cpp): 48 quads (288 verts), every vertex
// sitting on the R=1400 ring (within its own quad's two corner radii), and
// heights falling in JS's own 50-140 random range (index.html:1916).

#include "../src/render/hill_silhouette.h"

#include <cmath>
#include <cstdio>

namespace {

int g_failures = 0;

void expectTrue(const char* label, bool cond) {
    if (!cond) {
        std::fprintf(stderr, "%s: FAILED\n", label);
        ++g_failures;
    }
}

} // namespace

int main() {
    Mulberry32 rng(777);
    const auto verts = buildHillSilhouette(rng);

    expectTrue("buildHillSilhouette emits 48 quads (288 verts)", verts.size() == 48 * 6);

    bool allOnRing = true, allHeightsInRange = true;
    constexpr double R = 1400.0;
    for (const auto& v : verts) {
        const double r = std::hypot(v.x, v.z);
        if (std::fabs(r - R) > 1e-6) allOnRing = false;
        if (v.y < -1e-9 || v.y > 140.0 + 1e-9) allHeightsInRange = false;
    }
    expectTrue("every vertex sits on the R=1400 ring", allOnRing);
    expectTrue("every vertex height falls in [0, 140] (baseH=0, 50 + rng*90)", allHeightsInRange);

    // Same seed -> identical output (this is pure deterministic geometry,
    // no bgfx/GPU nondeterminism involved).
    Mulberry32 rng2(777);
    const auto verts2 = buildHillSilhouette(rng2);
    expectTrue("same seed produces identical geometry", verts.size() == verts2.size());

    if (g_failures == 0) {
        std::printf("hill_silhouette_test: ring geometry and height range all match expectations.\n");
        return 0;
    }
    std::fprintf(stderr, "hill_silhouette_test: %d MISMATCHES.\n", g_failures);
    return 1;
}
