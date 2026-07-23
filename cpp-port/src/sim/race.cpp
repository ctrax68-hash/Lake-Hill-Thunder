#include "race.h"

#include "constants.h"
#include "step_car.h"

#include <algorithm>
#include <cmath>

namespace {
double wrapMod(double s, double total) {
    return std::fmod(std::fmod(s, total) + total, total);
}

// spotterSay() (index.html:1385-1388). JS sets the HUD caption fields AND
// fires an audio blip synchronously in one call; this port only sets the
// fields (S.spotTxt/S.spotT are the only physics-adjacent state a
// bgfx/SDL2-free race.cpp should touch). Phase 6c's audio engine is
// expected to edge-detect a fresh message the same way audioTick() already
// edge-detects c.hitFx/c.blown -- see race_state.h's own comment on
// spotTxt/spotT.
void spotterSay(RaceState& state, const std::string& txt) {
    state.spotTxt = txt;
    state.spotT = 2.2;
}
} // namespace

// gridStart() (index.html:564-598)
void gridStart(const Track& track, Mulberry32& rng, RaceState& state, PaceCar& pace,
                std::vector<Car>& cars, std::vector<Car*>& finishOrder,
                const std::vector<int>* gridOrder) {
    cars.clear();
    finishOrder.clear();

    std::vector<int> order;
    if (gridOrder && static_cast<int>(gridOrder->size()) == FIELD) {
        order = *gridOrder;
    } else {
        for (int i = 1; i < FIELD; ++i) order.push_back(i);
        order.push_back(0);
    }
    for (int idx : order) cars.push_back(makeCar(idx == 0, idx, track, rng));

    const double sLead = track.segs()[2].s0 + 40;
    for (size_t i = 0; i < cars.size(); ++i) {
        Car& c = cars[i];
        const int row = static_cast<int>(i) / 2;
        const bool colL = (i % 2 == 0);
        const double s = sLead - row * 11;
        PointResult p = track.pointAt(s);
        const double off = colL ? -2.6 : 2.6;
        c.x = p.x - std::sin(p.hdg) * off;
        c.y = p.y + std::cos(p.hdg) * off;
        c.hdg = c.vdir = p.hdg;
        c.s = wrapMod(s, track.total());
        c.lat = off;
        c.v = 31;
        c.gridLane = off;
        c.gridAhead = i >= 2 ? static_cast<int>(i) - 2 : -1;
        c.gridSlot = static_cast<int>(i);
        c.prog = -1 + (static_cast<int>(i) * 0.001);
        c.px = c.x;
        c.py = c.y;
        c.phdg = c.hdg;
        c.ps = c.s;
        c.plat = c.lat;
    }

    state.flag = "green";
    state.cautionUntilLap = -1;
    state.t = 0;
    state.paceV = 31;
    state.greenT = 0;

    const double ps = sLead + 18;
    PointResult pp = track.pointAt(ps);
    pace.s = wrapMod(ps, track.total());
    pace.lat = 0;
    pace.v = 31;
    pace.hdg = pp.hdg;
    pace.x = pp.x;
    pace.y = pp.y;
    pace.state = "lead";
}

// stepPace() (index.html:599-626)
void stepPace(PaceCar& pace, const RaceState& state, const Track& track) {
    if (pace.state == "lead") {
        pace.s += pace.v * DT;
        if (pace.s >= track.total()) pace.s -= track.total();
        if (state.mode == "pace" && pace.s > 2 && pace.s < track.sFinish() - 60) pace.state = "peel";
    } else if (pace.state == "peel") {
        pace.s += pace.v * DT;
        if (pace.s >= track.total()) pace.s -= track.total();
        pace.lat = std::max(-11.0, pace.lat - 5.5 * DT);
        pace.v = std::max(0.0, pace.v - 6 * DT);
        if (pace.v <= 0.1 || pace.lat <= -10.5) pace.state = "parked";
    }
    PointResult p = track.pointAt(pace.s);
    const double nx = -std::sin(p.hdg), ny = std::cos(p.hdg);
    pace.x = p.x + nx * pace.lat;
    pace.y = p.y + ny * pace.lat;
    pace.hdg = p.hdg;
}

