#pragma once

#include <SDL.h>

// Populates RaceState::tiltG (race_state.h:41) from a real accelerometer via
// SDL2's SDL_Sensor API, mirroring JS's `deviceorientation` listener
// (index.html:1260-1264) which feeds S.tiltG from the browser's
// DeviceOrientationEvent. There is no DeviceOrientationEvent equivalent on
// desktop/SDL, so this wraps SDL_SENSOR_ACCEL instead -- see tilt_input.cpp's
// own comment for the axis-mapping approach and its open sign ambiguity
// (logged in PORT_PROGRESS.md, not silently assumed correct).
class TiltInput {
public:
    // Opens the first available accelerometer sensor. Returns false (and
    // leaves available() false) if none is present -- desktop Linux dev
    // machines, including this container, legitimately have no such sensor;
    // that is not an error condition, just as a desktop browser with no
    // motion sensor simply never fires `deviceorientation` in the JS original.
    bool init();
    void shutdown();
    bool available() const { return sensor_ != nullptr; }

    // Call once per frame before reading tiltG().
    void update();

    // Degrees, intended to match RaceState::tiltG's scale (stepCar's player
    // branch divides by 22 and clamps to [-1,1], same as JS).
    double tiltG() const { return tiltG_; }

private:
    SDL_Sensor* sensor_ = nullptr;
    double tiltG_ = 0;
};
