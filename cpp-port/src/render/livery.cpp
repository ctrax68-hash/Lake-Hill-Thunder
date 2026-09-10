#include "livery.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

#include "font_atlas.h"

namespace {

// T4: decoded once for the whole process. Every car's livery is painted
// through this, and decodeAtlas() re-inflates a PNG each call.
const font::AtlasImage& liveryFontAtlas() {
    static const font::AtlasImage kAtlas = font::decodeAtlas();
    return kAtlas;
}

std::array<double, 3> mixC(const std::array<double, 3>& a, const std::array<double, 3>& b, double t) {
    return {a[0] + (b[0] - a[0]) * t, a[1] + (b[1] - a[1]) * t, a[2] + (b[2] - a[2]) * t};
}

// T12: GLOSS, written into the livery's alpha channel.
//
// fs_car.sc applied the same reflectivity to every texel on the car -- a 0.16
// base plus Fresnel, paint and rubber and vinyl alike. Measured on a rendered
// frame, a BLACK TIRE came out (42, 72, 109): a blue-grey, because it was
// reflecting 16% of a bright sky. The body's green (11, 131, 2) rendered
// (31, 229, 138), its blue channel lifted from 2 to 138 by the same term.
// That uniform sky wash over every material is what reads as chalky plastic,
// and no amount of livery or geometry work can cancel it.
//
// The alpha channel was carrying a hardcoded 255 on every pixel and nothing
// read it, so the mask is free -- no second texture, no extra fetch, no
// vertex data. It is deliberately a SCALAR rather than a set of colour
// branches: a scalar survives mip filtering, which is exactly where
// fs_car.sc's RGB colour-distance material tests decayed -- at distance the
// filtered texel drifts off the reference colour and glass/chrome silently
// stop being glass/chrome, in the pack, at the distances where most cars are
// actually seen.
//
// Values are reflectivity, not shininess: what fraction of the environment
// this material returns. They live in livery.h so livery_test can assert
// against the real numbers rather than a second copy of them.

class Canvas {
public:
    explicit Canvas(int size) : size_(size), pixels_((size_t)size * size * 4, 0) {}

    // The gloss written by every subsequent blend(). Set it around a block
    // that paints one material and set it back; the default is body paint,
    // which is the overwhelming majority of the texture.
    void setGloss(double g) { gloss_ = std::clamp(g, 0.0, 1.0); }
    double gloss() const { return gloss_; }

    // Sets gloss for its lifetime and restores whatever was there before.
    // Used rather than paired set calls because the painting code below has
    // early-outs and loops in it, and a missed restore silently makes a whole
    // panel the wrong material -- the kind of defect that only shows up as
    // "something looks a bit off" from one angle.
    class ScopedGloss {
    public:
        ScopedGloss(Canvas& c, double g) : c_(c), prev_(c.gloss()) { c.setGloss(g); }
        ~ScopedGloss() { c_.setGloss(prev_); }
        ScopedGloss(const ScopedGloss&) = delete;
        ScopedGloss& operator=(const ScopedGloss&) = delete;

    private:
        Canvas& c_;
        double prev_;
    };

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

