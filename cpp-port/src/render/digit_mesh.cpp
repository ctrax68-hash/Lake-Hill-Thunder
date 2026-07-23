#include "digit_mesh.h"

#include <cmath>
#include <string>

namespace {

// SEGBITS (index.html:2175-2176): segments 1=top 2=topL 4=topR 8=mid
// 16=botL 32=botR 64=bottom.
constexpr int kSegBits[10] = {0b1110111, 0b0100100, 0b1011101, 0b1101101, 0b0101110,
                              0b1101011, 0b1111011, 0b0100101, 0b1111111, 0b1101111};

// Same tri()+quad() split as stadium_mesh.cpp's own addQuad() (a flat
// normal per triangle from its own two edge vectors) -- duplicated locally
// rather than shared, matching this project's established "each pure-logic
// geometry file is self-contained" precedent (stadium_mesh.cpp/
// hill_silhouette.cpp/pylon_mesh.cpp each keep their own copy too).
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

void addDigitQuads(std::vector<MeshVertex>& out, int n, double ox, double oy, double w, double h,
                    const PutFn& put, const std::array<double, 3>& col) {
    if (n < 0 || n > 9) return;
    const double t = w * 0.24;
    const int bits = kSegBits[n];
    auto seg = [&](double x0, double y0, double x1, double y1) {
        addQuad(out, put(ox + x0, oy + y0), put(ox + x1, oy + y0), put(ox + x1, oy + y1), put(ox + x0, oy + y1),
                col);
    };
    if (bits & 1) seg(t * 0.5, h - t, w - t * 0.5, h);
    if (bits & 2) seg(0, h / 2 + t * 0.25, t, h - t * 0.5);
    if (bits & 4) seg(w - t, h / 2 + t * 0.25, w, h - t * 0.5);
    if (bits & 8) seg(t * 0.5, h / 2 - t / 2, w - t * 0.5, h / 2 + t / 2);
    if (bits & 16) seg(0, t * 0.5, t, h / 2 - t * 0.25);
    if (bits & 32) seg(w - t, t * 0.5, w, h / 2 - t * 0.25);
    if (bits & 64) seg(t * 0.5, 0, w - t * 0.5, t);
}

void addNumber(std::vector<MeshVertex>& out, int num, const PutFn& put, double w, double h,
               const std::array<double, 3>& col) {
    const std::string s = std::to_string(num);
    const double dw = w / ((double)s.size() + ((double)s.size() - 1.0) * 0.22);
    double x = -w / 2;
    for (char ch : s) {
        addDigitQuads(out, ch - '0', x, -h / 2, dw, h, put, col);
        x += dw * 1.22;
    }
}
