#pragma once

#include "car.h"
#include "race_state.h"
#include "rng.h"
#include "track.h"

#include <vector>

// gridStart() (index.html:564-598). Builds the 20-car field via makeCar()
// (consuming `rng`'s 3-calls-per-AI-car sequence in `gridOrder` order, or
// the default [1..19,0] order if `gridOrder` is null/wrong-length, exactly
// matching JS), then places them on the backstretch grid and sets up
// `state`/`pace` for a rolling start. `cars` is cleared and repopulated.
// `finishOrder` is cleared too (JS's `S.finishOrder=[]`, index.html:590) --
// it holds `Car*` pointers into `cars`, so leaving stale entries around
// would dangle the instant a second race is started (Phase 4h's restart
// flow is what first makes gridStart() run more than once per process).
void gridStart(const Track& track, Mulberry32& rng, RaceState& state, PaceCar& pace,
                std::vector<Car>& cars, std::vector<Car*>& finishOrder,
                const std::vector<int>* gridOrder = nullptr);

// stepPace() (index.html:599-626).
void stepPace(PaceCar& pace, const RaceState& state, const Track& track);

// updateAero() (index.html:652-676): per-car drafting/dirty-air state.
void updateAero(std::vector<Car>& cars, const Track& track);

// collide() (index.html:1158-1231). Pairwise contact resolution + damage +
// wreck-roll (the wreck-roll and its S.flag/S.mode gates are fully ported
// even though they're provably inert during the pace phase, since collide()
// is one small self-contained function, not something that needs the same
// branch-by-branch splitting stepCar() does).
void collide(std::vector<Car>& cars, const RaceState& state, const Track& track, Mulberry32& rngR);

// activeLead() (index.html:1138-1141): first still-racing (not done/out) car
// in race-position order; falls back to the leader-by-position if every car
// is done/out. `order` must be race-position-sorted (see below) and
// non-empty.
Car* activeLead(const std::vector<Car*>& order);

// tick()'s caution controller (index.html:4251-4461): detects a wreck on
// track during green and throws the yellow flag (assigning caution slots by
// physical position), then manages the yellow-flag phase -- adaptive pace
// speed, slot compaction, time-compressed straggler warping, the
// bunched/one-to-go transition, and the green-flag restart trigger. HUD/
// audio/render-only side effects (S.msgTxt/msgT, CAR_MAT_AMBER, cam.pos) are
// NOT ported -- see PORT_PROGRESS.md's Phase 1g notes. (spotterSay() itself
// IS ported, but its OWN trigger sites are elsewhere -- the blowout/damage-
// DNF branches and the alongside/laps-to-go checks in tick(), not this
// function -- see Phase 6b's PORT_PROGRESS.md entry.)
// `order` must be race-position-sorted (see tick()'s own use of this same
// data, matching JS's S.order) and non-empty.
void cautionController(RaceState& state, std::vector<Car>& cars, PaceCar& pace, const Track& track,
                        const std::vector<Car*>& order);

// tick()'s full per-frame orchestration (index.html:4180-4595): advance sim
// time, step the pace car, update aero, step every car, resolve collisions,
// compute race position order, handle the pace->race mode transition, the
// green-flag clocks, the qualifying flying-lap completion check, AI
// pit-strategy (c.pitReq), blowout/DNF rolls, the caution controller, the
// green-white-checkered state machine, the unconditional finish-line
// arbitration, and the player-finish -> victory/done mode transitions.
// `finishOrder` mirrors S.finishOrder (index.html:509) -- appended to in
// crossing order as cars finish; caller owns its lifetime (cleared at the
// start of a race, same as gridStart() clearing `cars`).
// Also ported (Phase 6b): the spotter-message system (index.html:4549-4567)
// -- alongside calls (INSIDE!/OUTSIDE!/THREE WIDE!/CLEAR) and the laps-to-
// go/fuel/tire/damage one-shot warnings, all via the sim-side spotterSay()
// helper (sets state.spotTxt/spotT only -- no audio dependency; Phase 6c's
// audio engine and Phase 6d's HUD caption are expected to read those fields
// back, not call spotterSay() themselves), plus the blowout/terminal-damage
// DNF call sites and c.hitFx accumulation (index.html:1067,1227,4238).
// NOT ported here (deliberately, still not needed): storing previous poses
// for render interpolation (render-only), and every remaining HUD/audio-
// only side effect (S.msgTxt/msgT -- a separate, unrelated broadcast-banner
// system -- S.fastLap/halfMsg/whiteMsg, S.greenT's crowd-noise/banner reads
// -- the greenT clock itself IS decremented here since it's cheap and the
// field already exists, just nothing reads it back yet).
// `finishQualifying()`'s grid-building (needs
// rngR the same way makeCar() needs rng, plus the menu-flow setTimeout) is
// also not ported -- that's a Phase 2+ menu-wiring concern, not physics;
// this file only ports the mode='qual' -> 'menuwait' physics-state
// transition itself.
void tick(RaceState& state, std::vector<Car>& cars, PaceCar& pace, const Track& track,
          Mulberry32& rngR, const PlayerInput& input, std::vector<Car*>& finishOrder);