    // T4: blit a wordmark into the texture, reusing G26's baked glyph atlas.
    //
    // The reference photographs are covered in graphics -- a contingency
    // sticker strip along the lower doors, sponsor blocks on the quarters,
    // wordmarks on hood and deck. Ours had large blank painted panels, and
    // that density is most of what separates a race car from a coloured shape
    // at chase-cam distance. It also costs no geometry, which is why it is
    // worth more per unit of effort than anything else left.
    //
    // This deliberately does NOT introduce a second font path. font::pushText
    // already lays out a string with the atlas's real metrics and kerning; it
    // just emits GPU quads instead of pixels. So we call it, then rasterise
    // the quads it produced against the decoded atlas. Layout stays in exactly
    // one place, and a future atlas rebake carries through here for free.
    //
    // `fh` is CAP HEIGHT as a fraction of the texture, not the em size, so a
    // caller can size a decal against the panel it sits on. Non-player cars
    // only get a 1024 livery (see livery.h's memory note), so anything below
    // roughly 0.010 of the texture turns to mush in the pack -- callers are
    // responsible for staying above that, and the test pins it.
    // `mirrorU` flips the string about its own left edge, so it reads
    // correctly on the flank whose U axis runs the other way across the
    // screen. The body is a lofted TUBE: u is the same function of x on both
    // sides, so going around the ring reverses which screen direction u
    // advances in, and a string that reads forward on one flank necessarily
    // reads backward on the other. The 3D render is the only thing that can
    // say which -- see the T4 note where the marks are placed.
    void drawText(double fx, double fy, double fh, const std::string& text,
                  const std::array<double, 3>& color, double alpha = 1.0, bool centered = false,
                  bool mirrorU = false) {
        const font::AtlasImage& atlas = liveryFontAtlas();
        if (!atlas.ok || text.empty()) return;

        // ascent() scales linearly with pixelSize, so one division converts a
        // wanted cap height into the pixelSize that produces it.
        const double baked = (double)font::bakedPixelSize();
        const double ascentAtBaked = font::ascent((float)baked);
        if (ascentAtBaked <= 0.0) return;
        const double target = fh * size_;
        const float px = (float)(target * baked / ascentAtBaked);

        std::vector<PosColorUvVertex> quads;
        font::pushText(quads, 0.0f, font::ascent(px), text, px, 0xffffffffu);
        if (quads.empty()) return;

        double ox = fx * size_;
        if (centered) ox -= font::measure(text, px) * 0.5;
        const double oy = fy * size_;

        // pushText emits 6 vertices per glyph; [0] is the top-left corner and
        // [2] the bottom-right, which is all a blit needs.
        const double runW = font::measure(text, px);
        for (size_t g = 0; g + 5 < quads.size(); g += 6) {
            const PosColorUvVertex& a = quads[g];
            const PosColorUvVertex& b = quads[g + 2];
            // Mirror the glyph's BOX about the run's centre as well as its
            // sampling, so the whole string reverses rather than each letter
            // being flipped in place.
            const double gx0 = mirrorU ? runW - b.x : a.x;
            const double gx1 = mirrorU ? runW - a.x : b.x;
            const int x0 = (int)std::floor(ox + gx0), x1 = (int)std::ceil(ox + gx1);
            const int y0 = (int)std::floor(oy + a.y), y1 = (int)std::ceil(oy + b.y);
            if (x1 <= x0 || y1 <= y0) continue;
            for (int py = std::max(0, y0); py < std::min(size_, y1); ++py) {
                for (int pxx = std::max(0, x0); pxx < std::min(size_, x1); ++pxx) {
                    double tx = (pxx + 0.5 - (ox + gx0)) / (gx1 - gx0);
                    const double ty = (py + 0.5 - (oy + a.y)) / (double)(b.y - a.y);
                    if (tx < 0.0 || tx >= 1.0 || ty < 0.0 || ty >= 1.0) continue;
                    if (mirrorU) tx = 1.0 - tx;
                    const double u = a.u + (b.u - a.u) * tx, v = a.v + (b.v - a.v) * ty;
                    const int ax = std::clamp((int)(u * atlas.width), 0, atlas.width - 1);
                    const int ay = std::clamp((int)(v * atlas.height), 0, atlas.height - 1);
                    // The atlas is 8-bit coverage expanded to RGBA, so any
                    // channel is the coverage; red is as good as alpha and
                    // needs no swizzle on the GLES2 path.
                    const double cov = atlas.rgba8[((size_t)ay * atlas.width + ax) * 4] / 255.0;
                    if (cov > 0.004) blend(pxx, py, color, alpha * cov);
                }
            }
        }
    }

    // Width the same call would occupy, as a fraction of the texture -- so a
    // caller can lay a row of wordmarks out without guessing.
    double measureText(double fh, const std::string& text) const {
        const double baked = (double)font::bakedPixelSize();
        const double ascentAtBaked = font::ascent((float)baked);
        if (ascentAtBaked <= 0.0) return 0.0;
        const float px = (float)(fh * size_ * baked / ascentAtBaked);
        return font::measure(text, px) / (double)size_;
    }

    // T10: flip a rectangle of already-painted pixels left-to-right.
    //
    // The body is a lofted tube and carU() runs nose->tail, so a graphic laid
    // out in increasing u comes out BACKWARDS on both flanks -- established in
    // T4, where every sponsor wordmark shipped mirrored until it was measured
    // at 2560x1440. The wordmarks were fixed then and the DOOR NUMBERS were
    // not, which is exactly what the user reported: "numbers are backwards".
    //
    // Mirroring the finished pixels rather than the 7-segment rasteriser is
    // deliberate: it flips the digit shapes, the heavier outline behind them
    // and the inter-digit order all at once, and it cannot drift out of sync
    // with drawNumber()'s own layout the way a parallel mirrored code path
    // would.
    void mirrorRegionX(double fx, double fy, double fw, double fh) {
        const int x0 = std::max(0, (int)std::lround(fx * size_));
        const int y0 = std::max(0, (int)std::lround(fy * size_));
        const int x1 = std::min(size_, (int)std::lround((fx + fw) * size_));
        const int y1 = std::min(size_, (int)std::lround((fy + fh) * size_));
        for (int py = y0; py < y1; ++py) {
            for (int a = x0, b = x1 - 1; a < b; ++a, --b) {
                const size_t ia = ((size_t)py * size_ + (size_t)a) * 4;
                const size_t ib = ((size_t)py * size_ + (size_t)b) * 4;
                for (int k = 0; k < 4; ++k) std::swap(pixels_[ia + k], pixels_[ib + k]);
            }
        }
    }

