#pragma once

// G27 (graphics pass): the HUD's typographic vocabulary, on top of G26's
// font atlas.
//
// WHY THIS EXISTS RATHER THAN font_atlas.h CALLS EVERYWHERE. Converting the
// HUD off dbgText means touching ~30 call sites across six files. font_atlas
// deals in baselines, pixel sizes and packed colours; a HUD call site wants
// to say "this is a caption" or "this is the value under it" and have every
// other caption in the game agree with it. Without a shared vocabulary the
// sizes and colours drift the moment two files are edited a week apart --
// which is exactly what happened to the dbgText colour attributes this
// replaces (five files each defined their own copy of the VGA palette).
//
// SIZES. Fixed pixels, not viewport-relative, matching the HUD's existing
// documented "fixed pixel layout" precedent (hud.cpp, menu.cpp). The
// reference's HUD is a proportional bold sans at roughly three sizes over a
// live scene, drop-shadowed rather than panelled, and these are those three.

#include "color.h"
#include "font_atlas.h"

#include <string>
#include <vector>

namespace hudtext {

// The three roles the reference's HUD actually uses.
//
// kCaption: the small all-caps labels over a readout ("LAP TIME", "GEAR").
// kBody:    list rows -- leaderboard entries, ticker items.
// kValue:   a readout's number when it is not a segmented display.
constexpr float kCaption = 13.0f;
constexpr float kBody = 15.0f;
constexpr float kValue = 26.0f;

// Everything on this HUD sits over a live 3D scene that can be bright sky or
// dark asphalt from one frame to the next, so every string is shadowed --
// without it, light text vanishes against a pale wall. The reference does the
// same, which is why its HUD needs no panel behind the text.
constexpr float kShadow = 1.5f;
inline uint32_t shadowColor() { return packColor(Theme::kBlack, 0.8f); }

// `y` is the BASELINE in every call here, matching font_atlas. Callers
// positioning a block by its top edge should add font::ascent(size).
inline void draw(std::vector<PosColorUvVertex>& out, float x, float y, const std::string& text,
                 float size, uint32_t color) {
    font::pushTextShadowed(out, x, y, text, size, color, shadowColor(), kShadow);
}

// Right-aligned: the last glyph ends at `rightX`. Readouts whose digit count
// changes stay pinned to one edge instead of growing off-centre -- the same
// reason hud.cpp's seven-segment values are right-aligned.
inline void drawRight(std::vector<PosColorUvVertex>& out, float rightX, float y,
                      const std::string& text, float size, uint32_t color) {
    draw(out, rightX - font::measure(text, size), y, text, size, color);
}

inline void drawCentered(std::vector<PosColorUvVertex>& out, float centerX, float y,
                         const std::string& text, float size, uint32_t color) {
    draw(out, centerX - font::measure(text, size) * 0.5f, y, text, size, color);
}

// A caption in the HUD's standard label treatment: small, cool grey, so the
// value it labels is what the eye lands on. Takes the caption's TOP edge
// rather than its baseline, because captions are positioned against panel
// geometry, and a panel's top is the thing actually known at the call site.
inline void caption(std::vector<PosColorUvVertex>& out, float x, float topY,
                    const std::string& text) {
    draw(out, x, topY + font::ascent(kCaption), text, kCaption, packColor(Theme::kGraycool));
}

// Centre-screen announcements -- spotter calls, flag states. Outlined rather
// than merely shadowed: these are large, briefly shown, and land anywhere on
// the scene, so a single offset shadow leaves one edge unreadable against the
// wrong background. Four offsets is the cheapest outline that survives that,
// at 5x the geometry of plain text -- affordable only because at most one of
// these is on screen at a time.
inline void drawOutlined(std::vector<PosColorUvVertex>& out, float centerX, float y,
                         const std::string& text, float size, uint32_t color) {
    const uint32_t edge = packColor(Theme::kBlack, 0.9f);
    const float x = centerX - font::measure(text, size) * 0.5f;
    const float o = std::max(1.5f, size * 0.06f);
    font::pushText(out, x - o, y, text, size, edge);
    font::pushText(out, x + o, y, text, size, edge);
    font::pushText(out, x, y - o, text, size, edge);
    font::pushText(out, x, y + o, text, size, edge);
    font::pushText(out, x, y, text, size, color);
}

} // namespace hudtext
