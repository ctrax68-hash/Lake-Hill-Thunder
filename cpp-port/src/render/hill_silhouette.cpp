#include "hill_silhouette.h"

#include "track_surface.h" // Vec3

#include <cmath>

namespace {

constexpr std::array<double, 3> kSkyish{0.72, 0.80, 0.90};
constexpr std::array<double, 3> kGreenish{0.30, 0.42, 0.32};

std::array<double, 3> mixC(const std::array<double, 3>& a, const std::array<double, 3>& b, double f) {
    return {a[0] + (b[0] - a[0]) * f, a[1] + (b[1] - a[1]) * f, a[2] + (b[2] - a[2]) * f};
}

// quad() (index.html:1692-1696), duplicated locally -- same "each pure-
// logic geometry file is self-contained" precedent as stadium_mesh.cpp/
// digit_mesh.cpp/pylon_mesh.cpp.
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

} // namespace

std::vector<MeshVertex> buildHillSilhouette(Mulberry32& rng) {
    std::vector<MeshVertex> out;
    constexpr double R = 1400.0, baseH = 0.0;
    constexpr int N = 48;
    const std::array<double, 3> shade = mixC(kSkyish, kGreenish, 0.4);
    for (int i = 0; i < N; ++i) {
        const double a0 = (double)i / N * 2.0 * M_PI, a1 = (double)(i + 1) / N * 2.0 * M_PI;
        const double x0 = std::cos(a0) * R, z0 = std::sin(a0) * R;
        const double x1 = std::cos(a1) * R, z1 = std::sin(a1) * R;
        const double h0 = 50.0 + rng.next() * 90.0, h1 = 50.0 + rng.next() * 90.0;
        addQuad(out, Vec3{x0, baseH, z0}, Vec3{x1, baseH, z1}, Vec3{x1, baseH + h1, z1}, Vec3{x0, baseH + h0, z0},
                shade);
    }
    return out;
}
