#pragma once

// Shared physics constants (index.html:236): gravity and air density.
inline constexpr double G = 9.81;
inline constexpr double RHO = 1.225;

// Fixed physics timestep (index.html:679): 50Hz, deterministic.
inline constexpr double DT = 0.02;

// How long collide()'s spin-roll and the tire-blowout spin stay suppressed
// after ANY green flag (index.html:559). See the JS source's own extensive
// comment there for why this value specifically -- it's longer than the
// lane-ease duration on purpose, with margin for the staggered per-car
// restart release. Preserve that reasoning rather than re-deriving it if
// this ever needs revisiting.
inline constexpr double GREEN_LOCK_DUR = 28;
