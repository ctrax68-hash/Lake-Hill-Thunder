#include "stadium_mesh.h"

#include "track_surface.h"

#include "../sim/car.h" // FIELD, pitStallS()

#include <algorithm>
#include <cmath>

namespace {

// COL (index.html:1725-1734) -- only the entries this sub-phase's geometry
// actually uses; the rest (asphalt/groove/apron/lineY/etc.) belong to the
// track-surface loop, out of scope here (see stadium_mesh.h's own comment).
constexpr std::array<double, 3> kPitLine{0.95, 0.80, 0.15};
constexpr std::array<double, 3> kPitWall{0.86, 0.86, 0.88};
constexpr std::array<double, 3> kPitStall{0.80, 0.16, 0.14};
constexpr std::array<double, 3> kTire{0.05, 0.05, 0.06};
constexpr std::array<double, 3> kChrome{0.75, 0.77, 0.80};

Vec3 crossPt(const Track& track, double s, double lat, double raise = 0.0) {
    Vec3 p = pos3(track, s, lat);
    p.y += raise;
    return p;
}

std::array<double, 3> mixC(const std::array<double, 3>& a, const std::array<double, 3>& b, double f) {
    return {a[0] + (b[0] - a[0]) * f, a[1] + (b[1] - a[1]) * f, a[2] + (b[2] - a[2]) * f};
}

// tri()/quad() (index.html:1665-1691): each triangle gets its own flat
// normal from its own two edge vectors (not a shared per-quad normal) --
// matching JS's Builder.prototype.tri exactly, including which two
// triangles a quad splits into (a,b,c) + (a,c,d).
void addTri(std::vector<MeshVertex>& out, const Vec3& a, const Vec3& b, const Vec3& c,
            const std::array<double, 3>& color) {
    const double ux = b.x - a.x, uy = b.y - a.y, uz = b.z - a.z;
    const double vx = c.x - a.x, vy = c.y - a.y, vz = c.z - a.z;
    double nx = uy * vz - uz * vy, ny = uz * vx - ux * vz, nz = ux * vy - uy * vx;
    const double len = std::sqrt(nx * nx + ny * ny + nz * nz);
    if (len > 1e-9) {
        nx /= len;
        ny /= len;
        nz /= len;
    }
    out.push_back({a.x, a.y, a.z, nx, ny, nz, color});
    out.push_back({b.x, b.y, b.z, nx, ny, nz, color});
    out.push_back({c.x, c.y, c.z, nx, ny, nz, color});
}

void addQuad(std::vector<MeshVertex>& out, const Vec3& a, const Vec3& b, const Vec3& c, const Vec3& d,
             const std::array<double, 3>& color) {
    addTri(out, a, b, c, color);
    addTri(out, a, c, d, color);
}

// box() (index.html:1697-1705): 6 axis-aligned faces, each already wound to
// face outward.
void addBox(std::vector<MeshVertex>& out, double x0, double y0, double z0, double x1, double y1, double z1,
            const std::array<double, 3>& color) {
    auto p = [](double x, double y, double z) { return Vec3{x, y, z}; };
    addQuad(out, p(x0, y1, z0), p(x1, y1, z0), p(x1, y1, z1), p(x0, y1, z1), color); // top
    addQuad(out, p(x0, y0, z1), p(x1, y0, z1), p(x1, y0, z0), p(x0, y0, z0), color); // bottom
    addQuad(out, p(x0, y0, z0), p(x1, y0, z0), p(x1, y1, z0), p(x0, y1, z0), color); // -z
    addQuad(out, p(x1, y0, z1), p(x0, y0, z1), p(x0, y1, z1), p(x1, y1, z1), color); // +z
    addQuad(out, p(x1, y0, z0), p(x1, y0, z1), p(x1, y1, z1), p(x1, y1, z0), color); // +x
    addQuad(out, p(x0, y0, z1), p(x0, y0, z0), p(x0, y1, z0), p(x0, y1, z1), color); // -x
}

} // namespace

