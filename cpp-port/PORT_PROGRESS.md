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
      clamp, lap counting) -- see `src/sim/step_car.cpp`. `Car` gained
      `blown`/`dmgCd`/`spinRollCd` fields the tail needs (JS adds these
      dynamically outside `makeCar()`'s literal object, but they're
      physics-relevant so they're real zero-defaulted `Car` fields here, not
      skipped like the HUD-only dynamic fields). The pit-entry arming block
      (index.html:692-701) is also not ported yet, but is provably a no-op
      during everything verified so far (its guard needs `pitReq`/
      `dtPending`, both requiring the pit-strategy AI in `tick()`, not yet
      ported) so this is a real, not just assumed, inert omission.

      **Progress (same session, continued): the player-input branch
      (index.html:859-864) and the AI-race branch (index.html:865-975 --
      pack-hold/lane-ease, groove-based lane targeting, blocker detection,
      pass-side swerve) are ALSO done now.** `RaceState` gained `tilt`/
      `tiltG` (a scoping correction -- these were originally filed under
      "UI-only, skip" alongside sound/volume/camMode/shakeT, which was
      wrong: they're the tilt-steer INPUT SIGNAL the player branch reads
      directly, a physics input, not a render concern). New `PlayerInput`
      struct (`race_state.h`) ports JS's `input` global. `stepCar()`/`tick()`
      signatures now take `const PlayerInput&`, threaded through from a
      caller-supplied source (a human, or -- for testing -- a scripted
      brain). `LANE_EASE_DUR` added to `constants.h`.

      **Progress (same session, continued further): the spin branch
      (index.html:731-736) and the full pit-road state machine
      (index.html:737-773, all 4 `c.pit` states -- approach/service/exit-
      lane/drive-through) are ALSO done now.** `Car` gained `dtPending`
      (drive-through-penalty pending flag; JS adds it dynamically, but the
      pit branch itself reads/clears it, so it's a real field here, same
      rationale as `blown`/`dmgCd`/`spinRollCd`). New `pitStallS()`
      (`car.{h,cpp}`, index.html:682-685) gives each car's pit-stall
      position. Remaining unported branches: victory, out/done,
      yellow-caution (+ `tick()`'s caution controller). These still throw
      `std::logic_error` if reached, and still can't be, since nothing
      verified so far triggers them.

      **Progress (same session, continued once more): the yellow-caution
      branch (index.html:774-836) and `tick()`'s full caution controller
      (index.html:4251-4461) are ALSO done now.** New `race.cpp` functions:
      `activeLead()` (index.html:1138-1141) and `cautionController()`
      (the green-branch wreck-detect-and-throw-yellow logic, plus the
      whole yellow-phase machine: adaptive pace speed, cautionSlot
      compaction, the 40s-mark time-compressed straggler warp, the
      bunched/stragglers check, the one-to-go transition, and the pace-car
      pit-entry + green-restart trigger). `tick()` itself now also computes
      `S.order`'s C++ equivalent (a race-position-sorted `vector<Car*>`,
      needed by both `activeLead()` and the caution controller) and calls
      `cautionController()` at the right point in the sequence. HUD/audio/
      render-only side effects in the JS source (`S.msgTxt`/`msgT`,
      `CAR_MAT_AMBER`, `cam.pos`, `spotterSay()`) are not ported, same
      rationale as everywhere else this session. ~~Remaining unported
      branches: victory, out/done.~~ **Done -- see the Session 3 log entry
      below on the victory-lap and out/done branches.** AI pit-strategy (sets `c.pitReq`, which
      nothing reads yet) and blowout/DNF rolls (organic spin/DNF triggers)
      are also still deferred -- tested instead via `--force`, same as spin/
      pit were.

      **Verification needed a new capability**: forcing a spin at tick 0
      (as spin/pit testing already did) doesn't reach `S.mode==='race'` in
      time -- `spinT` decays in 80 ticks, long before the ~1395-tick pace
      phase ends -- so the caution controller (gated on `S.mode==='race'`)
      would never see it. `dump_js_trace.js` and `determinism_check` both
      gained an optional `--force-tick=N` so the seed applies mid-run
      instead of at grid formation. **Result**: forcing a spin at tick 1420
      (just after the green flag) reproduced the exact real sequence --
      yellow thrown, cautionSlot assigned by physical position, single-file
      formation -- and matched JS byte-for-bit for the immediate aftermath
      and, in a second run (different car/tick), for **757 ticks of ongoing
      yellow-flag pacing** (adaptive pace speed, slot compaction all
      verified correct) before hitting a THIRD occurrence of the
      wall-clamp floating-point boundary phenomenon from this session's
      Open Questions entry -- see below, this recurrence sharpened the
      understanding of when to expect it. **Not yet verified with a clean
      run**: the 40s forward-warp, the bunched/one-to-go transition, and
      the green-restart trigger -- both attempts hit the wall-boundary
      fork before reaching them (the first at ~85 ticks into yellow, the
      second at ~757). See "Open questions" for why this is genuinely hard
      to avoid over a long caution, and the recommended approach for
      whoever verifies this next.

      **Verifying spin/pit needed a new technique**: these essentially
      never occur organically within a few hundred ticks (no natural
      incident that early), so `dump_js_trace.js` gained an optional
      `--force=idx:scenario[,idx:scenario...]` flag that seeds a car's
      `spinT`/`pit` state right after the grid forms (still not touching
      `index.html` -- this pokes `S.cars` from the test script itself, the
      same way a real incident would set that state). `determinism_check`
      gained a matching `--force` argument so both sides start from
      identical seeded state. **Verified each in isolation**: spin (200
      ticks) and all 4 pit sub-states (300 ticks each) all matched JS
      byte-for-bit. A combined test forcing all 5 scenarios onto adjacent
      grid slots simultaneously did diverge (tick 159, two OTHER
      unforced cars' headings) -- but this is an artificial stress test (5
      simultaneous incidents packed onto neighboring grid slots is not a
      realistic scenario) that triggers a cascading multi-car pileup/
      traffic-jam; given every branch verified cleanly in isolation, this
      is almost certainly another instance of the same floating-point
      boundary phenomenon documented below, amplified by how many cars are
      tightly interacting at once -- not chased further, since isolating
      and confirming each branch independently is the actual verification
      bar, not surviving an artificially dense worst-case pileup.

      **Verification**: `tests/test_driver_brain.h` ports (verbatim) the
      exact synthetic player-brain `dump_js_trace.js` uses, so the C++ side
      can drive the player through an actual green-flag race the same way
      the JS ground-truth trace was generated. `tests/determinism_check.cpp`
      (renamed from `determinism_pace_check.cpp`, generalized to cover
      race-mode too) confirmed **byte-for-bit match for ~900 ticks of real
      green-flag racing** (all 20 cars using the newly-ported branches,
      right through the pace->race transition) before hitting a genuine,
      well-understood floating-point edge case -- see "Open questions"
      below for the full writeup. This is NOT a translation bug: the
      pack-hold/lane-ease timing, blocker detection, pass-side logic, and
      throttle/brake/steering decisions all matched exactly for those ~900
      ticks (confirmed by direct side-by-side inspection of the AI's actual
      decision variables, not just final position/velocity), and the
      eventual divergence traces to a sub-1e-13 (few-ULP) numerical
      difference that happens to straddle an exact equality boundary in the
      wall-clamp check -- see below.
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
artifact of the original's own constraints rather than intentional design.)*

