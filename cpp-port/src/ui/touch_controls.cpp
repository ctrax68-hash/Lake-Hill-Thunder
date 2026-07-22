#include "touch_controls.h"

namespace {
// index.html:19-20's --ctl* base pixel values (UI.scale===1).
constexpr int kCtlW = 88, kCtlH = 76, kCtlGasH = 96;
constexpr int kCtlGap = 14, kCtlPairGap = 10;
constexpr int kCtlPitH = 44, kCtlPitGap = 8;
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

    // bP: same right offset as bB, stacked directly above it
    // (index.html:194-197).
    r.bP = {r.bB.x, windowH - (kCtlGap + kCtlH + kCtlPitGap) - kCtlPitH, kCtlW, kCtlPitH};

    return r;
}
