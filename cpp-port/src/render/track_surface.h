#pragma once

#include "../sim/track.h"

// Phase 5a (PORT_PROGRESS.md): the JS "3D surface model (render only; physics
// stays planar)" block (index.html:377-395) -- pos3()/surfH() plus the
// WALL_LAT/APRON_IN constants and the camera-lean "up" vector JS derives
// from carBasis() (index.html:3064-3080). All pure functions of `const
// Track&`, zero bgfx dependency, reused by the camera (renderer.cpp) and by
// the stadium/pit-road mesh builders (Phase 5d).
//
// JS keeps these as free functions closing over a single global TRACK;
// this port has no such global, so every function takes `const Track&`
// explicitly -- a direct parameter-passing adaptation, not a behavior
// change (same rationale car.h's own header comment gives for cornerCap()).

struct Vec3 {
    double x = 0, y = 0, z = 0;
};

// WALL_LAT (index.html:379): the physics wall's lateral position -- also
// where the wall MESH's rendered surface sits (Phase 5d).
double wallLat(const Track& track);

// APRON_IN (index.html:380): the inside apron edge, physics-unrelated
// (render-only), used to clamp the camera/mesh generation to sane lateral
// bounds.
double apronIn(const Track& track);

// surfH() (index.html:385-389): banked-surface height at (s, lat) -- banking
// raises the +lat (outside) edge, inside apron edge sits at h=0 (+0.02).
double surfH(const Track& track, double s, double lat);

// pos3() (index.html:390-395): math (x,y) -> GL (x, h, z) at (s, lat).
Vec3 pos3(const Track& track, double s, double lat);

// The camera-lean "up" vector carBasis() derives from cross(latT, fw)
// (index.html:3064-3071), reduced to closed form here. This reduction is
// EXACT, not an approximation: the cross product only depends on the local
// heading (`th`) and `TRACK.bankAt(s)`, never on car pitch (`c.pitch`, which
// only perturbs `fw` in JS, not `up`) or any other part of the car's own
// model matrix -- so this is a faithful port of the up-vector math alone,
// valid even though this port has no 3D car body/model matrix to derive it
// from directly.
Vec3 surfaceUp(const Track& track, double s);
