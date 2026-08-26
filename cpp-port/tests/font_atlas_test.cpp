// G26 (graphics pass): exercises the font atlas -- both the baked table's own
// self-consistency and the layout math built on it. bgfx-free, same
// hand-rolled expect/expectNear pattern as particles_test.cpp.
//
// WHY THESE PROPERTIES. The atlas is a generated artifact, so the failure
// mode that matters is a regeneration that silently breaks an invariant the
// renderer depends on -- a glyph packed past the atlas edge (samples garbage),
// a zero advance (the pen never moves and the string piles up on one spot), a
// measure() that disagrees with what pushText() emits (every centred and
// right-aligned label in the HUD lands wrong). None of those would crash;
// they would just look broken on a phone, which is precisely the kind of bug
// this project has repeatedly shipped and then spent a round chasing.

#include "../src/render/font_atlas.h"
#include "../src/render/font_atlas_data.h"

#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

namespace {
int g_failures = 0;

void expect(bool cond, const char* what) {
    if (!cond) {
        std::fprintf(stderr, "FAIL: %s\n", what);
        ++g_failures;
    }
}

void expectNear(const char* label, double got, double expected, double tol = 1e-4) {
    if (std::fabs(got - expected) > tol) {
        std::fprintf(stderr, "%s: got %.9g expected %.9g (diff %.3g)\n", label, got, expected,
                     got - expected);
        ++g_failures;
    }
}

// Bounding box of everything pushText() emitted, so the tests can assert on
// where the ink actually landed rather than trusting the metrics table twice.
struct Bounds {
    bool any = false;
    float x0 = 0, y0 = 0, x1 = 0, y1 = 0;
    float u0 = 0, v0 = 0, u1 = 0, v1 = 0;
};

Bounds boundsOf(const std::vector<PosColorUvVertex>& v) {
    Bounds b;
    for (const PosColorUvVertex& p : v) {
        if (!b.any) {
            b.any = true;
            b.x0 = b.x1 = p.x;
            b.y0 = b.y1 = p.y;
            b.u0 = b.u1 = p.u;
            b.v0 = b.v1 = p.v;
            continue;
        }
        b.x0 = std::min(b.x0, p.x);
        b.x1 = std::max(b.x1, p.x);
        b.y0 = std::min(b.y0, p.y);
        b.y1 = std::max(b.y1, p.y);
        b.u0 = std::min(b.u0, p.u);
        b.u1 = std::max(b.u1, p.u);
        b.v0 = std::min(b.v0, p.v);
        b.v1 = std::max(b.v1, p.v);
    }
    return b;
}
} // namespace

