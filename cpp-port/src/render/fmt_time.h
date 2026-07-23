#pragma once

#include <string>

// Phase 4c (PORT_PROGRESS.md): direct port of JS's fmtT() (index.html:3769-
// 3771), used for the LAST/BEST lap time HUD rows. Deliberately its own
// header/source with zero bgfx dependency -- unlike hud.cpp (which must
// link bgfx for dbgTextPrintf()), this is pure formatting logic and can be
// unit-tested directly, same "isolate pure logic from anything needing a
// live render context" precedent as touch_controls.h/menu.cpp's region
// math.
std::string fmtLapTime(double t);
