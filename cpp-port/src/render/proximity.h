#pragma once

#include "../sim/car.h"
#include "vertex.h"

#include <cstddef>
#include <vector>

// G22 (NT2003 presentation plan): the reference's spotter strip -- a row of
// chips along the bottom of the screen showing the cars racing right around
// you, each in its own colour with its own number. This port already has
// every input it needs (`Car::s`, `lat`, `num`, `col`), so this is purely a
// display feature; nothing in the sim changes.
//
// Split the same way minimap.h/leaderboard.h are: the selection logic is
// pure and lives here as buildProximityList(), unit-tested directly
// (proximity_test.cpp), while drawing is a separate function that appends
// into the frame's shared UI vertex list.

struct ProximityCar {
    int num;
    Color3 col;
    // Signed lateral offset from the player, in track units. Negative means
    // the car is on the side the track's `lat` axis calls negative -- the
    // strip is ordered by this so a car physically to one side of the player
    // appears on that side of the strip, which is the whole point of a
    // spotter display.
    double dLat;
    // Signed along-track offset, positive = ahead of the player. Already
    // wrapped to the shorter way around the circuit, so a car just across
    // the start/finish line from the player reads as "just ahead", not "a
    // whole lap behind".
    double dS;
};

// Cars within `sRange` track units ahead or behind the player, nearest
// first, capped at `maxCars`, then re-sorted left-to-right by `dLat` for
// display. Skips the player, cars that are out/DNF, and cars that have
// finished -- none of those are someone you can hit.
//
// `trackTotal` is Track::total(), needed to wrap the along-track difference
// (same "pass the one scalar rather than the whole Track" rationale as
// minimap.h/leaderboard.h).
std::vector<ProximityCar> buildProximityList(const std::vector<Car>& cars, const Car& player,
                                              double trackTotal, double sRange, std::size_t maxCars);

struct ProximityBox {
    float x, y, w, h;
};

// Draws the strip: a beveled plate, a centre tick marking the player's own
// lane, and one colour chip per nearby car with its number beside it. The
// number goes through bgfx::dbgTextPrintf() rather than being drawn into the
// chip, matching leaderboard.cpp's "colour chip + text" idiom -- a number
// painted inside the chip would be unreadable against half the field's
// liveries.
//
// `captionRow` is the dbgText row the numbers print on; the caller owns it
// for the same reason status_bars.cpp's base row is caller-supplied.
void drawProximity(const ProximityBox& box, int captionRow, const std::vector<ProximityCar>& near,
                    std::vector<PosColorVertex>& uiOut);
