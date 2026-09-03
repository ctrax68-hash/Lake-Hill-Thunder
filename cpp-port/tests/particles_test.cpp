// H4 (NT2003 engine-feel plan): exercises particles.h's pure spawn/
// integrate/emit logic, bgfx-free, same hand-rolled expect/expectNear
// pattern as wheel_animation_test.cpp.

#include "../src/render/particles.h"
#include "../src/sim/tracks_data.h"

#include <cmath>
#include <cstdio>

namespace {
int g_failures = 0;

void expect(bool cond, const char* what) {
    if (!cond) {
        std::fprintf(stderr, "FAIL: %s\n", what);
        ++g_failures;
    }
}

void expectNear(const char* label, double got, double expected, double tol = 1e-9) {
    if (std::fabs(got - expected) > tol) {
        std::fprintf(stderr, "%s: got %.9g expected %.9g (diff %.3g)\n", label, got, expected, got - expected);
        ++g_failures;
    }
}
} // namespace

int main() {
    // ---- spawnParticle(): basic fields + FIFO cap ----
    {
        ParticleSystem ps;
        const double col[3] = {0.1, 0.2, 0.3};
        spawnParticle(ps, 1, 2, 3, 4, 5, 6, 0.5, 0.15, col);
        expect(ps.particles.size() == 1, "spawnParticle adds one particle");
        const Particle& p = ps.particles[0];
        expectNear("spawned x", p.x, 1.0);
        expectNear("spawned y", p.y, 2.0);
        expectNear("spawned z", p.z, 3.0);
        expectNear("spawned vx", p.vx, 4.0);
        expectNear("spawned vy", p.vy, 5.0);
        expectNear("spawned vz", p.vz, 6.0);
        expectNear("spawned life", p.life, 0.5);
        expectNear("spawned age", p.age, 0.0);
        expectNear("spawned size", p.size, 0.15);
        expectNear("spawned col r", p.col[0], 0.1);
        expectNear("spawned col g", p.col[1], 0.2);
        expectNear("spawned col b", p.col[2], 0.3);
    }
    {
        // JS's `if(PARTS.length > 220) PARTS.shift()` (index.html:3280):
        // the cap check happens BEFORE the push, so it never actually
        // evicts until the (kParticleCap+1)-th spawn call, and at that
        // point it evicts exactly the OLDEST (first-spawned) particle.
        ParticleSystem ps;
        const double col[3] = {1, 1, 1};
        for (int i = 0; i < kParticleCap; ++i) spawnParticle(ps, i, 0, 0, 0, 0, 0, 1, 0.1, col);
        expect((int)ps.particles.size() == kParticleCap, "fills to exactly the cap with no eviction yet");
        expectNear("oldest particle still present (x=0)", ps.particles.front().x, 0.0);
        spawnParticle(ps, 999, 0, 0, 0, 0, 0, 1, 0.1, col); // the (cap+1)-th spawn
        expect((int)ps.particles.size() == kParticleCap + 1,
               "spawnParticle never evicts on the same call that would exceed the cap (JS: shift() happens "
               "before push(), so a cap of 220 briefly holds 221)");
        spawnParticle(ps, 1000, 0, 0, 0, 0, 0, 1, 0.1, col); // this one evicts
        expect((int)ps.particles.size() == kParticleCap + 1, "size stays capped at (cap+1) once eviction starts");
        expectNear("oldest particle (x=0) was evicted", ps.particles.front().x, 1.0);
    }

    // ---- integrateParticles(): aging/expiry, gravity direction, drag ----
    {
        ParticleSystem ps;
        const double smokeCol[3] = {0.7, 0.7, 0.7};
        const double sparkCol[3] = {1, 0.8, 0.3};
        spawnParticle(ps, 0, 0, 0, 10, 0, 10, 1.0, 0.17, smokeCol); // smoke: size>=0.15
        spawnParticle(ps, 0, 0, 0, 10, 0, 10, 1.0, 0.07, sparkCol); // spark: size<0.15
        integrateParticles(ps, 0.1);
        expect(ps.particles.size() == 2, "neither particle expires after 0.1s of a 1.0s life");
        // Position integrates BEFORE the gravity term is added for this
        // step (index.html:3351-3352 order) -- so after one 0.1s step,
        // x/z reflect the ORIGINAL vx/vz (10), not yet drag-reduced.
        expectNear("smoke x after one step", ps.particles[0].x, 1.0);
        expectNear("smoke z after one step", ps.particles[0].z, 1.0);
        // Smoke (size>=0.15) rises: vy should have INCREASED from 0 by
        // +1.5*dt (buoyancy), not fallen.
        expectNear("smoke vy rises (buoyancy)", ps.particles[0].vy, 1.5 * 0.1);
        // Spark (size<0.15) falls: vy should have DECREASED from 0 by
        // -9.5*dt (gravity).
        expectNear("spark vy falls (gravity)", ps.particles[1].vy, -9.5 * 0.1);
        // Horizontal drag: vx,vz *= 0.96 each step, for BOTH particle kinds.
        expectNear("smoke vx drag", ps.particles[0].vx, 10.0 * 0.96);
        expectNear("spark vz drag", ps.particles[1].vz, 10.0 * 0.96);

        // Advance well past both particles' 1.0s life -- both must expire.
        integrateParticles(ps, 1.0);
        expect(ps.particles.empty(), "both particles expire once age reaches life");
    }

    // ---- emitCarParticles(): conditional spawning + slipFx/hitFx decay ----
    {
        ParticleSystem ps;
        Car c{};
        c.slipFx = 0;
        c.hitFx = 0;
        c.dmg = 0;
        emitCarParticles(ps, c, 0, 0, 0, 1, 0, 0.02);
        expect(ps.particles.empty(), "no emission at all with slipFx=hitFx=0 and dmg<=0.6");
    }
    {
        // The tire-smoke loop is stochastic (2 tries/call at 70% each,
        // index.html:3327) -- call a few times (each resetting slipFx back
        // up, since a real car would still be sliding) rather than relying
        // on one call's exact RNG draws, so this doesn't depend on exactly
        // which values Mulberry32(4242)'s first two draws happen to be.
        ParticleSystem ps;
        Car c{};
        for (int i = 0; i < 5; ++i) {
            c.slipFx = 1.0;
            emitCarParticles(ps, c, 100, 5, 200, 1, 0, 0.1);
        }
        expect(!ps.particles.empty(), "slipFx>0 spawns at least some tire-smoke particles over a few calls");
        for (const Particle& p : ps.particles) {
            // Tire smoke spawns behind the car (-fwd) with a fixed +0.2
            // height offset and +-0.5 lateral jitter on x/z (index.html:
            // 3328-3329): bx - fwdX*1.7 +- 0.5, with fwdX=1,fwdZ=0 here.
            expect(p.x > 100 - 1.7 - 0.6 && p.x < 100 - 1.7 + 0.6, "tire smoke x lands behind the car +- jitter");
            expectNear("tire smoke y offset", p.y, 5.2);
        }
        expectNear("slipFx decays by dt*3.5", c.slipFx, std::max(0.0, 1.0 - 0.1 * 3.5));
    }
    {
        ParticleSystem ps;
        Car c{};
        c.hitFx = 1.0;
        emitCarParticles(ps, c, 0, 0, 0, 1, 0, 0.1);
        expect(ps.particles.size() == 4, "hitFx>0 spawns exactly 4 impact-burst particles per call");
        expectNear("hitFx decays by dt*2.5", c.hitFx, std::max(0.0, 1.0 - 0.1 * 2.5));
    }
    {
        ParticleSystem ps;
        Car c{};
        c.dmg = 0.9;
        c.out = false;
        c.v = 20;
        // rng.next()<0.4 gate is stochastic -- run enough calls that at
        // least one spawns, without asserting a false-positive-prone exact
        // count.
        bool sawOne = false;
        for (int i = 0; i < 50 && !sawOne; ++i) {
            emitCarParticles(ps, c, 0, 0, 0, 1, 0, 0.02);
            if (!ps.particles.empty()) sawOne = true;
        }
        expect(sawOne, "wounded-engine smoke eventually spawns while badly damaged, moving, and not out");
    }
    {
        ParticleSystem ps;
        Car c{};
        c.dmg = 0.9;
        c.out = true; // gated off while out (index.html:3343's `!c.out`)
        c.v = 20;
        for (int i = 0; i < 50; ++i) emitCarParticles(ps, c, 0, 0, 0, 1, 0, 0.02);
        expect(ps.particles.empty(), "wounded-engine smoke never spawns once the car is out");
    }

    // ---- tickParticles(): end-to-end against a real Track ----
    {
        const Track track(TRACKS[0]);
        ParticleSystem ps;
        std::vector<Car> cars(1);
        cars[0].slipFx = 1.0;
        cars[0].s = 10.0;
        cars[0].lat = 0.0;
        cars[0].hdg = 0.0;
        tickParticles(ps, cars, track, 0.05);
        expect(!ps.particles.empty(), "tickParticles spawns from a real Track-derived position");
        expectNear("slipFx decayed by tickParticles too", cars[0].slipFx, std::max(0.0, 1.0 - 0.05 * 3.5));
    }

    // ---- L6: off-track dust ----
    //
    // The cue that was missing entirely: a car on the grass looked exactly
    // like a car on the asphalt. Pinned as a behaviour rather than a particle
    // count, since the count is random -- on the track, nothing; off it at
    // speed, something; and the dust must be brown, or it is just tire smoke
    // in the wrong place.
    {
        ParticleSystem ps;
        Car c;
        c.v = 40.0;
        // Clean car: no slip, no impact, no damage, so ONLY the dust rule can
        // emit anything here.
        emitCarParticles(ps, c, 0, 0, 0, 1, 0, 0.05, /*offTrack=*/false);
        expect(ps.particles.empty(), "a clean car on the racing surface emits nothing");

        for (int i = 0; i < 40 && ps.particles.empty(); ++i)
            emitCarParticles(ps, c, 0, 0, 0, 1, 0, 0.05, /*offTrack=*/true);
        expect(!ps.particles.empty(), "a car off the racing surface throws up dust");
        if (!ps.particles.empty()) {
            const Particle& d = ps.particles.front();
            expect(d.col[0] > d.col[2] + 0.15, "off-track dust is brown, not smoke-grey");
        }

        // Crawling off-track does not: dust comes from wheels doing work.
        ParticleSystem slow;
        Car s2;
        s2.v = 1.0;
        for (int i = 0; i < 40; ++i) emitCarParticles(slow, s2, 0, 0, 0, 1, 0, 0.05, /*offTrack=*/true);
        expect(slow.particles.empty(), "a car crawling off-track raises no dust");
    }

    // ---- L6: skid marks ----
    {
        const Track track(TRACKS[0]);
        auto slidingCar = [&]() {
            Car c;
            c.idx = 0;
            c.v = 40.0;
            c.s = 100.0;
            // Sliding is a BODY SLIP ANGLE, not slipFx -- slipFx is pinned at
            // 1.00 through normal racing (median, p90 and p99 all 1.00 over
            // 100 s on every track), so gating on it laid rubber everywhere.
            // vy = 12 at v = 40 is atan(12/40) = 16.7 degrees, a real slide.
            c.vy = 12.0;
            return c;
        };

        // A car that is not sliding lays nothing, however fast it goes.
        {
            ParticleSystem ps;
            Car c = slidingCar();
            c.vy = 0.0; // pointed exactly where it is going: not sliding
            for (int i = 0; i < 200; ++i) {
                laySkidMark(ps, track, c, 0.016);
                c.s += c.v * 0.016;
            }
            expect(ps.skids.empty(), "a car that is not sliding lays no rubber");
        }

        // A car sliding at speed does.
        {
            ParticleSystem ps;
            Car c = slidingCar();
            for (int i = 0; i < 200; ++i) {
                laySkidMark(ps, track, c, 0.016);
                c.s += c.v * 0.016;
            }
            expect(!ps.skids.empty(), "a car sliding at speed lays rubber");
        }

        // Crawling does not: rubber comes from a tire scrubbing, and marking
        // the pit lane and the grid would be wrong.
        {
            ParticleSystem ps;
            Car c = slidingCar();
            c.v = 3.0;
            for (int i = 0; i < 200; ++i) {
                laySkidMark(ps, track, c, 0.016);
                c.s += c.v * 0.016;
            }
            expect(ps.skids.empty(), "a car sliding at walking pace lays no rubber");
        }

        // THE PROPERTY THAT IS EASY TO GET WRONG: spacing is per METRE, not
        // per frame. A per-frame spawn lays marks twice as densely at 120 fps
        // as at 60 -- which looks fine on whatever machine it was written on
        // and wrong everywhere else. Same distance covered, same rubber.
        {
            ParticleSystem slow, fast;
            Car a = slidingCar(), b = slidingCar();
            for (int i = 0; i < 100; ++i) { // 100 steps of 1/60 s
                laySkidMark(slow, track, a, 1.0 / 60.0);
                a.s += a.v / 60.0;
            }
            for (int i = 0; i < 200; ++i) { // 200 steps of 1/120 s: same 100/60 s
                laySkidMark(fast, track, b, 1.0 / 120.0);
                b.s += b.v / 120.0;
            }
            const int n60 = (int)slow.skids.size(), n120 = (int)fast.skids.size();
            std::printf("skid marks over the same distance: %d at 60fps, %d at 120fps\n", n60, n120);
            expect(n60 > 0 && std::abs(n60 - n120) <= 1,
                   "skid-mark spacing is per metre, not per frame");
        }

        // Marks expire, and the list is capped so a long race cannot grow it
        // without bound.
        {
            ParticleSystem ps;
            Car c = slidingCar();
            for (int i = 0; i < 200; ++i) {
                laySkidMark(ps, track, c, 0.016);
                c.s += c.v * 0.016;
            }
            const size_t before = ps.skids.size();
            expect(before > 0, "expiry check needs marks to expire");
            ageSkidMarks(ps, 60.0); // past the longest life
            expect(ps.skids.empty(), "skid marks expire rather than accumulating forever");
            expect((int)before <= kSkidMarkCap, "skid-mark list stays within its cap");
        }

        // Corners follow the track surface: a mark on a banked corner must lie
        // in the banked plane, not flat. Asserted as the two lateral edges
        // sitting at different heights wherever the track is banked -- a flat
        // quad would have them equal, and would sink into the surface at one
        // side.
        {
            ParticleSystem ps;
            Car c = slidingCar();
            // Find a banked station on this track rather than assuming one.
            double bankedS = -1.0;
            for (double s = 0; s < track.total(); s += 5.0)
                if (std::fabs(track.bankAt(s)) > 0.05) { bankedS = s; break; }
            expect(bankedS >= 0.0, "track 0 has a banked section to test against");
            if (bankedS >= 0.0) {
                c.s = bankedS;
                for (int i = 0; i < 60 && ps.skids.empty(); ++i) {
                    laySkidMark(ps, track, c, 0.016);
                    c.s += 0.05; // creep, so the mark stays in the banked section
                }
                expect(!ps.skids.empty(), "a mark was laid in the banked section");
                if (!ps.skids.empty()) {
                    const SkidMark& m = ps.skids.front();
                    // Corners 0 and 3 are the two lateral extremes at s0.
                    const float dy = std::fabs(m.c[0][1] - m.c[3][1]);
                    expect(dy > 0.01f, "a skid mark on banking lies in the banked plane");
                }
            }
        }
    }

    std::printf(g_failures == 0 ? "particles_test: PASS\n" : "particles_test: FAILURES ABOVE\n");
    return g_failures == 0 ? 0 : 1;
}
