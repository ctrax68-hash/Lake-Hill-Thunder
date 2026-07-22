#include "race.h"

#include "constants.h"
#include "step_car.h"

#include <cmath>

namespace {
double wrapMod(double s, double total) {
    return std::fmod(std::fmod(s, total) + total, total);
}
} // namespace

// gridStart() (index.html:564-598)
void gridStart(const Track& track, Mulberry32& rng, RaceState& state, PaceCar& pace,
                std::vector<Car>& cars, const std::vector<int>* gridOrder) {
    cars.clear();

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
void collide(std::vector<Car>& cars, const RaceState& state, const Track& track, Mulberry32& rngR) {
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
                const double dvel = (a.v - b.v) * 0.25;
                const double closeMag = std::min(1.0, std::abs(dvel) / 1.0);
                a.v = std::max(0.0, a.v - std::abs(dvel) * 0.6 - 0.4 * closeMag);
                b.v = std::max(0.0, b.v - 0.3 * closeMag);
                a.hdg += ny * 0.008 * (a.isPlayer ? 1 : 2);
                b.hdg -= ny * 0.008;

                const double cv2 = std::abs(a.v - b.v);
                if (cv2 > 3 && state.flag != "yellow") {
                    Car& hitter = a.v > b.v ? a : b;
                    Car& victim2 = a.v > b.v ? b : a;
                    if (hitter.dmgCd <= 0) {
                        hitter.dmg = std::min(1.0, hitter.dmg + std::min(0.08, cv2 * 0.003));
                        victim2.dmg = std::min(1.0, victim2.dmg + std::min(0.05, cv2 * 0.002));
                        hitter.dmgCd = 0.6;
                        victim2.dmgCd = 0.6;
                        if (cv2 > 18 && state.mode == "race" && state.flag == "green" &&
                            state.greenLockT <= 0 && victim2.spinT <= 0 && victim2.spinCd <= 0 &&
                            victim2.spinRollCd <= 0) {
                            victim2.spinRollCd = 2.6;
                            if (rngR.next() < std::min(0.06, (cv2 - 18) * 0.0025 + 0.0025) * trackWreckScale) {
                                victim2.spinT = 1.6 + rngR.next() * 1.2;
                                victim2.spinDir = ny > 0 ? 1 : -1;
                                victim2.spinCd = 10;
                            }
                        }
                    }
                }
            }
        }
    }
}

// tick() (index.html:4180-4204), pace-phase-relevant subset only -- see
// race.h's comment for exactly what's deliberately not ported yet.
void tick(RaceState& state, std::vector<Car>& cars, PaceCar& pace, const Track& track,
          Mulberry32& rngR) {
    state.t += DT;

    if (state.mode == "pace") {
        stepPace(pace, state, track);
        if (pace.state != "lead") state.paceV = std::min(46.0, state.paceV + 3.4 * DT);
    }

    updateAero(cars, track);
    for (auto& c : cars) stepCar(c, state, track, cars, pace);
    collide(cars, state, track, rngR);

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
}
