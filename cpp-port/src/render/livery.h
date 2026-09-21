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
//     **OBSOLETE since G1/G8 added the real 3D loft, and its lingering
//     consequence was a bug G16 fixed.** Every face now has a real
//     wraparound UV (gen_car_rig.py's emit_quad()), so the side-panel
//     detail above IS visible now. But the nose/tail masks were still
//     being painted at u=0.845 -- dead space between the body wrap
//     (u<=0.78) and the G1c/G8 swatch columns (u>=0.85) that no geometry
//     samples -- so the headlights/taillights stayed invisible for a
//     different reason than this note claimed. G16 re-places them onto the
//     u=0.02 (nose cap) and u=0.78 (tail cap) columns emit_quad() actually
//     assigns; see livery.cpp's own comment there for the V-band layout.
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
//     **Partly reversed by G16** (NT2003 presentation plan): the
//     contingency decal chips are back. The "low visual value" call was
//     made against this port's own prior look; measured against the NT2003
//     reference footage G16 targets, the chip row along the lower rear
//     quarter is one of the few era cues that stays readable at real
//     chase-cam distance. Hood pins and the fuel-filler ring remain
//     skipped -- those genuinely are sub-pixel at every supported camera
//     distance.

// G1b (NASCAR-Thunder gap-analysis plan, car UV/livery fix): bumped
// 256->512 now that side panels actually get real UVs (see
// gen_car_rig.py's emit_quad()) and the painted stripes/numbers/glass are
// finally visible on the body instead of mostly landing on the one flat
// (0.4,0.5) texel every non-roof face used to sample -- still well under
// JS's original 768, no pixel-exact-fidelity requirement.
//
// H2 (NT2003 engine-feel plan): bumped again, 512->1024, now ABOVE JS's
// original 768. H1 gave the body real curved geometry and this port's
// chase camera sits close enough to it that hairline detail -- panel
// shutlines, window rubber, the wheel-arch lip -- needs more than one
// texel per line to read as a line rather than a soft smear once
// downsampleBox()'s box filter gets to it.
//
// J1 (car visual fidelity plan, part 2): bumped again, 1024->2048. Not
// forced by any single feature in this file -- every fill* call is
// fraction-based and auto-scales -- but J6's two-layer contingency chips
// add a 1-2 texel white border per chip, which isn't crisply resolvable at
// 1024 the way it is at 2048. A deliberate, user-accepted cost: this
// roughly quadruples each cached car livery's CPU-side paint buffer and
// GPU-resident mip-chained texture (kSupersample=2 means the scratch
// canvas goes from 2048x2048 to 4096x4096 per car), with no measured
// device-memory budget anywhere in this project to check it against --
// see this phase's PORT_PROGRESS.md entry for the full accounting.
inline constexpr int kLiveryTextureSize = 2048;

// T12: GLOSS (reflectivity) written into the livery's ALPHA channel, which
// until now carried a hardcoded 255 that nothing read. fs_car.sc samples it
// to give each material its own environment response.
//
// WHY THIS IS A SCALAR IN ALPHA rather than the RGB colour-distance material
// tests it replaces: a scalar mip-filters into a sensible in-between value,
// where a colour test drifts off its reference as the texture filters and
// silently stops firing -- in the pack, which is where nearly every car is
// actually seen. It also cannot collide, unlike glass and tire rubber, which
// are both "near black" and needed a hand-verified threshold radius to keep
// them from matching each other.
//
// The measurement that forced it: with one reflectivity for every texel, a
// BLACK TIRE rendered (42, 72, 109) -- a blue-grey -- and the body's
// (11, 131, 2) green rendered (31, 229, 138), its blue lifted from 2 to 138.
//
// Declared here, not in livery.cpp, so livery_test can assert against the
// real values instead of keeping its own copy of the numbers it checks --
// the exact duplication that has repeatedly let guards pass while describing
// geometry that no longer existed.
inline constexpr double kGlossPaint = 0.55;   // clearcoat over body paint
// T23: 0.95 -> 0.30. At 0.95 the greenhouse mirrored the sky hard enough to
// render BRIGHTER than white bodywork (0.55-0.65 against 0.50-0.60), while
// both reference cars in the second batch show the window opening at 0.09-0.24
// absolute -- a dark hole you see into, with at most a streak of sky across the
// top of the pane. The tint went down with it (livery.cpp's glassDark); this
// number is the half of the error that made darkening the tint alone useless,
// exactly as kGlossSteel was for the wheels below.
inline constexpr double kGlossGlass = 0.30;   // windows: a dark pane, lightly lit
inline constexpr double kGlossChrome = 0.85;  // bright trim: grille surround
// T22: a wheel is painted STEEL, not chrome. Carrying kGlossChrome it mirrored
// the sky and rendered cream however dark its albedo was -- darkening the
// swatch from (198,200,206) to (72,74,80) barely moved the rendered pixel,
// because at 0.85 reflectivity the paint underneath hardly matters. The
// reference's wheel face sits at 1.30x the tire beside it with no highlight
// on it at all, which is a low-gloss surface, not a dark mirror.
inline constexpr double kGlossSteel = 0.12;   // wheel rim: dark, near-matte
inline constexpr double kGlossDecal = 0.20;   // printed vinyl is matte next to paint
inline constexpr double kGlossRubber = 0.04;  // tires reflect essentially nothing
inline constexpr double kGlossMatte = 0.10;   // grille mesh, cage bars, rubber trim

