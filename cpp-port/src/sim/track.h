#pragma once

#include <array>
#include <string>
#include <vector>

// Port of the JS track builder (index.html:242-374): two-center-oval
// geometry built from a small per-track spec, with pointAt()/bankAt()/
// project() as the physics-facing query API. Phase 1 ported only the
// physics-relevant fields; Phase 5b (PORT_PROGRESS.md) added the
// visual-only Theme/Stadium data below (colors, stadium dressing,
// sky/env preset name), straight from each JS TRACKS[] entry
// (index.html:242-283) -- never read by sim/AI code, only by the renderer.

struct Vec2 {
    double x = 0, y = 0;
};

// theme (index.html e.g. `theme:{wall:[1,.267,0],grass:[.176,.314,.086]}`).
// Named TrackTheme, not Theme, to avoid colliding with render/color.h's
// unrelated `namespace Theme` (that one is the UI chrome palette).
struct TrackTheme {
    std::array<double, 3> wall;
    std::array<double, 3> grass;
};

// stadium.standTier (per-track grandstand row counts by zone).
struct StandTier {
    int front, back, corner;
};

// stadium.standScale (per-track riser depth/height in world units).
struct StandScale {
    double tierD, tierH;
};

// stadium.sky (per-track background gradient + optional silhouette).
struct Sky {
    std::array<double, 3> horizon;
    std::array<double, 3> zenith;
    std::string silhouette; // "none" | "hills"
};

// stadium.env (names an ENV_PRESETS entry -- see render/env_presets.h).
struct Env {
    std::string preset;
};

// stadium (index.html:246-252 etc.) -- all visual/dressing data for one
// track. Only theme.grass + env.preset are consumed as of Phase 5b; the
// rest (stands/crowd/sky/jumbotron/pylon) is ported now as data, matching
// the JS source's own "one config object, one generic code path" layout,
// and gets consumed incrementally by Phase 5d onward.
struct Stadium {
    StandTier standTier;
    double standDensity;
    std::string standReach; // "partial" | "full"
    StandScale standScale;
    double crowdFill;
    int seamEvery;
    int patches;
    bool jumbotron;
    bool pylon;
    double sponsorDensity;
    int crowdTiers = 2; // JS: `st.crowdTiers||2` (index.html:2062)
    Sky sky;
    Env env;
    std::array<std::array<double, 3>, 6> crowdPalette;
};

// Straight from each JS TRACKS[] entry (index.html:242-283): physics fields
// (Phase 1) plus visual dressing (Phase 5b).
struct TrackSpec {
    std::string name;
    double RL, RR;     // corner radii (left end = T1/T2, right end = T3/T4)
    double bankL, bankR; // degrees
    double D;           // center-to-center distance
    double sBank;        // straight banking, degrees
    double ramp;         // banking ramp-in/out distance
    TrackTheme theme;
    Stadium stadium;
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
    const TrackTheme& theme() const { return theme_; }
    const Stadium& stadium() const { return stadium_; }

private:
    std::string name_;
    std::vector<Seg> segs_;
    double total_ = 0;
    double halfW_ = 6.0;
    double sFinish_ = 0;
    double sBankRad_ = 0;
    double ramp_ = 0;
    TrackTheme theme_;
    Stadium stadium_;
};
