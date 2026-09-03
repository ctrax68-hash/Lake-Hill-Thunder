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

bool laySkidMark(ParticleSystem& ps, const Track& track, const Car& c, double dt) {
    if ((size_t)c.idx >= ps.skidAccum.size()) ps.skidAccum.resize((size_t)c.idx + 1, 0.0);
    double& accum = ps.skidAccum[(size_t)c.idx];

    // BODY SLIP ANGLE, not slipFx.
    //
    // slipFx was the obvious signal and it is unusable: measured over 100 s of
    // real racing on all four tracks, its median, 90th and 99th percentiles
    // are all **1.00**. It saturates, because `pastLimitAny` pins it to 1.0
    // whenever either axle is past its friction ellipse, and in this tire
    // model that is essentially every tick of normal cornering. Gating on it
    // laid marks continuously and pinned the 1200 cap on every track inside
    // the first two minutes -- the whole circuit carpeted in rubber.
    //
    // The angle between where the car points and where it is actually going
    // is not saturated, and it is the plain-language definition of sliding.
    // Measured distribution, degrees:
    //
    //   track            p50    p90    p99    max   >10 deg
    //   Thunder Oval    1.81   6.61  22.70  75.85     3.4%
    //   Milltown        2.54   8.13  32.82  84.31     7.4%
    //   Cedar Valley    1.46   5.38  13.66  45.10     2.0%
    //   Big Sable       3.42   7.50   8.41  14.65     0.1%
    //
    // 10 degrees sits above the 90th percentile on every track, so ordinary
    // cornering lays nothing. It also gives the right per-track character for
    // free: the bullring marks up, the superspeedway almost never does, which
    // is how real tracks behave.
    // Onset swept against real racing rather than picked. Steady-state live
    // marks per track at 100 s, and the peak reached during an incident:
    //
    //   onset   Thunder    Milltown   Cedar     Big Sable
    //    10 deg  418/1006   1192/1200  606/700   23/144
    //   *14 deg  120/733     214/1200  308/410    5/9
    //    18 deg   62/578      55/892   194/268    0/0
    //    22 deg    1/483       9/569   119/177    0/0
    //
    // 14 is the only value where every track lays something -- 18 and above
    // erase the superspeedway entirely -- while the steady state stays modest
    // and only Milltown, the tightest track, touches the 1200 cap, and only
    // transiently during an actual incident, which is exactly when a lot of
    // rubber is correct.
    constexpr double kSlideOnsetRad = 0.2443; // 14 deg
    constexpr double kSlideFullRad = 0.6109;  // 35 deg -- a proper sideways moment
    const double slipAng = std::fabs(std::atan2(c.vy, std::max(1.0, c.v)));
    // Below 8 m/s a car is manoeuvring, not sliding, and marking the pit lane
    // and the grid would be wrong.
    if (c.out || c.v < 8.0 || slipAng < kSlideOnsetRad) {
        // Reset rather than hold: a car that stops sliding and starts again
        // should begin a fresh mark, not inherit credit from the last one.
        accum = 0.0;
        return false;
    }
    // Floored at 0.25 so a mark at the onset angle is faint but visible
    // rather than fading to nothing exactly where it starts.
    const double slide =
        0.25 + 0.75 * std::min(1.0, (slipAng - kSlideOnsetRad) / (kSlideFullRad - kSlideOnsetRad));

    accum += c.v * dt;
    if (accum < kSkidMarkSpacing) return false;

    constexpr double kHalfW = 0.85;   // roughly a car's rear track width
    const double halfL = kSkidMarkSpacing * 0.75;
    // Lifted a few millimetres so it wins the depth test against the surface
    // it is drawn on without visibly floating.
    constexpr double kLift = 0.02;

    // A LOOP, not a single mark. This was written as one-mark-per-call and
    // particles_test caught it: at 40 m/s and 60 fps a car covers 0.667 m per
    // frame, more than the 0.55 m spacing, so a single mark per call silently
    // becomes one mark per FRAME -- 100 marks over the same 66.7 m where 120
    // belong, and a different number again at a different frame rate. That is
    // the exact defect the distance accumulator exists to prevent, reappearing
    // one line further down.
    //
    // The backlog is laid backwards along the track from the car's current
    // station, which is where the car actually was when it earned each one.
    bool laid = false;
    double back = 0.0;
    while (accum >= kSkidMarkSpacing) {
        accum -= kSkidMarkSpacing;
        const double sc = c.s - back;
        back += kSkidMarkSpacing;

        // Corners from the track's own surface function, so the quad lies in
        // the banked plane. Half-length is the spacing, so marks meet.
        const double s0 = sc - halfL, s1 = sc + halfL;
        const Vec3 q[4] = {pos3(track, s0, c.lat - kHalfW), pos3(track, s1, c.lat - kHalfW),
                           pos3(track, s1, c.lat + kHalfW), pos3(track, s0, c.lat + kHalfW)};

        if ((int)ps.skids.size() >= kSkidMarkCap) ps.skids.erase(ps.skids.begin());
        SkidMark m;
        for (int i = 0; i < 4; ++i) {
            m.c[i][0] = (float)q[i].x;
            m.c[i][1] = (float)(q[i].y + kLift);
            m.c[i][2] = (float)q[i].z;
        }
        // Life scales with severity so a light scuff fades before a lock-up.
        m.life = 14.0 + 16.0 * slide;
        m.strength = slide;
        ps.skids.push_back(m);
        laid = true;
    }
    return laid;
}

void ageSkidMarks(ParticleSystem& ps, double dt) {
    // Single compaction pass, not erase-in-place. The particle list next door
    // erases one element at a time (JS's own `shift()`), which is fine at 220
    // entries and is not fine at 1200: a caution that expires a few hundred
    // marks at once would move tens of megabytes for no reason.
    for (SkidMark& m : ps.skids) m.age += dt;
    ps.skids.erase(std::remove_if(ps.skids.begin(), ps.skids.end(),
                                   [](const SkidMark& m) { return m.age >= m.life; }),
                   ps.skids.end());
}

void tickParticles(ParticleSystem& ps, std::vector<Car>& cars, const Track& track, double dt) {
    for (auto& c : cars) {
        const double fwdX = std::cos(c.hdg), fwdZ = std::sin(c.hdg);
        const Vec3 p = pos3(track, c.s, c.lat);
        // L6: rubber under a sliding car. Called before emitCarParticles()
        // because that function decays slipFx as a side effect, and a mark
        // should reflect the slide that just happened rather than what is
        // left of it.
        laySkidMark(ps, track, c, dt);
        // L6: "off the racing surface" is |lat| past the track half-width --
        // the same test step_car.cpp uses to apply its grip penalty, so the
        // dust appears exactly when the handling changes.
        const bool offTrack = std::fabs(c.lat) > track.halfW();
        emitCarParticles(ps, c, p.x, p.y, p.z, fwdX, fwdZ, dt, offTrack);
    }
    integrateParticles(ps, dt);
    ageSkidMarks(ps, dt);
}
