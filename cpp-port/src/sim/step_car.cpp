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
        steerIn = yawCorrected((c.v * cFo) / std::max(0.05, c.v * 0.24) + dHo * 1.4, std::max(0.05, c.v * 0.24));
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
                c.fuel = 1;
                c.dmg = std::max(0.0, c.dmg - 0.5);
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
        steerIn = yawCorrected((c.v * cFF) / std::max(0.05, c.v * 0.24) + dHp * 1.5, std::max(0.05, c.v * 0.24));
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
        steerIn = yawCorrected((c.v * cFFy) / std::max(0.05, c.v * 0.24) + dHy * 1.3, std::max(0.05, c.v * 0.24));
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
        const Car* ahead = c.gridAhead >= 0 ? &allCars[c.gridAhead] : nullptr;
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
        steerIn = yawCorrected((c.v * curvFF) / std::max(0.05, c.v * 0.24) + dHdg * 1.3, std::max(0.05, c.v * 0.24));
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
        thr = input.gas ? 1 : 0;
        brk = input.brake ? 1 : 0;
        steerIn = (input.left ? -1 : 0) + (input.right ? 1 : 0);
        if (state.tilt) steerIn = std::max(-1.0, std::min(1.0, state.tiltG / 22));
        steerIn *= 1 - 0.10 * std::min(1.0, c.v / 85);
        steerIn += c.dmg * 0.02;
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

        const bool closing = blocker && bd < 8 && c.v > blocker->v - 0.5;
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

    // ---- shared physics tail (index.html:977-1109), applies to every branch ----
    c.thr += (thr - c.thr) * 0.28;
    c.brk += (brk - c.brk) * 0.4;
    c.steer += (steerIn - c.steer) * (c.isPlayer ? 0.22 : 0.5);

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

    c.fuel = std::max(0.0, c.fuel - c.thr * c.v * DT * 5e-5);
    const double dragMod = (1 - 0.25 * c.draftF) * (c.dirty ? 1.10 : 1) * (1 + 0.5 * c.dmg);
    const double drag = 0.5 * RHO * CAR.cdA * c.v * c.v * dragMod;
    const double roll = CAR.roll + (onGrass ? 900 : 0);
    const double engFRaw = c.thr * std::min(CAR.maxForce, CAR.power / std::max(4.0, c.v)) *
                            (onGrass ? 0.75 : 1) * (1 - 0.3 * c.dmg) * (c.fuel > 0 ? 1 : 0.25);
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
    const AxleLoads fz = axleLoads(CAR, c.v, c.aPrev, aeroEfficiency);
    const double engF = std::min(engFRaw, muEff * fz.rear);

    double a = (engF - drag - roll * sign(c.v != 0 ? c.v : 1) - brkF * (c.v > 0 ? 1 : 0)) / CAR.mass;
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
        c.fzFront = CAR.mass * G * CAR.weightDistF;
        c.fzRear = CAR.mass * G * (1 - CAR.weightDistF);
        c.slipRatio = 0;
    } else {
        c.fzFront = fz.front;
        c.fzRear = fz.rear;

        const double steerAngle = c.steer * CAR.maxSteerAngle;
        const SlipAngles alpha = slipAngles(CAR, c.vy, c.r, vDyn, steerAngle);

        // Longitudinal-grip fraction already spent at each axle (RWD engine
        // force + brake-bias split between the axles) -- feeds the friction
        // ellipse below, so less lateral grip is available under hard
        // acceleration or heavy braking (trail-braking, power-oversteer).
        const double fxRear = engF - brkF * (1 - CAR.brakeBiasFront);
        const double fxFront = -brkF * CAR.brakeBiasFront;
        const double fxFracRear = std::max(-1.0, std::min(1.0, fxRear / (muEff * fz.rear)));
        const double fxFracFront = std::max(-1.0, std::min(1.0, fxFront / (muEff * fz.front)));
        c.slipRatio = fxFracRear;

        const double fyFront = axleLateralForce(CAR.cf, alpha.front, muEff, fz.front, fxFracFront);
        const double fyRear = axleLateralForce(CAR.cr, alpha.rear, muEff, fz.rear, fxFracRear);

        const double aF = CAR.wheelBase * (1 - CAR.weightDistF);
        const double aR = CAR.wheelBase * CAR.weightDistF;
        const double rDot = (aF * fyFront * std::cos(steerAngle) - aR * fyRear) / CAR.iz;
        c.r += rDot * DT;
        // Semi-implicit (symplectic) Euler: vyDot uses the just-updated `r`,
        // not the pre-tick value -- a standard, cheap stability improvement
        // for a coupled vy/r oscillator under a fixed explicit timestep
        // (confirmed necessary via the regression pass: a lower CAR.iz meant
        // to make yaw response snappier caused vy to diverge numerically,
        // e.g. climbing past 1000 m/s, under plain forward Euler here).
        const double vyDot = (fyFront * std::cos(steerAngle) + fyRear) / CAR.mass - vSafe * c.r;
        c.vy += vyDot * DT;

        // Mirrors the old model's two wear contributions: a steady rate
        // proportional to slip while cornering (equivalent to the old
        // |yaw|*v term), plus an extra flat bump once an axle's demanded
        // force actually got friction-ellipse-clamped -- the equivalent of
        // the old model's `demand > cap` "past the grip limit" case.
        const double slipMag = std::abs(alpha.front) + std::abs(alpha.rear);
        c.wear = std::min(1.0, c.wear + slipMag * c.v * 0.0000004);
        const double fyMaxFront = muEff * fz.front * std::sqrt(std::max(0.0, 1.0 - fxFracFront * fxFracFront));
        const double fyMaxRear = muEff * fz.rear * std::sqrt(std::max(0.0, 1.0 - fxFracRear * fxFracRear));
        const bool pastLimit = std::abs(fyFront) >= fyMaxFront * 0.999 || std::abs(fyRear) >= fyMaxRear * 0.999;
        if (pastLimit) c.wear = std::min(1.0, c.wear + 0.00012);

        c.hdg += c.r * DT;
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
                c.dmg = std::min(1.0, c.dmg + std::min(0.12, vLost * 0.005));
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
