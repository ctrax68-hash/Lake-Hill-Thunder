#include "particles.h"

#include "track_surface.h"

#include <algorithm>
#include <cmath>

void spawnParticle(ParticleSystem& ps, double x, double y, double z, double vx, double vy, double vz,
                    double life, double size, const double col[3]) {
    if ((int)ps.particles.size() > kParticleCap) ps.particles.erase(ps.particles.begin());
    Particle p;
    p.x = x;
    p.y = y;
    p.z = z;
    p.vx = vx;
    p.vy = vy;
    p.vz = vz;
    p.life = life;
    p.age = 0;
    p.size = size;
    p.col[0] = col[0];
    p.col[1] = col[1];
    p.col[2] = col[2];
    ps.particles.push_back(p);
}

void integrateParticles(ParticleSystem& ps, double dt) {
    for (size_t i = ps.particles.size(); i-- > 0;) {
        Particle& p = ps.particles[i];
        p.age += dt;
        if (p.age >= p.life) {
            ps.particles.erase(ps.particles.begin() + (long)i);
            continue;
        }
        p.x += p.vx * dt;
        p.y += p.vy * dt;
        p.z += p.vz * dt;
        p.vy += (p.size < 0.15 ? -9.5 : 1.5) * dt;
        p.vx *= 0.96;
        p.vz *= 0.96;
    }
}

void emitCarParticles(ParticleSystem& ps, Car& c, double posX, double posY, double posZ, double fwdX,
                      double fwdZ, double dt, bool offTrack) {
    // L6: dust when a car runs off the racing surface.
    //
    // Nothing here keyed on leaving the track, so dropping a wheel onto the
    // grass or the apron looked exactly like staying on it -- no dust, no
    // spray, nothing. That is both a missing visual and a missing GAMEPLAY
    // cue: off-track is where the grip penalty lives, and the player had no
    // signal for it beyond the car feeling odd.
    //
    // Brown rather than the tire smoke's grey, and emitted from the rear
    // wheels, so it reads as thrown-up dirt instead of lock-up smoke.
    if (offTrack && c.v > 6.0 && !c.out) {
        Mulberry32& rd = ps.rng;
        const double dustCol[3] = {0.55, 0.44, 0.28};
        const int puffs = c.v > 25.0 ? 2 : 1;
        for (int i = 0; i < puffs; ++i) {
            if (rd.next() < 0.75) {
                spawnParticle(ps, posX - fwdX * 1.6 + (rd.next() - 0.5) * 1.4, posY + 0.15,
                              posZ - fwdZ * 1.6 + (rd.next() - 0.5) * 1.4, (rd.next() - 0.5) * 2.5,
                              0.9 + rd.next() * 1.1, (rd.next() - 0.5) * 2.5, 0.5 + rd.next() * 0.4,
                              0.20, dustCol);
            }
        }
    }

    // Early-out matching JS's own (index.html:3323) -- not load-bearing
    // (nothing below would spawn anyway with all three false), just avoids
    // rolling the RNG for cars with nothing to emit. Sits AFTER the dust
    // above, which has its own trigger and would otherwise be skipped for a
    // clean car sliding through the grass.
    if (!(c.slipFx > 0) && !(c.hitFx > 0) && !(c.dmg > 0.6)) return;

    Mulberry32& r = ps.rng;

    if (c.slipFx > 0) { // tire smoke off the rear (index.html:3326-3330)
        const double col[3] = {0.78, 0.78, 0.81};
        for (int i = 0; i < 2; ++i) {
            if (r.next() < 0.7) {
                spawnParticle(ps, posX - fwdX * 1.7 + (r.next() - 0.5), posY + 0.2,
                              posZ - fwdZ * 1.7 + (r.next() - 0.5), (r.next() - 0.5) * 2, 0.8 + r.next() * 0.8,
                              (r.next() - 0.5) * 2, 0.55 + r.next() * 0.3, 0.17, col);
            }
        }
        c.slipFx = std::max(0.0, c.slipFx - dt * 3.5);
    }

    if (c.hitFx > 0) { // impact burst: sparks + gray puffs (index.html:3332-3341)
        const double sparkCol[3] = {1.0, 0.82, 0.35};
        const double puffCol[3] = {0.60, 0.60, 0.62};
        for (int i = 0; i < 4; ++i) {
            const bool spark = r.next() < 0.6;
            spawnParticle(ps, posX + (r.next() - 0.5) * 1.6, posY + 0.4 + r.next() * 0.5,
                          posZ + (r.next() - 0.5) * 1.6, (r.next() - 0.5) * 8, 1 + r.next() * 4,
                          (r.next() - 0.5) * 8, spark ? 0.3 + r.next() * 0.2 : 0.55 + r.next() * 0.35,
                          spark ? 0.07 : 0.16, spark ? sparkCol : puffCol);
        }
        c.hitFx = std::max(0.0, c.hitFx - dt * 2.5);
    }

    if (c.dmg > 0.6 && !c.out && c.v > 5 && r.next() < 0.4) { // wounded engine smoke (index.html:3343-3346)
        const double col[3] = {0.38, 0.38, 0.40};
        spawnParticle(ps, posX + fwdX * 1.9, posY + 0.65, posZ + fwdZ * 1.9, (r.next() - 0.5),
                      0.7 + r.next() * 0.7, (r.next() - 0.5), 0.9, 0.15, col);
    }
}

void tickParticles(ParticleSystem& ps, std::vector<Car>& cars, const Track& track, double dt) {
    for (auto& c : cars) {
        const double fwdX = std::cos(c.hdg), fwdZ = std::sin(c.hdg);
        const Vec3 p = pos3(track, c.s, c.lat);
        // L6: "off the racing surface" is |lat| past the track half-width --
        // the same test step_car.cpp uses to apply its grip penalty, so the
        // dust appears exactly when the handling changes.
        const bool offTrack = std::fabs(c.lat) > track.halfW();
        emitCarParticles(ps, c, p.x, p.y, p.z, fwdX, fwdZ, dt, offTrack);
    }
    integrateParticles(ps, dt);
}
