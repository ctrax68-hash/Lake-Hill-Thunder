#pragma once

#include "stadium_mesh.h" // MeshVertex
#include "../sim/rng.h"

#include <vector>

// Port of index.html:1910-1919's addHillSilhouette() -- Cedar Valley's
// distant low-poly hill ring (`stadium.sky.silhouette=='hills'`), Phase 5g
// (PORT_PROGRESS.md). A ring of 48 radial quads at R=1400 (well past the
// grass extent, index.html:2044's EXT=900) centered on the world origin --
// same as JS, which builds this in world space, not relative to any
// per-track offset. Corner heights are randomized per quad; colored with a
// fixed sky/grass-ish blend (mixC([.72,.80,.90],[.30,.42,.32],0.4)) so it
// reads as atmospheric background depth rather than a hard cutout -- this
// is independent of any per-track theme/sky data, matching JS exactly.
// `rng` is the same scenery-only Mulberry32(777) stream the rest of
// buildWorld()'s decorative geometry uses (stands/atlas/sky clouds) --
// cosmetic only, already-blessed "safe to diverge" precedent elsewhere in
// this port.
std::vector<MeshVertex> buildHillSilhouette(Mulberry32& rng);
