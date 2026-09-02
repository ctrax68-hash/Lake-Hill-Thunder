#include "stadium_mesh.h"

#include "atlas_texture.h"
#include "digit_mesh.h" // G12: addNumber() for turn-signage numbers
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
constexpr std::array<double, 3> kCheckerWhite{0.92, 0.92, 0.92};
constexpr std::array<double, 3> kCheckerBlack{0.05, 0.05, 0.06};

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

// quadUV() (index.html:1692-1696): same two-triangle split, but each
// vertex carries a UV instead of a baked color (Phase 5e's textured-lit
// pipeline samples the atlas texture for color instead).
void addQuadUV(std::vector<MeshVertex>& out, const Vec3& a, const Vec3& b, const Vec3& c, const Vec3& d,
               const std::array<double, 2>& ua, const std::array<double, 2>& ub, const std::array<double, 2>& uc,
               const std::array<double, 2>& ud) {
    auto tri = [&](const Vec3& p0, const Vec3& p1, const Vec3& p2, const std::array<double, 2>& t0,
                   const std::array<double, 2>& t1, const std::array<double, 2>& t2) {
        const double ux = p1.x - p0.x, uy = p1.y - p0.y, uz = p1.z - p0.z;
        const double vx = p2.x - p0.x, vy = p2.y - p0.y, vz = p2.z - p0.z;
        double nx = uy * vz - uz * vy, ny = uz * vx - ux * vz, nz = ux * vy - uy * vx;
        const double len = std::sqrt(nx * nx + ny * ny + nz * nz);
        if (len > 1e-9) {
            nx /= len;
            ny /= len;
            nz /= len;
        }
        out.push_back({p0.x, p0.y, p0.z, nx, ny, nz, {1, 1, 1}, t0[0], t0[1]});
        out.push_back({p1.x, p1.y, p1.z, nx, ny, nz, {1, 1, 1}, t1[0], t1[1]});
        out.push_back({p2.x, p2.y, p2.z, nx, ny, nz, {1, 1, 1}, t2[0], t2[1]});
    };
    tri(a, b, c, ua, ub, uc);
    tri(a, c, d, ua, uc, ud);
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

StandMeshResult buildStandMesh(const Track& track, double sStart, double sEnd, int tiers, int crowdTiers,
                                double density, double tierD, double tierH,
                                const std::array<std::array<double, 3>, 6>& palette,
                                const std::array<double, 4>& crowdUV, Mulberry32& rng) {
    StandMeshResult result;
    const double zoneLen = sEnd - sStart;
    const int steps = std::min(40, std::max(16, (int)std::lround(zoneLen / 12.0)));
    const double baseLat = wallLat(track) + 6.0, baseH = 1.2;
    const std::array<double, 2> uv00{crowdUV[0], crowdUV[3]}, uv10{crowdUV[2], crowdUV[3]};
    const std::array<double, 2> uv11{crowdUV[2], crowdUV[1]}, uv01{crowdUV[0], crowdUV[1]};

    for (int i = 0; i < steps; ++i) {
        if (density < 1.0 && rng.next() > density) continue; // gaps/tunnels
        const double sa = sStart + zoneLen * i / steps, sb = sStart + zoneLen * (i + 1) / steps;
        for (int t = 0; t < tiers; ++t) {
            const double latB = baseLat + t * tierD, latT = latB + tierD;
            const double hB = baseH + t * tierH, hT = hB + tierH;
            const double riseH = hB + tierH * 0.28;
            // Riser (near-vertical, stepped): reversed vertex order (matches
            // JS's own already-fixed backface-culling winding for stands).
            // Always flat-colored, even for textured front tiers -- JS's
            // own addStand() only textures the seat quad, never the riser.
            const Vec3 ra = crossPt(track, sa, latB, hB), rb = crossPt(track, sb, latB, hB);
            const Vec3 rc = crossPt(track, sb, latB, riseH), rd = crossPt(track, sa, latB, riseH);
            // G18: risers are structure, not people. They used to take a
            // random crowd-palette color darkened 30%, which was survivable
            // at 2-4 tiers but turns a 9-tier bowl into a stack of garish
            // horizontal stripes that completely overpower the crowd
            // texture between them. Real grandstand risers are concrete;
            // only the seated crowd is colorful. Keeps a slight per-slice
            // tint of the track's own palette so the stands don't go
            // uniformly flat.
            constexpr std::array<double, 3> kRiserConcrete{0.48, 0.48, 0.47};
            addQuad(result.flat, rd, rc, rb, ra,
                    mixC(kRiserConcrete, palette[(size_t)(rng.next() * palette.size())], 0.15));
            // Seat (sloped): front `crowdTiers` tiers get the textured
            // crowd-atlas path (index.html:1816-1818); the rest keep the
            // flat palette path (index.html:1819-1821, "fog hides banding
            // up there").
            if (t < crowdTiers) {
                // G18 (NT2003 presentation plan) -- BUGFIX. This used to map
                // the whole crowd rect onto the seat quad exactly once, with
                // no tiling and no aspect correction, so the painted people
                // stretched or squashed with whatever `zoneLen / steps`
                // happened to be: ~6 units per slice on Milltown's short
                // front straight vs ~13 on Big Sable's, i.e. the same crowd
                // tile covering twice the seating. Crowd density visibly
                // varied by track and by zone for no reason.
                //
                // Fixed by repeating the tile along the slice instead: the
                // rect is a *sub-region* of the shared atlas, so hardware
                // wrap can't do it (U>1 would wrap into neighbouring atlas
                // regions) -- it has to be one quad per repeat. kCrowdTile
                // is the world-space width one tile of painted crowd
                // represents, so people now come out the same size
                // everywhere.
                constexpr double kCrowdTile = 7.0;
                const int nTile = std::max(1, (int)std::lround((sb - sa) / kCrowdTile));
                for (int j = 0; j < nTile; ++j) {
                    const double ta0 = sa + (sb - sa) * j / nTile;
                    const double ta1 = sa + (sb - sa) * (j + 1) / nTile;
                    const Vec3 qA = crossPt(track, ta0, latB, riseH), qB = crossPt(track, ta1, latB, riseH);
                    const Vec3 qC = crossPt(track, ta1, latT, hT), qD = crossPt(track, ta0, latT, hT);
                    addQuadUV(result.textured, qD, qC, qB, qA, uv01, uv11, uv10, uv00);
                }
            } else {
                const Vec3 sA = crossPt(track, sa, latB, riseH), sB = crossPt(track, sb, latB, riseH);
                const Vec3 sC = crossPt(track, sb, latT, hT), sD = crossPt(track, sa, latT, hT);
                addQuad(result.flat, sD, sC, sB, sA, palette[(size_t)(rng.next() * palette.size())]);
            }
        }
    }
    return result;
}

std::vector<MeshVertex> buildStandRoofMesh(const Track& track, double sStart, double sEnd, int tiers, double tierD,
                                            double tierH, const std::array<double, 3>& accent) {
    std::vector<MeshVertex> out;
    if (tiers <= 0) return out;
    // Sits directly on top of the last tier buildStandMesh() emits, derived
    // from the same baseLat/baseH + per-tier stepping rather than restating
    // the numbers (same "derive, don't duplicate" rule G14's suite tower
    // follows for its own placement).
    const double latTop = wallLat(track) + 6.0 + tiers * tierD;
    const double hTop = 1.2 + tiers * tierH;
    // Kept deliberately thin, and with **no overhanging roof cap**. A first
    // pass added an angled cap projecting back over the seating; from track
    // level the camera sits under it, so it read as a huge dark slab hanging
    // over the bowl -- and wherever `standDensity` had punched a slice gap in
    // the seating below, sky showed through beneath it. The reference bowl is
    // open-air anyway: above the crowd there is only a painted band and a
    // narrow press-box fascia, no roof projecting over the stands. Together
    // these come to ~2.0 units against an 11.7-unit stand on Thunder Oval,
    // about 17% of its height.
    constexpr double kStripeH = 0.7;  // sponsor band immediately above the crowd
    constexpr double kFasciaH = 1.3;  // dark press-box fascia above that
    const std::array<double, 3> fascia{0.16, 0.16, 0.19};
    const std::array<double, 3> stripeDark = mixC(accent, {0, 0, 0}, 0.35);
    // Deliberately continuous even where standDensity punched slice gaps in
    // the seating below: a real grandstand roof spans its access tunnels
    // rather than breaking over each one.
    const double zoneLen = sEnd - sStart;
    const int steps = std::min(60, std::max(20, (int)std::lround(zoneLen / 10.0)));
    for (int i = 0; i < steps; ++i) {
        const double sa = sStart + zoneLen * i / steps, sb = sStart + zoneLen * (i + 1) / steps;
        // Alternating sponsor band, mirroring the reference's painted stripe
        // running the length of the grandstand fascia.
        const auto& stripeCol = (i % 2 == 0) ? accent : stripeDark;
        addQuad(out, crossPt(track, sa, latTop, hTop + kStripeH), crossPt(track, sb, latTop, hTop + kStripeH),
                crossPt(track, sb, latTop, hTop), crossPt(track, sa, latTop, hTop), stripeCol);
        // Dark fascia above the stripe.
        addQuad(out, crossPt(track, sa, latTop, hTop + kStripeH + kFasciaH),
                crossPt(track, sb, latTop, hTop + kStripeH + kFasciaH),
                crossPt(track, sb, latTop, hTop + kStripeH), crossPt(track, sa, latTop, hTop + kStripeH), fascia);
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
        // G19 (NT2003 presentation plan): war wagons were all one dark tone,
        // so pit road read as a row of identical black boxes. Real pit lanes
        // are a wall of team colors, and the reference footage's pit road is
        // one of its most colorful areas. Cycled by stall index so each team
        // is visually distinct but the assignment stays stable.
        static const std::array<std::array<double, 3>, 8> kTeamColors{{
            {0.72, 0.16, 0.14}, {0.14, 0.28, 0.68}, {0.90, 0.74, 0.12}, {0.16, 0.46, 0.24},
            {0.86, 0.86, 0.88}, {0.55, 0.22, 0.62}, {0.90, 0.48, 0.10}, {0.16, 0.16, 0.19},
        }};
        const auto& teamCol = kTeamColors[(size_t)idx % kTeamColors.size()];
        addBox(out, wag.x - std::abs(wx) * 0.8 - std::abs(latX) * 0.45, 0,
               wag.z - std::abs(wz) * 0.8 - std::abs(latZ) * 0.45,
               wag.x + std::abs(wx) * 0.8 + std::abs(latX) * 0.45, 1.4,
               wag.z + std::abs(wz) * 0.8 + std::abs(latZ) * 0.45, teamCol);
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

std::vector<MeshVertex> buildPitCrewMesh(const Track& track) {
    std::vector<MeshVertex> out;
    const Seg& seg0 = track.segs()[0];
    const double s0 = seg0.s0;
    const std::array<double, 4> crewUV = atlasUV(kAtlasCrew);
    // Same stall spacing buildPitRoadMesh() uses, so a crew lands in every
    // stall it draws a box for. Standing on the track side of the stall box
    // (its lat1 edge), upright and facing the track -- which is both what a
    // real crew does and the orientation the chase camera sees face-on while
    // running past pit road.
    constexpr double kStallLat = -10.5, kBoxW = 2.6;
    constexpr double kCrewW = 2.4, kCrewH = 1.75;
    const double crewLat = kStallLat + kBoxW / 2;
    for (int idx = 0; idx < FIELD; ++idx) {
        const double sStall = s0 + seg0.len * (0.18 + 0.55 * idx / FIELD);
        const Vec3 a = crossPt(track, sStall - kCrewW / 2, crewLat);
        const Vec3 b = crossPt(track, sStall + kCrewW / 2, crewLat);
        const Vec3 aTop{a.x, a.y + kCrewH, a.z};
        const Vec3 bTop{b.x, b.y + kCrewH, b.z};
        // V=0 is the top of the crew region (helmets), V=1 the bottom (feet)
        // -- see atlas_texture.cpp's paintCrewTile(), which lays each figure
        // out head-down-to-feet across the region's height.
        addQuadUV(out, aTop, bTop, b, a, {crewUV[0], crewUV[1]}, {crewUV[2], crewUV[1]}, {crewUV[2], crewUV[3]},
                  {crewUV[0], crewUV[3]});
    }
    return out;
}

// G5b (NASCAR-Thunder gap-analysis plan, stadium wall/sponsor atlas
// wiring): the wall diamond pattern (kAtlasWall) was already painted by
// atlas_texture.cpp's paintWallPattern() but never sampled by any mesh --
// confirmed dead via grep before this change. Since the wall is a single
// quad strip (unlike the stands, which split front tiers textured / back
// tiers flat), there's no flat/textured split needed here: the whole wall
// just gets real UVs instead of a flat color, no new WallMeshResult-style
// struct. U wraps `s / kWallTileLength` into the atlas region's own
// [u0,u1) range in software (same technique renderer.cpp's ribbon build
// already uses for the asphalt texture) so hardware texture wrap never
// needs to see a value outside this one region; V spans the wall's own
// height (0 at the ground, 1 at the top).
std::vector<MeshVertex> buildOuterWallMesh(const Track& track) {
    std::vector<MeshVertex> out;
    const int N = 460;
    const double dS = track.total() / N;
    const double WALL_H = 1.35;
    const double wl = wallLat(track);
    const std::array<double, 4> wallUV = atlasUV(kAtlasWall);
    constexpr double kWallTileLength = 6.0;
    auto wrapU = [&](double s) {
        double t = std::fmod(s / kWallTileLength, 1.0);
        if (t < 0.0) t += 1.0;
        return wallUV[0] + t * (wallUV[2] - wallUV[0]);
    };
    for (int i = 0; i < N; ++i) {
        const double s0 = i * dS, s1 = (i + 1) * dS;
        const Vec3 w00 = crossPt(track, s0, wl), w01 = crossPt(track, s1, wl);
        const Vec3 w10 = crossPt(track, s0, wl, WALL_H), w11 = crossPt(track, s1, wl, WALL_H);
        const double u0 = wrapU(s0), u1 = wrapU(s1);
        const double vTop = wallUV[1], vBottom = wallUV[3];
        // Reversed winding (w10,w11,w01,w00), matching JS's own fix so the
        // face is visible from the track side, not just from outside.
        addQuadUV(out, w10, w11, w01, w00, {u0, vTop}, {u1, vTop}, {u1, vBottom}, {u0, vBottom});
    }
    return out;
}

// G10 (NASCAR-Thunder gap-analysis plan): the catch fence -- kAtlasFence's
// crosshatch was painted since Phase 5e but never sampled by any mesh
// (confirmed via grep), the same "already-painted-but-dead atlas content"
// situation G5b found and fixed for the wall/sponsor regions. A thin
// vertical band directly above the wall face, same per-slice loop and
// wrapU-into-atlas-region technique as buildOuterWallMesh() just above.
std::vector<MeshVertex> buildCatchFenceMesh(const Track& track, double fenceHeight) {
    std::vector<MeshVertex> out;
    const int N = 460;
    const double dS = track.total() / N;
    constexpr double kWallH = 1.35; // matches buildOuterWallMesh()'s own WALL_H
    const double wl = wallLat(track);
    const std::array<double, 4> fenceUV = atlasUV(kAtlasFence);
    // G28: 11 m per tile, up from 5. The atlas band carries ~33 diamonds
    // across its width, so 11 m puts them at roughly 33 cm horizontally --
    // matching the ~34 cm the same band gives vertically over a real fence
    // height. At 5 m the mesh was visibly stretched taller than it was wide.
    constexpr double kFenceTileLength = 11.0;
    auto wrapU = [&](double s) {
        double t = std::fmod(s / kFenceTileLength, 1.0);
        if (t < 0.0) t += 1.0;
        return fenceUV[0] + t * (fenceUV[2] - fenceUV[0]);
    };
    for (int i = 0; i < N; ++i) {
        const double s0 = i * dS, s1 = (i + 1) * dS;
        const Vec3 f00 = crossPt(track, s0, wl, kWallH), f01 = crossPt(track, s1, wl, kWallH);
        const Vec3 f10 = crossPt(track, s0, wl, kWallH + fenceHeight), f11 = crossPt(track, s1, wl, kWallH + fenceHeight);
        const double u0 = wrapU(s0), u1 = wrapU(s1);
        const double vTop = fenceUV[1], vBottom = fenceUV[3];
        // Same reversed winding as the wall face, visible from the track side.
        addQuadUV(out, f10, f11, f01, f00, {u0, vTop}, {u1, vTop}, {u1, vBottom}, {u0, vBottom});
    }
    return out;
}

// G29 (graphics pass): the infield, which had nothing in it at all.
//
// Every frame of the reference footage shows the inside of the track carrying
// as much structure as the outside -- a garage row, haulers parked behind it,
// a white multi-storey suite block, light towers. Ours was bare grass from the
// apron to the horizon, and on a left-turning oval the infield is in shot for
// most of the lap, so it is the single largest empty area in the game.
//
// All of it is axis-aligned boxes placed off crossPt(), the same approach
// buildFlagStandMesh()/buildSuiteTowerMesh() already take: the front straight
// is straight, so no heading rotation is needed for anything sitting along it,
// and a box is the right primitive for buildings that are, in fact, boxes.
//
// Everything is appended into the one static scenery buffer, so this costs
// triangles, not draw calls -- measured at ~1,900 triangles, against a car
// mesh that is 1,322 each.
std::vector<MeshVertex> buildInfieldMesh(const Track& track, Mulberry32& rng) {
    std::vector<MeshVertex> out;
    constexpr std::array<double, 3> kGarageWall{0.74, 0.75, 0.77};
    constexpr std::array<double, 3> kGarageRoof{0.38, 0.39, 0.42};
    constexpr std::array<double, 3> kDoor{0.30, 0.33, 0.38};
    constexpr std::array<double, 3> kTowerLeg{0.34, 0.35, 0.38};
    constexpr std::array<double, 3> kLampBank{0.86, 0.86, 0.80};
    constexpr std::array<double, 3> kSuite{0.88, 0.89, 0.91};
    constexpr std::array<double, 3> kGlass{0.30, 0.42, 0.52};

    const Seg& seg0 = track.segs()[0];

    // --- garage row, along the front straight, well inside pit road --------
    // Pit road occupies lat -7.2..-11.8 (buildPitRoadMesh), so -34 leaves a
    // clear paddock lane between the two rather than crowding the stalls.
    constexpr double kGarageLat = -34.0;
    constexpr double kBayW = 6.0, kBayD = 9.0, kBayH = 4.4;
    const int bays = 12;
    const double gs0 = seg0.s0 + seg0.len * 0.20;
    for (int i = 0; i < bays; ++i) {
        const double sc = gs0 + i * (kBayW + 0.6);
        const Vec3 c = crossPt(track, sc, kGarageLat);
        addBox(out, c.x - kBayW / 2, c.y, c.z - kBayD / 2, c.x + kBayW / 2, c.y + kBayH, c.z + kBayD / 2,
               kGarageWall);
        // A flat roof slab overhanging the walls, which is what actually
        // makes a row of boxes read as buildings rather than as crates.
        addBox(out, c.x - kBayW / 2 - 0.4, c.y + kBayH, c.z - kBayD / 2 - 0.4, c.x + kBayW / 2 + 0.4,
               c.y + kBayH + 0.35, c.z + kBayD / 2 + 0.4, kGarageRoof);
        // Roll-up door on the track-facing side.
        addQuad(out, Vec3{c.x - kBayW / 2 + 0.7, c.y, c.z + kBayD / 2 + 0.42},
                Vec3{c.x + kBayW / 2 - 0.7, c.y, c.z + kBayD / 2 + 0.42},
                Vec3{c.x + kBayW / 2 - 0.7, c.y + kBayH * 0.72, c.z + kBayD / 2 + 0.42},
                Vec3{c.x - kBayW / 2 + 0.7, c.y + kBayH * 0.72, c.z + kBayD / 2 + 0.42}, kDoor);
    }

    // --- haulers parked behind the garages --------------------------------
    // Long trailer boxes, alternating light and dark so the row does not read
    // as one solid wall. Team colours would be better still, but the crowd
    // palette is the only per-track colour set here and it is authored for
    // seats, not vehicles -- using it would tint the haulers to match the
    // grandstand, which is worse than neutral.
    constexpr double kHaulLat = -50.0;
    constexpr double kHaulW = 2.9, kHaulD = 15.0, kHaulH = 4.0;
    for (int i = 0; i < 9; ++i) {
        const double sc = gs0 + 4.0 + i * (kHaulW + 2.2);
        const Vec3 c = crossPt(track, sc, kHaulLat);
        const double shade = 0.62 + 0.30 * rng.next();
        const std::array<double, 3> body{shade, shade, shade * 1.02};
        addBox(out, c.x - kHaulW / 2, c.y, c.z - kHaulD / 2, c.x + kHaulW / 2, c.y + kHaulH, c.z + kHaulD / 2,
               body);
        // Cab, a shorter box at one end.
        addBox(out, c.x - kHaulW / 2, c.y, c.z + kHaulD / 2, c.x + kHaulW / 2, c.y + kHaulH * 0.62,
               c.z + kHaulD / 2 + 2.6, kGarageRoof);
    }

    // --- infield suite / media block --------------------------------------
    // The reference's white multi-storey block with window bands. Same
    // "horizontal glass stripes over a light box" treatment
    // buildSuiteTowerMesh() uses outside the track, so the two read as the
    // same facility from either side.
    {
        const Vec3 c = crossPt(track, seg0.s0 + seg0.len * 0.62, -44.0);
        constexpr double kW = 22.0, kD = 11.0, kH = 13.0;
        addBox(out, c.x - kW / 2, c.y, c.z - kD / 2, c.x + kW / 2, c.y + kH, c.z + kD / 2, kSuite);
        for (int f = 1; f <= 3; ++f) {
            const double y = c.y + kH * (f / 4.0);
            addQuad(out, Vec3{c.x - kW / 2, y, c.z + kD / 2 + 0.05},
                    Vec3{c.x + kW / 2, y, c.z + kD / 2 + 0.05},
                    Vec3{c.x + kW / 2, y + 1.3, c.z + kD / 2 + 0.05},
                    Vec3{c.x - kW / 2, y + 1.3, c.z + kD / 2 + 0.05}, kGlass);
        }
    }

    // --- light towers -----------------------------------------------------
    // Four of them, spaced around the infield. Tall enough to break the
    // horizon, which is most of what they contribute: they give the eye
    // something vertical to judge speed and distance against, on a track
    // whose infield is otherwise flat to the skyline.
    for (int i = 0; i < 4; ++i) {
        const double sc = track.total() * (0.10 + 0.25 * i);
        const Vec3 c = crossPt(track, sc, -38.0);
        constexpr double kLegW = 0.9, kTowerH = 22.0;
        addBox(out, c.x - kLegW / 2, c.y, c.z - kLegW / 2, c.x + kLegW / 2, c.y + kTowerH, c.z + kLegW / 2,
               kTowerLeg);
        // Lamp bank on top -- a wide shallow box, the shape a real one has.
        addBox(out, c.x - 3.2, c.y + kTowerH, c.z - 0.9, c.x + 3.2, c.y + kTowerH + 1.6, c.z + 0.9, kLampBank);
    }

    return out;
}

// G29: pit equipment -- a tire stack and a fuel rig beside every stall.
//
// The stalls had a war wagon and a crew billboard and nothing else, so pit
// road read as a line of identical boxes. The reference's pit lane is
// cluttered, and the clutter is what makes it look like work is happening
// there. Cheap geometry: two small boxes per stall, 20 stalls.
//
// Placed on the wall side of the stall (further from the racing lane than the
// crew), so nothing new sits where a car actually stops.
std::vector<MeshVertex> buildPitEquipmentMesh(const Track& track) {
    std::vector<MeshVertex> out;
    constexpr std::array<double, 3> kTire{0.09, 0.09, 0.10};
    constexpr std::array<double, 3> kRig{0.72, 0.62, 0.18};
    const Seg& seg0 = track.segs()[0];
    const double s0 = seg0.s0;
    // Same stall spacing buildPitCrewMesh()/buildPitRoadMesh() use, so the
    // equipment lands in the stall it belongs to rather than drifting out of
    // step with the crew standing next to it.
    constexpr double kStallLat = -10.5;
    const double eqLat = kStallLat - 1.6;
    for (int idx = 0; idx < FIELD; ++idx) {
        const double sStall = s0 + seg0.len * (0.18 + 0.55 * idx / FIELD);
        // Tire stack: four tires as one short box, which is what a stack of
        // four reads as at any distance a player sees pit road from.
        const Vec3 t = crossPt(track, sStall - 0.9, eqLat);
        addBox(out, t.x - 0.36, t.y, t.z - 0.36, t.x + 0.36, t.y + 0.95, t.z + 0.36, kTire);
        // Fuel rig, taller and narrower.
        const Vec3 f = crossPt(track, sStall + 1.0, eqLat);
        addBox(out, f.x - 0.28, f.y, f.z - 0.28, f.x + 0.28, f.y + 1.5, f.z + 0.28, kRig);
    }
    return out;
}

// G28 (graphics pass): the catch fence's STRUCTURE -- posts and a curved
// top rail.
//
// buildCatchFenceMesh() above draws the mesh as a single textured band, which
// is what a fence looks like from a distance and nothing like one up close:
// the reference's fence reads as fence because you can see the posts marching
// past and the rail curving in over the track. This adds those, leaving the
// mesh band itself untouched -- the two are separate meshes precisely so the
// band can keep its own texture and wrap while the structure is flat-coloured
// geometry.
//
// Post spacing is real-ish (every ~12 m) rather than per-slice: the band uses
// 460 slices around the lap, and a post on each would be a picket fence, both
// wrong-looking and ~5500 extra boxes for no gain.
std::vector<MeshVertex> buildFenceStructureMesh(const Track& track, double fenceHeight) {
    std::vector<MeshVertex> out;
    constexpr std::array<double, 3> kSteel{0.42, 0.44, 0.47};
    constexpr std::array<double, 3> kRail{0.52, 0.54, 0.57};
    constexpr double kWallH = 1.35;  // matches buildOuterWallMesh()'s WALL_H
    constexpr double kPostSpacing = 12.0;
    constexpr double kPostW = 0.18;
    const double wl = wallLat(track);
    const double topY = kWallH + fenceHeight;

    const int posts = std::max(4, (int)(track.total() / kPostSpacing));
    const double dS = track.total() / posts;
    for (int i = 0; i < posts; ++i) {
        const double sp = i * dS;
        // A post is a thin quad rather than a box: it is seen edge-on from the
        // car for all but a fraction of a second, and a box costs six times the
        // triangles to look identical at that angle.
        const Vec3 a = crossPt(track, sp - kPostW, wl, kWallH);
        const Vec3 b = crossPt(track, sp + kPostW, wl, kWallH);
        const Vec3 c = crossPt(track, sp + kPostW, wl, topY);
        const Vec3 d = crossPt(track, sp - kPostW, wl, topY);
        addQuad(out, d, c, b, a, kSteel);

        // The curved top return: real catch fences bend back over the track so
        // debris is turned down rather than launched. Two short segments read
        // as a curve at any distance the player ever sees it from.
        //
        // Proportional to the fence, not fixed. Fixed offsets were tried first
        // and looked absurd on the short track, where a 1.3 m overhang was
        // wider than the fence was tall -- the return stuck out over the track
        // like a wire and read as a stray line in the sky rather than as part
        // of the fence.
        const double lat1 = wl - fenceHeight * 0.10, lat2 = wl - fenceHeight * 0.24;
        const double y1 = topY + fenceHeight * 0.11, y2 = topY + fenceHeight * 0.17;
        const Vec3 e0 = crossPt(track, sp - kPostW, wl, topY);
        const Vec3 e1 = crossPt(track, sp + kPostW, wl, topY);
        const Vec3 f0 = crossPt(track, sp - kPostW, lat1, y1);
        const Vec3 f1 = crossPt(track, sp + kPostW, lat1, y1);
        const Vec3 g0 = crossPt(track, sp - kPostW, lat2, y2);
        const Vec3 g1 = crossPt(track, sp + kPostW, lat2, y2);
        addQuad(out, f0, f1, e1, e0, kSteel);
        addQuad(out, g0, g1, f1, f0, kSteel);
    }

    // Horizontal rails: one along the top of the posts and one mid-height, so
    // the fence has a line running through it rather than only vertical ticks.
    // Rail thickness scales too, for the same reason the return does.
    const int N = 160;
    const double dR = track.total() / N;
    const double railT = std::max(0.06, fenceHeight * 0.03);
    for (double h : {topY, kWallH + fenceHeight * 0.5}) {
        for (int i = 0; i < N; ++i) {
            const double s0 = i * dR, s1 = (i + 1) * dR;
            const Vec3 r0 = crossPt(track, s0, wl, h);
            const Vec3 r1 = crossPt(track, s1, wl, h);
            const Vec3 r2 = crossPt(track, s1, wl, h + railT);
            const Vec3 r3 = crossPt(track, s0, wl, h + railT);
            addQuad(out, r3, r2, r1, r0, kRail);
        }
    }
    return out;
}

// G28: red-and-white striped barriers on the corner exits.
//
// Straight off the reference, where they are one of the most recognisable
// things in frame -- and they earn their place beyond decoration: a striped
// block at the exit of a corner is a braking/turn-in reference, which a
// uniform grey wall gives the player none of.
std::vector<MeshVertex> buildCornerBarrierMesh(const Track& track) {
    std::vector<MeshVertex> out;
    constexpr std::array<double, 3> kRed{0.72, 0.12, 0.12};
    constexpr std::array<double, 3> kWhite{0.90, 0.90, 0.92};
    constexpr double kStripeLen = 2.0, kBarrierH = 0.75, kProud = 0.03;
    const double wl = wallLat(track) + kProud;

    // Corner exits only (the last third of each corner arc), which is where
    // the reference puts them and where a turn-in reference is useful.
    const Seg& seg1 = track.segs()[1];
    const Seg& seg3 = track.segs()[3];
    for (const Seg* seg : {&seg1, &seg3}) {
        const double s0 = seg->s0 + seg->len * 0.66;
        const double s1 = seg->s0 + seg->len;
        const int stripes = std::max(2, (int)((s1 - s0) / kStripeLen));
        const double dS = (s1 - s0) / stripes;
        for (int i = 0; i < stripes; ++i) {
            const double a = s0 + i * dS, b = a + dS;
            const auto& col = (i % 2 == 0) ? kRed : kWhite;
            const Vec3 p0 = crossPt(track, a, wl, 0.0);
            const Vec3 p1 = crossPt(track, b, wl, 0.0);
            const Vec3 p2 = crossPt(track, b, wl, kBarrierH);
            const Vec3 p3 = crossPt(track, a, wl, kBarrierH);
            addQuad(out, p3, p2, p1, p0, col);
        }
    }
    return out;
}

// G28: the sponsor arch spanning the track above the start/finish line.
//
// Two legs outside the racing surface and a beam across, carrying a board on
// its track-facing face. The board samples the SAME sponsor-panel atlas tiles
// the trackside boards do (atlasSponsorUV), so its wordmark is one of the
// eight invented names rather than a ninth thing to author -- and it picks up
// any change to those automatically.
std::vector<MeshVertex> buildSponsorArchMesh(const Track& track, std::vector<MeshVertex>& texturedOut) {
    std::vector<MeshVertex> out;
    constexpr std::array<double, 3> kLeg{0.30, 0.32, 0.36};
    constexpr double kLegW = 1.1, kClear = 11.0, kBeamH = 2.6;
    constexpr double kArchS = 26.0; // just past the stripe, so it frames the line

    // Legs stand clear of the wall on both sides. lat is signed across the
    // track, so -lat is the infield side.
    const double outLat = wallLat(track) + 1.5;
    const double inLat = -(std::fabs(wallLat(track)) + 1.5);
    for (double lat : {outLat, inLat}) {
        const Vec3 a = crossPt(track, kArchS - kLegW, lat, 0.0);
        const Vec3 b = crossPt(track, kArchS + kLegW, lat, 0.0);
        const Vec3 c = crossPt(track, kArchS + kLegW, lat, kClear + kBeamH);
        const Vec3 d = crossPt(track, kArchS - kLegW, lat, kClear + kBeamH);
        addQuad(out, d, c, b, a, kLeg);
        // A second face at 90 degrees, so a leg still reads as solid when the
        // camera is square-on to the first one.
        const Vec3 e = crossPt(track, kArchS, lat - kLegW, 0.0);
        const Vec3 f = crossPt(track, kArchS, lat + kLegW, 0.0);
        const Vec3 g = crossPt(track, kArchS, lat + kLegW, kClear + kBeamH);
        const Vec3 h = crossPt(track, kArchS, lat - kLegW, kClear + kBeamH);
        addQuad(out, h, g, f, e, kLeg);
    }

    // The beam's board, textured from the sponsor atlas. Faces the oncoming
    // cars, which on a left-turning oval means the s-negative side.
    const std::array<double, 4> uv = atlasSponsorUV(0);
    const Vec3 b0 = crossPt(track, kArchS, outLat, kClear);
    const Vec3 b1 = crossPt(track, kArchS, inLat, kClear);
    const Vec3 b2 = crossPt(track, kArchS, inLat, kClear + kBeamH);
    const Vec3 b3 = crossPt(track, kArchS, outLat, kClear + kBeamH);
    addQuadUV(texturedOut, b3, b2, b1, b0, {uv[0], uv[1]}, {uv[2], uv[1]}, {uv[2], uv[3]}, {uv[0], uv[3]});
    // Backed so the arch is not a one-sided sheet when seen from behind.
    addQuad(out, b0, b1, b2, b3, kLeg);
    return out;
}

// G5b: small sponsor-panel quads along each straightaway, cycling through
// the 8 pre-painted (but, before this change, never-sampled)
// atlasSponsorUV() rects -- same "wire up already-painted-but-dead atlas
// content" spirit as the wall above. Sits a hair proud of the wall face
// (`wl + kProud`) to avoid z-fighting two coplanar textured quads.
//
// G11 (NASCAR-Thunder gap-analysis plan): `sponsorDensity` (Stadium's own
// per-track field, authored with a distinct value for every track since
// Phase 5b but never actually read by this function until now) scales
// the gap between panels -- higher density packs panels tighter.
std::vector<MeshVertex> buildSponsorPanelsMesh(const Track& track, double sponsorDensity) {
    std::vector<MeshVertex> out;
    constexpr double kProud = 0.02;
    constexpr double kPanelH0 = 0.35, kPanelH1 = 1.15;
    constexpr double kPanelLen = 8.0, kBaseGap = 4.0;
    const double kGap = kBaseGap / sponsorDensity;
    const double kPitch = kPanelLen + kGap;
    const double wl = wallLat(track) + kProud;
    const Seg& seg0 = track.segs()[0];
    const Seg& seg2 = track.segs()[2];
    int panelIdx = 0;
    for (const Seg* seg : {&seg0, &seg2}) {
        const int count = (int)(seg->len / kPitch);
        for (int i = 0; i < count; ++i) {
            const double sCenter = seg->s0 + (i + 0.5) * kPitch;
            const double sa = sCenter - kPanelLen / 2.0, sb = sCenter + kPanelLen / 2.0;
            const std::array<double, 4> uv = atlasSponsorUV(panelIdx % kAtlasSponsorCount);
            const Vec3 pa0 = crossPt(track, sa, wl, kPanelH0), pb0 = crossPt(track, sb, wl, kPanelH0);
            const Vec3 pa1 = crossPt(track, sa, wl, kPanelH1), pb1 = crossPt(track, sb, wl, kPanelH1);
            addQuadUV(out, pa1, pb1, pb0, pa0, {uv[0], uv[1]}, {uv[2], uv[1]}, {uv[2], uv[3]}, {uv[0], uv[3]});
            ++panelIdx;
        }
    }
    return out;
}

// G12 (NASCAR-Thunder gap-analysis plan): one turn-number panel per
// corner, at the arc's own midpoint -- a flat dark bezel backing quad
// (matching the pylon/jumbotron's own bezel look, `kBezel` below) plus
// the corner's number rendered via digit_mesh.h's addNumber(), same
// tan/lat projection idiom pylon_mesh.cpp's own PutFn closures already
// establish for placing LED-digit geometry in 3D.
std::vector<MeshVertex> buildTurnSignageMesh(const Track& track) {
    std::vector<MeshVertex> out;
    constexpr std::array<double, 3> kBezel{0.08, 0.08, 0.10};
    constexpr std::array<double, 3> kYellow{0.95, 0.85, 0.10};
    constexpr double kProud = 0.02;
    constexpr double kPanelH0 = 0.9, kPanelH1 = 2.6;
    constexpr double kHalfLen = 1.8;
    const double wl = wallLat(track) + kProud;
    const Seg& seg1 = track.segs()[1];
    const Seg& seg3 = track.segs()[3];
    int turnNum = 1;
    for (const Seg* seg : {&seg1, &seg3}) {
        const double sMid = seg->s0 + seg->len * 0.5;
        const Vec3 pa0 = crossPt(track, sMid - kHalfLen, wl, kPanelH0);
        const Vec3 pb0 = crossPt(track, sMid + kHalfLen, wl, kPanelH0);
        const Vec3 pa1 = crossPt(track, sMid - kHalfLen, wl, kPanelH1);
        const Vec3 pb1 = crossPt(track, sMid + kHalfLen, wl, kPanelH1);
        addQuad(out, pa1, pb1, pb0, pa0, kBezel);

        const PointResult p = track.pointAt(sMid);
        const double tanX = std::cos(p.hdg), tanZ = std::sin(p.hdg);
        const double latX = -std::sin(p.hdg), latZ = std::cos(p.hdg);
        const Vec3 basePt = pos3(track, sMid, wl);
        const double midY = (kPanelH0 + kPanelH1) / 2.0;
        PutFn put = [&](double x, double y) {
            return Vec3{basePt.x + tanX * x + latX * 0.02, midY + y, basePt.z + tanZ * x + latZ * 0.02};
        };
        addNumber(out, turnNum, put, 1.1, 1.3, kYellow);
        ++turnNum;
    }
    return out;
}

// G13 (NASCAR-Thunder gap-analysis plan): a slender elevated flag stand
// beside the start/finish line -- a thin support pole plus a "crow's
// nest" booth on top, same addBox()/crossPt()/wallLat() conventions
// buildPitRoadMesh() and pylon_mesh.cpp already use for track-side
// props. Checkered accent band on the booth's track-facing face reuses
// kCheckerWhite/kCheckerBlack (already defined above for
// buildStartFinishMesh()) via the same alternating-column loop shape.
std::vector<MeshVertex> buildFlagStandMesh(const Track& track) {
    std::vector<MeshVertex> out;
    constexpr std::array<double, 3> kPoleColor{0.55, 0.55, 0.58};
    constexpr std::array<double, 3> kGlassColor{0.75, 0.85, 0.90};
    constexpr double kPoleW = 0.35, kPoleH = 9.0;
    constexpr double kBoothW = 2.2, kBoothD = 1.6, kBoothH = 2.0;
    const double latP = wallLat(track) + 2.5;
    const Vec3 basePt = pos3(track, 0.0, latP);

    addBox(out, basePt.x - kPoleW / 2, 0, basePt.z - kPoleW / 2, basePt.x + kPoleW / 2, kPoleH,
           basePt.z + kPoleW / 2, kPoleColor);
    addBox(out, basePt.x - kBoothW / 2, kPoleH, basePt.z - kBoothD / 2, basePt.x + kBoothW / 2, kPoleH + kBoothH,
           basePt.z + kBoothD / 2, kGlassColor);

    // Checkered accent band around the booth's base, on its +Z (track-
    // facing) face -- same alternating-column shape buildStartFinishMesh()
    // already uses for the s=0 stripe.
    constexpr int kCols = 6;
    const double cellW = kBoothW / kCols;
    constexpr double kBandH = 0.35;
    const double bandZ = basePt.z + kBoothD / 2 + 0.01;
    for (int i = 0; i < kCols; ++i) {
        const double x0 = basePt.x - kBoothW / 2 + i * cellW, x1 = x0 + cellW;
        const auto& color = (i % 2 == 0) ? kCheckerWhite : kCheckerBlack;
        addQuad(out, Vec3{x0, kPoleH, bandZ}, Vec3{x1, kPoleH, bandZ}, Vec3{x1, kPoleH + kBandH, bandZ},
                Vec3{x0, kPoleH + kBandH, bandZ}, color);
    }
    return out;
}

// G14 (NASCAR-Thunder gap-analysis plan): a suite/press-box tower behind
// the front-tier grandstand -- every real oval has one, per the real-
// track research. Position derives from buildStandMesh()'s OWN geometry
// formula (`baseLat = wallLat(track)+6.0`, `baseH = 1.2`, stepped by
// tierD/tierH per tier) rather than duplicating/guessing those numbers,
// so the tower always clears the actual stand it sits behind regardless
// of per-track tier counts/scale. Axis-aligned box (no heading rotation)
// matching buildJumbotronMesh()'s own bezel-box precedent -- the front
// straight is straight, so this doesn't skew visibly. A horizontal
// "glass band" look (a few lighter stripe quads over a dark box) stands
// in for a dark-glass tower facade with no new atlas region needed.
std::vector<MeshVertex> buildSuiteTowerMesh(const Track& track, int frontTiers, double tierD, double tierH) {
    std::vector<MeshVertex> out;
    constexpr std::array<double, 3> kDark{0.10, 0.10, 0.13};
    constexpr std::array<double, 3> kGlass{0.35, 0.45, 0.55};
    constexpr double kTowerW = 30.0, kTowerD = 4.0, kTowerH = 10.0;
    const double baseH = 1.2 + frontTiers * tierH + 0.5;
    const double latT = wallLat(track) + 6.0 + frontTiers * tierD + 4.0;
    const Seg& seg0 = track.segs()[0];
    const Vec3 basePt = pos3(track, seg0.s0 + seg0.len * 0.5, latT);

    addBox(out, basePt.x - kTowerW / 2, baseH, basePt.z - kTowerD / 2, basePt.x + kTowerW / 2, baseH + kTowerH,
           basePt.z + kTowerD / 2, kDark);

    constexpr int kBands = 4;
    const double bandH = kTowerH * 0.7 / kBands;
    const double bandZ = basePt.z + kTowerD / 2 + 0.01;
    for (int i = 0; i < kBands; ++i) {
        const double y0 = baseH + kTowerH * 0.15 + i * (kTowerH * 0.7 / kBands);
        addQuad(out, Vec3{basePt.x - kTowerW / 2, y0, bandZ}, Vec3{basePt.x + kTowerW / 2, y0, bandZ},
                Vec3{basePt.x + kTowerW / 2, y0 + bandH, bandZ}, Vec3{basePt.x - kTowerW / 2, y0 + bandH, bandZ},
                kGlass);
    }
    return out;
}

// G5a (NASCAR-Thunder gap-analysis plan, track surface texture): a
// checkered start/finish stripe at s=0, alternating black/white quads
// across the ribbon's full lateral width (matching the ribbon's own
// -halfW..+halfW extent, not the wall/apron lateral bounds). Flat vertex-
// colored (like the pit-road markings above), not part of the new asphalt
// texture -- a deliberate simplification over a dedicated texture region
// (the plan's original approach) since a handful of alternating quads
// gives the same visible result with far less complexity, reusing this
// file's existing addQuad()/crossPt() rather than introducing an atlas-
// style multi-region UV layout for one small feature.
std::vector<MeshVertex> buildSurfacePatchesMesh(const Track& track, int patches, Mulberry32& rng) {
    std::vector<MeshVertex> out;
    if (patches <= 0) return out;
    const double halfW = track.halfW();
    const double total = track.total();
    constexpr double kRaise = 0.012; // above the asphalt, below the start/finish stripe
    // Resurfacing patches are laid down as a repair strip across part of the
    // width, so each one gets its own along-track length, lateral span and
    // shade. Slightly darker and flatter than the surrounding asphalt (fresh
    // sealant), never the full width -- a full-width band would read as a
    // painted line rather than a repair.
    for (int i = 0; i < patches; ++i) {
        const double s0 = rng.next() * total;
        const double len = 9.0 + rng.next() * 16.0;
        const double lat0 = -halfW + rng.next() * (2.0 * halfW * 0.45);
        const double lat1 = std::min(halfW, lat0 + 2.5 + rng.next() * (2.0 * halfW * 0.4));
        const double shade = 0.19 + rng.next() * 0.05;
        const std::array<double, 3> col{shade, shade, shade * 1.04};
        // Tessellated along the arc so a patch in a corner follows the
        // banked surface instead of cutting a chord through it.
        const int steps = std::max(2, (int)std::lround(len / 3.0));
        for (int k = 0; k < steps; ++k) {
            const double sa = s0 + len * k / steps, sb = s0 + len * (k + 1) / steps;
            addQuad(out, crossPt(track, sa, lat0, kRaise), crossPt(track, sb, lat0, kRaise),
                    crossPt(track, sb, lat1, kRaise), crossPt(track, sa, lat1, kRaise), col);
        }
    }
    return out;
}

std::vector<MeshVertex> buildStartFinishMesh(const Track& track) {
    std::vector<MeshVertex> out;
    constexpr int kCols = 10;
    constexpr double kStripeLen = 2.4;
    const double halfW = track.halfW();
    const double cellW = (2.0 * halfW) / kCols;
    constexpr double kRaise = 0.02; // sit just above the asphalt surface, no z-fighting
    for (int i = 0; i < kCols; ++i) {
        const double lat0 = -halfW + i * cellW, lat1 = lat0 + cellW;
        const auto& color = (i % 2 == 0) ? kCheckerWhite : kCheckerBlack;
        addQuad(out, crossPt(track, -kStripeLen / 2, lat0, kRaise), crossPt(track, kStripeLen / 2, lat0, kRaise),
                crossPt(track, kStripeLen / 2, lat1, kRaise), crossPt(track, -kStripeLen / 2, lat1, kRaise), color);
    }
    return out;
}
