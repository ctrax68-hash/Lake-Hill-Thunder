// L2 (NT2003/2004 fidelity pass): the end-to-end drivability guard.
//
// Why this test exists, stated plainly: three separate steering "fixes"
// (H8, H9, and an earlier pass) each shipped with passing unit tests and
// each left the game unplayable. Playing the WASM build by hand -- hold
// gas, tap the steer key gently -- produced a terminal-damage DNF on lap 1,
// 20th of 20. Nothing in the suite noticed, because every steering test
// asserted an isolated SCENARIO (a single tap; one held corner) against a
// single car alone on an empty track.
//
// The thing that actually matters is none of those: it is whether a player
// can complete a race. That means the real race.cpp tick() -- grid start,
// pace laps, green flag, cautions, the other 19 cars, collisions, damage
// accumulation, the dmg>=1 DNF rule -- with the player car driven through
// the real PlayerInput path. Most of the damage in a real race comes from
// traffic, not from the car's own handling in isolation, so a single-car
// harness systematically under-reports the problem: measured on the
// shipped-at-the-time constants, the lone-car harness saw ~0.97 damage
// while the full race saw the player wrecked out entirely on 3 of 4 tracks
// with 50-82 separate impacts.
//
// The controller below is deliberately simple and human-shaped: aim at a
// point down the road (which gives curvature feed-forward for free -- a
// pure lateral-error controller always lags on a curved track and rides the
// wall regardless of how the car is tuned), ease off when the car is
// already rotating, and lift for corners using the game's own targetSpeed()
// planner. It is not a superhuman driver. If it cannot get around, a person
// cannot either.

#include "../src/sim/car.h"
#include "../src/sim/constants.h"
#include "../src/sim/race.h"
#include "../src/sim/rng.h"
#include "../src/sim/tracks_data.h"

#include <cmath>
#include <cstdio>
#include <vector>

namespace {
constexpr double kPi = 3.14159265358979323846;
int g_failures = 0;

void expect(bool cond, const char* what) {
    if (!cond) {
        std::fprintf(stderr, "FAIL: %s\n", what);
        ++g_failures;
    }
}

struct RaceOutcome {
    int lap = 0;
    double worstDmg = 0.0;
    bool out = false;
    int impacts = 0;
};

// Runs a full race on `trackIdx` for at most `seconds` of simulated time,
// driving the player with the lane-holding controller described above.
RaceOutcome raceWithDrivingPlayer(int trackIdx, double seconds) {
    const Track track(TRACKS[trackIdx]);
    Mulberry32 rng(12345u), rngR(555u);
    RaceState state;
    PaceCar pace;
    std::vector<Car> cars;
    std::vector<Car*> finishOrder;
    gridStart(track, rng, state, pace, cars, finishOrder, nullptr);
    // Mirrors main.cpp's startRaceFromMenu(): gridStart() alone leaves
    // mode=="menu", which skips tick()'s entire race block -- including the
    // dmg>=1 DNF rule, so a thoroughly wrecked player would report out=0 and
    // this test would pass for the wrong reason.
    state.mode = "pace";
    state.finishLaps = state.laps;

    Car* player = nullptr;
    for (auto& c : cars)
        if (c.isPlayer) player = &c;

    RaceOutcome out;
    PlayerInput input;
    double prevDmg = 0.0;
    const int ticks = (int)(seconds / DT);
    for (int t = 0; t < ticks; ++t) {
        if (player && !player->out && !player->done) {
            const ProjectResult pr = track.project(player->x, player->y);
            const double lookahead = std::max(15.0, player->v * 0.9);
            const PointResult aim = track.pointAt(pr.s + lookahead);
            double dHdg = std::atan2(aim.y - player->y, aim.x - player->x) - player->hdg;
            while (dHdg > kPi) dHdg -= 2 * kPi;
            while (dHdg < -kPi) dHdg += 2 * kPi;
            const double want = dHdg * 1.3 - player->r * 0.55; // -r = felt yaw, ease off
            const double vTarget = targetSpeed(track, *player);
            input.gas = player->v < vTarget;
            input.brake = player->v > vTarget * 1.06;
            input.left = want < -0.02;
            input.right = want > 0.02;
        } else {
            input = PlayerInput{};
        }

        tick(state, cars, pace, track, rngR, input, finishOrder);

        if (player) {
            if (player->dmg > out.worstDmg) out.worstDmg = player->dmg;
            if (player->dmg > prevDmg + 1e-9) ++out.impacts;
            prevDmg = player->dmg;
        }
        if (state.mode == "done") break;
    }
    if (player) {
        out.lap = player->lap;
        out.out = player->out;
    }
    return out;
}
} // namespace

int main() {
    for (int t = 0; t < (int)TRACKS.size(); ++t) {
        const RaceOutcome r = raceWithDrivingPlayer(t, 300.0);
        std::printf("%-20s lap=%2d dmg=%.3f impacts=%2d %s\n", TRACKS[t].name.c_str(), r.lap, r.worstDmg,
                    r.impacts, r.out ? "DNF" : "survived");

        // The headline guard: the exact failure the user hit. On the
        // constants shipped before L1 this failed on 3 of the 4 tracks.
        expect(!r.out, "a player driving normally is never wrecked out of the race");
        // Damage short of a write-off is expected -- this controller does
        // trade paint in traffic, and so does a real driver. A write-off is
        // the DNF failure mode itself.
        expect(r.worstDmg < 0.95, "a player driving normally never accumulates a write-off");
        // Impact count is the sensitive early-warning signal: it collapsed
        // from 50/82/19/5 to 13/12/6/11 across the four tracks when L1
        // landed. A car that is fighting the driver racks these up long
        // before it actually writes itself off.
        expect(r.impacts < 30, "a player driving normally isn't in near-constant contact with walls/traffic");
        expect(r.lap >= 3, "a player driving normally gets meaningfully through the race distance");
    }

    if (g_failures == 0) {
        std::printf("drivability_test: a player can actually drive this car.\n");
        return 0;
    }
    std::fprintf(stderr, "drivability_test: %d FAILURES.\n", g_failures);
    return 1;
}
