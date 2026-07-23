#pragma once

#include <SDL_rect.h>
#include <string>

// Phase 4b (PORT_PROGRESS.md): the menu screen (index.html:167-184's
// `#menu` DOM overlay), adapted to this port's bgfx-debug-text-only
// rendering (same approach as hud.cpp's drawHud()) and touch_controls.h's
// region-hit-testing pattern (same idea as bL/bR/bG/bB/bP, just for menu
// rows instead of drive controls).
//
// Fields not stored here, because JS binds them directly to S.laps/S.tilt
// (index.html:4706-4709, 4703-4705) with immediate real-time effect, not
// deferred to a "Start" click -- this port's RaceState already has both
// fields (race_state.h), so the menu's laps/tilt rows read and write
// `RaceState::laps`/`RaceState::tilt` directly instead of duplicating them
// here, matching JS's own binding.
struct MenuSelection {
    int trackIdx = 0;

    // Stored for UI parity with index.html's #qualTog (default false,
    // index.html:513), but NOT wired to a real qualifying flow yet:
    // startQualifying()/finishQualifying()'s single-car-timed-lap-then-
    // AI-grid-rebuild logic (index.html:4640ish) is genuine unported sim
    // core work, out of scope for this UI-only sub-task (see race.h's own
    // tick() comment: only the mode='qual'->'menuwait' transition itself
    // is ported, not what puts the sim into 'qual' mode in the first
    // place). Toggling this in the menu is honest UI parity; pressing
    // Start currently always launches a normal race regardless of its
    // value -- see main.cpp's Start-button handler for the explicit note.
    bool qual = false;

    // Stored for UI parity with index.html's #sndTog/#volSlider (defaults
    // true/100, index.html:510). Inert: this port has no audio system at
    // all yet (Phase 6, not started, see PORT_PROGRESS.md) -- there is
    // nothing for these to control.
    bool sound = true;
    int volume = 100; // 0-100
};

struct MenuRegions {
    SDL_Rect trackBtn, lapsBtn, qualBtn, soundBtn, tiltBtn, volumeBar, startBtn;
};

// Fixed-pixel layout, top-left anchored -- same "reasonable first pass, not
// yet DPI/viewport adaptive" precedent as touch_controls.h's own regions.
// Row positions match exactly what drawMenu() prints at, so the clickable
// area lines up with the visible text.
MenuRegions computeMenuRegions();

// Cycles RaceState::laps through 3 -> 5 -> 10 -> 20 -> 3, matching JS's
// #lapTog handler (index.html:4706-4709) exactly.
int cycleLaps(int laps);

// Maps a click's x position within `bar` to a 0-100 volume value,
// clamped -- a click-to-set adaptation of JS's drag-based <input
// type=range>, which this port has no drag-slider widget to replicate.
int volumeFromClickX(const SDL_Rect& bar, int clickX);

// Draws the menu (title, track/laps/qualifying/sound/tilt rows, volume
// bar, start prompt) via bgfx::dbgTextPrintf(), matching hud.cpp's
// approach and text-mode palette. Caller must have already called
// bgfx::dbgTextClear() this frame (Renderer::renderFrame() already does,
// same contract as drawHud()).
void drawMenu(const MenuSelection& sel, int laps, bool tilt, const std::string& trackName);