// updateAero() (index.html:652-676)
void updateAero(std::vector<Car>& cars, const Track& track) {
    for (auto& c : cars) {
        c.draftF = 0;
        c.dirty = false;
    }
    for (auto& c : cars) {
        for (auto& o : cars) {
            if (&o == &c) continue;
            double ds = o.s - c.s;
            if (ds < -track.total() / 2) ds += track.total();
            if (ds > track.total() / 2) ds -= track.total();
            if (ds > 1 && ds < 34 && std::abs(o.lat - c.lat) < 6.0) {
                double f = (ds < 9) ? 1.0 : std::max(0.0, 1 - (ds - 9) / 25);
                c.draftF = std::max(c.draftF, f);
                PointResult p = track.pointAt(c.s);
                if (std::abs(p.curv) > 1e-6 && ds < 16) c.dirty = true;
            }
        }
    }
}

// collide() (index.html:1158-1231)
void collide(std::vector<Car>& cars, const RaceState& state, const Track& track, Mulberry32& rngR) {
    const double trackWreckScale = std::min(1.0, 1600.0 / track.total());
    for (size_t i = 0; i < cars.size(); ++i) {
        for (size_t j = i + 1; j < cars.size(); ++j) {
            Car& a = cars[i];
            Car& b = cars[j];
            const double dx = b.x - a.x, dy = b.y - a.y, d = std::hypot(dx, dy), minD = 3.6;
            if (d > 0.01 && d < minD) {
                const double nx = dx / d, ny = dy / d, push = (minD - d) / 2;
                a.x -= nx * push;
                a.y -= ny * push;
                b.x += nx * push;
                b.y += ny * push;
                const double dvel = (a.v - b.v) * 0.25;
                const double closeMag = std::min(1.0, std::abs(dvel) / 1.0);
                a.v = std::max(0.0, a.v - std::abs(dvel) * 0.6 - 0.4 * closeMag);
                b.v = std::max(0.0, b.v - 0.3 * closeMag);
                a.hdg += ny * 0.008 * (a.isPlayer ? 1 : 2);
                b.hdg -= ny * 0.008;

                const double cv2 = std::abs(a.v - b.v);
                if (cv2 > 3 && state.flag != "yellow") {
                    Car& hitter = a.v > b.v ? a : b;
                    Car& victim2 = a.v > b.v ? b : a;
                    if (hitter.dmgCd <= 0) {
                        hitter.dmg = std::min(1.0, hitter.dmg + std::min(0.08, cv2 * 0.003));
                        victim2.dmg = std::min(1.0, victim2.dmg + std::min(0.05, cv2 * 0.002));
                        hitter.dmgCd = 0.6;
                        victim2.dmgCd = 0.6;
                        if (cv2 > 18 && state.mode == "race" && state.flag == "green" &&
                            state.greenLockT <= 0 && victim2.spinT <= 0 && victim2.spinCd <= 0 &&
                            victim2.spinRollCd <= 0) {
                            victim2.spinRollCd = 2.6;
                            if (rngR.next() < std::min(0.06, (cv2 - 18) * 0.0025 + 0.0025) * trackWreckScale) {
                                victim2.spinT = 1.6 + rngR.next() * 1.2;
                                victim2.spinDir = ny > 0 ? 1 : -1;
                                victim2.spinCd = 10;
                            }
                        }
                        hitter.hitFx = std::min(1.0, hitter.hitFx + cv2 * 0.04); // index.html:1227
                    }
                }
            }
        }
    }
}

// activeLead() (index.html:1138-1141)
Car* activeLead(const std::vector<Car*>& order) {
    for (auto* o : order) {
        if (!o->done && !o->out) return o;
    }
    return order[0];
}

namespace {
double wrapHalf(double d, double total) {
    while (d < -total / 2) d += total;
    while (d > total / 2) d -= total;
    return d;
}
} // namespace

