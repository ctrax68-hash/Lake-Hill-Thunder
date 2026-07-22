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
void gridStart(const Track& track, Mulberry32& rng, RaceState& state, PaceCar& pace,
                std::vector<Car>& cars, const std::vector<int>* gridOrder = nullptr);

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
// audio/render-only side effects (S.msgTxt/msgT, CAR_MAT_AMBER, cam.pos,
// spotterSay) are NOT ported -- see PORT_PROGRESS.md's Phase 1g notes.
// `order` must be race-position-sorted (see tick()'s own use of this same
// data, matching JS's S.order) and non-empty.
void cautionController(RaceState& state, std::vector<Car>& cars, PaceCar& pace, const Track& track,
                        const std::vector<Car*>& order);

// tick()'s pace-phase-, green-flag-racing-, and caution-relevant
// orchestration (index.html:4180-4462): advance sim time, step the pace
// car, update aero, step every car, resolve collisions, compute race
// position order, handle the pace->race mode transition, the green-flag
// clocks, and the caution controller. NOT ported here (deliberately, not
// yet needed): storing previous poses for render interpolation
// (render-only), qual-lap completion, AI pit-strategy (sets c.pitReq,
// which nothing reads yet since the pit-entry-arming block isn't ported),
// blowout/DNF rolls (organic spin/DNF triggers -- tested instead via
// dump_js_trace.js's --force flag, see PORT_PROGRESS.md), and
// green-white-checkered/qualifying (Phase 1g, still to come).
void tick(RaceState& state, std::vector<Car>& cars, PaceCar& pace, const Track& track,
          Mulberry32& rngR, const PlayerInput& input);
