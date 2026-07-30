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

// RGBA8, row-major, top-left origin (matching every other pixel buffer in
// this port). Content: a base gray with per-pixel speckle noise (aggregate
// texture), a darker "groove" band at a fixed V (tire-rubber buildup on
// the racing line), and a lighter warm apron tint near V=0 (the inner
// edge). No per-track parameters -- unlike the sky/atlas textures, the
// asphalt color itself doesn't vary by track in the original game either.
std::vector<uint8_t> buildAsphaltPixels();
