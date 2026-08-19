#include "touch_controls.h"

namespace {
// index.html:19-20's --ctl* base pixel values (UI.scale===1).
constexpr int kCtlW = 88, kCtlH = 76, kCtlGasH = 96;
constexpr int kCtlGap = 14, kCtlPairGap = 10;
// N3: kCtlPitGap was 8 px. bP is the same width and column as bB (BRAKE), so
// eight pixels was the entire margin between "brake" and "commit to a pit
// stop" -- far less than the contact patch of a thumb, on the one control in
// the game with a punishing, previously-uncancellable consequence (the car
// goes auto-driven and speed-limited to 22 m/s = 49.2 mph until the stop
// completes). It was harmless only for as long as the button was inert, which
// it was on touch until the M2 dispatch fix landed. Raised to 40 px -- about
// half a button height of dead space -- and bP narrowed so it no longer forms
// a perfect column with BRAKE, giving the edge a second visual cue. The
// stacked-above-bB arrangement itself is kept (index.html:194-197 parity);
// the spacing is what was indefensible, not the layout.
// touch_buttons.cpp draws straight from these regions, so the visible rect
// follows automatically and cannot drift from the hit rect.
constexpr int kCtlPitH = 44, kCtlPitGap = 40, kCtlPitW = 72;
// Camera-toggle button: no JS CSS precedent for exact pixel size (its DOM
// button just sizes to its own text), so this is a new, reasonably-sized
// touch target rather than a ported constant.
constexpr int kCtlCamW = 72, kCtlCamH = 40;
} // namespace

TouchRegions computeTouchRegions(int windowW, int windowH) {
    TouchRegions r{};

    // bL: left=ctlGap, bottom=ctlGap (index.html:46).
    r.bL = {kCtlGap, windowH - kCtlGap - kCtlH, kCtlW, kCtlH};

    // bR: left=ctlGap+ctlW+ctlPairGap, bottom=ctlGap (index.html:47).
    r.bR = {kCtlGap + kCtlW + kCtlPairGap, windowH - kCtlGap - kCtlH, kCtlW, kCtlH};

    // bB: right=ctlGap+ctlW+ctlPairGap, bottom=ctlGap (index.html:48).
    r.bB = {windowW - (kCtlGap + kCtlW + kCtlPairGap) - kCtlW, windowH - kCtlGap - kCtlH, kCtlW, kCtlH};

    // bG: right=ctlGap, bottom=ctlGap, taller than the other three
    // (index.html:51).
    r.bG = {windowW - kCtlGap - kCtlW, windowH - kCtlGap - kCtlGasH, kCtlW, kCtlGasH};

    // bP: stacked above bB (index.html:194-197), but narrower than it and set
    // well clear of it -- see kCtlPitGap's comment for why the old 8px
    // shoulder-to-shoulder placement was a trap. Right edges stay aligned so
    // the column still reads as one control group.
    r.bP = {r.bB.x + (kCtlW - kCtlPitW), windowH - (kCtlGap + kCtlH + kCtlPitGap) - kCtlPitH,
            kCtlPitW, kCtlPitH};

    // bC: top-right corner, away from every drive control -- matches JS's
    // own CAM button being a separate top-of-screen element, not grouped
    // with the drive controls.
    r.bC = {windowW - kCtlGap - kCtlCamW, kCtlGap, kCtlCamW, kCtlCamH};

    return r;
}
