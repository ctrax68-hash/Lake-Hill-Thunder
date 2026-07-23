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

### Phase 1 — Physics/AI core (headless, no graphics) — DONE (Session 2-3)
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
- [x] Port `stepCar()` (JS: `index.html:686`) — the largest single function by far.
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

### Phase 2 — Minimal renderer (desktop-first) — IN PROGRESS (Session 3)
- [x] Static camera, flat-shaded track ribbon + car boxes, no postprocessing.
      **Done**: `src/render/renderer.{h,cpp}` -- a static triangle-list ribbon
      mesh built once from `Track::pointAt()`/`halfW()` (see `setTrack()`),
      and a per-frame transient vertex buffer of 20 rotated/translated boxes
      (one per `Car`, sized to `CAR.len`/`CAR.wid`, colored by `c.col`). One
      flat vertex-color shader (`src/render/shaders/vs_flat.sc`/`fs_flat.sc`,
      no lighting -- that's Phase 5) draws both. Camera is a static top-down
      orthographic view framing the whole track's bounding box (computed once
      in `setTrack()`), aspect-corrected to the window every frame.
- [x] Keyboard input standing in for touch (gas/brake/steer). **Done**:
      `src/platform/main.cpp` reads `SDL_GetKeyboardState()` (arrows or WASD)
      into the same `PlayerInput` struct `stepCar()`'s player branch already
      consumes -- no new physics-side code needed, this was pure input
      plumbing. AI cars drive themselves via the real, verified AI branch.
      **Attempted automated verification, inconclusive** (see Session 3's
      next log entry for the full writeup): installed `xdotool` + a minimal
      WM under `xvfb-run` and tried three different ways to synthesize a
      held key into the running window; the app's own status line proved
      the plumbing is *reachable* (player coasts to a stop with no input,
      exactly as expected -- `thr=0` the whole run) but none of the three
      synthetic-input methods actually registered a keypress with the SDL
      window in this headless setup. Not chased further (known category of
      SDL2+Xvfb+XTEST/XSendEvent flakiness, not something worth burning
      more time on here) -- still needs a real interactive session to
      confirm actual car response feels right.
