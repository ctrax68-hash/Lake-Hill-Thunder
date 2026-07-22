#pragma once

// Reader/comparator for the flat-text trace format dump_js_trace.js writes
// (one "TICK ..." line followed by one "CAR ..." line per car, repeated per
// tick). Used to diff a C++-side simulation run against JS ground truth as
// stepCar()/tick() get ported (Phase 1f+). See PORT_PROGRESS.md's Phase 1e
// notes for the exact field list/order and how to regenerate a real fixture.

#include <string>
#include <vector>

struct CarSnapshot {
    int idx = 0;
    bool isPlayer = false;
    double x = 0, y = 0, hdg = 0, v = 0;
    double s = 0, lat = 0;
    int lap = 0;
    double prog = 0;
    bool done = false;
    double finishT = 0;
    double wear = 0, draftF = 0;
    bool dirty = false;
    double skill = 0, aggr = 0, grooveBias = 0;
    int passSide = 0;
    double passT = 0;
    double spinT = 0;
    int spinDir = 0;
    double spinCd = 0;
    int pit = 0;
    double pitT = 0;
    bool pitReq = false;
    double fuel = 0, dmg = 0;
    bool out = false;
    int cautionSlot = 0;
};

struct TickSnapshot {
    int tick = 0;
    double t = 0;
    std::string mode, flag;
    double greenLockT = 0, sinceGreenT = 0;
    double paceS = 0, paceLat = 0, paceV = 0;
    std::string paceState;
    std::vector<CarSnapshot> cars;
};

// Throws std::runtime_error on a malformed file.
std::vector<TickSnapshot> loadTrace(const std::string& path);

struct FieldDiff {
    int tick;
    int carIdx; // -1 for a tick-level (non-car) field
    std::string field;
    double got, expected;
};

// Compares two traces field-by-field within `tol`, stopping at the first
// diverging tick (subsequent ticks after a real divergence are usually just
// cascading noise, not independent bugs -- report where it *starts*).
// Returns an empty vector if they match; the trace lengths must be equal for
// this to make sense at all (a length mismatch reports one synthetic
// "trace_length" diff instead of touching indices out of range).
std::vector<FieldDiff> diffTraces(const std::vector<TickSnapshot>& a,
                                   const std::vector<TickSnapshot>& b,
                                   double tol = 1e-6);
