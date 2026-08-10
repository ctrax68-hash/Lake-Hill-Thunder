#include "livery.h"

#include <algorithm>
#include <cmath>

namespace {

std::array<double, 3> mixC(const std::array<double, 3>& a, const std::array<double, 3>& b, double t) {
    return {a[0] + (b[0] - a[0]) * t, a[1] + (b[1] - a[1]) * t, a[2] + (b[2] - a[2]) * t};
}

class Canvas {
public:
    explicit Canvas(int size) : size_(size), pixels_((size_t)size * size * 4, 0) {}

    void fillRectPx(int x, int y, int w, int h, const std::array<double, 3>& color, double alpha = 1.0) {
        const int x0 = std::max(0, x), y0 = std::max(0, y);
        const int x1 = std::min(size_, x + w), y1 = std::min(size_, y + h);
        for (int py = y0; py < y1; ++py)
            for (int px = x0; px < x1; ++px) blend(px, py, color, alpha);
    }

    // fx/fy/fw/fh in [0,1] fractions of the texture size (mirrors JS's
    // u()/v() = f*TX scaling used throughout paintLivery()).
    void fillRect(double fx, double fy, double fw, double fh, const std::array<double, 3>& color,
                  double alpha = 1.0) {
        fillRectPx((int)std::lround(fx * size_), (int)std::lround(fy * size_), (int)std::lround(fw * size_) + 1,
                   (int)std::lround(fh * size_) + 1, color, alpha);
    }

    void fillCircle(double fcx, double fcy, double fr, const std::array<double, 3>& color, double alpha = 1.0) {
        fillEllipse(fcx, fcy, fr, fr, color, alpha);
    }

    void fillEllipse(double fcx, double fcy, double frx, double fry, const std::array<double, 3>& color,
                      double alpha = 1.0) {
        const double cx = fcx * size_, cy = fcy * size_, rx = frx * size_, ry = fry * size_;
        const int x0 = std::max(0, (int)std::floor(cx - rx)), x1 = std::min(size_, (int)std::ceil(cx + rx));
        const int y0 = std::max(0, (int)std::floor(cy - ry)), y1 = std::min(size_, (int)std::ceil(cy + ry));
        for (int py = y0; py < y1; ++py) {
            for (int px = x0; px < x1; ++px) {
                const double dx = (px + 0.5 - cx) / rx, dy = (py + 0.5 - cy) / ry;
                if (dx * dx + dy * dy <= 1.0) blend(px, py, color, alpha);
            }
        }
    }

    // A rect defined in a shear-transformed local space (matches JS's
    // `c2.transform(1,0,-0.55,1,0,0)` before a fillRect -- stripe style 1's
    // forward-slash band): device_x = local_x - shear*local_y.
    void fillShearedRect(double flx, double fly, double flw, double flh, double shear,
                          const std::array<double, 3>& color) {
        const double lx0 = flx * size_, lx1 = (flx + flw) * size_;
        const double ly0 = fly * size_, ly1 = (fly + flh) * size_;
        for (int py = 0; py < size_; ++py) {
            if (py < ly0 || py >= ly1) continue;
            const double localXAtY0 = 0 + shear * py; // device_x = local_x - shear*py -> local_x = device_x + shear*py... see below
            (void)localXAtY0;
            for (int px = 0; px < size_; ++px) {
                const double localX = px + shear * py;
                if (localX >= lx0 && localX < lx1) blend(px, py, color, 1.0);
            }
        }
    }

    std::vector<uint8_t> take() { return std::move(pixels_); }

private:
    void blend(int px, int py, const std::array<double, 3>& color, double alpha) {
        const size_t idx = ((size_t)py * size_ + (size_t)px) * 4;
        const double er = pixels_[idx] / 255.0, eg = pixels_[idx + 1] / 255.0, eb = pixels_[idx + 2] / 255.0;
        const double r = std::clamp(er * (1 - alpha) + color[0] * alpha, 0.0, 1.0);
        const double g = std::clamp(eg * (1 - alpha) + color[1] * alpha, 0.0, 1.0);
        const double b = std::clamp(eb * (1 - alpha) + color[2] * alpha, 0.0, 1.0);
        pixels_[idx] = (uint8_t)std::lround(r * 255.0);
        pixels_[idx + 1] = (uint8_t)std::lround(g * 255.0);
        pixels_[idx + 2] = (uint8_t)std::lround(b * 255.0);
        pixels_[idx + 3] = 255;
    }

