#include "wheel_animation.h"

#include <algorithm>
#include <cmath>

namespace {

using Mat4 = std::array<double, 16>;

Mat4 mat4Identity() {
    Mat4 m{};
    m[0] = m[5] = m[10] = m[15] = 1.0;
    return m;
}

Mat4 mat4Translate(double x, double y, double z) {
    Mat4 m = mat4Identity();
    m[12] = x;
    m[13] = y;
    m[14] = z;
    return m;
}

// Rotation about the local X axis (a wheel's own spin axis).
Mat4 mat4RotateX(double angle) {
    Mat4 m = mat4Identity();
    const double c = std::cos(angle), s = std::sin(angle);
    m[5] = c;
    m[6] = s;
    m[9] = -s;
    m[10] = c;
    return m;
}

// Column-major 4x4 multiply: result = a * b.
Mat4 mat4Mul(const Mat4& a, const Mat4& b) {
    Mat4 r{};
    for (int col = 0; col < 4; ++col) {
        for (int row = 0; row < 4; ++row) {
            double sum = 0;
            for (int k = 0; k < 4; ++k) sum += a[k * 4 + row] * b[col * 4 + k];
            r[col * 4 + row] = sum;
        }
    }
    return r;
}

} // namespace

std::array<WheelLocalTransform, 4> computeWheelTransforms(WheelAnimState& state, const WheelAnimInputs& in,
                                                           double wheelRadius, double restLoadFront,
                                                           double restLoadRear, double loadToTravel,
                                                           double maxTravel, double dt) {
    const double omega = in.v / std::max(0.05, wheelRadius);
    for (double& angle : state.wheelAngle) angle += omega * dt;

    const double frontOffset =
        std::clamp((in.fzFront * 0.5 - restLoadFront) * loadToTravel, -maxTravel, maxTravel);
    const double rearOffset = std::clamp((in.fzRear * 0.5 - restLoadRear) * loadToTravel, -maxTravel, maxTravel);

    return {{
        {state.wheelAngle[0], frontOffset}, // FL
        {state.wheelAngle[1], frontOffset}, // FR
        {state.wheelAngle[2], rearOffset},  // RL
        {state.wheelAngle[3], rearOffset},  // RR
    }};
}

std::vector<std::array<double, 16>> computeBonePalette(const std::vector<ImportedJoint>& joints,
                                                        const std::array<WheelLocalTransform, 4>& wheelTransforms,
                                                        const std::array<int, 4>& wheelJointIndex) {
    const size_t n = joints.size();
    std::vector<Mat4> worldJoint(n);
    std::vector<Mat4> skinMatrix(n);

    // wheelForJoint[j] = index into wheelTransforms (0-3), or -1 if joint j
    // isn't a wheel joint.
    std::vector<int> wheelForJoint(n, -1);
    for (int w = 0; w < 4; ++w) {
        if (wheelJointIndex[w] >= 0 && (size_t)wheelJointIndex[w] < n) wheelForJoint[wheelJointIndex[w]] = w;
    }

    for (size_t i = 0; i < n; ++i) {
        Mat4 extra = mat4Identity();
        if (wheelForJoint[i] >= 0) {
            const WheelLocalTransform& wt = wheelTransforms[wheelForJoint[i]];
            extra = mat4Mul(mat4Translate(0.0, wt.suspOffset, 0.0), mat4RotateX(wt.spinAngle));
        }
        const Mat4 jointLocal = mat4Mul(joints[i].localBindMatrix, extra);
        const Mat4 parentWorld = joints[i].parent >= 0 ? worldJoint[joints[i].parent] : mat4Identity();
        worldJoint[i] = mat4Mul(parentWorld, jointLocal);
        skinMatrix[i] = mat4Mul(worldJoint[i], joints[i].inverseBindMatrix);
    }

    return std::vector<std::array<double, 16>>(skinMatrix.begin(), skinMatrix.end());
}
