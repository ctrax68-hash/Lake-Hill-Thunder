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
# on and leave the rest off.
set(BGFX_BUILD_TOOLS ON CACHE BOOL "" FORCE)
set(BGFX_BUILD_TOOLS_SHADER ON CACHE BOOL "" FORCE)
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
add_custom_target(lht_shaders DEPENDS ${LHT_VS_FLAT_OUTPUTS} ${LHT_FS_FLAT_OUTPUTS})

# ---- Main binary ----
# Phase 0: opens a window, initializes bgfx. Phase 1: the sim (src/sim) is a
# real, verified port of the JS physics/AI core. Phase 2: src/render draws a
# flat-shaded track ribbon + car boxes from a static top-down camera, driven
# by the real sim loop -- the first point this port is actually playable.
add_executable(lht_port
  src/platform/main.cpp
  src/render/renderer.cpp
  ${SIM_SOURCES}
)
add_dependencies(lht_port lht_shaders)
target_link_libraries(lht_port PRIVATE SDL2::SDL2 SDL2::SDL2main bgfx)
target_include_directories(lht_port PRIVATE
  third_party/bgfx.cmake/bgfx/include
  third_party/bgfx.cmake/bx/include
  ${CMAKE_CURRENT_BINARY_DIR}/generated_shaders
)

# ---- Phase 1 sim tests (headless, no SDL2/bgfx dependency) ----
# These verify src/sim's port of the JS physics/AI core against captured JS
# ground truth. Deliberately not linked against SDL2/bgfx: the physics core
# must be provably correct standalone before any rendering touches it.
enable_testing()

add_executable(rng_test tests/rng_test.cpp)
add_test(NAME rng_test COMMAND rng_test)

add_executable(track_test tests/track_test.cpp src/sim/track.cpp)
add_test(NAME track_test COMMAND track_test)

add_executable(car_test tests/car_test.cpp src/sim/track.cpp src/sim/car.cpp)
add_test(NAME car_test COMMAND car_test)

add_executable(speed_model_test tests/speed_model_test.cpp src/sim/track.cpp src/sim/car.cpp)
add_test(NAME speed_model_test COMMAND speed_model_test)

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
