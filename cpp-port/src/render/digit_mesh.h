#pragma once

#include "stadium_mesh.h" // MeshVertex
#include "track_surface.h" // Vec3

#include <array>
#include <functional>
#include <vector>

// Port of JS's 7-segment LED-digit geometry (index.html:2177-2201's
// addDigitQuads()/addNumber()) -- the segmented-LED look used by Big
// Sable's scoring pylon/jumbotron (Phase 5g, PORT_PROGRESS.md). Pure
// geometry, no texture: real quads are emitted per lit segment, positioned
// in 3D via a caller-supplied `put(x,y)` projection -- a direct port of
// JS's own closure-over-tanX/tanZ/latX/latZ idiom (index.html:2129-2131,
// 2144), reproduced here as a std::function so this header stays free of
// any Track/pos3() dependency (pylon_mesh.cpp supplies the projection).

using PutFn = std::function<Vec3(double x, double y)>;

// addDigitQuads() (index.html:2178-2192): one digit (0-9), `(ox,oy)` is its
// local-space origin, `(w,h)` its footprint. Un-lit segments emit nothing.
void addDigitQuads(std::vector<MeshVertex>& out, int n, double ox, double oy, double w, double h,
                    const PutFn& put, const std::array<double, 3>& col);

// addNumber() (index.html:2193-2201): a multi-digit number, centered at
// local x=0, digits packed left-to-right with a small gap.
void addNumber(std::vector<MeshVertex>& out, int num, const PutFn& put, double w, double h,
               const std::array<double, 3>& col);
