#pragma once

#include <cstdint>
#include <vector>

// H4 (NT2003 engine-feel plan): port of JS's buildParticleSprite()
// (index.html:3357-3373) -- a soft radial-gradient billboard sprite, built
// procedurally on the CPU like every other texture in this port (sky,
// livery, atlas). Pure white RGB throughout (only alpha varies), so each
// particle's own vertex color tints it directly when the fragment shader
// multiplies texel * vertexColor -- matches JS's `rgba(255,255,255,a)`
// gradient stops, which never touch RGB either.
inline constexpr int kParticleTextureSize = 64;

// RGBA8, row-major. Alpha stops 1.0 at center, 0.55 at 50% radius, 0.0 at
// the rim -- JS's exact 3 gradient stops.
std::vector<uint8_t> buildParticleSpritePixels();
