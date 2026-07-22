# Lake Hill Thunder — C++/SDL2/bgfx Mobile Port: Progress

This file is the source of truth for this port across sessions. **Read this file
first, every run.** Each run should do one phase (or one clearly-scoped sub-task
within a phase), verify it, update this file, and stop — don't try to do the whole
port in one pass.

Source of truth for gameplay/rendering behavior: `../index.html` (the original,
still-maintained single-file JS/Three.js game — do not modify it as part of this
port; it's the reference the C++ port must match).

## Ground rules (do not skip)

1. Read this file first.
2. One phase (or one clearly-scoped sub-task) per run.
3. **Never move to rendering/UI work until the physics core is verified deterministic
   against the JS original.** The physics/AI sim is the actual IP; visual fidelity is
   an explicit stretch goal, not a blocker.
4. Update this file at the end of every run: what you did, how you verified it,
   what's left, and anything the next run needs to know. Write it for a colleague
   with zero memory of this session, because that's exactly what the next run is.
5. Commit at logical checkpoints.
6. Log ambiguous JS behavior/magic numbers under "Open questions" below rather than
   guessing silently.
7. **Port faithfully first.** Don't restructure or "improve" the simulation logic
   while porting — match behavior exactly, including quirks and tuning constants.
   Preserve the JS source's own inline comments explaining *why* a constant is what
   it is (several document bugs earlier tuning attempts caused) — a future session
   "optimizing" a constant without that context could reintroduce a fixed bug.

## Decisions locked in (do not re-litigate without a real reason)

- **SDL2**, not SDL3 — mature, proven mobile deployment track record.
- **Location**: this port lives at `/cpp-port` inside the `lake-hill-thunder` repo,
  alongside `index.html`, not as a separate repository.
- **bgfx integration**: via `bkaradzic/bgfx.cmake` (a community CMake wrapper around
  bgfx+bx+bimg, which normally build via their own GENie-based toolchain) rather than
  hand-rolling a GENie build step — lets this project pull bgfx in cleanly as a CMake
  subdirectory/submodule.
- **Build sequencing**: desktop Linux build first (Phases 0-2) for fast iteration
  without any mobile toolchain. Android NDK install is deferred until Phase 3
  actually needs it (not installed yet as of this writing). **iOS build/run can never
  happen in this specific dev container** (no Xcode/macOS here — a hard environment
  fact, not a scoping choice) — the code/CMake structure should stay portable to iOS,
  but actually building and testing it always requires a real Mac+Xcode session later.

## Environment facts (confirmed this session, re-check if things seem to have changed)

- `cmake` 3.28.3, `gcc`/`g++`, `clang`/`clang++`, `git` 2.43.0 all present in this
  container. ~30GB disk free at last check.
- No `ANDROID_HOME`/`ANDROID_NDK_HOME` set, NDK not installed. No Xcode (Linux).
- **Networking quirk**: a bare `curl https://github.com/...` returns `403` through
  this environment's outbound proxy, but `git clone`/`git ls-remote` against GitHub
  **works fine** — confirmed by shallow-cloning both `libsdl-org/SDL` and
  `bkaradzic/bgfx` successfully in throwaway test directories. Don't conclude GitHub
  is unreachable from a `curl` failure alone — test with `git` operations instead.
- No display server confirmed yet in this container as of Phase 0 start — check for
  `Xvfb`/a usable `DISPLAY` before assuming an on-screen SDL2 window can actually be
  shown; if none exists, verify via a headless/offscreen bgfx context instead and
  note that limitation here rather than blocking on it.

## Phase checklist

### Phase 0 — Scaffolding — DONE (Session 1)
- [x] Repo layout created: `/cpp-port/{src/{sim,render,ui,platform},tests,third_party}`
- [x] `PORT_PROGRESS.md` written (this file)
- [x] Vendor SDL2 (pinned release tag) and bgfx.cmake as git submodules in `third_party/`
- [x] Top-level `CMakeLists.txt`, desktop-Linux target only for now
- [x] Minimal `src/platform/main.cpp`: opens an SDL2 window, initializes bgfx, clears
      the screen every frame
- [x] Verify: `cmake -B build && cmake --build build` succeeds; binary runs and
      produces at least a few non-crashing cleared frames (on-screen if a display is
      available, headless/offscreen bgfx context otherwise — record which)
- [x] Commit + push

**Verification results (Session 1):**
- Required installing Linux GL/X11 dev headers not present by default in this
  container: `libgl1-mesa-dev libglu1-mesa-dev libxrandr-dev libxinerama-dev
  libxcursor-dev libxi-dev libxext-dev` (via `apt-get update && apt-get install`).
  Root, so no sudo needed. This is a one-time container setup step a fresh session
  will need to redo if these aren't already present — check `pkg-config --exists gl`
  first.
- **`Xvfb` is present and works.** No real `DISPLAY` is set by default, but
  `xvfb-run -a --server-args="-screen 0 1280x720x24" ./build/lht_port` gives the
  SDL2/bgfx GL backend a real (virtual) X11 display to attach to — this is NOT
  bgfx's headless/Noop renderer, it's the actual GL renderer against a virtual
  framebuffer. Use this to verify future rendering work too.
- bgfx.cmake defaults `BGFX_WITH_WAYLAND` to `ON` on any Linux `CMAKE_SYSTEM_NAME`,
  which tries to link `-lwayland-egl` — not installed/needed here since this SDL2
  build only has X11 enabled. Set `BGFX_WITH_WAYLAND OFF` explicitly in the
  top-level `CMakeLists.txt` (already done) rather than installing Wayland dev
  libs neither SDL2 nor this project's build actually uses.
- **Important bgfx API gotcha, logged so it isn't rediscovered:** `bgfx::setPlatformData()`
  (the standalone free function) only writes a global that's consulted by
  `bgfx::reset()` for *changing* platform data after init. The window handle used
  by `bgfx::init()` itself must be set on `bgfx::Init::platformData` directly
  (`init.platformData = pd;` before calling `bgfx::init(init)`), not via the
  separate `setPlatformData()` call — using only the latter silently falls back to
  a headless/Noop device with a "window handle... not set" warning instead of
  erroring loudly. `main.cpp` does this correctly now.
- SDL2 native window handle on X11: `SDL_SysWMinfo` → `info.x11.display` (→
  `bgfx::PlatformData::ndt`) and `info.x11.window` (→ `nwh`, cast through
  `uintptr_t`). This SDL2 build has Wayland disabled, so only the `SDL_SYSWM_X11`
  case is implemented in `main.cpp`; add a Wayland branch back if that build
  option ever changes.
- Full from-source build (SDL2 + bx/bimg/bgfx) takes a few minutes; expect it to
  exceed a 120s foreground command timeout in an agent session — backgrounding it
  and waiting for the completion notification works fine.
- Actual run output (`xvfb-run` + `LD_LIBRARY_PATH=build/third_party/SDL2` so the
  SDL2 shared lib resolves): bgfx logged a real GL backbuffer swap chain init
  (1280x720, BGRA8/D24S8, V-sync on), completed init, then `main.cpp` rendered its
  bounded 180-frame loop and exited cleanly — `Rendered 180 frames without
  crashing.` / `EXIT CODE: 0`.
- `cpp-port/.gitignore` added (`build/`) so the CMake build directory doesn't get
  committed.

### Phase 1 — Physics/AI core (headless, no graphics) — NOT STARTED
- [ ] Port `mulberry32` (JS: `index.html:229`) bit-for-bit; unit test against logged
      JS output for seeds `12345` (gameplay `rng`), `999` (`rngR`, runtime events —
      qual spread, spins), `777` (`rng2`, scenery-only, safe to diverge from without
      affecting gameplay), `4242` (`rngP`, particle effects only)
- [ ] Port `buildTrack()`/`pointAt()`/`bankAt()`/`project()` (JS: `index.html:284`+)
      and the `TRACKS` data table (4 tracks: Thunder Oval, Milltown Bullring, Cedar
      Valley, Big Sable Speedway) — each has its own `RL`/`RR`/`bankL`/`bankR`/`D`/
      `sBank`/`ramp` plus a `stadium` sub-object (visual dressing only, lower
      priority than the physics fields)
- [ ] Port `CAR` constants (JS: `index.html:398`), `CAR_PALETTE`, `ROSTER` (19 AI
      drivers + player = 20-car field, `FIELD = ROSTER.length + 1`), `makeCar()`
- [ ] Port `cornerCap()` (JS: `index.html:404`) and the rest of the speed/grip model
- [ ] Port `stepCar()` (JS: `index.html:686`) — the largest single function by far.
      Split into sub-phases by branch: player input, AI groove/pass-side logic
      (`grooveBias`, `passSide`/`passT`), pit-road state machine (`c.pit` 0-4, see
      the JS source's own extensive comments on why state 4/`dtPending` is currently
      dead code — preserve that context), caution/pace-car following, spin/wreck
      (`spinT`/`spinDir`/`spinCd`), damage/blowout. Port and verify one branch at a
      time, not the whole function at once.
- [ ] Port pace car (`stepPace()`, JS: `index.html:599`), grid start (`gridStart()`),
      the race state object `S` (JS: `index.html:506`), green-white-checkered logic
      (`S.gwcState` machine: `'none'→'watch'→'clean1'→'white'`, JS: `index.html:4480`+)
- [ ] **Determinism harness**: instrument the JS (headless Node/Playwright — this
      repo already has `tools/playtest.js` and `tools/wreck_stats.js` as a working
      example of exactly this kind of headless harness, reuse its patterns) to dump
      per-car `{x,y,hdg,v,lap,s,lat}` every tick for a fixed scenario/seed. Run the
      same scenario through the C++ port and diff. Any divergence is a bug to find,
      not a rounding error to shrug off — match float operation *order*, not just
      formulas, since that's what determinism actually depends on.

### Phase 2 — Minimal renderer (desktop-first) — NOT STARTED
- [ ] Static camera, flat-shaded track ribbon + car boxes, no postprocessing
- [ ] Keyboard input standing in for touch (gas/brake/steer)
- [ ] Chase camera matching the JS original's behavior (JS: `updateCamera()`)

### Phase 3 — Mobile input — NOT STARTED
- [ ] SDL2 touch regions matching the JS original's button layout (`bL`/`bR` steer,
      `bB` brake, `bG` gas, `bP` pit)
