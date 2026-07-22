#pragma once

#include <string>
#include <vector>

// Port of the JS track builder (index.html:242-374): two-center-oval
// geometry built from a small per-track spec, with pointAt()/bankAt()/
// project() as the physics-facing query API. Visual-only fields from the
// JS TRACKS entries (theme colors, stadium dressing, sky/env presets) are
// deliberately NOT ported here -- Phase 1 is physics/AI only, and that data
// is lower priority per PORT_PROGRESS.md; it belongs in Phase 5 alongside
// the renderer that actually draws it.

struct Vec2 {
    double x = 0, y = 0;
};

// Physics-relevant fields only, straight from each JS TRACKS[] entry.
struct TrackSpec {
    std::string name;
    double RL, RR;     // corner radii (left end = T1/T2, right end = T3/T4)
    double bankL, bankR; // degrees
    double D;           // center-to-center distance
    double sBank;        // straight banking, degrees
    double ramp;         // banking ramp-in/out distance
};

struct Seg {
    enum Type { Straight, Arc } type;
    // Straight fields
    Vec2 A, B;
    double hdg = 0;
    // Arc fields
    Vec2 C;
    double R = 0, a0 = 0, da = 0;
    int dir = -1;
    // Shared
    double len = 0;
    double bank = 0; // radians
    double s0 = 0;   // cumulative arc-length at segment start
};

struct PointResult {
    double x, y, hdg, curv, bank, R;
};

struct ProjectResult {
    double s, lat, dist, px, py, hdg;
};

class Track {
public:
    explicit Track(const TrackSpec& spec);

    PointResult pointAt(double s) const;
    double bankAt(double s) const;
    ProjectResult project(double x, double y) const;

    double total() const { return total_; }
    double halfW() const { return halfW_; }
    double sFinish() const { return sFinish_; }
    const std::vector<Seg>& segs() const { return segs_; }
    const std::string& name() const { return name_; }

private:
    std::string name_;
    std::vector<Seg> segs_;
    double total_ = 0;
    double halfW_ = 6.0;
    double sFinish_ = 0;
    double sBankRad_ = 0;
    double ramp_ = 0;
};