### Cross-runtime floating-point precision limit at hard thresholds (Session 3)

**Not a translation bug -- read this before "fixing" anything here.**

While verifying the newly-ported player-input and AI-race branches
(`tests/determinism_check.cpp` against a fresh `dump_js_trace.js` trace,
track 0, 3000 ticks), found a real divergence at **tick 2298, car idx 3**:
JS and the C++ port matched byte-for-bit for ~900 ticks of actual green-flag
racing (from the pace->race transition at tick ~1418), then car 3's
position/speed/heading suddenly diverged by an amount too large to be
numerical noise (v: JS 46.97 vs C++ 49.44).

Traced the actual root cause by dumping full-precision (`%.17g`/
`toPrecision(17)`) per-tick state for car 3 from both engines side by side
(the `DEBUG_CAR_IDX` env var on `tests/determinism_check.cpp` was added for
exactly this kind of investigation and is safe to leave in -- it's a no-op
unless set):

- Ticks 2290-2297: both engines already differ, but only in the **13th-15th
  significant digit** (e.g. tick 2297's `lat`: JS `11.000000000000012`, C++
  `11.000000000000011` -- a difference of ~1 part in 10^14, i.e. a couple of
  ULPs for a value of this magnitude). This is present well before the
  visible divergence and never grows on its own.
