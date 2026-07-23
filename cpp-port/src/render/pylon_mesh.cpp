#include "pylon_mesh.h"

#include "track_surface.h"

#include "../sim/car.h" // ROSTER

#include <cmath>

namespace {

constexpr std::array<double, 3> kChrome{0.75, 0.77, 0.80};
constexpr std::array<double, 3> kBezel{0.08, 0.08, 0.10};
constexpr std::array<double, 3> kYellow{0.95, 0.85, 0.1};
constexpr std::array<double, 3> kWhite{0.9, 0.9, 0.92};
constexpr std::array<double, 3> kWhiteBlue{0.85, 0.9, 0.95};

// box() (index.html:1697-1705), duplicated locally rather than shared --
// same "each pure-logic geometry file is self-contained" precedent
// stadium_mesh.cpp/digit_mesh.cpp already follow.
void addQuad(std::vector<MeshVertex>& out, const Vec3& a, const Vec3& b, const Vec3& c, const Vec3& d,
             const std::array<double, 3>& color) {
    auto tri = [&](const Vec3& p0, const Vec3& p1, const Vec3& p2) {
        const double ux = p1.x - p0.x, uy = p1.y - p0.y, uz = p1.z - p0.z;
        const double vx = p2.x - p0.x, vy = p2.y - p0.y, vz = p2.z - p0.z;
        double nx = uy * vz - uz * vy, ny = uz * vx - ux * vz, nz = ux * vy - uy * vx;
        const double len = std::sqrt(nx * nx + ny * ny + nz * nz);
        if (len > 1e-9) {
            nx /= len;
            ny /= len;
            nz /= len;
        }
        out.push_back({p0.x, p0.y, p0.z, nx, ny, nz, color});
        out.push_back({p1.x, p1.y, p1.z, nx, ny, nz, color});
        out.push_back({p2.x, p2.y, p2.z, nx, ny, nz, color});
    };
    tri(a, b, c);
    tri(a, c, d);
}

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

std::vector<MeshVertex> buildPylonMesh(const Track& track) {
    std::vector<MeshVertex> out;
    if (!track.stadium().pylon) return out;

    const double sP = track.sFinish() + 15.0, latP = wallLat(track) + 3.0;
    const PointResult p = track.pointAt(sP);
    const Vec3 basePt = pos3(track, sP, latP);
    const double tanX = std::cos(p.hdg), tanZ = std::sin(p.hdg);
    const double latX = -std::sin(p.hdg), latZ = std::cos(p.hdg);
    constexpr double PY_W = 0.7, PY_H = 16.0;
    addBox(out, basePt.x - PY_W / 2, 0, basePt.z - PY_W / 2, basePt.x + PY_W / 2, PY_H, basePt.z + PY_W / 2, kChrome);

    const int panelCount = std::min<int>(6, (int)ROSTER.size());
    for (int i = 0; i < panelCount; ++i) {
        const int num = ROSTER[i].num;
        const double rowY = PY_H * 0.9 - i * 1.35;
        PutFn put = [&](double x, double y) {
            return Vec3{basePt.x + tanX * x + latX * 0.4, rowY + y, basePt.z + tanZ * x + latZ * 0.4};
        };
        addNumber(out, num, put, 1.7, 1.05, i == 0 ? kYellow : kWhite);
    }
    return out;
}

std::vector<MeshVertex> buildJumbotronMesh(const Track& track) {
    std::vector<MeshVertex> out;
    if (!track.stadium().jumbotron) return out;

    const Seg& seg0 = track.segs()[0];
    const Seg& seg2 = track.segs()[2];
    const double cx = (seg0.A.x + seg0.B.x + seg2.A.x + seg2.B.x) / 4.0;
    const double cz = (seg0.A.y + seg0.B.y + seg2.A.y + seg2.B.y) / 4.0;
    constexpr double JB_W = 14.0, JB_H = 8.0, baseH = 6.0;
    addBox(out, cx - 0.4, 0, cz - 0.4, cx + 0.4, baseH, cz + 0.4, kChrome); // support pillar
    addBox(out, cx - JB_W / 2, baseH, cz - 1.0, cx + JB_W / 2, baseH + JB_H, cz + 1.0, kBezel); // screen bezel

    const int rowCount = std::min<int>(5, (int)ROSTER.size());
    for (int i = 0; i < rowCount; ++i) {
        const int num = ROSTER[i].num;
        const double rowY = baseH + JB_H * 0.85 - i * 1.3;
        PutFn put = [&](double x, double y) { return Vec3{cx + x, rowY + y, cz + 1.05}; };
        addNumber(out, num, put, 4.2, 1.0, i == 0 ? kYellow : kWhiteBlue);
    }
    return out;
}
