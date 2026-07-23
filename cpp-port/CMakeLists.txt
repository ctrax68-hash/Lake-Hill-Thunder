# Lake Hill Thunder C++ port -- Phase 0 scaffolding.
#
# Desktop Linux target only for now (see PORT_PROGRESS.md's build-sequencing
# decision): Android NDK toolchain integration is deferred to Phase 3, and
# iOS can never be built/tested in this dev container at all (no Xcode/macOS
# here) regardless of what this file does -- the structure just needs to stay
# portable to a real Mac session later, not prove it here.
cmake_minimum_required(VERSION 3.20)   # bgfx.cmake itself requires 3.20+ (C++20)
project(lake_hill_thunder_port CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Debug)
endif()

# ---- Phase 7: WebAssembly build fix, applies before any add_subdirectory ----
# bx's own CMakeLists.txt (third_party/bgfx.cmake/cmake/bx/bx.cmake) adds
# -msse4.2 whenever CMAKE_SYSTEM_PROCESSOR (or CMAKE_CXX_COMPILER_ARCHITECTURE_ID)
# looks like x86/x86_64 -- true here too, since Emscripten's own toolchain file
# doesn't override those away from the host's architecture, and overriding
# CMAKE_SYSTEM_PROCESSOR ourselves (tried first, both as a plain variable and
# a FORCEd CACHE entry) did not stop bx.cmake's `if(...MATCHES...)` from still
# adding the flag -- CMAKE_CXX_COMPILER_ARCHITECTURE_ID appears to be
# re-derived internally in a way a simple set() before add_subdirectory()
# doesn't reach. -msse4.2 defines __SSE2__/__SSE4_2__, which routes
# bx/include/bx/simd_t.h's SIMD selection down the "translate x86 SSE
# intrinsics to WASM" path, and emcc's own error message says any x86 SSE
# flag under Emscripten also requires -msimd128 -- so add that globally.
#
# Also drop the whole build's optimization to -O1 under Emscripten: -msimd128
# combined with Release's default -O3 crashes the LLVM WebAssembly
# instruction selector outright (confirmed via the crash's own stack trace,
# SelectionDAGISel::SelectCodeCommon / "WebAssembly Instruction Selection",
# not a resource limit) -- first seen compiling bx::hsvToRgb
# (bx/src/math.cpp), then again in a *different* file
# (bimg/src/image_cubemap_filter.cpp) after the bx-specific fix below, which
# confirms this is a general LLVM bug tied to optimization level, not
# something specific to one translation unit worth chasing file-by-file. A
# later -O flag wins over an earlier one for GCC/Clang, so this overrides
# every target's -O3 down to -O1 for this first working build --
# correctness over vectorized performance, revisit if/when this LLVM bug is
# fixed upstream or a newer Emscripten release avoids it.
if(EMSCRIPTEN)
  add_compile_options(-msimd128 -O1)
endif()

# ---- SDL2 ----
# Keep SDL2's own test/example targets off -- we only want the library, not
# extra build time for stuff this project never uses.
set(SDL_TEST OFF CACHE BOOL "" FORCE)
add_subdirectory(third_party/SDL2)