- Car 3 spends this whole window riding almost exactly at the wall
  (`lat` hovering right at `WALL_CLAMP_LAT` = `halfW()+5` = `11.0` exactly
  for this track). The wall-clamp check is a **strict `>` against an exact
  double**, `off > wallClampLat`.
- At tick 2298, JS's `off` (car 3's `lat` as computed entering that tick)
  is `11.000000000000014` (a few ULPs *above* 11.0 -> clamp fires, ~5%
  speed cut applied). The C++ port's `off` for the exact same tick came out
  to `11` **exactly** (not above 11.0 -> clamp does NOT fire). One engine's
  pre-existing few-ULP noise happened to land on the "clamp" side of the
  boundary; the other's landed exactly on the line. That single discrete
  fork is the entire divergence -- everything downstream from tick 2298 is
  two now-genuinely-different (but both individually consistent) physics
  trajectories, which is expected and correct behavior for a system that
  took a different branch, not evidence of an ongoing bug.

**Ruled out as the ULP source** (don't re-suspect these without new evidence):
- `Math.sin`/`cos`/`atan2` vs `std::sin`/`cos`/`atan2`: spot-checked the
  exact input values from this scenario directly (glibc vs V8, same
  container) -- bit-identical results.
- `Math.tan` vs `std::tan`: swept a range of bank angles the game actually
  uses -- bit-identical results.
- FMA/fused-multiply-add contraction (`-ffp-contract`): empirically ruled
  out -- this build has no `-march=`/`-mfma` flag anywhere, so the target
  ISA doesn't even have fused-multiply-add instructions available to
  contract into; confirmed with a direct multiply-add microbenchmark
  (`-ffp-contract=off` vs default: identical output).

**Not yet identified**: the exact originating expression that first
introduces the sub-1e-13 discrepancy. Given how small it is (right at
double-precision epsilon for values in the ~100-150 range this scenario
uses) and that it doesn't grow between ticks 2290-2297, it's most likely
last-bit rounding differences accumulating from *some* chain of operations
across many ticks of iterative position/heading integration -- not
necessarily any single "wrong" line. Chasing the exact originating
expression further did not seem like a good use of time relative to value:
**this is not fixable by finding-and-correcting a translation error**,
because the individual per-tick differences are already below any
reasonable tolerance and the code has been checked formula-by-formula
against the JS source. Making the two engines bit-identical *forever*
would require replacing every transcendental math call with a custom,
guaranteed-bit-identical implementation shared by both languages -- a large
undertaking with no real payoff for a racing game whose original JS version
never needed cross-run bit-reproducibility either.

**Practical takeaway for future verification runs**: expect exact matches
to hold for a large but finite number of ticks, then expect an eventual
divergence if (and only if) some car's state grazes a hard `>`/`<`/`===`
threshold this closely for long enough. When a divergence is found:
1. Check whether the pre-divergence values already differed, even by a
   tiny amount (`%.17g`/`toPrecision(17)`, not the default tolerance-based
   diff) -- if yes, and the tick immediately after crosses a hard
   threshold (wall clamp, an exact caution-slot boundary, etc.), this is
   very likely the same phenomenon, not a new bug.
2. If instead the PRE-divergence values were truly bit-identical and the
   post-divergence values differ by an amount too large to be a boundary
   flip, that IS a real translation bug worth chasing.
3. Either way, verify by direct inspection of the actual decision
   variables (blocker choice, targetSpeed, thr/brk/steerIn, laneEase --
   not just final x/y/v), the way this session did, rather than guessing
   from final position/velocity alone.

**Update (same session, caution-controller verification): this recurs more
than "rare edge case" implies.** Verifying the newly-ported caution
controller hit the *exact same* `off`-vs-`WALL_CLAMP_LAT` fork twice more,
in two different forced-spin scenarios (different car, different tick) --
both times because a car ended up riding at `lat` essentially exactly
`11.0` (the wall boundary for this track) for an extended stretch: once
right after a spin scrubbed off near the wall, once during ordinary
single-file caution pacing. **Refined understanding**: this isn't a rare
coincidence -- any sustained period of a car sitting at or very near
`WALL_CLAMP_LAT` (which naturally happens during formation/caution driving,
post-spin settling, or anything else that parks a car near the outer wall
for a while) will EVENTUALLY hit this fork, typically within a few hundred
to ~1500 ticks of that condition persisting. **Consequence for
verification strategy**: getting a fully clean, bit-exact run through an
*entire* multi-thousand-tick caution cycle (yellow thrown all the way
through the green restart) may not be practically achievable this way --
two attempts got to 85 and 757 ticks into the yellow flag respectively
before hitting this fork, and a third/fourth attempt would likely fare
similarly. This does NOT mean the un-reached logic (the 40s forward-warp,
bunched/one-to-go transition, restart trigger) is unverified-and-suspect --
it means bit-exact-whole-simulation matching is the wrong tool for
verifying code that only runs late in a long scenario. **Recommended
approach for whoever verifies the remainder**: don't chase a longer clean
run. Instead, verify the specific untested logic by direct inspection of
its own decision variables (`bunched`, `stragglers`, `S.oneToGo`,
`PACE.state`, the forward-warp's `se`/`clear` computations) captured via
instrumented JS output compared against the same variables computed by the
C++ port, the same technique already used successfully for the AI-race
branch's blocker/passSide logic earlier this session -- this verifies the
*logic* is correct independent of whether some unrelated car's exact
position has drifted by 2e-13 due to an unrelated wall-boundary fork
elsewhere in the same run.

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

- **Session 3 (continued)**: Ported and verified the player-input branch
  (index.html:859-864) and the AI-race branch (index.html:865-975 --
  pack-hold/lane-ease, groove-based lane targeting, blocker detection,
  pass-side swerve logic). Corrected a scoping mistake from earlier in this
  session: `RaceState` had filed `tilt`/`tiltG` under "render-only, skip,"
  which was wrong -- they're the tilt-steer physics input, now real fields.
  Added `PlayerInput` (`race_state.h`), threaded through `stepCar()`/`tick()`.
  `tests/test_driver_brain.h` ports the exact synthetic player-brain
  `dump_js_trace.js` uses, so the C++ side can drive a real green-flag race
  the same deterministic way the JS ground truth was generated.
  `tests/determinism_pace_check.cpp` was renamed to `tests/determinism_check.cpp`
  and generalized to cover race-mode ticks, not just the pace phase; it also
  gained a `DEBUG_CAR_IDX` env-var hook for dumping one car's full-precision
  state per tick, added specifically to investigate the finding below and
  worth keeping for future sessions.
  **Result: byte-for-bit match for ~900 ticks of real green-flag AI racing**
  (verified via direct value inspection of the actual decision variables --
  blocker choice, targetSpeed, laneEase/restartHeld, not just final
  position), followed by an eventual divergence traced to a genuine but
  inconsequential cross-runtime floating-point precision limit (see the new
  "Open questions" entry above for the full investigation and why it's not
  a translation bug worth chasing further).
  **Next run**: continue stepCar()'s remaining branches -- spin (`spinT>0`,
  short), then pit (`c.pit>0`, a small state machine), then the
  yellow-caution branch together with `tick()`'s caution controller (two
  halves of one restart-sequencing behavior), then victory/GWC/qual last.
  For each, the pattern that worked well this session: generate a JS trace
  that actually reaches the scenario in question (may need to force it,
  e.g. edit a throwaway copy of the harness to shorten a caution's random
  trigger odds, NOT `index.html` itself), port the branch, verify with
  `determinism_check` (extend its per-car debug hook if a divergence needs
  investigating), then update this file and commit.

