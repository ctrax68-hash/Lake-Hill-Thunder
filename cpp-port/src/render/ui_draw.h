#pragma once

#include "vertex.h"

#include <string>
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

// G22 (NT2003 presentation plan): a *partial* ring -- the open-arc case
// pushRingOutline() can't express, since that one always closes the loop.
// Angles are radians in the same top-left-origin, y-down pixel space as
// everything else here, so 0 points right, PI/2 points **down**, and
// increasing the angle sweeps clockwise on screen. The tachometer's dial
// runs from 150 to 390 degrees, which in that convention starts at the
// lower left, climbs over the top, and ends at the lower right, leaving the
// bottom of the face clear for the digital readouts.
//
// `segments` is the count for a full circle; the arc uses a proportional
// share of them, so arcs of different lengths get consistent smoothness.
void pushArc(std::vector<PosColorVertex>& out, float cx, float cy, float radius, float a0, float a1,
              float thickness, uint32_t abgr, int segments = 64);

// G21 (NT2003 presentation plan): a beveled panel -- flat fill plus a
// light top/left edge and a dark bottom/right edge. The reference HUD's
// pos/lap, lap-time and gauge surrounds are all beveled plates rather than
// flat rectangles, and it's what separates them from the scene behind.
void pushBevelPanel(std::vector<PosColorVertex>& out, float x, float y, float w, float h, uint32_t fillAbgr,
                     uint32_t lightAbgr, uint32_t darkAbgr, float bevel = 2.0f);

// G21: UI-space seven-segment digits.
//
// The reference's MPH/gear readouts are segmented LCDs, not font text --
// so rather than adding a bitmap-font atlas and a textured-UI path just to
// print numbers, this reuses the segment layout `digit_mesh.h` already
// established for the pylon/jumbotron and emits plain `pushQuad()` calls in
// pixel space. `digit_mesh` itself can't serve here: it emits world-space
// `MeshVertex` with per-triangle normals for the lit 3D pipeline, and it
// takes an `int` (via `std::to_string`), so it can express neither ':' nor
// '.' -- which lap times need.
//
// `offAbgr` paints the unlit segments as faint ghosts, the way a real LCD
// shows its inactive bars; pass a fully transparent color to omit them.
void pushSevenSegDigit(std::vector<PosColorVertex>& out, float x, float y, float w, float h, int digit,
                       uint32_t onAbgr, uint32_t offAbgr);

// Renders `text` as seven-segment glyphs left-to-right starting at (x, y).
// Understands '0'-'9', ':', '.', '-', '/' and ' '; any other character
// advances one cell without drawing (so callers can pad freely). Returns
// the total advanced width in pixels, so callers can right-align.
float pushSevenSegText(std::vector<PosColorVertex>& out, float x, float y, float cellW, float cellH, float gap,
                        const std::string& text, uint32_t onAbgr, uint32_t offAbgr);

// Measures what pushSevenSegText() would advance, without emitting
// geometry -- for right-aligning a value inside a panel.
float measureSevenSegText(float cellW, float gap, const std::string& text);

// Direct port of JS's drawSegBar() (index.html:3868-3874): segN discrete
// blocks across width `w`, `frac` (clamped to [0,1]) of them filled with
// `filledAbgr`, the rest `emptyAbgr` -- a discrete diagnostic bar, not a
// smooth fill.
void pushSegBar(std::vector<PosColorVertex>& out, float x, float y, float w, float h, double frac,
                 int segN, uint32_t filledAbgr, uint32_t emptyAbgr);
