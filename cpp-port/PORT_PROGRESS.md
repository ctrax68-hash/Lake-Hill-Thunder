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

### Phase 1 — Physics/AI core (headless, no graphics) — IN PROGRESS (Session 2)
- [x] Port `mulberry32` (JS: `index.html:229`) bit-for-bit; unit test against logged
      JS output for seeds `12345` (gameplay `rng`), `999` (`rngR`, runtime events —
      qual spread, spins), `777` (`rng2`, scenery-only, safe to diverge from without
      affecting gameplay), `4242` (`rngP`, particle effects only). **Done**:
      `src/sim/rng.h` (`Mulberry32`), verified in `tests/rng_test.cpp` against all
      4 seeds. Only `12345`/`999` are actually needed for gameplay determinism;
      `777`/`4242` were verified anyway since it cost nothing and rules them out
      as a future source of confusion.
- [x] Port `buildTrack()`/`pointAt()`/`bankAt()`/`project()` (JS: `index.html:284`+)
      and the `TRACKS` data table (4 tracks: Thunder Oval, Milltown Bullring, Cedar
      Valley, Big Sable Speedway) — each has its own `RL`/`RR`/`bankL`/`bankR`/`D`/
      `sBank`/`ramp` plus a `stadium` sub-object (visual dressing only, lower
      priority than the physics fields). **Done**: `src/sim/track.{h,cpp}` +
      `src/sim/tracks_data.h` (physics fields only -- `theme`/`stadium` deliberately
      NOT ported, that's Phase 5's job). Verified in `tests/track_test.cpp` against
      JS ground truth for tracks 0 (asymmetric) and 3 (symmetric), covering segment
      boundaries, mid-segment, and wrapped/negative `s` inputs.
