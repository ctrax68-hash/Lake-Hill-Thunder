// Verifies pylon_mesh.{h,cpp}'s pure geometry (bgfx-free, same category as
// stadium_mesh_test.cpp): buildPylonMesh()/buildJumbotronMesh() emit nothing
// on tracks without `stadium.pylon`/`stadium.jumbotron` set, and emit real
// geometry (a support structure + number-decal quads) on Big Sable, the
// only track with both flags set (tracks_data.h).

#include "../src/render/pylon_mesh.h"
#include "../src/sim/tracks_data.h"

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
    Track thunderOval(TRACKS[0]);
    Track bigSable(TRACKS[3]);
    expectTrue("Thunder Oval has pylon=false (sanity check on tracks_data.h)", !TRACKS[0].stadium.pylon);
    expectTrue("Thunder Oval has jumbotron=false (sanity check on tracks_data.h)", !TRACKS[0].stadium.jumbotron);
    expectTrue("Big Sable has pylon=true (sanity check on tracks_data.h)", TRACKS[3].stadium.pylon);
    expectTrue("Big Sable has jumbotron=true (sanity check on tracks_data.h)", TRACKS[3].stadium.jumbotron);

    expectTrue("buildPylonMesh emits nothing on a non-pylon track", buildPylonMesh(thunderOval).empty());
    expectTrue("buildJumbotronMesh emits nothing on a non-jumbotron track", buildJumbotronMesh(thunderOval).empty());

    const auto pylon = buildPylonMesh(bigSable);
    const auto jumbotron = buildJumbotronMesh(bigSable);
    expectTrue("buildPylonMesh emits geometry on Big Sable", !pylon.empty());
    expectTrue("buildJumbotronMesh emits geometry on Big Sable", !jumbotron.empty());

    // Every emitted vertex is a well-formed triangle vertex: a unit-length
    // (or zero, for degenerate slivers -- none expected here) normal.
    bool allNormalsUnitOrZero = true;
    for (const auto& v : pylon) {
        const double len = std::sqrt(v.nx * v.nx + v.ny * v.ny + v.nz * v.nz);
        if (len > 1e-9 && std::fabs(len - 1.0) > 1e-6) allNormalsUnitOrZero = false;
    }
    expectTrue("pylon mesh normals are unit-length", allNormalsUnitOrZero);

    // The pylon pole + at least one lit number-decal quad both sit above
    // ground level (y=0) somewhere in the mesh.
    bool anyAboveGround = false;
    for (const auto& v : pylon) {
        if (v.y > 1.0) anyAboveGround = true;
    }
    expectTrue("pylon mesh has geometry above ground level", anyAboveGround);

    if (g_failures == 0) {
        std::printf("pylon_mesh_test: pylon/jumbotron per-track gating and geometry all match expectations.\n");
        return 0;
    }
    std::fprintf(stderr, "pylon_mesh_test: %d MISMATCHES.\n", g_failures);
    return 1;
}
