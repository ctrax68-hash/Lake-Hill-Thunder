#pragma once

// Aggregates the per-profile compiled shader bytecode (generated at build
// time by CMakeLists.txt's bgfx_compile_shaders(... AS_HEADERS ...) calls,
// under ${CMAKE_BINARY_DIR}/generated_shaders/<profile>/<shader>.sc.bin.h --
// that directory is on the include path, see CMakeLists.txt) into the table
// bgfx::createEmbeddedShader() needs to pick the right variant for whatever
// renderer backend actually got selected at runtime. See vs_flat.sc's own
// comment for why these are the only two shaders Phase 2 needs.

// embedded_shader.h's BGFX_PLATFORM_SUPPORTS_* guards are broader than
// they look: BGFX_PLATFORM_SUPPORTS_DXBC and _WGSL both unconditionally
// include BX_PLATFORM_LINUX (presumably for cross-compiling toward those
// targets from Linux), so on a desktop Linux build the BGFX_EMBEDDED_SHADER
// macro expects _dxbc/_wgsl byte arrays to exist even though this target
// will only ever select OpenGL/OpenGLES/Vulkan. Force those (and the other
// desktop-Linux-irrelevant backends) off before including the header so it
// only expects the essl/glsl/spirv variants CMakeLists.txt actually compiles.
#define BGFX_PLATFORM_SUPPORTS_DXBC 0
#define BGFX_PLATFORM_SUPPORTS_DXIL 0
#define BGFX_PLATFORM_SUPPORTS_METAL 0
#define BGFX_PLATFORM_SUPPORTS_NVN 0
#define BGFX_PLATFORM_SUPPORTS_PSSL 0
#define BGFX_PLATFORM_SUPPORTS_WGSL 0

#include <bgfx/embedded_shader.h>

#include "spirv/vs_flat.sc.bin.h"
#include "spirv/fs_flat.sc.bin.h"
#include "glsl/vs_flat.sc.bin.h"
#include "glsl/fs_flat.sc.bin.h"
#include "essl/vs_flat.sc.bin.h"
#include "essl/fs_flat.sc.bin.h"

// Phase 5a (PORT_PROGRESS.md): vs_lit/fs_lit, the hemisphere+directional lit
// shader for world-space geometry.
#include "spirv/vs_lit.sc.bin.h"
#include "spirv/fs_lit.sc.bin.h"
#include "glsl/vs_lit.sc.bin.h"
#include "glsl/fs_lit.sc.bin.h"
#include "essl/vs_lit.sc.bin.h"
#include "essl/fs_lit.sc.bin.h"

static const bgfx::EmbeddedShader s_embeddedShaders[] = {
    BGFX_EMBEDDED_SHADER(vs_flat),
    BGFX_EMBEDDED_SHADER(fs_flat),
    BGFX_EMBEDDED_SHADER(vs_lit),
    BGFX_EMBEDDED_SHADER(fs_lit),
    BGFX_EMBEDDED_SHADER_END()
};
