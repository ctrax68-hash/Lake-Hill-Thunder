#pragma once

#include "env_presets.h"
#include "../sim/track.h"

#include <cstdint>
#include <vector>

// Port of JS's buildSkyTexture() (index.html:3724-3766): a small "painted
// backdrop" texture -- vertical gradient (zenith -> horizon -> a slightly
// lightened horizon-haze band) plus a stylized sun-glow blob positioned by
// elevation, plus two faint soft cloud streaks. Explicitly a flat SCREEN-
// SPACE backdrop (JS: `scene.background = <this texture>`, not a cube/
// equirect map) -- it never rotates with camera direction, same
// simplification the JS original documents at its own call site.

inline constexpr int kSkyTextureWidth = 128;
inline constexpr int kSkyTextureHeight = 256;

// RGBA8, row-major, row 0 = top = zenith, row (height-1) = bottom = the
// lightened horizon-haze band. sunPreset may be null (JS's own
// `if(sunPreset)` branch is optional too), though every real call site
// resolves one via env_presets.h's resolveEnvPreset().
std::vector<uint8_t> buildSkyPixels(const Sky& sky, const EnvPreset* sunPreset);
