#pragma once

// Step 3 (PORT_PROGRESS.md, physics-driven car rig animation): pure,
// bgfx-free computation of a car rig's per-wheel joint transforms from live
// per-car physics state (Step 1/1c's real per-axle Fz + forward speed),
// combined with the rig's own bind pose + joint hierarchy (Step 2's
// mesh_import.h ImportedJoint list) into the final bone-matrix palette
// SkinnedMesh uploads to vs_skinned.sc each frame. Deliberately mirrors
// this port's "pure logic vs GPU" split one more time: this file is
// entirely testable arithmetic, consumed by Renderer (which owns the
// per-car persistent WheelAnimState and the actual bgfx upload).
//
// Scope note: this port's tire model (car.h) is an axle-level bicycle
// model, not per-wheel -- there's no real per-wheel lateral load split to
// animate from, so each axle's Fz is simply halved between its own left/
// right wheels here. Similarly, there's no separate suspension-joint
// hierarchy in the placeholder rig this step ships with (car_rig_data.h) --
// a wheel joint's own local transform carries both its spin rotation and
// its suspension-travel translation, rather than splitting them across two
// joints. A richer, artist-authored rig (Step 2's "After this" note) could
// add a real suspension-strut joint per wheel without changing this
// function's contract, just by wiring computeWheelTransforms()'s
// suspOffset into a different joint than the spin angle.

#include "mesh_import.h"

#include <array>
#include <vector>

// Persistent per-car animation state -- wheel spin is an *integral* of
// angular velocity, not a pure function of instantaneous physics state, so
// it needs to persist between frames the same way c.vy/c.r does in the sim
// itself (just render-side state: nothing else in this port ever reads a
// wheel's spin angle back, so it doesn't belong in Car/car.h).
struct WheelAnimState {
    double wheelAngle[4] = {0, 0, 0, 0}; // FL, FR, RL, RR, radians, wraps freely
};

// One car's inputs for this frame -- already-computed real physics values,
// not re-derived here.
struct WheelAnimInputs {
    double v = 0;       // forward speed, m/s (Car::v)
    double fzFront = 0; // N, front axle load (Car::fzFront, already suspension-lagged per Step 1c)
    double fzRear = 0;  // N, rear axle load (Car::fzRear)
};

struct WheelLocalTransform {
    double spinAngle = 0;  // radians, about the wheel's own local spin axis
    double suspOffset = 0; // meters, + = compressed (wheel moves toward the chassis)
};

// Advances `state`'s 4 wheel spin angles by `dt` (angular velocity =
// v/wheelRadius, same for all 4 wheels -- this port's tire model has one
// shared forward speed, not independent per-wheel slip) and computes each
// wheel's suspension offset from how far its axle's *half*-share of Fz sits
// above that axle's static per-wheel rest load (`restLoadFront`/
// `restLoadRear`), scaled by `loadToTravel` and clamped to +-`maxTravel`.
// Order: FL, FR, RL, RR.
std::array<WheelLocalTransform, 4> computeWheelTransforms(WheelAnimState& state, const WheelAnimInputs& in,
                                                           double wheelRadius, double restLoadFront,
                                                           double restLoadRear, double loadToTravel,
                                                           double maxTravel, double dt);

// Combines a rig's joints (bind pose + parent hierarchy) with each wheel's
// extra local transform into the final skinning matrix palette --
// worldJoint[i] = worldJoint[parent[i]] * translate(0,suspOffset,0) *
// bindLocal[i] * rotateX(spinAngle) (identity extra transform for non-wheel
// joints), skinMatrix[i] = worldJoint[i] * inverseBindMatrix[i]. Requires
// joints to be listed parent-before-child (true for any rig this port's
// mesh_import.h can produce, since cgltf's own node array is always parsed
// in that order for a tree with no forward references). Column-major 4x4
// each, ready for SkinnedMesh::setBoneMatrices(). `wheelJointIndex[i]` maps
// wheel i (FL/FR/RL/RR) to its joint index in `joints` (-1 = rig has no
// such wheel joint, e.g. a rig without a full 4-wheel set).
std::vector<std::array<double, 16>> computeBonePalette(const std::vector<ImportedJoint>& joints,
                                                        const std::array<WheelLocalTransform, 4>& wheelTransforms,
                                                        const std::array<int, 4>& wheelJointIndex);