- [x] Port `CAR` constants (JS: `index.html:398`), `CAR_PALETTE`, `ROSTER` (19 AI
      drivers + player = 20-car field, `FIELD = ROSTER.length + 1`), `makeCar()`.
      **Done**: `src/sim/car.{h,cpp}`. `progHist`/`replayHist`/`histTick` (added to
      the JS `Car` object much later, for HUD telemetry/replay-camera only, never
      read by `stepCar()`'s physics/AI logic) are intentionally NOT ported --
      revisit only if a future phase needs them for HUD/replay parity. Verified in
      `tests/car_test.cpp`: all 20 cars' `skill`/`aggr`/`grooveBias` match JS
      exactly for the default grid order, confirming the rng() call-order/count
      (3 calls per AI car, 0 for the player) stays in sync with the shared
      seed-12345 stream.
- [x] Port `cornerCap()` (JS: `index.html:404`) and the rest of the speed/grip model.
      **Done**: `cornerCap()`/`cornerSpeed()`/`targetSpeed()` all in `src/sim/car.cpp`,
      taking the active `Track` as an explicit parameter instead of closing over a
      JS-style single global (the only deliberate structural adaptation so far --
      math/iteration order unchanged). Verified in `tests/speed_model_test.cpp`.
- [ ] Port `stepCar()` (JS: `index.html:686`) — the largest single function by far.
      Split into sub-phases by branch: player input, AI groove/pass-side logic
      (`grooveBias`, `passSide`/`passT`), pit-road state machine (`c.pit` 0-4, see
      the JS source's own extensive comments on why state 4/`dtPending` is currently
      dead code — preserve that context), caution/pace-car following, spin/wreck
      (`spinT`/`spinDir`/`spinCd`), damage/blowout. Port and verify one branch at a
      time, not the whole function at once.
      **Progress (Session 3): the `S.mode==='pace'` branch (index.html:837-858,
      formation-following) is done, plus the shared physics tail every branch
      needs (index.html:977-1109: steering blend, track projection, tire
      grip/wear, fuel burn, drag/engine force, speed/yaw integration, wall
      clamp, lap counting) -- see `src/sim/step_car.cpp`. All other branches
      (victory, out/done, spin, pit, yellow-caution, player-input, AI-race)
      throw `std::logic_error` rather than silently running wrong physics if
      reached -- none of them CAN be reached yet because the only scenario
      exercised so far (the pace phase of a fresh green-flag start) never
      triggers them (see below). `Car` gained `blown`/`dmgCd`/`spinRollCd`
      fields the tail needs (JS adds these dynamically outside `makeCar()`,
      not part of its literal object, but they're physics-relevant so they're
      real zero-defaulted `Car` fields here, not skipped like the HUD-only
      dynamic fields). The pit-entry arming block (index.html:692-701) is
      also not ported yet, but is provably a no-op during the pace phase
      (its guard needs `pitReq`/`dtPending`, both unreachable before
      `S.mode==='race'`) so this is a real, not just assumed, inert omission.
- [x] Port pace car (`stepPace()`, JS: `index.html:599`), grid start (`gridStart()`),
      updateAero() and collide() are also done this session even though the
      checklist line doesn't mention them -- see below for why they had to
      come along with this item. The race state object `S` (JS:
      `index.html:506`) has a physics-relevant subset ported as `RaceState`
      (`src/sim/race_state.h`); UI/audio/render-only fields (sound, volume,
      tilt, tiltG, camMode, shakeT) are skipped, same rationale as Car's
      progHist/replayHist. Green-white-checkered logic (`S.gwcState` machine:
      `'none'→'watch'→'clean1'→'white'`, JS: `index.html:4480`+) is
      NOT ported yet -- it can't fire before a race reaches its final laps,
      well beyond what's been verified so far.
      **Scope correction found while building the Session 2 determinism harness
      (confirmed true while doing this work in Session 3):** `tick()` (JS:
      `index.html:4180`) is bigger than "call stepCar() per car" -- it's the
      actual per-frame orchestrator, and `stepPace()`/`updateAero()`/
      `collide()` all had to be ported alongside `stepCar()`'s pace-mode
      branch just to get ONE tick of the pace phase to run at all. AI
      pit-strategy decisions, blowout/DNF rolls, and the entire caution
      controller are still not ported -- all gated on `S.mode==='race'` in
      JS, so genuinely unreachable during the pace phase (not just assumed
      so -- see the Session 3 log entry for how this was actually confirmed).
      `src/sim/race.cpp`'s `tick()` ports exactly the pace-phase-relevant
      subset of the real `tick()`; see its own header comment in `race.h`
      for the precise list of what's deliberately deferred (render-only
      previous-pose storage, `S.order` sort, qual/pit-strategy/blowout/
      caution-controller blocks).
