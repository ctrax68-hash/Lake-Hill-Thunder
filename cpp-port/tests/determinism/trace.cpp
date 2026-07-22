#include "trace.h"

#include <cmath>
#include <fstream>
#include <sstream>
#include <stdexcept>
#include <unordered_map>

namespace {

bool toBool(const std::string& tok) { return tok != "0"; }

} // namespace

std::vector<TickSnapshot> loadTrace(const std::string& path) {
    std::ifstream in(path);
    if (!in) throw std::runtime_error("loadTrace: cannot open " + path);

    std::vector<TickSnapshot> ticks;
    std::string line;
    while (std::getline(in, line)) {
        if (line.empty()) continue;
        std::istringstream ls(line);
        std::string tag;
        ls >> tag;
        if (tag == "TICK") {
            TickSnapshot ts;
            std::string i;
            ls >> i >> ts.t >> ts.mode >> ts.flag >> ts.greenLockT >> ts.sinceGreenT >>
                ts.paceS >> ts.paceLat >> ts.paceV >> ts.paceState;
            ts.tick = std::stoi(i);
            ticks.push_back(std::move(ts));
        } else if (tag == "CAR") {
            if (ticks.empty()) throw std::runtime_error("loadTrace: CAR line before any TICK line");
            CarSnapshot c;
            std::string isPlayerTok, doneTok, dirtyTok, pitReqTok, outTok;
            ls >> c.idx >> isPlayerTok >> c.x >> c.y >> c.hdg >> c.v >> c.s >> c.lat >> c.lap >>
                c.prog >> doneTok >> c.finishT >> c.wear >> c.draftF >> dirtyTok >> c.skill >>
                c.aggr >> c.grooveBias >> c.passSide >> c.passT >> c.spinT >> c.spinDir >>
                c.spinCd >> c.pit >> c.pitT >> pitReqTok >> c.fuel >> c.dmg >> outTok >> c.cautionSlot;
            c.isPlayer = toBool(isPlayerTok);
            c.done = toBool(doneTok);
            c.dirty = toBool(dirtyTok);
            c.pitReq = toBool(pitReqTok);
            c.out = toBool(outTok);
            ticks.back().cars.push_back(c);
        } else {
            throw std::runtime_error("loadTrace: unrecognized line tag '" + tag + "'");
        }
    }
    return ticks;
}

namespace {

void checkNum(std::vector<FieldDiff>& out, int tick, int carIdx, const char* field,
              double got, double expected, double tol) {
    if (std::fabs(got - expected) > tol) {
        out.push_back({tick, carIdx, field, got, expected});
    }
}

void checkStr(std::vector<FieldDiff>& out, int tick, int carIdx, const char* field,
              const std::string& got, const std::string& expected) {
    if (got != expected) {
        // Encode string mismatch as a diff with NaN payload; the field name
        // and tick/car are what matters for reporting a string divergence.
        out.push_back({tick, carIdx, field, std::nan(""), std::nan("")});
    }
}

const CarSnapshot* findByIdx(const std::vector<CarSnapshot>& cars, int idx) {
    for (auto& c : cars) {
        if (c.idx == idx) return &c;
    }
    return nullptr;
}

} // namespace