    int size_;
    std::vector<uint8_t> pixels_;
};

// carU() (index.html:2249): nose (x=2.51) -> 0.02, tail (x=-2.51) -> 0.78.
double carU(double x) {
    return 0.02 + (2.51 - x) / 5.02 * 0.76;
}

// G1b (NASCAR-Thunder gap-analysis plan, car UV/livery fix): every fill*
// call above is fraction-based (fx/fy/fw/fh in [0,1]), so rendering into a
// Canvas built larger than the final texture and box-filter-downsampling
// afterward anti-aliases every hard rect/circle/ellipse edge for free --
// no change needed to any individual paint call. Mirrors the box-filter
// spirit of renderer.cpp's buildRgba8MipChain() (2x2 average), generalized
// to an arbitrary integer factor since that helper halves repeatedly for
// a mip chain rather than doing one fixed-factor reduction.
constexpr int kSupersample = 2;

std::vector<uint8_t> downsampleBox(const std::vector<uint8_t>& src, int srcSize, int factor) {
    const int dstSize = srcSize / factor;
    std::vector<uint8_t> dst((size_t)dstSize * dstSize * 4);
    const int n = factor * factor;
    for (int y = 0; y < dstSize; ++y) {
        for (int x = 0; x < dstSize; ++x) {
            int sums[4] = {0, 0, 0, 0};
            for (int sy = 0; sy < factor; ++sy) {
                for (int sx = 0; sx < factor; ++sx) {
                    const size_t idx = ((size_t)(y * factor + sy) * srcSize + (x * factor + sx)) * 4;
                    for (int ch = 0; ch < 4; ++ch) sums[ch] += src[idx + ch];
                }
            }
            const size_t didx = ((size_t)y * dstSize + x) * 4;
            for (int ch = 0; ch < 4; ++ch) dst[didx + ch] = (uint8_t)((sums[ch] + n / 2) / n);
        }
    }
    return dst;
}

// A minimal 7-segment digit rasterizer standing in for JS's real
// `drawNum()` font text (see livery.h's own simplification note #3).
// Segments: 0=top,1=top-left,2=top-right,3=middle,4=bottom-left,
// 5=bottom-right,6=bottom.
constexpr bool kDigitSegments[10][7] = {
    {1, 1, 1, 0, 1, 1, 1}, // 0
    {0, 0, 1, 0, 0, 1, 0}, // 1
    {1, 0, 1, 1, 1, 0, 1}, // 2
    {1, 0, 1, 1, 0, 1, 1}, // 3
    {0, 1, 1, 1, 0, 1, 0}, // 4
    {1, 1, 0, 1, 0, 1, 1}, // 5
    {1, 1, 0, 1, 1, 1, 1}, // 6
    {1, 0, 1, 0, 0, 1, 0}, // 7
    {1, 1, 1, 1, 1, 1, 1}, // 8
    {1, 1, 1, 1, 0, 1, 1}, // 9
};

// Draws one digit into a fcx,fcy-centered fh-tall (fw-wide) box.
void drawDigit(Canvas& c, int d, double fcx, double fcy, double fw, double fh, const std::array<double, 3>& color) {
    if (d < 0 || d > 9) return;
    const double t = fw * 0.22; // segment thickness
    const double x0 = fcx - fw / 2, x1 = fcx + fw / 2;
    const double yTop = fcy - fh / 2, yMid = fcy, yBot = fcy + fh / 2;
    const bool* seg = kDigitSegments[d];
    if (seg[0]) c.fillRect(x0, yTop, fw, t, color);                     // top
    if (seg[1]) c.fillRect(x0, yTop, t, fh / 2, color);                 // top-left
    if (seg[2]) c.fillRect(x1 - t, yTop, t, fh / 2, color);             // top-right
    if (seg[3]) c.fillRect(x0, yMid - t / 2, fw, t, color);             // middle
    if (seg[4]) c.fillRect(x0, yMid, t, fh / 2, color);                 // bottom-left
    if (seg[5]) c.fillRect(x1 - t, yMid, t, fh / 2, color);             // bottom-right
    if (seg[6]) c.fillRect(x0, yBot - t, fw, t, color);                 // bottom
}

// Draws a 1-2 digit number, outline then fill (JS's drawNum() stroke+fill
// order, index.html:2541-2542), centered at (fcx,fcy).
void drawNumber(Canvas& c, int num, double fcx, double fcy, double fh, const std::array<double, 3>& fill,
                const std::array<double, 3>& outline) {
    const std::string s = std::to_string(num);
    const double fw = fh * 0.62;
    const double gap = fw * 0.18;
    const double totalW = s.size() == 2 ? fw * 2 + gap : fw;
    double x = fcx - totalW / 2 + fw / 2;
    for (char ch : s) {
        const int d = ch - '0';
        // outline: a slightly larger dark digit painted first.
        // I5 (car visual fidelity plan): ratio raised 1.18/1.14 ->
        // 1.28/1.22. At 1.18/1.14 the outline was ~2px wide on a 256px
        // texture once the 7-segment thickness (fw*0.22) is accounted for,
        // which the mip chain and the chase camera's distance together
        // erase almost entirely -- the numbers read as flat fill with no
        // edge. The reference NASCAR Thunder cars carry a heavy contrasting
        // number outline that survives at any distance, so this widens the
        // dark ring rather than restyling the digits themselves.
        drawDigit(c, d, x, fcy, fw * 1.28, fh * 1.22, outline);
        drawDigit(c, d, x, fcy, fw, fh, fill);
        x += fw + gap;
    }
}

// drawFlameLick() (index.html:2583-2593): exact port.
void drawFlameLick(Canvas& c, double baseU, double baseV, double len, double amp, double dir,
                    const std::array<double, 3>& color) {
    constexpr int n = 9;
    for (int i = 0; i < n; ++i) {
        const double t = (double)i / (n - 1);
        const double x = baseU + len * t;
        const double y = baseV + dir * std::sin(t * M_PI * 0.85) * amp * (1 - t * 0.25);
        const double r = (1 - t) * amp * 0.85 + 3.0 / kLiveryTextureSize;
        c.fillCircle(x, y, r, color);
    }
}

} // namespace

