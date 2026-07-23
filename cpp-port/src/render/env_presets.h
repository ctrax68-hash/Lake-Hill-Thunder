#pragma once

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