    // T13: AMBIENT OCCLUSION. Darkens a band running from `fvEdge` (fully
    // occluded) to `fvInner` (open), smoothstepped between, across [fu0, fu1).
    // Works in either V direction, so the same call shape serves the +z flank
    // (edge at v 0.985, inner below it) and the -z flank (edge at 0.015,
    // inner above) without a second code path.
    //
    // It scales the GLOSS channel by the same factor as the colour, and that
    // is the whole reason this is worth doing rather than just painting a dark
    // stripe. Gloss drives fs_car.sc's reflectMix, so attenuating it is
    // literally "this surface can see less of the environment" -- which is
    // what occlusion IS. Darkening albedo alone would leave the recess still
    // reflecting a full share of bright sky, and the sky is most of what makes
    // the wheelhouse read as a lit blister instead of a hole.
    void occludeBand(double fu0, double fu1, double fvEdge, double fvInner, double strength) {
        const int x0 = std::max(0, (int)std::lround(fu0 * size_));
        const int x1 = std::min(size_, (int)std::lround(fu1 * size_));
        const double vLo = std::min(fvEdge, fvInner), vHi = std::max(fvEdge, fvInner);
        const int y0 = std::max(0, (int)std::lround(vLo * size_));
        const int y1 = std::min(size_, (int)std::lround(vHi * size_));
        const double span = fvInner - fvEdge;
        if (std::fabs(span) < 1e-9) return;
        for (int py = y0; py < y1; ++py) {
            const double v = (py + 0.5) / size_;
            double t = std::clamp((v - fvEdge) / span, 0.0, 1.0);  // 0 at the edge, 1 at open
            const double w = 1.0 - t * t * (3.0 - 2.0 * t);        // smoothstep, inverted
            const double f = 1.0 - strength * w;
            for (int px = x0; px < x1; ++px) {
                const size_t idx = ((size_t)py * size_ + (size_t)px) * 4;
                for (int k = 0; k < 4; ++k) {
                    pixels_[idx + k] = (uint8_t)std::lround(pixels_[idx + k] * f);
                }
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
        // T12: gloss composites exactly like colour does. A decal blitted at
        // partial coverage (drawText's antialiased glyph edges, a 0.90-alpha
        // sponsor plate) must blend its material into what is underneath, or
        // every glyph would be ringed by a one-texel band of the wrong
        // reflectivity -- visible as a shimmer along lettering as the car
        // moves, which is worse than no mask at all.
        const double ea = pixels_[idx + 3] / 255.0;
        const double a = std::clamp(ea * (1 - alpha) + gloss_ * alpha, 0.0, 1.0);
        pixels_[idx + 3] = (uint8_t)std::lround(a * 255.0);
    }

    int size_;
    std::vector<uint8_t> pixels_;
    double gloss_ = kGlossPaint;
};

// carU() (index.html:2249): nose (x=2.51) -> 0.02, tail (x=-2.51) -> 0.78.
//
// NOTE ON DOMAIN, because it has now caught two people. This takes RAW JS-scale
// x. gen_car_rig.py's car_u() takes ITS station x, which is the same coordinate
// already multiplied by HALF_LEN/2.51. The two functions agree at corresponding
// points by construction -- that scaling is the entire reason it exists -- but
// they are NOT interchangeable given the same number, and feeding one x to both
// produces a plausible-looking disagreement of up to 0.0044 that is pure domain
// error. check_car_rig.py's own glass-U section carries the same warning after
// its first draft made exactly this mistake.
double carU(double x) {
    return 0.02 + (2.51 - x) / 5.02 * 0.76;
}

// T13: WHERE THE WHEEL ARCHES ACTUALLY ARE, in this texture's UV.
//
// Derived from gen_car_rig.py rather than estimated: the U spans are the
// footprint of the stations its _arch_lip_y() genuinely carves an opening
// into, and kArchLipV is RINGV[K_LIP] -- the fender's bottom lip, the edge
// where the recess stops. check_car_rig.py recomputes all five from the
// generator and fails if this file drifts from them, the same guard the tail
// island already has.
//
// They exist because the two decorations that had been standing in for the
// arches -- the JS shadow rings and the H2 lip highlight -- were both placed
// by hand against an older axle position and never moved when the wheels did.
constexpr double kArchFrontU0 = 0.0878, kArchFrontU1 = 0.2285;
constexpr double kArchRearU0 = 0.5052, kArchRearU1 = 0.6528;
constexpr double kArchFrontCU = (kArchFrontU0 + kArchFrontU1) * 0.5;
constexpr double kArchRearCU = (kArchRearU0 + kArchRearU1) * 0.5;
constexpr double kArchLipV = 0.8848;  // RINGV[K_LIP]

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
                const std::array<double, 3>& outline, bool mirrorU = false) {
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

    // T10: flip the finished digits so they read forwards on the car. The
    // box is sized from the same totalW/fh the layout above used, widened by
    // the 1.28x outline ratio plus a little slack, so the mirror covers the
    // whole graphic and nothing beyond it.
    if (mirrorU) {
        const double W = totalW * 1.34, H = fh * 1.34;
        c.mirrorRegionX(fcx - W / 2, fcy - H / 2, W, H);
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
        //   rear win  u 0.556-0.658: roughly under the rear glass's own u
        //             span (K2: [0.551,0.609] after correcting uRG1, was
        //             [0.551,0.665] before -- these patches don't actually
        //             depend on the exact glass bound either way, since
        //             their v 0.244-0.308 sits on the sail panel below the
        //             glass rect entirely (glass paints v in [0.335,0.665]
        //             only), stopping short of the beltline seam (0.320).
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
    //
    // T13: THESE WERE PAINTED 0.031 OF THE TEXTURE AWAY FROM THE ARCHES.
    //
    // The centres came straight from the JS original as carU(+-1.395), and
    // they were right when they were written. Then the axles moved -- the
    // round that found the wheels sitting at +-WHEELBASE/2, centred in the
    // body like a generic car instead of a Cup car's short nose and long deck
    // -- and nothing brought these with them. The real openings are at
    // u 0.158 and 0.576; the shadow was being painted at 0.189 and 0.611,
    // about 0.16 m along the car, so the dark cavity sat PARTLY ON THE FENDER
    // beside each opening. Nothing failed, because no guard related what this
    // file paints to where gen_car_rig.py actually cuts the hole.
    //
    // The radius was fine: 0.071 against a real arch half-span of 0.0704.
    // It was only ever in the wrong place.
    for (double ux : {kArchFrontCU, kArchRearCU}) {
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
    // T13: same correction as the shadow rings above, plus two more. The V was
    // hand-placed at 0.812/0.182 from an estimate of "ring index k=2/3", and
    // the real lip -- RINGV[K_LIP], which is what K_LIP MEANS -- is at 0.8848,
    // so the highlight was drawn 0.073 below the edge it represents, out on
    // the open fender. And the rect was 0.056 wide against an arch opening
    // 0.1407 wide, so it covered 40% of the lip it was meant to trace.
    //
    // The old comment admitted the derivation was by eye ("not re-derived from
    // gen_car_rig.py here (this file has no import path to it)"). There is
    // still no import path; what changed is that check_car_rig.py now reads
    // these constants back out of this file and checks them against the
    // generator, which is the same answer used for the tail island.
    const std::array<double, 3> archLip{
        std::min(1.0, body[0] * 1.35 + 0.05), std::min(1.0, body[1] * 1.35 + 0.05), std::min(1.0, body[2] * 1.35 + 0.05)};
    for (const auto& arch : {std::pair{kArchFrontU0, kArchFrontU1}, std::pair{kArchRearU0, kArchRearU1}}) {
        const double w = arch.second - arch.first;
        c.fillRect(arch.first, kArchLipV - 0.003, w, 0.006, archLip, 0.55);
        c.fillRect(arch.first, 1.0 - kArchLipV - 0.003, w, 0.006, archLip, 0.55);
    }

    // rubber/dirt smudge behind rear wheel arches only (index.html:2680-2685).
    for (double vy : {0.055, 0.945}) {
        c.fillEllipse(carU(-1.395) + 0.035, vy, 0.05, 0.035, {10 / 255.0, 10 / 255.0, 12 / 255.0}, 0.35);
    }
    // exhaust soot smudge, right side only (index.html:2686-2691).
    c.fillEllipse(0.615, 0.058, 0.055, 0.028, {8 / 255.0, 8 / 255.0, 9 / 255.0}, 0.30);

    // kSeamW hoisted up from the panel-shutlines section below (it used to
    // be declared there) so K2's glass-rect bounds, right below, can reuse
    // it as their own inward reveal margin instead of a new magic number.
    constexpr double kSeamW = 0.0035;

    // Glass rect bounds, hoisted above both the H2 shutlines (which anchor
    // to the windshield/rear-glass edges) and the glass section itself
    // below (index.html:2692-2727), which is where these were originally
    // declared.
    // R1: re-anchored to the Gen-4 station table. These are expressed as
    // carU() of the real station x they belong to, so they move with the
    // geometry rather than being independent numbers -- the cowl went 0.68 ->
    // 0.80 and the A-pillar top 0.02 -> 0.28 when the greenhouse became a
    // notchback, and check_car_rig.py asserts these still land on the real
    // stations.
    // R2b: A-pillar 0.28 -> 0.35. The roof plateau grew and the backlite got
    // steeper when the vertical profile was corrected; both roof stations moved.
    // T6: cowl 0.80 -> 0.585 and A-pillar 0.35 -> 0.03. The whole greenhouse
    // moved back when the silhouette was re-authored against the reference
    // photo -- the cabin had been sitting far too far forward (cowl 0.21 of a
    // wheelbase behind the front axle against the reference's 0.367).
    const double uWS0 = carU(0.585), uWS1 = carU(0.03);
    // K2 (car visual fidelity plan, part 3): uSG0 used to be carU(0.30),
    // which landed 42% of the way inside the windshield's own real U-range
    // [uWS0,uWS1] -- the side window started well past "windshield mid"
    // toward the A-pillar. The windshield's own sun-strip accent band
    // (painted after the side-glass base fill, below) fell entirely inside
    // that overlap and visibly bled onto both front door windows -- a real
    // "bad window lines" bug, not just a theoretical one, found while
    // investigating the rear-glass bug below. Anchored instead to the
    // A-pillar station, the same real boundary uWS1 already uses, with the
    // same kSeamW reveal margin every other seam in this file uses.
    // R1: A-pillar 0.02 -> 0.28, and uSG1 -0.95 -> -0.67 so the side glass
    // still ends just ahead of where the rear glass now starts (the C-pillar
    // moved forward from -1.00 to -0.72 with the notchback roof).
    // R2b: A-pillar 0.28 -> 0.35, and uSG1 -0.67 -> -0.90 so the side glass
    // still ends just ahead of the C-pillar, which moved back to -0.95.
    const double uSG0 = carU(0.03) + kSeamW, uSG1 = carU(-0.74);
    // K2: uRG1 used to be carU(-1.75) (station 12, "deck start"), but the
    // real glass-adjacent roofline rise ends two stations earlier, at
    // carU(-1.40) (station 11, "rear axle... belt/roof rejoin" -- beltY
    // and roofY meet again there). The painted rect was 1.875x the real
    // glass width; ~47% of it landed on the opaque trunk decklid, not
    // glass. Pulled in to the real boundary with the same kSeamW reveal
    // margin, so trunk paint can't bleed onto glass at the seam either.
    // R1: uRG0 -1.00 -> -0.72. The old value was the "fastback glass start"
    // station; the notchback puts the rear glass between the C-pillar top
    // (-0.72) and the rear axle where belt and roof rejoin (-1.40, unchanged).
    // R2b: C-pillar -0.72 -> -0.95 (a Gen-4 backlite is steeper than its
    // windshield; R1 had it shallower, which read as a fastback slope).
    // T6: the backlite is long and SHALLOW on a real Gen-4 (21 deg, measured
    // off the reference), so the rear glass now runs from the roof trailing
    // edge all the way back to where the deck starts.
    const double uRG0 = carU(-0.78), uRG1 = carU(-1.67) - kSeamW;
    constexpr double GV0 = 0.335, GVH = 0.330;
    // K2: the beltline V-span (GV0/GVH) is inset only ~0.016 from the real
    // beltline [car_v(4),car_v(9)]=[0.319,0.681] (thin but non-zero --
    // glass still stays fully inside the real opening, unlike the two U-
    // axis bugs above which actually overshot). Deliberately left alone in
    // this phase: the beltline character line just below is a SEPARATELY
    // hardcoded 0.320/0.677 pair despite its own comment claiming it
    // tracks GV0/GVH, and the door-number badge clearance math was tuned
    // against the current band -- tightening GV0/GVH correctly means a
    // coordinated multi-constant change, not the single-value fix this
    // phase is otherwise scoped as. check_car_rig.py's own new checks
    // record the real target span so this is ready to pick up later.

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
    // T12: everything in this block is glass, so the whole block is scoped to
    // kGlossGlass. This is the mask's single biggest contribution: the
    // greenhouse was measured rendering as a pale tan blob, its dark navy
    // paint (lum 0.12) coming out around 0.42, because it took the same sky
    // wash as the bodywork and had nothing to say it was a mirror.
    {
        Canvas::ScopedGloss glassGloss(c, kGlossGlass);
        const std::array<double, 3> glassDark{16 / 255.0, 20 / 255.0, 30 / 255.0};
        c.fillRect(uWS0, GV0, uWS1 - uWS0, GVH, glassDark);
        c.fillRect(uRG0, GV0, uRG1 - uRG0, GVH, glassDark);
        c.fillRect(uSG0, 0.335, uSG1 - uSG0, 0.075, glassDark);
        c.fillRect(uSG0, 0.590, uSG1 - uSG0, 0.075, glassDark);
        const std::array<double, 3> glassHi{26 / 255.0, 33 / 255.0, 46 / 255.0};
        c.fillRect(uWS0, 0.47, uWS1 - uWS0, 0.06, glassHi);
        c.fillRect(uRG0, 0.48, uRG1 - uRG0, 0.04, glassHi);
    }
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
        // T12: painted tubing inside the car, not glass -- it must NOT take
        // the greenhouse's mirror response, or the cage reads as a bright
        // streak on the window rather than structure behind it.
        Canvas::ScopedGloss cageGloss(c, kGlossMatte);
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

    // ---- T4: wordmarks ----
    //
    // G16 put the contingency STRIP in and left it as blank coloured chips,
    // because there was no way to draw text into a texture. G26's font atlas
    // changed that and nothing came back to use it. Every reference car is
    // covered in lettering, and blank panels are most of what still reads as
    // "coloured shape" rather than "race car" at chase-cam distance.
    //
    // ALL NAMES ARE INVENTED. Standing project rule: match the layout and the
    // density of the real thing, never its branding. These are deliberately
    // synthetic words that no series has ever run.
    {
        static const std::array<const char*, 12> kBrands{{
            "VALKOR", "NORVAL", "KESTREL", "HALVERN", "TORQ-9", "DRIFTLINE",
            "IRONWAY", "CROSSCUT", "LUMEN", "SABLECO", "REDSHIFT", "APEXA",
        }};
        static const std::array<const char*, 6> kSmall{{
            "AXLON", "PRIMEX", "VERTAC", "OKAMI", "BRIGHT", "NUFUEL",
        }};
        const std::array<double, 3> inkDark{18 / 255.0, 18 / 255.0, 22 / 255.0};
        const std::array<double, 3> inkLight{244 / 255.0, 244 / 255.0, 246 / 255.0};

        // Every mark gets its OWN backing plate rather than being painted
        // straight onto the paint. Two reasons, and the first is a bug the
        // LHT_DUMP_LIVERY dump caught: choosing ink from the car's BODY
        // luminance is wrong, because a mark does not necessarily land on body
        // colour -- the quarter wordmark on a green car was landing on a white
        // scheme panel in light ink and disappearing completely. A plate makes
        // contrast independent of whatever the scheme put underneath.
        //
        // The second reason is that it is what the reference actually shows: a
        // real sponsor decal is printed on its own background and applied on
        // top of the paint, not painted into it.
        // Exactly ONE half is mirrored: v > 0.5. That IS what the tube
        // argument predicts -- u is the same function of x all the way round,
        // so going over the roof reverses which screen direction u advances
        // in, and precisely one flank comes out backwards.
        //
        // It took four attempts to land, and every wrong one came from trying
        // to read six-pixel glyphs in a turntable tile. What settled it was a
        // single-sided probe: draw one distinctive string at v = 0.79 ONLY,
        // unmirrored, somewhere known to be legible, and render both flanks at
        // 2560x1440. It appears at azimuth 57, reversed, and is absent at 237.
        // So v > 0.5 is the flank seen at 57 and it is the half to mirror.
        //
        // T4 shipped "mirror both", which is right on one flank and wrong on
        // the other; the verification angles I used then happened to show the
        // same flank twice.
        auto badge = [&](double x, double vy, double h, const char* t, bool lightPlate, double a) {
            // T12: a sponsor decal is printed vinyl applied over the paint. It
            // is noticeably flatter than the clearcoat around it, and saying so
            // is what stops the car reading as one uniformly shiny shell.
            Canvas::ScopedGloss decalGloss(c, kGlossDecal);
            const double w = c.measureText(h, t);
            const double padX = h * 0.45, padY = h * 0.40;
            const std::array<double, 3>& plate = lightPlate ? inkLight : inkDark;
            const std::array<double, 3>& ink = lightPlate ? inkDark : inkLight;
            c.fillRect(x - padX, vy - padY, w + 2 * padX, h + 2 * padY, plate, a);
            c.drawText(x, vy, h, t, ink, 1.0, false, vy > 0.5);
        };

        const char* primary = kBrands[(size_t)(idx % (int)kBrands.size())];
        const char* second = kSmall[(size_t)((idx + 3) % (int)kSmall.size())];
        const char* third = kBrands[(size_t)((idx + 7) % (int)kBrands.size())];

        // Cap height 0.020 of the texture: 41 texels at 2048 and still 20 at
        // the 1024 non-player cars get, comfortably above the ~4-texel floor
        // where a glyph stops resolving in the pack.
        constexpr double kMarkH = 0.020;

        // EVERY mark below is placed against its own measured width. The first
        // cut hardcoded left edges and the LHT_DUMP_LIVERY dump showed exactly
        // why that does not work: the quarter wordmark ran off the end of the
        // body's U span and the deck one landed underneath the tail number.
        // Brand names here differ in length by more than 2x, so a fixed left
        // edge is only ever correct for one of them.
        // Quarter-panel sponsor, both sides: right-aligned against its own
        // measured width so it stops clear of the tail column the taillight
        // cluster owns. The first cut hardcoded a left edge and the dump showed
        // it running off the end of the body's U span -- brand names here
        // differ in length by more than 2x, so one fixed edge cannot fit them.
        for (double vy : {0.170, 0.790}) {
            badge(0.752 - c.measureText(kMarkH, primary), vy, kMarkH, primary, false, 0.90);
        }
        // Associate mark forward of the door number, where the real cars carry
        // one, and a third on the lower door above the rocker band. 0.185 for
        // the second, not 0.310: at 0.310 the dump showed it buried under the
        // door-number roundel.
        for (double vy : {0.145, 0.815}) badge(0.290, vy, 0.014, second, true, 0.90);
        for (double vy : {0.262, 0.702}) badge(0.185, vy, 0.013, third, false, 0.85);

        // Hood wordmark, centred on the hood's own U span. Reads nose-on in
        // every mirror. Plated like the rest: the scheme's own white blocks sit
        // right here on several styles and a bare mark half-vanishes into them.
        badge(0.170 - c.measureText(0.024, primary) * 0.5, 0.480, 0.024, primary, false, 0.88);
        // Deck wordmark at 0.62, not 0.70: the dump caught it landing directly
        // underneath the tail number.
        badge(0.620 - c.measureText(0.018, second) * 0.5, 0.480, 0.018, second, false, 0.88);

        // A lettered contingency row beside the chips, which G16 left as blank
        // colour blocks because when it shipped there was no way to draw text
        // into a texture at all. Light plates with dark text, which is what a
        // real contingency sticker is.
        static const std::array<const char*, 4> kTiny{{"OKAMI", "AXLON", "VERTAC", "NUFUEL"}};
        for (double vy : {0.070, 0.906}) {
            double ux = 0.690;
            for (int i = 0; i < 3; ++i) {
                const char* t = kTiny[(size_t)((idx + i) % (int)kTiny.size())];
                const double w = c.measureText(0.011, t);
                if (ux + w > 0.760) break;
                badge(ux, vy, 0.011, t, true, 0.92);
                ux += w + 0.010;
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
        // T10: the v > 0.5 door only, matching the wordmark rule above. The
        // numbers were drawn unmirrored on BOTH doors, so they read backwards
        // on the v > 0.5 flank -- which is what "numbers are backwards" was.
        drawNumber(c, num, carU(-0.10), 0.235, 0.105, panelNum, dark, false);  // right door
        drawNumber(c, num, carU(-0.10), 0.765, 0.105, panelNum, dark, true);   // left door
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
    {
        Canvas::ScopedGloss grilleGloss(c, kGlossMatte);  // T12: an opening, not paint
        c.fillRect(kNoseU0, 0.430, kNoseUW, 0.140, dark);            // grille
    }
    // I5 (car visual fidelity plan): grille slats, replacing what was a
    // single flat dark rectangle. The nose cap's UV is degenerate in U
    // (every corner shares u=0.02, see the V-band note above), so V is the
    // only axis that varies across the car's width -- a thin V band here
    // paints as a narrow vertical slat down the nose, and three of them
    // give the opening internal structure at the distance the grille is
    // actually seen from.
    //
    // T12: the centre slat used to be chrome by COINCIDENCE -- it reused
    // SW_RIM's exact RGB so fs_car.sc's colour-distance rim test would fire
    // on it. That trick is gone with the colour branches, and it is no loss:
    // "this texel is chrome" is now stated directly in the gloss mask instead
    // of being smuggled through a shared RGB constant that any future recolour
    // would have silently broken.
    {
        Canvas::ScopedGloss slatGloss(c, kGlossMatte);
        for (double bv : {0.4575, 0.5315})
            c.fillRect(kNoseU0, bv, kNoseUW, 0.011, std::array<double, 3>{112 / 255.0, 114 / 255.0, 120 / 255.0});
    }
    {
        Canvas::ScopedGloss chromeGloss(c, kGlossChrome);
        c.fillRect(kNoseU0, 0.4945, kNoseUW, 0.011, std::array<double, 3>{198 / 255.0, 200 / 255.0, 206 / 255.0});
    }
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

    // ---- T13: baked ambient occlusion ----
    //
    // There is no ambient occlusion anywhere in this renderer, and the wheel
    // arches are where that shows worst: the wheelhouse is a CONCAVE recess
    // lit exactly as brightly as the convex fender wrapped around it, so each
    // arch renders as a glowing blister with a dark slot in it rather than as
    // a hole with a wheel in it. Same story, less severely, along the rocker,
    // which is the shaded underside of the car and reads as fully lit paint.
    //
    // Painted here: after every stripe, decal and wordmark, because occlusion
    // is a property of the SHAPE and applies to whatever happens to be painted
    // on it -- a sponsor decal inside a wheel arch is in shadow too. And
    // before the SW_* swatch column, which must never be touched: those are
    // flat material references sampled by the wheel and spoiler geometry, and
    // darkening them would darken every tire on the car.
    //
    // COORDINATES ARE DERIVED, NOT GUESSED. The arch U spans are the actual
    // footprint of the stations gen_car_rig.py carves an arch into, computed
    // from its own station table and _arch_lip_y(); the V edges are RINGV at
    // the rocker and at K_LIP, i.e. the fender's bottom lip, which is where
    // the recess stops and the visible fender begins. check_car_rig.py reads
    // these five constants back out of this file and compares them against the
    // generator, the same way it already pins the tail island and kWheelRadius
    // -- so a re-authored station table cannot leave the AO painted over the
    // wrong part of the car while everything still builds.
    {
        // Shares kArch* with the shadow rings and lip highlight above -- one
        // definition of where the arch is, not three hand-placed guesses.

        // The body wrap spans u [0.02, 0.78]; everything beyond is swatches.
        constexpr double BODY_U0 = 0.02, BODY_U1 = 0.78;
        constexpr double ROCKER_V = 0.985;  // RINGV[0], the floor/rocker edge

        // Rocker: the whole length, a shallow band. Deliberately weaker than
        // the arches -- the rocker is shaded, not enclosed.
        c.occludeBand(BODY_U0, BODY_U1, ROCKER_V, 0.945, 0.34);
        c.occludeBand(BODY_U0, BODY_U1, 1.0 - ROCKER_V, 0.055, 0.34);

        // Arches: stronger, and reaching all the way up to the lip. Compounds
        // with the rocker band where they overlap, which is correct -- the
        // bottom of a wheelhouse is the most enclosed point on the car.
        for (const auto& arch : {std::pair{kArchFrontU0, kArchFrontU1}, std::pair{kArchRearU0, kArchRearU1}}) {
            c.occludeBand(arch.first, arch.second, ROCKER_V, kArchLipV, 0.55);
            c.occludeBand(arch.first, arch.second, 1.0 - ROCKER_V, 1.0 - kArchLipV, 0.55);
        }
    }

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
    // T12: rubber. This pair is the clearest evidence the uniform-reflectivity
    // shader was wrong -- a black tire measured (42, 72, 109) on a rendered
    // frame, a blue-grey, because it returned 16% of the sky like everything
    // else did. Tires are the darkest thing on a race car and they were
    // reading as painted metal.
    {
        Canvas::ScopedGloss rubber(c, kGlossRubber);
        c.fillRect(0.85, 0.0, 0.10, 0.5, std::array<double, 3>{12 / 255.0, 12 / 255.0, 13 / 255.0});  // tire rubber
        c.fillRect(0.85, 0.5, 0.10, 0.5, std::array<double, 3>{14 / 255.0, 14 / 255.0, 16 / 255.0});  // sidewall (outer annulus, rubber)
    }

    // I1: the two new concentric wheel-cap bands, in the true open UV
    // margin (0.792, 0.85) -- livery.cpp's own nose/tail lamp decals
    // (kTailU0=0.764, kTailUW=0.028) reach u=0.792, past the body wrap's
    // U1=0.78, so u=0.80 was NOT actually free. u=0.83-0.85 stays reserved
    // for I2's mirror swatch. Coordinates must match gen_car_rig.py's
    // SW_TIRE_LETTER/SW_RIM constants.
    {
        // Lettering is moulded rubber, not paint; the rim is the one genuinely
        // metallic thing on the car.
        Canvas::ScopedGloss letterGloss(c, kGlossRubber);
        c.fillRect(0.80, 0.0, 0.03, 0.5, std::array<double, 3>{130 / 255.0, 130 / 255.0, 132 / 255.0});  // tire lettering band
    }
    {
        Canvas::ScopedGloss rimGloss(c, kGlossChrome);
        c.fillRect(0.80, 0.5, 0.03, 0.5, std::array<double, 3>{198 / 255.0, 200 / 255.0, 206 / 255.0});  // metallic rim/hub
    }

    // I2 (car visual fidelity plan): the new door-mirror housing's swatch --
    // dark plastic/trim, a fixed color rather than sampling the door's own
    // livery UV so the mirror can't accidentally inherit whatever number/
    // stripe graphic happens to land at that exact body coordinate on a
    // given car's scheme. Coordinates must match gen_car_rig.py's SW_MIRROR.
    {
        Canvas::ScopedGloss trimGloss(c, kGlossMatte);  // dark plastic trim
        c.fillRect(0.825, 0.0, 0.02, 1.0, std::array<double, 3>{26 / 255.0, 26 / 255.0, 29 / 255.0});  // mirror housing
    }

    // G8 (Gen-4 car overhaul): two more swatches for gen_car_rig.py's new
    // spoiler geometry, in the same reserved-margin column, a separate
    // band from the wheel swatches above (u in [0.95,1.0] vs [0.85,0.95])
    // -- coordinates must match gen_car_rig.py's SW_SPOILER_BODY/DARK.
    // Body swatch reuses this car's own base paint tone so the spoiler
    // reads as body-colored, not a fixed gray.
    c.fillRect(0.95, 0.0, 0.05, 0.5, tone(kBaseM));                                                // spoiler top (body color)
    c.fillRect(0.95, 0.5, 0.05, 0.5, std::array<double, 3>{10 / 255.0, 10 / 255.0, 12 / 255.0});  // spoiler underside/risers

    // ---- T8: the rear panel ----
    //
    // gen_car_rig's tail cap now unwraps into its own rectangle instead of
    // sampling a single texture column, so for the first time the back of the
    // car can be painted as a back rather than as vertical stripes. This is
    // the surface the player stares at for an entire race and it has never had
    // anything drawn on it.
    //
    // Painted AFTER the SW_* swatch column below, not before it. The swatches
    // are full-height bands by design (mip safety), so u > 0.80 has no free
    // rectangle -- the island is carved out of them and must therefore come
    // last. The first version painted before, and the whole panel was buried:
    // the dumped texture showed a black island with one red sliver.
    //
    // Coordinates mirror gen_car_rig.py's TAIL_UV_* exactly; check_car_rig.py
    // asserts the two agree AND that no swatch sample point falls inside.
    {
        constexpr double TU0 = 0.852, TU1 = 0.995;
        constexpr double TV0 = 0.020, TV1 = 0.150;
        const double tw = TU1 - TU0, th = TV1 - TV0;
        auto tRect = [&](double fx, double fy, double fw, double fh,
                         const std::array<double, 3>& col, double a = 1.0) {
            c.fillRect(TU0 + fx * tw, TV0 + fy * th, fw * tw, fh * th, col, a);
        };
        // Island coordinates run 0..1 left-to-right across the car's width and
        // 0 at the top of the panel to 1 at the bottom.
        const std::array<double, 3> panelDark{22 / 255.0, 22 / 255.0, 26 / 255.0};
        const std::array<double, 3> lampRed{150 / 255.0, 18 / 255.0, 15 / 255.0};
        const std::array<double, 3> lampHot{236 / 255.0, 64 / 255.0, 44 / 255.0};
        const std::array<double, 3> chrome{198 / 255.0, 200 / 255.0, 206 / 255.0};

        // Body colour behind everything, so the corners the dome wraps around
        // carry the car's own paint rather than a hard rectangle edge.
        tRect(0.0, 0.0, 1.0, 1.0, tone(kBaseM));
        // Decklid lip across the top.
        tRect(0.0, 0.0, 1.0, 0.10, tone(kShadowM));
        // The taillight band: the single strongest "this is the back of a race
        // car" cue, and the one thing the old single-column UV could not draw.
        tRect(0.04, 0.20, 0.92, 0.24, panelDark);
        for (int i = 0; i < 2; ++i) {
            const double lx = i == 0 ? 0.075 : 0.545;
            tRect(lx, 0.235, 0.38, 0.17, lampRed);
            tRect(lx + 0.02, 0.255, 0.34, 0.055, lampHot, 0.75);
        }
        // Bumper below, tucked and darker, with a thin chrome parting line.
        tRect(0.0, 0.52, 1.0, 0.04, chrome, 0.55);
        tRect(0.0, 0.56, 1.0, 0.44, tone(kShadowM * 0.92));
        tRect(0.30, 0.70, 0.40, 0.16, panelDark, 0.8);
    }


    std::vector<uint8_t> out = downsampleBox(c.take(), kLiveryTextureSize * kSupersample, kSupersample);
    // T4: LHT_DUMP_LIVERY=<dir> writes each car's finished texture as a PPM.
    // The livery is the one asset in this project that is authored blind --
    // it is only ever seen wrapped, at an angle, at chase-cam distance, and
    // several rounds of paint have shipped without anyone looking at the flat
    // image. Desktop-only debug hook, same idiom as the LHT_* flags in
    // main.cpp; no-ops on web, which has no environment to read.
    if (const char* dir = std::getenv("LHT_DUMP_LIVERY")) {
        char path[512];
        std::snprintf(path, sizeof(path), "%s/livery_%02d.ppm", dir, num);
        if (FILE* f = std::fopen(path, "wb")) {
            std::fprintf(f, "P6\n%d %d\n255\n", kLiveryTextureSize, kLiveryTextureSize);
            for (size_t i = 0; i < out.size(); i += 4) std::fwrite(&out[i], 1, 3, f);
            std::fclose(f);
        }
    }
    return out;
}
