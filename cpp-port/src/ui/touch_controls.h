#pragma once

// Phase 3a (PORT_PROGRESS.md): touch/click input regions matching the JS
// original's on-screen button layout (index.html:19,46-51,194-198 -- the
// `--ctl*` CSS custom properties and `#bL`/`#bR`/`#bB`/`#bG`/`#bP` rules).
// This is input recognition only -- there is no visible on-screen button
// drawn yet, that's Phase 4's job ("UI overlay ... hand-rolled quads+text").
//
// Fixed pixel sizes at JS's base (UI.scale===1) values, not yet DPI/viewport
// adaptive the way the JS's own UI.scale is -- a reasonable first pass, not
// full parity; revisit if a real mobile device's touch targets feel wrong.

#include <SDL_rect.h>

struct TouchRegions {
    SDL_Rect bL, bR, bB, bG, bP;
};

// Computes the five button regions for a `windowW` x `windowH` window,
// using the same relative layout as the JS CSS (bL/bR bottom-left steer
// pair, bB/bG bottom-right brake/gas pair, bP stacked above bB).
TouchRegions computeTouchRegions(int windowW, int windowH);

inline bool pointInRect(int x, int y, const SDL_Rect& r) {
    return x >= r.x && x < r.x + r.w && y >= r.y && y < r.y + r.h;
}