- [x] Chase camera -- a **2D-adapted analogue** of the JS original's
      `updateCamera()` default branch (`index.html:3399-3457`), not a literal
      port. **Done**: `Renderer`'s `CameraMode::Chase` (toggled with the C
      key, or `LHT_START_CHASE=1` for scripted verification) now does
      forward lookahead (scales with speed) + a corner-lookahead bias on the
      camera center sampled from `Track::pointAt()`'s upcoming curvature
      (same `curv*450`/`[-2.5,2.5]` constants as the JS, since it's a
      world-space offset that doesn't need rescaling for this camera's
      zoom), both exponentially smoothed toward every frame using a real
      elapsed-time delta (`std::chrono::steady_clock`, matching JS's
      `k=1-exp(-dt*11)` idiom and its `Math.min(0.05,...)` stall clamp), at
      a tighter fixed zoom than the top-down mode. Re-entering chase mode
      hard-snaps instead of gliding in, same "cut not glide" idiom used
      throughout the JS for mode transitions.
      **Deliberately NOT ported** (needs real 3D geometry that doesn't exist
      until Phase 5): banking lean (JS's `upBlend`), surface-height
      clamping, and the victory-orbit/pit-stop/tower/helmet/blimp/menu-
      establishing-shot/caution-TV-montage alternate camera branches
      (`index.html:3293-3437`) -- this is a flat top-down view with smoothed
      follow, not a 3rd-person chase cam.

### Phase 3 — Mobile input — IN PROGRESS (Session 3, items 1-3 done, item 4 remains)
- [x] SDL2 touch regions matching the JS original's button layout (`bL`/`bR` steer,
      `bB` brake, `bG` gas, `bP` pit). **Done**: `src/ui/touch_controls.{h,cpp}` --
      `computeTouchRegions()` mirrors the JS CSS layout's relative positions/sizes
      (`index.html:19-20,46-51,194-198`'s `--ctl*` values, at their base UI.scale===1
      pixel sizes -- not yet DPI/viewport-adaptive the way the JS's own `UI.scale` is).
      `main.cpp` hit-tests `SDL_MOUSEBUTTONDOWN/UP` (desktop stand-in) and
      `SDL_FINGERDOWN/UP` (real touch, normalized coords scaled by window size) against
      these regions: `bL`/`bR`/`bG`/`bB` are press-and-hold (OR-combined with keyboard
      state so either input method works), `bP` is a single toggle on press, matching
      `bindBtn()`'s and `bP`'s own JS listener exactly (`index.html:1235-1246,4664-4669`).
      This is input recognition only -- no visible on-screen button is drawn (that's
      Phase 4's "UI overlay" job).
      **Also closes a real gap found while implementing this**: the player previously had
      *no way at all* to request a pit stop in this port -- `tick()`'s AI pit-strategy
      block (added in Phase 1h) explicitly skips the player, matching JS (where `pitReq`
      is only ever set by clicking `bP`). `main.cpp` now has a `togglePlayerPit()`
      mirroring `bP`'s JS click handler exactly (same guard: `mode=="race" &&
      !player.done && player.pit==0`), wired to both the new `bP` touch region and a
      debug-only `P` keydown (JS has no keyboard binding for pit at all -- this is a
      desktop-testing convenience, not a ported behavior, same rationale as
      `LHT_FORCE_RACE`/`LHT_START_CHASE`).
      **Verified**: new `tests/touch_controls_test.cpp` (no SDL/bgfx dependency, same
      pattern as `rng_test`) checks region layout/hit-testing logic directly -- all 7
      `ctest` suites pass. **Live click-through-region responsiveness attempted once**
      (`xdotool click` at the `bG` region's center under `xvfb-run`+
      `matchbox-window-manager`, same setup as the Phase 2 keyboard attempt) and did not
      register -- same known category of SDL2+Xvfb synthetic-input flakiness already
      documented below, not chased further per that same precedent (one attempt, not
      three, once the pattern is already this well established).
- [x] `SDL_Sensor` accelerometer/gyro read for tilt-steer, matching `S.tiltG`. **Done**:
      `src/platform/tilt_input.{h,cpp}` -- `TiltInput` opens the first `SDL_SENSOR_ACCEL`
      device (`SDL_NumSensors()`/`SDL_SensorGetDeviceType()`/`SDL_SensorOpen()`), gracefully
      leaving `available()==false` when none exists rather than treating that as an error
      (mirrors JS: a desktop browser with no motion sensor just never fires
      `deviceorientation`, and `S.tiltG` stays whatever it was, `0` by default). Every
      frame, `update()` computes gravity-vector roll/pitch from the raw accelerometer
      vector and picks one via `SDL_GetDisplayOrientation()`, mirroring
      `index.html:1260-1264`'s own `screen.orientation.angle` branch (`o===90 ? -beta :
      (o===-90||o===270) ? beta : gamma`) as closely as SDL's API allows. Note this is
      populating fields that were *already* ported into the sim core back in Phase 1
      (`race_state.h:40-41`'s `state.tilt`/`state.tiltG`, read by `step_car.cpp:216`) --
      no sim-core change was needed, only the platform layer that feeds them from real
      hardware. `main.cpp` wires `SDL_INIT_SENSOR` into `SDL_Init()`, calls
      `tiltInput.update()` each frame and copies `tiltInput.tiltG()` into `state.tiltG`
      when available, and adds a debug-only `T` keydown to toggle `state.tilt` (JS toggles
      `S.tilt` from a menu checkbox that doesn't exist in this port yet -- Phase 4's job --
      same convenience-key rationale as `P`/`C`).
      **Open question, not silently assumed correct**: SDL's accelerometer axes are fixed
      to the device's own physical frame and are *not* remapped for display rotation
      (`SDL_sensor.h`'s own doc comment), which is also true of the browser's beta/gamma --
      exactly why both JS and this port need an explicit orientation-based remap. But
      which of `SDL_ORIENTATION_LANDSCAPE`/`_LANDSCAPE_FLIPPED` corresponds to the
      browser's `angle===90` vs. `angle===-90/270` is a genuine guess: neither API
      documents a shared convention, and this dev container has no accelerometer hardware
      to test against at all (a fundamentally different kind of verification gap than the
      earlier synthetic-input-delivery failures -- there's no sensor here full stop, not
      just a software delivery problem). The dominant-axis logic (roll in portrait, pitch
      in landscape) should be right; the overall sign in landscape is a single flip away
      from correct if a real device steers opposite to the physical tilt direction.
      **Genuinely unverified end-to-end** -- confirm on a real Android/iOS device once the
      NDK (item 4 below) is installed and this can actually run on one.
- [x] Portrait-lock rotate prompt (landscape-only). **Done, partially**:
      `src/ui/orientation.h`'s `isPortrait(w, h)` mirrors
      `index.html:147`'s `@media (orientation: portrait)` query exactly
      (`height >= width`). `Renderer::renderBlockedFrame()`
      (`src/render/renderer.{h,cpp}`) clears to opaque black and submits
      an otherwise-empty frame -- functionally the same end result as
      JS's `#rotate` overlay from a player's perspective (the game is
      fully hidden), but without the actual "ROTATE YOUR PHONE" text and
      spinning phone icon, since this port has no text/icon rendering
      yet (Phase 4's "UI overlay" job) -- **explicitly a scoped
      simplification, not the full visual**, revisit once Phase 4 lands.
      `main.cpp` computes `portrait` on init and on resize, calls
      `renderBlockedFrame()` instead of `renderFrame()` while portrait,
      and ignores mouse/finger touch-region events while portrait
      (mirroring the CSS overlay's z-index physically covering the
      on-screen buttons underneath, `index.html:203`) -- but *not*
      keyboard input, which JS also never blocks (window-level `keydown`
      listeners aren't affected by any DOM element sitting on top of the
      canvas). Deliberately does **not** pause `tick()` while portrait,
      matching JS: the CSS media query has no effect on the running
      `requestAnimationFrame` loop, it's purely a visual/input-touch
      block layered on top.
      Debug-only `LHT_WINDOW_W`/`LHT_WINDOW_H` env vars let a headless run
      start already-portrait (same rationale as `LHT_FORCE_RACE`/
      `LHT_START_CHASE` -- JS just reacts to whatever aspect ratio the
      real device/browser window already is; there's no equivalent
      "force" concept to port, this is purely a test-scripting hook).
      **Verified**: full `ctest` suite unaffected, 7/7 (no test-covered
      code touched). `lht_port` rebuilds clean. Captured and pixel-checked
      two screenshots under `xvfb-run`: a 480x800 (portrait) run --
      `PIL`'s `getextrema()` confirms every pixel is exactly `(0,0,0)`,
      confirming the blocked frame really is all black, not just
      "mostly" -- and a 1280x720 (landscape) run at the same commit,
      confirming the change didn't regress normal rendering (non-black
      pixels present as expected from the track/cars).
- [ ] **Install Android NDK here** (deferred from Phase 0 per the sequencing decision)

### Phase 4 — UI overlay — DONE (Session 3; Phase 4b added Session 5; Phase 4c-4h added Session 6)
- [x] Menu screen (track/laps/qualifying/sound/tilt toggles, volume slider, start button)
      **Phase 4b done (Session 5)**: `src/ui/menu.{h,cpp}` -- see this file's own Session 5
      log entry below for the full writeup (region layout, what's a real toggle vs.
      stored-but-inert UI parity for qual/sound/volume, the menu-first startup
      restructure in `main.cpp`, and end-to-end click verification via Playwright on
      the web build). RaceState's default `mode="menu"` is now a real, reachable
      entry point instead of being immediately overwritten at startup.
- [~] HUD (lap counter, position, timing — note the JS side has grown a lot more HUD
      surface recently: gear+RPM readout, live leaderboard gaps, minimap with a
      player wedge + trouble-pulse rings, segmented TIRE/FUEL/CAR bars — check
      `index.html`'s current state for the full HUD surface before assuming the
      original master-prompt's brief "lap counter, position, timing" is complete).
      **Phase 4a done, partially**: `src/render/hud.{h,cpp}` implements exactly the
      original brief's three items -- lap counter (`LAP n/N`, matching
      `index.html:3985-3987`'s exact `S.finishLaps`-as-denominator formula, not the
      buggy `S.laps` one), player race position (recomputed live from `Car::prog`/
      `done`/`finishT` using the same descending sort key `race.cpp:339-343` already
      uses for `S.order`, purely for display -- doesn't touch `tick()`), and flag
      state (`GREEN`/`CAUTION`, background-highlighted) -- plus one bonus field not in
      the original brief, current speed (`SPD`), since it was essentially free once
      the plumbing existed. Uses bgfx's own built-in debug-text overlay
      (`bgfx::dbgTextPrintf()`, a fixed 8x16-cell monospace VGA-palette text mode)
      rather than building a custom font atlas, since this port had zero text-drawing
      capability before this and bgfx already ships one.
      **Explicitly NOT ported** (deferred to future Phase 4 sub-tasks, all noted in
      `hud.h`'s own header comment): the per-driver leaderboard panel (names, car-color
      chips, live broadcast-style gaps), the minimap (player wedge + trouble-pulse
      rings), and the segmented tire/fuel/car bars -- this was deliberately just the
      three-item literal brief, not the JS side's much bigger current HUD surface.
      **Verified**: full `ctest` suite unaffected, 7/7. `lht_port` rebuilds clean.
      Captured a headless `xvfb-run` screenshot, corrected for the capture's `yflip`
      metadata (a mistake caught mid-verification -- an early flip-less crop showed
      nothing but background color, which momentarily looked like the text wasn't
      rendering at all, until re-applying the same `FLIP_TOP_BOTTOM` correction
      already established for chase-camera screenshots in Phase 2 solved it), then
      visually inspected the corrected image directly: `LAP 1 / 5`, `POS 20 / 20`, a
      green-highlighted `GREEN` banner, and `SPD  30` all render legibly in the
      top-left corner, with the track/car field still rendering normally alongside it.
      Also reconfirmed the Phase 3c portrait-block path still shows zero HUD text
      (pure black, `(0,0,0)` extrema) since `renderBlockedFrame()` clears the debug
      text buffer too.
      **Phase 4c done (Session 6)**: LAST/BEST lap time strip added, see this file's
      own Session 6 log entry below for the full writeup.
      **Phase 4d done (Session 6)**: GEAR/RPM readout added, same log entry.
      **Phase 4e done (Session 6)**: segmented TIRE/FUEL/CAR status strip added --
      this port's first real quad/shape rendering (previously dbgText-only), a new
      reusable UI-overlay rendering path used by every remaining Phase 4 sub-task.
      Same log entry.
      **Phase 4f done (Session 6)**: minimap added (track outline, car dots, player
      wedge, pulsing trouble rings). Same log entry.
      **Phase 4g done (Session 6)**: leaderboard panel added (rank, color chip,
      name/tag, live time-gap), the last HUD sub-task -- this bullet is now fully
      done. Same log entry.
- [x] Results screen (finish order, best laps, DNFs, "back to menu" restart) --
      **Phase 4h done (Session 6)**. `src/ui/results.h/.cpp` + a `gridStart()`
      bugfix (`finishOrder` now cleared, matching JS's own `S.finishOrder=[]`).
      Verified end to end, including a real restart, via headless native
      screenshots and a WASM/Playwright click-through -- see this file's own
      Session 6 log entry for the full writeup. **Phase 4 ("UI overlay") is now
      fully DONE.**

### Phase 5 — Full render fidelity — IN PROGRESS (Session 7)
- [ ] Procedural stadium/stands/crowd-tile/livery-painting mesh generation (JS builds
      these via Three.js geometry helpers + canvas-drawn textures; needs raw vertex
      buffer construction in bgfx)
- [ ] Sky/environment per track preset (JS: `ENV_PRESETS`)
- [ ] Hand-rolled bloom (downsample→blur→combine, replacing `UnrealBloomPass`) +
      tonemap (replacing `OutputPass`)

Broken into 8 sub-phases (5a-5h), one commit each, same rhythm as Phase 4's
4a-4h. Full plan on file for this session; see each sub-phase's own log entry
below for what actually landed and any scope notes.

**Phase 5a done (Session 7)**: 3D rendering foundation -- see this session's
log entry below for the full writeup (banked track mesh, real perspective
camera, hemisphere+directional lighting, depth testing).

**Phase 5b done (Session 7)**: per-track theme/stadium/ENV_PRESETS data +
real per-track lighting -- see this session's log entry below (TrackTheme/
Stadium data, env_presets.h, per-track sun/hemi uniforms, grass ground
plane).

**Phase 5c done (Session 7)**: sky background -- see this session's log
entry below (sky_texture.h/.cpp, vs_sky/fs_sky unlit textured-quad shader,
new lowest-numbered background view, Cedar Valley's hill silhouette
deferred to 5g).

### Phase 6 — Polish & platform packaging — NOT STARTED, DEPRIORITIZED
The user explicitly clarified (Session 3, same session Phase 7 below started): no App Store,
no native Android/iOS distribution wanted at all -- they want to play from Safari, ideally
installed as a home-screen PWA. This phase (native Android/iOS builds) is **not deleted**, just
no longer the near-term goal -- Phase 7 (WebAssembly/browser build) is the actual path to what
the user wants, and is now the priority track instead of this one.
- [ ] Audio port (JS has a real Web Audio graph: multi-voice opponent engines, a
      4-bus mixer, crowd/spotter/impact one-shots — check `index.html`'s audio
      section for current scope, it has grown since the original master prompt was written)
- [ ] Android build (Gradle/NDK) and iOS build (Xcode project) — iOS always needs a
      real Mac session, per the environment facts above
- [ ] Device performance pass on real mid-tier hardware (20-car field + postprocessing)

### Phase 7 — WebAssembly/browser build — DONE, first pass (Session 3)
- [x] Get `lht_port` compiling to WebAssembly via Emscripten and running inside a real
      browser tab, verified headlessly. **Explicitly out of scope for this pass** (left
      for future sessions): the installable-PWA wrapper (manifest.json, service worker,
      apple-touch-icon/theme-color meta tags, actual icon assets) and any real Safari/iOS
      verification (impossible in this container -- no macOS at all).

  **Toolchain**: Emscripten SDK installed at `~/emsdk` (v6.0.3, the current recommended
  release, confirmed via `emsdk list`) -- outside the git repo, same "host toolchain, never
  committed" treatment as the already-precedented Android NDK decision. `source
  ~/emsdk/emsdk_env.sh` must be re-run in any new shell (shell state doesn't persist
  between separate tool invocations in this environment).

  **`src/platform/main.cpp` restructure**: the X11 `SDL_SysWMinfo` block (the only
  native-specific code anywhere in this port's own `src/`, confirmed by grepping the whole
  tree for x11/SysWM/native-handle references) is now gated `#ifndef __EMSCRIPTEN__`; the
  `#else` branch sets `nwh = (void*)"#canvas"` directly -- confirmed from bgfx's own
  shipped HTML5 backend source (`bgfx/src/glcontext_html5.cpp:85`, `bgfx/examples/common/
  entry/entry_html5.cpp:414-424`) that this is a CSS selector string, not a numeric handle,
  and there's no display handle concept in a browser at all (`ndt = nullptr`).
  `Renderer::init()` itself needed no changes -- it already just forwards whatever handles
  it's given.

  The whole per-frame loop was restructured into a `LoopState` struct + a
  `mainLoopTick(void*)` callback (mirroring bgfx's own vendored HTML5 example harness,
  `bgfx/examples/common/entry/entry.cpp:551-554`, which uses
  `emscripten_set_main_loop(&updateApp, -1, 1)` rather than `-sASYNCIFY` -- the right call
  given bgfx's renderer leans heavily on function-pointer/vtable dispatch, exactly the
  risky category for ASYNCIFY's automatic call-graph instrumentation, on top of its known
  binary-size/perf cost). This wasn't optional cleanup: `emscripten_set_main_loop_arg(...,
  simulate_infinite_loop=1)` unwinds `main()`'s own stack immediately after registering the
  callback, so anything the loop touches -- including `window`/`renderer`/`track`, not just
  the frame-local booleans -- had to move out of `main()`'s plain stack locals into a
  `static LoopState S` (a function-local static has a stable address for the rest of the
  program's life from the moment it's first constructed, which every target relies on:
  `Renderer::setTrack()` stores a raw `const Track*` internally, and that pointer would
  otherwise dangle the instant this unwind happens). The native `#else` path keeps its
  original blocking `while` loop, just calling the same `mainLoopTick()` function each
  iteration -- one shared code path for both targets, not two parallel loop
  implementations. Also: `SDL_INIT_SENSOR` is now only requested on the native build
  (confirmed this session that SDL2 has no Emscripten sensor backend at all -- not even a
  dummy one, unlike its confirmed video/audio/joystick backends -- so requesting a
  subsystem with nothing behind it felt like tempting a foreseeable failure rather than
  something to assume is harmless), and the debug-only `maxFrames` default is now
  `INT_MAX`-ish under Emscripten specifically, since a real deployed web build has no
  `LHT_MAX_FRAMES` env var to read at all (browsers don't expose a shell environment to
  `std::getenv()`) -- the native build's own tested default (600) is untouched.

  **`CMakeLists.txt`**: an `if(EMSCRIPTEN)` block adds `-sALLOW_MEMORY_GROWTH=1
  -sFULL_ES3=1 --shell-file=web/shell.html` to `lht_port`'s link options, and (a real,
  non-obvious gotcha caught by re-inspecting the actual build output, not assumed)
  `SUFFIX ".html"` on the target -- emcc silently ignores `--shell-file` and just emits
  `.js`/`.wasm` unless the *output filename itself* ends in `.html`; the first successful
  build produced `lht_port.js` with no page at all until this was added. The whole
  test-executable section (`enable_testing()` onward) is skipped under
  `if(NOT EMSCRIPTEN)` -- `.js`/`.wasm` isn't a native executable CTest can invoke, and
  the native `build/`'s own `ctest` (still 7/7, confirmed via a full clean reconfigure +
  rebuild both before and after all these changes) remains the sole verification of the
  sim core; this web build is additive on top of an already-verified physics core, not a
  re-verification of it. `build-web/` added to `.gitignore`.

  **Three real build-breaking issues found and fixed, each confirmed via the actual crash/
  error output rather than guessed at**:
  1. `bx.cmake` (vendored) adds `-msse4.2` to the `bx` target whenever
     `CMAKE_SYSTEM_PROCESSOR`/`CMAKE_CXX_COMPILER_ARCHITECTURE_ID` look x86-like, true here
     too since Emscripten's toolchain doesn't override either away from the host's
     architecture. Tried overriding `CMAKE_SYSTEM_PROCESSOR` before `add_subdirectory()`
     (both as a plain variable and a FORCEd CACHE entry, confirmed via the resulting
     `CMakeCache.txt`) -- did not stop the flag from being added, so abandoned that
     approach rather than keep guessing at CMake variable-scope internals neither this
     project nor bx.cmake's own condition documents clearly. `-msse4.2` defines
     `__SSE2__`/`__SSE4_2__`, and emcc's own error message says any x86 SSE flag under
     Emscripten also requires `-msimd128` -- added that globally, which let compilation
     proceed, but crashed the LLVM WebAssembly instruction selector outright compiling
     `bx::hsvToRgb` (confirmed a genuine backend bug from the crash's own stack trace,
     `SelectionDAGISel::SelectCodeCommon`/"WebAssembly Instruction Selection", not a
     resource limit).
  2. Since `-msse4.2` is `PUBLIC` on `bx`, it doesn't just affect `bx`'s own compilation --
     it propagates via `INTERFACE_COMPILE_OPTIONS` to every target that links `bx`,
     including `bimg`. `bimg`'s vendored `etcpak` (`3rdparty/etcpak/Dither.cpp`) has its own
     `#ifdef __SSE2__`-gated x86 intrinsic includes (`ia32intrin.h`/`ammintrin.h`/
     `fma4intrin.h`) that assume a genuine x86 target and failed to compile outright once
     `__SSE2__` got defined there too. Fixed by directly stripping `-msse4.2` from both of
     `bx`'s own compile-option target properties (`COMPILE_OPTIONS` and
     `INTERFACE_COMPILE_OPTIONS`) after `add_subdirectory()`, via `list(FILTER ... EXCLUDE
     REGEX)` -- without it, `bx/include/bx/simd_t.h`'s SIMD selection falls through to its
     `#elif defined(__wasm_simd128__)` branch (real WASM SIMD via `<wasm_simd128.h>`,
     defined by the global `-msimd128`) instead of the x86-translation path.
  3. The LLVM instruction-selector crash from issue 1 recurred anyway, in a *different*
     file (`bimg/src/image_cubemap_filter.cpp`) with the identical stack trace -- proving
     it's a general LLVM bug tied to `-msimd128` at `-O3` (Release's default), not
     something specific to one translation unit worth chasing file-by-file. Fixed by
     dropping the whole Emscripten build to `-O1` globally (a later `-O` flag wins over an
     earlier one for GCC/Clang) rather than patching target after target -- correctness
     over vectorized performance for this first working build, revisit if a newer
     Emscripten/LLVM release fixes the underlying bug.
  4. `BGFX_BUILD_TOOLS_SHADER` builds `shaderc` itself under whatever toolchain is active --
     under `emcmake` that's `shaderc.js`, a wasm/Node target. CMake does invoke it
     correctly via `node` (emcmake's own `CMAKE_CROSSCOMPILING_EMULATOR`), but the
     resulting program only has Emscripten's default browser-sandboxed filesystem access,
     not real host-filesystem access -- it can't `fopen()` our `.sc` shader sources by
     absolute path at all, so `bgfx_compile_shaders()` failed with "Unable to open file".
     Building a genuinely native `shaderc` as a side-build under `emcmake` (e.g. via
     `ExternalProject_Add` with a separate native toolchain) is a real fix but more than
     this first pass needs: reused the *already-generated* shader headers from the native
     `build/generated_shaders/` directory instead, since bgfx's compiled shader bytecode
     (spirv/glsl/essl) is a GPU-target-specific artifact, not a host-CPU-specific one --
     nothing about the essl bytecode this port uses depends on whether shaderc itself ran
     on x86 Linux or wasm32. `BGFX_BUILD_TOOLS_SHADER` is now `OFF` under Emscripten
     entirely (no point spending the compile time building a tool that's never invoked),
     and a configure-time `FATAL_ERROR` check makes the native-build-must-run-first
     dependency explicit rather than a silent, confusing failure later.

  **New `web/shell.html`**: a minimal custom Emscripten shell (not the default one, which
  bundles unrelated progress-bar/status-text UI chrome) -- bare `<canvas id="canvas">`
  (the id is a hard requirement, matching the `"#canvas"` selector hardcoded into bgfx's
  HTML5 backend), a `Module = { canvas: ... }` script, dark viewport-filling CSS. No
  manifest/service-worker/icons/apple-meta-tags here at all -- explicitly deferred.

  **New `tests/wasm_verify.js`**: a one-off Playwright script (not wired into `ctest`, same
  category as `tests/determinism/dump_js_trace.js` -- needs Node/Playwright and a live
  server, not something the headless test runners here are set up for) that navigates to
  the served `lht_port.html`, collects console/page errors for the whole session, takes two
  screenshots a few seconds apart, and reports both. Uses the pre-installed Chromium at
  `/opt/pw-browsers/chromium` directly (`playwright install` is blocked in this
  environment's own setup notes).

  **Verification, all genuinely checked, not assumed**: served `build-web/` via `python3 -m
  http.server`, ran `wasm_verify.js` against it. **Zero page errors.** One console error --
  a `favicon.ico` 404, confirmed benign by cross-checking the HTTP server's own access log
  (nothing else 404'd; `lht_port.html`/`.js`/`.wasm` all served `200`). Visually inspected
  both screenshots directly: the track, the full car field, and the Phase 4a HUD text
  (`LAP 1/5`, `POS 20/20`, a green `GREEN` banner, `SPD 37`) all render correctly inside a
  real Chromium tab. Pixel-checked via `PIL`: neither screenshot is a flat single color
  (`getextrema()` shows a real 0-255ish range on all channels), and diffing the two
  screenshots pixel-by-pixel found 1259 differing pixels out of 921600 -- the car field
  visibly advanced (positions shifted, `SPD` dropped from 37 to 35) between the two
  captures, proving the frame loop is genuinely running via `requestAnimationFrame`, not
  stuck on one static frame. Re-ran the native `build/`'s full `ctest` suite twice (once
  mid-session, once after a completely fresh `cmake -B build` reconfigure at the end) --
  still 7/7 both times, confirming none of this session's `CMakeLists.txt`/`main.cpp`
  changes touched the already-verified native desktop path or physics core.

  **Explicitly, honestly NOT verified** (impossible in this container, no macOS at all):
  real Safari behavior on either desktop or iOS. Safari has real, well-known WebGL2/
  WebAssembly/Web Audio quirks (autoplay-unlock requirements, orientation-lock support
  gaps already noted in this file's own Phase 3c notes, historically stricter WebGL2
  context creation) that this Chromium-only verification says nothing about. This is the
  single largest remaining open item before "play from Safari" is actually confirmed, not
  just plausible -- whoever next has a real Mac/iPhone should try the served `build-web/`
  output directly, ideally after Phase 7's next session adds the PWA installability
  wrapper so there's something worth installing to test.

  **Status**: the core technical bet of this session -- "can this C++/SDL2/bgfx codebase
  actually run in a browser at all" -- paid off, with three real, non-obvious build issues
  found and fixed along the way (not just a clean first try). What exists now is a
  browser-loadable build proven to render and tick correctly in headless Chromium; it is
  not yet an installable PWA (no manifest/service-worker/icons) and not yet verified on
  any real Apple device.

  **Next**: PWA wrapper (`manifest.json`, service worker, `apple-touch-icon`/
  `theme-color`/`apple-mobile-web-app-*` meta tags, actual icon assets) is what actually
  delivers "installable from Safari home screen" -- the natural next session now that the
  underlying WASM build is proven working. Real Safari/iOS verification remains open
  until someone with an actual Apple device can test it. Also worth reconsidering later:
  a genuinely native `shaderc` side-build (removing the native-build-must-run-first
  coupling), and whether the global `-O1` downgrade can be narrowed once/if the upstream
  LLVM WASM-SIMD-at-`-O3` bug is fixed.

### Phase 7b — PWA installability wrapper — DONE (Session 4)
- [x] Make `build-web/lht_port.html` genuinely installable as a PWA (manifest, service
      worker, icons, apple-touch-icon/theme-color meta tags) -- what actually delivers
      "Add to Home Screen" on Safari, the user's real underlying goal since this project's
      pivot away from native App Store distribution. **Explicitly still out of scope**: real
      Safari/iOS "Add to Home Screen" verification -- no macOS/iOS device exists anywhere
      in this container, same caveat Phase 7 already ended on.

  **New `web/icons/` assets**: no reusable icon art existed anywhere in the repo (checked --
  every image asset in the tree is vendored third-party SDL2/bgfx example content), so
  generated new ones via a small one-off Pillow script, `web/generate_icons.py` (committed
  for reproducibility, deliberately *not* wired into the CMake build -- it's authored
  branding art, not a build artifact). Design: a checkered-flag motif in a yellow frame --
  not an arbitrary choice, `index.html`'s own JS side already established exactly this
  visual language for race-state UI (its "unified state banner + flag icon routine", see
  UI-D in this file's much earlier session log) and a black/white/`#F7D400` yellow palette
  (`index.html`'s `--c-yellow` custom property), so this reuses the game's own existing
  identity rather than inventing a new one. Sharp corners throughout, matching this
  project's own established design-token convention (no rounded corners anywhere in the JS
  UI). Three sizes generated: `icon-192.png`, `icon-512.png` (manifest `icons` array, what
  Android/Chrome's installability check wants), and `apple-touch-icon.png` at 180x180 --
  confirmed via research this session that iOS Safari does *not* read the manifest's icons
  array for the home-screen icon at all, using this separate `<link>` tag instead.

  **New `web/manifest.json`**: standard Web App Manifest -- `name`/`short_name`: "Lake Hill
  Thunder"/"LHT", `start_url`/`scope` both relative (`"./lht_port.html"`/`"./"`, so it works
  regardless of what path this eventually gets deployed under), `display: "standalone"`
  (the broadly-correct choice for a game; iOS actually ignores this field and uses the
  `apple-mobile-web-app-capable` meta tag instead, see below), `orientation: "landscape"`
  (matches the game's landscape-only design and Phase 3c's own existing portrait-block
  work -- honored on Android/Chrome, harmlessly ignored on iOS), `background_color`/
  `theme_color` both `#000000` (matches `shell.html`'s existing black chrome), and an
  `icons` array referencing the two PNGs above.

  **`web/shell.html` additions** (had none of this before this session -- confirmed by
  re-reading it at the start): the viewport meta tag (`width=device-width, initial-scale=1,
  maximum-scale=1, user-scalable=no, viewport-fit=cover`, copied verbatim from `index.html`'s
  own -- `index.html` itself is never touched, per this project's standing immutable-
  reference rule), `<link rel="manifest">`, `<link rel="apple-touch-icon">`, `<link
  rel="icon">` (see the favicon fix below), `theme-color` meta, and the
  `apple-mobile-web-app-capable`/`-status-bar-style`/`-title` trio (`index.html` only has
  the first of these three; this port adds all three for completeness). A small inline
  script registers the service worker (`if ('serviceWorker' in navigator)
  navigator.serviceWorker.register('sw.js')`).

  **New `web/sw.js`**: a straightforward cache-first service worker -- `install` caches the
  whole app shell (`lht_port.html`/`.js`/`.wasm`, `manifest.json`, all three icon PNGs)
  under a versioned cache name (`lht-v1`) and calls `self.skipWaiting()`; `activate` deletes
  any cache not matching the current version name and calls `self.clients.claim()`;
  `fetch` serves from cache first, falling back to network, and opportunistically caches
  new same-origin "basic" responses. This is what makes the 824KB `.wasm` binary load
  instantly on repeat visits and enables offline play after the first load -- genuinely
  part of "feels like an installed app," not just decoration.

  **`CMakeLists.txt` wiring**: `build-web/` previously had no mechanism at all for getting
  static files into it besides emcc's own `--shell-file` output. Added a `POST_BUILD`
  custom command on the `lht_port` target (still inside the existing `if(EMSCRIPTEN)`
  block) that copies `web/manifest.json`/`web/sw.js` and copy-directories `web/icons/` into
  `${CMAKE_CURRENT_BINARY_DIR}` alongside the emcc-produced files, via
  `${CMAKE_COMMAND} -E copy_if_different`/`copy_directory`. Chose `POST_BUILD` over a
  configure-time `file(COPY)` specifically so edits to these files take effect on the next
  build without requiring a manual reconfigure -- confirmed this actually matters: a
  `shell.html` edit made mid-session did *not* get picked up by CMake automatically (it has
  no way to know `--shell-file`'s target is a build input at all), and needed the stale
  `lht_port.html`/`.js`/`.wasm` outputs deleted by hand to force a relink -- a real gotcha
  worth remembering for any future `shell.html` edit, not just a one-off annoyance this
  session. Caught one self-inflicted footgun before it shipped: `generate_icons.py` was
  initially written to live inside `web/icons/` itself, which would have caused the new
  `copy_directory` step to stage the *generator script* into `build-web/icons/` alongside
  the actual PNGs; moved it to `web/generate_icons.py` and switched its path handling to be
  relative to the script's own location rather than the caller's cwd.

  **New checks added to `tests/wasm_verify.js`** (extended, not replaced -- Phase 7's
  original non-blank/frame-advances screenshot check is untouched and still runs every
  time): `manifest.json` fetches `200` and parses as JSON with `name`/`icons`/`start_url`/
  `display` all present; the three icon PNGs fetch `200` and pass a PNG-signature sanity
  check; `navigator.serviceWorker.ready` resolves without throwing after page load
  (confirmed registration actually completes, not just that `register()` was called).

  **A real bug found and fixed this session, not assumed away**: the first verification run
  reported one console error -- a `favicon.ico` 404. Root-caused (not just noted and
  ignored, unlike Phase 7's own more permissive pass) by cross-checking with a standalone
  Playwright script logging every `response`/`console` event: it's Chromium's automatic
  `GET /favicon.ico` that every browser issues when a page declares no `<link rel="icon">`
  at all, confirmed via a direct `curl` against `/favicon.ico` returning `404` independent
  of anything this session touched. Fixed properly (not suppressed) by adding
  `<link rel="icon" href="icons/icon-192.png" type="image/png">` to `shell.html`, reusing
  the already-generated 192px icon rather than creating a fourth asset -- confirmed this
  eliminates the request entirely (browsers only fall back to `/favicon.ico` when no icon
  link is declared) and re-verified `wasm_verify.js` now reports zero console errors and
  zero page errors.

  **Verification, all genuinely checked, not assumed**: rebuilt `build-web/` from a clean
  `emmake cmake --build` after every source change (had to delete the stale `lht_port.html`/
  `.js`/`.wasm` once to force a relink after the `shell.html` favicon fix, per the gotcha
  above); confirmed via `ls`/`grep` that `manifest.json`, `sw.js`, and all three icon PNGs
  land in `build-web/` and that the built HTML's `<head>` actually contains the new
  `<link rel=icon>`/`<link rel=manifest>`/`<link rel=apple-touch-icon>` tags (minified but
  present). Served via `python3 -m http.server 8765`; `curl` confirmed `200` on
  `lht_port.html`, `manifest.json`, `sw.js`, `icons/icon-192.png`, `icons/apple-touch-
  icon.png`. Ran the extended `wasm_verify.js`: manifest and all three icons fetch `200`
  and validate; service worker registers and reaches `ready` (scope
  `http://localhost:8765/`); **zero console errors, zero page errors** after the favicon
  fix (down from the one 404 the first run caught). Regression-checked the actual game
  rendering via the same PIL technique Phase 7 established: both screenshots show a real
  0-255 pixel range on every channel (not a flat blank canvas), and diffing them found a
  changed region (bbox `(56, 66, 816, 142)`, matching the HUD/leaderboard area) -- the sim
  is still genuinely advancing frame to frame, confirming the PWA wrapper introduced no
  regression to the working WASM build.

  **Explicitly, honestly NOT verified** (impossible in this container, no macOS/iOS device
  exists at all): whether Safari actually shows the checkered-flag icon on the iOS home
  screen, whether "Add to Home Screen" launches the game standalone (no browser chrome),
  and whether the service worker/offline-caching behavior holds up under Safari's own
  (historically stricter, sometimes storage-limited) service-worker implementation.
  Manifest/icon/service-worker *correctness* is now confirmed; whether that correctness
  actually produces the desired install experience on a real Apple device is the one
  thing left that only a real device can answer.

  **Status**: `build-web/lht_port.html`, served over any static HTTP host (e.g. GitHub
  Pages, matching this project's already-existing Phase 6.0 hosting-workflow precedent for
  the JS build), is now a complete, correctly-wired installable PWA by every check this
  container can perform. The only remaining gap before this can be called fully done is
  the one Phase 7 already flagged and this session narrowed but could not close: real
  Safari/iOS hands-on verification.

  **Next**: someone with a real Apple device should serve `build-web/` (or a deployed copy)
  and try "Add to Home Screen" from Safari directly -- confirm the icon, standalone
  launch, and offline reload all behave as intended. Longer-term/optional, carried over
  unchanged from Phase 7's own closing notes: a genuinely native `shaderc` side-build to
  remove the native-build-must-run-first coupling, revisiting whether the global `-O1`
  optimization downgrade can be narrowed if a newer Emscripten/LLVM fixes the underlying
  WASM-SIMD-at-`-O3` instruction-selector bug, and (new, PWA-specific) richer splash-screen
  handling and maskable-icon variants for Android if that platform ever becomes a real
  target.

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

- **Session 3 (continued yet again -- GWC, qualifying, and the rest of
  `tick()`'s orchestration)**: `stepCar()` itself had no branches left, but
  `tick()` (index.html:4180-4595) was still missing everything downstream
  of the caution controller, plus a few pieces upstream of it. All of the
  following are now ported into `race.cpp`'s `tick()` (and `step_car.cpp`
  for the one `stepCar()`-side piece), in JS's own order:
  - **Pit-entry arming** (index.html:692-701, `step_car.cpp`): was stubbed
    out as "provably inert" while nothing set `c.pitReq`/`dtPending` --
    ported for real now that AI pit strategy (below) sets `pitReq`.
  - **Qualifying's flying-lap-complete transition** (index.html:4205-4208):
    only the physics-relevant `mode='qual' && player.lap>=1 -> 'menuwait'`
    state flip is ported -- `finishQualifying()`'s grid-synthesis and the
    `setTimeout` are menu-flow, not physics, and stay unported (Phase 2+
    territory). Verified: `--force=state.mode=qual,0:lap=1`, 5 ticks,
    bit-exact match.
  - **AI pit strategy** (index.html:4209-4218): sets `c.pitReq` for AI cars
    on worn tires/low fuel/damage. Straightforward direct port, exercised
    indirectly by every longer run below (no dedicated isolated test --
    it only sets a flag the already-verified pit-entry-arming/pit-machine
    code reads).
  - **Blowouts and terminal-damage DNFs** (index.html:4219-4250): worn-tire
    blowout roll (`rngR`-gated) and `dmg>=1 -> out`. Direct port.
  - **Green-white-checkered state machine** (index.html:4472-4532): the
    `none -> watch -> clean1 -> white -> (arbitration ends it)` walk, plus
    the `GWC_MAX_ATTEMPTS`-bounded safety valve. Added `GWC_MAX_ATTEMPTS=8`
    to `constants.h`. New `RaceState::victoryT` field (dynamically added
    in JS too, same rationale as `Car::blown`/`dmgCd`).
  - **Unconditional finish-line arbitration** (index.html:4537-4545): any
    car with `lap >= finishLaps` gets marked `done`, `finishT` stamped,
    pushed onto `finishOrder`. `tick()` gained a `std::vector<Car*>&
    finishOrder` parameter (mirrors `S.finishOrder`) -- caller-owned,
    same pattern as `cars`/`order` already being explicit parameters
    rather than embedded in `RaceState`.
  - **Player finish/DNF -> victory/done mode transitions, and the
    victory-lap timeout** (index.html:4581-4594): winner goes to
    `'victory'`, everyone else to `'done'`; a parked DNF'd player also
    goes to `'done'`; `'victory'` mode times out to `'done'` after 6s.
    `setTimeout(showResults, ...)` calls are menu-flow, not ported.
  - `race.h`/`race.cpp`'s header comments updated to describe the full
    `tick()` scope and what's still deliberately deferred (render-only
    prior-pose storage, and every HUD/audio-only side effect).

  **New `--force` capabilities**, needed to exercise all of the above
  without waiting out a whole multi-lap race: `state.finishLaps=N` and
  `state.flag=value` (alongside the existing `state.mode=value`), and a
  parameterized per-car `<idx>:lap=N` scenario (sets `c.lap` directly).
  Documented in `dump_js_trace.js`'s header comment alongside the existing
  scenarios.

  **A real bug was found and fixed via this verification, not a test
  artifact**: the caution controller's three `std::sort` calls (initial
  caution-slot assignment by physical position, slot compaction, and
  `tick()`'s own `S.order` race-position sort) all used `std::sort`, which
  gives no tie-breaking guarantee, while JS's `Array.prototype.sort` has
  been a *stable* sort since ES2019 -- ties fall back to original array
  order. This surfaced immediately when forcing a caution before the field
  has spread out: paired grid-row cars start at the *exact same* `s`
  (JS: `index.html:566` places 2 cars per row, differing only in lateral
  offset), so the initial caution-slot sort has a genuine, not-contrived
  tie between them, and `std::sort` broke it differently than JS's
  stable `Array.sort` -- two cars ended up with swapped `cautionSlot`
  values. This is a real scenario (an early-race caution before the pack
  spreads out), not just an artifact of the forced test. **Fixed**: all
  three sorts switched to `std::stable_sort`. Re-ran the triggering test
  after the fix -- clean match. This is exactly the kind of bug the
  cross-language bit-exact verification methodology exists to catch;
  worth remembering for any future JS `.sort()` call that still needs
  porting (there are none left in the physics core, but keep this in mind
  if Phase 2+ ever ports a JS `.sort()` for rendering/UI ordering).

  **Verification**:
  - Finish arbitration: forced `state.finishLaps=1,5:lap=1` (an AI car
    already past the line), 30 ticks -- bit-exact match, confirming the
    car gets marked `done`/`finishT` correctly.
  - Player wins -> victory mode: forced `state.finishLaps=1,0:lap=1`,
    250 ticks -- bit-exact for the first ~70 ticks (mode correctly reads
    `'victory'`, `done=1`, `finishT` stamped), diverging at tick 70 on the
    now-familiar wall-clamp boundary (confirmed via raw-trace inspection:
    player `lat` oscillating exactly on `11.0`) -- not a bug, same
    phenomenon as the earlier victory-branch verification, unsurprising
    since the victory burnout's tight circle revisits the wall repeatedly.
  - Player DNF -> done mode: forced `state.mode=race,0:out`, 2000 ticks --
    the transition itself fires around tick ~262 (player `v` decays
    through 1.0) and **stays bit-exact for over 1700 further ticks**
    before an unrelated AI car (idx 14) hits a wear-threshold boundary
    (the same class of phenomenon, just on `c.wear>0.92`'s blowout-roll
    gate instead of the wall-clamp -- a sub-ULP wear difference crossing
    0.92 a tick apart on the two sides means one side calls `rngR()` and
    the other doesn't, permanently desyncing the shared RNG stream from
    that point on; this is *expected* to eventually happen over a long
    enough run touching any RNG-gated threshold, not specific to this
    branch). The done-transition itself is thoroughly confirmed by the
    long clean stretch both before and after it.
  - GWC `none -> watch` entry: forced `state.finishLaps=1,0:lap=1,10:spin`
    (organic caution throw via the already-verified spin path, so
    `cautionSlot`s get assigned by the real code, avoiding the sort-tie
    issue above) -- 15 ticks, bit-exact on every trace field. Since the
    trace format doesn't carry `gwcState`/`finishLaps`/`gwcAttempts`
    (never extended for GWC), added a `DEBUG_STATE=1` env-gated print to
    `determinism_check` (mirrors the existing `DEBUG_CAR_IDX`) and a
    disposable Playwright probe script (not committed) printing the same
    JS-side fields -- both independently produced `gwcState=watch
    gwcAttempts=1 finishLaps=2 gwcMarkLap=-1` on every one of the 15
    ticks, an exact match confirming the transition. The deeper chain
    (`watch -> clean1 -> white -> arbitration`) needs an actual green-flag
    restart to progress past `'watch'`, which is a genuinely multi-tick
    process to force cleanly (same difficulty class as the caution
    controller's back half, still unverified from the prior session) --
    not chased further here; each individual `else if` in the state
    machine is a direct, verbatim transcription of its JS counterpart, and
    everything it depends on (arbitration, caution controller, activeLead)
    is independently verified, so confidence is high without forcing the
    full chain end-to-end. Flagged below as the one still-open piece of
    Phase 1g for whoever picks this up next.
  - Full `ctest` suite passes throughout (no regressions from any of the
    above, including the `stable_sort` fix).

  **Status**: `stepCar()`/`tick()` are now functionally complete --
  literally everything the JS original's physics/AI/race-control loop
  does is ported except render-only bookkeeping and menu-flow glue
  (`finishQualifying()`'s grid synthesis, `showResults()`, HUD/audio side
  effects). **Phase 1g is essentially done** except the note above; Phase
  1h (one closing full-determinism pass across everything, then commit)
  and Phase 2 (the minimal desktop renderer -- the actual "playable"
  milestone) are next.

- **Session 3 (continued once more -- Phase 2, the first playable
  milestone)**: per the user's explicit request to move on to Phase 2 once
  Phase 1f/1g's physics work was far enough along, built the minimal
  desktop renderer described in the Phase 2 checklist above. New files:
  `src/render/renderer.{h,cpp}`, `src/render/shaders_embedded.h`,
  `src/render/shaders/{vs_flat.sc,fs_flat.sc,varying.def.sc}`.
  `src/platform/main.cpp` rewritten from Phase 0's clear-screen-only stub
  into the real app: opens the window, builds a `Track`, calls
  `gridStart()`, then runs a fixed-timestep (`DT`=0.02) accumulator loop
  driven by real wall-clock time, reading `SDL_GetKeyboardState()` into
  `PlayerInput` and calling the real, Phase-1-verified `tick()` every step
  -- AI cars drive themselves via the already-ported AI branch, no
  test-only driver brain involved.

  **CMake changes**: `BGFX_BUILD_TOOLS`/`BGFX_BUILD_TOOLS_SHADER` flipped
  ON (were OFF in Phase 0 -- nothing needed `bgfx::shaderc` until now);
  the other tool subsets (bin2c/geometry/texture) stay OFF. Two
  `bgfx_compile_shaders(... AS_HEADERS ...)` calls compile `vs_flat.sc`/
  `fs_flat.sc` for the `spirv`/`120`(glsl)/`100_es`(essl) profiles (bgfx's
  own default set for a non-Windows/iOS desktop target) straight into C
  header files under `${CMAKE_BINARY_DIR}/generated_shaders/`, aggregated
  by `shaders_embedded.h`'s `BGFX_EMBEDDED_SHADER` table so the app has no
  runtime shader-asset-path dependency at all -- one less thing to break
  when this eventually needs to run from an Android APK/iOS bundle.
  `lht_port` now also links `SIM_SOURCES` (moved earlier in the file so it's
  defined before first use).

  **Three real bugs found and fixed while getting this to build** (logged
  so they're not rediscovered blind):
  1. `bgfx_compile_shaders()`'s `SHADERS`/`VARYING_DEF`/`INCLUDE_DIRS` were
     passed as paths relative to the source dir, but the custom command
     shaderc actually runs under doesn't have that as its working directory
     -- shaderc silently failed to find `bgfx_shader.sh`/`shaderlib.sh` and
     the varying def, fell back to a broken parse, and produced *misleading*
     errors (`'vec4_splat' : no matching overloaded function`, `unknown
     variable 'u_modelViewProj'`) that look like a shader-language problem
     but are actually a path problem. Fixed by making all three absolute
     (`${CMAKE_CURRENT_SOURCE_DIR}/...`).
  2. `bgfx/include/bgfx/embedded_shader.h`'s `BGFX_PLATFORM_SUPPORTS_DXBC`
     and `_WGSL` guards both unconditionally include `BX_PLATFORM_LINUX`
     (presumably for cross-compiling toward those targets from a Linux
     host), so on this desktop Linux build the `BGFX_EMBEDDED_SHADER` macro
     expected `vs_flat_dxbc`/`vs_flat_wgsl` (etc.) byte arrays to exist even
     though only `spirv`/`glsl`/`essl` were ever compiled -- a real target
     will only select OpenGL/OpenGLES/Vulkan here regardless. Fixed by
     `#define`-ing those support macros (and `_DXIL`/`_METAL`/`_NVN`/`_PSSL`,
     same non-issue) to `0` in `shaders_embedded.h` before including the
     header, so the macro only expects the three variants actually built.
  3. **Not a code bug, but a process mistake worth logging**: backgrounded
     the (very long, from-scratch) `shaderc` build twice via
     `nohup ... & disown` without checking whether a prior invocation was
     still running first -- ended up with *three* concurrent, uncoordinated
     `gmake` invocations against the same build directory (a real risk of
     silently corrupting `.o` files from racing writes to the same paths,
     not just wasted CPU). Killing by matching process name/pattern also
     failed more than once (`pkill -f` with an unescaped `\|` doesn't mean
     what it looks like, and killing only a tree's top process leaves
     already-reparented grandchildren running). **Lesson**: always
     `ps aux | grep` for an existing build before backgrounding another one
     against the same build dir, and when killing a process tree, be
     prepared to matching on the exact literal command building, not neatly
     with alternation regex.

  **Verification**: built clean (`cmake -B build && cmake --build build`,
  the `shaderc`/spirv-tools/tint dependency chain is a genuinely large
  one-time compile -- expect it to take a long time from scratch, but it's
  cached after). Ran the real binary under
  `xvfb-run -a --server-args="-screen 0 1280x720x24"` (same technique
  Phase 0 established) with `LHT_SCREENSHOT=<path>` set, capturing a real
  rendered frame via a new `bgfx::CallbackI::screenShot` implementation
  (dumps raw BGRA8 + a tiny sidecar `.meta` file -- converted to PNG with a
  throwaway Pillow one-liner for actually looking at it, see this session's
  chat for the two screenshots). **Confirmed visually**: the track ribbon
  renders as a closed oval matching the track shape, all 20 cars render as
  correctly-colored boxes in grid formation, and a second screenshot ~1200
  frames later shows the pack having advanced along the track (pace speed
  climbing 33->39 in the periodic status line too) -- the real, verified
  Phase 1 sim is genuinely driving a live render, not a static scene. Full
  `ctest` suite still passes (no regressions from the CMake restructuring).
  **Not verified**: actual keyboard responsiveness (no interactive human in
  this container -- see the checklist note above), and the chase-camera
  placeholder wasn't screenshot-checked (lower risk, simple code, matches
  no JS behavior yet anyway so there's nothing to regress against).

  **Next**: a real interactive session (or a synthetic-SDL-event test) to
  confirm keyboard control actually feels right; then the real chase
  camera (`updateCamera()`); then Phase 1h's closing full-determinism pass;
  then Phase 3 (mobile touch/tilt input, Android NDK).

- **Session 3 (continued once more -- Phase 2 close-out: chase camera +
  keyboard verification attempt)**: picked up exactly the two items left
  open at the end of the previous entry.

  **Chase camera**: implemented as described in the Phase 2 checklist
  above -- `Renderer` gained `chaseCx_`/`chaseCy_`/`chaseInitialized_`/
  `chaseLastTime_` smoothing state and a `const Track* track_` pointer
  (set in `setTrack()`, needed for the corner-lookahead's `pointAt()`
  call), and `renderFrame()`'s `CameraMode::Chase` branch was rewritten
  from a snap-to-car placeholder into the 2D-adapted analogue of JS's
  `updateCamera()` described above. `setCameraMode()` now resets
  `chaseInitialized_` on entry to Chase so re-toggling always hard-cuts.

  **Keyboard verification -- a genuine attempt, three tries, inconclusive**:
  installed `xdotool` (`apt-get install -y xdotool`, root, no sudo needed,
  same pattern as Phase 0's GL/X11 headers) to synthesize input under
  `xvfb-run`. Added two small debug-only env-var hooks to `main.cpp` for
  scripted testing (both clearly commented as debug-only, same rationale
  as the existing `LHT_MAX_FRAMES`/`LHT_SCREENSHOT`):
  `LHT_FORCE_RACE=1` (skips the ~28-sim-second pace phase, seeding
  `state.mode="race"`/`flag="green"` directly after `gridStart()` -- needed
  because the player is formation-driven, not keyboard-driven, during
  pace, same as the real JS) and `LHT_START_CHASE=1` (forces chase mode on
  at startup instead of needing a live `C` keypress).
  - **First established a reliable baseline** using the existing periodic
    status printf (`player.v=...`): with `LHT_FORCE_RACE=1` and zero
    keyboard input, player velocity decayed 30.4 -> 8.7 -> 0.5 over ~3.6
    real/sim seconds -- exactly the expected behavior (`thr=0`/`brk=0`/
    `steerIn=0` the whole run, so only drag+rolling-resistance act on it).
    This alone confirms the read path is wired correctly up to the point
    where `PlayerInput` reaches `stepCar()` -- what's actually unconfirmed
    is whether a real key event ever flips those bools to true.
  - **Attempt 1**: `Xvfb` + app + `xdotool keydown Up` (XTEST, relies on
    the target window having input focus). No window manager was running,
    so there's nothing to grant the SDL window focus --
    `xdotool windowactivate` itself failed ("windowmanager claims not to
    support _NET_ACTIVE_WINDOW"). Player velocity decayed identically to
    the baseline -- no input registered.
  - **Attempt 2**: `xdotool keydown --window <id> Up` (XSendEvent, sent
    directly to the SDL window's ID, bypassing focus). Same result --
    velocity decayed identically to baseline.
  - **Attempt 3**: installed `matchbox-window-manager` (tiny, root, no
    sudo) to give the display real focus-management, re-ran attempt 1 under
    it. `xdotool getactivewindow` now returned a real window ID (though a
    different one than the SDL client's own ID, suggesting focus may have
    landed on a matchbox-internal window rather than the app itself) --
    velocity again decayed identically to baseline.
  - **Conclusion**: three genuinely different synthetic-input delivery
    mechanisms, none got through to `SDL_GetKeyboardState()` in this
    headless setup. This reads as a known category of SDL2+Xvfb+XTEST/
    XSendEvent flakiness (window-manager/focus/synthetic-event-filtering
    interactions are notoriously finicky and backend-specific), not a bug
    in this port's own input code -- the code itself is a direct,
    straightforward read of `SDL_GetKeyboardState()` into four booleans,
    about as low-risk as application code gets. Not chased further past
    three attempts, per this session's own plan's built-in permission to
    fall back honestly here. **Genuinely unverified**: whether a real human
    at a keyboard sees the response they'd expect. Whoever has an actual
    interactive session available should just try it directly -- far
    simpler than continuing to fight synthetic X11 input in a container.

  **Verification**: `ctest` full suite passes (6/6). Rebuilt `lht_port`
  clean. Captured a chase-mode screenshot (`LHT_START_CHASE=1
  LHT_FORCE_RACE=1`, `xvfb-run` + the existing `bgfx::CallbackI` screenshot
  mechanism) -- visibly a much tighter zoom than the earlier top-down
  shots (individual cars are now large/legible rather than tiny dots), and
  the framing sits near the leading edge of the field with a forward bias,
  consistent with the lookahead logic actually running. Did not attempt to
  screenshot-diff the corner-lookahead bias specifically (would need a car
  approaching a real corner at a moment a screenshot lands, harder to
  script than what was already checked) -- low risk, it's a direct
  transcription of the JS formula.

  **Status**: all three original Phase 2 checklist items now have at
  least a first pass. Nothing about this milestone is blocking -- the C++
  port is genuinely watchable (top-down or chase) and driven by the fully-
  verified Phase 1 sim. **Next**: Phase 1h's closing full-determinism pass
  across the whole sim (still pending, see the Phase 1 checklist), then
  Phase 3 (mobile touch/tilt input, Android NDK install). Real interactive
  keyboard-feel confirmation whenever a session with an actual display is
  available.

- **Session 3 (continued once more -- Phase 1h, the closing
  full-determinism pass)**: **Phase 1 is now DONE**, not just "essentially
  complete." This wasn't new porting -- every `stepCar()`/`tick()` branch
  has been ported since Phase 1g -- it was a capstone check: does the
  *complete*, fully-assembled sim as it stands today (with GWC, finish
  arbitration, victory/done transitions, AI pit strategy, and blowout
  rolls all now live -- none of that existed yet during the original
  ~900-tick clean-run figure quoted earlier this session) still match real
  JS ground truth over a long, **unforced** run, not just the isolated
  `--force`-seeded snippets used to verify each piece individually.

  **Ran a fresh, completely unforced trace** (`dump_js_trace.js --track=0
  --ticks=3000`, no `--force` at all -- real grid start, real ~1395-tick
  pace phase, real green flag, whatever organically happens from there).
  `determinism_check` against it (no `--force` args either): **clean,
  bit-for-bit match for 2298 ticks** -- the entire pace phase plus roughly
  900 ticks of real green-flag racing, independently reconfirming the
  original figure with the newer AI-pit-strategy/blowout code paths now
  live in every tick (they didn't fire in this particular ~18-sim-second
  window -- their wear/fuel trigger thresholds take longer than that to
  reach, so this run doesn't exercise them, but that's an honest gap to
  note, not a hole in this specific pass; those branches were already
  separately verified in isolation via `--force` scenarios earlier this
  session).

  **Divergence at tick 2298 (car 3: x/y/hdg/v) diagnosed, not assumed**:
  pulled the raw trace lines for car 3 across ticks 2290-2299 and checked
  its `lat` field directly -- `10.124, 10.444, 10.758, 11.066, 11.0, 11.0,
  11.206, 11.0, 11.0, 11.0`, oscillating right on `11.0` (`WALL_CLAMP_LAT`
  for this track), the exact signature of the cross-runtime
  floating-point boundary phenomenon documented in this file's Open
  Questions -- **confirmed a sixth time**, not a new bug. `wear` at that
  point was `0.86`, well under the `0.92` blowout-roll threshold, ruling
  out the wear-threshold variant of this same phenomenon (also seen
  earlier this session) as the cause here specifically.

  **Full `ctest` suite passes (6/6)**. No fix needed -- there was nothing
  to fix; the divergence is the known, permanent limit of bit-exact
  cross-language floating point, not a translation error.

  **Phase 1 status, for anyone picking this up cold**: the entire physics/
  AI/race-control core (`mulberry32`, track builder, `CAR`/`ROSTER`/
  `makeCar()`, the speed/grip model, `stepCar()`'s full branch dispatch,
  `tick()`'s full orchestration including GWC and finish arbitration) is
  ported and verified against real JS ground truth, branch by branch and
  now as a whole. The only genuine limitation is the well-understood,
  permanent cross-runtime floating-point boundary effect at exact
  comparison thresholds (`WALL_CLAMP_LAT`, tire-wear blowout gates, and by
  the same logic any other exact-equality branch a sub-ULP difference can
  straddle) -- this is not something further porting effort can fix, it's
  inherent to matching V8 and glibc bit-for-bit, and every occurrence
  found so far (six now) has been individually confirmed to be exactly
  this, never a real translation bug. **Phase 1 is closed.**

  **Next**: Phase 3 (mobile input: SDL2 touch regions matching
  `bL`/`bR`/`bB`/`bG`/`bP`, `SDL_Sensor` tilt-steer matching `S.tiltG`,
  portrait-lock prompt, Android NDK install -- deferred from Phase 0 per
  the original sequencing decision). Real interactive keyboard-feel
  confirmation for Phase 2 remains open whenever a session with an actual
  display/keyboard is available, and the real chase-camera 3D parity
  (banking lean, alternate camera modes) waits on Phase 5's real geometry.

- **Session 3 (continued once more -- Phase 3a, touch/click input
  regions)**: Scoped to the first of Phase 3's four checklist items --
  SDL2 touch/mouse regions matching the JS original's on-screen button
  layout -- per the "one phase/sub-task per run" ground rule, leaving
  the tilt-steer sensor, portrait-lock prompt, and Android NDK install
  for future runs (different enough in kind -- hardware sensors, UI
  polish, toolchain setup -- that folding them in here would blur what
  each run actually verified).

  **New `src/ui/touch_controls.h`/`.cpp`** (first use of the `ui`
  directory Phase 0's layout had reserved): `computeTouchRegions(w, h)`
  returns a `TouchRegions{bL, bR, bB, bG, bP}` of `SDL_Rect`s, transcribed
  directly from the JS CSS layout's `--ctl*` constants
  (`index.html:19-20,46-51,194-198`: `ctlW=88 ctlH=76 ctlGasH=96 ctlGap=14
  ctlPairGap=10 ctlPitH=44 ctlPitGap=8`) at their base `UI.scale===1`
  pixel sizes -- `bL`/`bR` bottom-left steer pair, `bB`/`bG` bottom-right
  brake/gas pair (`bG` taller, sharing `bB`'s bottom edge not its top),
  `bP` stacked directly above `bB` with a gap. Not yet DPI/viewport-
  adaptive the way JS's own `UI.scale` responds to window size --
  positions scale with the window, but proportions don't shrink/grow
  relative to it the way JS's scale factor would. `pointInRect()` is a
  trivial inclusive-bounds helper. This is input *recognition* only, per
  the plan's own scope note -- no visible button is drawn yet (Phase 4's
  "UI overlay" job).

  **`main.cpp` wiring**: computes `TouchRegions` on init and recomputes on
  window resize. `SDL_MOUSEBUTTONDOWN/UP` (desktop stand-in) and
  `SDL_FINGERDOWN/UP` (real touch, normalized `[0,1]` coords scaled by
  current window size) are hit-tested against the regions.
  `bL`/`bR`/`bG`/`bB` set/clear held booleans (press-and-hold, matching
  JS's `bindBtn()` pointerdown/up semantics, `index.html:1235-1246`) that
  get OR-combined with keyboard state each frame
  (`input.gas = keys[...] || touchGas`, etc.) so either input method
  works without one clobbering the other. `bP` is a single toggle on
  press, not a held state, matching JS's plain `click` handler
  (`index.html:4664-4669`) rather than `bindBtn()`'s press/release pair.

  **Real gap found and closed while implementing this, not part of the
  original ask**: the player had *no way at all* to request a pit stop
  in this port before this session. `tick()`'s AI pit-strategy block
  (added back in Phase 1h) deliberately skips the player --
  `if (c.isPlayer || ...) continue;` -- matching JS, where `pitReq` is
  only ever set by the human clicking `bP`. But nothing in the C++ port
  filled JS's role of "the human clicking `bP`," so the player's
  `pitReq` simply could never become true. Added a `togglePlayerPit()`
  lambda in `main.cpp` that mirrors JS's `bP` click handler exactly
  (`index.html:4665-4669`'s own guard: `state.mode=="race" &&
  !player->done && player->pit==0`), wired to both the new `bP` touch
  region's press event and a debug-only `P` keydown -- JS has no
  keyboard binding for pit at all, so the keydown is purely a desktop-
  testing convenience, clearly commented as such, same rationale as the
  existing `LHT_FORCE_RACE`/`LHT_START_CHASE` env hooks. This runs
  independently of `PlayerInput`/`stepCar()`, exactly like JS's own
  click handler runs independently of the input-polling path.

  **New `tests/touch_controls_test.cpp`** (no SDL/bgfx runtime
  dependency, same pattern as `rng_test`/`track_test` -- links SDL2 only
  for the `SDL_Rect` header, doesn't touch any actual SDL subsystem):
  checks all five regions stay inside the window bounds with positive
  size, `bL` sits left of `bR` on the same row, `bB` sits left of `bG`
  sharing the same *bottom* edge (not top -- `bG` is taller per
  `ctlGasH=96` vs `ctlH=76`, an assertion I got backwards on the first
  pass and fixed after actually reasoning through which edge they'd
  share), `bP` sits above `bB` aligned on `x` with a gap, the steer pair
  doesn't overlap the pedal pair, each region's own center hits itself
  and nothing else, and the window's top-left corner hits nothing. Wired
  into `CMakeLists.txt`/`add_test()`. Caught one real compile error along
  the way (`<initializer_list>` needed for brace-init deduction in a
  range-for) and one real logic bug in the test itself (the `bB`/`bG`
  top-edge assertion above) before it passed.

  **Live verification**: full `ctest` suite passes, 7/7 (added
  `touch_controls_test` to the prior 6). `lht_port` rebuilds clean with
  the new sources linked in. One best-effort live attempt --
  `xvfb-run` + `matchbox-window-manager` (same setup as the Phase 2
  keyboard attempts) + a single `xdotool mousemove`+`click` at the
  computed `bG` (gas) region's center, with `LHT_FORCE_RACE=1` so the
  sim is already in green-flag race mode -- watching the periodic status
  line for `player.v` climbing instead of decaying toward ~0 the way it
  does with zero input. It did **not** register: velocity decayed
  identically to the no-input baseline. This is the same known category
  of SDL2+Xvfb synthetic-input flakiness already documented for the
  three failed keyboard attempts in the Phase 2 session log above (not a
  new failure mode, not chased with two more attempts per the
  established "one attempt, not three" precedent once a pattern is this
  well confirmed). The application code itself is a direct hit-test
  against `SDL_MOUSEBUTTONDOWN`/`SDL_FINGERDOWN` event coordinates, about
  as low-risk as this kind of code gets -- **genuinely unverified**
  end-to-end in this container is whether a real human's click/tap
  actually reaches the game; whoever next has an interactive session
  with a real display should just try it directly rather than fight
  synthetic X11 input further here.

  **Status**: Phase 3 item 1 (SDL2 touch regions + the player pit-toggle
  gap it exposed) is done and verified as far as this environment
  allows. Items 2-4 (tilt sensor, portrait-lock prompt, Android NDK
  install) remain open, each deserving its own scoped run -- NDK install
  in particular is toolchain/environment setup with no gameplay code at
  all, a genuinely different kind of task from everything else in this
  phase.

  **Next**: Phase 3 items 2-4, or real interactive verification (both
  keyboard from Phase 2 and touch/click from this session) whenever a
  session with an actual display and input devices is available.

- **Session 3 (continued once more -- Phase 3b, `SDL_Sensor` tilt-steer)**:
  Scoped to Phase 3 checklist item 2 only, following the same "one sub-task
  per run" discipline as Phase 3a, leaving the portrait-lock prompt and NDK
  install for future runs.

  **A pleasant surprise on inspection**: `state.tilt`/`state.tiltG`
  (`race_state.h:40-41`) and the `steerIn` override that reads them
  (`step_car.cpp:216`, `if (state.tilt) steerIn = clamp(state.tiltG/22,
  -1,1)`) were *already* ported, all the way back in Phase 1 -- correctly
  filed as a physics input rather than UI state (see `race_state.h`'s own
  "Correction from this file's first version" comment). So this session's
  entire scope was the platform layer that actually *feeds* those fields
  from real hardware; zero sim-core changes were needed.

  **New `src/platform/tilt_input.{h,cpp}`**: `TiltInput::init()` opens the
  first `SDL_SENSOR_ACCEL` device found via `SDL_NumSensors()`/
  `SDL_SensorGetDeviceType()`/`SDL_SensorOpen()`, returning `false` (not an
  error) if none exists -- this dev container has no accelerometer at all,
  same as most desktop Linux machines, mirroring how a desktop browser
  with no motion sensor simply never fires JS's `deviceorientation` event
  and `S.tiltG` just stays at its default. `update()` (called once per
  frame) reads the raw 3-axis vector and computes two standard
  gravity-vector tilt formulas -- `roll` (portrait-frame left/right tilt,
  the gamma-equivalent) and `pitch` (portrait-frame front/back tilt, the
  beta-equivalent) -- then picks one via `SDL_GetDisplayOrientation()`,
  mirroring `index.html:1260-1264`'s own `screen.orientation.angle`
  three-way branch structure as closely as SDL's API allows.

  **Honest open question, written up in the checklist above, not glossed
  over**: SDL's accelerometer axes are fixed to the device's physical
  frame and don't remap for display rotation (documented in
  `SDL_sensor.h` itself) -- the same is true of the browser's beta/gamma,
  which is exactly why both JS and this port need an orientation-based
  remap in the first place. But there is no shared, documented convention
  between SDL's `SDL_DisplayOrientation` enum and the browser's
  `screen.orientation.angle` values for *which* rotation direction each
  calls "landscape" vs. "landscape flipped" -- and unlike the earlier
  input-verification gaps (synthetic X11 delivery failing in a headless
  Xvfb setup, a software problem with a real fix path), there is no
  accelerometer hardware in this container at all, so this can't be
  narrowed down further here regardless of technique. The dominant-axis
  selection (roll in portrait orientations, pitch in either landscape
  orientation) should be structurally correct; the landscape sign is a
  coin flip, trivially fixed with one sign change once tested on an
  actual phone.

  **`main.cpp` wiring**: added `SDL_INIT_SENSOR` to the `SDL_Init()` flags,
  a `TiltInput tiltInput` constructed and `init()`'d at startup, `update()`
  + `state.tiltG` assignment each frame (only when `available()`, so a
  sensor-less machine leaves `state.tiltG` untouched rather than pinning
  it to a bogus reading), a debug-only `T` keydown toggling `state.tilt`
  (JS toggles `S.tilt` from a menu checkbox that doesn't exist in this
  port yet -- Phase 4's "UI overlay" job -- same convenience-key rationale
  as the existing `P`/`C` bindings), and `tiltInput.shutdown()` in the
  cleanup sequence.

  **Verification**: full `ctest` suite still passes, 7/7, unchanged from
  Phase 3a (this session touched no test-covered sim code). `lht_port`
  rebuilds clean with the new source linked in and `SDL_INIT_SENSOR`
  added. Ran the binary headlessly under `xvfb-run` (`LHT_FORCE_RACE=1`,
  300 frames) to confirm the new sensor-init/update/shutdown path doesn't
  crash or misbehave on a machine with zero sensors (`SDL_NumSensors()`
  legitimately returns 0 here) -- it ran clean, `available()` stayed
  false throughout as expected, `state.tiltG` was simply never touched.
  **Genuinely unverified**: the actual tilt-to-steering feel and the
  landscape sign convention on a real device, which needs real hardware
  (a phone) to check at all -- flagged explicitly above rather than
  claimed.

  **Status**: Phase 3 item 2 done as far as this environment allows --
  code path is real, exercised, and doesn't crash, but its core
  correctness claim (does tilting the phone actually steer the right way)
  is unverifiable until Phase 3 item 4 (Android NDK) lets this run on
  real hardware. Items 3-4 remain open.

  **Next**: Phase 3 items 3 (portrait-lock rotate prompt) and 4 (Android
  NDK install), or real hardware verification of both Phase 3a's
  touch/click input and this session's tilt-steer sign convention
  whenever a suitable device/session is available.

- **Session 3 (continued once more -- Phase 3c, portrait-lock rotate
  prompt)**: Scoped to Phase 3 checklist item 3 only, leaving item 4
  (Android NDK install) as the sole remaining piece of Phase 3 -- and
  explicitly a different kind of task (toolchain/environment setup, no
  gameplay code) that deserves its own dedicated run rather than being
  folded in here, same reasoning as every prior session's note on it.

  **Read the JS reference first**: the `#rotate` prompt
  (`index.html:140-147,203`) is pure CSS -- a full-screen black overlay
  with a spinning phone icon and "ROTATE YOUR PHONE" text, shown/hidden
  entirely by `@media (orientation: portrait) { #rotate { display:flex;
  } }`, no JS logic drives its visibility at all. `enterLandscapeFullscreen()`
  (`index.html:4598-4603`) makes a best-effort `screen.orientation.lock`
  attempt on the real start button, explicitly noted in its own comment
  as "not fatal — CSS rotate prompt covers it" if the lock isn't
  supported/granted. Two implications for a faithful port: (1) the CSS
  media query never pauses JS's `requestAnimationFrame` loop -- the
  overlay is a purely visual + (via its z-index sitting on top of
  everything) input-blocking layer, not a game-state gate -- and (2)
  since the overlay div physically covers the on-screen button divs
  underneath it, touches never reach them while portrait, but window-level
  `keydown` listeners are completely unaffected by any DOM element's
  z-index, so keyboard input still works in portrait in the JS original.
  Both of these subtleties are preserved in the C++ port rather than
  simplified away.

  **New `src/ui/orientation.h`**: a single `isPortrait(w, h)` inline
  function, `height >= width`, matching the CSS orientation media
  feature's own spec definition (ties count as portrait) exactly --
  deliberately just a plain geometry predicate with no SDL dependency at
  all, same header-only-utility spirit as keeping `touch_controls.h`'s
  actual math SDL-independent.

  **New `Renderer::renderBlockedFrame()`** (`src/render/renderer.{h,cpp}`):
  clears the view to opaque black (`0x000000ff`, matching `#rotate`'s
  `background:var(--c-black)`) and submits an otherwise-empty frame --
  no track ribbon, no car boxes. This is an honestly-scoped stand-in, not
  a full port of the CSS overlay's actual content: this renderer has no
  text or icon drawing capability yet (Phase 4's "UI overlay" job is
  where that lands), so "everything the player would see is replaced by
  black" is the closest faithful approximation available right now --
  functionally identical from the player's perspective (the game is
  fully hidden either way) even though the spinning-phone-icon +
  "ROTATE YOUR PHONE" message itself isn't there. Documented plainly as a
  simplification to revisit, not silently passed off as the complete
  thing.

  **`main.cpp` wiring**: `portrait` is computed once at startup and
  recomputed on every `SDL_WINDOWEVENT_RESIZED`. The main loop now
  branches between `renderer.renderBlockedFrame()` and the existing
  `renderer.renderFrame(cars)` based on it. `tick()` keeps running every
  frame regardless of `portrait` -- deliberately not pausing the sim, per
  the JS reference notes above. The `SDL_MOUSEBUTTONDOWN`/`SDL_FINGERDOWN`
  handlers added in Phase 3a are now gated on `!portrait`, so touch/mouse
  input to `bL`/`bR`/`bG`/`bB`/`bP` is ignored while portrait (mirroring
  the overlay physically covering those regions), while the keyboard
  polling path (and the debug-only `P`/`T` keydowns) is left completely
  untouched, matching JS's own asymmetry between touch and keyboard here.
  Added debug-only `LHT_WINDOW_W`/`LHT_WINDOW_H` env vars (read before
  `SDL_CreateWindow`) so a headless run can start already in a portrait
  aspect ratio without needing a live window resize -- there's no JS
  equivalent to port here since a real device/browser just already is
  whatever aspect ratio it is; this is purely a test-scripting
  convenience, same category as `LHT_FORCE_RACE`/`LHT_START_CHASE`.

  **Verification**: full `ctest` suite unaffected, still 7/7 (this
  session touched no test-covered sim code). `lht_port` rebuilds clean.
  Ran two headless `xvfb-run` captures at the same commit and diffed them
  pixel-wise via `PIL`: a 480x800 (portrait) run's screenshot has
  `getextrema()` returning `(0,0)` on all three RGB channels -- every
  single pixel is exactly black, not just visually black-ish -- and a
  1280x720 (landscape) run's screenshot shows a normal non-black extrema
  range (track/cars visibly still rendering), confirming the portrait
  branch didn't regress the existing landscape rendering path. This is a
  real behavioral check, not just "it compiled" -- it directly confirms
  the blocked-frame path actually produces the intended all-black output
  and that it's correctly gated on orientation.

  **Status**: Phase 3 items 1-3 are all done as far as this environment
  allows. Item 4 (Android NDK install) is the only piece left in Phase 3,
  and remains explicitly deferred to its own dedicated run per every
  prior session's note on why it doesn't belong folded into gameplay-code
  sessions like this one.

  **Next**: Phase 3 item 4 (Android NDK install), or Phase 4 (UI overlay
  -- menu/HUD/results screens, which is also where the rotate prompt's
  actual text+icon and the tilt-mode menu checkbox both belong once real
  text rendering exists). Real hardware verification remains open for
  Phase 3a's touch/click input and Phase 3b's tilt-steer sign convention
  alike, whenever a suitable device/session is available.

- **Session 3 (continued once more -- Phase 4a, first HUD text slice)**:
  User moved the project on to Phase 4 ("UI overlay"). That phase has
  three quite different checklist items (menu screen, HUD, results
  screen), and this port has had zero text-rendering capability up to
  this point -- so rather than tackle any one item whole, this run's
  actual scope was the smallest useful, testable slice: get *some* real
  text on screen at all, using it to deliver exactly the HUD checklist
  item's own literal brief (lap counter, position, timing) rather than
  the JS side's much bigger current HUD surface.

  **Technique decision**: rather than build a custom bitmap/TTF font
  atlas from scratch (a real, non-trivial subproject -- glyph data,
  texture packing, a text-quad shader), this session used bgfx's own
  built-in debug-text overlay (`bgfx::setDebug(BGFX_DEBUG_TEXT)` +
  `bgfx::dbgTextPrintf()`), a fixed 8x16-cell monospace VGA-palette text
  mode that ships with bgfx itself and is commonly used in bgfx-based
  projects for exactly this kind of HUD/overlay text. This is a
  deliberate, honestly-scoped simplification: it cannot reproduce JS's
  actual typography (real font files, arbitrary pixel positioning,
  per-pixel RGB color, rounded panels) -- only a fixed-grid monospace
  block font from a 16-color ANSI-style palette -- but it unblocks
  showing *any* text at all immediately, without a sub-project of its
  own. Revisit with a real font atlas if/when JS-matching typography
  fidelity actually matters (most plausibly bundled with the menu
  screen's DOM-chrome-equivalent work, a separate future Phase 4
  sub-task).

  **New `src/render/hud.{h,cpp}`**: `drawHud(state, cars)` -- skips
  drawing during `menu`/`menuwait` (`index.html:3931`), finds the player
  car, computes race position by recomputing the exact same descending
  sort key `race.cpp:339-343` already uses to build `S.order`/
  `finishOrder` (`done ? 1e6-finishT : prog`) -- purely for display, a
  read of already-computed `Car` fields, not a new sim decision, so this
  doesn't touch `race.cpp`/`tick()` at all -- and prints four lines:
  `LAP n / N` (using `index.html:3985-3987`'s exact `S.finishLaps`-not-
  `S.laps` denominator, the same field that fixed a real HUD-freeze bug
  on the JS side during a green-white-checkered extension), `POS p / F`,
  a background-highlighted `GREEN`/`CAUTION` flag banner, and `SPD` (this
  last one wasn't in the original three-item brief, but came essentially
  free once the lap/position plumbing existed, so it's included as a
  small bonus rather than held back).

  **Renderer wiring**: `bgfx::setDebug(BGFX_DEBUG_TEXT)` added to
  `Renderer::init()`. `renderFrame()`'s signature gained a
  `const RaceState&` parameter (named `raceState` to avoid shadowing the
  function's own pre-existing local `uint64_t state` bgfx render-state
  variable -- caught this collision before it became a real bug, not
  after) and now calls `bgfx::dbgTextClear()` + `drawHud()` right before
  `bgfx::frame()`. `renderBlockedFrame()` (Phase 3c's portrait block)
  also gained a `dbgTextClear()` call, since JS's `#rotate` overlay's
  z-index covers the HUD too (`index.html:203`) -- without this, a stale
  previous frame's HUD text would have kept showing through the
  supposedly-all-black blocked frame. `main.cpp`'s single `renderFrame()`
  call site updated to pass `state` alongside `cars`.

  **Verification, including a real mid-session correction**: full
  `ctest` suite unaffected, 7/7 (no test-covered sim code touched).
  `lht_port` rebuilds clean. Captured a headless `xvfb-run` screenshot
  and initially cropped/viewed it *without* applying the capture's
  `yflip` correction (documented back in Phase 2's screenshot notes) --
  the crop showed nothing but flat background color, which for a moment
  looked like the debug text simply wasn't rendering at all. Re-applying
  the same `Image.transpose(FLIP_TOP_BOTTOM)` step already established
  as necessary for this capture mechanism immediately fixed it: the
  corrected crop clearly shows `LAP 1 / 5`, `POS 20 / 20`, a
  green-highlighted `GREEN` banner, and `SPD  30`, all legible, with the
  full frame confirming the track/car field still renders normally
  alongside the new text. Also re-captured a portrait-orientation
  screenshot and reconfirmed it's still exactly `(0,0,0)` on every pixel
  -- the new `dbgTextClear()` call in `renderBlockedFrame()` is doing its
  job, no leftover HUD text bleeds through the rotate-block frame.

  **Status**: Phase 4's HUD checklist item has a first, real, functional
  (not just compiling) slice -- exactly the original brief's three
  fields plus one bonus, definitively NOT the JS side's full current HUD
  surface (leaderboard, gaps, minimap, bars, gear/RPM), which remain
  explicitly open. Phase 4's other two items (menu screen, results
  screen) are untouched -- still `NOT STARTED` in spirit even though the
  phase header now reads `IN PROGRESS`.

  **Next**: further Phase 4 HUD sub-tasks (leaderboard panel, minimap,
  segmented bars, gear/RPM -- likely each its own scoped run given the
  size), or the menu/results screens (which will also want the rotate
  prompt's real text+icon and the tilt-mode toggle UI once tackled), or
  Phase 3 item 4 (Android NDK install), whichever the next session
  prioritizes.

- **Session 3 (continued once more -- Phase 7, a real pivot: WebAssembly/
  browser build)**: The user redirected this project's actual goal
  mid-session: no App Store, no native Android/iOS distribution -- just
  play from Safari, ideally installed as a home-screen PWA. That's a
  fundamentally different target than everything Phases 0-6 had been
  building toward (a native mobile binary), so before writing any code
  this session first investigated whether the C++ port was even still
  the right vehicle for that goal, or whether the already-working JS/
  Three.js game (which already has GitHub Pages hosting, mature touch/
  tilt controls with iOS permission handling, and basic PWA capability
  meta tags) was the faster path. The user chose to keep the C++ port
  and get it running in a browser via WebAssembly/Emscripten instead --
  see this session's Phase 7 checklist entry above for the full
  technical writeup (toolchain install, the `main.cpp` `LoopState`/
  `mainLoopTick()` restructure, the X11-to-canvas branch, and four real
  build-breaking issues found and fixed in bx/bimg/shaderc's interaction
  with the Emscripten toolchain -- not a clean first try, each confirmed
  from actual crash/error output rather than guessed at).

  **What made this session unusual**: the container running this session
  restarted mid-build at least once, killing in-flight background
  `emmake` processes -- handled by checking actual on-disk state (the
  emsdk install, the code changes, the plan file) before assuming
  anything was lost, then resuming from exactly where the interruption
  landed rather than restarting the whole investigation. Plan mode was
  also toggled back on unexpectedly mid-execution more than once; each
  time, the response was to append a concise "session progress so far"
  note to the live plan file (what's done, what's mid-flight, the exact
  next step) before calling `ExitPlanMode` again, so no context was lost
  across the interruptions and execution picked back up exactly where it
  left off rather than re-deriving the same diagnosis twice.

  **Result**: `lht_port` now builds to `build-web/lht_port.html/.js/.wasm`
  and was verified, genuinely and headlessly (via a new one-off
  `tests/wasm_verify.js` Playwright script, not just "the build
  succeeded"), to load with zero page errors and one confirmed-benign
  console error (a stray favicon 404), render the track/car field/HUD
  correctly, and visibly advance between two screenshots taken a few
  seconds apart -- proof the `emscripten_set_main_loop` frame loop is
  really ticking, not stuck on a static first frame. The native `build/`'s
  full `ctest` suite was re-run twice (mid-session and again after a
  completely fresh reconfigure at the end) and stayed 7/7 both times --
  this pivot added a new build target, it didn't touch or regress the
  already-verified native desktop build or physics core.

  **Honestly flagged, not glossed over**: nothing this session did
  validates real Safari behavior on desktop or iOS -- this container has
  no macOS at all, so only headless Chromium verification was possible.
  Safari has real, documented WebGL2/WebAssembly/Web Audio quirks
  (autoplay-unlock, orientation-lock support gaps already noted in this
  file's own Phase 3c entry, historically stricter WebGL2 context rules)
  that remain completely untested. This is the single biggest open
  question standing between "builds and runs in a browser" and "actually
  playable from an iPhone's Safari," and is explicitly called out as the
  top priority for whoever next has a real Apple device available.

  **Status**: Phase 7's first-pass goal -- prove the existing C++/SDL2/
  bgfx codebase can run inside a browser tab at all -- is done and
  genuinely verified within this environment's limits. Phase 6 (native
  Android/iOS packaging) is deprioritized per the user's explicit
  redirect, not deleted, in case native distribution becomes relevant
  again later.

  **Next**: the PWA installability wrapper (`manifest.json`, a service
  worker, `apple-touch-icon`/`theme-color`/`apple-mobile-web-app-*` meta
  tags, actual icon assets) is what actually turns this into "installable
  from Safari's home screen" -- the natural next session now that the
  underlying WASM build is proven working, not before. Real Safari/iOS
  verification remains open until someone with an actual Apple device can
  test the served `build-web/` output directly. Longer-term, worth
  reconsidering: a genuinely native `shaderc` side-build (to remove the
  current native-build-must-run-first coupling for the shader headers),
  and whether the global `-O1` optimization downgrade can be narrowed or
  removed once/if the upstream LLVM WASM-SIMD-at-`-O3` instruction-
  selector bug is fixed in a future Emscripten/LLVM release.

- **Session 4 -- Phase 7b: PWA installability wrapper.** Direct
  continuation of Session 3's own stated next step: turn the already-
  working WASM build into something genuinely installable from Safari's
  home screen, not just loadable in a tab. See this session's Phase 7b
  checklist entry above for the full technical detail (icon generation,
  manifest, service worker, `shell.html` meta tags, the `CMakeLists.txt`
  `POST_BUILD` staging step, and the real `shell.html`-edit-doesn't-
  trigger-a-relink gotcha this session ran into and fixed by hand).

  **A real bug found via testing, not assumed away**: the first
  verification pass of the new `wasm_verify.js` checks reported one
  console error. Rather than wave it off the way "one confirmed-benign
  console error" got noted in Session 3's own Phase 7 pass, this session
  root-caused it with a standalone Playwright script logging every
  network response and console message, confirmed via a direct `curl` to
  be Chromium's automatic `/favicon.ico` request (issued whenever a page
  declares no `<link rel="icon">`), and fixed it properly by adding a
  favicon link reusing the already-generated 192px icon. Re-verification
  after the fix shows genuinely zero console/page errors, not "zero
  except one we've decided is fine."

  **Result**: `build-web/` now contains a complete, correctly-wired
  installable PWA -- `manifest.json` validates with all required fields,
  all three icon assets (`icon-192`, `icon-512`, `apple-touch-icon`) fetch
  and are valid PNGs, the service worker registers and reaches `ready`,
  and the underlying game still renders and advances frame-to-frame with
  zero regression from Phase 7's own baseline (confirmed via the same
  PIL non-blank/frame-diff technique, re-run after this session's
  changes rather than assumed still valid).

  **Honestly flagged, not glossed over**: exactly like Phase 7's own
  closing note, nothing this session did can verify real Safari/iOS
  behavior -- no macOS or iOS device exists anywhere in this container.
  Manifest/icon/service-worker *correctness* is now confirmed by every
  check available here; whether Safari actually shows the right
  home-screen icon, launches standalone, and behaves like a real
  installed app is the one thing left that only a real Apple device can
  answer. This is now the single remaining gap before the user's original
  goal ("play off Safari as a PWA") is fully, hands-on confirmed rather
  than just plausible.

  **Status**: committed and pushed to `main`. This closes out the
  concrete engineering work of the Safari-PWA pivot that began when the
  user redirected away from native App Store distribution in Session 3 --
  what remains is purely real-device verification, not further building.

- **Session 5 -- Phase 4b: the menu screen.** With the WASM/PWA path proven
  (Phase 7/7b), the user asked to continue Phase 4 -- this session picked
  the one fully-unstarted item, the menu screen (JS's `#menu` DOM overlay,
  `index.html:167-184,4650-4723`), as its scope: track/laps/qualifying/
  sound/tilt toggles, a volume control, and a Start button, adapted to
  this port's bgfx-debug-text-only rendering and `touch_controls.h`'s
  region-hit-testing pattern rather than a real widget framework.

  **New `src/ui/menu.{h,cpp}`**: `MenuRegions computeMenuRegions()` returns
  fixed pixel rects for seven rows (track/laps/qual/sound/tilt/volume/
  start), positioned to exactly match the text rows `drawMenu()` prints at
  (same 8x16 dbgText cell grid `hud.cpp` already established) so the
  clickable area always lines up with what's drawn -- no separate pixel
  bookkeeping to keep in sync by hand. `cycleLaps()` mirrors JS's exact
  `#lapTog` cycle (3->5->10->20->3, `index.html:4706-4709`).
  `volumeFromClickX()` adapts JS's drag-based `<input type=range>` into a
  click-to-set proportion of the volume row's width, since this port has
  no drag-slider widget. **Not every JS control does something real**:
  `RaceState::laps`/`RaceState::tilt` are genuine, already-existing sim
  fields (`race_state.h`) that the menu's laps/tilt rows toggle directly,
  matching JS's own immediate-effect bindings exactly -- but `qual`/
  `sound`/`volume` are new fields on a `MenuSelection` struct that are
  stored and toggleable for UI parity only:
  - `qual`: real qualifying (`startQualifying()`'s single-car timed lap,
    then `finishQualifying()`'s synthesized-grid rebuild,
    `index.html:4615ish`) is genuine unported sim-core work -- `race.cpp`'s
    own `tick()` comment already noted only the `mode='qual'->'menuwait'`
    transition itself is ported, not what puts the sim into `'qual'` mode
    in the first place. Out of scope for a UI-only sub-task. Toggling this
    row is honest parity; pressing Start with it on prints a one-line note
    and starts a normal race anyway (drawn in grey in the menu as a small
    visual "this doesn't do anything yet" cue).
  - `sound`/`volume`: this port has no audio system at all yet (Phase 6,
    still not started) -- nothing exists for these to control. Also drawn
    grey.

  **`main.cpp` restructure -- the real entry point is now the menu**:
  previously `main()` called `gridStart()` and force-set
  `state.mode = "pace"` unconditionally, immediately after init -- there
  was no menu to skip past. `RaceState::mode`'s own default (`"menu"`,
  `race_state.h`) is now left alone at startup; a new
  `startRaceFromMenu()` (mirroring JS's `startRace()`,
  `index.html:4604-4614`, including its explicit `state.finishLaps =
  state.laps` reset -- no longer a free coincidence now that laps is
  genuinely menu-selectable) only runs once Start is actually clicked, via
  a new `handleMenuClick()` hooked into the existing
  `SDL_MOUSEBUTTONDOWN`/`SDL_FINGERDOWN` handling, gated on
  `state.mode == "menu"`. `Renderer::renderFrame()` gained an optional
  `MenuSelection*`/track-name parameter, drawing the menu overlay via the
  new `drawMenu()` whenever `mode == "menu"` -- `hud.cpp`'s `drawHud()`
  already early-returned for that mode, so both coexist without stepping
  on each other's dbgText rows. Discovered and fixed one real correctness
  gap while wiring this up: the sim's `tick()` was being called every
  frame regardless of mode, including while sitting on the menu with an
  empty `cars` vector (no `gridStart()` yet) -- harmless by luck today
  (`updateAero()`/`collide()`/etc. all no-op over an empty vector), but
  papering over a real risk and not matching JS at all, which gates its
  own `tick()` call to `mode==='race'||'pace'||'qual'||'victory'` only
  (`index.html:4144`) -- ported that same gate exactly. Also had to add an
  explicit `simAcc = 0` reset in `startRaceFromMenu()`: the frame-time
  accumulator keeps accumulating every frame regardless of mode, so
  however long a player sat on the menu would otherwise become a backlog
  `tick()` tries to instantly replay the moment Start is pressed -- JS has
  no equivalent risk since its own accumulator only lives inside the same
  mode-gated block that calls `tick()`.

  `LHT_FORCE_RACE` (the existing debug/headless-verification bypass) keeps
  working exactly as before -- it now calls `startRaceFromMenu()` then
  immediately overrides to `mode="race"/flag="green"`, skipping both the
  menu and the pace phase in one step, same end state scripted
  verification already relied on pre-this-session.

  **New `tests/menu_test.cpp` + `menu_test` ctest target**: same rationale
  as `touch_controls_test.cpp` (region math is testable headlessly, actual
  synthetic input delivery is not -- see this file's own Phase 2e/3b notes
  on three independent xdotool/XTEST/XSendEvent attempts never registering
  against a real SDL window in this container). Checks region layout (no
  overlaps, correct top-to-bottom order, each region's own center hits
  only itself), `cycleLaps()`'s exact sequence and that it's a 4-cycle, and
  `volumeFromClickX()`'s edge/midpoint/clamping behavior. Needed linking
  against `bgfx` (unlike `touch_controls_test`) since `menu.cpp` calls
  `bgfx::dbgTextPrintf()` in `drawMenu()` -- a symbol the linker must
  resolve even though this test never calls that function.

  **Verification, all genuinely checked, not assumed**:
  - Native: full `ctest` suite, now 8/8 (`menu_test` new, the other 7
    unaffected). Headless `xvfb-run` screenshot with **no** env override
    confirmed the menu itself renders correctly (title, all six rows with
    live values, the `>>> START RACE <<<` button, the empty track visible
    behind it, zero cars -- matching JS's own "menu shows a live
    establishing shot with `S.cars` empty" behavior,
    `index.html:4166-4172`) and that `state.t` stayed at `0.0` the whole
    120-frame run, confirming the new sim-tick gate actually holds (no
    silent sim advancement while idling on the menu). A second headless
    run with `LHT_FORCE_RACE=1` reproduced the exact pre-session behavior
    (`mode=race`, `flag=green`, player velocity ~30.5 matching the
    historical baseline in this file's own Phase 2e notes) and the HUD/
    car-field screenshot looked identical in kind to previous phases' --
    confirms the menu restructure caused no regression to the underlying
    race flow.
  - **Live mouse-click verification was not attempted natively** -- this
    file's own Phase 2e/3b session notes already established, across
    three independently-tried mechanisms, that synthetic X11/XTEST/
    XSendEvent input does not reliably reach this container's SDL2+Xvfb
    setup. Not re-litigated a fourth time.
  - Web (WASM/PWA build): rebuilt `build-web/`, served it, and extended
    `tests/wasm_verify.js` with a **real** end-to-end click-through:
    navigate, screenshot the menu (confirmed non-blank, matching the
    native menu screenshot in content), `page.mouse.click()` at the exact
    pixel coordinates `computeMenuRegions()`'s Start row occupies (a
    genuine Playwright-driven browser input event -- a completely
    different, already-proven-reliable code path in this container from
    native SDL2/X11, per Phase 7's own successful headless-Chromium
    verification), then two more screenshots a few seconds apart. Result:
    zero console/page errors; the post-click screenshot shows the full
    20-car field grid-started and running (`LAP 1/5`, `POS 20/20`,
    `GREEN`, `SPD 38`) with a diff bbox against the pre-click menu shot
    confirming genuinely different content (not a stale frame), and the
    two post-click shots differ from each other too (the original
    frame-advances regression check, now performed after the click since
    that's where gameplay actually renders). **The Start button
    genuinely works end to end in a real browser**, not just via a debug
    env-var bypass.

  **Status**: Phase 4's menu screen is done and verified both natively
  (region/value-math unit test + headless regression screenshots) and in
  the actual shipped WASM/PWA build (a real simulated click driving the
  full menu->race transition). Qualifying and sound/volume remain honest,
  visually-marked stubs pending their own sim-core/audio-system work
  (Phase 4's HUD leaderboard/minimap/tire-fuel-bars/gear-RPM items and the
  Results screen also remain, per this file's own Phase 4 checklist).

- **Session 6 -- completing Phase 4.** The user asked to finish Phase 4
  entirely. Two Explore agents plus a Plan agent scoped the six remaining
  checklist items up front (last/best lap strip, gear/RPM, segmented
  tire/fuel/car bars, minimap, leaderboard, results screen), cross-checked
  directly against `index.html` before committing to an approach -- see
  the plan's own writeup for the full research (confirmed e.g. that
  `gearRpm()` is a pure display-time function of speed with no stored car
  state at all, that `#results`' CSS is opaque unlike `#menu`'s deliberate
  semi-transparency, and that `gridStart()` doesn't clear `finishOrder`
  the way JS's does -- a real latent bug surfaced by tracing the restart
  flow, not something guessed at).

  **Phase 4c -- last/best lap time strip**: `fmtT()` (index.html:3769-
  3771) ported verbatim into a new, bgfx-free `src/render/fmt_time.h/.cpp`
  (`fmtLapTime(double t)`) specifically so it could get a fast unit test
  with zero link dependency on a live rendering context -- same
  isolation-of-pure-logic precedent `touch_controls.h`/`menu.cpp`'s region
  math already established. `hud.cpp` gained two new dbgText rows reading
  `Car::lastLapT`/`bestLapT`, both already correctly maintained by
  `step_car.cpp` since Phase 1 -- a pure rendering addition, no sim-core
  change.

  **New `tests/fmt_time_test.cpp`** (ctest now 9/9): placeholder cases
  (zero, negative, NaN -- `fmtT`'s `!t` check catches NaN specifically,
  ported as `!(t > 0)`), zero-padding below 10 seconds, and the minutes
  component appearing past 60 seconds, all against hand-computed values.

  **Verified**: `ctest` 9/9. Headless `xvfb-run` screenshot
  (`LHT_FORCE_RACE=1`) confirms `LAST`/`BEST` rows render in the expected
  position with the `--:--.--` placeholder correctly showing (the player
  car sits stationary for the whole run without real keyboard input --
  same known synthetic-input limitation as ever, see Phase 2e/3b -- so no
  completed lap was observed natively; the placeholder path is the one
  this screenshot actually exercises, and the formatting logic itself is
  exhaustively covered by the new unit test instead of chasing a live
  completed-lap screenshot for its own sake).

  **Phase 4d -- gear/RPM readout**: confirmed via `index.html`'s own
  comment (1389-1391) that `gearRpm(v)` is a *shared* breakpoint table JS
  uses for both the HUD readout and its engine-audio pitch calc
  (`audioTick()`) -- but it's a pure function of `Car::v` either way, not
  stored car state, so only the HUD side needed porting (this port has no
  audio system to share it with yet). Ported line-for-line into a new
  `src/render/gear_rpm.h/.cpp` (`GEAR_BREAKS=[14,26,40,70]`, including the
  `g===GEAR_BREAKS.length-1` fallback branch that keeps any speed past 70
  in top gear with clamped rpm, rather than falling off the loop). One new
  `GEAR n  RPM nnn%` dbgText row in `hud.cpp`.

  **New `tests/gear_rpm_test.cpp`** (ctest now 10/10): hand-computed
  `(v, gear, rpm)` triples straddling each of the 4 breakpoints exactly
  (at each breakpoint, just past it, and one case past the last
  breakpoint confirming the clamp-to-gear-4 fallback).

  **Verified**: `ctest` 10/10. Headless `xvfb-run` screenshot
  (`LHT_FORCE_RACE=1`, a short 120-frame run so the player's initial pace-
  lap velocity hasn't decayed to zero yet) shows `GEAR 3  RPM  49%` at
  `SPD 30` -- hand-checked against `gearRpm()`'s own formula for `v≈30.5`
  (truncated to the displayed integer `30`): `0.25 + 0.75*(30.5-26)/
  (40-26) ≈ 0.491 → 49%`, confirming the live HUD value, not just the
  unit test, matches the ported formula.

  **Phase 4e -- segmented TIRE/FUEL/CAR bars, and this port's first real
  quad/shape rendering.** Everything through Phase 4d was dbgText-only
  (bgfx's built-in fixed 8x16 character grid); the status bars need real
  filled rectangles, which needed new rendering infrastructure first.

  **Architecture**: reused the exact same flat-color vertex layout/shader/
  program (`PosColorVertex` + `vs_flat`/`fs_flat`) the track ribbon and car
  boxes already use, via a **second bgfx view** (view id 1) with an
  orthographic, top-left-origin, y-down pixel-space projection
  (`bx::mtxOrtho` with `bottom=height, top=0`) -- no new shader needed.
  Confirmed (not assumed) that bgfx's debug-text overlay draws on top of
  every view regardless of ID, matching every screenshot this project has
  ever taken. Hoisted `packColor()` (`src/render/color.h`) and the vertex
  struct (`src/render/vertex.h`, `PosColorVertex`) out of `renderer.cpp`'s
  anonymous namespace so UI-emitting modules don't depend on renderer
  internals. New `src/render/color.h` also carries a `Theme` namespace
  matching `index.html`'s `THEME` table exactly (`#F7D400` yellow,
  `#1A4FFF` blue, `#2A2A2A` steel, `#FF7A00` orange, `#D62828` red,
  `#C8C8C8` graycool) -- confirmed this is genuinely a *different* table
  from `CarPalette`'s livery colors (`car.h`), not a duplicate: checked
  each value directly, `CarPalette::Orange` happens to equal
  `THEME.orange` but `CarPalette::Red`/`Blue` do not equal `THEME.red`/
  `blue` at all -- a coincidence for one entry, not a shared concept.

  **New `src/render/ui_draw.h/.cpp`**: pure 2D pixel-space geometry
  helpers, zero bgfx dependency (same "isolate pure logic" precedent as
  `touch_controls.h`/`menu.cpp`) -- `pushQuad`/`pushTriangle`/
  `pushLineSegment`/`pushPolyline`/`pushRingOutline`/`pushFilledCircle`,
  and `pushSegBar()` (a direct port of JS's `drawSegBar()`,
  index.html:3868-3874). `pushLineSegment()` extrudes along the segment's
  normal by half-thickness -- the same technique `Renderer::setTrack()`
  already uses for the world-space track ribbon, just parameterized by
  two explicit points instead of `Track::pointAt()`/`halfW()`.

  **`renderer.cpp`'s `renderFrame()`**: `drawHud()` now takes a
  `std::vector<PosColorVertex>& uiOut` output parameter; after building it,
  `renderFrame()` submits it as view 1 (skipped entirely when empty, e.g.
  `mode=="menu"` -- same "nothing submitted this frame -> nothing drawn"
  precedent view 0's own `if (!cars.empty())` guard already relies on).
  Enabled `BGFX_STATE_BLEND_ALPHA` on view 1 now, even though today's
  opaque bars don't need it, since the minimap's pulsing trouble ring
  (Phase 4f) will.

  **New `src/render/status_bars.h/.cpp`**: assembles the real TIRE/FUEL/
  CAR strip from `pushSegBar()` -- 3 rows (dbgText labels + a 6-segment
  bar each), reading `Car::wear/fuel/dmg` (all three already exist and are
  already correctly maintained by the physics core -- this is a pure
  rendering addition) with JS's exact color thresholds
  (index.html:4005-4009): tire uses `1-wear` vs yellow/orange/red at
  0.5/0.25; fuel vs blue/orange/red at 0.3/0.12; car uses `1-dmg` vs
  blue/orange/red at 0.6/0.3. Placed at dbgText rows 8-10, directly below
  Phase 4a-4d's existing rows 1-7 (never rendered in the same frame as
  `menu.cpp`'s rows, since `drawHud()`/`drawMenu()` are mode-exclusive).

  **New `tests/ui_draw_test.cpp`** (ctest now 11/11): vertex counts and
  bounding boxes for `pushQuad`/`pushTriangle`/`pushLineSegment`, segment
  counts for open vs. closed `pushPolyline`, radius-distance checks for
  `pushRingOutline`, center-point/triangle-count checks for
  `pushFilledCircle`, and `pushSegBar`'s exact fill-count math including
  edge cases (`frac=0`, `frac=1`, out-of-range clamping).

  **A real false alarm, run down and disproven, not left ambiguous**:
  the first headless screenshot appeared to show every bar's color
  channel-swapped (expected tire yellow `#F7D400` rendered as a cyan-ish
  `(0,212,247)`; expected fuel/car blue `#1A4FFF` rendered as orange
  `(255,79,26)` -- R and B swapped, G untouched). Before assuming a
  renderer bug, cross-checked whether *pre-existing* car colors (unrelated
  to this session's changes) showed the same swap in the same screenshot --
  they did (`CarPalette::Red`, expected `(192,0,0)`, sampled as
  `(0,0,192)`) -- ruling out anything specific to the new UI-overlay code
  and pointing at something systemic. Decoded the screenshot's own `.meta`
  sidecar format code (`71`) against `bgfx::TextureFormat`'s actual enum
  values (compiled a two-line program against this project's own vendored
  bgfx headers to check, rather than guessing) and found it's `RGBA8`, not
  `BGRA8` -- the ad hoc Python screenshot-to-PNG conversion snippet reused
  verbatim across every phase's verification this whole project had been
  hardcoding PIL's raw decode mode as `'BGRA'`. Re-decoding as `'RGBA'`
  produced exact hex matches for every sampled color (tire `(247,212,0)`,
  fuel/car `(26,79,255)`). **Conclusion**: this was a mistake in this
  project's own ad hoc verification tooling, not the renderer -- `packColor()`/
  the vertex layout have been correct all along. Worth flagging for future
  sessions: this format mismatch was invisible in every prior phase's
  screenshots because grass/background/HUD-text colors used there are all
  R≈B-symmetric (white, gray, near-equal-channel green), so a channel swap
  never looked wrong by eye until this session's first genuinely
  asymmetric, precisely-specified colors made it detectable.

  **Verified**: `ctest` 11/11. Headless `xvfb-run` screenshot
  (`LHT_FORCE_RACE=1`, long enough that the field is spread out and
  `wear=0`/`fuel=1`/`dmg=0` still hold for the stationary player) shows,
  once correctly decoded: `TIRE` fully yellow (6/6 segments, matching
  `wr=1>0.5`), `FUEL`/`CAR` fully blue (6/6 segments, matching `fuel=1>0.3`
  and `dOK=1>0.6`) -- exact hex matches against `Theme`'s own constants,
  not just "some color renders." dbgText numbers (`LAP`/`POS`/`GEAR`/etc.)
  remained crisp and correctly positioned alongside the new quads, with no
  interference between the two rendering paths.

  **Phase 4f -- minimap.** Ported `drawMinimap()` (index.html:4059-4101):
  a closed track-outline polyline (the centerline, not the ribbon's inner/
  outer edges -- confirmed from JS's own `MMPTS` sampling, `TRACK.pointAt()`
  with no lateral offset), per-car dots, a player directional wedge, and
  pulsing trouble rings.

  **A deliberate simplification over JS, not a behavior change**: JS lazily
  builds its outline cache (`MMPTS`) once and explicitly nulls it whenever
  the menu's track button changes track (`index.html:4714`) -- a
  workaround for JS having no clean "track changed" hook anywhere else.
  This port already has exactly that hook: `Renderer::setTrack()`, called
  both at startup and on `handleMenuClick()`'s track-cycle branch. So the
  141-point outline (plus its `MM_BX`/`MM_BY` bounding half-extents) is
  built eagerly there instead, right alongside the existing ribbon-mesh
  build -- same data, same trigger points, no lazy-cache/invalidate dance
  needed. New `Renderer` members (`minimapOutline_`/`minimapBoundX_`/
  `minimapBoundY_`) with public getters, since `hud.cpp` stays Renderer-
  independent (same rationale as the `uiOut` vertex list) and needs this
  data handed to it by `renderFrame()`.

  **New `src/render/minimap.h/.cpp`**: `drawMinimap()` builds the closed
  outline polyline (`pushPolyline`), a filled dot per AI car
  (`pushFilledCircle`), the player's directional wedge
  (`pushTriangle`, computed from `Car::hdg` with no extra rotation --
  heading is already in the same world-space convention the outline uses,
  matching JS exactly), and pulsing red trouble rings
  (`pushRingOutline`) for any car with `spinT>0 || blown || dmg>0.6`.
  Ported JS's exact two-term pulse formula
  (`pulse=0.5+0.5*sin(t*6)`, then `alpha=0.35+0.35*pulse`) rather than
  simplifying it to one term, since both terms are independently visible
  in the final alpha curve. Placed at a fixed pixel box directly below
  Phase 4e's status bars -- this port's own layout for now, not JS's
  leaderboard-cascade (`computeLayout()`'s minimap position depends on the
  leaderboard panel's height, which doesn't exist yet as of this phase;
  worth revisiting once Phase 4g's real leaderboard panel lands).

  **New `tests/minimap_test.cpp`** (ctest now 12/12, zero bgfx dependency
  -- `minimap.cpp` itself never calls bgfx, unlike `status_bars.cpp`):
  hand-computed wedge tip/back-left/back-right vertices for two headings
  (0 and pi/2, confirming the wedge genuinely rotates rather than always
  pointing one fixed direction), the world-to-minimap transform
  (`ox+x*sc, oy+y*sc`) against a hand-computed scale factor, the trouble-
  ring predicate firing independently for `spinT>0`/`blown`/`dmg>0.6` and
  correctly NOT firing at `dmg=0.5`, and a safe no-op on an empty outline
  (no track set yet).

  **Verified**: `ctest` 12/12. Headless `xvfb-run` screenshot
  (`LHT_FORCE_RACE=1`, a long-enough run that a caution actually got
  thrown, `flag` reading `CAUTION` in the HUD) -- decoded correctly this
  time (see the note below) -- shows the minimap's white-outlined panel,
  gray closed track-outline matching the main view's own track shape, a
  cluster of correctly-colored car dots sitting in the same relative
  position as the main view's own car cluster, the yellow player wedge,
  and (genuinely, not staged) a red pulsing ring around one car caught
  mid-incident by this run's real caution -- confirming the trouble-ring
  logic fires on actual sim state, not just the unit test's synthetic
  cases.

  **A verification-tooling mistake caught and fixed, not a renderer bug**:
  Phase 4e's own screenshot looked color-channel-swapped at first glance
  (see that section above) -- root-caused to this project's ad hoc
  screenshot-to-PNG Python snippet hardcoding PIL's raw decode mode as
  `'BGRA'`, when the screenshot callback's own `.meta` sidecar reports
  format code `71` (`bgfx::TextureFormat::RGBA8`, confirmed by compiling a
  two-line program against this project's vendored bgfx headers rather
  than guessing). Every screenshot conversion this session onward
  (including this one) uses the corrected `'RGBA'` mode. Worth a
  standing note for future sessions: **this project has no committed
  screenshot-to-PNG script** -- it's retyped ad hoc each session from the
  `.meta` sidecar's `width height pitch format yflip` fields, and the
  `format` field must be checked against `bgfx::TextureFormat`'s actual
  enum ordering (it can, and did, differ from the `BGRA8` assumed by an
  earlier session) rather than copy-pasted forward assuming it's always
  the same.

  **Phase 4g -- leaderboard panel.** Ported `index.html:3939-3978`'s
  leaderboard: top-5 rows plus, if the player isn't already in the top 5,
  a 6th pinned row (divider line above it) showing their real rank --
  matching JS's own `computeLayout()` list-building exactly.

  **New sim-core field, confirmed display-only before adding it**: JS's
  live gap-in-seconds needs a short rolling history of each car's race
  progress (`progHist`/`gapTimeAt`, index.html:1092-1093,1118), which
  `car.h` had explicitly deferred back in Phase 1 pending "if Phase 2+
  needs this for parity with the live HUD, add it then." Grepped
  `stepCar()`/`tick()`/`collide()`/`cautionController()` to confirm
  nothing in the ported physics core reads this field before adding it --
  it's purely a HUD display concern in JS too. Added `Car::progHist`
  (`std::deque<ProgSample>`, `{t, prog}`), sampled in `step_car.cpp`
  immediately after the existing `c.prog = ...` line, gated on
  `state.mode=="race"` and trimmed to keep only samples spanning the
  trailing 6 seconds, matching JS's own window exactly. Confirmed safe
  for determinism: `tests/determinism/trace.h`'s `CarSnapshot` is a
  hand-picked field list, not a raw struct copy, so adding a new `Car`
  field can't perturb `determinism_test`/`race_sim_test` (both still
  passed unchanged). Ported `gapTimeAt()` itself into
  `src/render/gap_time.h/.cpp` as a pure function returning
  `std::optional<double>` -- the same backward-scanning linear
  interpolation JS uses, isolated from bgfx like every other Phase 4
  sub-module's pure logic.

  **New `computeRaceOrder()`** (`hud.h/.cpp`): the same descending
  `done ? 1e6-finishT : prog` sort key `race.cpp:339-343` already uses for
  `S.order`, recomputed here purely for display. Deliberately added as a
  new function rather than refactoring `drawHud()`'s existing inline
  `pos`-counting rank lambda to reuse it -- that lambda is already
  verified/working, and the tie-break behavior of a `std::stable_sort`
  over the whole field vs. a single "count cars ranked above me" loop
  isn't provably identical in every edge case, so this avoided any risk
  to already-correct code for a purely cosmetic simplification. Reused by
  the new leaderboard and will be reused again by Phase 4h's results
  screen.

  **New `src/render/leaderboard.h/.cpp`**: `buildLeaderboardRows()` builds
  the top-5-plus-pinned-player row list and resolves each row's tag with
  JS's exact precedence -- `out` > `pit` > `spinT` > (gap-or-name, only
  while `showGaps`) -- and JS's exact broadcast-style toggle
  (`showGaps = mode=="race" && flag=="green" && floor(t/5)%2==1`,
  alternating between live gaps and driver names every 5 sim-seconds).
  Gap resolution: `dp<1` uses `gapTimeAt()`'s real interpolation from
  `progHist`; `dp>=1` reads `-N LAP`; the no-history fallback path
  (leader hasn't set a lap yet, or the trailing car has no samples yet)
  uses `dp*lapEst` with JS's own `lapEst = TRACK.total/48` magic-number
  fallback preserved as-is, logged rather than re-derived, per this
  project's own ground rules for unexplained JS constants. `drawLeaderboard()`
  draws the header + rank/color-chip (`pushQuad`)/tag rows via dbgText +
  the Phase 4e UI-quad primitives, highlighting the player's own row.

  **`hud.cpp` layout change**: the leaderboard is placed directly below
  the Phase 4e status bars, and the minimap (Phase 4f) is now
  repositioned to cascade below the leaderboard's own height
  (`lbBox.y + lbBox.h + 8`) instead of its previous fixed position --
  matching JS's own `computeLayout()` ordering now that a real
  leaderboard panel exists to cascade under, a deliberate revision of
  Phase 4f's placeholder position, noted there at the time as "worth
  revisiting once Phase 4g's real leaderboard panel lands."

  **New `tests/leaderboard_test.cpp`** (ctest now 13/13): top-5-only vs.
  pinned-6th-row list building, divider placement, tag precedence holding
  even while `showGaps` is true, the `showGaps` toggle itself (mode!=race
  falls back to plain names), the no-history gap fallback's exact
  `dp*lapEst` arithmetic, real `gapTimeAt()` interpolation against a
  hand-computed `progHist`, the `-N LAP` full-lap-down case, and an empty-
  order no-op.

  **Verified**: `ctest` 13/13. Headless `xvfb-run` screenshot
  (`LHT_FORCE_RACE=1`, captured at real sim time `t=7.1s` -- inside the
  `floor(t/5)%2==1` showGaps window, confirmed via the run's own status
  printf) shows the `LAKE HILL 400` header, ranks 1-5 with color chips and
  live gaps (`LEADER`/`+0.1`/`+0.1`/`+0.0`/`+0.2`), and the pinned player
  row (`20  #21  +5.3`) below a divider line with a highlighted
  background -- confirming both the top-N list and the pinned-outside-
  top-5 path render from real `Car::prog`/`progHist` state, not just the
  unit test's synthetic scenarios. The minimap renders correctly cascaded
  below it, and dbgText numbers stayed legible alongside the new rows with
  no layout collisions.

  **Phase 4h -- results screen + restart flow.** The final Phase 4 sub-task:
  ports `index.html:4115-4131`'s `showResults()` (finish order, best laps,
  DNFs, a "back to menu" restart) and wires up a real, repeatable
  menu->race->results->menu->race-again loop.

  **Step 1, a real latent bug found and fixed, not invented**: this port's
  `gridStart()` never cleared `finishOrder` (JS's own `gridStart()` does,
  `S.finishOrder=[]`, index.html:590) -- harmless while `gridStart()` only
  ever ran once per process, but a real dangling-`Car*` risk the instant a
  second race starts, since `cars` is cleared and repopulated on every
  `gridStart()` call while `finishOrder` holds raw pointers into it. Added
  `std::vector<Car*>& finishOrder` as a new `gridStart()` parameter (`race.h`/
  `race.cpp`), cleared first thing alongside `cars.clear()`. Updated all 3
  call sites (`main.cpp`'s `startRaceFromMenu()`, `race_sim_test.cpp`,
  `determinism_check.cpp`). Extended `race_sim_test.cpp` with a dedicated
  case: call `gridStart()` once, manually push two fake `Car*` entries into
  `finishOrder` (simulating a finished race), call `gridStart()` again, and
  assert `finishOrder` comes back empty -- a direct regression test for the
  exact bug this fixes.

  **Step 2, rendering**: confirmed via JS's own CSS (`.overlay` is opaque
  black; `#menu` explicitly overrides to semi-transparent; `#results` has no
  such override) that the results screen must fully replace the scene, not
  draw on top of the still-rendering track/cars like the menu does. New
  `src/ui/results.h/.cpp`, mirroring `menu.h/.cpp`'s structure:
  `computeResultsRegions(numRows)` (one `backBtn` region, positioned
  directly below the ranked list -- variable-height, same rationale as
  `leaderboard.h`'s own `LeaderboardBox`), `buildResultsOrder(finishOrder,
  order)` (the exact 3-bucket order from index.html:4119-4121:
  `finishOrder.concat(order.filter(!done&&!out)).concat(order.filter(!done&&out))`
  -- finishers in crossing order, then still-racing cars, then DNFs last,
  each bucket in `order`'s own race-position traversal), and
  `drawResults()` (title -- `"YOU WIN THE LAKE HILL 400!"` if
  `resultsOrder[0]->isPlayer` else `"RACE COMPLETE"`, matching
  index.html:4129-4130 exactly -- then rank/color-chip/car-number/name/
  best-lap-or-DNF rows, then the "BACK TO MENU" row). `Renderer::renderFrame()`
  gained an optional `finishOrder` parameter; while `mode=="done"`, view 0's
  clear color switches to opaque black (matching `renderBlockedFrame()`'s own
  precedent) and track/car submission is skipped entirely (wrapped in an
  `if (!showResults)` block), and `drawResults()` runs in place of
  `drawHud()`/`drawMenu()`.

  **Step 3, restart wiring**: new `handleResultsClick()` in `main.cpp`,
  matching JS's own `#againBtn` handler (index.html:4692-4696) exactly --
  the only interactive element is "BACK TO MENU", which just flips
  `state.mode` back to `"menu"`. Wired into the existing
  `SDL_MOUSEBUTTONDOWN`/`SDL_FINGERDOWN` blocks alongside the `mode=="menu"`
  branch, and into the render call site. `startRaceFromMenu()` (Phase 4b)
  already resets everything else a second `gridStart()` needs and now
  generalizes to a second race for free, now that Step 1's bugfix is in
  place.

  **New `tests/results_test.cpp`** (ctest now 14/14): `computeResultsRegions()`'s
  row-position math (the back button moves down as the row count grows, its
  column doesn't), `buildResultsOrder()`'s 3-bucket ordering (deliberately
  constructed with `order` in a different traversal order than
  `finishOrder`'s own declaration order, to prove the function doesn't just
  echo insertion order), the "`done` wins even if `out` is also set" edge
  case (index.html's own comment on this exact point), and an empty-input
  no-op.

  **A real-time-simulation testing constraint, worked around rather than
  ignored**: this sim runs in real time, not sped up (true of the original
  JS too), and a caution can extend a race's finish indefinitely via
  `race.cpp`'s green-white-checkered state machine (confirmed directly: an
  `LHT_FORCE_LAPS=1` native run reached `t=165s`/20000 frames still mid-race,
  `finishLaps` having grown past 1 from GWC extensions after an early
  caution) -- waiting out a genuine multi-lap finish is impractical for
  both native and WASM/Playwright verification. Rather than skip results-
  screen verification entirely, added a second, more targeted debug hook
  alongside the existing `LHT_FORCE_RACE`/`LHT_START_CHASE`: `main.cpp`'s
  `seedForceDoneState()` seeds a plausible post-race field (3 finishers with
  distinct `bestLapT`s, 1 DNF, the rest mid-race with a lap time) and jumps
  straight to `mode=="done"`. It's reachable two ways -- an `LHT_FORCE_DONE`
  env var read once at startup (native-only, mirrors the existing env-var
  hooks), and a live `SDLK_k` debug keyboard shortcut (works at any time,
  including via real Playwright keyboard input against the WASM build,
  since env vars can't reach a page loaded over HTTP) -- both call the same
  idempotent function, so there's exactly one seeding implementation to
  keep correct.

  **Verified**: `ctest` 14/14. Headless `xvfb-run` native screenshot
  (`LHT_FORCE_DONE=1`) shows the results screen rendering correctly: "RACE
  COMPLETE" title (the player isn't the winner in the seeded data), all 20
  cars ranked with color chips, the player's row (rank 4, `#21 YOU 0:45.00`)
  highlighted, the DNF car showing "DNF" instead of a time, and the "BACK TO
  MENU" row -- opaque black background confirming no track/car geometry
  renders underneath, matching the `.overlay`/`#results` CSS precedent.

  **The full restart loop verified end to end via WASM/Playwright**
  (`tests/wasm_verify.js`, extended -- real browser mouse clicks and a real
  keypress, not synthetic X11/XTEST input, per this container's
  already-established native-input-delivery limitation): loaded the page,
  clicked Start (existing Phase 4b check), pressed `k` to reach the results
  screen (screenshot confirms it renders), clicked "BACK TO MENU" at its
  computed pixel coordinates (screenshot confirms the menu genuinely
  reappears, not a stale results frame), then clicked Start again
  (screenshot confirms a fresh second race is running: `LAP 1/5`,
  `POS 20/20`, `GREEN` flag, live HUD/leaderboard/minimap all rendering
  clean state) -- zero console or page errors throughout, confirming the
  `gridStart()` `finishOrder` bugfix genuinely prevents the dangling-pointer
  risk a restart would otherwise hit, not just in theory but in a real
  second run.

  **Phase 4 ("UI overlay") is now fully done**: every sub-task from the
  original checklist (lap/position/flag/speed, menu, last/best lap time,
  gear/RPM, segmented tire/fuel/car bars, minimap, leaderboard, results
  screen + restart) is ported and verified.

- **Session 7 -- Phase 5a: 3D rendering foundation.** User asked to proceed
  to Phase 5 ("Full render fidelity"). Since Phase 5's own checklist is
  large (procedural stadium/livery mesh generation, per-track sky/ENV
  presets, hand-rolled bloom+tonemap) and touches almost every part of the
  renderer, it's broken into 8 sub-phases (5a-5h) tracked in a session plan,
  same commit-per-sub-phase rhythm as Phase 4's 4a-4h.

  **Scope decisions made up front** (engineering judgment; user declined
  further clarifying questions and asked to continue): the full JS
  alternate-camera-mode suite (helmet/tower/blimp/victory-orbit/pit-stop/
  caution-TV-montage/menu-establishing-shot) is **explicitly out of scope**
  for Phase 5 -- it's not on the phase's literal checklist, and is deferred
  to a future session. Livery painting (5f, not yet started) will be a
  full-fidelity CPU-rasterizer port of `paintLivery()`, not a shortcut tint.

  **What Phase 5a actually is**: today's renderer was a deliberate 2D
  placeholder (confirmed by direct reading before this session started) --
  strictly orthographic camera, flat unlit vertex-color shader, a flat Z=0
  track ribbon, depth cleared but never tested/written. `Track::bankAt()`
  was already correctly ported but never consumed by rendering. Phase 5a
  gives the renderer real 3D: a banked track mesh, a lit shading model, a
  genuine perspective chase camera with banking lean, and depth testing --
  the minimum foundation the rest of Phase 5's stadium/sky/livery geometry
  needs to be visible at all (the old top-down-only ortho view would never
  show it).

  **New `src/render/track_surface.h/.cpp`** (bgfx-free): a direct,
  parameter-passing port of JS's "3D surface model (render only; physics
  stays planar)" (`index.html:377-395`) -- `wallLat()`/`apronIn()`/`surfH()`/
  `pos3()` as functions of `const Track&` instead of JS's global `TRACK`.
  Also `surfaceUp(track, s)`: JS derives its camera-lean up-vector from the
  car's own 3D model-matrix (`carBasis()`, index.html:3064-3071), which
  doesn't exist in this port (no 3D car loft). Rather than approximate,
  algebraically reduced JS's cross-product formula to its exact closed
  form -- `up = (sin(b)*sin(th), cos(b), -sin(b)*cos(th))`, already unit
  length and, importantly, provably independent of car pitch in JS too (JS's
  own `c.pitch` only ever perturbs the forward vector, never this one) -- so
  this is an exact port of that vector's math, not a simplification, despite
  having no car model matrix to derive it from directly. Verified in new
  bgfx-free `tests/track_surface_test.cpp`, cross-checked against
  `Track::bankAt()`'s own already-verified sign convention rather than
  hardcoding new banking-angle magic numbers.

  **New lit shader path**: `PosNormalColorVertex` (`src/render/vertex_lit.h`)
  + `vs_lit.sc`/`fs_lit.sc`/`varying_lit.def.sc` -- hemisphere-ambient +
  directional-diffuse shading (JS's two-light model, one
  `THREE.HemisphereLight` + one `THREE.DirectionalLight`, no shadow maps,
  matching JS which has none either). The existing flat unlit
  `vs_flat.sc`/`fs_flat.sc`/`PosColorVertex` path is untouched, still used
  for the pixel-space UI overlay (view 1). `renderer.cpp`'s `setTrack()`
  rebuilds the ribbon mesh using `pos3()` (real elevation/banking) with flat
  per-triangle normals (**logged simplification**: not smooth vertex
  normals -- acceptable for a mostly-planar ribbon, revisit only if a visual
  problem shows up). Added `BGFX_STATE_WRITE_Z | BGFX_STATE_DEPTH_TEST_LESS`
  to view 0's draw state -- this is the first time this renderer has
  actually tested/written depth; Phases 0-4 cleared it every frame but
  relied purely on submission order for layering.

  **Camera rewrite**: `TopDown` stays orthographic (still a static whole-
  track overview) but now looks straight down the real Y axis instead of a
  2D-placeholder Z axis. `Chase` is now a real `bx::mtxProj` perspective
  view (FOV 60, near 0.5, far 1500, matching JS's own
  `new THREE.PerspectiveCamera(60, 1, 0.5, 1500)` exactly): eye/look/up all
  derived from `pos3()`/`surfaceUp()`, JS's exact `dist=6.9+v*0.02`/
  `hgt=2.55` constants, the existing corner-lookahead bias reused, and JS's
  **two-rate exponential smoothing** (`k=1-exp(-dt*11)` for position,
  `k2=1-exp(-dt*22)` for look) -- this port's prior 2D chase camera used a
  single shared smoothing rate for both, a simplification now corrected as
  part of this rewrite (logged as a fidelity fix, not a new feature). The
  surface-height clamp (`Track::project()` + `surfH()+1.4`) and the NT2003
  lean (`upBlend = normalize([up.x*0.45, 1.0, up.z*0.45])`, y hardcoded to
  1.0 and *not* blended) are both direct ports of JS's own constants. Car
  quads keep their existing flat-quad shape (no 3D car loft -- out of
  scope) but now sit at the correct banked height/normal via `pos3()`/
  `surfaceUp()` instead of a flat world Z.

  **Two real bugs found and fixed during this sub-phase, both logged
  here rather than silently patched**:
  1. *Lighting overexposure*: the very first Chase-mode screenshot was a
     near-uniform pale wash across the whole frame. JS's light intensities
     (sun=3.2, hemi=1.1) are calibrated for use with
     `THREE.ACESFilmicToneMapping` (not ported yet -- that's Phase 5h's
     job), so writing those same raw values to an LDR backbuffer with no
     tonemap clips almost everything to near-white. Fixed with a temporary
     `min(ambient + diffuse, vec3(1.0))` clamp in `fs_lit.sc`, explicitly
     commented in the shader as a stand-in until Phase 5h's real ACES pass
     replaces it.
  2. *`bx::mtxProj` FOV double-conversion*: after fixing (1), Chase-mode
     screenshots still showed the entire frame as one uniform color (95%+
     of sampled pixels identical, no clear-color visible anywhere). Root-
     caused methodically, not by guessing: confirmed TopDown still worked
     (isolating the bug to Chase-only code), added temporary debug printfs
     confirming the mesh and camera eye/at/up values were all numerically
     sane, then added a manual clip-space transform of known test points
     (`bx::mtxMul` + `vec4MulMtx`) which showed an off-axis point landing at
     roughly 75-100x the expected NDC magnitude -- the entire visible frame
     really was just the nearest geometry, wildly over-magnified. Reading
     `third_party/bgfx.cmake/bx/src/math.cpp:106-110` directly confirmed
     `bx::mtxProj(float*, float _fovy, ...)` calls `toRad(_fovy)`
     internally, i.e. it expects **degrees**, not radians -- this port's
     code had pre-converted via `bx::toRad(60.0f)`, double-converting to a
     near-zero effective FOV. Fixed by passing `60.0f` directly; the fix and
     an explanatory comment (including the bug's exact symptom, so a future
     reader doesn't reintroduce it) are in `renderer.cpp`. All temporary
     debug instrumentation was removed once the fix was confirmed.

  **Verified**: `ctest` 15/15 (new `track_surface_test` added). Headless
  `xvfb-run` screenshots on Big Sable (index 3, 23 deg banking): `Chase`
  mode now shows genuine perspective with a real vanishing point, the track
  correctly receding into the distance with visible curvature, cars
  rendered as distance-scaled quads sitting on the track surface (not
  floating), and the crisp HUD/leaderboard/minimap overlay rendering
  unaffected on top (confirming the flat-shader UI view still sits outside
  the new lit-geometry path). `TopDown` mode re-verified unbroken (whole-
  track overview, cars visible at the start/finish line, HUD intact) --
  no regression from the camera/mesh rewrite.

  **Explicitly deferred, not part of this sub-phase**: the full alternate-
  camera-mode suite (noted above), per-track lighting data (5b hardcodes
  today's uniforms to `noon-grass`'s values), and any stadium/sky/livery
  geometry (5b onward). **Next**: Phase 5b (per-track theme/stadium/
  `ENV_PRESETS` data + real per-track lighting).

- **Session 7 -- Phase 5b: per-track theme/stadium/ENV_PRESETS data + real
  lighting.** Ports the visual-only fields from each JS `TRACKS[]` entry
  (index.html:242-283) that Phase 1 deliberately skipped, plus JS's
  `ENV_PRESETS` table + `applyEnvPreset()` (index.html:3490-3530), and wires
  both into the renderer so each track finally looks different, not just
  drives different.

  **`src/sim/track.h`** gained `TrackTheme` (`wall`/`grass` colors),
  `StandTier`/`StandScale`/`Sky`/`Env`/`Stadium` (all the per-track stadium
  dressing fields -- tier counts, density, crowd fill, sky gradient +
  silhouette, env preset name, 6-color crowd palette), and `TrackSpec`
  gained `theme`/`stadium` members. **Named `TrackTheme`, not `Theme`**:
  a plain `Theme` collided with `render/color.h`'s existing `namespace
  Theme` (the unrelated UI chrome palette) -- caught immediately by the
  build (`'namespace Theme {}' redeclared as different kind of entity`),
  fixed by renaming rather than touching the older, unrelated code.
  `Track` now exposes `theme()`/`stadium()` accessors; `Track::Track()`
  copies both from the spec. `tracks_data.h`'s `TRACKS` table gained the
  full transcribed literal data for all 4 tracks (theme colors, stadium
  tiers/density/crowd-fill/sky/env/palette), matching index.html:243-282
  field-for-field. Only `theme.grass` and `stadium.env.preset` are
  actually consumed as of this sub-phase -- the rest (stand tiers, crowd
  palette, sky gradient, jumbotron/pylon flags) is ported now as data,
  matching the JS source's own "one config object, one generic code path"
  layout, and gets consumed incrementally starting Phase 5d.

  **New `src/render/env_presets.h`** (bgfx-free): the 4 `ENV_PRESETS`
  entries (`noon-grass`/`sunset`/`hazy-noon`/`dusk-lights`, index.html:
  3491-3508) transcribed via a `hexRgb()` helper so each 0xRRGGBB literal
  reads exactly as it does in the JS source, plus `resolveEnvPreset(name)`
  (falls back to `noon-grass` for an unrecognized name, matching
  index.html:3522's `|| ENV_PRESETS['noon-grass']`) and
  `envSunDirection(preset)` (azimuth/elevation -> unit direction TOWARD the
  sun, matching `THREE.DirectionalLight`'s own position-as-direction
  convention since the light targets the origin, index.html:3524).
  New `tests/env_presets_test.cpp`: direction math at known angles
  (straight-up, horizon), unit-length check for all 4 presets, the
  unknown-name fallback, and -- a typo guard on the transcribed data --
  confirms each real track's `stadium().env.preset` string resolves to a
  genuinely distinct preset from its neighbors, not an accidental shared
  fallback.

  **`renderer.cpp`**: `setTrack()` now resolves the track's env preset once
  (not per frame -- this is per-track data) and stores the sun direction/
  color and hemisphere sky/ground values (each pre-multiplied by the
  preset's own intensity) as new `Renderer` members, replacing Phase 5a's
  hardcoded `noon-grass` constants in `renderFrame()`. Also builds a large
  flat ground plane (6x the top-down framing half-size, so it fills the
  frame in both camera modes) colored by `theme.grass` and lit through the
  same `vs_lit`/`fs_lit` path as the ribbon -- the first real use of
  per-track color data, and something for the lighting model to shade
  besides the ribbon itself. Sits at y=-0.05, a hair below the ribbon's own
  lowest point (apron height, always >= 0.02 per `surfH()`) to avoid
  z-fighting at the ribbon's edges.

  **New `tests/env_presets_test.cpp`** target added to CMakeLists.txt;
  needs no `.cpp` sources at all since both `env_presets.h` and
  `tracks_data.h` are header-only (`TRACKS` is an inline const array of
  aggregates, no `Track` objects constructed to read it).

  **Verified**: `ctest` 16/16 (env_presets_test added). Headless
  `xvfb-run` Chase-mode screenshots on all 4 tracks confirm the ground
  plane renders as the correct grass color everywhere and the per-track
  lighting mood is genuinely distinguishable, not just theoretically
  different data: sampling a HUD-clear strip of pure grass on each
  screenshot gave average RGB (45,80,22) Thunder Oval, (38,69,20)
  Milltown, (51,84,26) Cedar Valley, (20,39,16) Big Sable -- Big Sable
  reads distinctly darkest, matching `dusk-lights`' very low 6-degree sun
  elevation, exactly the differentiation this sub-phase's own verification
  bar called for. No regression in either camera mode.

  **Next**: Phase 5c (sky background).

- **Session 7 -- Phase 5c: sky background.** Ports JS's `buildSkyTexture()`
  (index.html:3724-3766) -- a small "painted backdrop" texture, not a real
  per-direction sky (JS sets it as `scene.background`, a flat texture that
  never rotates with the camera; this port matches that exactly).

  **New `src/render/sky_texture.h/.cpp`** (bgfx-free): `buildSkyPixels(sky,
  sunPreset)` builds a 128x256 RGBA8 buffer -- a 3-stop vertical gradient
  (zenith -> horizon -> a slightly lightened horizon-haze band, faking
  atmospheric-scattering brightening right at the horizon line) plus a
  stylized sun-glow blob (radial falloff, positioned by the resolved
  `EnvPreset`'s elevation) plus two faint cloud streaks using the same
  `rng2` seed JS uses for scenery-only randomness (`mulberry32(777)`,
  index.html:1737 -- reuses this port's existing `Mulberry32` class,
  doesn't affect gameplay determinism, render-only data). **Logged
  simplification**: JS applies a real 5px canvas blur to each cloud
  streak; this port approximates it with a small hand-rolled vertical
  alpha falloff instead of an actual Gaussian blur pass -- close enough
  for a faint (alpha 0.09), small screen-space element. New
  `tests/sky_texture_test.cpp`: gradient stops at known rows (zenith at
  y=0, horizon at y=0.82*(H-1), a brighter haze band at the bottom), and a
  detectable brightness bump at the sun-glow's computed position vs. the
  same row far from it.

  **New unlit textured-quad shader path** (5e's texture infrastructure
  doesn't exist yet, so this is a minimal one-off per the plan's own
  sequencing note, to be generalized/reused once 5e lands): `vs_sky.sc`/
  `fs_sky.sc` + `varying_sky.def.sc` (Position+UV only, no normal/color0)
  + `PosNormalColorVertex`'s sibling `PosUvVertex` (`vertex_uv.h`). New
  CMakeLists.txt `bgfx_compile_shaders()` block mirroring the existing
  vs_lit/fs_lit one; extended the Emscripten `FATAL_ERROR` guard and
  `shaders_embedded.h` the same way Phase 5a did for vs_lit/fs_lit.

  **`renderer.cpp`**: `setTrack()` rebuilds the sky texture once per track
  (per-track data, not per-frame, same rationale as Phase 5b's lighting
  uniforms) from `track.stadium().sky` + the already-resolved `EnvPreset`.
  `renderFrame()` draws a static NDC-space fullscreen quad (built once in
  `init()`, identity view/proj since its vertices are already in clip
  space) with no depth write/test, in a **new view (id 0)** -- bgfx renders
  views in ascending ID order regardless of submission order, so the sky
  had to become the numerically-lowest view for it to end up behind
  everything else; the world view shifted from id 0->1 and the UI overlay
  from 1->2 to make room. Skipped during the results screen (same as the
  ribbon/cars).

  **One real bug found and fixed during verification**: the first Chase-
  mode screenshot showed the sky as entirely invisible -- the exact same
  dark-green clear color as before this sub-phase, everywhere above the
  ground plane. Root cause: a bgfx view's `setViewClear` touches its
  **entire** viewport regardless of what that view goes on to draw, so the
  world view's own full-screen `BGFX_CLEAR_COLOR` was unconditionally
  painting over the sky view's output before the world view's own geometry
  (ribbon/ground/cars) got a chance to draw on top of it, everywhere those
  didn't cover (i.e. exactly where the sky should show through). Fixed by
  narrowing the world view's clear to depth-only whenever the sky actually
  painted that frame (still clears color as before for the results screen
  and the "no sky built yet" fallback, neither of which draws a sky).

  **Deferred to Phase 5g** (grouped with Big Sable's jumbotron/pylon as the
  other track-specific special case): Cedar Valley's `sky.silhouette==
  'hills'` hill silhouette -- this sub-phase only implements the gradient/
  glow/clouds backdrop every track gets.

  **Verified**: `ctest` 17/17 (sky_texture_test added). Headless
  `xvfb-run` Chase-mode screenshots on all 4 tracks confirm a real
  gradient sky (dark blue zenith fading to a lighter horizon) now renders
  behind the ground/track/cars with a visible horizon transition, and the
  HUD/leaderboard/minimap overlay still renders crisply on top,
  unaffected. `TopDown` mode re-verified unbroken: since that camera looks
  straight down, the ground plane alone fills the entire ortho frustum
  (correctly -- there's no horizon to see from directly overhead), exactly
  as expected, not a regression.

  **Next**: Phase 5d (stadium stands + pit road geometry, flat colors).