- [x] **Determinism harness**: instrument the JS (headless Node/Playwright — this
      repo already has `tools/playtest.js` and `tools/wreck_stats.js` as a working
      example of exactly this kind of headless harness, reuse its patterns) to dump
      per-car `{x,y,hdg,v,lap,s,lat}` every tick for a fixed scenario/seed. Run the
      same scenario through the C++ port and diff. Any divergence is a bug to find,
      not a rounding error to shrug off — match float operation *order*, not just
      formulas, since that's what determinism actually depends on.
      **Done (scaffolding half)**:
      - `tests/determinism/dump_js_trace.js` runs the REAL `index.html` headlessly
        (Playwright, same pattern as `tools/playtest.js`, including its exact
        synthetic player-brain verbatim -- that brain is a pure function of car/
        track state with no RNG, so reusing it doesn't perturb the game's own
        `rng()`/`rngR()` call sequence). Drives `tick()` directly for a fixed
        tick count and dumps a flat-text trace: one `TICK ...` line (sim time,
        mode, flag, greenLockT, sinceGreenT, PACE state) followed by one
        `CAR ...` line per car (see the script's own field-order comment) per
        tick. **Verified reproducible**: ran it twice independently for 400
        ticks on track 0 -- the two 2.17MB output files were byte-identical.
        The fixture itself is regeneratable on demand (see the script's usage
        comment) and intentionally NOT committed -- it's multi-MB and fully
        reproducible from the script + committed `index.html`, so committing it
        would just be repo bloat. Regenerate with:
        `NODE_PATH=$(npm root -g) node cpp-port/tests/determinism/dump_js_trace.js --track=0 --ticks=400 --out=cpp-port/tests/fixtures/trace_track0_green.txt`
      - `tests/determinism/trace.{h,cpp}` is the C++-side reader (`loadTrace()`)
        and comparator (`diffTraces()`, stops at the first diverging tick and
        reports every field that diverged there, since later ticks are almost
        always cascading noise from the same root cause). Self-tested in
        `tests/determinism_test.cpp` against a tiny hand-written 2-tick/2-car
        fixture (`tests/fixtures/trace_synthetic.txt`, committed -- it's ~10
        lines, not regeneratable-and-large like the real trace) -- confirms
        parsing is correct and that `diffTraces()` both reports clean on an
        identical trace and correctly localizes an injected single-field
        divergence to its exact tick/car/field.
      - **What's NOT done yet, deliberately**: there is no C++ simulation to
        actually run and diff against a real JS trace yet, because `stepCar()`/
        `tick()` don't exist in C++ (that's Phase 1f/1g). This sub-task was
        scoped to "build and prove the harness plumbing works," not "verify
        stepCar()" -- that verification happens incrementally as each branch
        of `stepCar()` lands, per the checklist item below.
      - **Critical bug found and fixed in Session 3, logged so it's never
        rediscovered the hard way**: `dump_js_trace.js`'s original version
        called `tick()` directly in a loop but did NOT stop `index.html`'s own
        `requestAnimationFrame(frame)` loop (started unconditionally at page
        load, index.html:4730) from *also* calling `tick()` at real-wall-clock
        rate via its own `acc += simDt; while(acc>=DT){tick(); acc-=DT;}`
        accumulator. Every `await` in the Playwright script (waitForTimeout,
        click, ...) let real time pass, during which the page's own rAF loop
        silently ticked the sim in the background -- so the "tick 0" the
        script captured was actually already several real ticks ahead of a
        true fresh start (observed: `S.t` was `0.46` instead of the expected
        `0.02` at the first captured tick). This produced a JS trace that
        looked plausible but was NOT what a byte-for-bit-correct C++ port
        would ever match, because it included nondeterministic wall-clock-
        dependent extra ticks before the script's own loop even started. Fixed
        by neutering `requestAnimationFrame` via `page.addInitScript()` before
        `page.goto()`, so the page's own loop never fires and `tick()` is
        *exclusively* driven by the script's explicit loop. **Any trace
        generated before this fix is invalid and must be regenerated** --
        there should be no such traces committed (the fixture is intentionally
        never committed, see above), but if a stale one is ever found lying
        around, throw it away rather than trusting it.

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

- **Session 2**: Phase 1a-1d complete (RNG, track builder, CAR/ROSTER/makeCar,
  speed model) -- see the Phase 1 checklist above for exactly what was ported
  and how each piece was verified. Every sub-task's ground truth was captured
  by running the *actual* JS algorithm (copy-pasted verbatim, not
  reimplemented from memory) under Node, never hand-derived -- this matters
  because determinism bugs are exactly the kind of thing that "looks right"
  but silently isn't. All 4 test binaries (`rng_test`, `track_test`,
  `car_test`, `speed_model_test`) pass and are wired into `ctest`. Committed
  as 3 separate checkpoints (1a alone, 1b alone, 1c+1d together since 1d was
  small and directly extended 1c's file), each pushed immediately after its
  tests passed, per the "commit at logical checkpoints" rule.
  **Next run: Phase 1e/1f -- the determinism harness and `stepCar()` itself.**
  This is the big one (JS: `index.html:686`, described in the JS source's own
  comments as touching player input, AI groove/pass-side logic, the pit-road
  state machine, caution/pace-car following, spin/wreck, and damage/blowout
  all in one function) -- do NOT attempt it in a single pass. Build the
  determinism harness first (instrument `index.html` headlessly, reusing
  `tools/playtest.js`'s patterns from the main JS repo, to dump per-car
  `{x,y,hdg,v,lap,s,lat}` every tick for a fixed seed/scenario), then port
  `stepCar()` branch by branch, verifying each branch against that harness
  before moving to the next, exactly as the Phase 1 checklist already says.
  `gridStart()`/`stepPace()`/the `S` race-state object/the GWC state machine
  (Phase 1g in the checklist) can go either before or after `stepCar()` --
  `stepCar()` doesn't strictly need them to be unit-testable in isolation (it
  can take a `Car&` and whatever state it reads as explicit parameters), but
  a *full* race-scenario determinism run will eventually need all of it
  wired together, so don't leave it unexamined until the very end.

