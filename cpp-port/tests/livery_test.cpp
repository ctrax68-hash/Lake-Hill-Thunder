// Verifies livery.{h,cpp}'s pure pixel math (bgfx-free): the 3-tone
// shading bands, pairwise-distinguishable stripe styles pulled from real
// ROSTER schemes, and that different car numbers produce visibly
// different pixels in the number-decal region.

#include "../src/render/livery.h"
#include "../src/sim/car.h"

#include <cmath>
#include <cstdio>

namespace {

int g_failures = 0;

void expectTrue(const char* label, bool cond) {
    if (!cond) {
        std::fprintf(stderr, "%s: FAILED\n", label);
        ++g_failures;
    }
}

std::array<double, 3> pixelAt(const std::vector<uint8_t>& pixels, int x, int y) {
    const size_t idx = ((size_t)y * kLiveryTextureSize + (size_t)x) * 4;
    return {pixels[idx] / 255.0, pixels[idx + 1] / 255.0, pixels[idx + 2] / 255.0};
}

double luminance(const std::array<double, 3>& p) {
    return 0.2126 * p[0] + 0.7152 * p[1] + 0.0722 * p[2];
}

// Average luminance over a horizontal band (fy0,fy1 as fractions of the
// texture height), sampled at a handful of x columns clear of any stripe
// graphics (near the very front, u~0.05, always base body color early on
// in the shading pass regardless of stripe style).
double bandLuminance(const std::vector<uint8_t>& pixels, double fy0, double fy1) {
    const int x = (int)(0.06 * kLiveryTextureSize);
    double sum = 0;
    int n = 0;
    for (double fy = fy0; fy < fy1; fy += 0.01) {
        const int y = (int)(fy * kLiveryTextureSize);
        if (y < 0 || y >= kLiveryTextureSize) continue;
        sum += luminance(pixelAt(pixels, x, y));
        ++n;
    }
    return n ? sum / n : 0.0;
}

} // namespace

