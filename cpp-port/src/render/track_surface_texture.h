#pragma once

#include <cstdint>
#include <vector>

// G5a (NASCAR-Thunder gap-analysis plan, track surface texture): the track
// ribbon previously got one hardcoded flat vertex color (renderer.cpp's old
// `asphalt` constant) -- zero texture, zero variation. This builds a
// tileable asphalt texture for the ribbon to sample instead, mirroring
// sky_texture.h's "pure logic, bgfx-free" pattern: renderer.cpp's ribbon-
// build loop computes U by wrapping `s / tileLength` into [0,1) in
// software (never relying on hardware texture-wrap, since a fraction is
// always produced before it ever reaches the GPU) and V as the lateral
// fraction across the track's fixed width (0 = inner/apron edge, 1 =
// outer/wall edge) -- so this texture only needs to tile visually at its
// own U=0/U=1 edges, which per-pixel speckle noise does for free (no
// structure to break at the seam, same reason atlas_texture.cpp's crowd-
// noise tile repeats without a visible seam).
inline constexpr int kAsphaltTextureSize = 256;

// G17 (NT2003 presentation plan): how many world units along the track one
// texture tile spans. Previously a hardcoded 8.0 in renderer.cpp; now
// derived from the track's own `Stadium::seamEvery`, which was authored
// per-track from the start but (grep-verified) never read anywhere. Tying
// tile length to it lets a single transverse expansion-seam band painted at
// the tile's U=0 edge reproduce real seam spacing exactly: one seam per
// tile means seams land every `seamEvery` world units. `seamEvery == 0`
// (Big Sable, a modern superspeedway with no visible seams) keeps the old
// 8.0 tiling and paints no seam.
inline double asphaltTileLength(double seamEvery) { return seamEvery > 0.0 ? seamEvery : 8.0; }

// RGBA8, row-major, top-left origin (matching every other pixel buffer in
// this port). Content: a base gray with per-pixel speckle noise (aggregate
// texture), several darker "groove" bands at fixed V (tire-rubber buildup
// along the low/middle/high racing lines), a lighter warm apron tint near
// V=0, a yellow apron boundary line, a white outer blend line, and -- when
// `seamEvery > 0` -- a transverse expansion seam at the tile's U edge.
std::vector<uint8_t> buildAsphaltPixels(double seamEvery);
