// Step 3 (PORT_PROGRESS.md, physics-driven car rig animation): exercises
// computeWheelTransforms()/computeBonePalette()'s pure joint-transform math,
// bgfx-free, same hand-rolled expect/expectNear pattern as tire_model_test.cpp.

#include "../src/render/wheel_animation.h"

#include <cmath>
#include <cstdio>

namespace {
int g_failures = 0;

void expect(bool cond, const char* what) {
    if (!cond) {
        std::fprintf(stderr, "FAIL: %s\n", what);
        ++g_failures;
    }
}

void expectNear(const char* label, double got, double expected, double tol = 1e-6) {
    if (std::fabs(got - expected) > tol) {
        std::fprintf(stderr, "%s: got %.9g expected %.9g (diff %.3g)\n", label, got, expected, got - expected);
        ++g_failures;
    }
}
} // namespace

int main() {
    // ---- computeWheelTransforms(): spin integrates v/wheelRadius*dt ----
    {
        WheelAnimState state;
        WheelAnimInputs in{10.0, 0.0, 0.0}; // v=10 m/s, no load (rest offsets both 0)
        auto wt = computeWheelTransforms(state, in, /*wheelRadius=*/0.3, /*restLoadFront=*/0.0,
                                          /*restLoadRear=*/0.0, /*loadToTravel=*/0.0001, /*maxTravel=*/0.08,
                                          /*dt=*/0.02);
        const double expectedAngle = (10.0 / 0.3) * 0.02;
        for (int i = 0; i < 4; ++i) {
            expectNear("wheel spin angle after one tick", wt[i].spinAngle, expectedAngle);
            expectNear("wheel spin angle stored in state", state.wheelAngle[i], expectedAngle);
        }
        // A second tick accumulates further (persistent state, not reset).
        auto wt2 = computeWheelTransforms(state, in, 0.3, 0.0, 0.0, 0.0001, 0.08, 0.02);
        expectNear("wheel spin angle after two ticks", wt2[0].spinAngle, expectedAngle * 2);
    }

    // ---- computeWheelTransforms(): suspension offset from Fz above rest,
    // front/rear independent, split evenly left/right ----
    {
        WheelAnimState state;
        WheelAnimInputs in{0.0, 8000.0, 6000.0};
        auto wt = computeWheelTransforms(state, in, 0.3, /*restLoadFront=*/3500.0, /*restLoadRear=*/2800.0,
                                          /*loadToTravel=*/0.0001, /*maxTravel=*/0.08, /*dt=*/0.0);
        const double expectedFront = (8000.0 * 0.5 - 3500.0) * 0.0001; // = 0.05
        const double expectedRear = (6000.0 * 0.5 - 2800.0) * 0.0001;  // = 0.02
        expectNear("FL suspension offset", wt[0].suspOffset, expectedFront);
        expectNear("FR suspension offset", wt[1].suspOffset, expectedFront);
        expectNear("RL suspension offset", wt[2].suspOffset, expectedRear);
        expectNear("RR suspension offset", wt[3].suspOffset, expectedRear);
    }

    // ---- computeWheelTransforms(): suspension offset clamps to maxTravel ----
    {
        WheelAnimState state;
        WheelAnimInputs in{0.0, 100000.0, 0.0}; // huge front load, way above rest
        auto wt = computeWheelTransforms(state, in, 0.3, 3500.0, 2800.0, 0.0001, /*maxTravel=*/0.08, 0.0);
        expectNear("suspension offset clamps to +maxTravel", wt[0].suspOffset, 0.08);
        // A load far *below* rest (e.g. wheel liftoff) should clamp the other way.
        WheelAnimState state2;
        WheelAnimInputs liftoff{0.0, 0.0, 0.0};
        auto wt2 = computeWheelTransforms(state2, liftoff, 0.3, 3500.0, 2800.0, 0.0001, 0.08, 0.0);
        expectNear("suspension offset clamps to -maxTravel", wt2[0].suspOffset, -0.08);
    }

    // ---- computeBonePalette(): 2-joint rig (root + one wheel joint bound
    // 1 unit along +X), same shape as tests/fixtures/skinned_wheel_test.gltf
    // -- verifies suspension-offset translation and spin rotation both
    // compose correctly with the bind pose + inverse bind matrix.
    {
        auto identity16 = [] {
            std::array<double, 16> m{};
            m[0] = m[5] = m[10] = m[15] = 1.0;
            return m;
        };
        auto translateX = [](double x) {
            std::array<double, 16> m{};
            m[0] = m[5] = m[10] = m[15] = 1.0;
            m[12] = x;
            return m;
        };

        std::vector<ImportedJoint> joints(2);
        joints[0].name = "root";
        joints[0].parent = -1;
        joints[0].localBindMatrix = identity16();
        joints[0].inverseBindMatrix = identity16();
        joints[1].name = "wheel_FL";
        joints[1].parent = 0;
        joints[1].localBindMatrix = translateX(1.0);
        joints[1].inverseBindMatrix = translateX(-1.0);

        // wheel 0 (FL) -> joint 1; wheels 1-3 unmapped (no such joints in
        // this minimal rig).
        const std::array<int, 4> wheelJointIndex = {1, -1, -1, -1};

        // Suspension-only case (spin=0): a mesh vertex authored at the
        // wheel's own bind position (world/mesh-space (1,0,0)) should end
        // up displaced straight up by suspOffset, staying at x=1.
        {
            std::array<WheelLocalTransform, 4> wt{};
            wt[0] = {0.0, 0.05};
            auto palette = computeBonePalette(joints, wt, wheelJointIndex);
            expect(palette.size() == 2, "palette has one matrix per joint");
            const auto& m0 = palette[0];
            expectNear("root skin matrix is identity (unaffected, unmapped)", m0[12], 0.0);
            expectNear("root skin matrix is identity (unaffected, unmapped)", m0[13], 0.0);
            const auto& m1 = palette[1];
            // Apply m1 to mesh-space point (1,0,0,1): x' = m1[0]*1+m1[12], etc.
            const double x1 = m1[0] * 1.0 + m1[12];
            const double y1 = m1[1] * 1.0 + m1[13];
            const double z1 = m1[2] * 1.0 + m1[14];
            expectNear("wheel hub x stays at bind position under suspension-only offset", x1, 1.0);
            expectNear("wheel hub moves up by suspOffset", y1, 0.05);
            expectNear("wheel hub z unaffected by suspension offset", z1, 0.0);
        }

        // Spin-only case (suspOffset=0): a mesh vertex 1 unit along local Z
        // from the hub should rotate around the wheel's local X axis.
        {
            std::array<WheelLocalTransform, 4> wt{};
            const double angle = M_PI / 2.0;
            wt[0] = {angle, 0.0};
            auto palette = computeBonePalette(joints, wt, wheelJointIndex);
            const auto& m1 = palette[1];
            // Apply m1 to mesh-space point (1,0,1,1) (1 unit along +Z from the hub).
            const double x1 = m1[0] * 1.0 + m1[8] * 1.0 + m1[12];
            const double y1 = m1[1] * 1.0 + m1[9] * 1.0 + m1[13];
            const double z1 = m1[2] * 1.0 + m1[10] * 1.0 + m1[14];
            expectNear("spin rotation keeps hub-relative x fixed", x1, 1.0);
            expectNear("spin rotation: point rotates -sin(angle) in y at 90deg", y1, -1.0, 1e-6);
            expectNear("spin rotation: point rotates cos(angle) in z at 90deg", z1, 0.0, 1e-6);
        }
    }

    if (g_failures == 0) {
        std::printf("wheel_animation_test: computeWheelTransforms/computeBonePalette all correct.\n");
        return 0;
    }
    std::fprintf(stderr, "wheel_animation_test: %d FAILURES.\n", g_failures);
    return 1;
}
