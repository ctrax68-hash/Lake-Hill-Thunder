// Verifies livery.{h,cpp}'s pure pixel math (bgfx-free): the 3-tone
// shading bands, pairwise-distinguishable stripe styles pulled from real
// ROSTER schemes, and that different car numbers produce visibly
// different pixels in the number-decal region.

#include "../src/render/livery.h"
#include "../src/sim/car.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <set>
#include <utility>

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
        // T23c: the 0.815 column is four bands now, sampled at v 0.26/0.42/
        // 0.58/0.74 -- see gen_car_rig.py's SW_* block for why they are bunched
        // into the middle of the column rather than spread evenly.
        const auto tireLetter = pixelAt(pixels, (int)(0.815 * kLiveryTextureSize), (int)(0.26 * kLiveryTextureSize));
        const auto bead = pixelAt(pixels, (int)(0.815 * kLiveryTextureSize), (int)(0.42 * kLiveryTextureSize));
        const auto rim = pixelAt(pixels, (int)(0.815 * kLiveryTextureSize), (int)(0.58 * kLiveryTextureSize));
        const auto lug = pixelAt(pixels, (int)(0.815 * kLiveryTextureSize), (int)(0.74 * kLiveryTextureSize));
        expectTrue("wheel tread differs from sidewall", tread != sidewall);
        expectTrue("wheel sidewall differs from tire-lettering band", sidewall != tireLetter);
        expectTrue("tire-lettering band differs from the metallic rim", tireLetter != rim);
        expectTrue("wheel tread differs from the metallic rim", tread != rim);
        // The rim should read as a bright, roughly-neutral metallic tone --
        // clearly lighter than the near-black tread/sidewall rubber, which
        // is the whole visual point of I1 (a hub distinct from the tire).
        // T22 turned this assertion around, and the reference is why. I1 wanted
        // the rim 0.3 brighter than the tread, which was calibrated to a chrome
        // hub. Measured off NASCAR Thunder's own car-select screens, the wheel
        // face reads 0.136 against 0.105 for the tire beside it -- a ratio of
        // 1.30, barely brighter at all, and no highlight on it. A big
        // luminance gap is exactly what the reference does NOT have.
        //
        // So the two properties worth holding are that the hub is still
        // DISTINGUISHABLE from the rubber (or the wheel loses its read
        // entirely, which is what I1 was really protecting against) and that
        // it is no longer chrome.
        expectTrue("steel rim stays distinguishable from the tread rubber",
                   luminance(rim) > luminance(tread) + 0.04);
        // T25: bound moved 0.25 -> 0.40. T22's 0.25 was an ALBEDO bound set from
        // an albedo ratio, and the render inverted it -- the face is dished and
        // in the arch's shade, so at 0.14 albedo it rendered 0.031 against the
        // sunlit tread's 0.035: darker than the rubber. At 0.29 albedo it
        // renders 0.060, a 1.7x ratio inside the reference's 1.3-2x. The
        // property this clause protects is "not chrome", and chrome was 0.78.
        expectTrue("wheel rim is steel, not the old chrome", luminance(rim) < 0.40);
        // T23c: the two BRIGHT RINGS, which are where the reference wheel's
        // read actually comes from. The face being dark is right and was never
        // the whole story -- with nothing on it to catch light the wheel is a
        // dark disc with dark slots. Both rings must clear the face they sit
        // on by a real margin, or the geometry added for them buys nothing.
        expectTrue("bead ring is materially brighter than the rim face it flanges onto",
                   luminance(bead) > luminance(rim) + 0.10);
        expectTrue("lug ring is materially brighter than the rim face it sits on",
                   luminance(lug) > luminance(rim) + 0.10);
        // The bead ring is the warm one and the lug ring the neutral one; if
        // they ever collapse to the same tone the wheel loses the distinction
        // the reference makes between a bronze flange and steel hardware.
        // T25: restated. The bronze/steel distinction encoded the #49's night
        // lighting; in daylight both rings are steel and the bead ring is the
        // brightest thing on the wheel, so that is the property held.
        expectTrue("bead ring is the brightest element on the wheel",
                   luminance(bead) > luminance(lug) && luminance(bead) > luminance(rim));
        // And the lettering band stays a moulded-rubber tone, not a whitewall:
        // T22 recorded that painting this solid annulus bright puts a hoop on
        // the tire, and that finding still holds at T23c's warmer value.
        expectTrue("tire lettering band stays below a whitewall", luminance(tireLetter) < 0.25);
        expectTrue("tire lettering band still reads above the rubber",
                   luminance(tireLetter) > luminance(sidewall) + 0.05);
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
            // T32: patch 5 moved off the lower door and onto the lower rear
            // quarter, because the door number now covers where it used to be.
            // Centre of patch 5, {0.652, 0.076, 0.030, 0.062}.
            {"lower quarter patch (-z flank)", 0.667, 0.107},
            {"lower quarter patch (+z flank)", 0.667, 0.893},
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

    // I5, rewritten for T16. The grille used to be three thin V bands at
    // u~0.023, because every nose-cap vertex shared one texture column and a
    // vertical band was the only mark that UV could express. It now lives in
    // the nose island as real horizontal slats, so this checks the slats where
    // they actually are.
    //
    // I5's original clause asserted the centre slat decoded to SW_RIM's EXACT
    // RGB, because that equality was the entire mechanism by which it picked
    // up fs_car.sc's chrome specular lobe -- a colour-distance match against
    // that same constant. T12 deleted those colour branches in favour of the
    // gloss mask, so the equality no longer means anything and asserting it
    // would be pinning a coincidence. What matters now is stated directly:
    // the chrome surround carries chrome GLOSS, and the opening stays dark
    // between its slats rather than merging into one bright block.
    {
        LiveryScheme scheme{0, 0, 0, CarPalette::White};
        const auto pixels = buildLiveryPixels(red, 7, 1, &scheme);
        // Nose island: u 0.852-0.995, v 0.850-0.980 (gen_car_rig.py's
        // NOSE_UV_*, which check_car_rig.py pins against this file's copy).
        auto islandPx = [&](double fx, double fy) {
            const double u = 0.852 + fx * (0.995 - 0.852);
            const double v = 0.850 + fy * (0.980 - 0.850);
            return pixelAt(pixels, (int)(u * kLiveryTextureSize), (int)(v * kLiveryTextureSize));
        };
        auto islandAlpha = [&](double fx, double fy) {
            const double u = 0.852 + fx * (0.995 - 0.852);
            const double v = 0.850 + fy * (0.980 - 0.850);
            const int x = (int)(u * kLiveryTextureSize), y = (int)(v * kLiveryTextureSize);
            return (int)pixels[((size_t)y * kLiveryTextureSize + (size_t)x) * 4 + 3];
        };
        // A slat, and the opening between two of them.
        const auto slatPx = islandPx(0.50, 0.346);
        const auto betweenPx = islandPx(0.50, 0.315);
        const int chromeGloss = islandAlpha(0.50, 0.288);

        expectTrue("grille slats are brighter than the opening behind them",
                   luminance(slatPx) > luminance(betweenPx) + 0.10);
        expectTrue("grille stays dark between slats", luminance(betweenPx) < 0.10);
        expectTrue("the grille surround carries chrome gloss",
                   chromeGloss == (int)(kGlossChrome * 255.0 + 0.5));
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
        // T32: the box is derived from livery.h's anchors like the mirror
        // guard's, and for the same reason -- it was written for a number that
        // has since moved 0.32 m forward and grown. Centred on the LOW door's
        // glyph box (the mirror of kDoorNumberTopV + fh/2) and on
        // kDoorCenterU, padded generously in both axes: this clause counts
        // dark texels and only needs to contain the graphic, not fit it.
        // The floor is unchanged: a bigger number with the same outline ratio
        // can only put MORE dark texels in its box, so 13000 remains a lower
        // bound the old size met.
        constexpr double kLoC = 1.0 - (kDoorNumberTopV + kDoorNumberFh * 0.5);
        for (int y = (int)((kLoC - kDoorNumberFh * 0.72) * kLiveryTextureSize);
             y < (int)((kLoC + kDoorNumberFh * 0.72) * kLiveryTextureSize); ++y)
            for (int x = (int)((kDoorCenterU - 0.085) * kLiveryTextureSize);
                 x < (int)((kDoorCenterU + 0.085) * kLiveryTextureSize); ++x)
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
        // R1: 0.68/0.02 -> 0.80/0.28. These mirror livery.cpp's own glass
        // anchors, which are carU() of the real cowl and A-pillar STATIONS --
        // and R1 moved both when the greenhouse became a Gen-4 notchback.
        // This replica is hand-synced (the same loose cross-file convention
        // the swatch coordinates above use), so it has to move with them;
        // check_car_rig.py is what actually pins livery.cpp to the geometry.
        // T23: 0.585/0.03 -> 0.80/0.205, tracking the greenhouse forward again.
        // T27: 0.80/0.205 -> 0.95/0.365, the greenhouse re-authored to the #41.
        const double uWS0 = carU(0.95), uWS1 = carU(0.365);
        constexpr double GV0 = 0.335, GVH = 0.330;
        const double wsClearU1 = uWS1 - 0.036;
        const double bar1u = uWS0 + (wsClearU1 - uWS0) * 0.35;
        const auto barPx =
            pixelAt(pixels, (int)(bar1u * kLiveryTextureSize), (int)((GV0 + GVH * 0.5) * kLiveryTextureSize));
        const auto glassHiPx = pixelAt(pixels, (int)((uWS0 + (wsClearU1 - uWS0) * 0.15) * kLiveryTextureSize),
                                        (int)(0.50 * kLiveryTextureSize));
        // T23 flips this BACK, because T22's measurement was taken on one dark
        // red car and did not generalise. Across the second reference batch the
        // window opening holds its own absolute luminance -- 0.087 on the white
        // #49 against a 0.747 door, 0.238 on the red #21 against a 0.259 door --
        // so it is a dark hole, and the cage and net inside it are a PALE
        // lattice catching light. Bar brighter than glass.
        expectTrue("windshield cage bar reads as pale structure inside a dark opening",
                   luminance(barPx) > luminance(glassHiPx) + 0.02);
        // Pins the glass tone itself, so a future edit cannot quietly lighten
        // the pane back toward a mirror and still satisfy the contrast clause
        // above by lightening the cage with it.
        expectTrue("glass is the dark-opening tone, not a lit aperture",
                   std::fabs(glassHiPx[0] - 50 / 255.0) < 0.03 && std::fabs(glassHiPx[1] - 53 / 255.0) < 0.03);
    }

    // K2 (car visual fidelity plan, part 3): the rear glass rect used to
    // overshoot its real 3D opening by ~47% of its width (uRG1 was pinned
    // to the "deck start" station instead of the "rear axle, belt/roof
    // rejoin" station two stations earlier), painting glassDark/glassHi
    // onto what should be plain trunk-decklid paint. u=0.635 sits inside
    // the OLD rear-glass rect (u<0.665) but outside the corrected one
    // (u>0.6085) -- measured directly against a scratch build: decodes to
    // (192,0,0), the plain red body tone, clearly distinct from both
    // glassDark(16,20,30) and glassHi(26,33,46), confirming the previously
    // glass-painted decklid area now reads as body paint.
    {
        LiveryScheme scheme{0, 0, 0, CarPalette::White};
        const auto pixels = buildLiveryPixels(red, 7, 1, &scheme);
        constexpr double GV0 = 0.335, GVH = 0.330;
        const auto px = pixelAt(pixels, (int)(0.635 * kLiveryTextureSize), (int)((GV0 + GVH * 0.5) * kLiveryTextureSize));
        const std::array<double, 3> glassDark{16 / 255.0, 20 / 255.0, 30 / 255.0};
        const std::array<double, 3> glassHi{26 / 255.0, 33 / 255.0, 46 / 255.0};
        expectTrue("corrected rear-glass boundary: former overshoot area is no longer glassDark",
                   px != glassDark);
        expectTrue("corrected rear-glass boundary: former overshoot area is no longer glassHi", px != glassHi);
    }

    // T4: the wordmark decals must actually put ink on the car, and must stay
    // legible at the 1024 texture non-player cars get.
    //
    // Two things this pins that nothing else could. First, drawText() fails
    // SILENTLY if the font atlas does not decode -- it returns early and the
    // livery is simply blank where a sponsor should be, which is invisible in
    // any 3D render until you go looking for it. Second, the decals are sized
    // in fractions of the texture, so a future resolution change or a "make
    // them a bit smaller" tweak can quietly cross the point where a glyph
    // stops resolving in a 20-car pack.
    {
        LiveryScheme scheme{0, 0, 0, CarPalette::White};
        const auto pixels = buildLiveryPixels(red, 7, 1, &scheme);

        // The quarter-panel badge is a dark plate. Assert RELATIVE to the body
        // it sits on, not against an absolute threshold: the plate is laid at
        // 0.90 alpha so it blends with whatever is underneath, and an absolute
        // bound would be a different number for every car colour. (The first
        // version of this used < 0.12 and failed for exactly that reason --
        // 0.9*0.07 + 0.1*body lands above it on a red car.)
        // Assert the GLYPHS, not the plate. The first version of this counted
        // dark pixels in the badge band and PASSED with drawText() stubbed out
        // to return immediately -- it was measuring the fillRect() plate the
        // letters sit on, which is exactly the worthless-guard failure this
        // file's own history warns about. What proves the text rendered is
        // LIGHT ink inside a DARK plate: count pixels far brighter than the
        // plate but confined to the plate's own band.
        const int vRow = (int)(0.170 * kLiveryTextureSize);
        int plateHits = 0, glyphHits = 0;
        for (int u = (int)(0.60 * kLiveryTextureSize); u < (int)(0.77 * kLiveryTextureSize); ++u) {
            for (int dv = -12; dv <= 12; ++dv) {
                const auto px = pixelAt(pixels, u, vRow + dv);
                const double lum = px[0] * 0.5 + px[1] * 0.4 + px[2] * 0.1;
                if (lum < 0.20) ++plateHits;        // the dark sponsor plate
                else if (lum > 0.70) ++glyphHits;   // light lettering on it
            }
        }
        expectTrue("T4: quarter-panel sponsor plate is painted", plateHits > 200);
        expectTrue("T4: sponsor wordmark glyphs are actually blitted onto the plate",
                   glyphHits > 60);

        // The smallest decal shipped is the contingency row at 0.011 of the
        // texture. At the 1024 a non-player car gets that is 11 texels of cap
        // height; below ~8 a glyph stops resolving in the pack. This is the
        // number to move if the decals are ever resized, and it is deliberately
        // expressed against the SMALL texture, not the 2048 the player gets.
        constexpr double kSmallestDecalH = 0.011;
        constexpr int kNonPlayerTexture = 1024;
        expectTrue("T4: smallest decal stays legible at the non-player 1024 livery",
                   kSmallestDecalH * kNonPlayerTexture >= 8.0);
    }

    // T10: the two door numbers must be 180-DEGREE ROTATIONS of each other.
    //
    // T32 changed what this clause asserts, because the thing it asserted was
    // wrong. It said "mirror images", which is what the paint did: the +z door
    // is drawn with mirrorU and the -z door plain. Both flanks share one U, so
    // that much was right -- and both flanks do NOT share one V. The ring's V
    // is mirrored about 0.5 between the sides, so +v runs down the +z door and
    // up the -z one, and the -z number rendered upside down on the car for as
    // long as there has been a number on it (verified at azimuth 237, the
    // flank the original single-sided probe never looked at).
    //
    // With the -z door drawn flipped in V and the +z one mirrored in U, one is
    // now the other turned through 180 degrees, and that is what is measured
    // below. The identity control is unchanged and still what proves the
    // transform is doing work.
    //
    // WHY THIS GUARD EXISTS. `car_u(x)` is the same function of x all the way
    // around the section, so u advances toward the TAIL on both flanks -- but
    // the two flanks are seen from opposite sides, so exactly one of them
    // shows u advancing right-to-left on screen. Verified three ways rather
    // than argued: v > 0.5 <=> z > 0 straight out of the generator's vertex
    // data; the +z flank rendered at showcase azimuth 90 matches its texture
    // FLIPPED; the -z flank at 270 matches its texture AS PAINTED. Both read
    // forwards only because livery.cpp mirrors the high-v half.
    //
    // Nothing checked this, and that is precisely how a build shipped with
    // every left-side number reversed. The wordmarks got the mirror treatment
    // when drawText() landed; drawNumber() did not, and no test could tell.
    //
    // Asserting the mirror alone is NOT enough: a horizontally symmetric glyph
    // (an 8, a 0) mirrors onto itself, so a guard that only checked "mirrored"
    // would pass on an un-mirrored number too. The second clause is the real
    // one -- the halves must agree markedly BETTER mirrored than superimposed.
    // Car 91 is used because its digits are the least symmetric on the roster,
    // which is what makes that gap wide.
    //
    // Measured over the dumped textures (LHT_DUMP_LIVERY), mirrored vs
    // identity agreement: #91 0.978/0.672, #7 0.985/0.791, #44 0.985/0.857,
    // #28 0.987/0.912. The thresholds below sit clear of both clusters.
    {
        LiveryScheme scheme{0, 0, 0, CarPalette::White};
        const auto pixels = buildLiveryPixels(red, 91, 1, &scheme);
        // T26: box re-centred on the door number's new centre, 0.7958 (top
        // edge 6 texels under the belt, fh 0.19 with the 1.22x outline), and
        // widened to hold the 0.056-wide digits; it was the old mid-door box.
        // Same box the "bold dark outline" check above uses, mirrored about
        // its own centre column -- which is drawNumber()'s fcx, carU(-0.10),
        // and therefore the axis mirrorRegionX() flips about.
        // T30: the box is the GLYPH RUN, not the panel. At 0.330-0.500 it was
        // 348 texels wide around a 177-texel run, so more than half of what it
        // compared was body paint -- and the scheme's stripes are not mirror-
        // symmetric about carU(-0.10), so they dominated the disagreement. The
        // 7-segment digits were wide enough to swamp that; the font's are not.
        // T32: THE BOX IS DERIVED, NOT WRITTEN DOWN. It was centred on 0.4152
        // -- carU(-0.10), a literal chosen when the axles were somewhere else
        // -- and stayed there while the door itself moved 0.32 m forward. Now
        // it reads livery.h's kDoorCenterU, which is the midpoint of the two
        // wheel openings and the axis drawText actually mirrors about, so the
        // guard cannot drift away from the paint again.
        //
        // Half-width from the cap height: a two-digit run measures about 0.5
        // of the cap height per digit once kFlankXScale has squeezed it, so
        // 0.25*fh each side covers the run with slack and still stops short of
        // the contingency stack (u 0.2365-0.2935) on one side and the scheme's
        // panel edges on the other.
        constexpr double kNumHalfU = kDoorNumberFh * 0.25;
        const int u0 = (int)((kDoorCenterU - kNumHalfU) * kLiveryTextureSize);
        const int u1 = (int)((kDoorCenterU + kNumHalfU) * kLiveryTextureSize);
        const int w = u1 - u0;
        // Half-height is the outlined digit box (0.19 x 1.22 / 2), not more:
        // the box's top edge sits 6 texels under the belt, and a taller box
        // reaches into the side glass, where the +z door carries the net and
        // the -z door does not -- an asymmetry that is not the number's.
        // T30: the box is centred on the GLYPH BOX (top 0.680, height 0.190,
        // so centre 0.775 and its mirror 0.225), not on the old 0.7958. Those
        // two differed by 43 texels in OPPOSITE directions on the two doors,
        // so the comparison was reading the halves 86 texels out of register
        // and reported the mirror failing on a correctly mirrored number.
        // T32: also derived. The glyph box's centre is its published top edge
        // plus half the published cap height; the low door's is its mirror.
        // Half-height stays just inside the cap height (0.49 of it, not 0.5)
        // so the box's top edge cannot cross the beltline into the glass,
        // where the +z door carries the window net and the -z door does not.
        // Its bottom edge lands at 0.903, clear of the rocker fastener row at
        // 0.9415 -- the other asymmetry on this flank.
        constexpr double kGlyphC = kDoorNumberTopV + kDoorNumberFh * 0.5;
        const int halfV = (int)(kDoorNumberFh * 0.49 * kLiveryTextureSize);
        const int loC = (int)((1.0 - kGlyphC) * kLiveryTextureSize);
        const int hiC = (int)(kGlyphC * kLiveryTextureSize);

        // T30: agreement is allowed one texel of slack in u. The two doors are
        // separate rasterisations of the same glyph run -- one mirrored -- and
        // the blit rounds each destination box with floor/ceil, so a correctly
        // mirrored number still differs by up to a texel along every glyph
        // edge. That cost about 6 points of agreement with the font's thin
        // outline, where the old 7-segment blocks had enough interior area to
        // swamp it. The slack does NOT rescue an unmirrored number: the
        // identity figure below is measured the same way and stays far lower.
        // The OUTLINE, which is the only near-black ink in this window: the
        // contingency stack (u 0.254-0.311) and the rocker fasteners (v 0.9415)
        // are both outside it, and the glass is above the belt. Measured on the
        // dumped texture, a row through the glyphs finds exactly eight dark
        // runs on each door and nothing else.
        //
        // The FILL was tried and is worse, for a reason worth keeping: the
        // scheme paints white panels on the door, so a brightness test picks up
        // body paint that is not mirror-symmetric about carU(-0.10) and buries
        // the glyphs in it.
        auto ink_at = [&](int x, int y) { return luminance(pixelAt(pixels, x, y)) < 0.10; };
        // T30: COMPARE COVERAGE IN 4x4 BLOCKS, not texel against texel.
        //
        // The two doors are independent rasterisations of the same run, one
        // mirrored, and the blit rounds every destination box with floor/ceil,
        // so corresponding edges land up to 3 texels apart -- measured: the
        // left door's outline runs sit at u 751/790/796/827..., the right
        // door's mirror about carU(-0.10) at 754/785/791/829.... The shapes ARE
        // mirrors. But the outline is a five-texel ring, so a three-texel slip
        // moves most of it off itself and a texel-exact comparison reports a
        // correct mirror as broken. It did: 0.913 where the 7-segment blocks,
        // being solid and wide, used to give 0.98.
        //
        // Coverage over a 4x4 block is the same shape measured at a resolution
        // where that slip does not matter, and it is no weaker a test -- an
        // unmirrored number moves its ink by a whole glyph, which no block
        // average hides. The identity figure printed alongside is what proves
        // that, and it is measured exactly the same way.
        constexpr int kBlk = 4;
        auto cov = [&](int x0, int y0, bool flipX, bool flipY) {
            int n = 0;
            for (int by = 0; by < kBlk; ++by)
                for (int bx = 0; bx < kBlk; ++bx)
                    n += ink_at(flipX ? x0 - bx : x0 + bx, flipY ? y0 - by : y0 + by) ? 1 : 0;
            return (double)n / (kBlk * kBlk);
        };
        long ink = 0, same = 0, mirrored = 0, total = 0;
        for (int dv = -halfV; dv + kBlk <= halfV; dv += kBlk) {
            for (int dx = 0; dx + kBlk <= w; dx += kBlk) {
                const double lo = cov(u0 + dx, loC + dv, false, false);
                const double hi = cov(u0 + dx, hiC + dv, false, false);
                const double hiRot = cov(u1 - 1 - dx, hiC - dv, true, true);
                if (lo > 0.5) ++ink;
                if (std::fabs(lo - hi) <= 0.25) ++same;
                if (std::fabs(lo - hiRot) <= 0.25) ++mirrored;
                ++total;
            }
        }
        ink *= kBlk * kBlk;
        const double fMirror = total ? (double)mirrored / (double)total : 0.0;
        const double fSame = total ? (double)same / (double)total : 0.0;
        std::printf("livery_test: T10 door-number halves -- rotated %.3f, identity %.3f (ink %ld)\n",
                    fMirror, fSame, ink);

        expectTrue("T10: door numbers are actually painted (ink present)", ink > 5000);
        // T30: 0.95 -> 0.93, and the evidence that this is registration and not
        // a broken mirror is a direct one. On the dumped texture, a row through
        // the glyphs finds the left door's outline runs at u 751/790/796/827/
        // 871/909/915/946; reflecting the right door's about carU(-0.10) gives
        // 754/785/791/829/873/904/910/949. Every one within four texels -- the
        // shapes are mirrors, and what is left is where a five-texel ring lands
        // on the grid. 0.938 is what a correct font-drawn mirror measures here;
        // the 7-segment blocks it was calibrated on were solid and wide enough
        // to reach 0.98.
        expectTrue("T10: the two door numbers are 180-degree rotations of each other", fMirror >= 0.93);
        expectTrue("T10: door numbers agree far better rotated than superimposed "
                   "(catches an untransformed number whose glyphs happen to be symmetric)",
                   fMirror - fSame >= 0.10);
    }

    // T12: the gloss mask in the alpha channel.
    //
    // Before this, alpha was a hardcoded 255 on every texel and fs_car.sc
    // applied one reflectivity to the whole car -- which measured, on a
    // rendered frame, as a BLACK TIRE coming out (42, 72, 109). The mask is
    // what tells rubber from clearcoat from glass, so it needs a guard that
    // fails the moment it goes back to being uniform.
    //
    // Asserted against livery.h's own kGloss* constants, not against copies:
    // a test carrying its own numbers cannot detect them drifting.
    {
        LiveryScheme scheme{0, 0, 0, CarPalette::White};
        const auto pixels = buildLiveryPixels(red, 28, 1, &scheme);
        auto alphaAt = [&](double u, double v) {
            const int x = (int)(u * kLiveryTextureSize), y = (int)(v * kLiveryTextureSize);
            return (int)pixels[((size_t)y * kLiveryTextureSize + (size_t)x) * 4 + 3];
        };
        auto expect = [](double g) { return (int)(g * 255.0 + 0.5); };

        // Swatch column coordinates match livery.cpp's own fills (and
        // gen_car_rig.py's SW_* constants, which check_car_rig.py pins).
        const int aTread = alphaAt(0.90, 0.25);   // tire rubber
        const int aSide = alphaAt(0.90, 0.75);    // sidewall rubber
        const int aRim = alphaAt(0.815, 0.75);    // metallic rim
        // T27: 0.325 -> 0.290. The windshield now spans u 0.256-0.345 (cowl
        // 0.95, A-pillar 0.365) and 0.325 fell inside its sun-strip accent,
        // which carries decal gloss, not glass gloss. 0.290 is mid-pane.
        // ...and 0.290 landed on the second roll-cage bar (matte, 0.85 alpha:
        // it read 33). 0.266 is open glass between the frame trim and bar one.
        const int aGlass = alphaAt(0.266, 0.44);  // windshield
        const int aPaint = alphaAt(0.100, 0.25);  // plain body paint

        std::printf("livery_test: T12 gloss mask -- tread %d sidewall %d rim %d glass %d paint %d\n",
                    aTread, aSide, aRim, aGlass, aPaint);

        expectTrue("T12: tire tread carries the rubber gloss", aTread == expect(kGlossRubber));
        expectTrue("T12: tire sidewall carries the rubber gloss", aSide == expect(kGlossRubber));
        expectTrue("T12: wheel rim carries the steel gloss", aRim == expect(kGlossSteel));
        expectTrue("T12: glass carries the glass gloss", aGlass == expect(kGlossGlass));
        expectTrue("T12: plain body paint carries the paint gloss", aPaint == expect(kGlossPaint));

        // The ordering is the property that actually matters, and it is what
        // a regression to a constant alpha breaks first: if the mask ever goes
        // uniform again these collapse to equal, whatever the value is.
        expectTrue("T12: rubber is less reflective than paint", aTread < aPaint);
        // T22: the rim went from chrome to steel, so it is now LESS reflective
        // than paint, not more. The ordering that still matters is that
        // rubber is the least reflective thing and glass the most.
        expectTrue("T12: steel rim is less reflective than clearcoat paint", aRim < aPaint);
        expectTrue("T12: steel rim is less reflective than glass", aRim < aGlass);

        // And the whole-texture form of the same statement: a uniform mask has
        // one distinct alpha value. This is the clause that fails loudest if
        // someone reinstates `pixels_[idx + 3] = 255`.
        bool seen[256] = {false};
        int distinct = 0;
        for (size_t i = 3; i < pixels.size(); i += 4) {
            if (!seen[pixels[i]]) {
                seen[pixels[i]] = true;
                ++distinct;
            }
        }
        std::printf("livery_test: T12 gloss mask has %d distinct values\n", distinct);
        expectTrue("T12: the gloss mask is not a constant", distinct >= 5);
    }

    // T13: baked ambient occlusion in the arches and along the rocker.
    //
    // The AO scales the gloss channel as well as the colour, which is the part
    // worth guarding: gloss drives fs_car.sc's reflectMix, so an occluded
    // surface seeing less of the environment is what stops a wheelhouse
    // reading as a lit blister. A version that only darkened albedo would look
    // almost right in a still and still reflect a full share of bright sky.
    {
        LiveryScheme scheme{0, 0, 0, CarPalette::White};
        const auto pixels = buildLiveryPixels(red, 28, 1, &scheme);
        auto alphaAt = [&](double u, double v) {
            const int x = (int)(u * kLiveryTextureSize), y = (int)(v * kLiveryTextureSize);
            return (int)pixels[((size_t)y * kLiveryTextureSize + (size_t)x) * 4 + 3];
        };
        auto lumAt = [&](double u, double v) {
            const int x = (int)(u * kLiveryTextureSize), y = (int)(v * kLiveryTextureSize);
            return luminance(pixelAt(pixels, x, y));
        };

        // Deep in the front wheelhouse vs the open door at the same height.
        // 0.155 is the front arch centre, 0.40 is clear of both arches.
        const int aArch = alphaAt(0.155, 0.980);
        const int aOpen = alphaAt(0.400, 0.500);

        // The rocker at a U clear of both arches: this is the band that is
        // ONLY darkened by AO.
        const int aRocker = alphaAt(0.400, 0.980);

        // Past the lip the fender is convex and open: AO must NOT reach it, or
        // the whole flank goes muddy. 0.860 is below kArchLipV (0.8848).
        const int aFender = alphaAt(0.155, 0.860);

        std::printf("livery_test: T13 AO -- wheelhouse %d, rocker %d, open body %d, fender-past-lip %d (gloss)\n",
                    aArch, aRocker, aOpen, aFender);

        // EVERY CLAUSE HERE READS THE GLOSS CHANNEL, and that is deliberate.
        // The first draft asserted that the wheelhouse was dark in COLOUR, and
        // it passed with the AO pass entirely commented out -- livery.cpp has
        // painted a JS-inherited shadow ring into the arches since long before
        // this, and the 3-tone shading already darkens the rocker, so colour
        // there proves nothing about occlusion. Gloss is touched by nothing
        // else, so it is the only channel that actually isolates this feature.
        expectTrue("T13: the wheelhouse is occluded (gloss damped well below open bodywork)",
                   aArch < aOpen * 3 / 4);
        expectTrue("T13: the rocker is occluded along its whole length, not just at the arches",
                   aRocker < aOpen * 9 / 10);
        expectTrue("T13: AO stops at the fender lip and does not bleed onto the open flank",
                   aFender > aOpen * 9 / 10);
    }

    // T17: the hood and deck wordmarks run ACROSS the car, not along it.
    //
    // They used to be ordinary along-u badges, and the render showed the hood
    // reading backwards. That is not a missing mirror flag: a string running
    // nose-to-tail along a HORIZONTAL surface reads left-to-right from one
    // side of the car and right-to-left from the other, so it is always
    // backwards from one of them and no flag can fix it. Running it across the
    // car makes it read from the front, which is where you stand to look at a
    // hood.
    //
    // The deck is the exact 180-degree counterpart, and it cannot be checked
    // in a render -- from every showcase angle the deck lid is edge-on behind
    // the spoiler. So it is checked here, where the geometry is decidable: a
    // rotated run is TALLER in v than it is WIDE in u, which is precisely the
    // property that was false before and is the thing the fix changes.
    {
        LiveryScheme scheme{0, 0, 0, CarPalette::White};
        const auto pixels = buildLiveryPixels(red, 28, 1, &scheme);

        // Bounding box of the light lettering inside a window around a mark.
        auto inkAspect = [&](double cu, double cv, double halfU, double halfV, const char* what) {
            int u0 = kLiveryTextureSize, u1 = -1, v0 = kLiveryTextureSize, v1 = -1;
            const int x0 = (int)((cu - halfU) * kLiveryTextureSize);
            const int x1 = (int)((cu + halfU) * kLiveryTextureSize);
            const int y0 = (int)((cv - halfV) * kLiveryTextureSize);
            const int y1 = (int)((cv + halfV) * kLiveryTextureSize);
            for (int y = y0; y < y1; ++y) {
                for (int x = x0; x < x1; ++x) {
                    // Light ink on its own dark plate -- the same signature the
                    // T4 wordmark guard uses, for the same reason: counting
                    // dark pixels would measure the plate, not the letters.
                    if (luminance(pixelAt(pixels, x, y)) > 0.70) {
                        u0 = std::min(u0, x); u1 = std::max(u1, x);
                        v0 = std::min(v0, y); v1 = std::max(v1, y);
                    }
                }
            }
            const int w = u1 - u0, h = v1 - v0;
            std::printf("livery_test: T17 %s wordmark ink %d wide (u) x %d tall (v)\n", what, w, h);
            return std::pair<int, int>{w, h};
        };

        // Windows sized to the PLATE, not generously around it. The first
        // version used halfU 0.050 for the hood and swept in the scheme's own
        // white blocks, which sit right beside the mark on this style -- the
        // measured box came out 204x302 instead of the plate's own ~44 wide,
        // and the aspect ratio it was testing was mostly those blocks. Plate
        // half-width is cap height/2 + drawText's 0.40*h padding.
        const auto hood = inkAspect(0.170, 0.500, 0.0215, 0.095, "hood");
        const auto deck = inkAspect(0.620, 0.500, 0.0165, 0.075, "deck");

        expectTrue("T17: hood wordmark is painted at all", hood.first > 0 && hood.second > 0);
        expectTrue("T17: deck wordmark is painted at all", deck.first > 0 && deck.second > 0);
        // A run of several letters at one cap height is far longer than it is
        // tall, so once rotated the v extent must dominate by a wide margin.
        // Before the fix these read roughly 5:1 the other way.
        expectTrue("T17: the hood wordmark runs across the car, not along it",
                   hood.second > hood.first * 3 / 2);
        expectTrue("T17: the deck wordmark runs across the car, not along it",
                   deck.second > deck.first * 3 / 2);
    }

    // T18: the driver's window net -- the first mark on the car that must NOT
    // be mirrored.
    //
    // This is the exact inverse of T10's guard, and both are needed. Nearly
    // everything on the livery has to be mirrored between the flanks so it
    // reads forwards from either side; the net must NOT be, because a real car
    // has webbing over the driver's window and open glass on the other side.
    // A future change that "fixes" the asymmetry by mirroring everything would
    // pass T10 and silently put a net on both windows.
    //
    // Driver's side is +z, which car_v() maps to HIGH v -- the same mapping
    // check_car_rig.py asserts out of the generator's vertex data.
    {
        LiveryScheme scheme{0, 0, 0, CarPalette::White};
        const auto pixels = buildLiveryPixels(red, 28, 1, &scheme);

        // Count webbing bars crossed by a vertical scan through each side-glass
        // band: a strap is a run brighter than the glass it sits on.
        // T22 broke this guard, and the way it broke is worth keeping. It
        // counted pixels BRIGHTER than an absolute 0.10 as webbing, which
        // worked only because the glass underneath was near-black. Once the
        // glass was lightened to the reference's aperture tone the glass
        // itself cleared that threshold and the open passenger window read as
        // four bars of net.
        //
        // An absolute threshold was always the wrong test. The webbing is
        // whatever CONTRASTS with the glass around it, so measure it that way:
        // sample the glass at a v the net's straps never occupy and count runs
        // that depart from it. That survives any future change to either tone.
        auto barsCrossed = [&](double v0, double v1) {
            // T28: SCAN SEVERAL COLUMNS AND TAKE THE BEST, instead of picking
            // one. A single column is hostage to where the net's VERTICAL
            // weave straps happen to fall: this probe was moved to 0.365 when
            // the pane moved, landed on a 2-texel vertical strap, and then the
            // whole column read as one bar -- so the reference sample WAS the
            // strap and the guard reported zero webbing on a fully netted
            // window. Which column is "the middle of the pane" is not a
            // property worth encoding; that the pane carries webbing is.
            //
            // 0.360-0.405 sits inside the door pane on both of the leaning
            // pane's rows (u 0.339-0.472 at the belt, 0.349-0.483 at the roof
            // edge) and clear of the A-pillar seal at either end.
            int best = 0;
            for (double fu = 0.360; fu <= 0.405; fu += 0.004) {
            const int x = (int)(fu * kLiveryTextureSize);
            const double glassLum =
                luminance(pixelAt(pixels, x, (int)((v0 + (v1 - v0) * 0.02) * kLiveryTextureSize)));
            int bars = 0;
            bool onBar = false;
            for (int y = (int)(v0 * kLiveryTextureSize); y < (int)(v1 * kLiveryTextureSize); ++y) {
                const bool onWebbing = std::fabs(luminance(pixelAt(pixels, x, y)) - glassLum) > 0.04;
                if (onWebbing && !onBar) ++bars;
                onBar = onWebbing;
            }
            best = std::max(best, bars);
            }
            return best;
        };
        const int driverBars = barsCrossed(0.595, 0.660);
        const int passengerBars = barsCrossed(0.340, 0.405);
        std::printf("livery_test: T18 window net -- driver side %d bars, passenger side %d\n",
                    driverBars, passengerBars);

        expectTrue("T18: the driver's window carries net webbing", driverBars >= 3);
        expectTrue("T18: the passenger window is open glass, not netted", passengerBars == 0);
    }

    // ---- T23e: the front contingency stack and the rocker fastener row ----
    //
    // Both are DENSITY features: what matters is not any one chip's colour but
    // that a cluster of distinct chips and a repeating row of rivets are
    // actually there, on the panel, clear of everything else painted on it. So
    // both clauses count, and both print what they counted -- a decal guard
    // that only samples one point passes just as happily against a single chip
    // as against the block it is supposed to describe.
    {
        LiveryScheme scheme{0, 0, 0, CarPalette::White};
        const auto pixels = buildLiveryPixels(red, 7, 1, &scheme);

        // THE FIRST VERSION OF THIS CLAUSE WAS WORTHLESS and the replacement is
        // shaped by why. It counted distinct saturated tones in the stack's U/V
        // box and asked for four or more. Disabling the stack entirely still
        // produced six -- the body paint and the scheme's own stripes are
        // saturated too -- so it passed against the exact defect it describes.
        // That is the fourth guard in this project to do that.
        //
        // What actually distinguishes a chip grid from a painted panel is
        // STRUCTURE, not palette: scanning DOWN a chip column must cross four
        // separate saturated runs, one per row, where plain bodywork gives one
        // long run however colourful it is.
        int minRuns = 1 << 30, totalTones = 0;
        std::set<int> chipTones;
        auto saturated = [](const std::array<double, 3>& px) {
            const double mx = std::max({px[0], px[1], px[2]});
            const double mn = std::min({px[0], px[1], px[2]});
            return mx >= 0.25 && mx - mn >= 0.20;
        };
        // T32: the column positions come from livery.h's kChip* anchors, which
        // hang off kArchFrontU1 -- the same edit that moved the stack forward
        // with the wheel opening used to leave this scan on bare paint.
        for (int col = 0; col < kChipCols; ++col) {
            const int x = (int)((kChipU0 + col * kChipDU + kChipW * 0.5) * kLiveryTextureSize);
            int runs = 0;
            bool on = false;
            for (int y = (int)((kChipV0 - 0.006) * kLiveryTextureSize);
                 y < (int)((kChipV0 + kChipRows * kChipDV) * kLiveryTextureSize); ++y) {
                const auto px = pixelAt(pixels, x, y);
                const bool sat = saturated(px);
                if (sat && !on) ++runs;
                if (sat) chipTones.insert(((int)(px[0] * 8) << 6) | ((int)(px[1] * 8) << 3) | (int)(px[2] * 8));
                on = sat;
            }
            minRuns = std::min(minRuns, runs);
        }
        totalTones = (int)chipTones.size();
        std::printf("livery_test: T23e front contingency stack -- %d chips down the thinnest column, %d tones\n",
                    minRuns, totalTones);
        expectTrue("T23e: the front quarter carries a STACK of chips, not one painted panel",
                   minRuns >= kChipRows);
        expectTrue("T23e: the stack is multi-colour", totalTones >= 3);

        // The fastener row, counted as RUNS of pale texels along its own v, so
        // one long smear cannot pass as a row of rivets.
        const int fy = (int)(0.9435 * kLiveryTextureSize);
        int rivets = 0;
        bool on = false;
        for (int x = (int)((kArchFrontU1 + 0.006) * kLiveryTextureSize);
             x < (int)((kArchRearU0 - 0.002) * kLiveryTextureSize); ++x) {
            const bool pale = luminance(pixelAt(pixels, x, fy)) > 0.30;
            if (pale && !on) ++rivets;
            on = pale;
        }
        std::printf("livery_test: T23e rocker fastener row -- %d separate rivets\n", rivets);
        expectTrue("T23e: the rocker carries a repeating row of fasteners, not a stripe", rivets >= 12);
    }

    // ---- T31: the underbody row is near-black for its whole length ----
    //
    // gen_car_rig.py emits every underbody floor quad with v = 0.01 (and 0.99
    // for its mirror), so that single texture row is the entire floor of the
    // car. Nothing had ever checked it, and the wheel-arch shadow rings --
    // circles centred at v 0.055 with radius 0.071, the outermost of them the
    // BODY COLOUR -- were painted after the near-black rocker band and put a
    // green disc through it at both arches. It showed in the chase and rear
    // views as a striped panel under the back bumper.
    //
    // Scanned across the body wrap (u 0.02-0.78) rather than sampled at a
    // point: the defect was two discs, and a point check placed anywhere but
    // on one of them would have passed.
    {
        LiveryScheme scheme{0, 0, 0, CarPalette::White};
        const auto pixels = buildLiveryPixels(red, 7, 1, &scheme);
        double worst = 0.0;
        int worstX = 0;
        for (int v : {1, 3, 8}) {
            for (int x = (int)(0.02 * kLiveryTextureSize); x < (int)(0.78 * kLiveryTextureSize); ++x) {
                for (int y : {v, kLiveryTextureSize - 1 - v}) {
                    const double l = luminance(pixelAt(pixels, x, y));
                    if (l > worst) { worst = l; worstX = x; }
                }
            }
        }
        std::printf("livery_test: T31 underbody row -- brightest %.3f at u %.3f\n",
                    worst, (double)worstX / kLiveryTextureSize);
        expectTrue("T31: the underbody row is near-black for the whole body wrap", worst < 0.12);
    }

    if (g_failures == 0) {
        std::printf("livery_test: shading bands, stripe styles, and number decals all match expectations.\n");
        return 0;
    }
    std::fprintf(stderr, "livery_test: %d MISMATCHES.\n", g_failures);
    return 1;
}
