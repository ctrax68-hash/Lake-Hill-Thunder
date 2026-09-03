#pragma once

#include "stadium_mesh.h" // MeshVertex
#include "../sim/rng.h"

#include <array>
#include <vector>

// G28/G29 (graphics pass): a treeline ringing the circuit, on every track.
//
// WHY. Three of the four tracks had nothing at all between the grandstands and
// the sky -- the ground plane simply ended and the sky began, which reads as a
// hard cut at the horizon and is the single clearest tell that the world stops
// at the fence. Only Cedar Valley had any distant dressing, its 48-quad hill
// ring (hill_silhouette.h), and that is authored per-track via
// `sky.silhouette`, so it was never going to cover the others.
//
// WHY IT IS NOT hill_silhouette's JOB. That file is a faithful port of JS's
// addHillSilhouette() and says so; this is new geometry with no JS
// counterpart, and bolting it into that file would blur a port boundary this
// project has been careful about elsewhere.
//
// SITS INSIDE THE HILLS. R here is well under the hills' 1400, so on Cedar
// Valley the two layer -- trees in front, hills behind -- which is exactly the
// depth cue the reference gets from its own treeline. G25's distance haze does
// the rest: at this range the trees desaturate toward the sky on their own,
// so no per-track colour tuning is needed to keep them from reading as a
// cut-out.
//
// `grass` is the track's own TrackTheme::grass, so a treeline never clashes
// with the ground it stands on.
std::vector<MeshVertex> buildTreeline(const std::array<double, 3>& grass, Mulberry32& rng);
