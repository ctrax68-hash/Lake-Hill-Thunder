#include "car.h"

#include <algorithm>
#include <cmath>

// cornerCap() (index.html:404-407)
double cornerCap(double mu, double bank) {
    const double t = std::tan(bank);
    return G * (mu + t) / std::max(0.25, 1 - mu * t);
}

// Tire-model upgrade: real per-axle Fz. `dfK*v*v` used to be added straight
// into a dimensionless mu (the old muEff formula in step_car.cpp); here it's
// force-ized as `dfK*v*v*mass*G` so it carries the exact same *relative*
// magnitude versus a car's own static weight that the old formula gave it
// versus mu=1.0, rather than introducing a fresh, untuned aero constant.
AxleLoads axleLoads(const CarConstants& c, double v, double a, double aeroEfficiency) {
    const double fzFrontStatic = c.mass * G * c.weightDistF;
    const double fzRearStatic = c.mass * G * (1 - c.weightDistF);
    const double dFzLong = c.mass * a * c.cgHeight / c.wheelBase;
    const double downforce = c.dfK * v * v * c.mass * G * aeroEfficiency;
    const double fzFront = fzFrontStatic - dFzLong + downforce * c.aeroBalanceF;
    const double fzRear = fzRearStatic + dFzLong + downforce * (1 - c.aeroBalanceF);
    // Floor, not clamp-to-zero: a division by fz elsewhere (friction-ellipse
    // fxFrac) must never see an exact zero.
    return {std::max(100.0, fzFront), std::max(100.0, fzRear)};
}

// Tire-model upgrade: bicycle-model slip angles (small-angle, standard
// single-track-model decomposition). `v` is the caller's already-floored
// forward speed (this file's existing `vSafe` convention).
SlipAngles slipAngles(const CarConstants& c, double vy, double r, double v, double steerAngle) {
    const double aF = c.wheelBase * (1 - c.weightDistF); // CG -> front axle
    const double aR = c.wheelBase * c.weightDistF;       // CG -> rear axle
    const double alphaFront = steerAngle - std::atan2(vy + aF * r, v);
    const double alphaRear = -std::atan2(vy - aR * r, v);
    return {alphaFront, alphaRear};
}

// Tire-model upgrade: linear cornering-stiffness region, friction-ellipse
// clamped. `fxFrac` in [-1,1] is how much of this axle's longitudinal grip
// is already spent (engine/brake force / muFz) -- reduces the lateral
// capacity available, same physical effect as trail-braking or
// power-on-oversteer in a real car.
double axleLateralForce(double stiffness, double slipAngle, double mu, double fz, double fxFrac) {
    const double fyMax = mu * fz * std::sqrt(std::max(0.0, 1.0 - fxFrac * fxFrac));
    const double fyWanted = -stiffness * slipAngle;
    return std::max(-fyMax, std::min(fyMax, fyWanted));
}

// Regression-pass fix (see CarConstants::yawSubsteps): subdivides `dt` into
// `substeps` equal inner steps, recomputing slipAngles()/axleLateralForce()
// every substep from the just-updated vy/r (semi-implicit: vyDot uses the
// substep's just-updated r, not the pre-substep value, matching the
// stability improvement already used before substepping existed).
YawIntegrationResult integrateYawDynamics(const CarConstants& c, double vy0, double r0, double vDyn,
                                           double vSafe, double steerAngle, const AxleLoads& fz, double muEff,
                                           double fxFracFront, double fxFracRear, double dt, int substeps) {
    const double dtSub = dt / substeps;
    const double aF = c.wheelBase * (1 - c.weightDistF);
    const double aR = c.wheelBase * c.weightDistF;
    double vy = vy0, r = r0;
    double hdgDelta = 0.0;
    double slipMagSum = 0.0;
    bool pastLimitAny = false;

    for (int i = 0; i < substeps; ++i) {
        const SlipAngles alpha = slipAngles(c, vy, r, vDyn, steerAngle);
        const double fyFront = axleLateralForce(c.cf, alpha.front, muEff, fz.front, fxFracFront);
        const double fyRear = axleLateralForce(c.cr, alpha.rear, muEff, fz.rear, fxFracRear);

        const double rDot = (aF * fyFront * std::cos(steerAngle) - aR * fyRear) / c.iz;
        r += rDot * dtSub;
        const double vyDot = (fyFront * std::cos(steerAngle) + fyRear) / c.mass - vSafe * r;
        vy += vyDot * dtSub;

        hdgDelta += r * dtSub;
        slipMagSum += std::abs(alpha.front) + std::abs(alpha.rear);

        const double fyMaxFront = muEff * fz.front * std::sqrt(std::max(0.0, 1.0 - fxFracFront * fxFracFront));
        const double fyMaxRear = muEff * fz.rear * std::sqrt(std::max(0.0, 1.0 - fxFracRear * fxFracRear));
        if (std::abs(fyFront) >= fyMaxFront * 0.999 || std::abs(fyRear) >= fyMaxRear * 0.999) pastLimitAny = true;
    }

    return {vy, r, hdgDelta, slipMagSum / substeps, pastLimitAny};
}

// Drivetrain upgrade: piecewise-linear torque-curve shape within one gear's
// rpm band. Plateaus at 1.0 across most of the band on purpose (see this
// function's declaration comment in car.h) -- only the near-edge regions
// taper down.
double torqueCurveMultiplier(const GearRpm& g) {
    constexpr double kLowRpm = 0.25, kLoPlateau = 0.45, kHiPlateau = 0.90, kHighRpm = 1.0;
    constexpr double kLowMult = 0.90, kPlateauMult = 1.0, kHighMult = 0.95;
    if (g.rpm <= kLoPlateau) {
        const double t = std::max(0.0, std::min(1.0, (g.rpm - kLowRpm) / (kLoPlateau - kLowRpm)));
        return kLowMult + (kPlateauMult - kLowMult) * t;
    }
    if (g.rpm >= kHiPlateau) {
        const double t = std::max(0.0, std::min(1.0, (g.rpm - kHiPlateau) / (kHighRpm - kHiPlateau)));
        return kPlateauMult + (kHighMult - kPlateauMult) * t;
    }
    return kPlateauMult;
}

// Suspension upgrade: exponential-smoothing lag toward `target` at rate
// `rate` (1/s) over `dt` seconds.
double suspensionLag(double current, double target, double rate, double dt) {
    const double alpha = 1.0 - std::exp(-rate * dt);
    return current + (target - current) * alpha;
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
