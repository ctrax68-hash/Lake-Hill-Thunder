#pragma once

#include "track.h"

#include <array>

// From the JS TRACKS[] table (index.html:242-283): physics fields (Phase 1)
// plus theme/stadium visual dressing (Phase 5b), transcribed verbatim.
inline const std::array<TrackSpec, 4> TRACKS = {{
    // THUNDER OVAL (index.html:243-252)
    {"THUNDER OVAL", 140, 120, 18, 16, 392, 4, 90,
     TrackTheme{{1.0, 0.267, 0.0}, {0.176, 0.314, 0.086}},
     Stadium{StandTier{5, 3, 2}, 0.85, "partial", StandScale{3.2, 2.1}, 0.82, 12, 0, false, false, 0.25, 2,
             Sky{{0.78, 0.86, 0.94}, {0.20, 0.45, 0.85}, "none"}, Env{"noon-grass"},
             {{{0.75, 0.2, 0.2}, {0.2, 0.35, 0.75}, {0.85, 0.8, 0.25}, {0.8, 0.8, 0.82}, {0.25, 0.6, 0.3},
               {0.5, 0.3, 0.6}}}}},
    // MILLTOWN BULLRING (index.html:253-262)
    {"MILLTOWN BULLRING", 100, 100, 14, 14, 110, 5, 50,
     TrackTheme{{0.88, 0.88, 0.90}, {0.15, 0.27, 0.08}},
     Stadium{StandTier{4, 4, 4}, 1.0, "full", StandScale{2.4, 1.6}, 0.96, 14, 0, false, false, 0.35, 2,
             Sky{{0.80, 0.85, 0.92}, {0.24, 0.42, 0.80}, "none"}, Env{"hazy-noon"},
             {{{0.55, 0.35, 0.18}, {0.30, 0.30, 0.30}, {0.70, 0.55, 0.20}, {0.35, 0.45, 0.30}, {0.60, 0.25, 0.20},
               {0.45, 0.40, 0.55}}}}},
    // CEDAR VALLEY (index.html:263-272)
    {"CEDAR VALLEY", 190, 160, 14, 12, 500, 4, 100,
     TrackTheme{{0.16, 0.38, 0.62}, {0.20, 0.33, 0.10}},
     Stadium{StandTier{3, 2, 1}, 0.55, "partial", StandScale{3.0, 1.9}, 0.55, 6, 6, false, false, 0.18, 2,
             Sky{{0.80, 0.88, 0.90}, {0.35, 0.55, 0.80}, "hills"}, Env{"sunset"},
             {{{0.65, 0.30, 0.15}, {0.70, 0.50, 0.20}, {0.40, 0.30, 0.55}, {0.30, 0.40, 0.35}, {0.75, 0.65, 0.35},
               {0.50, 0.20, 0.15}}}}},
    // BIG SABLE SPEEDWAY (index.html:273-282)
    {"BIG SABLE SPEEDWAY", 240, 240, 23, 23, 546, 5, 120,
     TrackTheme{{0.85, 0.12, 0.10}, {0.19, 0.34, 0.11}},
     Stadium{StandTier{10, 8, 6}, 0.95, "full", StandScale{4.0, 2.6}, 0.97, 0, 0, true, true, 0.30, 4,
             Sky{{0.75, 0.83, 0.93}, {0.18, 0.40, 0.80}, "none"}, Env{"dusk-lights"},
             {{{0.85, 0.15, 0.12}, {0.10, 0.25, 0.75}, {0.95, 0.85, 0.15}, {0.90, 0.90, 0.92}, {0.15, 0.15, 0.18},
               {0.55, 0.55, 0.60}}}}},
}};