- [ ] `SDL_Sensor` accelerometer/gyro read for tilt-steer, matching `S.tiltG`
- [ ] Portrait-lock rotate prompt (landscape-only)
- [ ] **Install Android NDK here** (deferred from Phase 0 per the sequencing decision)

### Phase 4 — UI overlay — NOT STARTED
- [ ] Menu screen (track/laps/qualifying/sound/tilt toggles, volume slider, start button)
- [ ] HUD (lap counter, position, timing — note the JS side has grown a lot more HUD
      surface recently: gear+RPM readout, live leaderboard gaps, minimap with a
      player wedge + trouble-pulse rings, segmented TIRE/FUEL/CAR bars — check
      `index.html`'s current state for the full HUD surface before assuming the
      original master-prompt's brief "lap counter, position, timing" is complete)
- [ ] Results screen

### Phase 5 — Full render fidelity — NOT STARTED
- [ ] Procedural stadium/stands/crowd-tile/livery-painting mesh generation (JS builds
      these via Three.js geometry helpers + canvas-drawn textures; needs raw vertex
      buffer construction in bgfx)
- [ ] Sky/environment per track preset (JS: `ENV_PRESETS`)
- [ ] Hand-rolled bloom (downsample→blur→combine, replacing `UnrealBloomPass`) +
      tonemap (replacing `OutputPass`)

### Phase 6 — Polish & platform packaging — NOT STARTED
- [ ] Audio port (JS has a real Web Audio graph: multi-voice opponent engines, a
      4-bus mixer, crowd/spotter/impact one-shots — check `index.html`'s audio
      section for current scope, it has grown since the original master prompt was written)
- [ ] Android build (Gradle/NDK) and iOS build (Xcode project) — iOS always needs a
      real Mac session, per the environment facts above
- [ ] Device performance pass on real mid-tier hardware (20-car field + postprocessing)

## Open questions

*(Seed this section as Phase 1 finds JS behavior that's ambiguous or looks like an
artifact of the original's own constraints rather than intentional design. Nothing
logged yet — Phase 0 doesn't touch simulation logic.)*

## Definition of done (unchanged from the original spec)

- Runs on both Android and iOS
- Physics/AI parity verified against the JS original via the determinism harness for
  at least: a full green-flag run, a caution/restart sequence, a pit stop
- Touch controls and tilt-steer functional
- Visual parity is a stretch goal, not a blocker — gameplay parity is the bar

## Session log

- **Session 1**: Phase 0 complete. Created repo scaffolding, wrote this file,
  vendored SDL2 (`release-2.32.10`) and bgfx.cmake (with bgfx/bx/bimg submodules)
  under `third_party/`, wrote `CMakeLists.txt` (desktop-Linux-only, bgfx
  examples/tools/tests/install and Wayland all disabled) and
  `src/platform/main.cpp` (SDL2 window + bgfx GL init via X11 native handle +
  bounded 180-frame clear loop). Installed missing Linux GL/X11 dev headers.
  Verified full build succeeds and the binary runs cleanly under `xvfb-run`
  (real GL backbuffer, not headless/Noop) — see the Phase 0 checklist above for
  the specific gotchas hit (bgfx `Init::platformData` vs. `setPlatformData()`,
  `BGFX_WITH_WAYLAND` default). Committed and pushed.
  **Next run (Phase 1): do NOT touch rendering.** Start the physics/AI core —
  `mulberry32` (`index.html:229`), `buildTrack()`/`pointAt()`/`bankAt()`/`project()`
  (`index.html:284`+) and the 4-track `TRACKS` table, `CAR`/`CAR_PALETTE`/`ROSTER`/
  `makeCar()` (`index.html:398`), `cornerCap()` (`index.html:404`), then the big one:
  `stepCar()` (`index.html:686`) branch-by-branch. Build the determinism harness
  early (reuse `tools/playtest.js` patterns from the main JS repo) so every ported
  branch is checked against real JS output as you go, not at the end. Seeds:
  `rng`=12345 (gameplay, must match exactly), `rngR`=999 (runtime events, must
  match exactly), `rng2`=777 (scenery, safe to diverge), `rngP`=4242 (particles,
  safe to diverge).