std::vector<MeshVertex> buildStandMesh(const Track& track, double sStart, double sEnd, int tiers,
                                        double density, double tierD, double tierH,
                                        const std::array<std::array<double, 3>, 6>& palette, Mulberry32& rng) {
    std::vector<MeshVertex> out;
    const double zoneLen = sEnd - sStart;
    const int steps = std::min(40, std::max(16, (int)std::lround(zoneLen / 12.0)));
    const double baseLat = wallLat(track) + 6.0, baseH = 1.2;

    for (int i = 0; i < steps; ++i) {
        if (density < 1.0 && rng.next() > density) continue; // gaps/tunnels
        const double sa = sStart + zoneLen * i / steps, sb = sStart + zoneLen * (i + 1) / steps;
        for (int t = 0; t < tiers; ++t) {
            const double latB = baseLat + t * tierD, latT = latB + tierD;
            const double hB = baseH + t * tierH, hT = hB + tierH;
            const double riseH = hB + tierH * 0.28;
            // Riser (near-vertical, stepped): reversed vertex order (matches
            // JS's own already-fixed backface-culling winding for stands).
            const Vec3 ra = crossPt(track, sa, latB, hB), rb = crossPt(track, sb, latB, hB);
            const Vec3 rc = crossPt(track, sb, latB, riseH), rd = crossPt(track, sa, latB, riseH);
            addQuad(out, rd, rc, rb, ra, mixC(palette[(size_t)(rng.next() * palette.size())], {0, 0, 0}, 0.3));
            // Seat (sloped). Phase 5d simplification: always the flat
            // palette path, regardless of crowdTiers -- that field only
            // selects which tiers get the crowd-tile texture once Phase 5e's
            // atlas exists; meaningless before then (see this file's own
            // header comment).
            const Vec3 sA = crossPt(track, sa, latB, riseH), sB = crossPt(track, sb, latB, riseH);
            const Vec3 sC = crossPt(track, sb, latT, hT), sD = crossPt(track, sa, latT, hT);
            addQuad(out, sD, sC, sB, sA, palette[(size_t)(rng.next() * palette.size())]);
        }
    }
    return out;
}

