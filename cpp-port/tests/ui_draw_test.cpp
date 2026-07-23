// Verifies ui_draw.{h,cpp}'s pure 2D geometry helpers -- zero bgfx
// dependency, same rationale as touch_controls_test.cpp/menu_test.cpp:
// this is testable math, not something that needs a live rendering
// context.

#include "../src/render/ui_draw.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>

namespace {

bool ok = true;

void check(bool cond, const char* what) {
    if (!cond) {
        std::fprintf(stderr, "ui_draw_test: FAILED -- %s\n", what);
        ok = false;
    }
}

float minX(const std::vector<PosColorVertex>& v) {
    float m = 1e9f;
    for (auto& p : v) m = std::min(m, p.x);
    return m;
}
float maxX(const std::vector<PosColorVertex>& v) {
    float m = -1e9f;
    for (auto& p : v) m = std::max(m, p.x);
    return m;
}
float minY(const std::vector<PosColorVertex>& v) {
    float m = 1e9f;
    for (auto& p : v) m = std::min(m, p.y);
    return m;
}
float maxY(const std::vector<PosColorVertex>& v) {
    float m = -1e9f;
    for (auto& p : v) m = std::max(m, p.y);
    return m;
}

} // namespace

int main() {
    // pushQuad: 6 vertices (2 triangles), bbox matches (x,y,x+w,y+h).
    {
        std::vector<PosColorVertex> v;
        pushQuad(v, 10.0f, 20.0f, 30.0f, 40.0f, 0xffffffff);
        check(v.size() == 6, "pushQuad should produce 6 vertices");
        check(std::abs(minX(v) - 10.0f) < 1e-4f && std::abs(maxX(v) - 40.0f) < 1e-4f,
              "pushQuad x bbox should be [10,40]");
        check(std::abs(minY(v) - 20.0f) < 1e-4f && std::abs(maxY(v) - 60.0f) < 1e-4f,
              "pushQuad y bbox should be [20,60]");
    }

    // pushTriangle: 3 vertices, matching input points exactly.
    {
        std::vector<PosColorVertex> v;
        pushTriangle(v, 0, 0, 10, 0, 5, 10, 0xff00ff00);
        check(v.size() == 3, "pushTriangle should produce 3 vertices");
        check(v[0].x == 0 && v[0].y == 0, "pushTriangle vertex 0 mismatch");
        check(v[1].x == 10 && v[1].y == 0, "pushTriangle vertex 1 mismatch");
        check(v[2].x == 5 && v[2].y == 10, "pushTriangle vertex 2 mismatch");
    }

    // pushLineSegment: a horizontal segment should extrude purely
    // vertically (normal is perpendicular to the segment direction).
    {
        std::vector<PosColorVertex> v;
        pushLineSegment(v, 0.0f, 50.0f, 100.0f, 50.0f, 4.0f, 0xffffffff);
        check(v.size() == 6, "pushLineSegment should produce 6 vertices");
        check(std::abs(minY(v) - 48.0f) < 1e-4f && std::abs(maxY(v) - 52.0f) < 1e-4f,
              "horizontal pushLineSegment should extrude +/-half-thickness vertically");
        check(std::abs(minX(v) - 0.0f) < 1e-4f && std::abs(maxX(v) - 100.0f) < 1e-4f,
              "horizontal pushLineSegment should span its x endpoints");
    }

    // pushPolyline: N points open = N-1 segments (6 verts each); closed
    // adds one more segment connecting the last point back to the first.
    {
        const std::vector<std::pair<float, float>> pts = {{0, 0}, {10, 0}, {10, 10}, {0, 10}};
        std::vector<PosColorVertex> vOpen, vClosed;
        pushPolyline(vOpen, pts, 2.0f, 0xffffffff, false);
        pushPolyline(vClosed, pts, 2.0f, 0xffffffff, true);
        check(vOpen.size() == 3 * 6, "open 4-point polyline should have 3 segments");
        check(vClosed.size() == 4 * 6, "closed 4-point polyline should have 4 segments");
    }

    // pushRingOutline: a closed N-segment polyline around a circle ->
    // N*6 vertices, and every vertex should sit at ~radius distance
    // (+/- half thickness) from the center.
    {
        std::vector<PosColorVertex> v;
        pushRingOutline(v, 100.0f, 100.0f, 20.0f, 2.0f, 0xffffffff, 16);
        check(v.size() == 16 * 6, "pushRingOutline(segments=16) should have 96 vertices");
        bool allNearRadius = true;
        for (auto& p : v) {
            const float dx = p.x - 100.0f, dy = p.y - 100.0f;
            const float dist = std::sqrt(dx * dx + dy * dy);
            if (dist < 18.0f || dist > 22.0f) allNearRadius = false;
        }
        check(allNearRadius, "pushRingOutline vertices should sit near the requested radius");
    }

    // pushFilledCircle: N triangles (3 verts each), center point present.
    {
        std::vector<PosColorVertex> v;
        pushFilledCircle(v, 50.0f, 50.0f, 5.0f, 0xffffffff, 12);
        check(v.size() == 12 * 3, "pushFilledCircle(segments=12) should have 36 vertices");
        bool hasCenter = false;
        for (auto& p : v) {
            if (std::abs(p.x - 50.0f) < 1e-4f && std::abs(p.y - 50.0f) < 1e-4f) hasCenter = true;
        }
        check(hasCenter, "pushFilledCircle should include the center point (triangle fan)");
    }

    // pushSegBar: direct port of JS's drawSegBar(). frac=0.5, segN=6 ->
    // round(0.5*6)=3 filled segments, matching Math.round()'s
    // round-half-up behavior JS uses.
    {
        const uint32_t kFilled = 0xff0000ff, kEmpty = 0xff333333;
        std::vector<PosColorVertex> v;
        pushSegBar(v, 0.0f, 0.0f, 60.0f, 8.0f, 0.5, 6, kFilled, kEmpty);
        check(v.size() == 6 * 6, "pushSegBar(segN=6) should produce 6 quads (36 vertices)");
        int filledCount = 0, emptyCount = 0;
        for (size_t i = 0; i < 6; ++i) {
            const uint32_t col = v[i * 6].abgr;
            if (col == kFilled) ++filledCount;
            else if (col == kEmpty) ++emptyCount;
        }
        check(filledCount == 3 && emptyCount == 3, "pushSegBar(frac=0.5) should split 3 filled / 3 empty");
    }

    // pushSegBar edge cases: frac=0 -> all empty; frac=1 -> all filled;
    // out-of-range frac clamped the same way.
    {
        const uint32_t kFilled = 0xff0000ff, kEmpty = 0xff333333;
        std::vector<PosColorVertex> vZero, vOne, vNeg, vOver;
        pushSegBar(vZero, 0, 0, 60, 8, 0.0, 6, kFilled, kEmpty);
        pushSegBar(vOne, 0, 0, 60, 8, 1.0, 6, kFilled, kEmpty);
        pushSegBar(vNeg, 0, 0, 60, 8, -0.5, 6, kFilled, kEmpty);
        pushSegBar(vOver, 0, 0, 60, 8, 1.5, 6, kFilled, kEmpty);
        auto countFilled = [&](const std::vector<PosColorVertex>& v) {
            int n = 0;
            for (size_t i = 0; i < 6; ++i) if (v[i * 6].abgr == kFilled) ++n;
            return n;
        };
        check(countFilled(vZero) == 0, "pushSegBar(frac=0) should have 0 filled");
        check(countFilled(vOne) == 6, "pushSegBar(frac=1) should have all 6 filled");
        check(countFilled(vNeg) == 0, "pushSegBar(frac=-0.5) should clamp to 0 filled");
        check(countFilled(vOver) == 6, "pushSegBar(frac=1.5) should clamp to all 6 filled");
    }

    if (ok) {
        std::printf("ui_draw_test: all geometry helpers match expectations.\n");
        return 0;
    }
    return 1;
}
