#include "gauge_cluster.h"
#include "color.h"
#include "ui_draw.h"

#include <bgfx/bgfx.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <string>

namespace {

constexpr float kCellW = 8.0f, kCellH = 16.0f;
constexpr uint8_t kWhite = 15, kBlack = 0; // dbgText VGA palette, matching hud.cpp

uint8_t attr(uint8_t fg, uint8_t bg) {
    return (uint8_t)((bg << 4) | fg);
}

int colAt(float x) { return (int)(x / kCellW); }

constexpr float kPi = 3.14159265358979323846f;
constexpr float kDeg = kPi / 180.0f;

// The dial's sweep, in ui_draw.h's y-down convention (0 = right, 90 = down,
// increasing = clockwise on screen). 150 -> 390 degrees starts at the lower
// left, climbs over the top and finishes at the lower right -- the standard
// tachometer sweep, and it leaves the bottom 120 degrees of the face
// permanently clear of the needle.
constexpr float kSweepStart = 150.0f * kDeg;
constexpr float kSweepEnd = 390.0f * kDeg;

// Where the dial turns red. The reference's tach redlines in roughly the
// last fifth of its sweep.
constexpr float kRedlineFrac = 0.80f;

float sweepAngle(double frac) {
    const float f = (float)std::clamp(frac, 0.0, 1.0);
    return kSweepStart + (kSweepEnd - kSweepStart) * f;
}

} // namespace

void drawGaugeCluster(const GaugeBox& box, int captionRow, double rpm, int gear, double speed,
                       double draftF, std::vector<PosColorVertex>& uiOut) {
    const uint32_t segOff = packColor(Theme::kSteel, 0.55f);

    pushBevelPanel(uiOut, box.x, box.y, box.w, box.h, packColor(Theme::kBlack, 0.62f),
                   packColor(Theme::kGraycool, 0.5f), packColor(Theme::kSteel, 0.95f));

    // --- dial -------------------------------------------------------------
    // Sized off the panel height so the cluster scales as one piece.
    const float r = box.h * 0.44f;
    const float cx = box.x + 12.0f + r;
    const float cy = box.y + box.h / 2.0f;

    // Face, then bezel, then the sweep -- append order is draw order in the
    // UI list, so the face has to go down before anything that sits on it.
    pushFilledCircle(uiOut, cx, cy, r, packColor(Theme::kBlack, 0.80f), 40);
    pushRingOutline(uiOut, cx, cy, r, 2.0f, packColor(Theme::kGraycool, 0.75f), 40);

    // Unlit track, then the redline band over its top end, then the lit
    // portion up to the needle -- three arcs at one radius, back to front.
    const float trackR = r * 0.84f;
    const float trackW = r * 0.11f;
    pushArc(uiOut, cx, cy, trackR, kSweepStart, kSweepEnd, trackW, packColor(Theme::kSteel, 0.9f));
    pushArc(uiOut, cx, cy, trackR, sweepAngle(kRedlineFrac), kSweepEnd, trackW,
            packColor(Theme::kRed, 0.55f));

    const float needleA = sweepAngle(rpm);
    if (rpm > 0.0) {
        pushArc(uiOut, cx, cy, trackR, kSweepStart, needleA, trackW,
                rpm >= kRedlineFrac ? packColor(Theme::kRed) : packColor(Theme::kYellow));
    }

    // Tick marks across the sweep, majors twice as long.
    constexpr int kTicks = 9;
    for (int i = 0; i < kTicks; ++i) {
        const float f = (float)i / (float)(kTicks - 1);
        const float a = sweepAngle(f);
        const bool major = (i % 2) == 0;
        const float r0 = r * (major ? 0.56f : 0.62f), r1 = r * 0.70f;
        pushLineSegment(uiOut, cx + r0 * std::cos(a), cy + r0 * std::sin(a), cx + r1 * std::cos(a),
                        cy + r1 * std::sin(a), major ? 2.5f : 1.5f,
                        f >= kRedlineFrac ? packColor(Theme::kRed) : packColor(Theme::kGraycool, 0.9f));
    }

    // Needle and hub last, so they sit over everything on the face.
    const float needleR = r * 0.76f;
    pushLineSegment(uiOut, cx, cy, cx + needleR * std::cos(needleA), cy + needleR * std::sin(needleA), 3.0f,
                    packColor(Theme::kRed));
    pushFilledCircle(uiOut, cx, cy, r * 0.10f, packColor(Theme::kGraycool), 16);

    // --- readout column ---------------------------------------------------
    // To the right of the dial, where the needle can never cross it.
    const float colX = cx + r + 14.0f;
    const float rightX = box.x + box.w - 12.0f;

    bgfx::dbgTextPrintf(colAt(colX), captionRow, attr(kWhite, kBlack), "GEAR");
    {
        const std::string txt = std::to_string(std::max(1, gear));
        const float w = measureSevenSegText(20.0f, 4.0f, txt);
        pushSevenSegText(uiOut, rightX - w, (float)captionRow * kCellH + 18.0f, 20.0f, 30.0f, 4.0f, txt,
                         packColor(Theme::kWhite), segOff);
    }

    // "SPD", not "MPH": Car::v is the simulation's own speed unit -- the
    // ported gear table breaks at 14/26/40/70 (gear_rpm.cpp, index.html:1392),
    // which are not mile-per-hour figures -- so labelling it MPH would be
    // stating a unit this number does not carry. The JS original's own HUD
    // prints it unqualified for the same reason.
    bgfx::dbgTextPrintf(colAt(colX), captionRow + 3, attr(kWhite, kBlack), "SPD");
    {
        char buf[8];
        std::snprintf(buf, sizeof(buf), "%d", (int)std::lround(std::max(0.0, speed)));
        const std::string txt(buf);
        const float w = measureSevenSegText(20.0f, 4.0f, txt);
        pushSevenSegText(uiOut, rightX - w, (float)(captionRow + 3) * kCellH + 18.0f, 20.0f, 30.0f, 4.0f, txt,
                         packColor(Theme::kWhite), segOff);
    }

    // Draft, as a labelled bar rather than a second ring inside the dial.
    // Tire wear deliberately isn't here: it already has a labelled bar in the
    // left column (status_bars.cpp), and an unlabelled duplicate inside the
    // tach would be decoration, not information -- the same reasoning that
    // took SPD/GEAR *out* of the left column once this cluster showed them.
    // Draft is the one signal that had no display at all before G22.
    bgfx::dbgTextPrintf(colAt(colX), captionRow + 6, attr(kWhite, kBlack), "DRAFT");
    {
        const float barY = (float)(captionRow + 6) * kCellH + 18.0f;
        const float barW = rightX - colX;
        pushSegBar(uiOut, colX, barY, barW, 10.0f, std::clamp(draftF, 0.0, 1.0), 6,
                   packColor(Theme::kBlue), packColor(Theme::kSteel));
    }
}
