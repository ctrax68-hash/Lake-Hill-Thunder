#include "track_surface.h"

#include <algorithm>
#include <cmath>

double wallLat(const Track& track) {
    return track.halfW() + 6.0;
}

double apronIn(const Track& track) {
    return -(track.halfW() + 8.0);
}

double surfH(const Track& track, double s, double lat) {
    const double b = track.bankAt(s);
    const double l = std::max(apronIn(track), std::min(wallLat(track), lat));
    return std::max(0.0, l + track.halfW()) * std::tan(b) + 0.02;
}

Vec3 pos3(const Track& track, double s, double lat) {
    const PointResult p = track.pointAt(s);
    const double nx = -std::sin(p.hdg), ny = std::cos(p.hdg);
    const double b = track.bankAt(s), cb = std::cos(b);
    return {p.x + nx * lat * cb, surfH(track, s, lat), p.y + ny * lat * cb};
}

Vec3 surfaceUp(const Track& track, double s) {
    const PointResult p = track.pointAt(s);
    const double th = p.hdg;
    const double b = track.bankAt(s);
    const double sb = std::sin(b), cb = std::cos(b);
    // cross(latT, fw) where fw=(cos(th),0,sin(th)), latT=(-sin(th)*cb, sb,
    // cos(th)*cb) (index.html:3067-3069) -- already unit length (sb^2+cb^2
    // = 1), so no normalize() needed.
    Vec3 up{sb * std::sin(th), cb, -sb * std::cos(th)};
    // JS flips if up.y<0; never triggers for this game's bank range (b is
    // always well under 90 degrees, so cb>0), kept for faithfulness.
    if (up.y < 0) {
        up.x = -up.x;
        up.y = -up.y;
        up.z = -up.z;
    }
    return up;
}
