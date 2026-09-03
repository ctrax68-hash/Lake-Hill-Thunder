#include "race.h"

#include "constants.h"
#include "step_car.h"

#include <algorithm>
#include <cmath>

namespace {
double wrapMod(double s, double total) {
    return std::fmod(std::fmod(s, total) + total, total);
}

double sign(double x) { return x > 0 ? 1.0 : (x < 0 ? -1.0 : 0.0); }

// spotterSay() (index.html:1385-1388). JS sets the HUD caption fields AND
// fires an audio blip synchronously in one call; this port only sets the
// fields (S.spotTxt/S.spotT are the only physics-adjacent state a
// bgfx/SDL2-free race.cpp should touch). Phase 6c's audio engine is
// expected to edge-detect a fresh message the same way audioTick() already
// edge-detects c.hitFx/c.blown -- see race_state.h's own comment on
// spotTxt/spotT.
void spotterSay(RaceState& state, const std::string& txt) {
    state.spotTxt = txt;
    state.spotT = 2.2;
}
} // namespace

// gridStart() (index.html:564-598)
void gridStart(const Track& track, Mulberry32& rng, RaceState& state, PaceCar& pace,
                std::vector<Car>& cars, std::vector<Car*>& finishOrder,
                const std::vector<int>* gridOrder) {
    cars.clear();
    finishOrder.clear();

    std::vector<int> order;
    if (gridOrder && static_cast<int>(gridOrder->size()) == FIELD) {
        order = *gridOrder;
    } else {
        for (int i = 1; i < FIELD; ++i) order.push_back(i);
        order.push_back(0);
    }
    for (int idx : order) cars.push_back(makeCar(idx == 0, idx, track, rng));

    const double sLead = track.segs()[2].s0 + 40;
    for (size_t i = 0; i < cars.size(); ++i) {
        Car& c = cars[i];
        const int row = static_cast<int>(i) / 2;
        const bool colL = (i % 2 == 0);
        const double s = sLead - row * 11;
        PointResult p = track.pointAt(s);
        const double off = colL ? -2.6 : 2.6;
        c.x = p.x - std::sin(p.hdg) * off;
        c.y = p.y + std::cos(p.hdg) * off;
        c.hdg = c.vdir = p.hdg;
        c.s = wrapMod(s, track.total());
        c.lat = off;
        c.v = 31;
        c.gridLane = off;
        c.gridAhead = i >= 2 ? static_cast<int>(i) - 2 : -1;
        c.gridSlot = static_cast<int>(i);
        c.prog = -1 + (static_cast<int>(i) * 0.001);
        c.px = c.x;
        c.py = c.y;
        c.phdg = c.hdg;
        c.ps = c.s;
        c.plat = c.lat;
    }

    state.flag = "green";
    state.cautionUntilLap = -1;
    state.t = 0;
    state.paceV = 31;
    state.greenT = 0;

    const double ps = sLead + 18;
    PointResult pp = track.pointAt(ps);
    pace.s = wrapMod(ps, track.total());
    pace.lat = 0;
    pace.v = 31;
    pace.hdg = pp.hdg;
    pace.x = pp.x;
    pace.y = pp.y;
    pace.state = "lead";
    // G20: seed the pace car's previous pose to its spawn pose, exactly as
    // the loop above does for each Car -- otherwise the first rendered frame
    // interpolates from a zeroed pose at the world origin.
    pace.px = pace.x;
    pace.py = pace.y;
    pace.phdg = pace.hdg;
    pace.ps = pace.s;
    pace.plat = pace.lat;
}

// stepPace() (index.html:599-626)
void stepPace(PaceCar& pace, const RaceState& state, const Track& track) {
    if (pace.state == "lead") {
        pace.s += pace.v * DT;
        if (pace.s >= track.total()) pace.s -= track.total();
        if (state.mode == "pace" && pace.s > 2 && pace.s < track.sFinish() - 60) pace.state = "peel";
    } else if (pace.state == "peel") {
        pace.s += pace.v * DT;
        if (pace.s >= track.total()) pace.s -= track.total();
        pace.lat = std::max(-11.0, pace.lat - 5.5 * DT);
        pace.v = std::max(0.0, pace.v - 6 * DT);
        if (pace.v <= 0.1 || pace.lat <= -10.5) pace.state = "parked";
    }
    PointResult p = track.pointAt(pace.s);
    const double nx = -std::sin(p.hdg), ny = std::cos(p.hdg);
    pace.x = p.x + nx * pace.lat;
    pace.y = p.y + ny * pace.lat;
    pace.hdg = p.hdg;
}

