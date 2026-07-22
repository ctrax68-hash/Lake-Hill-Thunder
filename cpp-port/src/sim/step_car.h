#pragma once

#include "car.h"
#include "race_state.h"
#include "track.h"

#include <vector>

// stepCar() (index.html:686-1110). Ported branch-by-branch -- see
// PORT_PROGRESS.md's Phase 1f notes for which branches exist so far. An
// unported branch throws std::logic_error rather than silently running
// wrong physics if a scenario reaches it before it's been ported.
//
// `allCars` and `pace` are needed even by branches not yet ported (e.g. the
// pace-mode branch reads `allCars[c.gridAhead]` and `pace.s`/`pace.state`),
// so the signature is settled now rather than growing per-branch.
void stepCar(Car& c, RaceState& state, const Track& track, const std::vector<Car>& allCars,
             const PaceCar& pace);
