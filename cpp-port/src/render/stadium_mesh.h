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
    // Phase 5e (PORT_PROGRESS.md): unused (0,0) by every flat-colored
    // caller; only buildStandMesh()'s `textured` output sets these to
    // real crowd-atlas UVs. Kept on the one shared vertex type rather than
    // introducing a second struct, matching this sub-phase's own "reuse
    // over parallel-struct sprawl" call.
    double u = 0.0, v = 0.0;
};

// buildStandMesh()'s two output meshes: `flat` (risers + upper-tier seats,
// vertex-colored, drawn through the existing lit/flat-color pipeline) and
// `textured` (front `crowdTiers` seats, UV'd into the crowd atlas region,
// drawn through Phase 5e's new textured-lit pipeline).
struct StandMeshResult {
    std::vector<MeshVertex> flat;
    std::vector<MeshVertex> textured;
};

// addStand() (index.html:1787-1830): riser + seat quads per tier, tessellated
// into `steps` along-track slices with density-gated gaps. `rng` is a
// scenery-only stream (JS's rng2 = mulberry32(777), index.html:1737) shared
// across all stand-zone calls for one track, matching JS's own call order
// (front, back, corner, corner) -- cosmetic only, doesn't affect gameplay
// determinism (see PORT_PROGRESS.md's existing "safe to diverge" precedent
// for scenery RNG). `crowdTiers` selects how many of the front tiers (t <
// crowdTiers) get the textured crowd-atlas path vs. the flat palette path
// (index.html:1816-1821) -- Phase 5d always used the flat path for every
// tier since the atlas didn't exist yet; Phase 5e is what makes this
// parameter meaningful. `crowdUV` is the crowd region's UV rect
// (atlasUV(kAtlasCrowd), atlas_texture.h) -- passed in rather than computed
// here so this header stays free of any atlas_texture.h dependency.
StandMeshResult buildStandMesh(const Track& track, double sStart, double sEnd, int tiers, int crowdTiers,
                                double density, double tierD, double tierH,
                                const std::array<std::array<double, 3>, 6>& palette,
                                const std::array<double, 4>& crowdUV, Mulberry32& rng);

// G18 (NT2003 presentation plan): the grandstand's roof line -- a painted
// sponsor band immediately above the top row of crowd, a dark press-box
// fascia above that, and an angled roof cap. Every frame of the reference
// footage has this horizontal element capping the bowl; without it the
// stands just stop in mid-air. `tiers`/`tierD`/`tierH` are the SAME values
// passed into the matching buildStandMesh() call, so the roof's height and
// lateral position derive from the stand it sits on rather than restating
// the numbers (same rule G14's suite tower follows). `accent` tints the
// sponsor band -- pass the track's own theme wall color.
std::vector<MeshVertex> buildStandRoofMesh(const Track& track, double sStart, double sEnd, int tiers, double tierD,
                                            double tierH, const std::array<double, 3>& accent);

// addPitRoad() (index.html:1836-1909): entry/exit lines, pit wall, numbered
// stall box outlines (digits themselves deferred to Phase 5g alongside the
// jumbotron/pylon's own LED-digit geometry), war wagons, tire stacks, a
// pit-sign post (no digit face yet, same deferral), and the small pit-
// control building. Crew-figure billboards are skipped (need the crew
// atlas texture, Phase 5e/5f).
std::vector<MeshVertex> buildPitRoadMesh(const Track& track, double pitOut, double pitIn);

// G19 (NT2003 presentation plan): pit-crew billboards, one per stall.
// `atlas_texture.cpp`'s paintCrewTile() has painted three crew silhouettes
// into kAtlasCrew since Phase 5e, and (grep-verified) **no mesh has ever
// sampled it** -- the third instance of the same dead-atlas-content
// situation G10 (kAtlasFence) and G11 (sponsorDensity) each fixed once.
// buildPitRoadMesh()'s own comment above even records the deferral
// ("crew-figure billboards are deferred"). Returned separately rather than
// folded into buildPitRoadMesh() because these are textured and everything
// that function emits is flat-colored -- keeps its signature and its single
// return type intact, matching how buildOuterWallMesh() and friends are
// already appended through renderer.cpp's own appendTextured() path.
std::vector<MeshVertex> buildPitCrewMesh(const Track& track);

// The outer wall's face (index.html:1986-2000). G5b (NASCAR-Thunder
// gap-analysis plan) wired this into the atlas's wall-diamond region
// (kAtlasWall) -- real UVs now, not a flat color; the catch-fence band
// above it remains deferred (no geometry pass exists for it at all).
std::vector<MeshVertex> buildOuterWallMesh(const Track& track);

// G5b: small sponsor-panel quads along each straightaway, cycling through
// the atlas's 8 pre-painted sponsor-panel UV rects (atlasSponsorUV()).
// `sponsorDensity` is the track's own Stadium::sponsorDensity (G11,
// NASCAR-Thunder gap-analysis plan: this field was authored with a
// distinct value per track from the start but never actually read by
// this function until now) -- scales the gap between panels so denser-
// sponsored tracks (e.g. Milltown Bullring) pack panels tighter than
// sparser ones (e.g. Cedar Valley).
std::vector<MeshVertex> buildSponsorPanelsMesh(const Track& track, double sponsorDensity);