// cautionController() (index.html:4251-4461)
void cautionController(RaceState& state, std::vector<Car>& cars, PaceCar& pace, const Track& track,
                        const std::vector<Car*>& order) {
    if (state.mode != "race") return;

    if (state.flag == "green") {
        // index.html:4254-4263: finish the pace car's dive to the apron.
        if (pace.state == "peel") stepPace(pace, state, track);

        for (auto& c : cars) {
            if (c.spinT > 0 && !c.done) {
                state.flag = "yellow";
                state.yellowT = 0;
                Car* lead = activeLead(order);
                state.cautionUntilLap = lead->lap + 2;
                pace.state = "lead";
                pace.lat = 0;
                pace.v = 38;
                pace.s = std::fmod(lead->s + 90, track.total());

                std::vector<Car*> act;
                for (auto& o : cars) {
                    if (!o.out && !o.done) act.push_back(&o);
                }
                std::stable_sort(act.begin(), act.end(), [&](Car* a, Car* b) {
                    double da = wrapMod(pace.s - a->s, track.total());
                    double db = wrapMod(pace.s - b->s, track.total());
                    return da < db;
                });
                for (size_t i = 0; i < act.size(); ++i) act[i]->cautionSlot = static_cast<int>(i);
                state.cautionMaxSlot = static_cast<int>(act.size()) - 1;
                state.oneToGo = false;
                state.pitsOpen = false;
                break;
            }
        }
    } else if (state.flag == "yellow") {
        state.yellowT += DT;
        stepPace(pace, state, track);

        // index.html:4296-4314: adaptive pace speed.
        {
            Car* front = nullptr;
            double best = 1e9;
            for (auto& o : cars) {
                if (o.out || o.done || o.pit != 0 || o.spinT > 0) continue;
                if (o.cautionSlot >= 0 && o.cautionSlot < best) {
                    best = o.cautionSlot;
                    front = &o;
                }
            }
            if (front) {
                double gap = wrapHalf(pace.s - 16 - front->cautionSlot * 9 - front->s, track.total());
                pace.v = 38 - std::max(0.0, std::min(16.0, (gap - 24) * 0.15));
            } else {
                pace.v = 38;
            }
        }

        Car* lead = activeLead(order);

        // index.html:4341-4343: slot compaction.
        std::vector<Car*> activeY;
        for (auto* o : order) {
            if (!o->out && !o->done && o->pit == 0) activeY.push_back(o);
        }
        {
            std::vector<Car*> sorted = activeY;
            std::stable_sort(sorted.begin(), sorted.end(),
                      [](Car* a, Car* b) { return a->cautionSlot < b->cautionSlot; });
            for (size_t i = 0; i < sorted.size(); ++i) sorted[i]->cautionSlot = static_cast<int>(i);
        }
        state.cautionMaxSlot = static_cast<int>(activeY.size()) - 1;

        // index.html:4355-4392: time-compressed straggler warp.
        if (state.yellowT > 40 && !state.oneToGo) {
            for (auto* o : activeY) {
                if (o->isPlayer || o->spinT > 0) continue;
                double se = wrapHalf(pace.s - 16 - o->cautionSlot * 9 - o->s, track.total());
                if (se > 30) {
                    double sNew = wrapMod(o->s + se, track.total());
                    bool clear = true;
                    for (auto* other : activeY) {
                        if (other == o) continue;
                        double dso = std::abs(other->s - sNew);
                        if (dso > track.total() / 2) dso = track.total() - dso;
                        if (dso < 12) {
                            clear = false;
                            break;
                        }
                    }
                    if (clear) {
                        PointResult p = track.pointAt(sNew);
                        o->s = sNew;
                        o->lat = 0;
                        o->x = p.x;
                        o->y = p.y;
                        o->hdg = o->vdir = p.hdg;
                        o->v = 38;
                    }
                }
            }
        }

        // index.html:4393-4404: bunched check.
        int stragglers = 0;
        for (auto* o : activeY) {
            double se = wrapHalf(pace.s - 16 - o->cautionSlot * 9 - o->s, track.total());
            if (std::abs(se) > 25) ++stragglers;
        }
        const bool bunched = stragglers <= 3;

        // index.html:4405-4411: pit road opens once collected.
        if (!state.pitsOpen && !state.oneToGo && bunched) {
            state.pitsOpen = true;
        }

        if (!state.oneToGo) {
            // index.html:4412-4444: one-to-go transition.
            if ((lead->lap >= state.cautionUntilLap || state.yellowT > 120) && state.yellowT > 8 &&
                (bunched || state.yellowT > 75)) {
                state.oneToGo = true;
                state.pitsOpen = false;
            }
        } else {
            // index.html:4445-4460: pace car pulls in, green as leader nears the line.
            if (pace.state == "lead") {
                double dEntry = wrapHalf(track.segs()[0].s0 - 25 - pace.s, track.total());
                if (std::abs(dEntry) < 4) pace.state = "peel";
            }
            double dl = std::fmod(track.sFinish() - lead->s + track.total(), track.total());
            if (pace.state != "lead" && (dl < 60 || dl > track.total() - 10)) {
                state.flag = "green";
                state.greenT = 2.2;
                state.greenLockT = GREEN_LOCK_DUR;
                state.sinceGreenT = 0;
                state.oneToGo = false;
                state.pitsOpen = true;
                pace.state = "peel";
            }
        }
    }
}

