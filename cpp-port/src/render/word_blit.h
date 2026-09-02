#pragma once

// G28 (graphics pass): draws text into a CPU pixel buffer, using G26's baked
// glyph atlas as a coverage mask.
//
// WHY THIS EXISTS SEPARATELY FROM font_atlas.h. That header emits textured
// quads for the GPU to draw -- it is the runtime path, and it needs a live
// texture and a shader. This is the bake-time path: atlas_texture.cpp paints
// the world atlas procedurally into a byte buffer once at track load, and the
// trackside sponsor boards are part of that image, not separate geometry. So
// the glyphs have to be rasterised into those bytes, not submitted as quads.
//
// WHAT IT UNBLOCKS. atlas_texture.h's own header has carried a note since
// Phase 5e that sponsor-name text was "out of scope" because "JS's drawWord()
// is a full bitmap-font renderer"; stadium_mesh.h repeats it for the turn
// signage, which fell back to LED digits for want of letters. Both deferrals
// were waiting on exactly this.
//
// NOTE ON NAMES. Every wordmark this draws is invented. The project rule is
// that no real sponsor, series or manufacturer name appears anywhere -- the
// point of trackside boards is that the world does not read as empty, and
// invented names do that job completely.

#include <array>
#include <cstdint>
#include <string>
#include <vector>

namespace wordblit {

// A destination pixel buffer: RGBA8, `size` x `size`, row-major.
struct Target {
    uint8_t* pixels;
    int size;
};

// Width in pixels of `text` at `pixelSize`, so a caller can centre it before
// committing to a draw.
double measureWord(const std::string& text, double pixelSize);

// Draws `text` with its LEFT edge at `x` and its BASELINE at `y`, blending the
// glyph coverage over whatever is already there. Clipped to the target.
//
// Returns false when the font atlas could not be decoded, so a caller can fall
// back to a blank panel rather than silently drawing nothing and leaving the
// world looking like the font simply failed to load.
bool drawWord(const Target& dst, double x, double y, const std::string& text, double pixelSize,
              const std::array<double, 3>& color);

// Convenience: centre `text` in the rect and scale it down if it would not
// otherwise fit, so a long invented brand name cannot spill off its board.
// Returns false for the same reason drawWord() does.
bool drawWordCentered(const Target& dst, int rx, int ry, int rw, int rh, const std::string& text,
                      double maxPixelSize, const std::array<double, 3>& color);

} // namespace wordblit
