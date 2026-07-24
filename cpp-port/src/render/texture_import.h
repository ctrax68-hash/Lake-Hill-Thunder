#pragma once

// Step 2 (PORT_PROGRESS.md, glTF skinned-mesh import pipeline): decodes an
// encoded image (PNG/JPEG bytes, e.g. mesh_import.h's
// ImportedMesh::baseColorImageBytes) into a plain RGBA8 pixel buffer --
// bgfx-free, same "pure logic vs GPU" split as this port's other texture
// producers (atlas_texture.h/sky_texture.h/livery.h), which all hand-roll
// their own pixel buffers and let renderer.cpp do the actual
// bgfx::createTexture2D() upload.
//
// Uses stb_image.h directly (already vendored as one of bimg's own
// 3rdparty dependencies, third_party/bgfx.cmake/bimg/3rdparty/stb/
// stb_image.h) rather than bimg's own public ImageContainer/imageParse API:
// the pinned bimg version's public header (bimg.h) only exposes
// imageParse() for container formats (DDS/KTX/PVR3) -- its PNG/JPEG decode
// path is internal to image_decode.cpp, not part of bimg's public API
// surface in this vendored version. stb_image.h is the same, well-tested
// decoder bimg itself already uses internally for those formats, so this
// is not a step down in reliability, just a more direct route to it.

#include <cstdint>
#include <vector>

struct DecodedImage {
    bool ok = false;
    int width = 0;
    int height = 0;
    std::vector<uint8_t> rgba8; // width*height*4 bytes, row-major, top-to-bottom
};

DecodedImage decodeImageRGBA8(const uint8_t* data, size_t size);
