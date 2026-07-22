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

// tick()'s pace-phase-relevant orchestration only (index.html:4180-4204):
// advance sim time, step the pace car, update aero, step every car, resolve
// collisions, handle the pace->race mode transition, and the green-flag
// clocks. NOT ported here (deliberately, not yet needed): storing previous
// poses for render interpolation (render-only), S.order (HUD/leaderboard
// only, not read by any physics this phase touches), qual-lap completion,
// AI pit-strategy/blowout/DNF rolls and the caution controller (all gated
// on S.mode==='race' in JS, meaning they cannot fire during the pace phase
// at all -- see PORT_PROGRESS.md's Phase 1f notes for why this is a
// genuinely inert omission for THIS phase, not a shortcut).
void tick(RaceState& state, std::vector<Car>& cars, PaceCar& pace, const Track& track,
          Mulberry32& rngR);
