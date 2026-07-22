#pragma once

// Mirrors index.html:147's `@media (orientation: portrait)` query, which
// gates the CSS #rotate prompt (index.html:140-147,203): a full-screen
// black overlay with a spinning phone icon and "ROTATE YOUR PHONE" text,
// shown whenever the viewport is at least as tall as it is wide. The CSS
// media feature's own definition is height >= width; ties (a perfectly
// square window) count as portrait here to match it exactly.
inline bool isPortrait(int width, int height) {
    return height >= width;
}
