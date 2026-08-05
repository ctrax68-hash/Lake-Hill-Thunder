#pragma once

#include "track.h"

#include <array>

// From the JS TRACKS[] table (index.html:242-283): physics fields (Phase 1)
// plus theme/stadium visual dressing (Phase 5b), transcribed verbatim.
//
// G18 (NT2003 presentation plan) raised every track's `crowdTiers` to cover
// all of its tiers. It had sat at 2 (4 on Big Sable) since Phase 5e, so
// every tier above the second fell through buildStandMesh()'s flat-color
// branch and rendered as a blank painted panel -- an empty upper deck above
// a populated lower one, which reads as unfinished rather than as a real
// grandstand. The field stays meaningful (a track *could* leave upper tiers
// as bare structure); it just isn't the right look for any of these four.
//
// **Deliberately NOT changed: banking.** The plan proposed steepening
// Milltown toward Bristol's 24-30 degrees, but `bankL`/`bankR` are physics
// inputs, not just geometry -- `cornerCap(mu, bank)` (car.cpp:7) and
// step_car.cpp:400's `muEffLateral = (muEff + tan(bank)) / (1 -
// muEff*tan(bank))`. Going 14 -> 24 degrees would raise lateral grip by
// roughly 56% and rewrite the track's corner speeds and lap times. This
// pass was explicitly scoped to graphics, so the coliseum read comes from
// the stand geometry below (more tiers, taller risers, roof line) and the
// track drives exactly as before.
inline const std::array<TrackSpec, 4> TRACKS = {{
    // THUNDER OVAL (index.html:243-252)
    {"THUNDER OVAL", 140, 120, 18, 16, 392, 4, 90,
     TrackTheme{{1.0, 0.267, 0.0}, {0.176, 0.314, 0.086}},
     Stadium{StandTier{5, 3, 2}, 0.85, "partial", StandScale{3.2, 2.1}, 0.82, 12, 0, false, false, 0.25, 5,
             Sky{{0.78, 0.86, 0.94}, {0.20, 0.45, 0.85}, "none"}, Env{"noon-grass"},
             {{{0.75, 0.2, 0.2}, {0.2, 0.35, 0.75}, {0.85, 0.8, 0.25}, {0.8, 0.8, 0.82}, {0.25, 0.6, 0.3},
               {0.5, 0.3, 0.6}}}}},
    // MILLTOWN BULLRING (index.html:253-262)
    // G18: restyled into the reference's steep-walled coliseum bowl. At
    // 848m it is already within 1% of Bristol's half-mile and was already
    // the only track with uniform 360-degree seating (standTier{4,4,4},
    // standReach "full", standDensity 1.0), but 4 tiers of the shortest
    // risers in the game (tierH 1.6) made it a low, flat saucer. Now 9
    // tiers of 2.3-high risers wrapping the whole lap -- the grandstand
    // towers over the racing surface the way the reference footage does.
    // The drab brown/gray crowd palette went with it: the reference's
    // packed house reads bright and high-contrast, not muddy.
    {"MILLTOWN BULLRING", 100, 100, 14, 14, 110, 5, 50,
     TrackTheme{{0.88, 0.88, 0.90}, {0.15, 0.27, 0.08}},
     Stadium{StandTier{9, 9, 9}, 1.0, "full", StandScale{2.7, 2.3}, 0.97, 14, 0, false, false, 0.35, 9,
             Sky{{0.80, 0.85, 0.92}, {0.24, 0.42, 0.80}, "none"}, Env{"hazy-noon"},
             {{{0.80, 0.22, 0.20}, {0.20, 0.35, 0.78}, {0.90, 0.82, 0.28}, {0.88, 0.88, 0.90}, {0.25, 0.58, 0.32},
               {0.55, 0.35, 0.65}}},
             1.9}},  // G18: taller catch fence to match the taller bowl
    // CEDAR VALLEY (index.html:263-272)
    {"CEDAR VALLEY", 190, 160, 14, 12, 500, 4, 100,
     TrackTheme{{0.16, 0.38, 0.62}, {0.20, 0.33, 0.10}},
     Stadium{StandTier{3, 2, 1}, 0.55, "partial", StandScale{3.0, 1.9}, 0.55, 6, 6, false, false, 0.18, 3,
             Sky{{0.80, 0.88, 0.90}, {0.35, 0.55, 0.80}, "hills"}, Env{"sunset"},
             {{{0.65, 0.30, 0.15}, {0.70, 0.50, 0.20}, {0.40, 0.30, 0.55}, {0.30, 0.40, 0.35}, {0.75, 0.65, 0.35},
               {0.50, 0.20, 0.15}}}}},
    // BIG SABLE SPEEDWAY (index.html:273-282)
    {"BIG SABLE SPEEDWAY", 240, 240, 23, 23, 546, 5, 120,
     TrackTheme{{0.85, 0.12, 0.10}, {0.19, 0.34, 0.11}},
     Stadium{StandTier{10, 8, 6}, 0.95, "full", StandScale{4.0, 2.6}, 0.97, 0, 0, true, true, 0.30, 10,
             Sky{{0.75, 0.83, 0.93}, {0.18, 0.40, 0.80}, "none"}, Env{"dusk-lights"},
             {{{0.85, 0.15, 0.12}, {0.10, 0.25, 0.75}, {0.95, 0.85, 0.15}, {0.90, 0.90, 0.92}, {0.15, 0.15, 0.18},
               {0.55, 0.55, 0.60}}},
             2.2}},  // G10: taller catch fence -- this is the superspeedway
}};
