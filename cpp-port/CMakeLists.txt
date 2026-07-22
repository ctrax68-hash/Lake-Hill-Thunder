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
set(BGFX_BUILD_TOOLS OFF CACHE BOOL "" FORCE)
set(BGFX_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(BGFX_INSTALL OFF CACHE BOOL "" FORCE)
# bgfx.cmake defaults this ON for any Linux build, but this container only has
# X11 dev headers installed (no libwayland-egl) -- our SDL2 build above is
# X11-only too, so there's nothing for a Wayland backend to attach to here.
set(BGFX_WITH_WAYLAND OFF CACHE BOOL "" FORCE)
add_subdirectory(third_party/bgfx.cmake)

# ---- Phase 0 scaffolding binary ----
# Just opens a window, initializes bgfx, clears the screen every frame. The
# real sim (src/sim) and renderer (src/render) are empty until Phase 1/2.
add_executable(lht_port
  src/platform/main.cpp
)
target_link_libraries(lht_port PRIVATE SDL2::SDL2 SDL2::SDL2main bgfx)
target_include_directories(lht_port PRIVATE
  third_party/bgfx.cmake/bgfx/include
  third_party/bgfx.cmake/bx/include
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
