#include "treeline.h"

#include "track_surface.h" // Vec3

#include <cmath>

namespace {

std::array<double, 3> mixC(const std::array<double, 3>& a, const std::array<double, 3>& b, double f) {
    return {a[0] + (b[0] - a[0]) * f, a[1] + (b[1] - a[1]) * f, a[2] + (b[2] - a[2]) * f};
}

// Same self-contained quad helper every other pure-geometry file here keeps
// (stadium_mesh.cpp / hill_silhouette.cpp / pylon_mesh.cpp).
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

// Two rings at different radii, drawn far-to-near. The back row is taller,
// darker and coarser; the front row is shorter, lighter and finer. One ring on
// its own reads as a painted band no matter how jagged its top edge is -- it
// is the overlap between two rows at different distances that makes it read as
// depth, because the gaps in the near row show the far row through them.
struct Row {
    double radius;
    int segments;
    double hMin, hMax;
    double mixToward; // 0 = pure grass, 1 = fully toward the far-shade
};

// Radii. The oval's own footprint reaches x = -347..327 and the stands sit
// ~30 m outside the wall, so anything under ~400 would cut through the far end
// of the track; 620/520 is about as close as a full ring can legally come.
//
// The first attempt put these at 980/880, reasoning that the hills sit at 1400
// and the trees should tuck inside them. That was wrong for a reason worth
// writing down: G25's haze is exponential in distance, so at 950 m the fog
// blend is ~79% and the treeline arrived as a barely-there pale smudge -- I
// had even claimed in this file's own header that the haze "does the rest",
// when what it actually did was erase them. Halving the distance drops the
// blend to ~58%, which is still real atmospheric perspective but leaves
// something behind to look at.
constexpr Row kRows[] = {
    {620.0, 96, 26.0, 48.0, 0.55},
    {520.0, 128, 16.0, 34.0, 0.25},
};

// What the far row blends toward: a desaturated blue-green. Not the sky
// colour -- G25's haze already pulls distant geometry toward the sky, and
// pre-blending it here as well would double-count and leave the treeline
// washed out to nothing on the hazier presets.
// Distinctly DARKER than the grass, not lighter. A real treeline at this
// range is a dark blue-green mass -- and since the haze then lifts it back
// toward the sky by more than half, starting anywhere near grass brightness
// leaves nothing but pale mush. Chosen after the first pass, at 0.42/0.50/
// 0.48, vanished.
constexpr std::array<double, 3> kFarShade{0.14, 0.22, 0.20};

} // namespace

std::vector<MeshVertex> buildTreeline(const std::array<double, 3>& grass, Mulberry32& rng) {
    std::vector<MeshVertex> out;

    for (const Row& row : kRows) {
        const std::array<double, 3> base = mixC(grass, kFarShade, row.mixToward);
        // Each segment gets its own height AND its own shade jitter. Without
        // the shade jitter a jagged top edge still reads as one cut-out shape,
        // because a silhouette with uniform colour is a silhouette however
        // ragged you make it.
        double hPrev = row.hMin + rng.next() * (row.hMax - row.hMin);
        for (int i = 0; i < row.segments; ++i) {
            const double a0 = (double)i / row.segments * 2.0 * M_PI;
            const double a1 = (double)(i + 1) / row.segments * 2.0 * M_PI;
            const double x0 = std::cos(a0) * row.radius, z0 = std::sin(a0) * row.radius;
            const double x1 = std::cos(a1) * row.radius, z1 = std::sin(a1) * row.radius;

            // Carry the previous segment's right-hand height into this one's
            // left, so the tops join instead of leaving a vertical slot of sky
            // between every pair of quads.
            const double h0 = hPrev;
            const double h1 = row.hMin + rng.next() * (row.hMax - row.hMin);
            hPrev = h1;

            const double j = 0.88 + rng.next() * 0.24;
            const std::array<double, 3> shade{base[0] * j, base[1] * j, base[2] * j};

            // Base sunk slightly below zero: the ground plane is not perfectly
            // flat under every track, and a treeline hovering a few
            // centimetres above it shows a bright line of sky underneath that
            // is far more noticeable than the sinking is.
            addQuad(out, Vec3{x0, -4.0, z0}, Vec3{x1, -4.0, z1}, Vec3{x1, h1, z1}, Vec3{x0, h0, z0}, shade);
        }
    }
    return out;
}