std::vector<MeshVertex> buildPitRoadMesh(const Track& track, double pitOut, double pitIn) {
    std::vector<MeshVertex> out;
    const Seg& seg0 = track.segs()[0];
    const double s0 = seg0.s0;

    // Entry/exit lines (index.html:1838-1842).
    const double sOutS = s0 + seg0.len * 0.97;
    for (double s : {s0 - 20.0, sOutS}) {
        addQuad(out, crossPt(track, s, pitOut, 0.015), crossPt(track, s, pitIn, 0.015),
                crossPt(track, s + 1.0, pitIn, 0.015), crossPt(track, s + 1.0, pitOut, 0.015), kPitLine);
    }

    // Low pit wall (index.html:1846-1851).
    const double wallLatPit = pitOut - 0.35, wallH = 0.55;
    const int wallSteps = 28;
    for (int i = 0; i < wallSteps; ++i) {
        const double sa = s0 + seg0.len * (0.02 + 0.93 * i / wallSteps);
        const double sb = s0 + seg0.len * (0.02 + 0.93 * (i + 1) / wallSteps);
        addQuad(out, crossPt(track, sa, wallLatPit), crossPt(track, sb, wallLatPit),
                crossPt(track, sb, wallLatPit, wallH), crossPt(track, sa, wallLatPit, wallH), kPitWall);
    }

    // Numbered stall box outlines + war wagon/tire stacks/pit sign per car
    // (index.html:1852-1904). Digit painting (addNumber) and crew-figure
    // billboards are deferred -- see this file's own header comment.
    const double t = 0.12, boxLen = 3.2, boxW = 2.6, stallLat = -10.5;
    for (int idx = 0; idx < FIELD; ++idx) {
        const double sStall = s0 + seg0.len * (0.18 + 0.55 * idx / FIELD);
        const double lat0 = stallLat - boxW / 2, lat1 = stallLat + boxW / 2;
        const double sb0 = sStall - boxLen / 2, sb1 = sStall + boxLen / 2;
        addQuad(out, crossPt(track, sb0, lat0, 0.012), crossPt(track, sb1, lat0, 0.012),
                crossPt(track, sb1, lat0 + t, 0.012), crossPt(track, sb0, lat0 + t, 0.012), kPitStall);
        addQuad(out, crossPt(track, sb0, lat1 - t, 0.012), crossPt(track, sb1, lat1 - t, 0.012),
                crossPt(track, sb1, lat1, 0.012), crossPt(track, sb0, lat1, 0.012), kPitStall);
        addQuad(out, crossPt(track, sb0, lat0, 0.012), crossPt(track, sb0 + t, lat0, 0.012),
                crossPt(track, sb0 + t, lat1, 0.012), crossPt(track, sb0, lat1, 0.012), kPitStall);
        addQuad(out, crossPt(track, sb1 - t, lat0, 0.012), crossPt(track, sb1, lat0, 0.012),
                crossPt(track, sb1, lat1, 0.012), crossPt(track, sb1 - t, lat1, 0.012), kPitStall);

        // tanX/tanZ/latX/latZ sampled at sb0-0.6 (index.html:1862-1863),
        // reused below for the war wagon box too -- a minor pre-existing
        // JS approximation (heading barely changes over this small an s
        // delta), kept faithfully rather than "corrected".
        const PointResult p = track.pointAt(sb0 - 0.6);
        const double tanX = std::cos(p.hdg), tanZ = std::sin(p.hdg);
        const double latX = -std::sin(p.hdg), latZ = std::cos(p.hdg);

        const Vec3 wag = pos3(track, sStall, -13.0);
        const double wx = tanX, wz = tanZ;
        addBox(out, wag.x - std::abs(wx) * 0.8 - std::abs(latX) * 0.45, 0,
               wag.z - std::abs(wz) * 0.8 - std::abs(latZ) * 0.45,
               wag.x + std::abs(wx) * 0.8 + std::abs(latX) * 0.45, 1.4,
               wag.z + std::abs(wz) * 0.8 + std::abs(latZ) * 0.45, {0.16, 0.16, 0.19});
        addBox(out, wag.x - std::abs(wx) * 0.45 - std::abs(latX) * 0.3, 1.4,
               wag.z - std::abs(wz) * 0.45 - std::abs(latZ) * 0.3,
               wag.x + std::abs(wx) * 0.45 + std::abs(latX) * 0.3, 1.9,
               wag.z + std::abs(wz) * 0.45 + std::abs(latZ) * 0.3, kChrome);

        const Vec3 ts1 = pos3(track, sb0 - 0.3, -12.2), ts2 = pos3(track, sb1 + 0.3, -12.2);
        addBox(out, ts1.x - 0.28, 0, ts1.z - 0.28, ts1.x + 0.28, 1.05, ts1.z + 0.28, kTire);
        addBox(out, ts2.x - 0.28, 0, ts2.z - 0.28, ts2.x + 0.28, 0.55, ts2.z + 0.28, kTire);

        const Vec3 sp = pos3(track, sb1 + 0.4, -8.2);
        addBox(out, sp.x - 0.05, 0, sp.z - 0.05, sp.x + 0.05, 2.4, sp.z + 0.05, kChrome);
    }

    // Small pit-control building at the entry end (index.html:1906-1908).
    const Vec3 pcx = pos3(track, s0 - 24, pitOut - 4);
    addBox(out, pcx.x - 2.2, 0, pcx.z - 1.6, pcx.x + 2.2, 3.4, pcx.z + 1.6, kPitWall);

    return out;
}

std::vector<MeshVertex> buildOuterWallMesh(const Track& track) {
    std::vector<MeshVertex> out;
    const int N = 460;
    const double dS = track.total() / N;
    const double WALL_H = 1.35;
    const double wl = wallLat(track);
    const auto& wallColor = track.theme().wall;
    for (int i = 0; i < N; ++i) {
        const double s0 = i * dS, s1 = (i + 1) * dS;
        const Vec3 w00 = crossPt(track, s0, wl), w01 = crossPt(track, s1, wl);
        const Vec3 w10 = crossPt(track, s0, wl, WALL_H), w11 = crossPt(track, s1, wl, WALL_H);
        // Reversed winding (w10,w11,w01,w00), matching JS's own fix so the
        // face is visible from the track side, not just from outside.
        addQuad(out, w10, w11, w01, w00, wallColor);
    }
    return out;
}
