#include "ui_draw.h"

#include <algorithm>
#include <cmath>

namespace {
constexpr float kTwoPi = 6.28318530717958647692f;
} // namespace

void pushQuad(std::vector<PosColorVertex>& out, float x, float y, float w, float h, uint32_t abgr) {
    const float x0 = x, y0 = y, x1 = x + w, y1 = y + h;
    out.push_back({x0, y0, 0.0f, abgr});
    out.push_back({x1, y0, 0.0f, abgr});
    out.push_back({x1, y1, 0.0f, abgr});
    out.push_back({x0, y0, 0.0f, abgr});
    out.push_back({x1, y1, 0.0f, abgr});
    out.push_back({x0, y1, 0.0f, abgr});
}

void pushTriangle(std::vector<PosColorVertex>& out, float x0, float y0, float x1, float y1,
                   float x2, float y2, uint32_t abgr) {
    out.push_back({x0, y0, 0.0f, abgr});
    out.push_back({x1, y1, 0.0f, abgr});
    out.push_back({x2, y2, 0.0f, abgr});
}

void pushLineSegment(std::vector<PosColorVertex>& out, float x0, float y0, float x1, float y1,
                     float thickness, uint32_t abgr) {
    const float dx = x1 - x0, dy = y1 - y0;
    const float len = std::sqrt(dx * dx + dy * dy);
    if (len < 1e-6f) return;
    const float half = thickness * 0.5f;
    const float nx = -dy / len * half, ny = dx / len * half;
    out.push_back({x0 + nx, y0 + ny, 0.0f, abgr});
    out.push_back({x1 + nx, y1 + ny, 0.0f, abgr});
    out.push_back({x1 - nx, y1 - ny, 0.0f, abgr});
    out.push_back({x0 + nx, y0 + ny, 0.0f, abgr});
    out.push_back({x1 - nx, y1 - ny, 0.0f, abgr});
    out.push_back({x0 - nx, y0 - ny, 0.0f, abgr});
}

void pushPolyline(std::vector<PosColorVertex>& out, const std::vector<std::pair<float, float>>& points,
                   float thickness, uint32_t abgr, bool closed) {
    if (points.size() < 2) return;
    for (size_t i = 0; i + 1 < points.size(); ++i) {
        pushLineSegment(out, points[i].first, points[i].second, points[i + 1].first, points[i + 1].second,
                        thickness, abgr);
    }
    if (closed && points.size() > 2) {
        pushLineSegment(out, points.back().first, points.back().second, points.front().first,
                        points.front().second, thickness, abgr);
    }
}

void pushRingOutline(std::vector<PosColorVertex>& out, float cx, float cy, float radius,
                      float thickness, uint32_t abgr, int segments) {
    std::vector<std::pair<float, float>> pts;
    pts.reserve((size_t)segments);
    for (int i = 0; i < segments; ++i) {
        const float a = (float)i / (float)segments * kTwoPi;
        pts.push_back({cx + radius * std::cos(a), cy + radius * std::sin(a)});
    }
    pushPolyline(out, pts, thickness, abgr, true);
}

void pushFilledCircle(std::vector<PosColorVertex>& out, float cx, float cy, float radius,
                       uint32_t abgr, int segments) {
    for (int i = 0; i < segments; ++i) {
        const float a0 = (float)i / (float)segments * kTwoPi;
        const float a1 = (float)(i + 1) / (float)segments * kTwoPi;
        pushTriangle(out, cx, cy, cx + radius * std::cos(a0), cy + radius * std::sin(a0),
                     cx + radius * std::cos(a1), cy + radius * std::sin(a1), abgr);
    }
}

void pushSegBar(std::vector<PosColorVertex>& out, float x, float y, float w, float h, double frac,
                 int segN, uint32_t filledAbgr, uint32_t emptyAbgr) {
    // index.html:3868-3874's drawSegBar(): segN discrete blocks, `gap`
    // px apart, `filled` of them lit -- a discrete diagnostic bar, not a
    // smooth fill.
    const float gap = 2.0f;
    const float segW = (w - gap * (float)(segN - 1)) / (float)segN;
    const int filled = (int)std::lround(std::max(0.0, std::min(1.0, frac)) * segN);
    for (int i = 0; i < segN; ++i) {
        pushQuad(out, x + (float)i * (segW + gap), y, segW, h, i < filled ? filledAbgr : emptyAbgr);
    }
}
