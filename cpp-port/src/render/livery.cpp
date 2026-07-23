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
        // outline: a slightly larger dark digit painted first
        drawDigit(c, d, x, fcy, fw * 1.18, fh * 1.14, outline);
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

std::vector<uint8_t> buildLiveryPixels(const Color3& body, int num, int idx, const LiveryScheme* scheme) {
    Canvas c(kLiveryTextureSize);

    // Base + 3-tone flat panel shading (index.html:2597-2603): rockers get
    // the shadow tone, the roof+hood band gets the highlight tone.
    constexpr double kShadowM = 0.70, kBaseM = 0.94, kHiliteM = 1.08;
    auto tone = [&](double m) { return std::array<double, 3>{body[0] * m, body[1] * m, body[2] * m}; };
    c.fillRect(0, 0, 1.0, 1.0, tone(kBaseM));
    c.fillRect(0, 0.00, 1.0, 0.11, tone(kShadowM));
    c.fillRect(0, 0.89, 1.0, 0.11, tone(kShadowM));
    c.fillRect(0, 0.40, 1.0, 0.20, tone(std::min(1.0, kHiliteM)));

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

    // rubber/dirt smudge behind rear wheel arches only (index.html:2680-2685).
    for (double vy : {0.055, 0.945}) {
        c.fillEllipse(carU(-1.395) + 0.035, vy, 0.05, 0.035, {10 / 255.0, 10 / 255.0, 12 / 255.0}, 0.35);
    }
    // exhaust soot smudge, right side only (index.html:2686-2691).
    c.fillEllipse(0.615, 0.058, 0.055, 0.028, {8 / 255.0, 8 / 255.0, 9 / 255.0}, 0.30);

    // ---- glass (index.html:2692-2727) ----
    const double uWS0 = carU(0.68), uWS1 = carU(0.02);
    const double uSG0 = carU(0.30), uSG1 = carU(-0.95);
    const double uRG0 = carU(-1.00), uRG1 = carU(-1.75);
    constexpr double GV0 = 0.335, GVH = 0.330;
    const std::array<double, 3> glassDark{16 / 255.0, 20 / 255.0, 30 / 255.0};
    c.fillRect(uWS0, GV0, uWS1 - uWS0, GVH, glassDark);
    c.fillRect(uRG0, GV0, uRG1 - uRG0, GVH, glassDark);
    c.fillRect(uSG0, 0.335, uSG1 - uSG0, 0.075, glassDark);
    c.fillRect(uSG0, 0.590, uSG1 - uSG0, 0.075, glassDark);
    const std::array<double, 3> glassHi{26 / 255.0, 33 / 255.0, 46 / 255.0};
    c.fillRect(uWS0, 0.47, uWS1 - uWS0, 0.06, glassHi);
    c.fillRect(uRG0, 0.48, uRG1 - uRG0, 0.04, glassHi);
    c.fillRect(uWS1 - 0.02, GV0 + 0.02, 0.02, GVH - 0.02, {235 / 255.0, 235 / 255.0, 238 / 255.0});
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
    // roof flaps
    c.fillRect(carU(-0.45), 0.435, 0.045, 0.052, tone(0.72));
    c.fillRect(carU(-0.45), 0.513, 0.045, 0.052, tone(0.72));

    // ---- numbers (index.html:2732-2744; simplified per this file's note #3) ----
    const std::array<double, 3> white{250 / 255.0, 250 / 255.0, 250 / 255.0};
    const std::array<double, 3> dark{10 / 255.0, 10 / 255.0, 12 / 255.0};
    drawNumber(c, num, carU(-0.49), 0.50, 0.105, white, dark);          // roof
    drawNumber(c, num, carU(-0.10), 0.235, 0.105, white, dark);         // right door
    drawNumber(c, num, carU(-0.10), 0.765, 0.105, white, dark);         // left door

    // ---- nose/tail masks (index.html:2793-2855) ----
    constexpr double NIu = 0.845, NIv = 0.24, TIu = 0.845, TIv = 0.74;
    const int maskStyle = scheme ? scheme->mask : (idx) % 3;
    c.fillRect(NIu - 0.07, NIv - 0.06, 0.14, 0.12, tone(0.96));
    c.fillRect(NIu - 0.068, NIv - 0.040, 0.136, 0.016, accent);
    if (maskStyle == 0) {
        c.fillRect(NIu - 0.052, NIv - 0.018, 0.038, 0.036, {232 / 255.0, 232 / 255.0, 225 / 255.0});
        c.fillRect(NIu + 0.014, NIv - 0.018, 0.038, 0.036, {232 / 255.0, 232 / 255.0, 225 / 255.0});
        c.fillRect(NIu - 0.058, NIv + 0.026, 0.116, 0.013, dark);
    } else if (maskStyle == 1) {
        c.fillRect(NIu - 0.062, NIv - 0.014, 0.124, 0.026, {232 / 255.0, 232 / 255.0, 225 / 255.0});
        c.fillRect(NIu - 0.006, NIv - 0.014, 0.012, 0.026, tone(0.96));
        c.fillRect(NIu - 0.050, NIv + 0.022, 0.100, 0.011, dark);
    } else {
        for (double dx : {-0.050, -0.020, 0.020, 0.050})
            c.fillCircle(NIu + dx, NIv - 0.002, 0.013, {232 / 255.0, 232 / 255.0, 225 / 255.0});
        c.fillEllipse(NIu, NIv + 0.030, 0.044, 0.011, dark);
    }
    c.fillRect(TIu - 0.07, TIv - 0.12, 0.14, 0.24, tone(0.9));
    const std::array<double, 3> taillight{120 / 255.0, 14 / 255.0, 12 / 255.0};
    if (maskStyle == 0) {
        c.fillRect(TIu - 0.062, TIv - 0.008, 0.124, 0.042, taillight);
        c.fillRect(TIu - 0.066, TIv + 0.045, 0.132, 0.052, dark);
    } else if (maskStyle == 1) {
        c.fillRect(TIu - 0.066, TIv - 0.020, 0.132, 0.028, taillight);
        c.fillRect(TIu - 0.066, TIv + 0.045, 0.132, 0.052, dark);
    } else {
        c.fillRect(TIu - 0.062, TIv - 0.008, 0.050, 0.042, taillight);
        c.fillRect(TIu + 0.012, TIv - 0.008, 0.050, 0.042, taillight);
        c.fillRect(TIu - 0.012, TIv - 0.008, 0.024, 0.042, dark);
        c.fillRect(TIu - 0.066, TIv + 0.045, 0.132, 0.052, dark);
    }
    // small deck-lid number + badge (index.html:2848-2855)
    drawNumber(c, num, TIu, TIv - 0.086, 0.05, white, dark);
    c.fillEllipse(TIu, TIv - 0.052, 0.018, 0.009, accent);
    c.fillRect(TIu - 0.0144, TIv - 0.0535, 0.0288, 0.003, tone(0.9));

    return c.take();
}
