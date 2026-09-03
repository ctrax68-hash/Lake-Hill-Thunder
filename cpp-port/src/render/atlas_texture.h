#pragma once

#include "../sim/rng.h"
#include "../sim/track.h"

#include <array>
#include <cstdint>
#include <vector>

// Port of JS's paintWorldAtlas() + its region-painter helpers (index.html:
// 2890-3024): one shared texture holding the wall pattern, catch-fence
// band, crowd tile, pit-crew figures, and sponsor panels, all sampled via
// UV rects (atlasUV()). Deliberately bgfx-free (same "pure logic" split as
// every other Phase 5 geometry/pixel generator) -- outputs a plain RGBA8
// buffer; renderer.cpp uploads it.
//
// **Logged simplifications** (PORT_PROGRESS.md): (1) a 512x512 atlas
// instead of JS's 2048x2048 WX -- no pixel-exact-fidelity requirement, and
// this port's own region layout (below) doesn't need to match JS's pixel
// offsets, only the same conceptual regions. (2) the wall diamond pattern
// and fence crosshatch are computed analytically per-pixel (closed-form
// rotated-checkerboard / diagonal-stripe math) rather than by literally
// simulating canvas translate+rotate+fillRect/stroke calls -- visually
// equivalent, not a pixel-for-pixel port. (3) sponsor panels are flat
// alternating light/dark panels with a border, no sponsor-name text --
// JS's `drawWord()` is a full custom bitmap-font renderer, out of scope
// for this sub-phase's "texture infrastructure + crowd-tile atlas"
// checklist item; deferred indefinitely, not tied to a specific future
// sub-phase. (4) JS's post-paint gutter blur (anti-seam-bleed technique)
// is skipped -- this port already avoids the same problem via atlasUV()'s
// own inset margin, so there's no seam-bleed to hide in the first place.
// (5) JS's "white texel" (a solid-white patch every vertex-colored
// triangle samples, working around Three.js's single-material-per-mesh
// limitation) has no equivalent here -- this port's flat-colored geometry
// uses an entirely separate unlit-texture-free shader (fs_lit.sc), so
// there's nothing for it to work around.

inline constexpr int kAtlasSize = 512;

struct AtlasRegion {
    int x, y, w, h;
};

// Fixed region layout (see this header's own simplification note #1 for
// why these don't need to match JS's pixel offsets).
inline constexpr AtlasRegion kAtlasWall{0, 0, 256, 128};
inline constexpr AtlasRegion kAtlasFence{256, 0, 256, 128};
inline constexpr AtlasRegion kAtlasCrowd{0, 128, 256, 256};
inline constexpr AtlasRegion kAtlasSponsor{256, 128, 256, 256};
inline constexpr AtlasRegion kAtlasCrew{0, 384, 256, 64};
inline constexpr int kAtlasSponsorCount = 8;

// G31: the world-space footprint one crowd tile covers, which is what makes
// the painted spectators the right SIZE rather than a fine random noise.
//
// The tile is mapped by stadium_mesh.cpp's buildStandMesh(): `kCrowdTile`
// metres along the stand, by one tier's sloped seat face. Those two numbers
// are what a texel is worth in metres, and until G31 nothing on the painting
// side knew them -- the tile was a flat 32x32 grid of 8 px cells regardless,
// which put a "person" at 0.22 m wide and 0.12 m tall. People are not 12 cm
// tall, so the crowd resolved as static instead of as a stand full of
// spectators.
//
// Declared here, next to the region it describes, so the painter and the mesh
// cannot drift apart: stadium_mesh.cpp's kCrowdTile is asserted equal to this
// in atlas_texture_test.
inline constexpr double kCrowdTileWidthM = 7.0;
// Nominal slope length of one seat tier, sqrt(tierD^2 + tierH^2). Real values
// run 3.83 m (Thunder Oval, 3.2/2.1) to 4.77 m (Big Sable, 4.0/2.6); one
// nominal figure is used because a +-20% difference in painted person height
// between tracks is not something anyone can see, and per-track atlases would
// cost four bakes to fix an invisible problem.
inline constexpr double kCrowdTileSlopeM = 4.1;
// A seated adult, shoulders to seat and shoulder to shoulder. These are the
// numbers the grid below is derived from rather than tuned pixel counts.
inline constexpr double kSpectatorWidthM = 0.50;
inline constexpr double kSpectatorHeightM = 1.15;
// Resulting grid. Rounded to whole cells because a partial person at the tile
// edge would tile into a visible seam.
inline constexpr int kCrowdCols = (int)(kCrowdTileWidthM / kSpectatorWidthM + 0.5);   // 14
inline constexpr int kCrowdRows = (int)(kCrowdTileSlopeM / kSpectatorHeightM + 0.5);  // 4

// atlasUV() (index.html:2909-2921): inset a few texels from each tile's
// edge so UVs never sample the literal boundary pixel. Returns
// {u0,v0,u1,v1} normalized to [0,1].
std::array<double, 4> atlasUV(const AtlasRegion& r);
std::array<double, 4> atlasSponsorUV(int i);

// Builds the full atlas as an RGBA8 buffer (row-major, top-left origin --
// matching every other pixel buffer in this port, e.g. sky_texture.h).
// `palette`/`crowdFill` are the track's own Stadium::crowdPalette/
// crowdFill (paintCrowdTile()'s inputs, index.html:3003). `rng` is a
// scenery-only stream (JS's rng2 = mulberry32(777)) -- the SAME instance
// stadium_mesh.h's buildStandMesh() calls use, so callers should pass a
// fresh one if atlas painting order relative to stand-building doesn't
// matter (it doesn't -- see stadium_mesh.h's own "safe to diverge" note).
std::vector<uint8_t> buildAtlasPixels(const std::array<double, 3>& wallColor,
                                       const std::array<std::array<double, 3>, 6>& palette, double crowdFill,
                                       Mulberry32& rng);
