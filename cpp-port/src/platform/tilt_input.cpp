#include "tilt_input.h"

#include <cmath>

bool TiltInput::init() {
    const int n = SDL_NumSensors();
    for (int i = 0; i < n; ++i) {
        if (SDL_SensorGetDeviceType(i) == SDL_SENSOR_ACCEL) {
            sensor_ = SDL_SensorOpen(i);
            break;
        }
    }
    return sensor_ != nullptr;
}

void TiltInput::shutdown() {
    if (sensor_) {
        SDL_SensorClose(sensor_);
        sensor_ = nullptr;
    }
}

void TiltInput::update() {
    if (!sensor_) return;
    SDL_SensorUpdate();
    float v[3] = {0, 0, 0};
    if (SDL_SensorGetData(sensor_, v, 3) != 0) return;
    const double x = v[0], y = v[1], z = v[2];

    // SDL's accelerometer axes are fixed to the device's own physical frame
    // and are NOT remapped for the current display rotation (SDL_sensor.h's
    // own doc comment: "The accelerometer axis data is not changed when the
    // device is rotated."). JS's browser-based deviceorientation listener
    // has the same underlying property (beta/gamma are device-frame, not
    // screen-frame) -- which is exactly why index.html:1260-1264 explicitly
    // reads screen.orientation.angle and picks beta vs. gamma (with a sign
    // flip) depending on which way the device is physically rotated into
    // landscape. Do the same remap here using SDL_GetDisplayOrientation():
    //
    //   roll  = tilt around the device's own "up" axis (Y) -- this is the
    //           gamma-equivalent, i.e. left/right tilt AS EXPERIENCED IN
    //           PORTRAIT.
    //   pitch = tilt around the device's own "right" axis (X) -- the
    //           beta-equivalent, i.e. front/back tilt as experienced in
    //           portrait, which becomes the *visual* left/right tilt once
    //           the device is physically rotated 90 degrees into landscape.
    //
    // Both are standard gravity-vector tilt formulas over SDL's documented
    // at-rest axis convention (SDL_sensor.h: X right, Y up, Z toward the
    // user, for a device held in natural/portrait orientation).
    const double kRadToDeg = 180.0 / M_PI;
    const double roll = std::atan2(x, std::sqrt(y * y + z * z)) * kRadToDeg;
    const double pitch = std::atan2(-y, std::sqrt(x * x + z * z)) * kRadToDeg;

    // OPEN QUESTION (see PORT_PROGRESS.md): this branch mirrors JS's
    // `o===90 ? -beta : (o===-90||o===270) ? beta : gamma` structure, but
    // which of SDL_ORIENTATION_LANDSCAPE / _LANDSCAPE_FLIPPED corresponds to
    // the browser's angle===90 vs. angle===-90/270 is a genuine guess -- SDL
    // and the browser orientation APIs don't document a shared convention,
    // and there's no accelerometer hardware in this dev container to test
    // against. The dominant-axis logic (roll in portrait, pitch in
    // landscape) should be right; the overall sign in landscape is a single
    // flip away from correct if a real device shows the car steering
    // opposite to the physical tilt direction.
    const SDL_DisplayOrientation o = SDL_GetDisplayOrientation(0);
    if (o == SDL_ORIENTATION_LANDSCAPE) {
        tiltG_ = -pitch;
    } else if (o == SDL_ORIENTATION_LANDSCAPE_FLIPPED) {
        tiltG_ = pitch;
    } else {
        tiltG_ = roll;
    }
}
