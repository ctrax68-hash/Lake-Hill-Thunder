#pragma once

#include "../sim/car.h"
#include "../sim/rng.h"
#include "../sim/track.h"

#include <vector>

// H4 (NT2003 engine-feel plan): port of JS's PARTS/spawnP()/emitFX()
// (index.html:3276-3355) -- tire smoke, impact-burst sparks, and wounded-
// engine smoke. Deliberately bgfx-free (this file never touches bgfx),
// same "pure logic vs GPU" split as wheel_animation.h: Renderer only ever
// reads the resulting `particles` list to build camera-facing billboards
// each frame (renderer.cpp's submitWorld()); this file owns spawning,
// aging, physics integration (gravity/drag), and -- like JS's own
// emitFX() -- decaying Car::slipFx/Car::hitFx over real wall-clock time
// (car.h's own comment on hitFx already anticipated this: "decayed by the
// renderer/audio layer, not stepCar()... same category as slipFx").
//
// **Scope note**: JS's emitFX() also spawns an unrelated ambient
// "crowd camera-flash sparkle" effect in the grandstands (index.html:
// 3293-3321), gated on `S.mode==='menu'` or big race moments, reading
// `STAND_ZONES`. That's stadium/crowd dressing, not car feel, and outside
// this plan's stated scope ("make our engine... feel the same" -- the
// engine being the car); not ported here, and not silently dropped either
// -- logged as a deliberate exclusion in PORT_PROGRESS.md.
struct Particle {
    double x = 0, y = 0, z = 0;
    double vx = 0, vy = 0, vz = 0;
    double life = 0, age = 0;
    // World-space half-width AND the spark/smoke classifier (size < 0.15
    // -> spark) at both integration time (gravity direction) and draw time
    // (blend mode/alpha) -- one field serving both roles, exactly matching
    // JS's own `p.size` (index.html:3352,3437).
    double size = 0;
    double col[3] = {1, 1, 1};
};

// JS's `if(PARTS.length > 220) PARTS.shift()` cap (index.html:3280).
constexpr int kParticleCap = 220;

struct ParticleSystem {
    std::vector<Particle> particles;
    // JS's own independent `rngP = mulberry32(4242)` stream
    // (index.html:3277) -- kept separate from the sim's rng/rngR so
    // cosmetic particle jitter can never perturb a determinism-checked
    // replay.
    Mulberry32 rng{4242};
};

// spawnP() (index.html:3279-3282): FIFO-evicts the oldest particle once at
// the cap, exactly like JS's Array.shift().
void spawnParticle(ParticleSystem& ps, double x, double y, double z, double vx, double vy, double vz,
                    double life, double size, const double col[3]);

// The integration half of emitFX() (index.html:3348-3354): ages, expires,
// and moves every live particle. size<0.15 particles (sparks) fall
// (gravity -9.5 m/s^2); everything else (smoke) rises (buoyancy +1.5) --
// JS's own inverted-seeming but deliberate convention (index.html:3352).
// Horizontal drag *0.96/tick on both axes.
void integrateParticles(ParticleSystem& ps, double dt);

// The per-car emission half of emitFX() (index.html:3322-3347): tire smoke
// off the rear while Car::slipFx>0, an impact burst (spark/gray-puff mix)
// while Car::hitFx>0, and a wounded-engine smoke trail while badly damaged
// and still moving -- then decays slipFx/hitFx by the same rates JS uses
// (dt*3.5, dt*2.5). `fwdX`/`fwdZ` is the car's own world-space forward unit
// vector (cos(hdg), sin(hdg)): tire smoke spawns behind the car along
// -forward, engine smoke in front along +forward, matching JS's
// `bx - M[0]*1.7` / `bx + M[0]*1.9` (M's column 0 is the car's own forward
// axis in JS's carModelMat -- see H3's PORT_PROGRESS.md entry for the full
// column-mapping derivation this reuses).
// L6: `offTrack` (|lat| past the track half-width) adds thrown-up dirt from
// the rear wheels. Nothing here keyed on leaving the racing surface before, so
// dropping a wheel onto the grass looked identical to staying on it -- a
// missing visual and, more importantly, a missing cue for the grip penalty
// that lives out there.
void emitCarParticles(ParticleSystem& ps, Car& c, double posX, double posY, double posZ, double fwdX,
                      double fwdZ, double dt, bool offTrack = false);

// Top-level per-frame entry point: for every car, derives its world
// position (pos3(), the same banked-surface function the renderer's own
// per-car draw loop uses for carY) and forward vector from its current
// s/lat/hdg, emits from it, then integrates the whole system once. Called
// once per real (wall-clock) frame from main.cpp -- NOT once per fixed-DT
// sim tick -- exactly where JS's own emitFX(dt) sits in the frame loop
// (after the physics tick loop, alongside audioTick()).
void tickParticles(ParticleSystem& ps, std::vector<Car>& cars, const Track& track, double dt);
