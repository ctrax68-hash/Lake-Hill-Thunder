#pragma once

#include "../sim/car.h"

#include <cstdint>
#include <vector>

// Port of JS's paintLivery()/buildCarTexture() (index.html:2594-2880): a
// per-car livery texture painted once and cached by car number (matching
// JS's CARBUFS cache). Deliberately bgfx-free (same "pure logic" split as
// every other Phase 5 pixel/geometry generator).
//
// **Logged simplifications** (PORT_PROGRESS.md):
// (1) 256x256 texture instead of JS's 768x768 -- consistent with this
//     port's other reduced-resolution textures (sky, atlas); no pixel-
//     exact-fidelity requirement.
// (2) UV mapping: JS's `carU()`/`carV()` map onto a full 3D lofted car
//     body (nose/hood/roof/deck/tail all separate curved surfaces) --
//     out of scope for this port (no 3D car loft, an already-agreed
//     Phase 5 scope cut). This port's car is a single flat top-down quad,
//     so most of paintLivery()'s side-panel-specific detail (rocker
//     shadows, wheel-arch rings meant to be seen from the side, nose/tail
//     masks meant to be seen from the front/back) is never actually
//     visible from the camera angles this port supports -- ported anyway,
//     for a complete, correct texture (useful if a 3D loft is ever added
//     later), but a direct, accepted consequence of that same scope cut,
//     not a new corner cut here.
// (3) Car numbers use a small embedded 7-segment-style digit rasterizer
//     instead of real font text (JS's `drawNum()` calls into the browser's
//     own font renderer, `'900 ...px Arial'` -- no equivalent exists in
//     this CPU rasterizer without embedding a full font, well outside this
//     item's scope). Renders 1-2 digit numbers (every real ROSTER/player
//     number is 1-2 digits).
// (4) Skipped entirely, all needing a bitmap-font renderer this port
//     doesn't have (same rationale as Phase 5e's sponsor-tile-text skip):
//     sponsor wordmarks, the "LHT CUP" series bar text.
// (5) Skipped as inapplicable, not deferred: the SW.* "swatches" JS paints
//     so its separate 3D geometry parts (tires/rims/chrome trim) can
//     sample flat solid colors from this same texture -- this port's car
//     has no such separate geometry to sample them, so there's nothing
//     for them to serve.
// (6) Skipped for scope control (low visual value relative to
//     implementation cost, unlike the number decals/stripe styles which
//     are the livery's actual identity): contingency decal chips, hood
//     pins, the fuel-filler ring.

inline constexpr int kLiveryTextureSize = 256;

// body: car.col (or CarPalette::White for a pace car -- not built here,
// see this file's own note below). accent: auto-derived from body's
// luminance (index.html:2867-2868), just like JS. num/idx/scheme: the
// same per-car identity fields already threaded through this port
// (car.h). scheme may be null (only ever true for the player car),
// matching JS's own fallback to idx-based mechanical stripe/mask/sponsor
// picks. No pace-car variant: this port has no pace-car visual yet
// (verified before this sub-phase started), so there is nothing to build
// one for.
std::vector<uint8_t> buildLiveryPixels(const Color3& body, int num, int idx, const LiveryScheme* scheme);
