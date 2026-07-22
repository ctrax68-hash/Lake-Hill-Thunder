#include "car.h"

#include <algorithm>
#include <cmath>

// cornerCap() (index.html:404-407)
double cornerCap(double mu, double bank) {
    const double t = std::tan(bank);
    return G * (mu + t) / std::max(0.25, 1 - mu * t);
}

// ROSTER (index.html:431-451)
const std::array<RosterEntry, 19> ROSTER{{
    {"B. HOLLISTER", 28, CarPalette::Red, {0, 0, 0, CarPalette::White}},
    {"D. MCCREADY", 7, CarPalette::Blue, {1, 1, 1, CarPalette::Yellow}},
    {"R. LAFONTAINE", 44, CarPalette::Green, {2, 0, 2, CarPalette::White}},
    {"T. VANCE", 3, CarPalette::Black, {1, 2, 3, CarPalette::Orange}},
    {"C. DELGADO", 91, CarPalette::Orange, {0, 1, 4, CarPalette::Black}},
    {"W. STROUD", 15, CarPalette::Purple, {1, 0, 5, CarPalette::White}},
    {"E. KOWALSKI", 62, CarPalette::Blue, {3, 2, 4, CarPalette::White}},
    {"J. WHITFIELD", 5, CarPalette::White, {2, 1, 6, CarPalette::Blue}},
    {"M. OKONKWO", 12, CarPalette::Purple, {4, 2, 2, CarPalette::Yellow}},
    {"A. CERVANTES", 18, CarPalette::Orange, {2, 2, 9, CarPalette::White}},
    {"P. LINDQVIST", 24, CarPalette::Blue, {4, 0, 8, CarPalette::Black}},
    {"G. THIBODEAUX", 31, CarPalette::Green, {0, 2, 6, CarPalette::Black}},
    {"H. NAKAGAWA", 37, CarPalette::Red, {3, 2, 7, CarPalette::Black}},
    {"S. MCALLISTER", 48, CarPalette::Silver, {0, 2, 7, CarPalette::Red}},
    {"D. FONTAINE", 53, CarPalette::Yellow, {1, 2, 8, CarPalette::Black}},
    {"K. BRENNAN", 66, CarPalette::Black, {3, 0, 5, CarPalette::Silver}},
    {"V. ROSSI", 73, CarPalette::Yellow, {4, 0, 3, CarPalette::Black}},
    {"L. HARGROVE", 82, CarPalette::Green, {4, 1, 9, CarPalette::Yellow}},
    {"C. BJORNSTAD", 88, CarPalette::Silver, {3, 1, 1, CarPalette::Blue}},
}};

// makeCar() (index.html:453-503). Field-by-field, matching the JS object
// literal's own order so the 3-call rng() sequence for AI cars (skill, aggr,
// grooveBias) lands in exactly the same order it does in JS.
Car makeCar(bool isPlayer, int idx, const Track& track, Mulberry32& rng) {
    Car c;
    PointResult p = track.pointAt(0);

    c.isPlayer = isPlayer;
    c.idx = idx;
    c.name = isPlayer ? "YOU" : ROSTER[idx - 1].name;
    c.num = isPlayer ? 21 : ROSTER[idx - 1].num;
    c.scheme = isPlayer ? nullptr : &ROSTER[idx - 1].scheme;
    c.col = isPlayer ? Color3{1.0, 0.82, 0.24} : ROSTER[idx - 1].col;

    c.x = p.x;
    c.y = p.y;
    c.hdg = p.hdg;
    c.vdir = p.hdg;
    c.v = 0;

    c.s = 0;
    c.lat = 0;
    c.lap = -1;
    c.prog = 0;
    c.done = false;
    c.finishT = 0;

    c.steer = 0;
    c.thr = 0;
    c.brk = 0;

    c.wear = 0;
    c.draftF = 0;
    c.dirty = false;

    c.skill = isPlayer ? 1.0 : 0.90 + rng.next() * 0.085;
    c.aggr = isPlayer ? 1.0 : 0.4 + rng.next() * 0.6;

    // Personal preferred racing groove (see the JS source's own long comment
    // at index.html:467-490 for why 3 tiers spaced 5 apart, +-0.3 jitter --
    // preserved verbatim there; not re-derived here).
    if (isPlayer) {
        c.grooveBias = 0;
    } else {
        static const double tiers[3] = {-4, 1, 6};
        c.grooveBias = tiers[idx % 3] + (rng.next() * 2 - 1) * 0.3;
    }

    c.passSide = 0;
    c.passT = 0;

    c.lapStartT = 0;
    c.lastLapT = 0;
    c.bestLapT = 0;

    c.pitch = 0;

    c.spinT = 0;
    c.spinDir = 1;
    c.spinCd = 0;

    c.pit = 0;
    c.pitT = 0;
    c.pitReq = false;

    c.fuel = 1;
    c.dmg = 0;
    c.out = false;

    c.cautionSlot = -1;
    c.gridSlot = -1;
    c.gridLane = 0;

    return c;
}

// cornerSpeed() (index.html:629-636)
double cornerSpeed(const Track& track, double R, double s, double wear) {
    double v = 55;
    for (int it = 0; it < 4; ++it) {
        const double mu = CAR.mu * (1 - 0.12 * wear) + CAR.dfK * v * v;
        v = std::sqrt(cornerCap(mu, track.bankAt(s)) * R);
    }
    return v;
}

// targetSpeed() (index.html:637-649)
double targetSpeed(const Track& track, const Car& car) {
    double vT = 95;
    const double look = std::max(30.0, car.v * 2.4);
    for (double d = 0; d <= look; d += 12) {
        PointResult p = track.pointAt(car.s + d);
        if (std::abs(p.curv) > 1e-6) {
            double vc = cornerSpeed(track, 1 / std::abs(p.curv), car.s + d, car.wear) *
                        (0.90 + 0.07 * car.skill);
            const double vAllowed = std::sqrt(vc * vc + 2 * 6.5 * d);
            vT = std::min(vT, vAllowed);
        }
    }
    return vT;
}

// pitStallS() (index.html:682-685)
double pitStallS(const Track& track, int idx) {
    const Seg& seg0 = track.segs()[0];
    return seg0.s0 + seg0.len * (0.18 + 0.55 * idx / static_cast<double>(FIELD));
}
