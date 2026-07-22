#include "step_car.h"

#include "constants.h"

#include <algorithm>
#include <cmath>
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
             const PaceCar& pace) {
    double thr = 0, brk = 0, steerIn = 0;

    if (c.spinCd > 0) c.spinCd -= DT;
    if (c.dmgCd > 0) c.dmgCd -= DT;
    if (c.spinRollCd > 0) c.spinRollCd -= DT;

    // Pit-entry arming block (index.html:692-701) intentionally NOT ported
    // yet: it only matters once pitReq/dtPending can actually be set, which
    // happens in tick()'s pit-strategy AI -- gated on S.mode==='race', so it
    // cannot fire during the pace phase this file currently covers. Its
    // condition is provably always-false right now; revisit when porting
    // the pit-road state machine.

    if (false) {
        // S.mode==='victory' && c.isPlayer (index.html:702-707)
        throw std::logic_error("stepCar: victory branch not yet ported");
    } else if (c.out || c.done) {
        // index.html:708-730
        throw std::logic_error("stepCar: out/done branch not yet ported");
    } else if (c.spinT > 0) {
        // index.html:731-736
        throw std::logic_error("stepCar: spin branch not yet ported");
    } else if (c.pit > 0) {
        // index.html:737-773
        throw std::logic_error("stepCar: pit branch not yet ported");
    } else if (state.mode == "race" && state.flag == "yellow") {
        // index.html:774-836
        throw std::logic_error("stepCar: yellow-caution branch not yet ported");
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
        const double lane = c.gridLane;
        const double LA = std::max(12.0, c.v * 0.62);
        PointResult pT = track.pointAt(c.s + LA);
        const double tx = pT.x - std::sin(pT.hdg) * lane, ty = pT.y + std::cos(pT.hdg) * lane;
        double dHdg = wrapPi(std::atan2(ty - c.y, tx - c.x) - c.hdg);
        const double curvFF = track.pointAt(c.s + std::max(6.0, c.v * 0.3)).curv;
        steerIn = std::max(-1.0, std::min(1.0, (c.v * curvFF) / std::max(0.05, c.v * 0.24) + dHdg * 1.3));
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
        throw std::logic_error("stepCar: player-input branch not yet ported");
    } else {
        // index.html:865-975 (AI race branch)
        throw std::logic_error("stepCar: AI race branch not yet ported");
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
    const double muEff = CAR.mu * muSurf + CAR.dfK * c.v * c.v * (c.dirty ? 0.85 : 1) * (1 - 0.35 * c.dmg);

    c.fuel = std::max(0.0, c.fuel - c.thr * c.v * DT * 5e-5);
    const double dragMod = (1 - 0.25 * c.draftF) * (c.dirty ? 1.10 : 1) * (1 + 0.5 * c.dmg);
    const double drag = 0.5 * RHO * CAR.cdA * c.v * c.v * dragMod;
    const double roll = CAR.roll + (onGrass ? 900 : 0);
    const double engF = c.thr * std::min(CAR.maxForce, CAR.power / std::max(4.0, c.v)) *
                         (onGrass ? 0.75 : 1) * (1 - 0.3 * c.dmg) * (c.fuel > 0 ? 1 : 0.25);
    const double brkF = c.brk * CAR.brakeForce * muSurf;
    double a = (engF - drag - roll * sign(c.v != 0 ? c.v : 1) - brkF * (c.v > 0 ? 1 : 0)) / CAR.mass;
    c.v = std::max(0.0, c.v + a * DT);
    c.pitch += ((-a * 0.006) - c.pitch) * 0.12;

    const double cap = cornerCap(muEff, bank);
    const double vSafe = std::max(3.0, c.v);
    const double maxYaw = cap / vSafe;
    const double kinYaw = c.v * 0.24;
    const double wantYaw = c.steer * std::min({1.3, kinYaw, maxYaw * 1.06});
    double yaw = wantYaw;
    const double demand = std::abs(wantYaw) * vSafe;
    if (demand > cap) {
        yaw = sign(wantYaw) * maxYaw;
        c.v = std::max(0.0, c.v - (demand - cap) * 0.16 * DT);
        c.wear = std::min(1.0, c.wear + 0.00012);
        // c.slipFx (tire-smoke render hook) intentionally not ported -- cosmetic only.
    }
    c.wear = std::min(1.0, c.wear + std::abs(yaw) * c.v * 0.0000004);
    c.hdg += yaw * DT;
    double dv = wrapPi(c.hdg - c.vdir);
    c.vdir += dv * std::min(1.0, (6 + 10 * muSurf) * DT);
    c.x += std::cos(c.vdir) * c.v * DT;
    c.y += std::sin(c.vdir) * c.v * DT;

    const double wallClampLat = track.halfW() + 5.0; // WALL_LAT(halfW+6) - CAR_HALF_WID(1)
    if (off > wallClampLat) {
        ProjectResult p2 = track.project(c.x, c.y);
        const double nx = -std::sin(p2.hdg), ny = std::cos(p2.hdg);
        const double excess = std::abs(p2.lat) - wallClampLat;
        c.x -= nx * sign(p2.lat) * excess;
        c.y -= ny * sign(p2.lat) * excess;
        const double vLost = c.v * 0.28;
        c.v *= 0.95;
        if (c.spinT <= 0) {
            double dh = wrapPi(c.hdg - p2.hdg);
            c.hdg = p2.hdg + dh * 0.9;
        }
        c.vdir = c.hdg;
        if (c.dmgCd <= 0 && state.flag != "yellow") {
            c.dmg = std::min(1.0, c.dmg + std::min(0.12, vLost * 0.005));
            c.dmgCd = 0.6;
        }
        // c.hitFx (impact particle/audio hook) intentionally not ported -- cosmetic only.
        // S.shakeT (camera-shake trigger) intentionally not ported -- render only.
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
    // progHist/replayHist sampling (index.html:1091-1108) intentionally not
    // ported -- HUD telemetry / replay-camera bookkeeping only, see car.h.
}
