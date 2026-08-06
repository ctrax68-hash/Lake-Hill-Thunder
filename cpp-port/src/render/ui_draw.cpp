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

void pushArc(std::vector<PosColorVertex>& out, float cx, float cy, float radius, float a0, float a1,
              float thickness, uint32_t abgr, int segments) {
    const float sweep = a1 - a0;
    if (std::abs(sweep) < 1e-6f || radius <= 0.0f) return;
    // `segments` is the count for a full turn, so a short arc gets
    // proportionally fewer -- consistent smoothness whatever the span.
    const int n = std::max(2, (int)std::lround(std::abs(sweep) / kTwoPi * (float)segments));
    std::vector<std::pair<float, float>> pts;
    pts.reserve((size_t)n + 1);
    for (int i = 0; i <= n; ++i) {
        const float a = a0 + sweep * ((float)i / (float)n);
        pts.push_back({cx + radius * std::cos(a), cy + radius * std::sin(a)});
    }
    pushPolyline(out, pts, thickness, abgr, false);
}

void pushBevelPanel(std::vector<PosColorVertex>& out, float x, float y, float w, float h, uint32_t fillAbgr,
                     uint32_t lightAbgr, uint32_t darkAbgr, float bevel) {
    pushQuad(out, x, y, w, h, fillAbgr);
    // Light along the top and left, dark along the bottom and right -- the
    // standard raised-plate read, and the direction the reference HUD's own
    // plates are lit from.
    pushQuad(out, x, y, w, bevel, lightAbgr);
    pushQuad(out, x, y, bevel, h, lightAbgr);
    pushQuad(out, x, y + h - bevel, w, bevel, darkAbgr);
    pushQuad(out, x + w - bevel, y, bevel, h, darkAbgr);
}

namespace {

// Segment order matches digit_mesh.h's own table exactly (0=top,
// 1=top-left, 2=top-right, 3=middle, 4=bottom-left, 5=bottom-right,
// 6=bottom) so the two rasterizers can't drift apart.
constexpr bool kSevenSeg[10][7] = {
    {1, 1, 1, 0, 1, 1, 1}, // 0
    {0, 0, 1, 0, 0, 1, 0}, // 1
    {1, 0, 1, 1, 1, 0, 1}, // 2
    {1, 0, 1, 1, 0, 1, 1}, // 3
    {0, 1, 1, 1, 0, 1, 0}, // 4
    {1, 1, 0, 1, 0, 1, 1}, // 5
    {1, 1, 0, 1, 1, 1, 1}, // 6
    {1, 0, 1, 0, 0, 1, 0}, // 7
    {1, 1, 1, 1, 1, 1, 1}, // 8
    {1, 1, 1, 1, 0, 1, 1}, // 9
};

// ':' and '.' are visually narrow, so they advance less than a full digit
// cell -- otherwise "1:23.456" reads with obvious gaps around the
// punctuation.
constexpr float kNarrowAdvance = 0.45f;

bool isNarrowGlyph(char ch) { return ch == ':' || ch == '.'; }

} // namespace

void pushSevenSegDigit(std::vector<PosColorVertex>& out, float x, float y, float w, float h, int digit,
                       uint32_t onAbgr, uint32_t offAbgr) {
    if (digit < 0 || digit > 9) return;
    const float t = w * 0.20f; // segment thickness
    const float half = h * 0.5f;
    const bool* seg = kSevenSeg[digit];
    const auto bar = [&](int idx, float bx, float by, float bw, float bh) {
        const uint32_t col = seg[idx] ? onAbgr : offAbgr;
        if ((col >> 24) == 0u) return; // fully transparent -> emit nothing
        pushQuad(out, bx, by, bw, bh, col);
    };
    bar(0, x, y, w, t);                   // top
    bar(1, x, y, t, half);                // top-left
    bar(2, x + w - t, y, t, half);        // top-right
    bar(3, x, y + half - t * 0.5f, w, t); // middle
    bar(4, x, y + half, t, half);         // bottom-left
    bar(5, x + w - t, y + half, t, half); // bottom-right
    bar(6, x, y + h - t, w, t);           // bottom
}

float measureSevenSegText(float cellW, float gap, const std::string& text) {
    if (text.empty()) return 0.0f;
    float adv = 0.0f;
    for (char ch : text) adv += (isNarrowGlyph(ch) ? cellW * kNarrowAdvance : cellW) + gap;
    return adv - gap;
}

float pushSevenSegText(std::vector<PosColorVertex>& out, float x, float y, float cellW, float cellH, float gap,
                        const std::string& text, uint32_t onAbgr, uint32_t offAbgr) {
    float cx = x;
    const float t = cellW * 0.20f;
    for (char ch : text) {
        const float adv = isNarrowGlyph(ch) ? cellW * kNarrowAdvance : cellW;
        if (ch >= '0' && ch <= '9') {
            pushSevenSegDigit(out, cx, y, cellW, cellH, ch - '0', onAbgr, offAbgr);
        } else if (ch == ':') {
            pushQuad(out, cx + adv * 0.5f - t * 0.5f, y + cellH * 0.28f, t, t, onAbgr);
            pushQuad(out, cx + adv * 0.5f - t * 0.5f, y + cellH * 0.64f, t, t, onAbgr);
        } else if (ch == '.') {
            pushQuad(out, cx + adv * 0.5f - t * 0.5f, y + cellH - t, t, t, onAbgr);
        } else if (ch == '-') {
            pushQuad(out, cx, y + cellH * 0.5f - t * 0.5f, cellW, t, onAbgr);
        } else if (ch == '/') {
            pushLineSegment(out, cx + cellW * 0.85f, y, cx + cellW * 0.15f, y + cellH, t, onAbgr);
        }
        // Anything else (including ' ') advances without drawing, so callers
        // can pad to a fixed width for a stable, non-jittering readout.
        cx += adv + gap;
    }
    return text.empty() ? 0.0f : cx - gap - x;
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
