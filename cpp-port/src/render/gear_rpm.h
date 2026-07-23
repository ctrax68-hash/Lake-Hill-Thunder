#pragma once

// Phase 4d (PORT_PROGRESS.md): direct port of JS's gearRpm(v) (index.html:
// 1393-1400) -- a shared gear/RPM breakpoint table JS uses to drive both
// its engine-audio pitch (audioTick()) and the HUD gear+RPM readout
// (drawSpeedModule()). Not stored car state: it's a pure function of
// speed, recomputed fresh every call -- this port has no audio system
// yet, so only the HUD side is ported here.
struct GearRpm {
    int gear;
    double rpm; // [0.25, 1.0]
};

GearRpm gearRpm(double v);
