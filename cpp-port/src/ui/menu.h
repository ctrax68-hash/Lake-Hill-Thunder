#pragma once

#include "../render/font_atlas.h"
#include "../render/vertex.h"

#include <SDL_rect.h>
#include <string>
#include <vector>

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

// Advances MenuSelection::trackIdx to the next track, wrapping. `trackCount`
// is TRACKS.size(), passed in rather than read here so this stays free of a
// tracks_data.h dependency (menu.cpp is linked into menu_test, which has no
// reason to pull the track table in).
//
// M2: extracted from main.cpp's handleMenuClick() so the 4-cycle is actually
// testable. The arithmetic was never wrong -- one tap invoked it twice (see
// isSyntheticTouchMouse() in touch_controls.h), so the selector advanced by 2
// and half the tracks were unreachable. A helper with a test pins the cycle
// itself, which is the half that belongs in this file.
int cycleTrack(int trackIdx, int trackCount);

// Maps a click's x position within `bar` to a 0-100 volume value,
// clamped -- a click-to-set adaptation of JS's drag-based <input
// type=range>, which this port has no drag-slider widget to replicate.
int volumeFromClickX(const SDL_Rect& bar, int clickX);

// G26: where the menu's title band puts the game name and the build stamp.
//
// Split out of drawMenu() for the same reason computeMenuRegions() is:
// drawMenu() calls bgfx::dbgTextPrintf() for the bar labels, which asserts
// outright without a live bgfx context, so it cannot be called from a test
// at all. The header's placement, though, is exactly the kind of arithmetic
// that goes wrong quietly -- the first version of it drew the title straight
// through the build stamp -- so it lives here, pure and checkable.
//
// `stamp` is the full string to be drawn (LHT_BUILD_STAMP with its prefix),
// passed in because its width depends on the build hash and timestamp and
// therefore cannot be known at authoring time. All coordinates are pixels;
// `baseline` is the text baseline both strings sit on.
struct MenuHeaderLayout {
    float titleSize, stampSize;
    float titleX, stampX;
    float baseline;
    float bandWidth, bandHeight;
};
MenuHeaderLayout computeMenuHeader(const std::string& title, const std::string& stamp);

// Draws the menu (title, track/laps/qualifying/sound/tilt rows, volume
// bar, start prompt) via bgfx::dbgTextPrintf() for labels plus beveled
// quad panels behind each row (G24, NT2003 presentation plan) -- matching
// hud.cpp's dbgText approach and status_bars.cpp's "quads underneath,
// dbgText labels on top" idiom. Caller must have already called
// bgfx::dbgTextClear() this frame (Renderer::renderFrame() already does,
// same contract as drawHud()). `uiOut` is the frame's UI-quad vertex list
// (Renderer::renderFrame()'s `uiVerts`, same one drawHud()/drawResults()
// already append into).
//
// G26: `textOut`, when non-null, is the frame's font-atlas text-vertex list
// (Renderer::renderFrame()'s `textVerts`). Optional rather than required
// because the atlas decode can fail at startup, and a menu that falls back
// to the debug font is a far better outcome than one with no title at all --
// the renderer passes null in exactly that case.
void drawMenu(const MenuSelection& sel, int laps, bool tilt, const std::string& trackName,
              std::vector<PosColorVertex>& uiOut,
              std::vector<PosColorUvVertex>* textOut = nullptr);