// tick() (index.html:4180-4595) -- see race.h's comment for exactly what's
// deliberately not ported yet.
void tick(RaceState& state, std::vector<Car>& cars, PaceCar& pace, const Track& track,
          Mulberry32& rngR, const PlayerInput& input, std::vector<Car*>& finishOrder) {
    state.t += DT;

    if (state.mode == "pace") {
        stepPace(pace, state, track);
        if (pace.state != "lead") state.paceV = std::min(46.0, state.paceV + 3.4 * DT);
    }

    updateAero(cars, track);
    for (auto& c : cars) stepCar(c, state, track, cars, pace, input);
    collide(cars, state, track, rngR);

    // S.order (index.html:4192): race-position order, descending.
    std::vector<Car*> order;
    order.reserve(cars.size());
    for (auto& c : cars) order.push_back(&c);
    std::stable_sort(order.begin(), order.end(), [](Car* a, Car* b) {
        double pa = a->done ? 1e6 - a->finishT : a->prog;
        double pb = b->done ? 1e6 - b->finishT : b->prog;
        return pb < pa; // descending
    });

    if (state.mode == "pace") {
        for (auto& c : cars) {
            if (c.lap >= 0) {
                state.mode = "race";
                state.greenT = 2.2;
                state.greenLockT = GREEN_LOCK_DUR;
                state.sinceGreenT = 0;
                break;
            }
        }
    }
    if (state.greenLockT > 0 && state.flag == "green") state.greenLockT -= DT;
    if (state.flag == "green") state.sinceGreenT += DT;

    // index.html:4205-4208: qualifying flying-lap complete. Only the
    // mode='qual'->'menuwait' physics transition is ported here -- the
    // setTimeout(()=>finishQualifying(...), 400) that follows is menu-flow
    // (builds a synthesized grid and calls startRace()), not physics.
    Car* player = nullptr;
    for (auto& c : cars) {
        if (c.isPlayer) {
            player = &c;
            break;
        }
    }
    if (state.mode == "qual" && player && player->lap >= 1) {
        state.mode = "menuwait";
    }

    // index.html:4209-4218: AI pit strategy.
    if (state.mode == "race") {
        for (auto& c : cars) {
            if (c.isPlayer || c.done || c.out || c.pit > 0 || c.spinT > 0) continue;
            c.pitReq = (state.flag == "yellow" && state.pitsOpen && (c.wear > 0.25 || c.fuel < 0.5)) ||
                       c.wear > 0.7 || c.fuel < 0.18 || (c.dmg > 0.45 && c.dmg < 1);
        }
    }

    // index.html:4219-4250: blowouts and terminal-damage DNFs.
    if (state.mode == "race" || state.mode == "victory") {
        for (auto& c : cars) {
            if (c.done || c.out || c.pit > 0) continue;
            if (c.wear > 0.92 && !c.blown && c.v > 25 && rngR.next() < 0.0004) {
                c.blown = true;
                c.hitFx = 1;
                if (c.isPlayer) spotterSay(state, "FLAT TIRE — PIT NOW!");
                if (state.flag == "green" && state.greenLockT <= 0) {
                    c.spinT = 1.8 + rngR.next() * 1.2;
                    c.spinDir = rngR.next() < 0.5 ? -1 : 1;
                    c.spinCd = 10;
                }
            }
            if (c.dmg >= 1 && !c.out) {
                c.out = true;
                if (c.isPlayer) spotterSay(state, "TOO MUCH DAMAGE — WE’RE DONE");
            }
        }
    }

    cautionController(state, cars, pace, track, order);

    // index.html:4463-4545: green-white-checkered state machine, then the
    // unconditional finish-line arbitration.
    if (state.mode == "race") {
        Car* gwcLead = activeLead(order);
        if (gwcLead && !gwcLead->done && !gwcLead->out) {
            if (state.gwcState == "none") {
                if (state.flag == "yellow" && gwcLead->lap >= state.finishLaps) {
                    if (state.gwcAttempts < GWC_MAX_ATTEMPTS) {
                        state.finishLaps += 1;
                        state.gwcState = "watch";
                        state.gwcAttempts++;
                    } else {
                        state.finishLaps = gwcLead->lap;
                    }
                }
            } else if (state.gwcState == "watch") {
                if (state.flag == "yellow" && gwcLead->lap >= state.finishLaps) {
                    state.finishLaps = gwcLead->lap + 1;
                }
                if (state.flag == "green") {
                    state.gwcMarkLap = gwcLead->lap;
                    state.finishLaps = gwcLead->lap + 3;
                    state.gwcState = "clean1";
                }
            } else if (state.gwcState == "clean1") {
                if (state.flag == "yellow") {
                    state.gwcState = "none";
                } else if (gwcLead->lap >= state.gwcMarkLap + 2) {
                    state.finishLaps = gwcLead->lap + 1;
                    state.gwcState = "white";
                }
            } else if (state.gwcState == "white") {
                if (state.flag == "yellow") state.gwcState = "none";
            }
        }

        for (auto& c : cars) {
            if (!c.done && !c.out && c.lap >= state.finishLaps) {
                c.done = true;
                c.finishT = state.t;
                finishOrder.push_back(&c);
            }
        }
    }

    // index.html:4547: greenT is HUD/audio-only (crowd noise, banner flash)
    // but the field is real and already exists, so keep it in sync.
    if (state.greenT > 0) state.greenT -= DT;

    // index.html:4549-4567: spotter -- alongside calls + laps-to-go/fuel/
    // tire/damage one-shot messages. Player-relative (S.player == the sole
    // isPlayer car); JS's S.msgT/S.msgTxt (a separate, unrelated broadcast-
    // banner system) are NOT ported here -- see race.h's own "not ported"
    // list.
    if (state.spotT > 0) state.spotT -= DT;
    if (state.mode == "race" && player && !player->done && player->pit == 0) {
        bool inside = false, outside = false;
        for (auto& o : cars) {
            if (&o == player || o.done || o.pit > 0) continue;
            double ds = o.s - player->s;
            if (ds < -track.total() / 2) ds += track.total();
            if (ds > track.total() / 2) ds -= track.total();
            if (std::abs(ds) < 5.5 && std::abs(o.lat - player->lat) < 3.4) {
                if (o.lat < player->lat) inside = true;
                else outside = true;
            }
        }
        if (inside && state.spotState != "in") {
            spotterSay(state, outside ? "THREE WIDE!" : "INSIDE!");
            state.spotState = "in";
        } else if (outside && !inside && state.spotState != "out") {
            spotterSay(state, "OUTSIDE!");
            state.spotState = "out";
        } else if (!inside && !outside && (state.spotState == "in" || state.spotState == "out")) {
            spotterSay(state, "CLEAR");
            state.spotState = "clear";
        }
        if (player->lap == state.laps - 3 && !state.togoMsg && state.laps >= 6) {
            state.togoMsg = true;
            spotterSay(state, "3 TO GO");
        }
        if (player->fuel < 0.15 && !state.fuelMsg) {
            state.fuelMsg = true;
            spotterSay(state, "FUEL LOW — PIT SOON");
        }
        if (player->wear > 0.85 && !state.tireMsg) {
            state.tireMsg = true;
            spotterSay(state, "TIRES ARE GONE");
        }
        if (player->dmg > 0.6 && !state.dmgMsg) {
            state.dmgMsg = true;
            spotterSay(state, "HEAVY DAMAGE — PIT FOR REPAIRS");
        }
    }

    // index.html:4581-4594: player finish -> victory/done, player DNF ->
    // done, and the victory-lap timeout back to done. setTimeout(showResults,
    // ...) calls are menu-flow, not ported.
    if (player && player->done && state.mode == "race") {
        if (!finishOrder.empty() && finishOrder[0] == player) {
            state.mode = "victory";
            state.victoryT = 0;
        } else {
            state.mode = "done";
        }
    }
    if (player && player->out && state.mode == "race" && player->v < 1) {
        state.mode = "done";
    }
    if (state.mode == "victory") {
        state.victoryT += DT;
        if (state.victoryT > 6) state.mode = "done";
    }
}