# ---- bgfx (via bgfx.cmake) ----
# Same idea: bgfx.cmake defaults to building its own examples/tools ON, which
# pulls in a lot of unrelated compile time for a project that only wants the
# bgfx/bx/bimg libraries themselves.
set(BGFX_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
# Phase 2 needs bgfx::shaderc (built only when BGFX_BUILD_TOOLS is ON) to
# compile src/render/shaders/*.sc at build time -- the other tool sets
# (bin2c/geometry/texture) aren't needed, so turn just BGFX_BUILD_TOOLS_SHADER
# on and leave the rest off. Skipped entirely under Emscripten: building
# shaderc there produces a wasm/Node target that can't fopen() our shader
# sources at build time anyway (see the Phase 7 shader-reuse comment further
# down), so there's no point spending the compile time on it.
set(BGFX_BUILD_TOOLS ON CACHE BOOL "" FORCE)
if(EMSCRIPTEN)
  set(BGFX_BUILD_TOOLS_SHADER OFF CACHE BOOL "" FORCE)
else()
  set(BGFX_BUILD_TOOLS_SHADER ON CACHE BOOL "" FORCE)
endif()
set(BGFX_BUILD_TOOLS_BIN2C OFF CACHE BOOL "" FORCE)
set(BGFX_BUILD_TOOLS_GEOMETRY OFF CACHE BOOL "" FORCE)
set(BGFX_BUILD_TOOLS_TEXTURE OFF CACHE BOOL "" FORCE)
set(BGFX_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(BGFX_INSTALL OFF CACHE BOOL "" FORCE)
# bgfx.cmake defaults this ON for any Linux build, but this container only has
# X11 dev headers installed (no libwayland-egl) -- our SDL2 build above is
# X11-only too, so there's nothing for a Wayland backend to attach to here.
set(BGFX_WITH_WAYLAND OFF CACHE BOOL "" FORCE)
add_subdirectory(third_party/bgfx.cmake)

# ---- Phase 7: WebAssembly build fix, part 2 ----
# bx.cmake adds -msse4.2 to `bx` as PUBLIC (see the comment near the top of
# this file for why the CMAKE_SYSTEM_PROCESSOR override attempt didn't
# prevent it -- neither that variable nor CMAKE_CXX_COMPILER_ARCHITECTURE_ID
# show up anywhere in this build's CMakeCache.txt/CMakeCXXCompiler.cmake at
# all, so whatever bx.cmake's check is actually keying off isn't interceptable
# with a simple set() before add_subdirectory()). PUBLIC means it's not just
# bx's own problem: it propagates via INTERFACE_COMPILE_OPTIONS to every
# target that links bx, including bimg -- and bimg's vendored etcpak
# (3rdparty/etcpak/Dither.cpp) has its own `#ifdef __SSE2__`-gated x86
# intrinsic includes (ia32intrin.h/ammintrin.h/fma4intrin.h) that assume a
# genuine x86 target and fail to compile at all under Emscripten's clang once
# -msse4.2 makes __SSE2__ defined there too. So: strip -msse4.2 from both of
# bx's own compile-option properties directly, rather than only compiling bx
# itself correctly and leaving every consumer broken.
if(EMSCRIPTEN)
  foreach(_prop COMPILE_OPTIONS INTERFACE_COMPILE_OPTIONS)
    get_target_property(_bx_opts bx ${_prop})
    if(_bx_opts)
      list(FILTER _bx_opts EXCLUDE REGEX "msse4\\.2")
      set_target_properties(bx PROPERTIES ${_prop} "${_bx_opts}")
    endif()
  endforeach()
  # Without -msse4.2, bx/include/bx/simd_t.h's SIMD selection falls through
  # to its `#elif defined(__wasm_simd128__)` branch instead (real WASM SIMD
  # via <wasm_simd128.h>, defined by the global -msimd128 above) -- a
  # cleaner code-gen path than translating x86 intrinsics. The global -O1
  # override above (not a per-target one here) is what actually prevents the
  # LLVM instruction-selector crash itself, since the same crash recurred in
  # an unrelated bimg file even after this -msse4.2 strip.
endif()

set(SIM_SOURCES
  src/sim/track.cpp
  src/sim/car.cpp
  src/sim/step_car.cpp
  src/sim/race.cpp
)

# ---- Phase 2 shaders ----
# vs_flat/fs_flat: flat vertex-color passthrough (no lighting yet, see
# src/render/shaders/vs_flat.sc's own comment) used for both the static
# track-ribbon mesh and the per-frame car boxes. AS_HEADERS embeds the
# compiled bytecode directly as C arrays (via bgfx/include/bgfx/embedded_shader.h's
# BGFX_EMBEDDED_SHADER macro in src/render/shaders_embedded.h) so there's no
# runtime asset-path dependency -- one less thing to get wrong when this
# eventually needs to run from an Android APK/iOS bundle instead of a normal
# filesystem.
# Absolute paths throughout: the custom command shaderc runs under doesn't
# necessarily have this source dir as its working directory, so relative
# paths here silently failed to resolve (shaderc couldn't find
# bgfx_shader.sh/shaderlib.sh or the varying def, and produced bogus
# unresolved-symbol errors instead of a clear "file not found").
set(LHT_SHADER_INCLUDE_DIRS
  ${CMAKE_CURRENT_SOURCE_DIR}/third_party/bgfx.cmake/bgfx/src
  ${CMAKE_CURRENT_SOURCE_DIR}/third_party/bgfx.cmake/bgfx/examples/common
)
if(EMSCRIPTEN)
  # bgfx.cmake's BGFX_BUILD_TOOLS_SHADER builds `shaderc` itself under
  # whatever toolchain is active -- under emcmake that means shaderc.js, a
  # wasm/Node target, not a native host tool. CMake does invoke it correctly
  # via node (emcmake's own CMAKE_CROSSCOMPILING_EMULATOR), but the resulting
  # program only has the sandboxed filesystem access Emscripten's *default*
  # (browser-oriented) build target gets, not real host-filesystem access --
  # it can't fopen() our .sc source files by absolute path at all, so
  # `bgfx_compile_shaders()` fails outright ("Unable to open file ...").
  # Building a genuinely native shaderc as a side-build under emcmake (e.g.
  # via ExternalProject_Add with a separate, non-Emscripten toolchain) is a
  # real fix but more than this first working build needs -- reuse the
  # native build's already-generated header output instead, since bgfx's
  # compiled shader bytecode (spirv/glsl/essl) is a GPU-target-specific
  # artifact, not a host-CPU-specific one: nothing about the essl bytecode
  # this port actually uses depends on whether shaderc itself was compiled to
  # run on x86 Linux or wasm32.
  set(LHT_NATIVE_GENERATED_SHADERS ${CMAKE_CURRENT_SOURCE_DIR}/build/generated_shaders)
  if(NOT EXISTS ${LHT_NATIVE_GENERATED_SHADERS}/essl/vs_flat.sc.bin.h
     OR NOT EXISTS ${LHT_NATIVE_GENERATED_SHADERS}/essl/vs_lit.sc.bin.h
     OR NOT EXISTS ${LHT_NATIVE_GENERATED_SHADERS}/essl/vs_sky.sc.bin.h
     OR NOT EXISTS ${LHT_NATIVE_GENERATED_SHADERS}/essl/vs_textured_lit.sc.bin.h)
    message(FATAL_ERROR
      "Phase 7 web build reuses the native build's already-compiled shader "
      "headers (see this file's own comment) instead of cross-compiling "
      "shaderc -- run `cmake --build build` (the native target) at least "
      "once first so ${LHT_NATIVE_GENERATED_SHADERS} exists (Phase 5a added "
      "vs_lit/fs_lit, Phase 5c added vs_sky/fs_sky, and Phase 5e added "
      "vs_textured_lit/fs_textured_lit, so a stale native build missing "
      "those will also hit this guard).")
  endif()
  set(LHT_GENERATED_SHADERS_DIR ${LHT_NATIVE_GENERATED_SHADERS})
  add_custom_target(lht_shaders) # nothing to build, just a no-op dependency target
else()
  bgfx_compile_shaders(
    TYPE VERTEX
    SHADERS ${CMAKE_CURRENT_SOURCE_DIR}/src/render/shaders/vs_flat.sc
    VARYING_DEF ${CMAKE_CURRENT_SOURCE_DIR}/src/render/shaders/varying.def.sc
    OUTPUT_DIR ${CMAKE_CURRENT_BINARY_DIR}/generated_shaders
    INCLUDE_DIRS ${LHT_SHADER_INCLUDE_DIRS}
    AS_HEADERS
    OUT_FILES_VAR LHT_VS_FLAT_OUTPUTS
  )
  bgfx_compile_shaders(
    TYPE FRAGMENT
    SHADERS ${CMAKE_CURRENT_SOURCE_DIR}/src/render/shaders/fs_flat.sc
    VARYING_DEF ${CMAKE_CURRENT_SOURCE_DIR}/src/render/shaders/varying.def.sc
    OUTPUT_DIR ${CMAKE_CURRENT_BINARY_DIR}/generated_shaders
    INCLUDE_DIRS ${LHT_SHADER_INCLUDE_DIRS}
    AS_HEADERS
    OUT_FILES_VAR LHT_FS_FLAT_OUTPUTS
  )
  # Phase 5a (PORT_PROGRESS.md): vs_lit/fs_lit -- hemisphere+directional
  # lit shading for world-space geometry (the banked track ribbon, and
  # stadium/stands starting Phase 5d). Own varying def since it adds a
  # normal attribute vs_flat/fs_flat don't have.
  bgfx_compile_shaders(
    TYPE VERTEX
    SHADERS ${CMAKE_CURRENT_SOURCE_DIR}/src/render/shaders/vs_lit.sc
    VARYING_DEF ${CMAKE_CURRENT_SOURCE_DIR}/src/render/shaders/varying_lit.def.sc
    OUTPUT_DIR ${CMAKE_CURRENT_BINARY_DIR}/generated_shaders
    INCLUDE_DIRS ${LHT_SHADER_INCLUDE_DIRS}
    AS_HEADERS
    OUT_FILES_VAR LHT_VS_LIT_OUTPUTS
  )
  bgfx_compile_shaders(
    TYPE FRAGMENT
    SHADERS ${CMAKE_CURRENT_SOURCE_DIR}/src/render/shaders/fs_lit.sc
    VARYING_DEF ${CMAKE_CURRENT_SOURCE_DIR}/src/render/shaders/varying_lit.def.sc
    OUTPUT_DIR ${CMAKE_CURRENT_BINARY_DIR}/generated_shaders
    INCLUDE_DIRS ${LHT_SHADER_INCLUDE_DIRS}
    AS_HEADERS
    OUT_FILES_VAR LHT_FS_LIT_OUTPUTS
  )
  # Phase 5c (PORT_PROGRESS.md): vs_sky/fs_sky -- the unlit fullscreen
  # textured quad for the sky background. Own varying def (Position+UV,
  # no normal/color0) since it's a plain texture sample, not lit geometry.
  bgfx_compile_shaders(
    TYPE VERTEX
    SHADERS ${CMAKE_CURRENT_SOURCE_DIR}/src/render/shaders/vs_sky.sc
    VARYING_DEF ${CMAKE_CURRENT_SOURCE_DIR}/src/render/shaders/varying_sky.def.sc
    OUTPUT_DIR ${CMAKE_CURRENT_BINARY_DIR}/generated_shaders
    INCLUDE_DIRS ${LHT_SHADER_INCLUDE_DIRS}
    AS_HEADERS
    OUT_FILES_VAR LHT_VS_SKY_OUTPUTS
  )
  bgfx_compile_shaders(
    TYPE FRAGMENT
    SHADERS ${CMAKE_CURRENT_SOURCE_DIR}/src/render/shaders/fs_sky.sc
    VARYING_DEF ${CMAKE_CURRENT_SOURCE_DIR}/src/render/shaders/varying_sky.def.sc
    OUTPUT_DIR ${CMAKE_CURRENT_BINARY_DIR}/generated_shaders
    INCLUDE_DIRS ${LHT_SHADER_INCLUDE_DIRS}
    AS_HEADERS
    OUT_FILES_VAR LHT_FS_SKY_OUTPUTS
  )
  # Phase 5e (PORT_PROGRESS.md): vs_textured_lit/fs_textured_lit -- the
  # crowd-tile-textured front stand tiers (same lighting as vs_lit/fs_lit,
  # plus a UV-sampled texture in place of the vertex color).
  bgfx_compile_shaders(
    TYPE VERTEX
    SHADERS ${CMAKE_CURRENT_SOURCE_DIR}/src/render/shaders/vs_textured_lit.sc
    VARYING_DEF ${CMAKE_CURRENT_SOURCE_DIR}/src/render/shaders/varying_textured_lit.def.sc
    OUTPUT_DIR ${CMAKE_CURRENT_BINARY_DIR}/generated_shaders
    INCLUDE_DIRS ${LHT_SHADER_INCLUDE_DIRS}
    AS_HEADERS
    OUT_FILES_VAR LHT_VS_TEXTURED_LIT_OUTPUTS
  )
  bgfx_compile_shaders(
    TYPE FRAGMENT
    SHADERS ${CMAKE_CURRENT_SOURCE_DIR}/src/render/shaders/fs_textured_lit.sc
    VARYING_DEF ${CMAKE_CURRENT_SOURCE_DIR}/src/render/shaders/varying_textured_lit.def.sc
    OUTPUT_DIR ${CMAKE_CURRENT_BINARY_DIR}/generated_shaders
    INCLUDE_DIRS ${LHT_SHADER_INCLUDE_DIRS}
    AS_HEADERS
    OUT_FILES_VAR LHT_FS_TEXTURED_LIT_OUTPUTS
  )
  add_custom_target(lht_shaders DEPENDS ${LHT_VS_FLAT_OUTPUTS} ${LHT_FS_FLAT_OUTPUTS}
    ${LHT_VS_LIT_OUTPUTS} ${LHT_FS_LIT_OUTPUTS} ${LHT_VS_SKY_OUTPUTS} ${LHT_FS_SKY_OUTPUTS}
    ${LHT_VS_TEXTURED_LIT_OUTPUTS} ${LHT_FS_TEXTURED_LIT_OUTPUTS})
  set(LHT_GENERATED_SHADERS_DIR ${CMAKE_CURRENT_BINARY_DIR}/generated_shaders)
endif()

# ---- Main binary ----
# Phase 0: opens a window, initializes bgfx. Phase 1: the sim (src/sim) is a
# real, verified port of the JS physics/AI core. Phase 2: src/render draws a
# flat-shaded track ribbon + car boxes from a static top-down camera, driven
# by the real sim loop -- the first point this port is actually playable.
add_executable(lht_port
  src/platform/main.cpp
  src/platform/tilt_input.cpp
  src/render/atlas_texture.cpp
  src/render/fmt_time.cpp
  src/render/gap_time.cpp
  src/render/gear_rpm.cpp
  src/render/hud.cpp
  src/render/leaderboard.cpp
  src/render/livery.cpp
  src/render/minimap.cpp
  src/render/renderer.cpp
  src/render/sky_texture.cpp
  src/render/stadium_mesh.cpp
  src/render/status_bars.cpp
  src/render/track_surface.cpp
  src/render/ui_draw.cpp
  src/ui/menu.cpp
  src/ui/results.cpp
  src/ui/touch_controls.cpp
  ${SIM_SOURCES}
)
add_dependencies(lht_port lht_shaders)
target_link_libraries(lht_port PRIVATE SDL2::SDL2 SDL2::SDL2main bgfx)
target_include_directories(lht_port PRIVATE
  third_party/bgfx.cmake/bgfx/include
  third_party/bgfx.cmake/bx/include
  ${LHT_GENERATED_SHADERS_DIR}
)

# ---- Phase 7: WebAssembly/browser build (PORT_PROGRESS.md) ----
# Additive on top of the native build above -- both SDL2's and bgfx.cmake's
# own CMakeLists.txt already branch on CMAKE_SYSTEM_NAME STREQUAL Emscripten
# internally (SDL2's video/audio/joystick/emscripten backends; bgfx.cmake's
# own -sMAX_WEBGL_VERSION=2 link option), so this project's file only needs
# to add what's specific to lht_port itself: memory growth (the sim/render
# heap footprint isn't knowable at compile time the way a fixed WASM memory
# size would require), FULL_ES3 (WebGL2, matching the already-embedded essl
# shader profile -- see shaders_embedded.h, no shader/profile changes needed
# here), and the custom minimal shell (web/shell.html) instead of
# Emscripten's own default one (which bundles unrelated progress-bar/status
# UI chrome).
if(EMSCRIPTEN)
  # emcc only honors --shell-file (and produces the wrapping HTML page at
  # all) when the *output filename itself* ends in .html -- otherwise it
  # silently ignores the flag and just emits lht_port.js/.wasm (confirmed:
  # that's exactly what happened before adding this SUFFIX override).
  set_target_properties(lht_port PROPERTIES SUFFIX ".html")
  target_link_options(lht_port PRIVATE
    "-sALLOW_MEMORY_GROWTH=1"
    "-sFULL_ES3=1"
    "--shell-file=${CMAKE_CURRENT_SOURCE_DIR}/web/shell.html"
  )

  # ---- Phase 7b: PWA installability wrapper (PORT_PROGRESS.md) ----
  # emcc's own output mechanism only ever produces lht_port.html/.js/.wasm --
  # there's no existing static-asset-copy step in this file to piggyback on,
  # so this POST_BUILD command stages manifest.json/sw.js/icons/ into
  # build-web/ alongside those every time lht_port is built. POST_BUILD
  # (not a configure-time file(COPY)) so editing these files doesn't need a
  # manual reconfigure to take effect.
  add_custom_command(TARGET lht_port POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
            ${CMAKE_CURRENT_SOURCE_DIR}/web/manifest.json
            ${CMAKE_CURRENT_SOURCE_DIR}/web/sw.js
            $<TARGET_FILE_DIR:lht_port>
    COMMAND ${CMAKE_COMMAND} -E copy_directory
            ${CMAKE_CURRENT_SOURCE_DIR}/web/icons
            $<TARGET_FILE_DIR:lht_port>/icons
    COMMENT "Staging PWA manifest/service-worker/icons alongside lht_port.html"
  )
endif()

# ---- Phase 1 sim tests (headless, no SDL2/bgfx dependency) ----
# These verify src/sim's port of the JS physics/AI core against captured JS
# ground truth. Deliberately not linked against SDL2/bgfx: the physics core
# must be provably correct standalone before any rendering touches it.
# Skipped entirely under Emscripten: emcc's ".js"+".wasm" output isn't a
# native executable CTest can invoke directly (no ELF/execute bit), and
# wiring a CMAKE_CROSSCOMPILING_EMULATOR (e.g. node) to make that work is a
# separate side-quest this phase doesn't need -- the native build/'s ctest
# (7/7) remains the sole verification of the sim core; this web build is
# additive on top of an already-verified physics core, not a re-verification
# of it.
if(NOT EMSCRIPTEN)
enable_testing()

add_executable(rng_test tests/rng_test.cpp)
add_test(NAME rng_test COMMAND rng_test)

add_executable(track_test tests/track_test.cpp src/sim/track.cpp)
add_test(NAME track_test COMMAND track_test)

# Phase 5a (PORT_PROGRESS.md): track_surface_test exercises pos3()/surfH()/
# surfaceUp()'s pure geometry math -- zero bgfx dependency, same category as
# track_test itself (links only track.cpp, plus the new track_surface.cpp).
add_executable(track_surface_test tests/track_surface_test.cpp src/sim/track.cpp src/render/track_surface.cpp)
add_test(NAME track_surface_test COMMAND track_surface_test)

# Phase 5b (PORT_PROGRESS.md): env_presets.h/tracks_data.h are both
# header-only (TRACKS is an inline const array of aggregates, no Track
# objects constructed), so this test needs no .cpp sources at all.
add_executable(env_presets_test tests/env_presets_test.cpp)
add_test(NAME env_presets_test COMMAND env_presets_test)

# Phase 5c (PORT_PROGRESS.md): sky_texture_test exercises buildSkyPixels()'s
# pure pixel math, bgfx-free.
add_executable(sky_texture_test tests/sky_texture_test.cpp src/render/sky_texture.cpp)
add_test(NAME sky_texture_test COMMAND sky_texture_test)

# Phase 5d (PORT_PROGRESS.md): stadium_mesh_test exercises buildStandMesh()/
# buildPitRoadMesh()/buildOuterWallMesh()'s pure geometry, bgfx-free (links
# track.cpp + track_surface.cpp + car.cpp for pitStallS()/FIELD).
add_executable(stadium_mesh_test tests/stadium_mesh_test.cpp src/sim/track.cpp src/sim/car.cpp
  src/render/track_surface.cpp src/render/stadium_mesh.cpp)
add_test(NAME stadium_mesh_test COMMAND stadium_mesh_test)

# Phase 5e (PORT_PROGRESS.md): atlas_texture_test exercises buildAtlasPixels()'s
# pure pixel math, bgfx-free.
add_executable(atlas_texture_test tests/atlas_texture_test.cpp src/render/atlas_texture.cpp)
add_test(NAME atlas_texture_test COMMAND atlas_texture_test)

# Phase 5f (PORT_PROGRESS.md): livery_test exercises buildLiveryPixels()'s
# pure pixel math, bgfx-free (links track.cpp/car.cpp for ROSTER data).
add_executable(livery_test tests/livery_test.cpp src/sim/track.cpp src/sim/car.cpp src/render/livery.cpp)
add_test(NAME livery_test COMMAND livery_test)

add_executable(car_test tests/car_test.cpp src/sim/track.cpp src/sim/car.cpp)
add_test(NAME car_test COMMAND car_test)

add_executable(speed_model_test tests/speed_model_test.cpp src/sim/track.cpp src/sim/car.cpp)
add_test(NAME speed_model_test COMMAND speed_model_test)

# touch_controls_test only needs SDL2's headers (SDL_Rect) for the region
# math -- linked against SDL2::SDL2 for its include path the same way
# lht_port is, not because this test touches any actual SDL subsystem.
add_executable(touch_controls_test tests/touch_controls_test.cpp src/ui/touch_controls.cpp)
target_link_libraries(touch_controls_test PRIVATE SDL2::SDL2)
add_test(NAME touch_controls_test COMMAND touch_controls_test)

# Phase 4b (PORT_PROGRESS.md): menu_test exercises menu.cpp's pure region/
# value-math functions (touch_controls_test's own rationale applies here
# too -- see menu_test.cpp's header comment). Unlike touch_controls.cpp,
# menu.cpp calls bgfx::dbgTextPrintf() in drawMenu() (not exercised by this
# test, but still a symbol the linker must resolve), so this target needs
# bgfx's include paths and library the same way lht_port itself does.
add_executable(menu_test tests/menu_test.cpp src/ui/menu.cpp)
target_link_libraries(menu_test PRIVATE SDL2::SDL2 bgfx)
target_include_directories(menu_test PRIVATE
  third_party/bgfx.cmake/bgfx/include
  third_party/bgfx.cmake/bx/include
)
add_test(NAME menu_test COMMAND menu_test)

# Phase 4c (PORT_PROGRESS.md): fmt_time_test exercises fmtLapTime()'s pure
# formatting logic -- zero bgfx/SDL2 dependency at all, unlike menu_test.
add_executable(fmt_time_test tests/fmt_time_test.cpp src/render/fmt_time.cpp)
add_test(NAME fmt_time_test COMMAND fmt_time_test)

# Phase 4d (PORT_PROGRESS.md): gear_rpm_test exercises gearRpm()'s pure
# breakpoint math -- same zero-dependency category as fmt_time_test.
add_executable(gear_rpm_test tests/gear_rpm_test.cpp src/render/gear_rpm.cpp)
add_test(NAME gear_rpm_test COMMAND gear_rpm_test)

# Phase 4e (PORT_PROGRESS.md): ui_draw_test exercises ui_draw.cpp's pure 2D
# geometry helpers -- same zero-dependency category as fmt_time_test/
# gear_rpm_test (ui_draw.{h,cpp} itself has no bgfx dependency at all).
add_executable(ui_draw_test tests/ui_draw_test.cpp src/render/ui_draw.cpp)
add_test(NAME ui_draw_test COMMAND ui_draw_test)

# Phase 4f (PORT_PROGRESS.md): minimap_test exercises minimap.cpp's wedge-
# direction/world-transform/trouble-ring-predicate logic -- minimap.{h,cpp}
# has no bgfx dependency either (unlike status_bars.cpp, which prints
# dbgText labels), so this is the same zero-dependency category as
# ui_draw_test.
add_executable(minimap_test tests/minimap_test.cpp src/render/minimap.cpp src/render/ui_draw.cpp)
add_test(NAME minimap_test COMMAND minimap_test)

# Phase 4g (PORT_PROGRESS.md): leaderboard_test exercises
# buildLeaderboardRows()'s pure row-building/tag-precedence/gap-
# interpolation logic. Needs bgfx linked (like menu_test) because
# leaderboard.cpp's drawLeaderboard() calls bgfx::dbgTextPrintf(), even
# though this test never calls that function.
add_executable(leaderboard_test tests/leaderboard_test.cpp src/render/leaderboard.cpp
  src/render/gap_time.cpp src/render/ui_draw.cpp)
target_link_libraries(leaderboard_test PRIVATE bgfx)
target_include_directories(leaderboard_test PRIVATE
  third_party/bgfx.cmake/bgfx/include
  third_party/bgfx.cmake/bx/include
)
add_test(NAME leaderboard_test COMMAND leaderboard_test)

# Phase 4h (PORT_PROGRESS.md): results_test exercises computeResultsRegions()'s
# row-position math and buildResultsOrder()'s 3-bucket ordering (finished ++
# still-racing ++ DNF). Needs SDL2 (SDL_Rect) + bgfx linked (like menu_test)
# because results.cpp's drawResults() calls bgfx::dbgTextPrintf(), even
# though this test never calls that function.
add_executable(results_test tests/results_test.cpp src/ui/results.cpp
  src/render/fmt_time.cpp src/render/ui_draw.cpp)
target_link_libraries(results_test PRIVATE SDL2::SDL2 bgfx)
target_include_directories(results_test PRIVATE
  third_party/bgfx.cmake/bgfx/include
  third_party/bgfx.cmake/bx/include
)
add_test(NAME results_test COMMAND results_test)

add_executable(determinism_test tests/determinism_test.cpp tests/determinism/trace.cpp)
add_test(NAME determinism_test COMMAND determinism_test)
# Reads tests/fixtures/trace_synthetic.txt by a repo-relative path.
set_tests_properties(determinism_test PROPERTIES WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR})

# race_sim_test: unit tests for gridStart()/stepPace()/updateAero()/collide()
# that don't need a JS-generated fixture (small, hand-checkable scenarios).
add_executable(race_sim_test tests/race_sim_test.cpp ${SIM_SOURCES})
add_test(NAME race_sim_test COMMAND race_sim_test)

# determinism_check: NOT added via add_test (see the file's own header
# comment) -- it needs an external JS trace file generated by
# tests/determinism/dump_js_trace.js, which requires Playwright/Node and
# isn't committed to the repo. Build and run it manually to verify the
# ported sim against real JS ground truth.
add_executable(determinism_check
  tests/determinism_check.cpp
  tests/determinism/trace.cpp
  ${SIM_SOURCES}
)
endif() # NOT EMSCRIPTEN
