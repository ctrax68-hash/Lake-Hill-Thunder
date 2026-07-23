#include "track.h"

#include <algorithm>
#include <cmath>
#include <limits>

namespace {
constexpr double PI = 3.14159265358979323846;

double ang(const Vec2& c, const Vec2& p) {
    return std::atan2(p.y - c.y, p.x - c.x);
}

// cwDelta (index.html:296): sweep from a0 to a1 clockwise, always positive.
double cwDelta(double a0, double a1) {
    double d = a0 - a1;
    while (d <= 0) d += PI * 2;
    return d;
}
} // namespace

Track::Track(const TrackSpec& spec) : name_(spec.name), theme_(spec.theme), stadium_(spec.stadium) {
    const double RL = spec.RL, bankL = spec.bankL * PI / 180;
    const double RR = spec.RR, bankR = spec.bankR * PI / 180;
    const double D = spec.D;

    Vec2 C1{D / 2, 0}, C2{-D / 2, 0};
    const double nx = -(RR - RL) / D;
    const double nyU = std::sqrt(1 - nx * nx);
    Vec2 nU{nx, nyU}, nL{nx, -nyU};
    Vec2 P1U{C1.x + RR * nU.x, C1.y + RR * nU.y};
    Vec2 P2U{C2.x + RL * nU.x, C2.y + RL * nU.y};
    Vec2 P1L{C1.x + RR * nL.x, C1.y + RR * nL.y};
    Vec2 P2L{C2.x + RL * nL.x, C2.y + RL * nL.y};

    const double a2s = ang(C2, P2L), a2d = cwDelta(a2s, ang(C2, P2U));
    const double a1s = ang(C1, P1U), a1d = cwDelta(a1s, ang(C1, P1L));

    auto addStraight = [&](Vec2 A, Vec2 B, double bank) {
        Seg s;
        s.type = Seg::Straight;
        s.A = A;
        s.B = B;
        s.len = std::hypot(B.x - A.x, B.y - A.y);
        s.hdg = std::atan2(B.y - A.y, B.x - A.x);
        s.bank = bank;
        segs_.push_back(s);
    };
    auto addArc = [&](Vec2 C, double R, double a0, double da, double bank) {
        Seg s;
        s.type = Seg::Arc;
        s.C = C;
        s.R = R;
        s.a0 = a0;
        s.da = da;
        s.dir = -1;
        s.len = R * da;
        s.bank = bank;
        segs_.push_back(s);
    };

    addStraight(P1L, P2L, 0);
    addArc(C2, RL, a2s, a2d, bankL);
    addStraight(P2U, P1U, 0);
    addArc(C1, RR, a1s, a1d, bankR);

    total_ = 0;
    for (auto& s : segs_) {
        s.s0 = total_;
        total_ += s.len;
    }
    halfW_ = 6.0;
    sFinish_ = segs_[0].len * 0.5;
    sBankRad_ = spec.sBank * PI / 180;
    ramp_ = spec.ramp;
}

PointResult Track::pointAt(double s) const {
    s = std::fmod(std::fmod(s, total_) + total_, total_);
    const Seg* seg = &segs_.back();
    for (auto& g : segs_) {
        if (s < g.s0 + g.len) {
            seg = &g;
            break;
        }
    }
    const double u = s - seg->s0;
    if (seg->type == Seg::Straight) {
        const double t = u / seg->len;
        return {seg->A.x + (seg->B.x - seg->A.x) * t,
                seg->A.y + (seg->B.y - seg->A.y) * t,
                seg->hdg, 0.0, 0.0, std::numeric_limits<double>::infinity()};
    } else {
        const double a = seg->a0 + seg->dir * u / seg->R;
        return {seg->C.x + seg->R * std::cos(a),
                seg->C.y + seg->R * std::sin(a),
                a + seg->dir * PI / 2,
                seg->dir / seg->R,
                seg->bank,
                seg->R};
    }
}

double Track::bankAt(double s) const {
    s = std::fmod(std::fmod(s, total_) + total_, total_);
    double b = sBankRad_;
    for (auto& g : segs_) {
        if (g.type != Seg::Arc) continue;
        double d = s - g.s0;
        if (d < -total_ / 2) d += total_;
        if (d > total_ / 2) d -= total_;
        double f = 0;
        if (d >= 0 && d <= g.len) {
            f = 1;
        } else if (d > -ramp_ && d < 0) {
            f = 1 + d / ramp_;
        } else {
            const double e = d - g.len;
            if (e > 0 && e < ramp_) f = 1 - e / ramp_;
        }
        if (f > 0) {
            const double sm = 0.5 - 0.5 * std::cos(f * PI);
            b = std::max(b, sBankRad_ + (g.bank - sBankRad_) * sm);
        }
    }
    return b;
}

ProjectResult Track::project(double x, double y) const {
    bool haveBest = false;
    ProjectResult best{};
    for (auto& g : segs_) {
        double s, px, py, hdg;
        if (g.type == Seg::Straight) {
            const double ux = (g.B.x - g.A.x) / g.len, uy = (g.B.y - g.A.y) / g.len;
            double t = (x - g.A.x) * ux + (y - g.A.y) * uy;
            t = std::max(0.0, std::min(g.len, t));
            px = g.A.x + ux * t;
            py = g.A.y + uy * t;
            s = g.s0 + t;
            hdg = std::atan2(uy, ux);
        } else {
            double a = std::atan2(y - g.C.y, x - g.C.x);
            double da = g.dir * (a - g.a0);
            while (da < 0) da += PI * 2;
            while (da > PI * 2) da -= PI * 2;
            da = std::max(0.0, std::min(g.da, da));
            const double aa = g.a0 + g.dir * da;
            px = g.C.x + g.R * std::cos(aa);
            py = g.C.y + g.R * std::sin(aa);
            s = g.s0 + g.R * da;
            hdg = aa + g.dir * PI / 2;
        }
        const double dist = std::hypot(x - px, y - py);
        if (!haveBest || dist < best.dist) {
            const double ux2 = std::cos(hdg), uy2 = std::sin(hdg);
            const double lat = (x - px) * (-uy2) + (y - py) * (ux2);
            best = {s, lat, dist, px, py, hdg};
            haveBest = true;
        }
    }
    return best;
}