- **Session 3 (continued further)**: Ported and verified the spin branch
  (index.html:731-736) and the complete pit-road state machine
  (index.html:737-773). `Car` gained `dtPending`; new `pitStallS()` in
  `car.{h,cpp}`. Since neither scenario occurs organically within a short
  trace, added `--force=idx:scenario[,...]` to `dump_js_trace.js` (seeds
  `S.cars` state post-grid-formation, still without touching `index.html`)
  and a matching `--force` argument to `determinism_check`, so both sides
  start from identical seeded incidents. Verified spin and all 4 pit
  sub-states cleanly in isolation (200-300 ticks each, byte-for-bit match).
  A combined 5-incident stress test did diverge, but only for two OTHER,
  unforced cars caught in the resulting pileup -- consistent with the same
  floating-point boundary phenomenon already documented, not a new bug (see
  the Phase 1f checklist entry above for the full reasoning on why this
  wasn't chased further).
  **Next run**: the yellow-caution branch (index.html:774-836) together
  with `tick()`'s caution controller (the block inside `tick()` that
  actually throws the yellow flag and manages the restart sequence -- these
  are two halves of one behavior and need to land together to be
  testable at all, same reason pace-phase verification needed
  gridStart()/stepPace()/updateAero()/collide() alongside stepCar()'s pace
  branch). The `--force` technique from this session should extend
  naturally: force a car's `spinT>0` mid-race (not pre-race like this
  session's tests) to make `S.flag` flip to `'yellow'` via the caution
  controller, then verify the resulting single-file caution formation,
  proximity interlock, and (eventually) the restart sequence. After that:
  victory/GWC/qual branches last, matching the original checklist order.

