// Verifies results.{h,cpp}'s pure logic (computeResultsRegions()'s row-
// position math, buildResultsOrder()'s 3-bucket ordering) -- no SDL2/bgfx
// window dependency, same rationale as menu_test.cpp: actual synthetic
// click delivery can't be reliably tested headlessly in this container, so
// this only exercises the region-computation/ordering math that
// handleResultsClick() (main.cpp) and drawResults() (results.cpp) call into.

#include "../src/ui/results.h"

#include <cstdio>

namespace {

bool ok = true;

void check(bool cond, const char* what) {
    if (!cond) {
        std::fprintf(stderr, "results_test: FAILED -- %s\n", what);
        ok = false;
    }
}

Car makeCar(const char* name, int num, bool done, bool out) {
    Car c;
    c.name = name;
    c.num = num;
    c.done = done;
    c.out = out;
    return c;
}

} // namespace

int main() {
    // computeResultsRegions(): the back button sits directly below the
    // ranked list, one blank row below the last car row.
    {
        const ResultsRegions r0 = computeResultsRegions(0);
        const ResultsRegions r5 = computeResultsRegions(5);
        check(r0.backBtn.w > 0 && r0.backBtn.h > 0, "backBtn has positive size");
        check(r5.backBtn.y > r0.backBtn.y, "more rows should push the back button further down");
        check(r5.backBtn.x == r0.backBtn.x, "back button column shouldn't depend on row count");
    }

    // buildResultsOrder(): index.html:4119-4121's exact 3-bucket order --
    // finishOrder (crossing order) first, then still-racing (!done &&
    // !out) in `order`'s own traversal order, then DNFs (!done && out)
    // last, also in `order`'s own traversal order.
    {
        Car winner = makeCar("A B", 1, true, false);
        Car second = makeCar("C D", 2, true, false);
        Car racing1 = makeCar("E F", 3, false, false);
        Car racing2 = makeCar("G H", 4, false, false);
        Car dnf1 = makeCar("I J", 5, false, true);
        Car dnf2 = makeCar("K L", 6, false, true);

        // finishOrder mirrors crossing order: winner first, second next.
        std::vector<Car*> finishOrder = {&winner, &second};
        // `order` is race-position-sorted -- deliberately NOT the same
        // order as finishOrder/dnf declaration above, to prove
        // buildResultsOrder() doesn't just echo insertion order.
        std::vector<const Car*> order = {&winner, &second, &racing2, &racing1, &dnf2, &dnf1};

        const std::vector<const Car*> result = buildResultsOrder(finishOrder, order);
        check(result.size() == 6, "all 6 cars should appear exactly once");
        check(result[0] == &winner, "finisher 1 (winner) should be first");
        check(result[1] == &second, "finisher 2 should be second");
        check(result[2] == &racing2, "still-racing cars follow finishers, in `order`'s traversal order");
        check(result[3] == &racing1, "still-racing cars follow finishers, in `order`'s traversal order");
        check(result[4] == &dnf2, "DNFs come last, in `order`'s traversal order");
        check(result[5] == &dnf1, "DNFs come last, in `order`'s traversal order");
    }

    // A car that's `done` even while also carrying `out` is never
    // reclassified as a DNF here -- it simply isn't re-added by either
    // filter pass (it's already in finishOrder), matching JS's own
    // comment that c.done wins even if c.out is also set.
    {
        Car doneButOut = makeCar("M N", 7, true, true);
        std::vector<Car*> finishOrder = {&doneButOut};
        std::vector<const Car*> order = {&doneButOut};
        const std::vector<const Car*> result = buildResultsOrder(finishOrder, order);
        check(result.size() == 1, "a done-but-out car should appear exactly once, not duplicated as a DNF");
        check(result[0] == &doneButOut, "the single entry should be the finisher itself");
    }

    // Empty inputs are a safe no-op.
    {
        std::vector<Car*> finishOrder;
        std::vector<const Car*> order;
        const std::vector<const Car*> result = buildResultsOrder(finishOrder, order);
        check(result.empty(), "empty finishOrder/order should produce an empty results list");
    }

    if (ok) {
        std::printf("results_test: region math and 3-bucket ordering match expectations.\n");
        return 0;
    }
    return 1;
}
