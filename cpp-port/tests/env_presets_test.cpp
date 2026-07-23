// Verifies env_presets.h's pure data/math (bgfx-free, same category as
// track_surface_test.cpp): az/el -> sun-direction conversion, the unknown-
// name fallback to noon-grass (index.html:3522's `|| ENV_PRESETS['noon-grass']`),
// and that each real track's `stadium().env.preset` string actually resolves
// to a distinct preset (a typo guard -- if two tracks silently fell back to
// the same default, this would catch it).

#include "../src/render/env_presets.h"
#include "../src/sim/tracks_data.h"

#include <cmath>
#include <cstdio>

namespace {

int g_failures = 0;

void expectNear(const char* label, double got, double expected, double tol = 1e-9) {
    if (std::fabs(got - expected) > tol) {
        std::fprintf(stderr, "%s: got %.17g expected %.17g (diff %.3g)\n", label, got, expected,
                     got - expected);
        ++g_failures;
    }
}

void expectTrue(const char* label, bool cond) {
    if (!cond) {
        std::fprintf(stderr, "%s: FAILED\n", label);
        ++g_failures;
    }
}

} // namespace

int main() {
    // Straight-up sun (elevation 90) should be pure +y regardless of azimuth.
    {
        const EnvPreset p{123.0, 90.0, {1, 1, 1}, 1.0, {0, 0, 0}, {0, 0, 0}, 1.0};
        const Vec3d dir = envSunDirection(p);
        expectNear("elevation=90 -> x=0", dir.x, 0.0, 1e-9);
        expectNear("elevation=90 -> y=1", dir.y, 1.0, 1e-9);
        expectNear("elevation=90 -> z=0", dir.z, 0.0, 1e-9);
    }
    // Horizon sun (elevation 0) at azimuth 0 should be pure +x.
    {
        const EnvPreset p{0.0, 0.0, {1, 1, 1}, 1.0, {0, 0, 0}, {0, 0, 0}, 1.0};
        const Vec3d dir = envSunDirection(p);
        expectNear("azimuth=0,elevation=0 -> x=1", dir.x, 1.0, 1e-9);
        expectNear("azimuth=0,elevation=0 -> y=0", dir.y, 0.0, 1e-9);
        expectNear("azimuth=0,elevation=0 -> z=0", dir.z, 0.0, 1e-9);
    }
    // Every preset's direction is unit length.
    for (const EnvPreset* p : {&kEnvNoonGrass, &kEnvSunset, &kEnvHazyNoon, &kEnvDuskLights}) {
        const Vec3d dir = envSunDirection(*p);
        const double len = std::sqrt(dir.x * dir.x + dir.y * dir.y + dir.z * dir.z);
        expectNear("preset sun direction is unit length", len, 1.0);
    }

    // Named lookups resolve to the right preset object.
    expectTrue("noon-grass resolves", &resolveEnvPreset("noon-grass") == &kEnvNoonGrass);
    expectTrue("sunset resolves", &resolveEnvPreset("sunset") == &kEnvSunset);
    expectTrue("hazy-noon resolves", &resolveEnvPreset("hazy-noon") == &kEnvHazyNoon);
    expectTrue("dusk-lights resolves", &resolveEnvPreset("dusk-lights") == &kEnvDuskLights);
    // Unknown name falls back to noon-grass (index.html:3522).
    expectTrue("unknown name falls back to noon-grass", &resolveEnvPreset("bogus") == &kEnvNoonGrass);
    expectTrue("empty name falls back to noon-grass", &resolveEnvPreset("") == &kEnvNoonGrass);

    // Each real track's env.preset string resolves to a genuinely distinct
    // preset object from its neighbors (typo guard on the transcribed data).
    const EnvPreset* resolved[4];
    for (size_t i = 0; i < TRACKS.size(); ++i) {
        resolved[i] = &resolveEnvPreset(TRACKS[i].stadium.env.preset);
    }
    expectTrue("Thunder Oval resolves to noon-grass", resolved[0] == &kEnvNoonGrass);
    expectTrue("Milltown resolves to hazy-noon", resolved[1] == &kEnvHazyNoon);
    expectTrue("Cedar Valley resolves to sunset", resolved[2] == &kEnvSunset);
    expectTrue("Big Sable resolves to dusk-lights", resolved[3] == &kEnvDuskLights);
    for (size_t i = 0; i < 4; ++i)
        for (size_t j = i + 1; j < 4; ++j) {
            char label[64];
            std::snprintf(label, sizeof(label), "track %zu and %zu resolve to different presets", i, j);
            expectTrue(label, resolved[i] != resolved[j]);
        }

    if (g_failures == 0) {
        std::printf("env_presets_test: az/el conversion, fallback, and per-track resolution all match.\n");
        return 0;
    }
    std::fprintf(stderr, "env_presets_test: %d MISMATCHES.\n", g_failures);
    return 1;
}
