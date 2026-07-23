#pragma once

#include "digit_mesh.h" // MeshVertex, PutFn, addNumber
#include "../sim/track.h"

#include <vector>

// Port of index.html:2116-2153's scoring-pylon + jumbotron geometry (Big
// Sable only, `stadium.pylon`/`stadium.jumbotron`) -- Phase 5g
// (PORT_PROGRESS.md). Numeric content is real 3D digit geometry
// (digit_mesh.h), not atlas-textured, matching JS's own choice: "a
// segmented-LED look, matching a real pylon... a static build-time
// snapshot, not a per-frame texture repaint." Content is the first 6 (pylon)
// / 5 (jumbotron) ROSTER car numbers -- a static roster snapshot, same
// simplification JS itself already made (it's not a live leaderboard feed
// either). Each function returns an empty mesh when the track's
// `stadium.pylon`/`stadium.jumbotron` flag is false (every track but Big
// Sable), so callers can invoke both unconditionally.
std::vector<MeshVertex> buildPylonMesh(const Track& track);
std::vector<MeshVertex> buildJumbotronMesh(const Track& track);
