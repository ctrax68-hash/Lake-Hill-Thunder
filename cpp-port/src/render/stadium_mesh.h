#pragma once

#include "../sim/rng.h"
#include "../sim/track.h"

#include <array>
#include <vector>

// Port of JS's grandstand/pit-road/outer-wall geometry builders (index.html:
// 1787-1909's addStand()/addPitRoad(), 1986-2020's per-slice wall loop
// inside buildWorld()). Deliberately bgfx-free (same "pure logic" split as
// track_surface.h/sky_texture.h) -- outputs plain MeshVertex lists;
// renderer.cpp converts to PosNormalColorVertex and uploads.
//
// Phase 5d scope (PORT_PROGRESS.md): flat colors only -- crowd tiers,
// the wall face, and the catch fence are all texture-mapped in JS
// (crowd atlas / diamond-or-sponsor / crosshatch), but Phase 5e's texture
// infrastructure doesn't exist yet. Crowd tiers use a flat palette color
// here regardless of `crowdTiers` (that field only selects which tiers get
// textured once 5e lands -- meaningless before then). The catch fence
// itself is skipped entirely rather than drawn as a second flat wall (a
// solid quad wouldn't read as fencing at all without its crosshatch
// texture); it's added in 5e alongside the crowd atlas. Sponsor-panel wall
// slices, jumbotron/pylon digit geometry, the hill silhouette, and crew-
// figure billboards are all deferred to later sub-phases (5e-5g), noted
// at each call site below.

struct MeshVertex {
    double x, y, z;
    double nx, ny, nz;
    std::array<double, 3> color;
};

// addStand() (index.html:1787-1830): riser + seat quads per tier, tessellated
// into `steps` along-track slices with density-gated gaps. `rng` is a
// scenery-only stream (JS's rng2 = mulberry32(777), index.html:1737) shared
// across all stand-zone calls for one track, matching JS's own call order
// (front, back, corner, corner) -- cosmetic only, doesn't affect gameplay
// determinism (see PORT_PROGRESS.md's existing "safe to diverge" precedent
// for scenery RNG).
std::vector<MeshVertex> buildStandMesh(const Track& track, double sStart, double sEnd, int tiers,
                                        double density, double tierD, double tierH,
                                        const std::array<std::array<double, 3>, 6>& palette, Mulberry32& rng);

// addPitRoad() (index.html:1836-1909): entry/exit lines, pit wall, numbered
// stall box outlines (digits themselves deferred to Phase 5g alongside the
// jumbotron/pylon's own LED-digit geometry), war wagons, tire stacks, a
// pit-sign post (no digit face yet, same deferral), and the small pit-
// control building. Crew-figure billboards are skipped (need the crew
// atlas texture, Phase 5e/5f).
std::vector<MeshVertex> buildPitRoadMesh(const Track& track, double pitOut, double pitIn);

// The outer wall's face + white cap (index.html:1986-2000), flat-colored
// with `theme.wall` -- the diamond/sponsor texture and the catch-fence
// band above it are both deferred to Phase 5e.
std::vector<MeshVertex> buildOuterWallMesh(const Track& track);
