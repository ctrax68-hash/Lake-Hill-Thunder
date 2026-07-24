#pragma once

// Phase 3a (PORT_PROGRESS.md): touch/click input regions matching the JS
// original's on-screen button layout (index.html:19,46-51,194-198 -- the
// `--ctl*` CSS custom properties and `#bL`/`#bR`/`#bB`/`#bG`/`#bP` rules).
// Roadmap Phase 0 (PORT_PROGRESS.md) added the matching visible quad+dbgText
// drawing (src/render/touch_buttons.h) for all of these regions -- this file
// stays the pure region math, same "isolate region math from drawing" split
// its own header comment always called for.
//
// Fixed pixel sizes at JS's base (UI.scale===1) values, not yet DPI/viewport
// adaptive the way the JS's own UI.scale is -- a reasonable first pass, not
// full parity; revisit if a real mobile device's touch targets feel wrong.

#include <SDL_rect.h>

struct TouchRegions {
    SDL_Rect bL, bR, bB, bG, bP;
    // Camera-mode toggle (top-right corner, away from the drive controls) --
    // added alongside the default-camera fix (Renderer::cameraMode_ now
    // defaults to Chase, not the orthographic TopDown) so touch/mobile
    // players without a keyboard can still reach the `c` key's toggle,
    // mirroring JS's own always-visible "CAM: <mode>" button (index.html
    // ~5259-5266), which this port had no equivalent of until now.
    SDL_Rect bC;
};

// Computes the six button regions for a `windowW` x `windowH` window,
// using the same relative layout as the JS CSS (bL/bR bottom-left steer
// pair, bB/bG bottom-right brake/gas pair, bP stacked above bB, bC in the
// top-right corner).
TouchRegions computeTouchRegions(int windowW, int windowH);

inline bool pointInRect(int x, int y, const SDL_Rect& r) {
    return x >= r.x && x < r.x + r.w && y >= r.y && y < r.y + r.h;
}