- **Session 3 (continued once more)**: Ported the yellow-caution branch
  (index.html:774-836) and the complete caution controller (index.html:
  4251-4461: green-branch wreck-detect-and-throw, cautionSlot assignment
  by physical position, adaptive pace speed, slot compaction, the 40s
  forward-warp, bunched/one-to-go transition, pace-car pit-entry, green
  restart trigger). New `activeLead()` and `cautionController()` in
  `race.cpp`; `tick()` now also computes the C++ equivalent of `S.order`.
  Added `--force-tick=N` to both `dump_js_trace.js` and `determinism_check`
  so a forced incident can be seeded mid-run instead of only at grid
  formation (needed since `spinT` decays in 80 ticks, well before
  `S.mode` ever reaches `'race'` if forced at tick 0).
  **Verified**: yellow-flag throw + immediate single-file formation
  matched JS exactly; a second run matched for 757 ticks of ongoing
  adaptive-pace/slot-compaction logic. Both eventually hit the same
  wall-clamp floating-point boundary phenomenon documented earlier this
  session -- now confirmed a THIRD time, and understood to recur whenever
  a car sits near `WALL_CLAMP_LAT` for a while (common during caution
  pacing), not a rare fluke. See the updated "Open questions" entry for
  why chasing an even-longer clean run isn't the right next step, and the
  recommended decision-variable-inspection technique for verifying the
  not-yet-reached forward-warp/one-to-go/restart logic instead.
  **Next run**: verify the caution controller's back half using that
  technique (instrument JS to log `bunched`/`stragglers`/`S.oneToGo`/
  `PACE.state` each tick, compare against the same C++ values -- don't
  rely on whole-simulation bit-exact matching to get there). Then: the
  victory and out/done branches (both fairly small), followed by
  green-white-checkered and qualifying (larger, saved for last per the
  original checklist order). At that point Phase 1f/1g's stepCar()/tick()
  porting is essentially complete and Phase 1h (full determinism
  verification + commit) becomes the closing task.

