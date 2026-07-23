// Verifies track_surface.{h,cpp}'s pure geometry math (surfH()/pos3()/
// surfaceUp(), the JS "3D surface model" port, index.html:377-395,3064-3071)
// -- zero bgfx dependency, same category as track_test.cpp itself. Rather
// than hardcoding new expected banking values, these tests cross-check
// against Track::bankAt() itself (already verified in track_test.cpp), so
// no bank-angle magic numbers are re-derived here.

#include "../src/render/track_surface.h"
#include "../src/sim/tracks_data.h"

#include <cmath>
#include <cstdio>

namespace {

int g_failures = 0;

void expectNear(const char* label, double got, double expected, double tol = 1e-9) {
    if (std::fabs(got - expected) > tol) {
        std::fprintf(stderr, "%s: got %.17g expected %.17g (diff %.3g)\n", label, got, expected,
                     got - expected);
        ++g_failures;
    }
}

void expectTrue(const char* label, bool cond) {
    if (!cond) {
        std::fprintf(stderr, "%s: FAILED\n", label);
        ++g_failures;
    }
}

} // namespace

int main() {
    Track t(TRACKS[0]);
    const double total = t.total();
    const double halfW = t.halfW();

    // surfH() baseline: at the inner apron edge (lat=-halfW), l+halfW==0
    // regardless of bank, so height is always the flat +0.02 offset.
    for (double s : {0.0, 37.5, 123.456, total * 0.5, total - 1.0}) {
        char label[96];
        std::snprintf(label, sizeof(label), "surfH(s=%.4g, lat=-halfW) baseline", s);
        expectNear(label, surfH(t, s, -halfW), 0.02);
    }

    // surfH() linearity in lat within the clamp range: the height's slope
    // w.r.t. lat is tan(bankAt(s)), independent of lat itself.
    {
        const double s = total * 0.25;
        const double b = t.bankAt(s);
        const double h1 = surfH(t, s, 0.0);
        const double h2 = surfH(t, s, 2.0);
        expectNear("surfH linearity in lat", (h2 - h1) / 2.0, std::tan(b), 1e-9);
    }

    // surfH() clamps lat beyond WALL_LAT/APRON_IN.
    {
        const double s = total * 0.25;
        expectNear("surfH clamps beyond WALL_LAT", surfH(t, s, wallLat(t) + 50.0), surfH(t, s, wallLat(t)));
        expectNear("surfH clamps beyond APRON_IN", surfH(t, s, apronIn(t) - 50.0), surfH(t, s, apronIn(t)));
    }

    // pos3() at lat=0 matches pointAt()'s x/y exactly (the lateral offset
    // term vanishes regardless of the cos(bank) scale factor).
    for (double s : {0.0, 100.0, total * 0.6}) {
        const PointResult p = t.pointAt(s);
        const Vec3 v = pos3(t, s, 0.0);
        char label[96];
        std::snprintf(label, sizeof(label), "pos3(s=%.4g, lat=0).x matches pointAt", s);
        expectNear(label, v.x, p.x);
        std::snprintf(label, sizeof(label), "pos3(s=%.4g, lat=0).z matches pointAt", s);
        expectNear(label, v.z, p.y);
    }

    // surfaceUp(): unit length, and its components decompose exactly into
    // cos(bankAt(s)) (vertical) / sin(bankAt(s)) (horizontal magnitude) --
    // cross-checking against Track::bankAt()'s own already-verified sign
    // convention rather than hardcoding a new expected angle.
    for (double s : {0.0, 50.0, total * 0.3, total * 0.75}) {
        const double b = t.bankAt(s);
        const Vec3 up = surfaceUp(t, s);
        const double len = std::sqrt(up.x * up.x + up.y * up.y + up.z * up.z);
        char label[96];
        std::snprintf(label, sizeof(label), "surfaceUp(s=%.4g) is unit length", s);
        expectNear(label, len, 1.0);
        std::snprintf(label, sizeof(label), "surfaceUp(s=%.4g).y == cos(bankAt(s))", s);
        expectNear(label, up.y, std::cos(b), 1e-9);
        const double horizLen = std::sqrt(up.x * up.x + up.z * up.z);
        std::snprintf(label, sizeof(label), "surfaceUp(s=%.4g) horizontal magnitude == sin(bankAt(s))", s);
        expectNear(label, horizLen, std::sin(b), 1e-9);
        // JS's carBasis() flips if up.y<0; never triggers for this game's
        // bank range (b well under 90 degrees) -- confirm that holds here.
        std::snprintf(label, sizeof(label), "surfaceUp(s=%.4g).y is non-negative", s);
        expectTrue(label, up.y >= 0.0);
    }

    if (g_failures == 0) {
        std::printf("track_surface_test: surfH/pos3/surfaceUp match expectations.\n");
        return 0;
    }
    std::fprintf(stderr, "track_surface_test: %d MISMATCHES.\n", g_failures);
    return 1;
}