- **Session 3**: Ported and verified the pace phase end-to-end -- the first
  real slice of `stepCar()`/`tick()` with an actual C++ simulation running
  and matching JS tick-for-tick, not just isolated pure functions. New files:
  `src/sim/race_state.h` (`RaceState`, `PaceCar`), `src/sim/race.{h,cpp}`
  (`gridStart()`, `stepPace()`, `updateAero()`, `collide()`, `tick()`),
  `src/sim/step_car.{h,cpp}` (`stepCar()`, pace-mode branch + shared physics
  tail only -- every other branch throws `std::logic_error` if reached).
  `Car` gained `blown`/`dmgCd`/`spinRollCd` (JS adds these dynamically, not
  in `makeCar()`'s literal, but they're physics-relevant so they're real
  fields here now). `tests/race_sim_test.cpp` unit-tests `gridStart()`/
  `stepPace()`/`updateAero()`/`collide()` against small hand-checked JS
  ground truth. `tests/determinism_pace_check.cpp` is the real end-to-end
  check -- runs the ported `gridStart()`+`tick()` loop and diffs against a
  JS-generated trace via the Phase 1e harness; **not** a `ctest` target
  since it needs an external Playwright-generated fixture (build it and run
  it manually: `./build/determinism_pace_check <trace_file> <track_idx>
  <num_ticks>`).
  **Result: full match, byte-for-bit, for the ENTIRE pace phase** -- 1395
  ticks on Thunder Oval (every tick from grid formation through the pace
  car's lead→peel→parked transitions, right up to the tick before the
  leader takes the green and `S.mode` flips to `'race'`), plus 600 ticks
  verified on Big Sable Speedway (the symmetric track) for cross-track
  confidence. This is a real, substantial, `stepCar()`-adjacent physics
  surface working correctly: track projection, tire grip/wear, fuel burn,
  drag/engine force, speed/yaw integration, drafting, and pairwise collision
  resolution, all bit-matching JS across ~28 sim-seconds x 20 cars.
  Along the way, found and fixed a real bug in the Session 2 harness itself
  (not the C++ port) -- see the Phase 1e checklist item's new note on
  `requestAnimationFrame` double-stepping. This means the Session 2 claim
  "verified reproducible" was still true (both runs had the SAME bug, so
  they still matched each other), but the trace's actual tick-0 content was
  wrong until this session's fix. Lesson: reproducibility-with-itself is
  necessary but not sufficient -- it doesn't catch a bug that's present
  identically in every run.
  **Next run: keep working through `stepCar()`'s remaining branches.** The
  natural next branch is whichever comes right after the pace phase in a
  real race's timeline -- once `S.mode` flips to `'race'`, EVERY car (AI and
  player) needs either the player-input branch (`c.isPlayer`, index.html:
  859-864, trivial) or the AI-race branch (the final `else`, index.html:
  865-975, the big one: pack-hold/lane-ease, groove-based lane targeting,
  blocker detection, and the pass-side swerve logic). Recommend porting the
  player-input branch first (small, and player-only so it's independently
  testable against the exact same synthetic brain script already used for
  trace generation), then the AI-race branch (budget real time for it --
  it's dense and has several interacting sub-behaviors, see its own
  extensive JS comments for why each piece exists before touching any of
  it). After that: spin (`spinT>0`, short), then pit (`c.pit>0`, a small
  state machine), then the yellow-caution branch and `tick()`'s caution
  controller together (they're two halves of the same restart-sequencing
  behavior), then GWC/qual last, matching the original checklist's
  suggested order. Keep using `determinism_pace_check`-style verification
  (extend it or add a sibling tool) for each new branch -- generate a JS
  trace that actually exercises the new branch (e.g. a scenario that forces
  a caution to check the yellow branch), diff, fix, repeat.
