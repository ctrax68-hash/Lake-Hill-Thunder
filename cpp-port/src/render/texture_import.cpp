#include "texture_import.h"

// Single-header library, implementation compiled exactly here (STB-style
// convention, same as mesh_import.cpp's CGLTF_IMPLEMENTATION) -- see
// texture_import.h's own comment for why this reuses bimg's already-
// vendored copy instead of a separate one under this port's own
// third_party/.
#define STB_IMAGE_IMPLEMENTATION
#define STBI_NO_STDIO // this pipeline only ever decodes from an in-memory buffer
#include <stb_image.h>

DecodedImage decodeImageRGBA8(const uint8_t* data, size_t size) {
    DecodedImage result;
    int w = 0, h = 0, channelsInFile = 0;
    stbi_uc* pixels = stbi_load_from_memory(data, (int)size, &w, &h, &channelsInFile, 4);
    if (!pixels) return result;

    result.ok = true;
    result.width = w;
    result.height = h;
    result.rgba8.assign(pixels, pixels + (size_t)w * h * 4);
    stbi_image_free(pixels);
    return result;
}
