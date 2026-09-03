#include "step_car.h"

#include "constants.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <stdexcept>

namespace {
constexpr double PI = 3.14159265358979323846;

double sign(double x) { return x > 0 ? 1.0 : (x < 0 ? -1.0 : 0.0); }

double wrapPi(double a) {
    while (a > PI) a -= 2 * PI;
    while (a < -PI) a += 2 * PI;
    return a;
}
} // namespace

void stepCar(Car& c, RaceState& state, const Track& track, const std::vector<Car>& allCars,
             const PaceCar& pace, const PlayerInput& input) {
    double thr = 0, brk = 0, steerIn = 0;
    // Tire-model upgrade: these two branches directly increment c.hdg
    // themselves (victory burnout, wreck spin-out) rather than steering
    // through the normal yaw model -- set by whichever branch below applies,
    // and used by the shared tail to skip the new bicycle-model integration
    // for them (their own forced rotation would otherwise fight the tire
    // forces, which is exactly the failure mode a real tire model would
    // have no reason to resolve sensibly).
    bool freeSpin = false;

    // M1: true only when the human is ACTUALLY driving this tick -- i.e. the
    // `else if (c.isPlayer)` branch below is the one that ran and read
    // PlayerInput. This is NOT the same question as `c.isPlayer`, and
    // conflating the two was a real, shipped bug.
    //
    // The branch chain below puts pace/yellow/pit/spin/DNF ahead of the
    // player-input branch, so the player's own car is auto-driven by AI code
    // for whole phases of a race -- correctly, and by design (during the
    // formation lap every car, the player's included, is placed in line by
    // the game). But three transformations further down were gated on
    // `c.isPlayer` because they exist to make a DIGITAL ON/OFF KEY feel good:
    // the slow input ramp, the steerCurveGamma curve, and the
    // steerYawAuthority lock cap. Applied to an AI branch's continuous
    // yawCorrected() output they are simply wrong -- and they compound: at
    // pace speed they delivered under 10% of the steer angle the pace logic
    // asked for (0.0080 rad against the 0.0910 an AI car gets for the same
    // command), so the player's car understeered off the racing line and
    // ground down the apron through the entire formation lap while the other
    // 19 held station. Reported as "car drives recklessly before race
    // begins"; it was the opposite of reckless, it was steering-starved.
    //
    // Gating on "a human is driving" instead of "this is the human's car"
    // makes every auto-driven phase behave bit-identically to an AI car,
    // while leaving real player driving exactly as L8/L15/L17 tuned it.
    // tests/drivability_test.cpp had already discovered this same
    // linear-vs-curved mismatch from the outside and worked around it by
    // inverting the curve in the test itself; the game code never got the
    // corresponding fix.
    bool humanInput = false;

    // Regression-pass fix (see CarConstants::yawCorrGain in car.h): every
    // non-player steerIn formula below was written treating its raw value as
    // a directly-realized yaw-rate fraction (matching the old kinematic
    // model). Against the bicycle model's real yaw inertia, that raw value
    // is only a feedforward term now -- this adds the feedback half, nudging
    // steerIn by however far `c.r` currently is from the yaw rate that raw
    // value implies (`steerRaw * yawScale`), so the AI corrects when it isn't
    // turning as fast as intended instead of assuming zero-latency response.
    auto yawCorrected = [&](double steerRaw, double yawScale) {
        const double steerClamped = std::max(-1.0, std::min(1.0, steerRaw));
        const double rWant = steerClamped * yawScale;
        return std::max(-1.0, std::min(1.0, steerClamped + CAR.yawCorrGain * (rWant - c.r)));
    };

    if (c.spinCd > 0) c.spinCd -= DT;
    if (c.dmgCd > 0) c.dmgCd -= DT;
    if (c.hitCd > 0) c.hitCd -= DT; // N6: see Car::hitCd
    if (c.spinRollCd > 0) c.spinRollCd -= DT;
    if (c.wallCd > 0) c.wallCd -= DT;

    // index.html:692-701: arm pit entry as the car reaches the frontstretch.
    // Now live -- tick()'s AI pit-strategy block sets c.pitReq/dtPending.
    if ((c.pitReq || c.dtPending) && c.pit == 0 && !c.done && !c.out && c.spinT <= 0 &&
        (state.flag != "yellow" || state.pitsOpen)) {
        const Seg& seg0 = track.segs()[0];
        double dIn = c.s - seg0.s0;
        if (dIn > track.total() / 2) dIn -= track.total();
        if (dIn > -25 && dIn < seg0.len * 0.04) {
            if (c.dtPending && !c.pitReq) {
                if (state.flag == "green") c.pit = 4;
            } else {
                c.pit = 1;
            }
        }
    }

    if (state.mode == "victory" && c.isPlayer) {
        // index.html:702-707: winner's burnout -- tight donuts on the
        // frontstretch. `skid` bump (index.html:707) is a render-only
        // screen-shake variable, not ported.
        thr = 0.6;
        brk = 0;
        steerIn = 0;
        c.hdg += 3.1 * DT;
        c.v = std::max(7.0, c.v * 0.985);
        freeSpin = true;
    } else if (c.out || c.done) {
        // index.html:708-730: DNF, or already-finished (cool-down) -- limp
        // to the infield apron and park.
        const double lane = -9.5;
        const double LAo = std::max(8.0, c.v * 0.62);
        PointResult pTo = track.pointAt(c.s + LAo);
        const double txo = pTo.x - std::sin(pTo.hdg) * lane, tyo = pTo.y + std::cos(pTo.hdg) * lane;
        double dHo = wrapPi(std::atan2(tyo - c.y, txo - c.x) - c.hdg);
        const double cFo = track.pointAt(c.s + std::max(6.0, c.v * 0.3)).curv;
        // P1 (NT2003 engine-feel plan): this and the other four muNow*
        // sites below are the AI's own aggregate yaw-limit lookahead (feeds
        // cornerCap(), the same single-scalar planning heuristic
        // cornerSpeed()/targetSpeed() use), not the actual per-tick tire
        // force integration -- that's the friction ellipse further down,
        // which now reads muEffLateralFront/muEffLateralRear instead.
        // Deliberately left on c.wear (now `max(wearFront, wearRear)`,
        // updated once per tick below) rather than split: a lookahead
        // yaw-rate CAP has no separate front/rear concept to split into,
        // and using the worse axle keeps this planning heuristic exactly
        // as conservative as before a car develops any front/rear split.
        const double muNowO = CAR.mu * (1 - 0.12 * c.wear) + CAR.dfK * c.v * c.v;
        const double yawLimO = std::min({1.3, std::max(0.05, c.v * 0.24),
                                          cornerCap(muNowO, track.bankAt(c.s + 10)) / std::max(3.0, c.v) * 1.15});
        steerIn = yawCorrected((c.v * cFo) / yawLimO + dHo * 1.4, yawLimO);
        thr = 0;
        brk = c.lat < -7 ? 0.9 : 0.25;
    } else if (c.spinT > 0) {
        // index.html:731-736: wrecked -- no control, rotating slide until it
        // scrubs off. c.isPlayer's `skid` bump (index.html:736) is a
        // render-only screen-shake variable, not ported.
        c.spinT -= DT;
        thr = 0;
        brk = 0.4;
        freeSpin = true;
        steerIn = 0;
        c.hdg += (c.spinDir != 0 ? c.spinDir : 1) * 4.0 * DT;
    } else if (c.pit > 0) {
        // index.html:737-773: auto-pit -- drive the apron lane, stop at the
        // stall, service, exit.
        const Seg& seg0 = track.segs()[0];
        const double sStall = pitStallS(track, c.idx);
        const double sOut = seg0.s0 + seg0.len * 0.97;
        double lane = (c.pit == 2) ? -10.5 : -8.4;
        double vT;
        if (c.pit == 1) {
            double ds = sStall - c.s;
            if (ds < -track.total() / 2) ds += track.total();
            if (ds > track.total() / 2) ds -= track.total();
            vT = std::min(22.0, std::max(0.0, ds * 0.35));
            if (ds < 2.0 && c.v < 0.8) {
                c.pit = 2;
                c.pitT = 4 + (c.fuel < 0.3 ? 2 : 0) + (c.dmg > 0.3 ? 3 : 0);
            }
        } else if (c.pit == 2) {
            vT = 0;
            c.pitT -= DT;
            if (c.pitT <= 0) {
                c.wear = 0;
                c.wearFront = 0;
                c.wearRear = 0;
                c.fuel = 1;
                c.dmg = std::max(0.0, c.dmg - 0.5);
                // P3: same partial-repair magnitude as c.dmg's own, nudged
                // toward (not snapped to) zero since dmgPull is signed.
                c.dmgPull -= sign(c.dmgPull) * std::min(std::abs(c.dmgPull), 0.5);
                c.blown = false;
                c.pit = 3;
            }
        } else if (c.pit == 4) {
            vT = 22;
            double de2 = sOut - c.s;
            if (de2 < -track.total() / 2) de2 += track.total();
            if (de2 > track.total() / 2) de2 -= track.total();
            if (de2 < 4) {
                c.pit = 0;
                c.pitReq = false;
                c.dtPending = false;
                if (state.flag == "yellow") c.cautionSlot = ++state.cautionMaxSlot;
            }
        } else {
            vT = 22;
            double de = sOut - c.s;
            if (de < -track.total() / 2) de += track.total();
            if (de > track.total() / 2) de -= track.total();
            if (de < 4) {
                c.pit = 0;
                c.pitReq = false;
                if (state.flag == "yellow") c.cautionSlot = ++state.cautionMaxSlot;
            }
        }
        const double LAp = std::max(8.0, c.v * 0.62);
        PointResult pTp = track.pointAt(c.s + LAp);
        const double txp = pTp.x - std::sin(pTp.hdg) * lane, typ = pTp.y + std::cos(pTp.hdg) * lane;
        double dHp = wrapPi(std::atan2(typ - c.y, txp - c.x) - c.hdg);
        const double cFF = track.pointAt(c.s + std::max(6.0, c.v * 0.3)).curv;
        const double muNowP = CAR.mu * (1 - 0.12 * c.wear) + CAR.dfK * c.v * c.v;
        const double yawLimP = std::min({1.3, std::max(0.05, c.v * 0.24),
                                          cornerCap(muNowP, track.bankAt(c.s + 10)) / std::max(3.0, c.v) * 1.15});
        steerIn = yawCorrected((c.v * cFF) / yawLimP + dHp * 1.5, yawLimP);
        if (c.v < vT - 0.3) {
            thr = 0.4;
            brk = 0;
        } else if (c.v > vT + 0.4) {
            thr = 0;
            brk = 0.8;
        } else {
            thr = 0.15;
            brk = 0;
        }
    } else if (state.mode == "race" && state.flag == "yellow") {
        // index.html:774-836: single file behind the pace car in assigned
        // slots.
        double vT;
        if (pace.state == "lead") {
            double ds = (pace.s - 16 - c.cautionSlot * 9) - c.s;
            if (ds < -track.total() / 2) ds += track.total();
            if (ds > track.total() / 2) ds -= track.total();
            const double catchCap = std::min(95.0, 44 + std::max(0.0, ds - 20) * 0.15);
            vT = std::min(catchCap, pace.v + ds * 0.4);
            vT = ds < 0 ? std::max(30.0, vT) : std::max(0.0, vT);
        } else {
            vT = 38;
        }
        for (auto& o : allCars) {
            if (&o == &c || o.pit > 0 || o.done || o.out) continue;
            double da = o.s - c.s;
            if (da < -track.total() / 2) da += track.total();
            if (da > track.total() / 2) da -= track.total();
            if (da > 0.5 && da < 80) vT = std::max(0.0, std::min(vT, o.v + std::max(0.0, da - 8) * 0.35));
        }
        // Regression-pass fix: the old model always enforced a real,
        // grip-based yaw cap (cornerCap()) in its shared execution physics,
        // regardless of what any given driving branch's target speed was --
        // so a caution-lap vT this branch could otherwise ask for (up to 95)
        // never actually threw a car off at a tight corner. The bicycle
        // model has no such blanket safety net (understeer replaces a hard
        // yaw clamp), so branches that don't already look ahead at curvature
        // need to do it themselves now. targetSpeed() is the AI-race
        // branch's own existing, already-tested corner-speed lookahead --
        // reused here rather than inventing a second formula.
        vT = std::min(vT, targetSpeed(track, c));
        const double lane = 0;
        const double LAy = std::max(12.0, c.v * 0.62);
        PointResult pTy = track.pointAt(c.s + LAy);
        const double txy = pTy.x - std::sin(pTy.hdg) * lane, tyy = pTy.y + std::cos(pTy.hdg) * lane;
        double dHy = wrapPi(std::atan2(tyy - c.y, txy - c.x) - c.hdg);
        const double cFFy = track.pointAt(c.s + std::max(6.0, c.v * 0.3)).curv;
        const double muNowY = CAR.mu * (1 - 0.12 * c.wear) + CAR.dfK * c.v * c.v;
        const double yawLimY = std::min({1.3, std::max(0.05, c.v * 0.24),
                                          cornerCap(muNowY, track.bankAt(c.s + 10)) / std::max(3.0, c.v) * 1.15});
        steerIn = yawCorrected((c.v * cFFy) / yawLimY + dHy * 1.3, yawLimY);
        if (c.v < vT - 0.3) {
            thr = std::min(1.0, 0.45 + (vT - c.v) * 0.04);
            brk = 0;
        } else if (c.v > vT + 0.5) {
            thr = 0;
            brk = std::min(1.0, 0.6 + (c.v - vT) * 0.03);
        } else {
            thr = 0.2;
            brk = 0;
        }
    } else if (state.mode == "pace") {
        // index.html:837-858: formation -- hold grid lane, match pace speed,
        // keep gap to the car ahead.
        double vT = state.paceV;
        // M1: upper bound checked too, not just >= 0. gridAhead is an index
        // into the field as it stood at gridStart(); any caller holding a
        // smaller field than that (tests/drivability_test.cpp strips the
        // 20-car field to the player alone, leaving gridAhead == 17 against a
        // size-1 vector) otherwise reads out of bounds here, and the whole
        // pace-phase target speed is then derived from that garbage.
        const Car* ahead =
            (c.gridAhead >= 0 && c.gridAhead < (int)allCars.size()) ? &allCars[c.gridAhead] : nullptr;
        if (ahead) {
            double ds = ahead->s - c.s;
            if (ds < -track.total() / 2) ds += track.total();
            if (ds > track.total() / 2) ds -= track.total();
            vT = ahead->v + (ds - 9) * 0.6;
        } else {
            double dp = pace.s - c.s;
            if (dp < -track.total() / 2) dp += track.total();
            if (dp > track.total() / 2) dp -= track.total();
            if (pace.state == "lead") vT = std::min(vT, state.paceV + (dp - 16) * 0.5);
        }
        // Regression-pass fix: see the identical comment in the yellow-flag
        // branch above -- pace-lap speed here was never curvature-aware
        // either, relying on the same now-removed blanket yaw-cap safety net.
        vT = std::min(vT, targetSpeed(track, c));
        const double lane = c.gridLane;
        const double LA = std::max(12.0, c.v * 0.62);
        PointResult pT = track.pointAt(c.s + LA);
        const double tx = pT.x - std::sin(pT.hdg) * lane, ty = pT.y + std::cos(pT.hdg) * lane;
        double dHdg = wrapPi(std::atan2(ty - c.y, tx - c.x) - c.hdg);
        const double curvFF = track.pointAt(c.s + std::max(6.0, c.v * 0.3)).curv;
        const double muNowC = CAR.mu * (1 - 0.12 * c.wear) + CAR.dfK * c.v * c.v;
        const double yawLimC = std::min({1.3, std::max(0.05, c.v * 0.24),
                                          cornerCap(muNowC, track.bankAt(c.s + 10)) / std::max(3.0, c.v) * 1.15});
        steerIn = yawCorrected((c.v * curvFF) / yawLimC + dHdg * 1.3, yawLimC);
        if (c.v < vT - 0.3) {
            thr = 0.5;
            brk = 0;
        } else if (c.v > vT + 0.5) {
            thr = 0;
            brk = 0.5;
        } else {
            thr = 0.25;
            brk = 0;
        }
    } else if (c.isPlayer) {
        // index.html:859-864
        // M1: the ONLY branch that reads PlayerInput, and therefore the only
        // one whose steerIn is a digital key press rather than a computed
        // continuous value. The player-feel transformations in the shared
        // tail below key off this flag, not off c.isPlayer.
        humanInput = true;
        thr = input.gas ? 1 : 0;
        brk = input.brake ? 1 : 0;
        steerIn = (input.left ? -1 : 0) + (input.right ? 1 : 0);
        if (state.tilt) steerIn = std::max(-1.0, std::min(1.0, state.tiltG / 22));
        steerIn *= 1 - 0.10 * std::min(1.0, c.v / 85);
        // P3 (NT2003 engine-feel plan): JS's fixed-direction, player-only
        // `steerIn += c.dmg*0.02` (index.html:986) is replaced by the
        // general, signed `c.dmgPull` bias applied to every branch in the
        // shared physics tail below -- see car.h's own comment.
    } else {
        // index.html:865-975 (AI race branch)
        const double vT = targetSpeed(track, c);

        bool restartHeld = false;
        double laneEase = 1;
        const double holdLane = c.cautionSlot >= 0 ? 0 : c.gridLane;
        const int holdSlot = c.cautionSlot >= 0 ? c.cautionSlot : std::max(0, c.gridSlot);
        if (state.flag == "green") {
            const double releaseAt = 0.4 + holdSlot * 0.12;
            const double heldT = state.sinceGreenT - releaseAt;
            restartHeld = heldT < LANE_EASE_DUR;
            laneEase = std::max(0.0, std::min(1.0, heldT / LANE_EASE_DUR));
        }
        double lane = holdLane + (c.grooveBias - holdLane) * laneEase;

        const Car* blocker = nullptr;
        double bd = 1e9;
        for (auto& o : allCars) {
            if (&o == &c) continue;
            double ds = o.s - c.s;
            if (ds < -track.total() / 2) ds += track.total();
            if (ds > track.total() / 2) ds -= track.total();
            if (ds > 0 && ds < 26 && std::abs(o.lat - c.lat) < 2.4 && ds < bd) {
                bd = ds;
                blocker = &o;
            }
        }
        if (blocker && !restartHeld) {
            if (c.passT <= 0) {
                double side = blocker->lat > 0.3 ? -1 : (blocker->lat < -0.3 ? 1 : (c.passSide != 0 ? c.passSide : 1));
                const double tryLat = std::max(-6.0, std::min(6.0, blocker->lat + side * 5.0));
                bool laneTaken = false;
                for (auto& o : allCars) {
                    if (&o == &c || &o == blocker) continue;
                    double ds2 = o.s - c.s;
                    if (ds2 < -track.total() / 2) ds2 += track.total();
                    if (ds2 > track.total() / 2) ds2 -= track.total();
                    if (ds2 > -4 && ds2 < 26 && std::abs(o.lat - tryLat) < 2.4) {
                        laneTaken = true;
                        break;
                    }
                }
                c.passSide = laneTaken ? -static_cast<int>(side) : static_cast<int>(side);
                c.passT = 2.5;
            }
            lane = std::max(-6.0, std::min(6.0, blocker->lat + c.passSide * 5.0));
        }
        if (c.passT > 0 && !restartHeld) c.passT -= DT;

        const double LA = std::max(12.0, c.v * 0.62);
        PointResult pT = track.pointAt(c.s + LA);
        const double tx = pT.x - std::sin(pT.hdg) * lane, ty = pT.y + std::cos(pT.hdg) * lane;
        double dHdg = wrapPi(std::atan2(ty - c.y, tx - c.x) - c.hdg);
        const double curvFF = track.pointAt(c.s + std::max(6.0, c.v * 0.3)).curv;
        const double muNow = CAR.mu * (1 - 0.12 * c.wear) + CAR.dfK * c.v * c.v;
        const double yawLim = std::min({1.3, std::max(0.05, c.v * 0.24),
                                         cornerCap(muNow, track.bankAt(c.s + 10)) / std::max(3.0, c.v) * 1.15});
        const double ff = (c.v * curvFF) / std::max(0.05, yawLim);
        steerIn = yawCorrected(ff + dHdg * 1.3, yawLim);

        // N2: `c.v > blocker->v - 0.5` is a "not meaningfully slower than the
        // car ahead" test, and it is correct while moving -- but it does not
        // require any actual CLOSING SPEED, so two stopped cars satisfy it
        // (0 > -0.5). The result is a permanent latch: a stationary car sees a
        // stationary blocker within 8 m, commands thr=0/brk=0.7, and therefore
        // stays stationary, forever.
        //
        // That is not a corner case. Measured on Cedar Valley, the entire
        // 19-car field ended up jammed into a 33 m stretch (s=1770..1803) with
        // 11 active collision pairs, every car at exactly 0.00 m/s with zero
        // throttle, and the race never finished -- the field simply froze at
        // t~450 s and sat there. That is the "after one lap all cars crashed"
        // report: they had not crashed, they had deadlocked.
        //
        // The `c.v > 2.0` floor breaks the latch: below walking pace a car
        // stops yielding to the queue and falls through to its normal
        // target-speed logic, so the jam disperses. Contact at under 2 m/s is
        // harmless, and above that the original yielding behaviour is intact.
        // N6: the AI half of the jam, and a fix that was tried and REJECTED.
        //
        // The `c.v > 2.0` floor was added (N2) to break a hard freeze: below
        // walking pace a car stops yielding and falls through to its normal
        // target-speed logic, which unfreezes it. The cost is that "normal
        // target-speed logic" at 0 m/s means full throttle, so a stopped car
        // in a queue drives into the stopped car in front of it.
        //
        // A third "crawl" state was added to express what a queue actually
        // needs -- light throttle under 4 m of gap, enough to shuffle forward
        // without ramming -- and it MEASURED WORSE: Milltown's moving fraction
        // fell from 0.57/0.62 to 0.64/0.21, because a continuous gentle push
        // into the car ahead is still a push, and the collision response
        // dissipates exactly that. Recorded so it is not retried. The jam is
        // improved by the collision fix in race.cpp, not by this branch.
        const bool closing = blocker && bd < 8 && c.v > blocker->v - 0.5 && c.v > 2.0;
        if (closing) {
            thr = 0;
            brk = 0.7;
        } else if (c.v < vT - 0.4) {
            thr = std::min(1.0, 0.55 + 0.45 * c.aggr);
            brk = 0;
        } else if (c.v > vT + 0.8) {
            thr = 0;
            brk = std::min(1.0, (c.v - vT) * 0.35);
        } else {
            thr = 0.35;
            brk = 0;
        }
    }

    // Spin-recovery override: a real driver lifts off the throttle once the
    // car is visibly out of control, rather than holding a fixed throttle
    // command while spinning (the "avoid crash" racecraft behavior real
    // racing-sim AI already models). c.r here is still last tick's yaw rate
    // (this tick's integration hasn't run yet) -- an already-observed "am I
    // currently spinning" signal, not a new planning heuristic. Only engages
    // far beyond any legitimate cornering yaw rate (regression pass: normal
    // racing at speed never exceeds ~1 rad/s; the pathological sustained-
    // full-lock case reaches 3-4+ rad/s), so ordinary cornering is
    // unaffected. Deliberately leaves steerIn untouched -- it's already the
    // yaw-rate-error feedback's job (CarConstants::yawCorrGain) to correct
    // the spin, and reducing steering authority on top of that made overall
    // damage worse, not better, when tried.
    // M1: gated on humanInput, not c.isPlayer -- a human keeps the right to
    // hold the throttle through a slide, but the player's car while the GAME
    // is driving it (pace/caution/pit) gets the same spin recovery every AI
    // car gets. Withholding it there just meant the auto-driver was denied a
    // tool it was written to rely on.
    if (!freeSpin && !humanInput && std::abs(c.r) > 1.2) {
        const double lift = std::max(0.0, 1.0 - (std::abs(c.r) - 1.2) / 2.0);
        thr *= lift;
        if (lift < 1.0) brk = std::max(brk, 0.3);
    }

    // ---- shared physics tail (index.html:977-1109), applies to every branch ----
    // P3 (NT2003 engine-feel plan, damage that pulls): added AFTER every
    // branch above has already computed its own steerIn (including the AI's
    // yawCorrGain feedback baked into yawCorrected()'s calls) -- so this is a
    // fresh disturbance the yaw-rate-error feedback only starts correcting
    // for on SUBSEQUENT ticks, not something silently cancelled before it
    // ever shows up. Applies to every car, not just the player.
    constexpr double kDmgPullSteerGain = 0.06;
    steerIn += c.dmgPull * kDmgPullSteerGain;
    c.thr += (thr - c.thr) * 0.28;
    c.brk += (brk - c.brk) * 0.4;
    // L1 (NT2003/2004 fidelity pass): the player's blend was 0.22, an exact
    // port of index.html's own steerIn smoothing. Deliberately diverged to
    // 0.12 -- the FIRST intentional divergence from JS here, so it needs its
    // own justification.
    //
    // JS could use 0.22 safely because its kinematic model mapped c.steer
    // straight to a yaw-rate fraction; there was no steer angle and no tire.
    // Against the real bicycle model, and against a full-lock angle now at a
    // realistic 0.26 rad (see CarConstants::maxSteerAngle), 0.22 per tick
    // reaches 81% of full lock -- ~12 degrees of steering -- within a 160ms
    // "tap", which is the reported "one small tap car jolts hard that
    // direction". This is where that belongs: the player's digital on/off
    // input has no analogue travel, so the ramp rate IS the input curve.
    //
    // 0.12 was measured, not guessed, against the drivability harness on all
    // four tracks plus the tap harness: it halves a 160ms tap's yaw response
    // (r 0.68 -> 0.58 rad/s, heading change 3.19 -> 2.33 degrees) at
    // IDENTICAL drivability (damage 0.275/0.402/0.351/0.000 either way), so
    // it is a free win. Slower ramps keep improving tap feel but start
    // costing recovery authority -- 0.07 already raises Thunder Oval's damage
    // 0.28 -> 0.53, and 0.04 collapses to write-offs on two tracks, because
    // the player can no longer wind on lock fast enough to catch a slide.
    // The AI's own 0.5 is untouched: its steerIn is a continuous computed
    // value, not a digital key, so it has no tap-harshness problem to fix.
    //
    // M1: that last sentence is exactly why this is gated on humanInput and
    // not on c.isPlayer. When an AI branch drives the player's car (pace,
    // caution, pit) its steerIn is likewise a continuous computed value, so
    // it needs the AI's 0.5 too -- at 0.12 the player's car tracked the
    // formation-lap steering command with ~4x the lag of the cars around it.
    // N4: the player's rate is asymmetric -- winding the wheel ON keeps 0.12,
    // letting it return toward centre uses the slower steerReleaseRamp. A
    // release is "the target is closer to centre than the wheel currently is",
    // which covers both letting go entirely and easing off part way; reversing
    // to the opposite lock is NOT a release and keeps the fast apply rate, so
    // catching a slide is untouched. See car.h's steerReleaseRamp for the
    // measurements. The AI's 0.5 stays symmetric: its steerIn is continuous,
    // so it has no snap-back transient to damp.
    double playerRamp = 0.12;
    if (humanInput && std::fabs(steerIn) < std::fabs(c.steer) && steerIn * c.steer >= 0.0) {
        playerRamp = CAR.steerReleaseRamp;
    }
    c.steer += (steerIn - c.steer) * (humanInput ? playerRamp : 0.5);

    ProjectResult proj = track.project(c.x, c.y);
    c.s = proj.s;
    c.lat = proj.lat;
    const double off = std::abs(c.lat);
    const bool onGrass = off > track.halfW() + 0.4 && c.lat < 0 && c.pit == 0;
    const double bank = track.bankAt(c.s);
    const double muSurf = (onGrass ? 0.72 : 1.0) * (1 - 0.12 * c.wear) * (c.blown ? 0.7 : 1);
    if (c.blown && c.v > 30) c.v = std::max(30.0, c.v - 18 * DT);
    // Tire-model upgrade: aero no longer adds straight into a scalar mu --
    // it's now a real Fz contribution (axleLoads() below). aeroEfficiency
    // carries over the exact same dirty-air/damage degradation the old
    // mu-additive term had (see axleLoads()'s own comment).
    const double muEff = CAR.mu * muSurf;
    const double aeroEfficiency = (c.dirty ? 0.85 : 1) * (1 - 0.35 * c.dmg);

    // Tire-model upgrade regression-pass fix: cornerCap() (car.cpp) already
    // accounts for banking's centripetal assist when planning a safe corner
    // speed (targetSpeed()/yawLim in every stepCar() branch above) via
    // G*(mu+tan(bank))/(1-mu*tan(bank)) -- but the actual tire-force
    // integration below only ever received the flat, unbanked muEff, so a
    // car driven at the speed a banked turn's targetSpeed() judged "safe"
    // could never actually generate as much real lateral force as planning
    // assumed. Mirrors cornerCap()'s own formula for the lateral-force
    // integration specifically so planning and physics agree on how much
    // banking is worth (longitudinal accel/brake traction below is left on
    // the flat muEff -- banking's main effect is cornering grip).
    //
    // P1 (NT2003 engine-feel plan, the loose/tight axis): muEff/muSurf above
    // stay a single shared value for the LONGITUDINAL grip they gate (the
    // rear-axle traction-budget cap on engine force, and brake force split
    // across both axles by brakeBiasFront) -- out of this phase's scope,
    // which is specifically the cornering (lateral) friction ellipse.
    // muEffLateralFront/Rear replace the old single muEffLateral there: each
    // axle's OWN wear now degrades only that axle's own cornering grip, so
    // axleLateralForce() (car.cpp) genuinely bounds the front and rear
    // differently once they've worn unevenly -- the mechanism the whole
    // loose/tight axis runs on.
    const double bankTan = std::tan(bank);
    // P3 (NT2003 engine-feel plan, damage that pulls): "unbalances front
    // grip" -- one-sided damage shaves front-axle grip proportional to
    // |dmgPull| (magnitude only; a bent nose costs front grip whichever way
    // it's actually bent), alongside wearFront's own P1 penalty. The rear
    // axle is untouched -- dmgPull's steering bias (the shared physics tail
    // above) is the mechanism that makes the car pull; this is the
    // "fighting it" half, not a second copy of the same effect.
    constexpr double kDmgPullFrontGrip = 0.15;
    const double muBaseFront = CAR.mu * (onGrass ? 0.72 : 1.0) * (1 - 0.12 * c.wearFront) * (c.blown ? 0.7 : 1) *
                                (1 - kDmgPullFrontGrip * std::abs(c.dmgPull));
    const double muBaseRear = CAR.mu * (onGrass ? 0.72 : 1.0) * (1 - 0.12 * c.wearRear) * (c.blown ? 0.7 : 1);
    const double muEffLateralFront = (muBaseFront + bankTan) / std::max(0.25, 1.0 - muBaseFront * bankTan);
    const double muEffLateralRear = (muBaseRear + bankTan) / std::max(0.25, 1.0 - muBaseRear * bankTan);

    c.fuel = std::max(0.0, c.fuel - c.thr * c.v * DT * 5e-5);

    // P2 (NT2003 engine-feel plan, fuel as real mass): every mass/weightDistF
    // read below (axle loads, longitudinal acceleration, yaw dynamics) goes
    // through this per-tick copy instead of CAR directly, so a full tank
    // measurably adds mass and shifts static balance toward the front (see
    // CarConstants::fuelWeightShiftF's comment for why front, not the
    // rear-mounted-cell-implies-rearward assumption tried and rejected
    // first), and both fall away smoothly as the tank burns off. mu/aero/
    // drivetrain constants are untouched -- only these two fields change.
    CarConstants carEff = CAR;
    carEff.mass = CAR.mass + c.fuel * CAR.fuelMass;
    carEff.weightDistF = CAR.weightDistF + c.fuel * CAR.fuelWeightShiftF;

    // P4 (NT2003 engine-feel plan, pit adjustments): the player's persistent
    // setupWedge/setupTrackBar knobs (0 for every AI car, always) layer onto
    // the SAME carEff copy fuel already adjusts -- setupWedge adds to
    // weightDistF exactly like fuel's own shift (same verified-correct
    // "toward front = tighter" sign), setupTrackBar scales ONLY the rear
    // axle's cornering stiffness (see CarConstants::trackBarStiffnessRange's
    // comment for why cr alone, and the verified sign).
    carEff.weightDistF += c.setupWedge * CAR.wedgeWeightDistRange;
    carEff.cr = CAR.cr * (1.0 + c.setupTrackBar * CAR.trackBarStiffnessRange);

    const double dragMod = (1 - 0.25 * c.draftF) * (c.dirty ? 1.10 : 1) * (1 + 0.5 * c.dmg);
    const double drag = 0.5 * RHO * CAR.cdA * c.v * c.v * dragMod;
    const double roll = CAR.roll + (onGrass ? 900 : 0);

    // Drivetrain upgrade: gearRpm() (src/render/gear_rpm.h) was previously
    // cosmetic-only (HUD readout + engine-audio pitch). Reused here verbatim
    // (same gear breakpoints) to shape engFRaw's accel curve
    // (torqueCurveMultiplier()) and to detect gear changes, which apply a
    // brief force-reduction dip -- mirroring a real shift's momentary power
    // interruption. On the tick a shift is detected, shiftCd is set to its
    // full duration immediately (the dip is active that same tick).
    //
    // N5: two defects here made the drivetrain hunt, reported as "shifting
    // seems to struggle so cars aren't going as fast as they should".
    //
    // 1. gearRpm() is a pure function of INSTANTANEOUS speed with hard
    //    breakpoints (14/26/40/70 m/s) and no hysteresis, so a car whose speed
    //    sits on a boundary -- which is exactly what happens mid-corner --
    //    flips gear back and forth.
    // 2. shiftCd only counted down in the `else`, i.e. only on ticks where the
    //    gear did NOT change. A hunting car therefore re-armed the dip at
    //    least as fast as it could ever expire, pinning the engine at
    //    shiftDipMag (60%) more or less permanently.
    //
    // Measured on Cedar Valley before the fix: 74 shifts in 120 s, engine held
    // at 60% power 10.1% of the time. After: 12 shifts, 1.6%.
    //
    // Fix is both halves -- decrement unconditionally, and require the car to
    // be 4% clear of the boundary before committing the change. The boundary
    // comes from gearBreakSpeed() rather than a copy of the table, so this
    // cannot drift away from gearRpm()'s own breakpoints.
    const GearRpm gear = gearRpm(c.v);
    if (c.shiftCd > 0) c.shiftCd -= DT;
    if (gear.gear != c.prevGear) {
        const double edge = gearBreakSpeed(std::min(gear.gear, c.prevGear));
        if (c.v > edge * 1.04 || c.v < edge * 0.96) {
            c.prevGear = gear.gear;
            c.shiftCd = CAR.shiftDipDur;
        }
    }
    const double shiftMult = c.shiftCd > 0 ? CAR.shiftDipMag : 1.0;

    const double engFRaw = c.thr * std::min(CAR.maxForce, CAR.power / std::max(4.0, c.v)) *
                            (onGrass ? 0.75 : 1) * (1 - 0.3 * c.dmg) * (c.fuel > 0 ? 1 : 0.25) *
                            torqueCurveMultiplier(gear) * shiftMult;
    const double brkF = c.brk * CAR.brakeForce * muSurf;

    // Traction budget: the drive wheels can't transmit more longitudinal
    // force than available rear-axle grip allows. Without this cap, full
    // throttle regularly demanded more force than the tires could put down
    // (regression pass finding: CAR.maxForce sits close to/above static rear
    // traction, so fxFracRear pinned at 1.0 whenever a car needed to
    // accelerate from near-zero speed -- e.g. right after a wall-clamp reset
    // -- permanently zeroing rear lateral grip via the friction ellipse
    // below and leaving the car unable to steer away from the wall at all;
    // confirmed via a git-worktree regression run against the pre-tire-model
    // baseline: baseline finished the same headless race with wreckCount=3
    // and every car completing laps, this model left the entire field stuck
    // at lap=-1 with wreckCount=0). Uses last tick's acceleration (`c.aPrev`)
    // to estimate this tick's rear axle load before engine force is finalized
    // -- avoids a circular dependency (axleLoads() needs `a`, which needs
    // this cap first) at the cost of a one-tick-stale weight-transfer
    // estimate, self-correcting every tick.
    // Suspension upgrade: axleLoads() itself stays instantaneous/unmodified
    // (preserves its own passing unit tests) -- lag its output here via a
    // first-order filter (CarConstants::suspRate) so Fz has spring/damper-
    // like settling time instead of jumping discontinuously every tick.
    // Steady state is unaffected; only the transient response smooths out.
    // Every downstream use of `fz` below (traction budget, fxFrac, the yaw
    // integration, and the final c.fzFront/c.fzRear assignment) reads this
    // lagged value, not axleLoads()'s raw instantaneous one.
    const AxleLoads fzRaw = axleLoads(carEff, c.v, c.aPrev, aeroEfficiency);
    c.fzFrontLag = suspensionLag(c.fzFrontLag, fzRaw.front, CAR.suspRate, DT);
    c.fzRearLag = suspensionLag(c.fzRearLag, fzRaw.rear, CAR.suspRate, DT);
    const AxleLoads fz{c.fzFrontLag, c.fzRearLag};
    const double engF = std::min(engFRaw, muEff * fz.rear);

    double a = (engF - drag - roll * sign(c.v != 0 ? c.v : 1) - brkF * (c.v > 0 ? 1 : 0)) / carEff.mass;
    c.v = std::max(0.0, c.v + a * DT);
    c.aPrev = a;
    c.pitch += ((-a * 0.006) - c.pitch) * 0.12;

    const double vSafe = std::max(3.0, c.v);
    // Tire-model upgrade: the slip-angle formula's atan2(vy/v, r/v)-style
    // terms get MORE sensitive as v shrinks, not less -- confirmed via a
    // headless regression run: cars launching from a stop (v near 0) hit
    // this floor at its lowest, produced huge apparent slip angles from tiny
    // vy/r, saturated the friction ellipse immediately, and got stuck
    // wedged against the inside wall spinning their wheels indefinitely
    // (TIREDBG trace: v stuck ~1-2 m/s, slipRatio ~0.9, lat pinned at the
    // wall-clamp boundary, for 60+ simulated seconds straight). A real car
    // doesn't drift/slip meaningfully at parking-lot speed either -- the old
    // kinematic model's behavior there was already fine -- so the dynamics
    // get their own, higher floor instead of reusing vSafe.
    const double vDyn = std::max(8.0, c.v);

    // Tire-model upgrade: real bicycle-model cornering physics -- replaces
    // the old single-scalar cornerCap()-capped yaw-rate formula. cornerCap()
    // itself is untouched and still used by cornerSpeed()/targetSpeed() for
    // the AI's forward-looking corner-speed planning (verified bit-for-bit
    // against JS by speed_model_test.cpp) -- this block only changes how
    // stepCar() executes the actual per-tick cornering physics, a separate
    // concern from how far ahead the AI plans.
    if (freeSpin) {
        // Victory burnout / wreck spin-out: those branches drive c.hdg
        // directly themselves. Decay the dynamic state toward zero instead
        // of integrating it -- real tire forces have no reason to resolve
        // sensibly against an externally forced rotation, and stale
        // vy/r would otherwise make the car lurch the instant normal
        // control resumes.
        c.vy *= std::max(0.0, 1.0 - 6.0 * DT);
        c.r *= std::max(0.0, 1.0 - 6.0 * DT);
        c.fzFront = carEff.mass * G * carEff.weightDistF;
        c.fzRear = carEff.mass * G * (1 - carEff.weightDistF);
        c.slipRatio = 0;
    } else {
        c.fzFront = fz.front;
        c.fzRear = fz.rear;

        // L8 (NT2003/2004 fidelity pass): the player's digital input goes
        // through a curve rather than straight through, so a short tap is a
        // correction instead of a lock-slam -- see CarConstants::
        // steerCurveGamma for the measurements behind it. Player-gated on
        // purpose: the AI's steerIn is a continuous computed value out of
        // yawCorrected(), not an on/off key, so it has no resolution problem
        // to solve, and curving it would move every AI racing line and lap
        // time for nothing.
        //
        // M1: gated on humanInput rather than c.isPlayer, because that
        // "continuous computed value out of yawCorrected()" is precisely what
        // the pace/caution/pit branches feed in for the PLAYER's car too. The
        // curve and the cap below then compounded on a value that needed
        // neither, delivering under 10% of the commanded angle at pace speed
        // and steering the player's car off the formation line. Whether the
        // curve applies must follow where steerIn CAME FROM, not whose car
        // it is.
        double steerAngle = c.steer * CAR.maxSteerAngle;
        if (humanInput) {
            // L15: cap full lock by constant yaw AUTHORITY before applying
            // L8's curve. Full lock is 15 deg, but the tightest corner needs
            // 2.47 deg at racing speed -- so holding the key used to command
            // ~6x what the track asks for, i.e. a spin ("car spins wildly").
            // Inverting the steady-state relation r = v*delta/(L + Kus*v^2),
            // delta_max = C*(L + Kus*v^2)/v makes full input command exactly
            // C rad/s at any speed. Clamped to maxSteerAngle so low speed
            // keeps every degree of lock for manoeuvring and spin recovery.
            const double aF = CAR.wheelBase * (1 - CAR.weightDistF);
            const double aR = CAR.wheelBase * CAR.weightDistF;
            const double kus = (CAR.mass / CAR.wheelBase) * (aR / CAR.cf - aF / CAR.cr);
            const double vAuth = std::max(6.0, vSafe);
            const double lockAuth = CAR.steerYawAuthority * (CAR.wheelBase + kus * vAuth * vAuth) / vAuth;
            // N1b: blend the cap in with speed instead of applying it flat.
            // Lowering steerYawAuthority to a value that stops the front tires
            // saturating at racing speed (see car.h) would otherwise also strip
            // lock at PIT-LANE speed, where the cap has no business acting --
            // spin recovery and low-speed manoeuvring need every degree of the
            // mechanical lock, which is a property tire_model_test pins and
            // which L15 deliberately protected. Full mechanical lock below
            // 10 m/s, fully capped above 16, linear between:
            //
            //      8 m/s  14.90 deg (full lock, unchanged)
            //     16 m/s   5.87 deg
            //     45 m/s   3.02 deg   (was 5.22 at the old uncapped 0.95)
            const double capBlend = std::max(0.0, std::min(1.0, (vAuth - 10.0) / 6.0));
            const double lockBlended = CAR.maxSteerAngle + capBlend * (lockAuth - CAR.maxSteerAngle);
            // N7: never less than the tightest corner in the game actually
            // needs at this speed. The constant-yaw cap above scales as 1/v
            // while a corner's demand scales as v/R, so without this the car
            // becomes physically unable to turn in above cap*R -- which is
            // reachable at the start of a race. See car.h's
            // steerCornerFloorMargin for the ceiling table and the crossover.
            const double lockFloor = CAR.steerCornerFloorMargin *
                                     (CAR.wheelBase + kus * vAuth * vAuth) / CAR.steerTightestCornerR;
            const double lock = std::min(CAR.maxSteerAngle, std::max(lockBlended, lockFloor));
            // L8's curve, applied to that speed-appropriate lock: fine
            // control near centre, and now a ceiling that is a hard corner
            // rather than a spin.
            const double mag = std::pow(std::fabs(c.steer), CAR.steerCurveGamma);
            steerAngle = sign(c.steer) * mag * lock;
        }

        // Longitudinal-grip fraction already spent at each axle (RWD engine
        // force + brake-bias split between the axles) -- feeds the friction
        // ellipse below, so less lateral grip is available under hard
        // acceleration or heavy braking (trail-braking, power-oversteer).
        const double fxRear = engF - brkF * (1 - CAR.brakeBiasFront);
        const double fxFront = -brkF * CAR.brakeBiasFront;
        const double fxFracRear = std::max(-1.0, std::min(1.0, fxRear / (muEff * fz.rear)));
        const double fxFracFront = std::max(-1.0, std::min(1.0, fxFront / (muEff * fz.front)));
        c.slipRatio = fxFracRear;

        // Regression-pass fix: vy/r/hdg integration now runs substepped
        // inside integrateYawDynamics() (car.cpp) -- fixes a numerical
        // divergence in this coupled oscillator at highway speed under
        // sustained full-lock steering (see CarConstants::yawSubsteps).
        const YawIntegrationResult yawInt =
            integrateYawDynamics(carEff, c.vy, c.r, vDyn, vSafe, steerAngle, fz, muEffLateralFront, muEffLateralRear,
                                 fxFracFront, fxFracRear, DT, CAR.yawSubsteps);
        c.vy = yawInt.vy;
        c.r = yawInt.r;
        c.hdg += yawInt.hdgDelta;

        // P1 (NT2003 engine-feel plan, the loose/tight axis): the old model's
        // two wear contributions (a steady rate proportional to slip while
        // cornering, plus a flat bump once the friction ellipse actually
        // clamped) now apply PER AXLE, from that same axle's own slip/limit
        // output -- this is the entire mechanism the loose/tight axis runs
        // on: push the car into understeer and the fronts genuinely see more
        // slip and clamp more often than the rears, so wearFront specifically
        // grows faster, tightening the car further; drive it loose and the
        // rears wear instead, loosening it further.
        //
        // The per-axle rate constant is 2x the old shared 0.0000004 (not the
        // same value split in half): the old formula summed BOTH axles'
        // slip magnitudes into one term before scaling, so under perfectly
        // symmetric slip each axle's OWN magnitude was already only half of
        // that sum. Applying the old constant unscaled per axle would make
        // max(wearFront, wearRear) climb at half the old combined pace for
        // an ordinary, roughly-symmetric driving line -- doubling it keeps
        // an unworn car's overall wear pace matching every prior phase's
        // tuning/playtest baseline, and only the FRONT-vs-REAR split is new.
        constexpr double kWearRate = 0.0000008;
        c.wearFront = std::min(1.0, c.wearFront + yawInt.slipFrontAvg * c.v * kWearRate);
        c.wearRear = std::min(1.0, c.wearRear + yawInt.slipRearAvg * c.v * kWearRate);
        if (yawInt.pastLimitFront) c.wearFront = std::min(1.0, c.wearFront + 0.00012);
        if (yawInt.pastLimitRear) c.wearRear = std::min(1.0, c.wearRear + 0.00012);
        c.wear = std::max(c.wearFront, c.wearRear);

        // H4 (NT2003 engine-feel plan): tire-smoke drive signal (car.h's
        // own slipFx comment). Boosts toward the current tick's slip
        // intensity rather than overwriting it outright, so a car that was
        // sliding hard a moment ago doesn't visually "unsmoke" mid-tick --
        // the actual decay-over-time is the render-side particle tick's job
        // (particles.cpp), exactly mirroring how JS's sim only ever SETS
        // c.slipFx=1 and leaves emitFX() to bring it back down.
        // kSlipFxScale=6 maps a total front+rear slip angle of ~0.17 rad to
        // full intensity -- roughly this car's own saturation slip angle
        // (fyMax/cf at static Fz, ~0.077 rad per axle, summed) at CAR.mu=1;
        // pastLimitAny already guarantees a hard 1.0 right at the real
        // friction limit regardless of this estimate's precision, so this
        // constant only shapes the smoke's build-up just below full lock,
        // not whether it ever reaches full intensity.
        constexpr double kSlipFxScale = 6.0;
        const double slipIntensity =
            yawInt.pastLimitAny ? 1.0 : std::min(1.0, yawInt.slipMagAvg * kSlipFxScale);
        c.slipFx = std::max(c.slipFx, slipIntensity);
    }
    c.vdir = c.hdg - wrapPi(std::atan2(c.vy, vSafe));
    c.x += std::cos(c.vdir) * c.v * DT;
    c.y += std::sin(c.vdir) * c.v * DT;

    const double wallClampLat = track.halfW() + 5.0; // WALL_LAT(halfW+6) - CAR_HALF_WID(1)
    if (off > wallClampLat) {
        ProjectResult p2 = track.project(c.x, c.y);
        const double nx = -std::sin(p2.hdg), ny = std::cos(p2.hdg);
        const double excess = std::abs(p2.lat) - wallClampLat;
        c.x -= nx * sign(p2.lat) * excess;
        c.y -= ny * sign(p2.lat) * excess;

        // Tire-model upgrade regression-pass fix: a car moving slowly enough
        // to still be past wallClampLat after this tick's own re-clamp
        // re-triggers this whole block again next tick (confirmed via a
        // headless regression run: a car embedded near a wall at low speed
        // retriggered it every single tick, forever). The old kinematic yaw
        // model recomputed heading fresh from steerIn every frame with no
        // persistent state to lose, so that was harmless there. The new
        // bicycle model's vy/r carry real inertia, though -- wiping them
        // (plus the speed cut below) on every one of those ticks never let a
        // turn-away yaw rate survive more than one 20ms step, wedging the
        // car at the wall indefinitely (regression pass: the whole AI field
        // got stuck this way on a race-start stress test that the
        // pre-tire-model baseline passed cleanly). c.wallCd -- separate from
        // c.dmgCd, which stays suppressed during yellow flag and must not
        // gate this -- limits the full "fresh impact" response (speed loss,
        // heading snap, vy/r reset, damage) to once per contact, so the yaw
        // dynamics get real ticks to actually turn the car off the wall.
        if (c.wallCd <= 0) {
            c.wallCd = 0.3;
            const double vLost = c.v * 0.28;
            c.v *= 0.95;
            if (c.spinT <= 0) {
                double dh = wrapPi(c.hdg - p2.hdg);
                c.hdg = p2.hdg + dh * 0.9;
            }
            c.vdir = c.hdg;
            c.vy = 0;
            c.r = 0;
            if (c.dmgCd <= 0 && state.flag != "yellow") {
                const double dmgDelta = std::min(0.12, vLost * 0.005);
                c.dmg = std::min(1.0, c.dmg + dmgDelta);
                // P3: signed by which wall (p2.lat's sign) was actually hit,
                // set at this exact impact moment -- see car.h's own comment
                // on why this replaces JS's fixed-direction nudge.
                c.dmgPull = std::max(-1.0, std::min(1.0, c.dmgPull + sign(p2.lat) * dmgDelta));
                c.dmgCd = 0.6;
            }
            c.hitFx = std::min(1.0, c.hitFx + vLost * 0.06); // particle/audio hook (index.html:1067)
            // S.shakeT (camera-shake trigger) intentionally not ported -- render only.
        }
    }

    double rel = std::fmod(c.s - track.sFinish() + track.total(), track.total());
    if (rel < track.total() * 0.1 && std::fmod(std::fmod(c.prog, 1.0) + 1.0, 1.0) > 0.85 && !c.done) {
        c.lap++;
        if (c.lap >= 1) {
            c.lastLapT = state.t - c.lapStartT;
            if (c.bestLapT == 0 || c.lastLapT < c.bestLapT) c.bestLapT = c.lastLapT;
        }
        c.lapStartT = state.t;
    }
    c.prog = c.lap + rel / track.total();
    // Phase 4g (PORT_PROGRESS.md): progHist sampling (index.html:1090-1093),
    // for the leaderboard's live-gap calculation (gap_time.h's
    // gapTimeAt()) -- gated on mode=="race" exactly like JS, trimmed to a
    // ~6s trailing window so this never grows unbounded over a long race.
    // Pure display-only bookkeeping appended after this car's physics for
    // the tick is already fully decided; replayHist/histTick remain NOT
    // ported (see car.h's own comment -- an unwired replay-camera spike in
    // the JS original).
    if (state.mode == "race") {
        c.progHist.push_back({state.t, c.prog});
        while (c.progHist.size() > 2 && state.t - c.progHist.front().t > 6.0) c.progHist.pop_front();
    }
}
