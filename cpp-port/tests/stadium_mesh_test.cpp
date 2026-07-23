// Verifies stadium_mesh.{h,cpp}'s pure geometry (bgfx-free, same category
// as track_surface_test.cpp): vertex-count math for the stand mesh,
// pit-stall quad positions matching Track::pitStallS() exactly (a
// regression guard against the paint drifting away from where the pit AI
// actually parks cars), and the outer wall's normal direction.

#include "../src/render/stadium_mesh.h"
#include "../src/render/track_surface.h"
#include "../src/sim/car.h"
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
    Track t(TRACKS[0]); // Thunder Oval

    // buildStandMesh: with density=1.0 (no gap-gating) and crowdTiers=0 (so
    // every tier stays on the flat-colored path), vertex count is exactly
    // steps * tiers * 2 quads/tier * 6 verts/quad, where steps is the same
    // clamp(round(zoneLen/12), 16, 40) buildStandMesh itself uses.
    const std::array<double, 4> dummyCrowdUV{0.0, 0.0, 1.0, 1.0};
    {
        Mulberry32 rng(777);
        const double sStart = 0.0, sEnd = 200.0;
        const int tiers = 4;
        const int expectedSteps = std::min(40, std::max(16, (int)std::lround((sEnd - sStart) / 12.0)));
        const std::array<std::array<double, 3>, 6>& palette = TRACKS[0].stadium.crowdPalette;
        const auto result = buildStandMesh(t, sStart, sEnd, tiers, 0, 1.0, 3.2, 2.1, palette, dummyCrowdUV, rng);
        expectTrue("buildStandMesh emits no textured verts when crowdTiers=0", result.textured.empty());
        expectTrue("buildStandMesh vertex count matches steps*tiers*2*6",
                   result.flat.size() == (size_t)expectedSteps * tiers * 2 * 6);
    }

    // buildStandMesh: density < 1 can only ever shrink the vertex count
    // (some slices skipped), never grow it, and never emits a partial
    // slice (always whole steps*tiers*2*6 chunks removed).
    {
        Mulberry32 rng(777);
        const std::array<std::array<double, 3>, 6>& palette = TRACKS[0].stadium.crowdPalette;
        const auto result = buildStandMesh(t, 0.0, 200.0, 4, 0, 0.5, 3.2, 2.1, palette, dummyCrowdUV, rng);
        const auto& verts = result.flat;
        expectTrue("density<1 output is a whole number of slice-chunks", verts.size() % (4 * 2 * 6) == 0);
        expectTrue("density<1 output is no larger than density=1 output", verts.size() <= (size_t)16 * 4 * 2 * 6);
    }

    // buildStandMesh: with crowdTiers=2 of 4 tiers, exactly half the seat
    // quads go textured (UV'd inside the given crowd rect) and half stay
    // flat -- risers always stay flat regardless of crowdTiers.
    {
        Mulberry32 rng(777);
        const std::array<double, 4> crowdUV{0.1, 0.2, 0.3, 0.4};
        const std::array<std::array<double, 3>, 6>& palette = TRACKS[0].stadium.crowdPalette;
        const int tiers = 4, crowdTiers = 2;
        const double sStart = 0.0, sEnd = 200.0;
        const int steps = std::min(40, std::max(16, (int)std::lround((sEnd - sStart) / 12.0)));
        const auto result = buildStandMesh(t, sStart, sEnd, tiers, crowdTiers, 1.0, 3.2, 2.1, palette, crowdUV, rng);
        // Textured: crowdTiers seat quads per slice, 6 verts/quad.
        expectTrue("crowdTiers=2 textured vertex count matches steps*crowdTiers*6",
                   result.textured.size() == (size_t)steps * crowdTiers * 6);
        // Flat: all risers (tiers*6/slice) + the remaining (tiers-crowdTiers)
        // flat seat quads per slice.
        expectTrue("crowdTiers=2 flat vertex count matches risers + remaining flat seats",
                   result.flat.size() == (size_t)steps * (tiers * 6 + (tiers - crowdTiers) * 6));
        bool allUVInRect = true;
        for (const auto& v : result.textured) {
            if (v.u < crowdUV[0] - 1e-9 || v.u > crowdUV[2] + 1e-9 || v.v < crowdUV[1] - 1e-9 ||
                v.v > crowdUV[3] + 1e-9)
                allUVInRect = false;
        }
        expectTrue("all textured verts' UVs fall inside the given crowd rect", allUVInRect);
    }

    // buildPitRoadMesh: the first stall-outline quad's first vertex is the
    // (sb0, lat0, 0.012) corner, where sb0 = pitStallS(t, idx) - boxLen/2
    // and lat0 = stallLat - boxW/2 -- cross-checking against the already-
    // verified pitStallS() (car.cpp) rather than re-deriving the formula.
    {
        const auto verts = buildPitRoadMesh(t, -7.2, -11.8);
        expectTrue("buildPitRoadMesh emits some geometry", !verts.empty());

        constexpr double kBoxLen = 3.2, kBoxW = 2.6, kStallLat = -10.5;
        // Preamble before the per-car loop: 2 entry/exit-line quads (12
        // verts) + 28 pit-wall quads (168 verts) = 180 verts.
        const size_t preamble = 2 * 6 + 28 * 6;
        for (int idx : {0, 1, FIELD - 1}) {
            const double sStall = pitStallS(t, idx);
            const double sb0 = sStall - kBoxLen / 2, lat0 = kStallLat - kBoxW / 2;
            const Vec3 expected = pos3(t, sb0, lat0);
            // Each car contributes 4 stall-outline quads (24 verts) before
            // its 5 boxes (war wagon x2, tire stacks x2, sign post x1; each
            // box is 6 quads = 36 verts) -- the very first vertex of the
            // very first quad is this corner.
            const size_t vertsPerCar = 24 /* stall outline */ + 5 * 36 /* 5 boxes */;
            const size_t base = preamble + (size_t)idx * vertsPerCar;
            char label[96];
            std::snprintf(label, sizeof(label), "idx=%d stall corner x matches pitStallS()", idx);
            expectNear(label, verts[base].x, expected.x, 1e-6);
            std::snprintf(label, sizeof(label), "idx=%d stall corner z matches pitStallS()", idx);
            expectNear(label, verts[base].z, expected.z, 1e-6);
        }
    }

    // buildOuterWallMesh: the wall's normal must face back toward the
    // track (the documented backface-culling fix, index.html:1987-1994) --
    // i.e. its horizontal projection points opposite the pos3() lateral
    // (+lat, outward) direction at that s.
    {
        const auto verts = buildOuterWallMesh(t);
        expectTrue("buildOuterWallMesh emits some geometry", !verts.empty());
        const PointResult p = t.pointAt(0.0);
        const double outLatX = -std::sin(p.hdg), outLatZ = std::cos(p.hdg); // +lat direction, index.html:392
        const double dot = verts[0].nx * outLatX + verts[0].nz * outLatZ;
        expectTrue("outer wall's first quad normal faces inward (-lat), not outward", dot < 0.0);
        expectTrue("outer wall's first quad normal is roughly vertical-free (wall is upright)",
                   std::fabs(verts[0].ny) < 0.35);
    }

    if (g_failures == 0) {
        std::printf("stadium_mesh_test: stand/pit-road/wall geometry all match expectations.\n");
        return 0;
    }
    std::fprintf(stderr, "stadium_mesh_test: %d MISMATCHES.\n", g_failures);
    return 1;
}