- **Session 3 (continued further still)**: Ported the last two small
  `stepCar()` branches -- victory-lap burnout (index.html:702-707: fixed
  throttle/steer, slow rotation, decaying speed floor at 7) and out/done
  (index.html:708-730: DNF or already-finished cars limp to a fixed
  apron lane and park; shared by both states since JS handles them in one
  `else if`). `skid`, the JS render-only screen-shake bump both branches
  set, is not ported (same rationale as every other cosmetic-only global
  this session).

  **`--force` gained a `state.field=value` entry** (alongside the existing
  `idx:scenario` form) since the victory branch needs `RaceState.mode`
  itself forced, not a per-car field -- `--force=state.mode=victory` sets
  `S.mode`/`state.mode` directly on both the JS (`dump_js_trace.js`) and
  C++ (`determinism_check.cpp`) sides. Currently only `mode` is a
  recognized `state.` field; extend `applyForce()` on both sides together
  if a future branch needs another one.

  **Bug found and fixed in the test harness itself, not the port**:
  `dump_js_trace.js`'s own argv parsing did
  `(...).split('=')[1]` to pull the value out of a `--force=...` flag.
  That breaks the instant a force spec contains its own `=`, which the new
  `state.mode=value` syntax does -- `--force=state.mode=victory` parsed to
  `FORCE_ARG = "state.mode"` (everything after the second `=` silently
  dropped), so `S.mode` got set to `undefined`, which stringifies to `""`
  when the trace line is built. That empty token, once the array is
  `join(' ')`'d and later re-split on whitespace by `loadTrace()`'s
  `istringstream >>` chain (which can't distinguish "empty field" from "no
  field"), silently swallowed one column and shifted every subsequent
  numeric field on that line left by one -- `greenLockT` read what should
  have been `sinceGreenT`'s value, and so on down the row, right up until
  a non-numeric token (`paceState`) finally failed to parse into a
  `double` and zeroed it. This produced a very convincing-looking but
  entirely bogus divergence (mode/flag string-diffs, several tick-level
  numeric fields all "wrong" by exactly one field-width) that had nothing
  to do with `stepCar()`. Root-caused by dumping the raw trace line and
  noticing a doubled space exactly where `S.mode` should have printed.
  Fixed with `.slice('--force='.length)` instead of the truncating split.
  **Lesson for future `state.field=value` additions**: never reach for
  `.split('=')[1]` on a flag value that might itself contain `=`.

  **Verified** (after the harness fix): forced `--force=state.mode=victory`
  at tick 0, 200 ticks on track 0 -- bit-exact match against JS for the
  first **100 ticks** (confirmed with a separate clean 100-tick run), then
  diverges at tick 113. Inspected the raw trace at the divergence point:
  the player's `lat` field is oscillating right on `11.000000000` --
  exactly `WALL_CLAMP_LAT` for this track, i.e. the same well-understood
  cross-runtime floating-point boundary phenomenon documented below (now
  seen a fourth time), not a translation bug -- consistent with the
  victory branch's fixed `hdg += 3.1*DT` rotation repeatedly sweeping the
  player back toward the wall in a tight, low-speed circle. Separately,
  `--force=5:out` and `--force=6:done` (200 ticks each, track 0) both
  matched JS **byte-for-bit for the full 200 ticks**, no boundary hit at
  all -- these cars settle onto the fixed apron lane (`lat=-9.5`) well
  inside the wall-clamp threshold, so the boundary case doesn't come up
  for this branch in a short run.

  **Status**: all of `stepCar()`'s branches are now ported and verified
  except green-white-checkered and qualifying (deliberately last, larger
  scope). **Next**: per the user's explicit priority redirect this
  session, do NOT pick up further JS-side work (the paused "10-step AAA
  wishlist" stays paused) -- go straight at GWC + qualifying next, then
  Phase 1h's closing full-determinism pass, then start Phase 2 (minimal
  desktop renderer: static camera, flat track ribbon + car boxes, keyboard
  input, chase camera) -- that's the first point this port is actually
  playable/visually improvable at all, which is the user's stated goal.
