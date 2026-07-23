#pragma once

#include "../sim/car.h"
#include "vertex.h"

#include <utility>
#include <vector>

// Fixed pixel rect for the minimap panel -- this port's own layout, not
// JS's computeLayout() cascade (which depends on the leaderboard panel's
// height, not yet ported as of Phase 4f; revisit once Phase 4g's real
// leaderboard panel exists).
struct MinimapBox {
    float x, y, w, h;
};

// Phase 4f (PORT_PROGRESS.md): direct port of JS's drawMinimap()
// (index.html:4059-4101) -- closed track-outline polyline, per-car dots,
// player directional wedge, pulsing trouble rings for cars in trouble.
// `outline`/`boundX`/`boundY` come from Renderer::setTrack()'s eager cache
// (see renderer.h) -- this port has a clean "track changed" hook
// (setTrack() itself) that JS lacks, so the outline is built once there
// instead of JS's lazy-build-then-null-to-invalidate MMPTS pattern.
// `simT` is RaceState::t, driving the trouble-ring pulse phase exactly
// like JS's `S.t`.
void drawMinimap(const MinimapBox& box, const std::vector<std::pair<float, float>>& outline,
                  float boundX, float boundY, const std::vector<Car>& cars, double simT,
                  std::vector<PosColorVertex>& uiOut);
