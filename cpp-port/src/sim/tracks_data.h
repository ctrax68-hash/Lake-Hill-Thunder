#pragma once

#include "track.h"

#include <array>

// Physics-relevant fields only, from the JS TRACKS[] table (index.html:242).
// Visual dressing (theme, stadium) is intentionally not ported -- see
// track.h's header comment.
inline const std::array<TrackSpec, 4> TRACKS = {{
    {"THUNDER OVAL", 140, 120, 18, 16, 392, 4, 90},
    {"MILLTOWN BULLRING", 100, 100, 14, 14, 110, 5, 50},
    {"CEDAR VALLEY", 190, 160, 14, 12, 500, 4, 100},
    {"BIG SABLE SPEEDWAY", 240, 240, 23, 23, 546, 5, 120},
}};
