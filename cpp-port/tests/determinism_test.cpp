// Self-test for the determinism-harness plumbing itself (src/../tests/
// determinism/trace.{h,cpp}): parsing and diffing, independent of whether
// any C++ sim code exists yet to actually compare against. Phase 1f will
// point diffTraces() at real ported stepCar()/tick() output; this just
// proves the loader/comparator are correct first, against a tiny
// hand-written fixture (tests/fixtures/trace_synthetic.txt, 2 ticks x 2 cars).

#include "determinism/trace.h"

#include <cstdio>
#include <cstdlib>

namespace {
int g_failures = 0;

void expect(bool cond, const char* what) {
    if (!cond) {
        std::fprintf(stderr, "FAIL: %s\n", what);
        ++g_failures;
    }
}
} // namespace

int main() {
    auto trace = loadTrace("tests/fixtures/trace_synthetic.txt");

    expect(trace.size() == 2, "expected 2 ticks");
    expect(trace[0].tick == 0 && trace[1].tick == 1, "tick indices");
    expect(trace[0].mode == "pace" && trace[0].flag == "green", "tick 0 mode/flag");
    expect(trace[0].paceState == "lead", "tick 0 paceState");
    expect(trace[0].cars.size() == 2, "expected 2 cars per tick");

    const CarSnapshot* c1 = nullptr;
    const CarSnapshot* c0 = nullptr;
    for (auto& c : trace[0].cars) {
        if (c.idx == 1) c1 = &c;
        if (c.idx == 0) c0 = &c;
    }
    expect(c1 != nullptr && c0 != nullptr, "found both car idx 0 and 1");
    if (c1) {
        expect(!c1->isPlayer, "car 1 is not the player");
        expect(c1->x == 10.0 && c1->y == 20.0, "car 1 position");
        expect(c1->lap == -1, "car 1 lap");
        expect(c1->skill == 0.98, "car 1 skill");
    }
    if (c0) {
        expect(c0->isPlayer, "car 0 is the player");
        expect(c0->skill == 1.0 && c0->aggr == 1.0, "player skill/aggr");
    }

    // Identical trace loaded twice must diff clean.
    auto traceAgain = loadTrace("tests/fixtures/trace_synthetic.txt");
    auto noDiff = diffTraces(trace, traceAgain);
    expect(noDiff.empty(), "identical traces should have no diff");

    // Inject a divergence at tick 1, car idx 1's x, and confirm diffTraces()
    // finds exactly that -- proves the comparator actually looks at the
    // data, not just trace length.
    auto mutated = traceAgain;
    for (auto& c : mutated[1].cars) {
        if (c.idx == 1) c.x += 5.0;
    }
    auto diffs = diffTraces(trace, mutated);
    bool foundInjected = false;
    for (auto& d : diffs) {
        if (d.tick == 1 && d.carIdx == 1 && d.field == "x") foundInjected = true;
    }
    expect(foundInjected, "diffTraces() found the injected tick=1 car=1 x divergence");
    expect(!diffs.empty(), "diffTraces() reported at least one diff for the mutated trace");

    // A length mismatch should report a single synthetic diff, not crash.
    std::vector<TickSnapshot> shorter(trace.begin(), trace.begin() + 1);
    auto lenDiff = diffTraces(trace, shorter);
    expect(lenDiff.size() == 1 && lenDiff[0].field == "trace_length", "length-mismatch diff");

    if (g_failures == 0) {
        std::printf("determinism_test: trace loader + comparator both verified correct.\n");
        return 0;
    }
    std::fprintf(stderr, "determinism_test: %d FAILURES.\n", g_failures);
    return 1;
}