int main() {
    const Color3 red = CarPalette::Red;

    // 3-tone shading: shadow bands (top/bottom rockers) are darkest, the
    // highlight band (roof/hood, v~0.40-0.60) is brightest, base (mid-body,
    // v~0.20-0.30 clear of the roof band) is in between.
    {
        LiveryScheme scheme{0, 0, 0, CarPalette::White}; // style 0, no stripe reaching this sample column
        const auto pixels = buildLiveryPixels(red, 7, 1, &scheme);
        const double shadowLum = bandLuminance(pixels, 0.0, 0.05);
        const double baseLum = bandLuminance(pixels, 0.15, 0.20);
        const double hiliteLum = bandLuminance(pixels, 0.45, 0.55);
        expectTrue("shadow band is darker than base", shadowLum < baseLum - 0.01);
        expectTrue("highlight band is brighter than base", hiliteLum > baseLum + 0.01);
    }

    // Pairwise-distinguishable stripe styles: same body/accent/scheme
    // fields except `stripe`, all 5 styles should paint genuinely
    // different pixel data somewhere on the texture.
    {
        std::vector<std::vector<uint8_t>> variants;
        for (int style = 0; style < 5; ++style) {
            LiveryScheme scheme{style, 0, 0, CarPalette::Yellow};
            variants.push_back(buildLiveryPixels(red, 28, 0, &scheme));
        }
        bool allDistinct = true;
        for (size_t i = 0; i < variants.size(); ++i) {
            for (size_t j = i + 1; j < variants.size(); ++j) {
                if (variants[i] == variants[j]) allDistinct = false;
            }
        }
        expectTrue("all 5 stripe styles produce distinct textures", allDistinct);
    }

    // Different car numbers produce different pixels in the roof number-
    // decal region (a real regression guard: if this ever degenerated to
    // "same texture regardless of number," every car would look identical).
    {
        LiveryScheme scheme{0, 0, 0, CarPalette::White};
        const auto p7 = buildLiveryPixels(red, 7, 1, &scheme);
        const auto p91 = buildLiveryPixels(red, 91, 1, &scheme);
        expectTrue("car #7 and car #91 render different textures", p7 != p91);
    }

    // Every real ROSTER scheme builds without crashing and produces a
    // fully-opaque, correctly-sized RGBA8 buffer.
    {
        for (const auto& entry : ROSTER) {
            const auto pixels = buildLiveryPixels(entry.col, entry.num, 0, &entry.scheme);
            char label[64];
            std::snprintf(label, sizeof(label), "ROSTER #%d builds a full-size buffer", entry.num);
            expectTrue(label, pixels.size() == (size_t)kLiveryTextureSize * kLiveryTextureSize * 4);
        }
    }

    // J1 (car visual fidelity plan, part 2): a literal resolution guard.
    // The "ROSTER builds a full-size buffer" check above compares
    // pixels.size() against kLiveryTextureSize symbolically on both sides,
    // so it can never catch a reverted bump -- pin the actual number.
    expectTrue("livery texture bumped to 2048 (J1)", kLiveryTextureSize == 2048);

    // Player car (num=21, scheme=nullptr) falls back to idx-based picks
    // without crashing.
    {
        const auto pixels = buildLiveryPixels({1.0, 0.82, 0.24}, 21, 0, nullptr);
        expectTrue("player car (null scheme) builds a full-size buffer",
                   pixels.size() == (size_t)kLiveryTextureSize * kLiveryTextureSize * 4);
    }

    // I1 (car visual fidelity plan): the wheel end-cap swatches -- tread,
    // sidewall (outer rubber annulus), tire-lettering annulus, and the
    // metallic rim disc -- must all decode to distinct colors from each
    // other. A fan sampling one fixed UV point is structurally incapable of
    // showing a rim/tire distinction no matter what that swatch is painted
    // (every vertex on it decodes to the identical texel), so this is a
    // real regression guard: if any two of these four swatch coordinates
    // ever collapsed to the same color, the wheel would silently lose its
    // rim/tire read even though the geometry itself still has 3 bands.
    // Coordinates must track gen_car_rig.py's SW_TREAD/SW_SIDEWALL/
    // SW_TIRE_LETTER/SW_RIM constants exactly (loose cross-file sync, same
    // convention this file's other swatch coordinates already follow).
    {
        LiveryScheme scheme{0, 0, 0, CarPalette::White};
        const auto pixels = buildLiveryPixels(red, 7, 1, &scheme);
        const auto tread = pixelAt(pixels, (int)(0.90 * kLiveryTextureSize), (int)(0.25 * kLiveryTextureSize));
        const auto sidewall = pixelAt(pixels, (int)(0.90 * kLiveryTextureSize), (int)(0.75 * kLiveryTextureSize));
        const auto tireLetter = pixelAt(pixels, (int)(0.815 * kLiveryTextureSize), (int)(0.25 * kLiveryTextureSize));
        const auto rim = pixelAt(pixels, (int)(0.815 * kLiveryTextureSize), (int)(0.75 * kLiveryTextureSize));
        expectTrue("wheel tread differs from sidewall", tread != sidewall);
        expectTrue("wheel sidewall differs from tire-lettering band", sidewall != tireLetter);
        expectTrue("tire-lettering band differs from the metallic rim", tireLetter != rim);
        expectTrue("wheel tread differs from the metallic rim", tread != rim);
        // The rim should read as a bright, roughly-neutral metallic tone --
        // clearly lighter than the near-black tread/sidewall rubber, which
        // is the whole visual point of I1 (a hub distinct from the tire).
        expectTrue("metallic rim is brighter than the tread rubber", luminance(rim) > luminance(tread) + 0.3);
    }

    // I2 (car visual fidelity plan): the mirror housing swatch decodes to
    // its own fixed dark trim color, distinct from the wheel swatches --
    // coordinates must match gen_car_rig.py's SW_MIRROR.
    {
        LiveryScheme scheme{0, 0, 0, CarPalette::White};
        const auto pixels = buildLiveryPixels(red, 7, 1, &scheme);
        const auto mirror = pixelAt(pixels, (int)(0.835 * kLiveryTextureSize), (int)(0.5 * kLiveryTextureSize));
        const auto rim = pixelAt(pixels, (int)(0.815 * kLiveryTextureSize), (int)(0.75 * kLiveryTextureSize));
        expectTrue("mirror housing differs from the metallic rim", mirror != rim);
    }

    // I5 (car visual fidelity plan): the livery-density patch pass. The
    // blocks are painted AFTER all five stripe styles precisely so density
    // rises regardless of scheme, which is exactly the property a decode
    // check can pin down and a screenshot cannot: sample one patch centre
    // per target region (hood, lower quarter panel and its mirrored twin,
    // rear-window surround) for every style and require the car's own acc2
    // tone there. With idx=0 the tone rotation is kPatchTones[i % 4], so
    // patches 1, 5 and 9 are all acc2 -- pinned to a body/acc2 pair (red
    // body, yellow acc2) no stripe style paints in these regions, so a
    // pass means the patch really landed rather than coincidentally
    // matching whatever was underneath.
    {
        struct Sample {
            const char* what;
            double u, v;
        };
        // Centres of patches 1 (hood), 5 (lower quarter, both flanks) and
        // 9 (rear-window surround), computed from livery.cpp's own kPatches
        // table -- the same loose cross-file coordinate sync this file's
        // swatch checks already use.
        const Sample kPatchSamples[] = {
            {"hood patch", 0.095, 0.553},
            {"lower quarter patch (-z flank)", 0.315, 0.107},
            {"lower quarter patch (+z flank)", 0.315, 0.893},
            {"rear-window surround patch", 0.580, 0.276},
        };
        for (int style = 0; style < 5; ++style) {
            LiveryScheme scheme{style, 0, 0, CarPalette::Yellow};
            const auto pixels = buildLiveryPixels(red, 7, 0, &scheme);
            for (const auto& s : kPatchSamples) {
                const auto px = pixelAt(pixels, (int)(s.u * kLiveryTextureSize), (int)(s.v * kLiveryTextureSize));
                char label[96];
                std::snprintf(label, sizeof(label), "%s is painted in acc2 (stripe style %d)", s.what, style);
                expectTrue(label, std::fabs(px[0] - CarPalette::Yellow[0]) < 0.01 &&
                                       std::fabs(px[1] - CarPalette::Yellow[1]) < 0.01 &&
                                       std::fabs(px[2] - CarPalette::Yellow[2]) < 0.01);
            }
            // ...and they are blocks, not another full-width band: the gap
            // between the two forward hood patches keeps whatever the
            // shading/stripe pass left there.
            const auto gap = pixelAt(pixels, (int)(0.137 * kLiveryTextureSize), (int)(0.553 * kLiveryTextureSize));
            char label[64];
            std::snprintf(label, sizeof(label), "hood patches leave gaps (stripe style %d)", style);
            expectTrue(label, std::fabs(gap[1] - CarPalette::Yellow[1]) > 0.05);
        }
    }

    // I5: grille slats. The centre slat must decode to SW_RIM's *exact*
    // color, because that is the entire mechanism by which it picks up
    // fs_car.sc's I3 chrome specular lobe (a color-match against that same
    // constant) -- a nearby-but-not-equal color would silently lose the
    // glint with nothing else visibly wrong, which is precisely the class
    // of regression a decode check exists to catch. The gaps between slats
    // must still read as the dark grille opening, or the slats have merged
    // into one bright block.
    {
        LiveryScheme scheme{0, 0, 0, CarPalette::White};
        const auto pixels = buildLiveryPixels(red, 7, 1, &scheme);
        const int noseX = (int)(0.023 * kLiveryTextureSize);
        const auto chrome = pixelAt(pixels, noseX, (int)(0.500 * kLiveryTextureSize));
        const auto between = pixelAt(pixels, noseX, (int)(0.480 * kLiveryTextureSize));
        expectTrue("grille centre slat is exactly SW_RIM's chrome color",
                   chrome[0] == 198 / 255.0 && chrome[1] == 200 / 255.0 && chrome[2] == 206 / 255.0);
        expectTrue("grille stays dark between slats", luminance(between) < 0.06);
    }

    // I5: bolder number outlines. drawNumber()'s outline-to-fill ratio went
    // 1.18/1.14 -> 1.28/1.22, so the dark ring drawn under every digit
    // grows. Measured as the dark-pixel count over the door-number
    // ellipse's own bounding box (u 0.343-0.487, v 0.153-0.317, centred on
    // carU(-0.10)): 12413 pixels at the old ratio, 13976 at the new one, so
    // the 13000 floor below sits between the two measurements rather than
    // being a round number picked blind -- it fails if the ratio is ever
    // reverted or quietly reduced.
    //
    // Note what this region actually measures. On a dark-bodied car the
    // door number is dark-on-white (panelNum == the outline tone), so the
    // "outline" reads as a bolder glyph rather than a contrasting ring; the
    // ring only reads as a ring on the deck-lid number, which is drawn
    // white-on-body-paint. Both are driven by this one ratio, so counting
    // it here -- where the dark/white contrast is unambiguous and no stripe
    // style paints anything dark -- is the cleanest place to pin it.
    {
        LiveryScheme scheme{0, 0, 0, CarPalette::White};
        const auto pixels = buildLiveryPixels(red, 28, 1, &scheme);
        int darkPixels = 0;
        for (int y = (int)(0.153 * kLiveryTextureSize); y < (int)(0.317 * kLiveryTextureSize); ++y)
            for (int x = (int)(0.343 * kLiveryTextureSize); x < (int)(0.487 * kLiveryTextureSize); ++x)
                if (luminance(pixelAt(pixels, x, y)) < 0.10) ++darkPixels;
        expectTrue("door number carries a bold dark outline", darkPixels >= 13000);
    }

    // J6 (car visual fidelity plan, part 2): contingency chips now carry a
    // white/light backing behind each colored chip (livery.cpp's own
    // kChipU0/chipW/kChipBorder -- loose cross-file sync, matching this
    // file's other swatch-coordinate checks). Coordinates and expected
    // colors below were measured against a scratch build of the real
    // function, not guessed -- (240,240,240) decoded exactly at the
    // backing sample, (219,41,36) exactly at the chip-0 (red) interior.
    {
        LiveryScheme scheme{0, 0, 0, CarPalette::White};
        const auto pixels = buildLiveryPixels(red, 7, 0, &scheme);
        constexpr double kChipU0 = 0.688, kChipU1 = 0.750;
        constexpr int kChips = 6;
        const double chipW = (kChipU1 - kChipU0) / kChips;
        const double kChipBorder = 2.0 / kLiveryTextureSize;
        const double cx = kChipU0, cw = chipW * 0.78, ch = 0.034, vy = 0.118;
        const auto backing = pixelAt(pixels, (int)((cx - kChipBorder * 0.5) * kLiveryTextureSize),
                                      (int)((vy + ch * 0.5) * kLiveryTextureSize));
        const auto chipColor =
            pixelAt(pixels, (int)((cx + cw * 0.5) * kLiveryTextureSize), (int)((vy + ch * 0.5) * kLiveryTextureSize));
        expectTrue("chip backing decodes to the light backing tone",
                   std::fabs(backing[0] - 240 / 255.0) < 0.01 && std::fabs(backing[1] - 240 / 255.0) < 0.01 &&
                       std::fabs(backing[2] - 240 / 255.0) < 0.01);
        expectTrue("chip backing differs from the chip's own color", backing != chipColor);
    }

    // J6: manufacturer badge upgrade (fill + outline + bar, replacing a
    // single flat ellipse). All three sample points and expected colors
    // measured against a scratch build first -- notably, the badge's own
    // dead centre lands on the BAR (drawn last, spanning the ellipse's
    // middle by design, same as JS's own drawBadge()), not the plain fill,
    // so the fill sample below is deliberately offset above the bar band
    // rather than at (badgeCx,badgeCy).
    {
        LiveryScheme scheme{0, 0, 0, CarPalette::White};
        const auto pixels = buildLiveryPixels(red, 7, 1, &scheme);
        constexpr double kTailU0 = 0.764, kTailUW = 0.028;
        const double badgeCx = kTailU0 + kTailUW * 0.5, badgeCy = 0.50;
        const double badgeRx = 0.008, badgeRy = 0.018;
        const auto fillPx = pixelAt(pixels, (int)(badgeCx * kLiveryTextureSize),
                                     (int)((badgeCy - badgeRy * 0.9) * kLiveryTextureSize));
        const auto outlinePx = pixelAt(pixels, (int)(badgeCx * kLiveryTextureSize),
                                        (int)((badgeCy - badgeRy * 1.1) * kLiveryTextureSize));
        const auto barPx = pixelAt(pixels, (int)(badgeCx * kLiveryTextureSize), (int)(badgeCy * kLiveryTextureSize));
        expectTrue("badge fill decodes to the accent tone (white, this car)",
                   std::fabs(fillPx[0] - 242 / 255.0) < 0.02 && std::fabs(fillPx[1] - 242 / 255.0) < 0.02);
        expectTrue("badge outline decodes to the dark outline tone",
                   std::fabs(outlinePx[0] - 10 / 255.0) < 0.02 && std::fabs(outlinePx[1] - 10 / 255.0) < 0.02);
        expectTrue("badge bar differs from both the fill and the outline", barPx != fillPx && barPx != outlinePx);
    }

    // J6: roll-cage glimpse bars inside the windshield/rear-glass rects.
    // Verified safe against fs_car.sc's I3/H6 color-match thresholds by
    // direct calculation (see livery.cpp's own comment at the paint site);
    // this check instead confirms the bars are actually painted where
    // expected, and that they don't spread past their own thin band --
    // a nearby sample in the same glass rect must still decode to plain
    // glassDark/glassHi, not the bar tone.
    {
        LiveryScheme scheme{0, 0, 0, CarPalette::White};
        const auto pixels = buildLiveryPixels(red, 7, 1, &scheme);
        // carU() (livery.cpp's own nose-to-tail UV formula) is private to
        // that file -- inlined here, matching the loose cross-file sync
        // convention this test already uses for every other swatch/region
        // coordinate.
        auto carU = [](double x) { return 0.02 + (2.51 - x) / 5.02 * 0.76; };
        const double uWS0 = carU(0.68), uWS1 = carU(0.02);
        constexpr double GV0 = 0.335, GVH = 0.330;
        const double wsClearU1 = uWS1 - 0.036;
        const double bar1u = uWS0 + (wsClearU1 - uWS0) * 0.35;
        const auto barPx =
            pixelAt(pixels, (int)(bar1u * kLiveryTextureSize), (int)((GV0 + GVH * 0.5) * kLiveryTextureSize));
        const auto glassHiPx = pixelAt(pixels, (int)((uWS0 + (wsClearU1 - uWS0) * 0.15) * kLiveryTextureSize),
                                        (int)(0.50 * kLiveryTextureSize));
        expectTrue("windshield cage bar reads as a distinct steel tone, not plain glass",
                   luminance(barPx) > luminance(glassHiPx) + 0.02);
        expectTrue("glass away from the bar is still plain glassHi",
                   std::fabs(glassHiPx[0] - 26 / 255.0) < 0.02 && std::fabs(glassHiPx[1] - 33 / 255.0) < 0.02);
    }

    if (g_failures == 0) {
        std::printf("livery_test: shading bands, stripe styles, and number decals all match expectations.\n");
        return 0;
    }
    std::fprintf(stderr, "livery_test: %d MISMATCHES.\n", g_failures);
    return 1;
}
