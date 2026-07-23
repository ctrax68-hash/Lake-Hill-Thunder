// Verifies minimap.{h,cpp}'s drawMinimap() against hand-computed geometry:
// the player wedge points in the car's heading direction, the world-space
// -> minimap-space transform matches JS's own formula (index.html:4068),
// and the trouble-ring predicate (spinT>0 || blown || dmg>0.6) only fires
// for cars actually in trouble.

#include "../src/render/minimap.h"

#include <cmath>
#include <cstdio>

namespace {

bool ok = true;

void check(bool cond, const char* what) {
    if (!cond) {
        std::fprintf(stderr, "minimap_test: FAILED -- %s\n", what);
        ok = false;
    }
}

bool hasVertexNear(const std::vector<PosColorVertex>& v, float x, float y, float eps = 1e-3f) {
    for (auto& p : v) {
        if (std::fabs(p.x - x) < eps && std::fabs(p.y - y) < eps) return true;
    }
    return false;
}

Car makeTestCar(bool isPlayer, double x, double y, double hdg) {
    Car c;
    c.isPlayer = isPlayer;
    c.x = x;
    c.y = y;
    c.hdg = hdg;
    c.col = {0.5, 0.6, 0.7};
    return c;
}

} // namespace

int main() {
    const MinimapBox box = {0.0f, 0.0f, 100.0f, 100.0f};
    const std::vector<std::pair<float, float>> outline = {{-10, -10}, {10, -10}, {10, 10}, {-10, 10}};
    const float boundX = 10.0f, boundY = 10.0f;

    // Hand-computed to match index.html:4068 exactly:
    // sc = min((100-18)/2/10, (100-14)/2/10) = min(4.1, 4.3) = 4.1
    const float sc = 4.1f;
    const float ox = 50.0f, oy = 50.0f;

    // Player wedge, facing +x (hdg=0), at world origin (maps to minimap
    // center). Hand-computed tip/back-left/back-right vertices, matching
    // index.html:4083-4089's tipR=5.5/backR=3/halfW=2.4 exactly.
    {
        std::vector<Car> cars = {makeTestCar(true, 0.0, 0.0, 0.0)};
        std::vector<PosColorVertex> uiOut;
        drawMinimap(box, outline, boundX, boundY, cars, /*simT=*/0.0, uiOut);

        const float X = ox, Y = oy; // car at world (0,0)
        check(hasVertexNear(uiOut, X + 5.5f, Y), "wedge tip should point in +x for hdg=0");
        check(hasVertexNear(uiOut, X - 3.0f, Y + 2.4f), "wedge back-left vertex mismatch for hdg=0");
        check(hasVertexNear(uiOut, X - 3.0f, Y - 2.4f), "wedge back-right vertex mismatch for hdg=0");
    }

    // Player wedge facing +y (hdg=pi/2) -- tip should now point in +y,
    // confirming the wedge genuinely rotates with heading rather than
    // always pointing the same fixed direction.
    {
        std::vector<Car> cars = {makeTestCar(true, 0.0, 0.0, M_PI / 2.0)};
        std::vector<PosColorVertex> uiOut;
        drawMinimap(box, outline, boundX, boundY, cars, 0.0, uiOut);
        check(hasVertexNear(uiOut, ox, oy + 5.5f), "wedge tip should point in +y for hdg=pi/2");
        check(!hasVertexNear(uiOut, ox + 5.5f, oy), "wedge should NOT still point +x once hdg rotates");
    }

    // World -> minimap transform for a non-player car: X = ox + x*sc,
    // Y = oy + y*sc (index.html:4071). pushFilledCircle's triangle fan
    // includes the exact center point (confirmed in ui_draw_test.cpp),
    // so the car's own mapped position should appear as a vertex.
    {
        std::vector<Car> cars = {makeTestCar(false, 5.0, -5.0, 0.0)};
        std::vector<PosColorVertex> uiOut;
        drawMinimap(box, outline, boundX, boundY, cars, 0.0, uiOut);
        const float X = ox + 5.0f * sc, Y = oy + (-5.0f) * sc;
        check(hasVertexNear(uiOut, X, Y), "AI car dot should sit at ox+x*sc, oy+y*sc");
    }

    // Trouble-ring predicate: a car with none of spinT>0/blown/dmg>0.6
    // should NOT get a ring; an otherwise-identical car that is spinning
    // should produce strictly more geometry (the extra pushRingOutline).
    {
        Car clean = makeTestCar(false, 3.0, 3.0, 0.0);
        Car spinning = makeTestCar(false, 3.0, 3.0, 0.0);
        spinning.spinT = 1.0;

        std::vector<PosColorVertex> uiClean, uiSpinning;
        drawMinimap(box, outline, boundX, boundY, {clean}, 0.0, uiClean);
        drawMinimap(box, outline, boundX, boundY, {spinning}, 0.0, uiSpinning);
        check(uiSpinning.size() > uiClean.size(),
              "a spinning car should add trouble-ring geometry a clean car doesn't have");

        // blown and dmg>0.6 should each independently trigger it too.
        Car blownCar = makeTestCar(false, 3.0, 3.0, 0.0);
        blownCar.blown = true;
        std::vector<PosColorVertex> uiBlown;
        drawMinimap(box, outline, boundX, boundY, {blownCar}, 0.0, uiBlown);
        check(uiBlown.size() > uiClean.size(), "blown should also trigger the trouble ring");

        Car damaged = makeTestCar(false, 3.0, 3.0, 0.0);
        damaged.dmg = 0.7;
        std::vector<PosColorVertex> uiDamaged;
        drawMinimap(box, outline, boundX, boundY, {damaged}, 0.0, uiDamaged);
        check(uiDamaged.size() > uiClean.size(), "dmg>0.6 should also trigger the trouble ring");

        Car slightlyDamaged = makeTestCar(false, 3.0, 3.0, 0.0);
        slightlyDamaged.dmg = 0.5;
        std::vector<PosColorVertex> uiSlight;
        drawMinimap(box, outline, boundX, boundY, {slightlyDamaged}, 0.0, uiSlight);
        check(uiSlight.size() == uiClean.size(), "dmg<=0.6 should NOT trigger the trouble ring");
    }

    // Empty outline (no track set yet) should be a safe no-op, not a crash.
    {
        std::vector<PosColorVertex> uiOut;
        drawMinimap(box, {}, boundX, boundY, {}, 0.0, uiOut);
        check(uiOut.empty(), "an empty outline should produce no geometry at all");
    }

    if (ok) {
        std::printf("minimap_test: wedge direction, transform, and trouble-ring predicate match expectations.\n");
        return 0;
    }
    return 1;
}