std::vector<uint8_t> buildLiveryPixels(const Color3& body, int num, int idx, const LiveryScheme* scheme,
                                        bool paceLightBar) {
    Canvas c(kLiveryTextureSize * kSupersample);

    // Base + 3-tone flat panel shading (index.html:2597-2603): rockers get
    // the shadow tone, the roof+hood band gets the highlight tone.
    constexpr double kShadowM = 0.70, kBaseM = 0.94, kHiliteM = 1.08;
    auto tone = [&](double m) { return std::array<double, 3>{body[0] * m, body[1] * m, body[2] * m}; };
    c.fillRect(0, 0, 0.80, 1.0, tone(kBaseM));
    c.fillRect(0, 0.00, 1.0, 0.11, tone(kShadowM));
    c.fillRect(0, 0.89, 1.0, 0.11, tone(kShadowM));
    c.fillRect(0, 0.40, 1.0, 0.20, tone(std::min(1.0, kHiliteM)));

    // H2 (NT2003 engine-feel plan): a "clearcoat gradient" -- graduated
    // steps softening the hard 0.70->0.94 jump right at the shadow band's
    // own edge (v=0.11/0.89) into something closer to a smooth falloff.
    // Distinct from the three tones above, which stay exactly as they were
    // (the existing shadow/base/hilite sample windows in livery_test.cpp
    // land at v<0.05, v 0.15-0.20 and v 0.45-0.55, none of which this new
    // band at v 0.11-0.148 touches). Alpha-blended rather than opaque, so
    // it reads as a soft roll-off riding on top of the base tone rather
    // than a fourth hard step.
    for (int i = 0; i < 3; ++i) {
        const double t = (i + 1) / 4.0; // 0.25, 0.5, 0.75 -> shadow toward base
        const double m = kShadowM + (kBaseM - kShadowM) * t;
        const double vLo = 0.110 + i * 0.0127, vHi = 0.890 - i * 0.0127;
        c.fillRect(0, vLo, 1.0, 0.0127, tone(m), 0.6);
        c.fillRect(0, vHi - 0.0127, 1.0, 0.0127, tone(m), 0.6);
    }

    const double U0 = 0.02, U1 = 0.78;
    // accent auto-derived from body luminance (index.html:2867-2868).
    const double lum = body[0] * 0.5 + body[1] * 0.4 + body[2] * 0.1;
    const std::array<double, 3> accent = lum > 0.5 ? CarPalette::Black : CarPalette::White;

    const int style = scheme ? scheme->stripe : (idx) % 5;
    const std::array<double, 3> acc2 = scheme ? scheme->acc2 : CarPalette::White;

    if (style == 0) {
        // twin side stripes + hood band (index.html:2611-2620).
        c.fillRect(U0, 0.260, U1 - U0, 0.030, accent);
        c.fillRect(U0, 0.300, U1 - U0, 0.014, accent);
        c.fillRect(U0, 0.710, U1 - U0, 0.030, accent);
        c.fillRect(U0, 0.686, U1 - U0, 0.014, accent);
        c.fillRect(0.05, 0.470, 0.24, 0.060, accent);
    } else if (style == 1) {
        // forward slash (index.html:2621-2632).
        c.fillShearedRect(0.46, 0.0, 0.09, 1.0, 0.55, accent);
        c.fillShearedRect(0.57, 0.0, 0.028, 1.0, 0.55, acc2);
        c.fillRect(U0, 0.260, U1 - U0, 0.016, accent);
        c.fillRect(U0, 0.724, U1 - U0, 0.016, accent);
    } else if (style == 2) {
        // two-tone fade (index.html:2633-2644).
        for (int sBand = 0; sBand < 4; ++sBand) {
            const auto bandColor = mixC(body, accent, 0.25 + sBand * 0.25);
            const double x0 = 0.40 + sBand * 0.028;
            const double w2 = sBand < 3 ? 0.028 : (0.78 - x0);
            c.fillRect(x0, 0.052, w2, 0.248, bandColor, 0.96);
            c.fillRect(x0, 0.70, w2, 0.248, bandColor, 0.96);
        }
        c.fillRect(0.40, 0.052, 0.010, 0.896, acc2);
    } else if (style == 3) {
        // halo (index.html:2645-2654).
        c.fillRect(U0, 0.40, 0.10, 0.20, accent);
        c.fillRect(0.36, 0.435, 0.185, 0.13, accent);
        c.fillRect(U0, 0.260, U1 - U0, 0.012, acc2);
        c.fillRect(U0, 0.296, U1 - U0, 0.012, acc2);
        c.fillRect(U0, 0.728, U1 - U0, 0.012, acc2);
        c.fillRect(U0, 0.692, U1 - U0, 0.012, acc2);
    } else {
        // flames (index.html:2655-2664).
        const double flU0 = 0.26;
        for (auto [vSide, dir] : {std::pair{0.145, -1.0}, std::pair{0.855, 1.0}}) {
            drawFlameLick(c, flU0, vSide, 0.26, 0.075, dir, accent);
            drawFlameLick(c, flU0, vSide + dir * 0.01, 0.18, 0.045, dir, acc2);
        }
    }

    // ---- I5 (car visual fidelity plan): livery density patches ----
    //
    // Each of the five stripe styles above is a handful of long bands --
    // faithful to the JS original, but measured against the NASCAR-Thunder
    // reference images this I-series targets, a car of that era is covered
    // in *blocks*: sponsor panels, contrasting hood and lower-quarter
    // slabs, a colored band wrapping the rear glass. That block density,
    // more than the stripes themselves, is what separates the reference
    // look from this port's flat one at chase-cam distance.
    //
    // G16's contingency chips are the direct precedent (a fixed layout of
    // small rects whose colors rotate by the car's own `idx`); this is the
    // same idea scaled up -- fewer, much larger blocks, painted in the
    // car's OWN body/accent/acc2 tones rather than the chips' fixed
    // sticker palette, so a patch reads as part of the paint scheme rather
    // than a decal stuck on top of it.
    //
    // Placed here, between the stripe styles and everything below,
    // deliberately on both counts:
    //   - AFTER the stripes, so density rises for all 5 styles rather than
    //     only the sparse ones. A per-style patch set would mean five more
    //     layouts to tune and the same flat read on whichever style got
    //     skipped.
    //   - BEFORE the rocker band, wheel-arch rings, shutlines, glass,
    //     contingency chips, number panels and lamp clusters, so no block
    //     can bury any of those however its coordinates drift later. The
    //     blocks are ALSO placed clear of all of them by coordinate (see
    //     each region's bounds below) -- belt and braces, since "it happens
    //     to get overpainted" is not the same as "it was placed correctly".
    {
        // Tones taken from the car's own scheme, so patches are in-scheme
        // by construction: its accent, its secondary accent, a half-mix of
        // body and accent (a shade that exists nowhere else on the car,
        // which is what keeps a large block from reading as a misplaced
        // stripe), and a deep shadow tone of the body itself.
        const std::array<std::array<double, 3>, 4> kPatchTones{{
            accent,
            acc2,
            mixC(body, accent, 0.55),
            tone(0.55),
        }};

        // Region bounds, in the same UV space as everything else here:
        //   hood      u 0.060-0.264: clear of the nose lamp column (which
        //             ends at 0.038) and the cowl shutline (0.285).
        //             v 0.412-0.588, inside the roof/hood highlight band.
        //   quarters  u 0.286-0.524: the door/lower-quarter run between the
        //             front wheel-arch ring (reaches u=0.260) and the rear
        //             one (starts at 0.540). v 0.076-0.138, above the
        //             near-black rocker (ends 0.052) and below the door-
        //             number ellipse (starts 0.153).
        //   rear win  u 0.556-0.658: inside the rear glass's own u span
        //             (0.551-0.665). v 0.244-0.308, the sail panel below
        //             the glass rect (starts 0.335), stopping short of the
        //             beltline seam (0.320).
        // The hood straddles v=0.5 on one continuous surface, so its blocks
        // are authored symmetric already; the side-panel blocks are single
        // entries mirrored onto the far flank.
        struct Patch {
            double u, v, w, h;
            bool mirror;
        };
        static const std::array<Patch, 11> kPatches{{
            {0.060, 0.412, 0.070, 0.070, false}, // hood, forward pair
            {0.060, 0.518, 0.070, 0.070, false},
            {0.142, 0.428, 0.048, 0.144, false}, // hood, centre slab
            {0.202, 0.440, 0.062, 0.050, false}, // hood, cowl pair
            {0.202, 0.510, 0.062, 0.050, false},
            {0.286, 0.076, 0.058, 0.062, true},  // lower door/quarter run
            {0.352, 0.076, 0.048, 0.062, true},
            {0.408, 0.084, 0.042, 0.046, true},
            {0.458, 0.076, 0.066, 0.062, true},
            {0.556, 0.244, 0.048, 0.064, true},  // rear-window surround
            {0.612, 0.244, 0.046, 0.064, true},
        }};

        int i = 0;
        for (const auto& p : kPatches) {
            const auto& fillTone = kPatchTones[(size_t)((idx + i) % (int)kPatchTones.size())];
            const auto& pinTone = kPatchTones[(size_t)((idx + i + 2) % (int)kPatchTones.size())];
            // The livery wraps the body, so a -z-flank band and its +z twin
            // are v-mirrors of each other -- the same pairing every band
            // above already uses (rockers 0.055/0.945, doors 0.235/0.765).
            for (double vy : {p.v, 1.0 - p.v - p.h}) {
                c.fillRect(p.u, vy, p.w, p.h, fillTone);
                // Every third block carries a thin contrasting stripe along
                // its lower edge: a sponsor-panel cue, and it stops a run of
                // similar blocks from merging into one long band.
                if (i % 3 == 0) c.fillRect(p.u, vy + p.h - 0.007, p.w, 0.007, pinTone);
                if (!p.mirror) break;
            }
            ++i;
        }
    }

    // rocker + seam dark (index.html:2666-2669).
    const std::array<double, 3> kNearBlack{18 / 255.0, 18 / 255.0, 20 / 255.0};
    c.fillRect(0, 0, 1.0, 0.052, kNearBlack);
    c.fillRect(0, 0.948, 1.0, 0.052, kNearBlack);

    // wheel arches: graduated shadow rings (index.html:2670-2679).
    for (double ux : {carU(1.395), carU(-1.395)}) {
        for (double vy : {0.055, 0.945}) {
            c.fillCircle(ux, vy, 0.071, tone(0.9));
            c.fillCircle(ux, vy, 0.064, tone(0.55));
            c.fillCircle(ux, vy, 0.056, tone(0.25));
            c.fillCircle(ux, vy, 0.047, {10 / 255.0, 10 / 255.0, 12 / 255.0});
        }
    }

    // H2 (NT2003 engine-feel plan): wheel-arch lip. Distinct from the
    // shadow rings above (which paint the dark cavity INSIDE the opening,
    // centered at the rocker); this is the crisp sheet-metal edge AT the
    // opening, where gen_car_rig.py's wheel-arch relief actually creases
    // the mesh (H1's ring_normals() shows a 93.7deg normal turn right here
    // -- the one place on the body a hard edge is real and wanted rather
    // than a shading artifact). That crease sits at ring index k=2/3 (the
    // last relieved point before the fender flares back out), which H1's
    // car_v() maps to v~0.825 on the +z side / ~0.175 on the -z side --
    // not re-derived from gen_car_rig.py here (this file has no import
    // path to it), just placed at the same handful of anchor points this
    // file already uses elsewhere (rocker 0.055/0.945, beltline
    // 0.335/0.665) which the H1 decode check confirmed line up. A thin
    // bright line, the way a fender lip catches light along its edge.
    const std::array<double, 3> archLip{
        std::min(1.0, body[0] * 1.35 + 0.05), std::min(1.0, body[1] * 1.35 + 0.05), std::min(1.0, body[2] * 1.35 + 0.05)};
    for (double ux : {carU(1.395), carU(-1.395)}) {
        c.fillRect(ux - 0.028, 0.812, 0.056, 0.006, archLip, 0.55);
        c.fillRect(ux - 0.028, 0.182, 0.056, 0.006, archLip, 0.55);
    }

    // rubber/dirt smudge behind rear wheel arches only (index.html:2680-2685).
    for (double vy : {0.055, 0.945}) {
        c.fillEllipse(carU(-1.395) + 0.035, vy, 0.05, 0.035, {10 / 255.0, 10 / 255.0, 12 / 255.0}, 0.35);
    }
    // exhaust soot smudge, right side only (index.html:2686-2691).
    c.fillEllipse(0.615, 0.058, 0.055, 0.028, {8 / 255.0, 8 / 255.0, 9 / 255.0}, 0.30);

    // Glass rect bounds, hoisted above both the H2 shutlines (which anchor
    // to the windshield/rear-glass edges) and the glass section itself
    // below (index.html:2692-2727), which is where these were originally
    // declared.
    const double uWS0 = carU(0.68), uWS1 = carU(0.02);
    const double uSG0 = carU(0.30), uSG1 = carU(-0.95);
    const double uRG0 = carU(-1.00), uRG1 = carU(-1.75);
    constexpr double GV0 = 0.335, GVH = 0.330;

    // ---- H2 (NT2003 engine-feel plan): panel shutlines ----
    //
    // A flat-shaded box car has no panels to speak of, so there was nothing
    // for a shutline to sit on. H1's real hood/roof/deck/door surfaces give
    // these somewhere to go. All painted as thin, low-alpha dark lines --
    // real panel gaps read as a subtle shadow line, not a bold graphic, and
    // alpha-blending (rather than an opaque fill like the stripe styles
    // above) means the crease shows through whatever paint scheme is
    // underneath, the way a real seam would regardless of the wrap on it.
    const std::array<double, 3> seamShadow{0.0, 0.0, 0.0};
    constexpr double kSeamW = 0.0035;

    // Beltline character line: right where the glass band starts/ends
    // (GV0=0.335 / GV0+GVH=0.665 below), the crease real cars have at the
    // base of the greenhouse. Runs the full nose-to-tail body wrap; stops
    // just shy of the door-number badges (centered at v=0.235/0.765,
    // fry=0.082, so their far edges sit at v~0.317/0.683) so it reads as
    // running behind them rather than through them.
    c.fillRect(U0, 0.320, U1 - U0, kSeamW, seamShadow, 0.30);
    c.fillRect(U0, 0.677, U1 - U0, kSeamW, seamShadow, 0.30);

    // Door shutlines: one ahead of the door (roughly the front-fender/
    // door break, x~1.00) and one behind it (roughly the door/quarter-panel
    // break, at the roof-peak station x~-0.60), each side, spanning only
    // the door panel itself (rocker to beltline) rather than the full
    // body height -- a shutline doesn't cross the window.
    for (double ux : {carU(1.00), carU(-0.60)}) {
        c.fillRect(ux, 0.062, kSeamW, 0.320 - 0.062, seamShadow, 0.28);   // -z door panel
        c.fillRect(ux, 0.680, kSeamW, 0.945 - 0.680, seamShadow, 0.28);  // +z door panel
    }

    // Cowl line (hood meets windshield) and decklid line (trunk meets rear
    // glass), each just outside its glass rect so the crease reads as the
    // panel edge the glass sits in, not a line drawn across the glass.
    c.fillRect(uWS0 - 0.012, GV0, kSeamW, GVH, seamShadow, 0.30);
    c.fillRect(uRG1 + 0.008, GV0, kSeamW, GVH, seamShadow, 0.30);

    // ---- glass (index.html:2692-2727) ----
    const std::array<double, 3> glassDark{16 / 255.0, 20 / 255.0, 30 / 255.0};
    c.fillRect(uWS0, GV0, uWS1 - uWS0, GVH, glassDark);
    c.fillRect(uRG0, GV0, uRG1 - uRG0, GVH, glassDark);
    c.fillRect(uSG0, 0.335, uSG1 - uSG0, 0.075, glassDark);
    c.fillRect(uSG0, 0.590, uSG1 - uSG0, 0.075, glassDark);
    const std::array<double, 3> glassHi{26 / 255.0, 33 / 255.0, 46 / 255.0};
    c.fillRect(uWS0, 0.47, uWS1 - uWS0, 0.06, glassHi);
    c.fillRect(uRG0, 0.48, uRG1 - uRG0, 0.04, glassHi);
    // G16 (NT2003 presentation plan): the windshield sun strip. Previously a
    // fixed near-white band; every car in the reference footage carries a
    // colored one instead, so it now takes the car's own secondary accent
    // and is a touch deeper, making it read at chase-cam distance and giving
    // the field visible per-car variety through the windshield.
    c.fillRect(uWS1 - 0.028, GV0 + 0.015, 0.028, GVH - 0.015, acc2);
    c.fillRect(uWS1 - 0.032, GV0 + 0.015, 0.005, GVH - 0.015,
               std::array<double, 3>{14 / 255.0, 14 / 255.0, 16 / 255.0});
    // driver window net (index.html:2713-2722)
    c.fillRect(uSG0, 0.590, (uSG1 - uSG0) * 0.55, 0.075, {8 / 255.0, 8 / 255.0, 10 / 255.0});
    for (int i = 0; i < 6; ++i) {
        const std::array<double, 3> weave{60 / 255.0, 60 / 255.0, 64 / 255.0};
        c.fillRect(uSG0 + 4.0 / kLiveryTextureSize + i * ((uSG1 - uSG0) * 0.55 - 8.0 / kLiveryTextureSize) / 5.0,
                   0.593, 2.0 / kLiveryTextureSize, 0.069, weave);
        c.fillRect(uSG0 + 2.0 / kLiveryTextureSize, 0.596 + i * 0.063 / 5.0, (uSG1 - uSG0) * 0.55 - 4.0 / kLiveryTextureSize,
                   2.0 / kLiveryTextureSize, weave);
    }
    // A/B pillar dark edges
    const std::array<double, 3> pillarDark{14 / 255.0, 14 / 255.0, 16 / 255.0};
    c.fillRect(uWS0 - 2.0 / kLiveryTextureSize, GV0, 4.0 / kLiveryTextureSize, GVH, pillarDark);
    c.fillRect(uSG1 - 2.0 / kLiveryTextureSize, 0.335, 4.0 / kLiveryTextureSize, 0.075, pillarDark);
    c.fillRect(uSG1 - 2.0 / kLiveryTextureSize, 0.590, 4.0 / kLiveryTextureSize, 0.075, pillarDark);

    // H2 (NT2003 engine-feel plan): window rubber. The pillar edges above
    // only frame the SIDES of each glass rect (the A/B pillars, which are
    // real structural members); a window also has rubber trim running its
    // full perimeter, and at 1024px there's finally enough texel density
    // for that trim to read as a frame rather than noise. Two elements per
    // glass rect: a thin dark seal on all four edges, and a thinner bright
    // line just inside the TOP edge only -- the way real window rubber
    // catches a highlight along its upper lip, never the lower one. Only
    // the windshield and rear glass get it (the two glass panes the chase
    // camera actually holds in frame for an entire race); the side windows
    // stay as they were.
    const std::array<double, 3> rubberHi{40 / 255.0, 46 / 255.0, 58 / 255.0};
    constexpr double kTrim = 3.0;
    auto glassFrame = [&](double gu0, double gu1, double gv0, double gvh) {
        const double t = kTrim / kLiveryTextureSize;
        c.fillRect(gu0 - t, gv0 - t, (gu1 - gu0) + 2 * t, t, pillarDark);       // top
        c.fillRect(gu0 - t, gv0 + gvh, (gu1 - gu0) + 2 * t, t, pillarDark);     // bottom
        c.fillRect(gu0 - t, gv0, t, gvh, pillarDark);                          // left
        c.fillRect(gu1, gv0, t, gvh, pillarDark);                              // right
        c.fillRect(gu0, gv0 - t * 0.5, gu1 - gu0, t * 0.6, rubberHi);          // top highlight
    };
    glassFrame(uWS0, uWS1, GV0, GVH);
    glassFrame(uRG0, uRG1, GV0, GVH);
    // roof flaps
    c.fillRect(carU(-0.45), 0.435, 0.045, 0.052, tone(0.72));
    c.fillRect(carU(-0.45), 0.513, 0.045, 0.052, tone(0.72));

    // ---- J6 (car visual fidelity plan, part 2): roll-cage glimpse bars ----
    // No JS precedent (grep confirms zero "roll cage"/"rollbar" hits anywhere
    // in index.html) -- a windshield/backlight painted as a flat dark
    // rectangle reads as an empty hole rather than glass with something
    // behind it. Same "cheap, plausible look over a physical model"
    // philosophy this file already uses for the window-net weave and
    // glassFrame()'s rubber trim above: a couple of thin, partially
    // transparent bars inside each glass rect, positioned clear of the
    // windshield's own sun-strip (u in [uWS1-0.032, uWS1]) and every
    // glassFrame() trim edge, so they read as a glimpsed structural member
    // rather than a texture artifact painted ON the glass.
    //
    // Color chosen and verified, not assumed safe -- squared-distance to
    // fs_car.sc's own reference colors, computed the same way I3's own
    // comment already worked out for glassDark vs. the near-black wheel
    // swatches: (70,72,78)/255 sits ~0.122 from glassDarkRef (>>0.0015
    // threshold), ~0.069 from glassHiRef, and >0.2 from tailRef/amberRef/
    // rimRef (>>0.01 threshold) -- comfortably outside every I3/H6
    // color-match radius, so these bars correctly render as plain diffuse-
    // lit surface rather than picking up glass's damped/reflective
    // treatment or any emissive/chrome response never intended for them.
    {
        const std::array<double, 3> cageBar{70 / 255.0, 72 / 255.0, 78 / 255.0};
        constexpr double kCageW = 6.0 / kLiveryTextureSize;
        const double t = kTrim / kLiveryTextureSize;
        const double wsClearU1 = uWS1 - 0.036; // clear of the sun-strip + its divider line
        c.fillRect(uWS0 + (wsClearU1 - uWS0) * 0.35 - kCageW * 0.5, GV0 + t, kCageW, GVH - 2 * t, cageBar, 0.55);
        c.fillRect(uWS0 + (wsClearU1 - uWS0) * 0.65 - kCageW * 0.5, GV0 + t, kCageW, GVH - 2 * t, cageBar, 0.55);
        c.fillRect((uRG0 + uRG1) * 0.5 - kCageW * 0.5, GV0 + t, kCageW, GVH - 2 * t, cageBar, 0.55);
    }

    // ---- G16 (NT2003 presentation plan): contingency decal chips ----
    // livery.h's note #6 previously listed these as "skipped for scope
    // control (low visual value relative to implementation cost)". That call
    // is reversed here: the NT2003 reference footage this phase targets shows
    // the lower rear quarter of every car carrying a dense row of small
    // colored sponsor chips, and it's one of the few era cues readable at
    // real chase-cam distance. Placed behind the rear wheel-arch shadow ring
    // (painted above at carU(-1.395) with radius 0.071, i.e. reaching
    // u=0.682) and stopping short of the tail column the new taillight
    // cluster below owns, on both side panels -- emit_quad()'s side faces
    // span v in [0.055,0.3] and [0.7,0.945] (gen_car_rig.py's own UV
    // comment), so these sit just above the near-black rocker band.
    {
        constexpr double kChipU0 = 0.688, kChipU1 = 0.750;
        constexpr int kChips = 6;
        const double chipW = (kChipU1 - kChipU0) / kChips;
        // Era-typical contingency-sticker colors. Indexed off the car's own
        // `idx` so each car gets a stable but distinct run of chips, matching
        // how `style`/`maskStyle` above already derive per-car variety.
        static const std::array<std::array<double, 3>, 6> kChipColors{{
            {0.86, 0.16, 0.14}, // red
            {0.96, 0.80, 0.10}, // yellow
            {0.13, 0.35, 0.76}, // blue
            {0.95, 0.95, 0.95}, // white
            {0.16, 0.58, 0.25}, // green
            {0.93, 0.48, 0.10}, // orange
        }};
        // J6 (car visual fidelity plan, part 2): a white/light backing
        // behind each chip, sized slightly larger and painted first, direct
        // port of JS's own two-layer sticker look (index.html:2942-2956: a
        // 12x12 light backing square with a 10x10 colored square inset 1px
        // on top) -- these chips were the one piece of that JS precedent
        // that shipped without it. Border sized in the same texel-fraction
        // idiom the A/B pillar edges above already use (2.0/
        // kLiveryTextureSize), so it auto-scales with J1's resolution bump
        // instead of being a fixed UV fraction picked once at 1024 and
        // going either too thin or too thick if the texture size changes
        // again.
        constexpr double kChipBorder = 2.0 / kLiveryTextureSize;
        const std::array<double, 3> chipBacking{240 / 255.0, 240 / 255.0, 240 / 255.0};
        for (double vy : {0.118, 0.846}) {
            for (int i = 0; i < kChips; ++i) {
                const auto& chip = kChipColors[(size_t)((idx + i) % (int)kChipColors.size())];
                const double cx = kChipU0 + i * chipW, cw = chipW * 0.78, ch = 0.034;
                c.fillRect(cx - kChipBorder, vy - kChipBorder, cw + 2 * kChipBorder, ch + 2 * kChipBorder,
                           chipBacking);
                c.fillRect(cx, vy, cw, ch, chip);
            }
        }
    }

    // ---- numbers (index.html:2732-2744; simplified per this file's note #3) ----
    const std::array<double, 3> white{250 / 255.0, 250 / 255.0, 250 / 255.0};
    const std::array<double, 3> dark{10 / 255.0, 10 / 255.0, 12 / 255.0};
    // G16: real Gen-4 cars carry the roof and door numbers on a contrasting
    // panel rather than floating them straight on the paint -- painted first
    // so drawNumber() below lands on top. Roof panel sits between the
    // windshield (ends at carU(0.02)=0.397) and the rear glass (starts at
    // carU(-1.00)=0.551), i.e. on the actual roof; the deck faces sample
    // v in [0.3,0.7] per gen_car_rig.py's is_top rule, so v=0.5 +- 0.08
    // stays on the roof band.
    const std::array<double, 3> panelFill = lum > 0.5 ? dark : white;
    const std::array<double, 3> panelNum = lum > 0.5 ? white : dark;
    if (paceLightBar) {
        // G20: amber light bar across the roof instead of a race number.
        // Deliberately over-bright (>1.0 before clamping) so that after
        // fs_lit.sc multiplies by the lighting amount it still clears the
        // bloom bright-pass threshold and glows -- see livery.h's note on
        // why this stands in for a real emissive material. A dark mounting
        // foot at each end keeps it reading as a fitted bar rather than a
        // painted stripe.
        c.fillRect(carU(-0.49) - 0.060, 0.452, 0.120, 0.020, dark);
        c.fillRect(carU(-0.49) - 0.060, 0.472, 0.120, 0.056, {1.0, 0.70, 0.10});
        c.fillRect(carU(-0.49) - 0.060, 0.528, 0.120, 0.020, dark);
        c.fillRect(carU(-0.49) - 0.062, 0.452, 0.014, 0.096, {0.30, 0.30, 0.33});
        c.fillRect(carU(-0.49) + 0.048, 0.452, 0.014, 0.096, {0.30, 0.30, 0.33});
    } else {
        c.fillRect(carU(-0.49) - 0.056, 0.420, 0.112, 0.160, panelFill);
        drawNumber(c, num, carU(-0.49), 0.50, 0.105, panelNum, dark);   // roof
    }
    for (double vy : {0.235, 0.765}) c.fillEllipse(carU(-0.10), vy, 0.072, 0.082, panelFill);
    if (!paceLightBar) {
        drawNumber(c, num, carU(-0.10), 0.235, 0.105, panelNum, dark);  // right door
        drawNumber(c, num, carU(-0.10), 0.765, 0.105, panelNum, dark);  // left door
    }

    // ---- nose/tail lamp clusters (index.html:2793-2855, re-placed) ----
    // G16 (NT2003 presentation plan) -- BUGFIX. These masks were previously
    // painted at u=0.845, which **no geometry has ever sampled**: livery.h's
    // note (2) placed them back when the car was a single flat top-down quad
    // and openly called them "never actually visible from the camera angles
    // this port supports". The G1/G8 3D loft since gave every face a real
    // wraparound UV, and gen_car_rig.py's emit_quad() (see its own UV
    // comment) pins the nose cap to u=0.02 and the tail cap to u=0.78 --
    // nowhere near 0.845, which sits in the dead margin between the body
    // wrap (u<=0.78) and the G1c/G8 swatch columns (u>=0.85). So the
    // headlights, taillights, deck-lid number and badge have all been
    // painted into unreachable texture space ever since the loft landed.
    //
    // Both caps are UV-degenerate in U (all four corners share one u), so
    // content must vary in **V** to read across the car's width: V runs
    // 0.055 -> 0.5 -> 0.945 along the cap's bottom edge and 0.3 -> 0.5 ->
    // 0.7 along its top edge, so a V band shows up as a vertical stripe on
    // the cap. Bands are placed symmetrically about V=0.5 accordingly. The
    // narrow U spread also catches the rearmost/foremost sliver of the side
    // panels, which reads correctly as a lamp wrapping around the corner.
    // This matters most for the tail: the chase camera stares at the car
    // ahead for an entire race, so the taillight cluster is the single
    // most-visible piece of car detail in the game.
    const int maskStyle = scheme ? scheme->mask : (idx) % 3;
    const std::array<double, 3> lampWhite{232 / 255.0, 232 / 255.0, 225 / 255.0};
    const std::array<double, 3> taillight{150 / 255.0, 18 / 255.0, 15 / 255.0};

    // Band placement follows the cap quad's real V extent rather than the
    // full [0,1] range: its corners are (bottom-left 0.055, bottom-right
    // 0.945, top-right 0.7, top-left 0.3), so across the cap's *mid-height*
    // V only spans 0.1775..0.8225. Bands sized against that, and the U
    // spread kept tight (~2% of body length past the wrap's 0.78 edge) so
    // the lamps wrap the corner onto the rear quarter without painting a
    // slab down the whole rearmost body segment.
    constexpr double kNoseU0 = 0.008, kNoseUW = 0.030;
    constexpr double kTailU0 = 0.764, kTailUW = 0.028;

    // Nose: grille block flanked by headlight lenses.
    c.fillRect(kNoseU0, 0.15, kNoseUW, 0.70, tone(0.94));
    c.fillRect(kNoseU0, 0.430, kNoseUW, 0.140, dark);                // grille
    // I5 (car visual fidelity plan): grille slats, replacing what was a
    // single flat dark rectangle. The nose cap's UV is degenerate in U
    // (every corner shares u=0.02, see the V-band note above), so V is the
    // only axis that varies across the car's width -- a thin V band here
    // paints as a narrow vertical slat down the nose, and three of them
    // give the opening internal structure at the distance the grille is
    // actually seen from. The centre slat reuses SW_RIM's exact RGB, so
    // fs_car.sc's I3 rim color-match fires on it and it gets the tight
    // chrome specular lobe for free -- no new shader constant and no new
    // threshold to re-check for collisions. The outer two are a duller gray
    // ~0.36 squared-distance from that reference, far outside I3's 0.01
    // match radius, so only the centre bar glints.
    for (double bv : {0.4575, 0.5315})
        c.fillRect(kNoseU0, bv, kNoseUW, 0.011, std::array<double, 3>{112 / 255.0, 114 / 255.0, 120 / 255.0});
    c.fillRect(kNoseU0, 0.4945, kNoseUW, 0.011, std::array<double, 3>{198 / 255.0, 200 / 255.0, 206 / 255.0});
    c.fillRect(kNoseU0, 0.412, kNoseUW, 0.012, accent);              // grille surround
    c.fillRect(kNoseU0, 0.576, kNoseUW, 0.012, accent);
    if (maskStyle == 0) {
        c.fillRect(kNoseU0, 0.290, kNoseUW, 0.105, lampWhite);
        c.fillRect(kNoseU0, 0.605, kNoseUW, 0.105, lampWhite);
    } else if (maskStyle == 1) {
        c.fillRect(kNoseU0, 0.275, kNoseUW, 0.120, lampWhite);
        c.fillRect(kNoseU0, 0.605, kNoseUW, 0.120, lampWhite);
        c.fillRect(kNoseU0, 0.327, kNoseUW, 0.014, tone(0.94));      // lens divider
        c.fillRect(kNoseU0, 0.659, kNoseUW, 0.014, tone(0.94));
    } else {
        for (double vy : {0.292, 0.345, 0.610, 0.663})
            c.fillRect(kNoseU0, vy, kNoseUW, 0.042, lampWhite);      // quad round lamps
    }

    // Tail: two wide taillight lenses split by a thin dark centre panel.
    c.fillRect(kTailU0, 0.15, kTailUW, 0.70, tone(0.86));
    if (maskStyle == 0) {
        c.fillRect(kTailU0, 0.310, kTailUW, 0.150, taillight);
        c.fillRect(kTailU0, 0.540, kTailUW, 0.150, taillight);
    } else if (maskStyle == 1) {
        c.fillRect(kTailU0, 0.295, kTailUW, 0.165, taillight);
        c.fillRect(kTailU0, 0.540, kTailUW, 0.165, taillight);
        c.fillRect(kTailU0, 0.365, kTailUW, 0.014, dark);            // lens divider
        c.fillRect(kTailU0, 0.621, kTailUW, 0.014, dark);
    } else {
        for (double vy : {0.310, 0.390, 0.540, 0.620})
            c.fillRect(kTailU0, vy, kTailUW, 0.062, taillight);      // stacked lenses
    }
    c.fillRect(kTailU0, 0.462, kTailUW, 0.076, dark);                // centre panel
    // J6 (car visual fidelity plan, part 2): manufacturer badge upgrade --
    // outline + bar, a direct port of JS's own drawBadge()
    // (index.html:2724-2732: fill ellipse + a stroked outline + a bar rect
    // in a third color) rather than a single flat ellipse. Canvas has no
    // stroke primitive, so the outline uses the same "paint a bigger shape
    // in the outline color first, then the existing smaller shape on top"
    // trick drawNumber()'s own outline-then-fill digits already establish
    // in this file. Bar dimensions/position are JS's own 0.80/0.34-of-size
    // fractions (call site: index.html:3028-3029) applied to this ellipse's
    // full width/height (2x its radii) rather than the radii themselves.
    {
        const double badgeCx = kTailU0 + kTailUW * 0.5, badgeCy = 0.50;
        const double badgeRx = 0.008, badgeRy = 0.018;
        c.fillEllipse(badgeCx, badgeCy, badgeRx * 1.25, badgeRy * 1.25, dark);   // outline
        c.fillEllipse(badgeCx, badgeCy, badgeRx, badgeRy, accent);              // fill
        const double bw = badgeRx * 2, bh = badgeRy * 2;
        c.fillRect(badgeCx - 0.40 * bw, badgeCy - 0.17 * bh, 0.80 * bw, 0.34 * bh, body, 0.9);  // bar
    }
    c.fillRect(kTailU0, 0.212, kTailUW, 0.018, accent);              // quarter-panel trim
    c.fillRect(kTailU0, 0.770, kTailUW, 0.018, accent);

    // Deck-lid number (index.html:2848-2855), moved onto the actual trunk
    // deck: the deck faces sit between the rear glass (ends at
    // carU(-1.75)=0.665) and the tail cap (0.78) and sample v in [0.3,0.7]
    // per emit_quad()'s is_top rule. Sized and centred so a 2-digit number
    // stays clear of the tail cluster's u range above.
    drawNumber(c, num, 0.706, 0.50, 0.062, white, dark);

    // G1c (NASCAR-Thunder gap-analysis plan, wheel/tire mesh upgrade): two
    // small fixed-color swatches in the U margin the body paint never
    // reaches (every fill above stays within u in [0, 0.80]) -- reviving
    // the concept of JS's SW.* solid-color swatches (index.html) for
    // gen_car_rig.py's new add_wheel() tread/sidewall faces to sample,
    // since livery.h's own comment previously called these "inapplicable"
    // only because the car had no separate wheel geometry to sample them;
    // that's no longer true. Painted last, after every other fill in this
    // function, so nothing here can be overwritten regardless of paint
    // order -- coordinates must match gen_car_rig.py's own
    // SW_TREAD/SW_SIDEWALL constants (loose cross-file sync, same
    // convention this codebase already uses for carU()/livery bands).
    // I1 (car visual fidelity plan): SW_SIDEWALL used to stand in for the
    // wheel cap's *entire* face (hence the mid-gray, "not solid black"
    // choice); now it's only the outer rubber annulus, so recolor it
    // near-black -- distinct from SW_TREAD's own near-black so the two
    // stay independently checkable, but visually reads the same "tire
    // rubber" as the reference images.
    c.fillRect(0.85, 0.0, 0.10, 0.5, std::array<double, 3>{12 / 255.0, 12 / 255.0, 13 / 255.0});  // tire rubber
    c.fillRect(0.85, 0.5, 0.10, 0.5, std::array<double, 3>{14 / 255.0, 14 / 255.0, 16 / 255.0});  // sidewall (outer annulus, rubber)

    // I1: the two new concentric wheel-cap bands, in the true open UV
    // margin (0.792, 0.85) -- livery.cpp's own nose/tail lamp decals
    // (kTailU0=0.764, kTailUW=0.028) reach u=0.792, past the body wrap's
    // U1=0.78, so u=0.80 was NOT actually free. u=0.83-0.85 stays reserved
    // for I2's mirror swatch. Coordinates must match gen_car_rig.py's
    // SW_TIRE_LETTER/SW_RIM constants.
    c.fillRect(0.80, 0.0, 0.03, 0.5, std::array<double, 3>{130 / 255.0, 130 / 255.0, 132 / 255.0});  // tire lettering band
    c.fillRect(0.80, 0.5, 0.03, 0.5, std::array<double, 3>{198 / 255.0, 200 / 255.0, 206 / 255.0});  // metallic rim/hub

    // I2 (car visual fidelity plan): the new door-mirror housing's swatch --
    // dark plastic/trim, a fixed color rather than sampling the door's own
    // livery UV so the mirror can't accidentally inherit whatever number/
    // stripe graphic happens to land at that exact body coordinate on a
    // given car's scheme. Coordinates must match gen_car_rig.py's SW_MIRROR.
    c.fillRect(0.825, 0.0, 0.02, 1.0, std::array<double, 3>{26 / 255.0, 26 / 255.0, 29 / 255.0});  // mirror housing

    // G8 (Gen-4 car overhaul): two more swatches for gen_car_rig.py's new
    // spoiler geometry, in the same reserved-margin column, a separate
    // band from the wheel swatches above (u in [0.95,1.0] vs [0.85,0.95])
    // -- coordinates must match gen_car_rig.py's SW_SPOILER_BODY/DARK.
    // Body swatch reuses this car's own base paint tone so the spoiler
    // reads as body-colored, not a fixed gray.
    c.fillRect(0.95, 0.0, 0.05, 0.5, tone(kBaseM));                                                // spoiler top (body color)
    c.fillRect(0.95, 0.5, 0.05, 0.5, std::array<double, 3>{10 / 255.0, 10 / 255.0, 12 / 255.0});  // spoiler underside/risers

    return downsampleBox(c.take(), kLiveryTextureSize * kSupersample, kSupersample);
}
