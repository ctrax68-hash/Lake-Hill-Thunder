#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <string>

// Port of JS's ENV_PRESETS (index.html:3490-3509) + applyEnvPreset()'s
// azimuth/elevation -> direction math (index.html:3520-3530). Each preset is
// a full per-track lighting mood: one directional "sun" (azimuth/elevation/
// color/intensity) plus one hemisphere ambient (sky/ground color/intensity).
// A preset is shared by name across tracks (Stadium::env.preset) -- it is
// not itself per-track data, matching the JS source's own layout.

struct EnvPreset {
    double azimuthDeg, elevationDeg;
    std::array<double, 3> sunColor; // 0-1, JS's 0xRRGGBB already normalized
    double sunIntensity;
    std::array<double, 3> hemiSky;
    std::array<double, 3> hemiGround;
    double hemiIntensity;
};

namespace detail {
// Transcribes one JS 0xRRGGBB hex literal into a normalized {r,g,b}.
inline constexpr std::array<double, 3> hexRgb(unsigned hex) {
    return {((hex >> 16) & 0xFF) / 255.0, ((hex >> 8) & 0xFF) / 255.0, (hex & 0xFF) / 255.0};
}
} // namespace detail

// index.html:3491
inline const EnvPreset kEnvNoonGrass{35, 55, detail::hexRgb(0xfff4e0), 3.2, detail::hexRgb(0xbfd6ff),
                                      detail::hexRgb(0x332f28), 1.1};
// index.html:3498
inline const EnvPreset kEnvSunset{250, 12, detail::hexRgb(0xffab66), 2.6, detail::hexRgb(0xb8c8e0),
                                   detail::hexRgb(0x362e28), 1.05};
// index.html:3502
inline const EnvPreset kEnvHazyNoon{55, 64, detail::hexRgb(0xfff0da), 2.85, detail::hexRgb(0xd7e4f2),
                                     detail::hexRgb(0x3a3324), 1.0};
// index.html:3507
inline const EnvPreset kEnvDuskLights{265, 6, detail::hexRgb(0xffc48a), 2.2, detail::hexRgb(0x2a3c66),
                                       detail::hexRgb(0x241f1a), 1.15};

// Env{preset} -> EnvPreset, falling back to noon-grass for an unrecognized
// name (matching JS's `ENV_PRESETS[stadium.env.preset] || ENV_PRESETS['noon-grass']`,
// index.html:3522).
inline const EnvPreset& resolveEnvPreset(const std::string& name) {
    if (name == "sunset") return kEnvSunset;
    if (name == "hazy-noon") return kEnvHazyNoon;
    if (name == "dusk-lights") return kEnvDuskLights;
    return kEnvNoonGrass;
}

// G25: the light multiplier this preset produces on a FLAT-UP surface --
// `luminance(hemiSky) + luminance(sunColor) * sin(elevation)`, i.e. exactly
// what fs_lit.sc computes for a normal of (0,1,0). This is the number that
// makes the track read bright and flat: the JS-inherited intensities put
// noon-grass at ~3.4x, so 0.25-albedo asphalt arrives near 0.86 before
// tonemapping.
inline double envFlatUpMultiplier(const EnvPreset& preset) {
    auto lum = [](const std::array<double, 3>& c, double k) {
        return (0.2126 * c[0] + 0.7152 * c[1] + 0.0722 * c[2]) * k;
    };
    const double sinEl = std::max(0.0, std::sin(preset.elevationDeg * M_PI / 180.0));
    return lum(preset.hemiSky, preset.hemiIntensity) + lum(preset.sunColor, preset.sunIntensity) * sinEl;
}

// G25: exposure for a preset, normalising its flat-up multiplier toward
// `target` -- but ONLY DOWNWARD, which is the whole point.
//
// Normalising every preset up AND down to one target would defeat the purpose
// of having presets: dusk-lights sits at 0.45 by design, and "correcting" it to
// 1.35 would turn dusk into midday. Clamping at 1.0 means overexposed presets
// (noon-grass 3.43, hazy-noon 3.31) get tamed while presets that are already
// reasonable (sunset 1.21) or deliberately dark (dusk-lights) are left alone.
inline double envExposure(const EnvPreset& preset, double target) {
    const double flatUp = envFlatUpMultiplier(preset);
    if (flatUp <= 1e-6) return 1.0;
    return std::min(1.0, target / flatUp);
}

// Unit direction TOWARD the sun, matching THREE.DirectionalLight's own
// position-as-direction convention (light aimed at the origin, so its
// position vector doubles as the direction, index.html:3524). Already unit
// length: cos^2(el)*(cos^2(az)+sin^2(az)) + sin^2(el) == 1.
struct Vec3d {
    double x, y, z;
};
inline Vec3d envSunDirection(const EnvPreset& preset) {
    const double az = preset.azimuthDeg * M_PI / 180.0;
    const double el = preset.elevationDeg * M_PI / 180.0;
    return {std::cos(az) * std::cos(el), std::sin(el), std::sin(az) * std::cos(el)};
}
