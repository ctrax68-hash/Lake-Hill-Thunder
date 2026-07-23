#pragma once

// Phase 4d (PORT_PROGRESS.md): direct port of JS's gearRpm(v) (index.html:
// 1393-1400) -- a shared gear/RPM breakpoint table JS uses to drive both
// its engine-audio pitch (audioTick()) and the HUD gear+RPM readout
// (drawSpeedModule()). Not stored car state: it's a pure function of
// speed, recomputed fresh every call. Phase 6c's src/audio/mixer.cpp now
// reuses this same function for the engine-audio pitch too (a deliberate
// cross-folder include -- this header has always been bgfx-free pure
// logic despite living under render/, same as track_surface.h/etc.),
// exactly mirroring how JS shares the one function between both call sites.
struct GearRpm {
    int gear;
    double rpm; // [0.25, 1.0]
};

GearRpm gearRpm(double v);