int main() {
    // ---- the baked table's own invariants ----
    {
        expect(fontatlas::kGlyphCount == fontatlas::kLastChar - fontatlas::kFirstChar + 1,
               "glyph table covers exactly the declared character range");
        expect(fontatlas::kAtlasWidth > 0 && fontatlas::kAtlasHeight > 0, "atlas has a size");
        expect(fontatlas::kBakedPixelSize > 0, "baked pixel size is positive");
        expect(fontatlas::kAscent > 0 && fontatlas::kDescent > 0, "vertical metrics are positive");
        expect(fontatlas::kAtlasPngSize > 0, "atlas PNG blob is non-empty");

        int inked = 0;
        for (int i = 0; i < fontatlas::kGlyphCount; ++i) {
            const fontatlas::Glyph& g = fontatlas::kGlyphs[i];
            const char ch = (char)(fontatlas::kFirstChar + i);
            char label[96];

            // Packed inside the atlas. A glyph whose rect runs past the edge
            // samples whatever is wrapped around from the next row.
            std::snprintf(label, sizeof(label), "glyph '%c' packed inside the atlas", ch);
            expect(g.x >= 0 && g.y >= 0 && g.x + g.w <= fontatlas::kAtlasWidth &&
                       g.y + g.h <= fontatlas::kAtlasHeight,
                   label);

            // Non-negative advance, and positive for anything with ink. A zero
            // advance on a visible glyph stacks the whole string on one pen
            // position -- silent, and unmistakable once seen.
            std::snprintf(label, sizeof(label), "glyph '%c' advance is sane", ch);
            expect(g.advance >= 0.0f && (g.w == 0 || g.advance > 0.0f), label);

            if (g.w > 0 && g.h > 0) ++inked;
        }
        expect(inked >= 90, "nearly every printable ASCII glyph carries ink");

        // Space is the one character that must advance without drawing.
        const fontatlas::Glyph& space = fontatlas::kGlyphs[' ' - fontatlas::kFirstChar];
        expect(space.w == 0 || space.h == 0, "space glyph has no ink");
        expect(space.advance > 0.0f, "space glyph still advances the pen");
    }

    // ---- measure() ----
    {
        const float size = (float)font::bakedPixelSize();
        expectNear("empty string measures zero", font::measure("", size), 0.0);

        // Additive over concatenation: this is what lets the HUD measure a
        // label once and lay out around it.
        const float a = font::measure("LAP ", size);
        const float b = font::measure("12/50", size);
        expectNear("measure is additive", font::measure("LAP 12/50", size), a + b, 1e-3);

        // Linear in requested size, since everything is one uniform scale off
        // the baked metrics.
        const float full = font::measure("BEST", size);
        expectNear("measure halves at half size", font::measure("BEST", size * 0.5f), full * 0.5,
                   1e-3);

        // Proportional, not monospace -- the whole reason this exists.
        expect(font::measure("W", size) > font::measure("i", size) + 1.0f,
               "font is proportional: 'W' is meaningfully wider than 'i'");
    }

    // ---- pushText(): geometry count and placement ----
    {
        const float size = (float)font::bakedPixelSize();
        std::vector<PosColorUvVertex> v;

        font::pushText(v, 0.0f, 0.0f, "", size, 0xffffffff);
        expect(v.empty(), "empty string emits no geometry");

        // Six vertices (two triangles) per inked glyph; spaces emit none.
        font::pushText(v, 0.0f, 0.0f, "AB", size, 0xffffffff);
        expect(v.size() == 12, "two inked glyphs emit two quads");
        v.clear();
        font::pushText(v, 0.0f, 0.0f, "A B", size, 0xffffffff);
        expect(v.size() == 12, "the space between them adds no geometry");

        // But it does move the pen, so the second glyph sits further right.
        std::vector<PosColorUvVertex> tight, spaced;
        font::pushText(tight, 0.0f, 0.0f, "AB", size, 0xffffffff);
        font::pushText(spaced, 0.0f, 0.0f, "A B", size, 0xffffffff);
        expect(boundsOf(spaced).x1 > boundsOf(tight).x1, "a space still advances the pen");
    }

    // ---- pushText(): the pen advance matches measure() ----
    {
        // The property that keeps centring honest. Two runs of the same string
        // starting one measured width apart must not overlap, and must sit
        // flush -- if measure() over- or under-reported, this shows it as a gap
        // or a collision rather than as an abstract number mismatch.
        const float size = (float)font::bakedPixelSize();
        const std::string s = "PIT ROAD";
        const float w = font::measure(s, size);

        std::vector<PosColorUvVertex> one, two;
        font::pushText(one, 0.0f, 0.0f, s, size, 0xffffffff);
        font::pushText(two, w, 0.0f, s, size, 0xffffffff);

        const Bounds b1 = boundsOf(one), b2 = boundsOf(two);
        expect(b1.any && b2.any, "both runs emitted geometry");
        expect(b2.x0 >= b1.x1 - 1.0f, "measure() is not narrower than the ink it reports");
        // The second run starts within one glyph's left bearing of where the
        // first ended -- i.e. the advance is not wildly over-reported either.
        expect(b2.x0 - b1.x1 < size, "measure() is not padded with dead space");
    }

    // ---- pushText(): y is the baseline ----
    {
        const float size = (float)font::bakedPixelSize();
        std::vector<PosColorUvVertex> caps, desc;
        font::pushText(caps, 0.0f, 100.0f, "H", size, 0xffffffff);
        font::pushText(desc, 0.0f, 100.0f, "p", size, 0xffffffff);

        const Bounds bc = boundsOf(caps), bd = boundsOf(desc);
        // Screen-space y grows downward, matching ui_draw.cpp's ortho.
        expect(bc.y1 <= 100.5f, "a capital sits entirely above the baseline");
        expect(bd.y1 > 100.5f, "a descender drops below the baseline");
        expect(bc.y0 > 100.0f - font::ascent(size) - 1.0f,
               "the cap height fits inside the reported ascent");
    }

    // ---- pushText(): scaling and translation ----
    {
        const float size = (float)font::bakedPixelSize();
        std::vector<PosColorUvVertex> at0, at50;
        font::pushText(at0, 0.0f, 0.0f, "48", size, 0xffffffff);
        font::pushText(at50, 50.0f, 0.0f, "48", size, 0xffffffff);
        expect(at0.size() == at50.size(), "translation does not change the vertex count");
        expectNear("translation shifts every vertex by exactly dx", at50[0].x - at0[0].x, 50.0);

        std::vector<PosColorUvVertex> half;
        font::pushText(half, 0.0f, 0.0f, "48", size * 0.5f, 0xffffffff);
        const Bounds bf = boundsOf(at0), bh = boundsOf(half);
        expectNear("half size halves the ink width", bh.x1 - bh.x0, (bf.x1 - bf.x0) * 0.5, 1e-3);
    }

    // ---- pushText(): UVs address the glyph the table points at ----
    {
        // Checked PER GLYPH against its own packed rect, not just against the
        // [0,1] atlas bounds. A first version of this test only asserted the
        // latter, and a deliberate 8-texel overshoot on every glyph sailed
        // straight through it -- an interior glyph can be badly wrong and
        // still have UVs inside the atlas. What actually matters is that the
        // quad samples that glyph's rect and nothing else, so that is what is
        // asserted: each edge within half a texel of the rect, inset inward.
        const float invW = 1.0f / (float)fontatlas::kAtlasWidth;
        const float invH = 1.0f / (float)fontatlas::kAtlasHeight;
        const float halfU = 0.5f * invW, halfV = 0.5f * invH;
        const float tol = 1e-5f;

        for (int i = 0; i < fontatlas::kGlyphCount; ++i) {
            const fontatlas::Glyph& g = fontatlas::kGlyphs[i];
            if (g.w <= 0 || g.h <= 0) continue;
            const char ch = (char)(fontatlas::kFirstChar + i);

            std::vector<PosColorUvVertex> v;
            font::pushText(v, 0.0f, 0.0f, std::string(1, ch), 24.0f, 0xffffffff);
            const Bounds b = boundsOf(v);

            char label[96];
            std::snprintf(label, sizeof(label), "glyph '%c' UVs match its packed rect", ch);
            const bool matches = std::fabs(b.u0 - ((float)g.x * invW + halfU)) < tol &&
                                 std::fabs(b.v0 - ((float)g.y * invH + halfV)) < tol &&
                                 std::fabs(b.u1 - ((float)(g.x + g.w) * invW - halfU)) < tol &&
                                 std::fabs(b.v1 - ((float)(g.y + g.h) * invH - halfV)) < tol;
            expect(matches, label);

            std::snprintf(label, sizeof(label), "glyph '%c' UVs land inside the atlas", ch);
            expect(b.u0 >= 0.0f && b.v0 >= 0.0f && b.u1 <= 1.0f && b.v1 <= 1.0f, label);
        }
    }

    // ---- pushText(): out-of-range bytes are skipped, not indexed ----
    {
        // A high-bit byte from a data file must not index off the end of the
        // glyph table. L4 already forced the spotter strings to ASCII for a
        // related reason; the text renderer should not be the thing that
        // crashes when that slips again.
        std::vector<PosColorUvVertex> clean, dirty;
        font::pushText(clean, 0.0f, 0.0f, "AB", 24.0f, 0xffffffff);
        std::string s = "A";
        s += (char)0xE9; // 'e' acute in latin-1, outside the baked range
        s += '\t';       // control character, likewise
        s += "B";
        font::pushText(dirty, 0.0f, 0.0f, s, 24.0f, 0xffffffff);
        expect(clean.size() == dirty.size(), "unbaked bytes emit no geometry");
        expectNear("unbaked bytes do not advance the pen", font::measure(s, 24.0f),
                   font::measure("AB", 24.0f), 1e-3);
    }

    // ---- pushTextShadowed() ----
    {
        std::vector<PosColorUvVertex> plain, shadowed;
        font::pushText(plain, 10.0f, 10.0f, "P1", 24.0f, 0xffffffff);
        font::pushTextShadowed(shadowed, 10.0f, 10.0f, "P1", 24.0f, 0xffffffff, 0xff000000, 2.0f);
        expect(shadowed.size() == plain.size() * 2, "shadowed text draws the string twice");
        // Shadow first, so the main pass covers it.
        expect(shadowed[0].abgr == 0xff000000u, "the shadow pass is emitted first");
        expect(shadowed[plain.size()].abgr == 0xffffffffu, "the main pass is emitted on top");
        expectNear("the shadow is offset by the requested amount",
                   shadowed[0].x - shadowed[plain.size()].x, 2.0);
    }

    // ---- decodeAtlas(): the embedded PNG round-trips ----
    {
        const font::AtlasImage img = font::decodeAtlas();
        expect(img.ok, "the embedded atlas PNG decodes");
        if (img.ok) {
            expect(img.width == fontatlas::kAtlasWidth && img.height == fontatlas::kAtlasHeight,
                   "decoded atlas matches the declared dimensions");
            expect(img.rgba8.size() == (size_t)img.width * (size_t)img.height * 4,
                   "decoded atlas is RGBA8");

            // Sample the centre of a glyph known to be solid there. If the
            // pack offsets and the image disagree, this reads background.
            const fontatlas::Glyph& g = fontatlas::kGlyphs['M' - fontatlas::kFirstChar];
            expect(g.w > 0 && g.h > 0, "'M' has ink to sample");
            if (g.w > 0 && g.h > 0) {
                int lit = 0;
                for (int dy = 0; dy < g.h; ++dy) {
                    for (int dx = 0; dx < g.w; ++dx) {
                        const size_t o = ((size_t)(g.y + dy) * img.width + (g.x + dx)) * 4;
                        if (img.rgba8[o] > 128) ++lit;
                    }
                }
                expect(lit > 0, "'M' has coverage where the table says it is");
            }

            // The gutter between shelves must be empty, or bilinear filtering
            // bleeds one glyph into its neighbour at small sizes.
            bool cornerClear = img.rgba8[0] < 8;
            expect(cornerClear, "the atlas margin is transparent");
        }
    }

    std::printf(g_failures == 0 ? "font_atlas_test: PASS\n" : "font_atlas_test: FAILURES ABOVE\n");
    return g_failures == 0 ? 0 : 1;
}