// T32: THE FLANK'S LAYOUT ANCHORS, PUBLISHED.
//
// These were file-local in livery.cpp, and everything that needed to know
// where a mark sits on the door -- the paint, check_car_rig.py, livery_test --
// kept its own copy of the number. That is how the door number came to be
// painted 0.32 m behind the door's real centre and stayed there through two
// re-authorings of the body: it was written as carU(-0.10) when the axles were
// somewhere else, and nothing related it to where the wheels actually are.
//
// The arch spans are generated: check_car_rig.py recomputes all five from
// gen_car_rig.py and fails if these drift. Everything else on the flank is
// derived FROM them here, so a mark's position is a consequence of where the
// wheels are rather than a literal that has to be remembered.
constexpr double kArchFrontU0 = 0.0878, kArchFrontU1 = 0.2285;
// T23: kArchRearU1 0.6528 -> 0.6459. The rear axle did not move; the arch's
// rear U bound is the last STATION the carve reaches, and moving deck_start
// from -1.67 to -1.56 with the greenhouse changed which station that is.
constexpr double kArchRearU0 = 0.5052, kArchRearU1 = 0.6459;
constexpr double kArchFrontCU = (kArchFrontU0 + kArchFrontU1) * 0.5;
constexpr double kArchRearCU = (kArchRearU0 + kArchRearU1) * 0.5;
constexpr double kArchLipV = 0.8848;  // RINGV[K_LIP]

// T32b: WHERE THE NUMBER SITS, MEASURED OFF THE PHOTOGRAPH.
//
// T32 put this at the midpoint of the two wheel openings and said so as if it
// were a fact about race cars. It is not. Measured on the #41 side view --
// glyph ink box (598,112)-(692,181), hubs (518,161) and (846,188) -- the
// number's centre sits at 0.381 of the wheelbase forward of the REAR hub, not
// 0.500. Behind the middle of the door, which is where a Gen-4 door number
// actually lives, because the front of the door is where the contingency block
// goes.
//
// So T32 moved the number 0.322 m FORWARD of where it belongs while claiming
// to have moved it 0.323 m back to where it belonged. The literal it replaced,
// carU(-0.10) = 0.4151, was within 10 mm of the photograph all along; what was
// wrong with it was that it was a literal, not that it was in the wrong place.
// That is now fixed properly: the fraction is measured, and it hangs off the
// arch centres, so it still moves with the wheels.
constexpr double kDoorNumberT = 0.381;  // fraction of the wheelbase, from the rear hub
constexpr double kDoorCenterU = kArchRearCU - kDoorNumberT * (kArchRearCU - kArchFrontCU);
// Cap height and the glyph box's top edge, in texture fractions, both measured
// on the same frame. The ink is 69 px tall against 117.9 px/m, so 0.585 m, and
// V advances 0.362 per metre down the flank: fh 0.212. Its top edge sits 7 px
// under the beltline seam, about 0.07 m along the door's surface, so 0.025 of
// V below the seam at 0.677.
//
// NOT sized as a fraction of the belt-to-rocker band, which is how T32 got
// 0.225. That band is 0.308 of V but 0.851 m of ARC, against the photo's 0.72 m
// of PROJECTED height -- the door curves away under the camera, so the same
// "80% of the door" reads as two different numbers depending on which one you
// measure it in. Metres are the same in both.
constexpr double kDoorNumberFh = 0.212;
constexpr double kDoorNumberTopV = 0.677 + 0.025;
// The contingency stack: 3 wide, 4 deep, starting just aft of the front arch.
constexpr double kChipU0 = kArchFrontU1 + 0.008;
constexpr double kChipDU = 0.020, kChipW = 0.017;
constexpr double kChipV0 = 0.846, kChipDV = 0.024, kChipH = 0.018;
constexpr int kChipCols = 3, kChipRows = 4;

// body: car.col (or CarPalette::White for a pace car -- not built here,
// see this file's own note below). accent: auto-derived from body's
// luminance (index.html:2867-2868), just like JS. num/idx/scheme: the
// same per-car identity fields already threaded through this port
// (car.h). scheme may be null (only ever true for the player car),
// matching JS's own fallback to idx-based mechanical stripe/mask/sponsor
// picks. No pace-car variant: this port has no pace-car visual yet
// (verified before this sub-phase started), so there is nothing to build
// one for.
// `paceLightBar` (G20, NT2003 presentation plan): paints an amber roof
// light bar in place of the roof number. The JS original gave its pace car
// a genuinely emissive bar (`CAR_MAT_AMBER`, index.html:2477,
// emissiveIntensity 1.2), and this port has **no emissive path at all** --
// fs_lit.sc is ambient + sun only, with no self-illumination term. Rather
// than add a whole emissive pipeline (new shader + uniform + three CMake
// touchpoints) for one prop, the bar is painted bright enough that after
// lighting it clears Phase 5h's bloom bright-pass threshold (0.85,
// renderer.cpp), so the existing post-FX chain gives it a real glow for
// free. It does not pulse -- that would need per-frame texture work or the
// emissive uniform this port doesn't have; deferred, and noted here rather
// than silently dropped.
std::vector<uint8_t> buildLiveryPixels(const Color3& body, int num, int idx, const LiveryScheme* scheme,
                                        bool paceLightBar = false);
