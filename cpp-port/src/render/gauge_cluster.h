#pragma once

#include "vertex.h"

#include <vector>

// G22 (NT2003 presentation plan): the analog tachometer, which is the
// reference HUD's strongest signature -- a sweeping needle over a dial,
// with the digital gear and speed beside it and a draft bar below them.
//
// Built entirely from ui_draw.h primitives that already existed or were
// added alongside it: pushFilledCircle() for the face, pushArc() for the
// sweep (added here -- pushRingOutline() is closed-circle only),
// pushLineSegment() for the ticks and the needle (it already extrudes an
// arbitrarily-oriented quad, so the needle came for free), pushSegBar() for
// the draft bar, and G21's pushSevenSegText() for the readouts. No new
// shader, no new vertex layout, no texture.
//
// **Painter order matters.** The UI list is submitted as one
// depth-test-free draw call, so append order *is* draw order: the face has
// to go down before the ticks, and the needle last of all. drawGaugeCluster()
// appends in that order internally; callers only need to know that whatever
// they append after this call paints on top of the gauge.

struct GaugeBox {
    float x, y, w, h;
};

// `rpm` and `gear` come from gear_rpm.h's gearRpm(), which is a pure
// function of Car::v -- not stored physics state -- so the gauge cannot
// disagree with the car it is reporting on. `speed` is Car::v and `draftF`
// is Car::draftF, both straight off the player's Car.
//
// L9: the speed readout is captioned MPH and converted from Car::v. This
// reverses an earlier claim here that Car::v carried no known unit -- the
// gear breakpoints (14/26/40/70) are not mph figures precisely because they
// are m/s, i.e. 31/58/89/157 mph, exactly a stock car's shift points. See
// gauge_cluster.cpp's own comment for why the raw number made the game read
// as half pace.
//
// `captionRow` is the dbgText row the "GEAR" caption prints on; "SPD"
// follows three rows below (the MPH readout) and "DRAFT" six. The caller owns it, for the same
// reason status_bars.cpp's base row is caller-supplied -- dbgText can only
// be addressed in whole 8x16 cells, so only the caller knows which rows its
// own layout has left free.
void drawGaugeCluster(const GaugeBox& box, int captionRow, double rpm, int gear, double speed,
                       double draftF, std::vector<PosColorVertex>& uiOut);
