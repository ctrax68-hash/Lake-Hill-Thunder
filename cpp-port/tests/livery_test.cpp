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

    if (g_failures == 0) {
        std::printf("livery_test: shading bands, stripe styles, and number decals all match expectations.\n");
        return 0;
    }
    std::fprintf(stderr, "livery_test: %d MISMATCHES.\n", g_failures);
    return 1;
}
