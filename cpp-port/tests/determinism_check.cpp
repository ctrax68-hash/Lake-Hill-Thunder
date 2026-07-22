// Manual verification tool (not wired into ctest -- it needs an externally
// generated JS ground-truth trace file, which requires Playwright/Node and
// isn't committed to the repo, see PORT_PROGRESS.md's Phase 1e notes).
//
// (Renamed from determinism_pace_check.cpp once it grew to cover race-mode
// ticks too, not just the pace phase.)
//
// Usage:
//   ./build/determinism_check <js_trace_file> <track_index> <num_ticks>
//
// Runs the ported gridStart()/tick() for <num_ticks> ticks on the given
// track (must match what the JS trace was generated with), driving the
// player with the same synthetic brain dump_js_trace.js uses, builds an
// in-memory trace of the same shape dump_js_trace.js writes, and diffs it
// against the JS trace via diffTraces(). <num_ticks> must be small enough
// that the whole run stays within branches that are actually ported (check
// the JS trace's own MODE/FLAG columns and PORT_PROGRESS.md's Phase 1f
// checklist for what's done) -- an unported stepCar() branch throws
// std::logic_error rather than silently running wrong physics.

#include "determinism/trace.h"
#include "test_driver_brain.h"

#include "../src/sim/car.h"
#include "../src/sim/race.h"
#include "../src/sim/rng.h"
#include "../src/sim/tracks_data.h"

#include <cstdio>
#include <cstdlib>

namespace {

TickSnapshot buildSnapshot(int tickIdx, const RaceState& state, const PaceCar& pace,
                           const std::vector<Car>& cars) {
    TickSnapshot ts;
    ts.tick = tickIdx;
    ts.t = state.t;
    ts.mode = state.mode;
    ts.flag = state.flag;
    ts.greenLockT = state.greenLockT;
    ts.sinceGreenT = state.sinceGreenT;
    ts.paceS = pace.s;
    ts.paceLat = pace.lat;
    ts.paceV = pace.v;
    ts.paceState = pace.state;
    for (auto& c : cars) {
        CarSnapshot cs;
        cs.idx = c.idx;
        cs.isPlayer = c.isPlayer;
        cs.x = c.x;
        cs.y = c.y;
        cs.hdg = c.hdg;
        cs.v = c.v;
        cs.s = c.s;
        cs.lat = c.lat;
        cs.lap = c.lap;
        cs.prog = c.prog;
        cs.done = c.done;
        cs.finishT = c.finishT;
        cs.wear = c.wear;
        cs.draftF = c.draftF;
        cs.dirty = c.dirty;
        cs.skill = c.skill;
        cs.aggr = c.aggr;
        cs.grooveBias = c.grooveBias;
        cs.passSide = c.passSide;
        cs.passT = c.passT;
        cs.spinT = c.spinT;
        cs.spinDir = c.spinDir;
        cs.spinCd = c.spinCd;
        cs.pit = c.pit;
        cs.pitT = c.pitT;
        cs.pitReq = c.pitReq;
        cs.fuel = c.fuel;
        cs.dmg = c.dmg;
        cs.out = c.out;
        cs.cautionSlot = c.cautionSlot;
        ts.cars.push_back(cs);
    }
    return ts;
}

} // namespace

int main(int argc, char** argv) {
    if (argc < 4) {
        std::fprintf(stderr, "usage: %s <js_trace_file> <track_index> <num_ticks>\n", argv[0]);
        return 2;
    }
    const std::string jsTracePath = argv[1];
    const int trackIdx = std::atoi(argv[2]);
    const int numTicks = std::atoi(argv[3]);

    Track track(TRACKS[trackIdx]);
    Mulberry32 rng(12345);
    Mulberry32 rngR(999);

    RaceState state;
    PaceCar pace;
    std::vector<Car> cars;
    gridStart(track, rng, state, pace, cars, nullptr);
    state.mode = "pace";
    state.tilt = true; // matches dump_js_trace.js's `S.tilt = true;`

    PlayerInput input;

    std::vector<TickSnapshot> ours;
    ours.reserve(numTicks);
    for (int i = 0; i < numTicks; ++i) {
        const Car* player = nullptr;
        for (auto& c : cars) {
            if (c.isPlayer) {
                player = &c;
                break;
            }
        }
        driverBrain(*player, track, state, input);
        ::tick(state, cars, pace, track, rngR, input);
        ours.push_back(buildSnapshot(i, state, pace, cars));
        if (const char* dbgIdx = std::getenv("DEBUG_CAR_IDX")) {
            int wantIdx = std::atoi(dbgIdx);
            for (auto& c : cars) {
                if (c.idx == wantIdx) {
                    std::fprintf(stderr, "%d %.17g %.17g %.17g %.17g %.17g\n", i, c.x, c.y, c.hdg, c.v, c.lat);
                }
            }
        }
    }

    std::vector<TickSnapshot> theirs;
    try {
        theirs = loadTrace(jsTracePath);
    } catch (const std::exception& e) {
        std::fprintf(stderr, "failed to load JS trace: %s\n", e.what());
        return 2;
    }
    if (static_cast<int>(theirs.size()) > numTicks) {
        theirs.resize(numTicks);
    }

    auto diffs = diffTraces(theirs, ours);
    if (diffs.empty()) {
        std::printf("determinism_check: MATCH -- %d ticks identical to JS.\n", numTicks);
        return 0;
    }
    std::fprintf(stderr, "determinism_check: DIVERGED at tick %d:\n", diffs[0].tick);
    for (auto& d : diffs) {
        std::fprintf(stderr, "  car=%d field=%s got=%.9g expected=%.9g\n",
                     d.carIdx, d.field.c_str(), d.got, d.expected);
    }
    return 1;
}
