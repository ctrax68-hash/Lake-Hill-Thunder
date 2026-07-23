#pragma once

#include "../sim/car.h"

#include <deque>
#include <optional>

// Phase 4g (PORT_PROGRESS.md): direct port of JS's gapTimeAt()
// (index.html:1118-1126) -- the standard "time-shifted position"
// technique real timing systems use: given the car AHEAD's recent
// {t,prog} history, find when it was at the trailing car's CURRENT
// prog (linear-interpolating between the two bracketing samples),
// returning nullopt (JS's `null`) if there isn't enough history yet
// (e.g. just after a restart).
std::optional<double> gapTimeAt(const std::deque<ProgSample>& hist, double targetProg);