std::vector<FieldDiff> diffTraces(const std::vector<TickSnapshot>& a,
                                   const std::vector<TickSnapshot>& b,
                                   double tol) {
    std::vector<FieldDiff> out;
    if (a.size() != b.size()) {
        out.push_back({-1, -1, "trace_length",
                        static_cast<double>(a.size()), static_cast<double>(b.size())});
        return out;
    }

    for (size_t i = 0; i < a.size(); ++i) {
        const TickSnapshot& ta = a[i];
        const TickSnapshot& tb = b[i];
        std::vector<FieldDiff> tickDiffs;

        checkNum(tickDiffs, ta.tick, -1, "t", ta.t, tb.t, tol);
        checkStr(tickDiffs, ta.tick, -1, "mode", ta.mode, tb.mode);
        checkStr(tickDiffs, ta.tick, -1, "flag", ta.flag, tb.flag);
        checkNum(tickDiffs, ta.tick, -1, "greenLockT", ta.greenLockT, tb.greenLockT, tol);
        checkNum(tickDiffs, ta.tick, -1, "sinceGreenT", ta.sinceGreenT, tb.sinceGreenT, tol);
        checkNum(tickDiffs, ta.tick, -1, "paceS", ta.paceS, tb.paceS, tol);
        checkNum(tickDiffs, ta.tick, -1, "paceLat", ta.paceLat, tb.paceLat, tol);
        checkNum(tickDiffs, ta.tick, -1, "paceV", ta.paceV, tb.paceV, tol);
        checkStr(tickDiffs, ta.tick, -1, "paceState", ta.paceState, tb.paceState);

        for (auto& ca : ta.cars) {
            const CarSnapshot* cb = findByIdx(tb.cars, ca.idx);
            if (!cb) {
                tickDiffs.push_back({ta.tick, ca.idx, "missing_car", 0, 0});
                continue;
            }
            checkNum(tickDiffs, ta.tick, ca.idx, "x", ca.x, cb->x, tol);
            checkNum(tickDiffs, ta.tick, ca.idx, "y", ca.y, cb->y, tol);
            checkNum(tickDiffs, ta.tick, ca.idx, "hdg", ca.hdg, cb->hdg, tol);
            checkNum(tickDiffs, ta.tick, ca.idx, "v", ca.v, cb->v, tol);
            checkNum(tickDiffs, ta.tick, ca.idx, "s", ca.s, cb->s, tol);
            checkNum(tickDiffs, ta.tick, ca.idx, "lat", ca.lat, cb->lat, tol);
            checkNum(tickDiffs, ta.tick, ca.idx, "lap", ca.lap, cb->lap, 0);
            checkNum(tickDiffs, ta.tick, ca.idx, "prog", ca.prog, cb->prog, tol);
            checkNum(tickDiffs, ta.tick, ca.idx, "wear", ca.wear, cb->wear, tol);
            checkNum(tickDiffs, ta.tick, ca.idx, "draftF", ca.draftF, cb->draftF, tol);
            checkNum(tickDiffs, ta.tick, ca.idx, "skill", ca.skill, cb->skill, tol);
            checkNum(tickDiffs, ta.tick, ca.idx, "aggr", ca.aggr, cb->aggr, tol);
            checkNum(tickDiffs, ta.tick, ca.idx, "grooveBias", ca.grooveBias, cb->grooveBias, tol);
            checkNum(tickDiffs, ta.tick, ca.idx, "passSide", ca.passSide, cb->passSide, 0);
            checkNum(tickDiffs, ta.tick, ca.idx, "passT", ca.passT, cb->passT, tol);
            checkNum(tickDiffs, ta.tick, ca.idx, "spinT", ca.spinT, cb->spinT, tol);
            checkNum(tickDiffs, ta.tick, ca.idx, "spinDir", ca.spinDir, cb->spinDir, 0);
            checkNum(tickDiffs, ta.tick, ca.idx, "spinCd", ca.spinCd, cb->spinCd, tol);
            checkNum(tickDiffs, ta.tick, ca.idx, "pit", ca.pit, cb->pit, 0);
            checkNum(tickDiffs, ta.tick, ca.idx, "pitT", ca.pitT, cb->pitT, tol);
            checkNum(tickDiffs, ta.tick, ca.idx, "fuel", ca.fuel, cb->fuel, tol);
            checkNum(tickDiffs, ta.tick, ca.idx, "dmg", ca.dmg, cb->dmg, tol);
            checkNum(tickDiffs, ta.tick, ca.idx, "cautionSlot", ca.cautionSlot, cb->cautionSlot, 0);
        }

        if (!tickDiffs.empty()) {
            // Stop at the first diverging tick: everything after is almost
            // certainly cascading noise from this same root cause, not
            // independent bugs worth separately enumerating.
            out.insert(out.end(), tickDiffs.begin(), tickDiffs.end());
            break;
        }
    }
    return out;
}