// updateAero() (index.html:652-676)
void updateAero(std::vector<Car>& cars, const Track& track) {
    for (auto& c : cars) {
        c.draftF = 0;
        c.dirty = false;
    }
    for (auto& c : cars) {
        for (auto& o : cars) {
            if (&o == &c) continue;
            double ds = o.s - c.s;
            if (ds < -track.total() / 2) ds += track.total();
            if (ds > track.total() / 2) ds -= track.total();
            if (ds > 1 && ds < 34 && std::abs(o.lat - c.lat) < 6.0) {
                double f = (ds < 9) ? 1.0 : std::max(0.0, 1 - (ds - 9) / 25);
                c.draftF = std::max(c.draftF, f);
                PointResult p = track.pointAt(c.s);
                if (std::abs(p.curv) > 1e-6 && ds < 16) c.dirty = true;
            }
        }
    }
}

// collide() (index.html:1158-1231)
void collide(std::vector<Car>& cars, RaceState& state, const Track& track, Mulberry32& rngR) {
    const double trackWreckScale = std::min(1.0, 1600.0 / track.total());
    for (size_t i = 0; i < cars.size(); ++i) {
        for (size_t j = i + 1; j < cars.size(); ++j) {
            Car& a = cars[i];
            Car& b = cars[j];
            const double dx = b.x - a.x, dy = b.y - a.y, d = std::hypot(dx, dy), minD = 3.6;
            if (d > 0.01 && d < minD) {
                const double nx = dx / d, ny = dy / d, push = (minD - d) / 2;
                a.x -= nx * push;
                a.y -= ny * push;
                b.x += nx * push;
                b.y += ny * push;
                // N6: dissipate only the CLOSING component of the impact.
                //
                // The original applied its speed penalty on every tick two
                // cars overlapped, scaled by the difference in their speeds.
                // That made sustained contact a permanent velocity sink, and
                // the trap is that it bit hardest on exactly the car trying to
                // escape: gaining speed relative to your neighbour is what
                // makes the penalty large. At a 1 m/s difference the loss was
                // ~0.25 m/s per 0.02 s tick, about 12 m/s^2, against roughly
                // 5 m/s^2 of available drive. Escaping a pile-up was
                // arithmetically impossible, so the whole field stayed welded
                // together for the rest of the race -- measured on Milltown as
                // 20 cars in a 35 m stretch of an 848 m lap, most shoved off
                // the racing surface, all at ~0 m/s.
                //
                // The mistake was using the scalar speed DIFFERENCE as a proxy
                // for impact severity. Two cars side by side at different
                // speeds are not approaching each other; two cars nose-to-tail
                // at the same speed are not either. What a collision actually
                // removes is the approach velocity along the contact normal --
                // so that is what is computed and removed here.
                //
                // This keeps real impacts costly (a genuine rear-ending still
                // sheds speed) while making a car that is separating pay
                // nothing, which is what breaks the deadlock. A blunter fix --
                // simply rate-limiting the old penalty with a cooldown -- was
                // tried first and measured: it cured the jam but removed the
                // energy dissipation entirely, so cars ground against each
                // other at speed and wrecked instead, taking Cedar Valley from
                // 16 cars running to 4.
                //
                // The positional push above is deliberately NOT gated on any
                // of this -- cars must always be separated, or they
                // interpenetrate.
                const double avx = a.v * std::cos(a.hdg), avy = a.v * std::sin(a.hdg);
                const double bvx = b.v * std::cos(b.hdg), bvy = b.v * std::sin(b.hdg);
                // n points from a toward b, so a positive projection means the
                // pair is closing.
                const double closingRate = (avx - bvx) * nx + (avy - bvy) * ny;
                // BOTH conditions, because each fixes a different half and
                // neither is sufficient alone -- measured, not assumed:
                //   closing-only, no cooldown: attrition healthy (17-20 cars
                //     running) but the jam partly returns, because a wedged
                //     pack oscillates and re-closes every few ticks.
                //   cooldown-only, old scalar penalty: jam cured but cars
                //     ground against each other at speed and wrecked, taking
                //     Cedar Valley from 16 running to 4.
                // Applied every tick, NOT rate-limited. A cooldown was tried
                // (0.25 s, mirroring dmgCd) and measured worse overall: it
                // cures the jam but stops dissipating contact energy, so cars
                // grind against each other at speed and wreck instead --
                // Cedar Valley fell from 16 cars running to 4. Dissipating the
                // closing component continuously is both the physical answer
                // and the one that keeps the field alive; the jam's remaining
                // half is an AI problem, fixed in step_car.cpp rather than by
                // blunting the collision response until it goes away.
                if (closingRate > 0.0) {
                    const double take = std::min(closingRate, 6.0);
                    // L10: the split is by ROLE, not by array position.
                    //
                    // It was a fixed 0.45 for `a` and 0.20 for `b` -- and a/b
                    // are just the lower/higher index in the pairwise loop, so
                    // which car shed more speed depended on where it sat in a
                    // vector. Measured: the player is always at index 19 of 20,
                    // therefore always `b`, therefore always on the gentler
                    // side. The player was being favoured by accident.
                    //
                    // Worth keeping -- being punted by the AI is the most
                    // demoralising thing that can happen to a player, and the
                    // report behind L10 was exactly that -- but as a deliberate
                    // assist, not a side effect of iteration order. Two AI cars
                    // split evenly (equal masses, equal share); the player keeps
                    // the gentler share explicitly, whichever slot they occupy.
                    // Who loses more is decided by WHO IS DOING THE HITTING,
                    // not by array position and not evenly.
                    //
                    // An even 0.325/0.325 split was tried first -- it is what
                    // "equal masses" suggests -- and measured worse for the
                    // field (moving 0.774 -> 0.690, cars running 159 -> 154).
                    // The reason is the interesting part: if both cars in a
                    // contact shed the same speed, neither ever gets clear of
                    // the other, so pairs stay locked together. An asymmetric
                    // loss lets one car pull out, which is what actually
                    // dissolves a tangle.
                    //
                    // Asymmetric BY ROLE is also the physical reading: the car
                    // driving into the other is the one that loses the speed.
                    // `hitting` is that car -- the one whose own motion is
                    // carrying it along the contact normal.
                    constexpr double kHitter = 0.45, kVictim = 0.20;
                    const double aAlong = avx * nx + avy * ny;   // a moving toward b
                    const double bAlong = -(bvx * nx + bvy * ny); // b moving toward a
                    const bool aHitting = aAlong >= bAlong;
                    double aShare = aHitting ? kHitter : kVictim;
                    double bShare = aHitting ? kVictim : kHitter;
                    // The player always gets the gentler share. Being punted by
                    // the AI is the most demoralising thing that can happen to
                    // a player, and the report behind L10 was exactly that.
                    // Previously the player got this by accident -- they sit at
                    // vector index 19 of 20, so they were always the `b` of the
                    // old fixed 0.45/0.20 split. Made deliberate.
                    if (a.isPlayer) aShare = kVictim;
                    if (b.isPlayer) bShare = kVictim;
                    a.v = std::max(0.0, a.v - take * aShare);
                    b.v = std::max(0.0, b.v - take * bShare);

                    // The heading kick belongs INSIDE the closing test, and
                    // scaled by how hard the hit was.
                    //
                    // It used to sit outside, applying a fixed rotation on
                    // every tick two cars overlapped. That is the same
                    // structural bug the velocity penalty had -- an impact
                    // response used as a continuous drain -- and it is a major
                    // jam cause in its own right: cars in sustained contact get
                    // rotated a little further off the racing line every tick
                    // until they are pointing at the wall, which is exactly the
                    // "shoved off the surface to lat -11 and stuck" state the
                    // N6 diagnosis found.
                    //
                    // Measured, restoring the ungated version costs the field
                    // badly: moving fraction 0.83 -> 0.61, Milltown 0.80 ->
                    // 0.29. Gated and scaled, a real hit still turns you and a
                    // gentle lean does almost nothing.
                    const double kickMag = std::min(1.0, take / 2.0);
                    a.hdg += ny * 0.004 * kickMag * (a.isPlayer ? 1.0 : 2.0);
                    b.hdg -= ny * 0.004 * kickMag * (b.isPlayer ? 1.0 : 2.0);
                }

                const double cv2 = std::abs(a.v - b.v);
                if (cv2 > 3 && state.flag != "yellow") {
                    Car& hitter = a.v > b.v ? a : b;
                    Car& victim2 = a.v > b.v ? b : a;
                    if (hitter.dmgCd <= 0) {
                        const double hitterDelta = std::min(0.08, cv2 * 0.003);
                        const double victimDelta = std::min(0.05, cv2 * 0.002);
                        hitter.dmg = std::min(1.0, hitter.dmg + hitterDelta);
                        victim2.dmg = std::min(1.0, victim2.dmg + victimDelta);
                        hitter.dmgCd = 0.6;
                        victim2.dmgCd = 0.6;

                        // P3 (NT2003 engine-feel plan, damage that pulls):
                        // each car's OWN local (heading-relative) lateral
                        // component of the push direction it just received
                        // -- a's push is (-nx,-ny), b's is (nx,ny) (see
                        // above) -- so the persistent bias is anchored to the
                        // car's own chassis, not world axes, and survives a
                        // heading change the same way a real bent fender
                        // would (car.h's own comment on dmgPull).
                        const double pullA = std::sin(a.hdg) * nx - std::cos(a.hdg) * ny;
                        const double pullB = -std::sin(b.hdg) * nx + std::cos(b.hdg) * ny;
                        const double aDelta = (&hitter == &a) ? hitterDelta : victimDelta;
                        const double bDelta = (&hitter == &b) ? hitterDelta : victimDelta;
                        a.dmgPull = std::max(-1.0, std::min(1.0, a.dmgPull + sign(pullA) * aDelta));
                        b.dmgPull = std::max(-1.0, std::min(1.0, b.dmgPull + sign(pullB) * bDelta));
                        if (cv2 > 18 && state.mode == "race" && state.flag == "green" &&
                            state.greenLockT <= 0 && victim2.spinT <= 0 && victim2.spinCd <= 0 &&
                            victim2.spinRollCd <= 0) {
                            victim2.spinRollCd = 2.6;
                            if (rngR.next() < std::min(0.06, (cv2 - 18) * 0.0025 + 0.0025) * trackWreckScale) {
                                victim2.spinT = 1.6 + rngR.next() * 1.2;
                                victim2.spinDir = ny > 0 ? 1 : -1;
                                victim2.spinCd = 10;
                                ++state.wreckCount; // debug/regression-measurement only, see race_state.h
                            }
                        }
                        hitter.hitFx = std::min(1.0, hitter.hitFx + cv2 * 0.04); // index.html:1227
                    }
                }
            }
        }
    }
}