// G12 (NASCAR-Thunder gap-analysis plan): real tracks plaster distinct
// large sponsor panels at each corner apex ("Turn 1 presented by X"),
// separate from straightaway signage -- corners were previously bare of
// any signage (buildSponsorPanelsMesh() above only ever covers seg0/
// seg2, the straights). One panel per corner (seg1/seg3), a dark bezel
// backing quad plus the corner's own number rendered via the existing
// LED-segment digit geometry (digit_mesh.h's addNumber(), already used
// for the pylon/jumbotron) -- reuses this port's own established numeric-
// display convention instead of adding bitmap-font text to the atlas
// (out of scope, same reasoning atlas_texture.h's own header already
// gives for skipping sponsor-name text).
std::vector<MeshVertex> buildTurnSignageMesh(const Track& track);

// G13 (NASCAR-Thunder gap-analysis plan): a slender elevated flag stand
// beside the start/finish line -- a support pole plus a "crow's nest"
// booth on top with a checkered accent band, following the same
// addBox()/crossPt()/wallLat() track-side-prop conventions
// buildPitRoadMesh() and pylon_mesh.cpp already use.
std::vector<MeshVertex> buildFlagStandMesh(const Track& track);

// G14 (NASCAR-Thunder gap-analysis plan): a suite/press-box tower behind
// the front-tier grandstand. `frontTiers`/`tierD`/`tierH` are the SAME
// three values already passed into buildStandMesh()'s own front-tier
// call (Stadium::standTier.front / standScale.tierD / standScale.tierH)
// -- required so this tower's position derives from the actual stand
// geometry it sits behind rather than duplicating/guessing those
// numbers.
std::vector<MeshVertex> buildSuiteTowerMesh(const Track& track, int frontTiers, double tierD, double tierH);

// G10 (NASCAR-Thunder gap-analysis plan): the catch fence -- `atlas_
// texture.cpp` has painted a crosshatch fence band into kAtlasFence since
// Phase 5e, but (confirmed via grep) no mesh ever sampled it; this port's
// own header comments elsewhere explicitly called the fence "deferred,
// no geometry pass exists for it at all." A thin vertical band sitting
// just above the outer wall, same per-slice loop and atlas-wrap technique
// as buildOuterWallMesh(). `fenceHeight` is the track's own
// Stadium::fenceHeight (taller on the superspeedway, per real catch-fence
// height varying by track type).
std::vector<MeshVertex> buildCatchFenceMesh(const Track& track, double fenceHeight);

// G29: a tire stack and a fuel rig beside every pit stall. The stalls had a
// war wagon and a crew billboard and nothing else, so pit road read as a line
// of identical boxes; the reference's pit lane is cluttered, and the clutter
// is what makes it look like work happens there. Placed on the wall side of
// the stall so nothing sits where a car actually stops.
std::vector<MeshVertex> buildPitEquipmentMesh(const Track& track);

// G29 (graphics pass): the infield -- garage row, haulers, a suite/media
// block and light towers. This area had NO geometry at all before: it was
// bare grass from the apron to the horizon, and on a left-turning oval the
// infield is in shot for most of the lap, which made it the largest empty
// space in the game. `rng` is the shared scenery-only stream (the same
// cosmetic-determinism precedent buildStandMesh()/buildSurfacePatchesMesh()
// already set).
std::vector<MeshVertex> buildInfieldMesh(const Track& track, Mulberry32& rng);

// G28 (graphics pass): the catch fence's structure -- posts marching along
// the wall, two horizontal rails, and the curved top return that bends back
// over the track. buildCatchFenceMesh() draws only the mesh band, which reads
// as a fence at distance and as nothing at all up close; this is what makes it
// legible. Kept a separate mesh so the band keeps its own texture and wrap
// while the structure stays flat-coloured geometry.
std::vector<MeshVertex> buildFenceStructureMesh(const Track& track, double fenceHeight);

// G28: red-and-white striped barriers on the corner exits -- one of the most
// recognisable things in the reference footage, and useful beyond decoration:
// a striped block at a corner exit is a braking/turn-in reference that a
// uniform grey wall does not give the player.
std::vector<MeshVertex> buildCornerBarrierMesh(const Track& track);

// G28: a sponsor arch spanning the track just past the start/finish line.
// Returns the flat-coloured structure (legs and beam backing); the beam's
// board is TEXTURED and is appended to `texturedOut` instead, because it
// samples the same atlasSponsorUV() tiles the trackside boards do and so has
// to go into the textured-lit draw rather than the flat one. Reusing those
// tiles rather than authoring a ninth means the arch picks up any change to
// the invented wordmark set automatically.
std::vector<MeshVertex> buildSponsorArchMesh(const Track& track, std::vector<MeshVertex>& texturedOut);

// G17 (NT2003 presentation plan): resurfacing patches on the racing
// surface. `patches` is the track's own `Stadium::patches` -- authored
// per-track from the start (only Cedar Valley is non-zero, at 6) but
// (grep-verified) never read by anything until now, the same dead-data
// situation G10 (kAtlasFence) and G11 (sponsorDensity) each fixed once.
// Deliberately geometry rather than texture content: the asphalt texture
// tiles every `seamEvery` world units, so a patch painted into it would
// repeat at every tile and read as a pattern rather than as repair work.
// `rng` is the shared scenery-only stream (same cosmetic-determinism
// precedent buildStandMesh() already sets).
std::vector<MeshVertex> buildSurfacePatchesMesh(const Track& track, int patches, Mulberry32& rng);

// G5a (NASCAR-Thunder gap-analysis plan, track surface texture): a
// checkered start/finish stripe at s=0, flat vertex-colored (see this
// function's own comment in stadium_mesh.cpp for why this isn't part of
// the new asphalt texture).
std::vector<MeshVertex> buildStartFinishMesh(const Track& track);
