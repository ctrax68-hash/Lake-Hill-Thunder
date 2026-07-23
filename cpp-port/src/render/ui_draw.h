#pragma once

#include "vertex.h"

#include <utility>
#include <vector>

// Phase 4e (PORT_PROGRESS.md): pure 2D pixel-space geometry helpers -- zero
// bgfx dependency (just math appending to a std::vector<PosColorVertex>&),
// same "isolate pure logic from anything needing a live render context"
// precedent as touch_controls.h/menu.cpp's region math, so this module gets
// a fast unit test (ui_draw_test.cpp). All positions are in the same
// top-left-origin, y-down pixel space bgfx's dbgText already uses --
// callers (minimap.cpp, leaderboard.cpp, status_bars.cpp, results.cpp)
// append into one frame-scoped vertex list that Renderer submits as a
// second, orthographic-pixel-space bgfx view (renderer.cpp).

void pushQuad(std::vector<PosColorVertex>& out, float x, float y, float w, float h, uint32_t abgr);

void pushTriangle(std::vector<PosColorVertex>& out, float x0, float y0, float x1, float y1,
                   float x2, float y2, uint32_t abgr);

// A thin quad extruded along the segment's normal by half of `thickness` --
// the same "extrude by half-width along the normal" technique
// Renderer::setTrack() already uses for the world-space track ribbon, just
// parameterized by two explicit points instead of Track::pointAt()/halfW().
void pushLineSegment(std::vector<PosColorVertex>& out, float x0, float y0, float x1, float y1,
                     float thickness, uint32_t abgr);

// Calls pushLineSegment() per adjacent point pair; `closed` also connects
// the last point back to the first (used for the minimap's closed
// track-outline loop).
void pushPolyline(std::vector<PosColorVertex>& out, const std::vector<std::pair<float, float>>& points,
                   float thickness, uint32_t abgr, bool closed);

// An N-gon approximation of a circle's outline (a closed pushPolyline()
// around a parametric circle) -- used for the minimap's trouble-pulse
// rings. `abgr` should already have any desired alpha baked in via
// packColor()'s `a` parameter.
void pushRingOutline(std::vector<PosColorVertex>& out, float cx, float cy, float radius,
                      float thickness, uint32_t abgr, int segments = 16);

// A filled N-gon (triangle fan from the center) approximating a disc --
// used for the minimap's per-car dots.
void pushFilledCircle(std::vector<PosColorVertex>& out, float cx, float cy, float radius,
                       uint32_t abgr, int segments = 16);

// Direct port of JS's drawSegBar() (index.html:3868-3874): segN discrete
// blocks across width `w`, `frac` (clamped to [0,1]) of them filled with
// `filledAbgr`, the rest `emptyAbgr` -- a discrete diagnostic bar, not a
// smooth fill.
void pushSegBar(std::vector<PosColorVertex>& out, float x, float y, float w, float h, double frac,
                 int segN, uint32_t filledAbgr, uint32_t emptyAbgr);