// activeLead() (index.html:1138-1141)
Car* activeLead(const std::vector<Car*>& order) {
    for (auto* o : order) {
        if (!o->done && !o->out) return o;
    }
    return order[0];
}

namespace {
double wrapHalf(double d, double total) {
    while (d < -total / 2) d += total;
    while (d > total / 2) d -= total;
    return d;
}
} // namespace

// cautionController() (index.html:4251-4461)
void cautionController(RaceState& state, std::vector<Car>& cars, PaceCar& pace, const Track& track,
                        const std::vector<Car*>& order) {
    if (state.mode != "race") return;

    if (state.flag == "green") {
        // index.html:4254-4263: finish the pace car's dive to the apron.
        if (pace.state == "peel") stepPace(pace, state, track);

        for (auto& c : cars) {
            if (c.spinT > 0 && !c.done) {
                state.flag = "yellow";
                state.yellowT = 0;
                Car* lead = activeLead(order);
                state.cautionUntilLap = lead->lap + 2;
                pace.state = "lead";
                pace.lat = 0;
                pace.v = 38;
                pace.s = std::fmod(lead->s + 90, track.total());

                std::vector<Car*> act;
                for (auto& o : cars) {
                    if (!o.out && !o.done) act.push_back(&o);
                }
                std::stable_sort(act.begin(), act.end(), [&](Car* a, Car* b) {
                    double da = wrapMod(pace.s - a->s, track.total());
                    double db = wrapMod(pace.s - b->s, track.total());
                    return da < db;
                });
                for (size_t i = 0; i < act.size(); ++i) act[i]->cautionSlot = static_cast<int>(i);
                state.cautionMaxSlot = static_cast<int>(act.size()) - 1;
                state.oneToGo = false;
                state.pitsOpen = false;
                break;
            }
        }
    } else if (state.flag == "yellow") {
        state.yellowT += DT;
        stepPace(pace, state, track);

        // index.html:4296-4314: adaptive pace speed.
        {
            Car* front = nullptr;
            double best = 1e9;
            for (auto& o : cars) {
                if (o.out || o.done || o.pit != 0 || o.spinT > 0) continue;
                if (o.cautionSlot >= 0 && o.cautionSlot < best) {
                    best = o.cautionSlot;
                    front = &o;
                }
            }
            if (front) {
                double gap = wrapHalf(pace.s - 16 - front->cautionSlot * 9 - front->s, track.total());
                pace.v = 38 - std::max(0.0, std::min(16.0, (gap - 24) * 0.15));
            } else {
                pace.v = 38;
            }
        }

        Car* lead = activeLead(order);

        // index.html:4341-4343: slot compaction.
        std::vector<Car*> activeY;
        for (auto* o : order) {
            if (!o->out && !o->done && o->pit == 0) activeY.push_back(o);
        }
        {
            std::vector<Car*> sorted = activeY;
            std::stable_sort(sorted.begin(), sorted.end(),
                      [](Car* a, Car* b) { return a->cautionSlot < b->cautionSlot; });
            for (size_t i = 0; i < sorted.size(); ++i) sorted[i]->cautionSlot = static_cast<int>(i);
        }
        state.cautionMaxSlot = static_cast<int>(activeY.size()) - 1;

        // index.html:4355-4392: time-compressed straggler warp.
        if (state.yellowT > 40 && !state.oneToGo) {
            for (auto* o : activeY) {
                if (o->isPlayer || o->spinT > 0) continue;
                double se = wrapHalf(pace.s - 16 - o->cautionSlot * 9 - o->s, track.total());
                if (se > 30) {
                    double sNew = wrapMod(o->s + se, track.total());
                    bool clear = true;
                    for (auto* other : activeY) {
                        if (other == o) continue;
                        double dso = std::abs(other->s - sNew);
                        if (dso > track.total() / 2) dso = track.total() - dso;
                        if (dso < 12) {
                            clear = false;
                            break;
                        }
                    }
                    if (clear) {
                        PointResult p = track.pointAt(sNew);
                        o->s = sNew;
                        o->lat = 0;
                        o->x = p.x;
                        o->y = p.y;
                        o->hdg = o->vdir = p.hdg;
                        o->v = 38;
                    }
                }
            }
        }

        // index.html:4393-4404: bunched check.
        int stragglers = 0;
        for (auto* o : activeY) {
            double se = wrapHalf(pace.s - 16 - o->cautionSlot * 9 - o->s, track.total());
            if (std::abs(se) > 25) ++stragglers;
        }
        const bool bunched = stragglers <= 3;

        // index.html:4405-4411: pit road opens once collected.
        if (!state.pitsOpen && !state.oneToGo && bunched) {
            state.pitsOpen = true;
        }

        if (!state.oneToGo) {
            // index.html:4412-4444: one-to-go transition.
            if ((lead->lap >= state.cautionUntilLap || state.yellowT > 120) && state.yellowT > 8 &&
                (bunched || state.yellowT > 75)) {
                state.oneToGo = true;
                state.pitsOpen = false;
            }
        } else {
            // index.html:4445-4460: pace car pulls in, green as leader nears the line.
            if (pace.state == "lead") {
                double dEntry = wrapHalf(track.segs()[0].s0 - 25 - pace.s, track.total());
                if (std::abs(dEntry) < 4) pace.state = "peel";
            }
            double dl = std::fmod(track.sFinish() - lead->s + track.total(), track.total());
            if (pace.state != "lead" && (dl < 60 || dl > track.total() - 10)) {
                state.flag = "green";
                state.greenT = 2.2;
                state.greenLockT = GREEN_LOCK_DUR;
                state.sinceGreenT = 0;
                state.oneToGo = false;
                state.pitsOpen = true;
                pace.state = "peel";
            }
        }
    }
}

// tick() (index.html:4180-4595) -- see race.h's comment for exactly what's
// deliberately not ported yet.
void tick(RaceState& state, std::vector<Car>& cars, PaceCar& pace, const Track& track,
          Mulberry32& rngR, const PlayerInput& input, std::vector<Car*>& finishOrder) {
    state.t += DT;

    // G15 (NASCAR-Thunder gap-analysis plan): store every pre-tick pose
    // before anything below mutates it, matching JS's own store-before-step
    // order (index.html:4634-4635) -- renderer.cpp's interpolatedPose()
    // blends these against the post-tick poses by the leftover accumulator
    // fraction, so the 50Hz physics rate doesn't visibly snap/stutter
    // against the uncorrelated ~60Hz display refresh rate.
    //
    // G20 extended this to the pace car, now that it is actually rendered.
    // Note the ordering requirement, which is easy to get wrong: `stepPace()`
    // below runs *before* the `stepCar()` loop, so a pace-car store placed
    // next to the old car-only store (which sat after the pace block) would
    // capture the already-stepped pose -- previous == current, interpolation
    // silently becoming a no-op and the pace car stuttering exactly as every
    // car did before G15. Both stores now sit at the very top, so the
    // invariant is uniform: capture all poses first, then step everything.
    for (auto& c : cars) {
        c.px = c.x;
        c.py = c.y;
        c.phdg = c.hdg;
        c.ps = c.s;
        c.plat = c.lat;
    }
    pace.px = pace.x;
    pace.py = pace.y;
    pace.phdg = pace.hdg;
    pace.ps = pace.s;
    pace.plat = pace.lat;

    if (state.mode == "pace") {
        stepPace(pace, state, track);
        if (pace.state != "lead") state.paceV = std::min(46.0, state.paceV + 3.4 * DT);
    }

    updateAero(cars, track);
    for (auto& c : cars) stepCar(c, state, track, cars, pace, input);
    collide(cars, state, track, rngR);

    // S.order (index.html:4192): race-position order, descending.
    std::vector<Car*> order;
    order.reserve(cars.size());
    for (auto& c : cars) order.push_back(&c);
    std::stable_sort(order.begin(), order.end(), [](Car* a, Car* b) {
        double pa = a->done ? 1e6 - a->finishT : a->prog;
        double pb = b->done ? 1e6 - b->finishT : b->prog;
        return pb < pa; // descending
    });

    if (state.mode == "pace") {
        for (auto& c : cars) {
            if (c.lap >= 0) {
                state.mode = "race";
                state.greenT = 2.2;
                state.greenLockT = GREEN_LOCK_DUR;
                state.sinceGreenT = 0;
                break;
            }
        }
    }
    if (state.greenLockT > 0 && state.flag == "green") state.greenLockT -= DT;
    if (state.flag == "green") state.sinceGreenT += DT;

    // index.html:4205-4208: qualifying flying-lap complete. Only the
    // mode='qual'->'menuwait' physics transition is ported here -- the
    // setTimeout(()=>finishQualifying(...), 400) that follows is menu-flow
    // (builds a synthesized grid and calls startRace()), not physics.
    Car* player = nullptr;
    for (auto& c : cars) {
        if (c.isPlayer) {
            player = &c;
            break;
        }
    }
    if (state.mode == "qual" && player && player->lap >= 1) {
        state.mode = "menuwait";
    }

    // index.html:4209-4218: AI pit strategy.
    if (state.mode == "race") {
        for (auto& c : cars) {
            if (c.isPlayer || c.done || c.out || c.pit > 0 || c.spinT > 0) continue;
            // N6 residual: the damage threshold is PER CAR, not a shared 0.45.
            //
            // Measured cause of the late-race collapse. Damage accumulates
            // almost uniformly across a field that is rubbing on every lap, so
            // with one shared threshold the entire field crosses it within a
            // few seconds of each other: on Milltown, cars pitting went 1 -> 9
            // -> 12 -> 16 between t=120 and t=180 while the moving count fell
            // 20 -> 11 -> 4 -> 2. Pit lane cannot absorb sixteen cars at once,
            // so the race ends in a pit-lane queue rather than on track.
            //
            // Spread deterministically by car index (0.40..0.70) rather than
            // randomly: real teams have different damage tolerances, it keeps
            // the sim reproducible for the harnesses, and it staggers entry
            // across many laps instead of one.
            const double dmgPitAt = 0.40 + 0.30 * ((double)c.idx / (double)std::max(1, FIELD - 1));
            c.pitReq = (state.flag == "yellow" && state.pitsOpen && (c.wear > 0.25 || c.fuel < 0.5)) ||
                       c.wear > 0.7 || c.fuel < 0.18 || (c.dmg > dmgPitAt && c.dmg < 1);
        }
    }

    // index.html:4219-4250: blowouts and terminal-damage DNFs.
    if (state.mode == "race" || state.mode == "victory") {
        for (auto& c : cars) {
            if (c.done || c.out || c.pit > 0) continue;
            if (c.wear > 0.92 && !c.blown && c.v > 25 && rngR.next() < 0.0004) {
                c.blown = true;
                c.hitFx = 1;
                if (c.isPlayer) spotterSay(state, "FLAT TIRE - PIT NOW!");
                if (state.flag == "green" && state.greenLockT <= 0) {
                    c.spinT = 1.8 + rngR.next() * 1.2;
                    c.spinDir = rngR.next() < 0.5 ? -1 : 1;
                    c.spinCd = 10;
                    ++state.wreckCount; // debug/regression-measurement only, see race_state.h
                }
            }
            if (c.dmg >= 1 && !c.out) {
                c.out = true;
                if (c.isPlayer) spotterSay(state, "TOO MUCH DAMAGE - WE'RE DONE");
            }
        }
    }

    cautionController(state, cars, pace, track, order);

    // index.html:4463-4545: green-white-checkered state machine, then the
    // unconditional finish-line arbitration.
    if (state.mode == "race") {
        Car* gwcLead = activeLead(order);
        if (gwcLead && !gwcLead->done && !gwcLead->out) {
            if (state.gwcState == "none") {
                if (state.flag == "yellow" && gwcLead->lap >= state.finishLaps) {
                    if (state.gwcAttempts < GWC_MAX_ATTEMPTS) {
                        state.finishLaps += 1;
                        state.gwcState = "watch";
                        state.gwcAttempts++;
                    } else {
                        state.finishLaps = gwcLead->lap;
                    }
                }
            } else if (state.gwcState == "watch") {
                if (state.flag == "yellow" && gwcLead->lap >= state.finishLaps) {
                    state.finishLaps = gwcLead->lap + 1;
                }
                if (state.flag == "green") {
                    state.gwcMarkLap = gwcLead->lap;
                    state.finishLaps = gwcLead->lap + 3;
                    state.gwcState = "clean1";
                }
            } else if (state.gwcState == "clean1") {
                if (state.flag == "yellow") {
                    state.gwcState = "none";
                } else if (gwcLead->lap >= state.gwcMarkLap + 2) {
                    state.finishLaps = gwcLead->lap + 1;
                    state.gwcState = "white";
                }
            } else if (state.gwcState == "white") {
                if (state.flag == "yellow") state.gwcState = "none";
            }
        }

        for (auto& c : cars) {
            if (!c.done && !c.out && c.lap >= state.finishLaps) {
                c.done = true;
                c.finishT = state.t;
                finishOrder.push_back(&c);
            }
        }
    }

    // index.html:4547: greenT is HUD/audio-only (crowd noise, banner flash)
    // but the field is real and already exists, so keep it in sync.
    if (state.greenT > 0) state.greenT -= DT;

    // index.html:4549-4567: spotter -- alongside calls + laps-to-go/fuel/
    // tire/damage one-shot messages. Player-relative (S.player == the sole
    // isPlayer car); JS's S.msgT/S.msgTxt (a separate, unrelated broadcast-
    // banner system) are NOT ported here -- see race.h's own "not ported"
    // list.
    if (state.spotT > 0) state.spotT -= DT;
    if (state.mode == "race" && player && !player->done && player->pit == 0) {
        bool inside = false, outside = false;
        for (auto& o : cars) {
            if (&o == player || o.done || o.pit > 0) continue;
            double ds = o.s - player->s;
            if (ds < -track.total() / 2) ds += track.total();
            if (ds > track.total() / 2) ds -= track.total();
            if (std::abs(ds) < 5.5 && std::abs(o.lat - player->lat) < 3.4) {
                if (o.lat < player->lat) inside = true;
                else outside = true;
            }
        }
        if (inside && state.spotState != "in") {
            spotterSay(state, outside ? "THREE WIDE!" : "INSIDE!");
            state.spotState = "in";
        } else if (outside && !inside && state.spotState != "out") {
            spotterSay(state, "OUTSIDE!");
            state.spotState = "out";
        } else if (!inside && !outside && (state.spotState == "in" || state.spotState == "out")) {
            spotterSay(state, "CLEAR");
            state.spotState = "clear";
        }
        if (player->lap == state.laps - 3 && !state.togoMsg && state.laps >= 6) {
            state.togoMsg = true;
            spotterSay(state, "3 TO GO");
        }
        if (player->fuel < 0.15 && !state.fuelMsg) {
            state.fuelMsg = true;
            spotterSay(state, "FUEL LOW - PIT SOON");
        }
        if (player->wear > 0.85 && !state.tireMsg) {
            state.tireMsg = true;
            spotterSay(state, "TIRES ARE GONE");
        }
        if (player->dmg > 0.6 && !state.dmgMsg) {
            state.dmgMsg = true;
            spotterSay(state, "HEAVY DAMAGE - PIT FOR REPAIRS");
        }

        // N1: corner-entry speed warning.
        //
        // Reported as "car pulls way hard to right when going into corners".
        // Measured against the real physics, the steering pipeline is NOT at
        // fault: full lock delivers 0.58 rad/s where the tightest corner needs
        // 0.25, and sweeping steerCurveGamma (2.5 -> 1.0) and
        // steerYawAuthority (0.95 -> cap effectively off) changed line-holding
        // by nothing at all. Given a controller that manages its speed, the
        // player's car tracks the centre line to ~4 m.
        //
        // What is actually happening is plain understeer from carrying too
        // much speed, and on a left oval running wide IS "pulling right".
        // Flat out -- which is what a player does, because nothing tells them
        // otherwise -- the car arrives at corners at 55-60 m/s where the tires
        // allow 40-49, then scrubs from 55 m/s down to 15 fighting for grip.
        // The AI never has this problem because targetSpeed() plans its
        // corner entry for it; the player has been given no equivalent, which
        // is a fair thing to call a steering problem from the driver's seat.
        //
        // This is guidance, not an assist: it does not touch the player's
        // inputs or the car's physics, it tells them what every real spotter
        // would. cornerSpeed() is now clamped to the friction circle (N2), so
        // it is finally a number worth quoting.
        const double lookahead = std::max(20.0, player->v * 1.1);
        const double curvAhead = std::fabs(track.pointAt(player->s + lookahead).curv);
        double overRatio = 0.0;
        if (curvAhead > 1e-6) {
            const double vCorner = cornerSpeed(track, 1.0 / curvAhead, player->s + lookahead, player->wear);
            if (vCorner > 0.1) overRatio = player->v / vCorner;
        }
        // Edge-triggered with HYSTERESIS on the ratio, not on reaching a
        // straight: at a ~50 m lookahead these ovals are almost never clear of
        // curvature, so a rearm condition of "back on a straight" fires once
        // per race and then never again (measured: exactly 1 call on Thunder
        // Oval, 0 everywhere else, identical for a reckless and a careful
        // driver -- i.e. useless). Rearming when the driver has actually
        // slowed back inside the limit makes it track what it is meant to.
        if (overRatio > 1.12 && !state.overSpeedMsg) {
            state.overSpeedMsg = true;
            spotterSay(state, "TOO FAST IN - LIFT!");
        } else if (state.overSpeedMsg && (curvAhead <= 1e-6 || overRatio < 1.02)) {
            state.overSpeedMsg = false;
        }
    }

    // index.html:4581-4594: player finish -> victory/done, player DNF ->
    // done, and the victory-lap timeout back to done. setTimeout(showResults,
    // ...) calls are menu-flow, not ported.
    if (player && player->done && state.mode == "race") {
        if (!finishOrder.empty() && finishOrder[0] == player) {
            state.mode = "victory";
            state.victoryT = 0;
        } else {
            state.mode = "done";
        }
    }
    if (player && player->out && state.mode == "race" && player->v < 1) {
        state.mode = "done";
    }
    if (state.mode == "victory") {
        state.victoryT += DT;
        if (state.victoryT > 6) state.mode = "done";
    }
}
