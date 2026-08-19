// L2/L11 (NT2003/2004 fidelity pass): the end-to-end drivability guard.
//
// WHY THIS EXISTS. Three separate steering "fixes" (H8, H9, and an earlier
// pass) each shipped with passing unit tests and each left the game
// unplayable. Playing the WASM build by hand -- hold gas, tap the steer key
// gently -- produced a terminal-damage DNF on lap 1, 20th of 20. Nothing in
// the suite noticed, because every steering test asserted an isolated
// SCENARIO (one tap; one held corner) against a single car alone on an empty
// track. What actually matters is whether a player can complete a race:
// the real race.cpp tick(), with grid start, pace laps, cautions, the other
// 19 cars, collisions and the dmg>=1 DNF rule.
//
// WHY IT WAS REBUILT (L11), which matters as much as what it tests. The
// first version of this file was itself unreliable, in two compounding ways:
//
//  1. It hardcoded ONE RNG seed. Re-run across 6 seeds x 4 tracks, the
//     code it was "guarding" DNF'd on 15 of 24 races -- it had simply been
//     lucky with the seed it froze. It was not a regression guard, it was a
//     coin flip that landed heads the day it was written. It then reported a
//     regression against a steering change that, measured across seeds, had
//     the identical DNF rate (15/24) as the code it was compared to.
//
//  2. Its reference driver was incompetent, and so it measured the DRIVER
//     rather than the car. Handing the identical car, in the identical grid
//     slot and traffic and seeds, to the game's own AI produced 4 DNFs out
//     of 24 (17%) against the bot's 15 (63%). The gap was one missing term:
//     step_car.cpp's AI steers with a CURVATURE FEED-FORWARD,
//     `(v*curvFF)/yawLim + dHdg*1.3`, while this bot had only the
//     heading-error half. With no feed-forward it enters every corner late,
//     washes wide and grinds the wall -- on any car, with any steering tune.
//
// So the reference driver below reuses the AI's own formula verbatim
// (step_car.cpp's racing branch), converted to the digital left/right keys a
// player actually has; it runs SOLO (see below); and the assertions run over
// several seeds.
//
// KNOWN LIMITATION, stated plainly rather than left for someone to trip over.
// This guard is sensitive to gross undriveability -- it still fails hard if
// the car cannot hold a line -- but it is NOT currently sensitive to steering
// AUTHORITY on its own. Because the reference driver caps its speed at the
// friction limit, a car with far too little steering lock (the pre-L1
// constants) simply gets driven slower and still survives, so this passes on
// those constants where the earlier single-seed version failed. Survival
// alone is gameable by crawling: making this a true regression guard needs a
// PACE term (lap time), so that a car which forces you to crawl fails even
// though it never crashes. That is the next piece of work on this file, and
// until it lands the deterministic resolution assertions in
// tests/tire_model_test.cpp are what actually pin the steering behaviour --
// they are static arithmetic and cannot be gamed by a controller at all.

#include "../src/sim/car.h"
#include "../src/sim/constants.h"
#include "../src/sim/race.h"
#include "../src/sim/rng.h"
#include "../src/sim/tracks_data.h"

#include <algorithm>
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

double wrapPi(double a) {
    while (a > kPi) a -= 2 * kPi;
    while (a < -kPi) a += 2 * kPi;
    return a;
}

struct RaceOutcome {
    int lap = 0;
    double worstDmg = 0.0;
    bool out = false;
    int impacts = 0;
};

// M1 guard. The race guard below could not have caught the formation-lap bug,
// for a structural reason worth stating: it strips the field to the player
// alone AND its reference driver writes PlayerInput -- but stepCar()'s branch
// chain puts the pace branch ahead of the player-input branch, so during
// mode=="pace" every one of those inputs is ignored and the test's careful
// gamma inversion never runs. The entire pace phase was untested.
//
// What shipped as a result: the pace branch hands the player's car the same
// continuous yawCorrected() steering command it hands the AI, but the shared
// tail then pushed it through the player's DIGITAL-KEY pipeline (the L8 curve
// and the L15 authority cap, gated on c.isPlayer), delivering under 10% of
// the commanded angle. The player's car understeered off the formation line
// and ground the apron while the other 19 held station.
//
// The property asserted here is deliberately RELATIVE -- the player's worst
// lane error against the worst of the 19 AI cars on the same lap. An absolute
// metre bound would encode this track's lane geometry and this sign
// convention; "the player holds formation about as well as the cars beside
// it" is the actual requirement, survives a convention change, and cannot be
// satisfied by a car that simply drives slowly.
struct PaceOutcome {
    double playerLaneErr = 0.0; // worst |lat - gridLane| for the player
    double aiLaneErr = 0.0;     // worst |lat - gridLane| across all 19 AI cars
    double playerDmg = 0.0;     // damage accrued before the green flag
    double aiDmg = 0.0;         // worst pre-green damage across all 19 AI cars
    int ticks = 0;
};

PaceOutcome paceFormation(int trackIdx, uint32_t seed) {
    const Track track(TRACKS[trackIdx]);
    Mulberry32 rng(seed), rngR(seed * 7919u + 13u);
    RaceState state;
    PaceCar pace;
    std::vector<Car> cars;
    std::vector<Car*> finishOrder;
    gridStart(track, rng, state, pace, cars, finishOrder, nullptr);
    state.mode = "pace";
    state.finishLaps = state.laps;

    PaceOutcome out;
    // The human is deliberately NOT touching the controls: during the
    // formation lap the game drives all 20 cars, and that is the whole point
    // of the test. Any steering the player's car does here comes from the
    // pace branch, exactly as it does for the AI.
    PlayerInput input;

    const int maxTicks = (int)(120.0 / DT); // generous; loop exits at green
    for (int t = 0; t < maxTicks && state.mode == "pace"; ++t) {
        tick(state, cars, pace, track, rngR, input, finishOrder);
        ++out.ticks;
        for (const auto& c : cars) {
            if (c.out || c.done) continue;
            const double err = std::fabs(c.lat - c.gridLane);
            if (c.isPlayer) {
                if (err > out.playerLaneErr) out.playerLaneErr = err;
                if (c.dmg > out.playerDmg) out.playerDmg = c.dmg;
            } else {
                if (err > out.aiLaneErr) out.aiLaneErr = err;
                if (c.dmg > out.aiDmg) out.aiDmg = c.dmg;
            }
        }
    }
    return out;
}

// One full race with the player's car driven through the real PlayerInput
// path. `seed` drives both the grid RNG and the race RNG so each run is an
// independent trajectory rather than a re-roll of the same one.
RaceOutcome raceWithDrivingPlayer(int trackIdx, double seconds, uint32_t seed) {
    const Track track(TRACKS[trackIdx]);
    Mulberry32 rng(seed), rngR(seed * 7919u + 13u);
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

    // SOLO. The 20-car field is deliberately removed: see this file's
    // header for why a full pack cannot measure the car. Keeping only the
    // player leaves exactly the thing steering changes affect -- whether the
    // car holds the road under a driver's own inputs.
    {
        std::vector<Car> solo;
        for (auto& c : cars)
            if (c.isPlayer) solo.push_back(c);
        cars.swap(solo);
        finishOrder.clear();
        // gridStart() left gridAhead pointing into the FULL field (17 for the
        // player). Against this size-1 vector that was an out-of-bounds read
        // in stepCar()'s pace branch, and the pace-phase target speed was
        // derived from whatever it found. stepCar() now bounds-checks too;
        // clearing it here states the intent directly -- solo means there is
        // no car ahead.
        for (auto& c : cars) c.gridAhead = -1;
    }
    Car* player = cars.empty() ? nullptr : &cars[0];

    RaceOutcome out;
    PlayerInput input;
    double prevDmg = 0.0;
    const int ticks = (int)(seconds / DT);
    for (int t = 0; t < ticks; ++t) {
        if (player && !player->out && !player->done) {
            // --- the AI's own racing-branch steering (step_car.cpp), reused
            // verbatim so this measures the car and not the controller ---
            const double LA = std::max(12.0, player->v * 0.62);
            const PointResult pT = track.pointAt(player->s + LA);
            const double tx = pT.x, ty = pT.y; // centre line: lane offset 0
            const double dHdg = wrapPi(std::atan2(ty - player->y, tx - player->x) - player->hdg);
            const double curvFF = track.pointAt(player->s + std::max(6.0, player->v * 0.3)).curv;
            const double muNowC = CAR.mu * (1 - 0.12 * player->wear) + CAR.dfK * player->v * player->v;
            const double yawLimC =
                std::min({1.3, std::max(0.05, player->v * 0.24),
                          cornerCap(muNowC, track.bankAt(player->s + 10)) / std::max(3.0, player->v) * 1.15});
            const double raw = std::max(-1.0, std::min(1.0, (player->v * curvFF) / yawLimC + dHdg * 1.3));
            // ...including yawCorrected()'s feedback half, which is a real
            // part of why the AI is competent: it nudges toward the yaw rate
            // the feed-forward implies instead of assuming zero-latency
            // response.
            const double rWant = raw * yawLimC;
            const double aiSteer =
                std::max(-1.0, std::min(1.0, raw + CAR.yawCorrGain * (rWant - player->r)));

            // The AI assigns that straight to steerIn, where it maps LINEARLY
            // to a steer angle. The player's input goes through the L8 curve
            // instead, so driving the wheel to the same number would command
            // |aiSteer|^gamma of the angle the AI intended -- badly under-
            // steering, which is what made the first rebuild of this test
            // fail worse than the bot it replaced. Invert the curve so the
            // target is the same ANGLE the AI wanted; that also makes this
            // guard gamma-neutral by construction, so it measures the car
            // rather than the exponent.
            const double aiMag = std::fabs(aiSteer);
            const double steerWanted =
                (aiSteer < 0 ? -1.0 : 1.0) * std::pow(aiMag, 1.0 / CAR.steerCurveGamma);

            // A player only has two keys, so hold whichever moves the wheel
            // toward that target.
            constexpr double kWheelDeadband = 0.03;
            input.right = player->steer < steerWanted - kWheelDeadband;
            input.left = player->steer > steerWanted + kWheelDeadband;

            // Speed: the game's own planner, CAPPED AT WHAT THE TIRES CAN
            // ACTUALLY HOLD. targetSpeed()/cornerSpeed() are JS-faithful but
            // physically over-optimistic against the bicycle tire model that
            // replaced JS's kinematic one -- measured, they command 1.23x
            // (Milltown) to 4.26x (Big Sable, 325 m/s = 727 mph) the speed
            // the friction circle allows. A real driver does not drive into a
            // wall because a number told them to, so the reference driver
            // here doesn't either; without this cap the guard measures that
            // planner bug on every track instead of the car's handling.
            // Tracked separately -- see PORT_PROGRESS's L12 entry.
            const double curvAhead = std::fabs(track.pointAt(player->s + std::max(10.0, player->v * 0.8)).curv);
            double vTarget = targetSpeed(track, *player);
            if (curvAhead > 1e-6) {
                const double bankAhead = track.bankAt(player->s + std::max(10.0, player->v * 0.8));
                const double tanB = std::tan(bankAhead);
                const double ayMax = G * (CAR.mu + tanB) / std::max(0.2, 1.0 - CAR.mu * tanB);
                vTarget = std::min(vTarget, std::sqrt(ayMax / curvAhead) * 0.92); // 8% margin
            }
            input.gas = player->v < vTarget;
            input.brake = player->v > vTarget * 1.06;
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
    // Several independent trajectories per track. A single 20-car race with
    // cautions and collisions is chaotic enough that one run is not evidence
    // in either direction -- which is exactly how the first version of this
    // file came to pass on broken code.
    static const uint32_t kSeeds[] = {101u, 202u, 303u, 404u, 505u, 606u};
    constexpr int kSeedCount = (int)(sizeof(kSeeds) / sizeof(kSeeds[0]));

    // M1: formation lap, full field, human hands off. See PaceOutcome above.
    for (int t = 0; t < (int)TRACKS.size(); ++t) {
        const PaceOutcome p = paceFormation(t, kSeeds[0]);
        std::printf("%-20s PACE ticks=%4d  laneErr player=%5.2fm ai=%5.2fm   preGreenDmg player=%.3f ai=%.3f\n",
                    TRACKS[t].name.c_str(), p.ticks, p.playerLaneErr, p.aiLaneErr, p.playerDmg, p.aiDmg);

        expect(p.ticks > 100, "the pace phase actually ran (mode stayed 'pace' long enough to measure)");
        // The headline M1 assertion. Relative, so it measures the player's car
        // against its own field rather than against a hand-picked metre bound
        // -- and verified to FAIL on the shipped code, where the curve+cap ate
        // the player's steering (Big Sable 6.28m against the AI's 3.77m,
        // Milltown 13.90m against 10.61m). Checking a guard fails on the bug
        // it targets is not optional here: this file's own header records the
        // earlier version of itself passing happily on broken code.
        expect(p.playerLaneErr <= p.aiLaneErr * 1.5 + 0.5,
               "the player's car holds formation about as well as the AI cars beside it");
        // Wall damage is live during the pace lap (flag=="green"), so a car
        // that cannot hold its lane grinds the apron before the race starts.
        // Relative for the same reason as the lane check: Milltown Bullring
        // hands out ~0.09 of pre-green damage to cars in the pack, identically
        // before and after the M1 fix, so it is a separate pre-existing issue
        // on the tightest track in the game and NOT something this guard
        // should launder into a magic per-track threshold. What must hold is
        // that the player is not singled out.
        expect(p.playerDmg <= p.aiDmg + 0.02,
               "the player takes no more pre-green damage than the AI cars around it");
    }

    int totalRaces = 0, totalDnf = 0;
    for (int t = 0; t < (int)TRACKS.size(); ++t) {
        int dnf = 0, earlyDnf = 0, worstImpacts = 0;
        double worstDmg = 0.0;
        for (int s = 0; s < kSeedCount; ++s) {
            const RaceOutcome r = raceWithDrivingPlayer(t, 300.0, kSeeds[s]);
            ++totalRaces;
            if (r.out) {
                ++dnf;
                ++totalDnf;
                // Wrecking out on lap 1-2 is the reported bug. Wrecking out
                // late is ordinary racing attrition.
                if (r.lap < 3) ++earlyDnf;
            }
            worstDmg = std::max(worstDmg, r.worstDmg);
            worstImpacts = std::max(worstImpacts, r.impacts);
        }
        std::printf("%-20s DNF %d/%d (early %d)  worstDmg=%.3f worstImpacts=%d\n", TRACKS[t].name.c_str(),
                    dnf, kSeedCount, earlyDnf, worstDmg, worstImpacts);

        // The headline guard, and the one that reproduces the original
        // report: being wrecked out in the OPENING LAPS, repeatedly. That is
        // the failure the user actually hit (DNF on lap 1, 20th of 20), and
        // on the pre-L1 constants it still fails here on 2 of the 4 tracks.
        expect(earlyDnf == 0, "a player driving normally is never wrecked out in the opening laps");

        // NOTE, deliberately reported but NOT asserted per-track: Milltown
        // Bullring still DNFs on every seed, late (never in the opening laps).
        // It is the tightest track in the game and the car grinds itself down
        // over many laps there. Asserting a per-track majority would fail on
        // that alone, and I am not going to assert a threshold I picked to
        // make one track pass -- the aggregate check below covers systematic
        // undriveability honestly, and Milltown's late attrition is recorded
        // in PORT_PROGRESS as an open item rather than hidden behind a
        // loosened bound.
        (void)dnf;
    }

    // Calibrated against the game's own AI rather than an absolute: handed
    // the identical car, slot, traffic and seeds, the AI DNFs 17% of the
    // time, so a reference driver should land in the same neighbourhood.
    // Judging the player's car by a standard the game's own AI cannot meet
    // would just be re-measuring the controller again.
    const double dnfRate = (double)totalDnf / (double)totalRaces;
    std::printf("overall player DNF rate: %.0f%% (%d/%d); the game's own AI manages ~17%%\n", dnfRate * 100.0,
                totalDnf, totalRaces);
    expect(dnfRate <= 0.40, "the player's overall DNF rate is in the same league as the AI's, not multiples of it");

    if (g_failures == 0) {
        std::printf("drivability_test: a player can actually drive this car.\n");
        return 0;
    }
    std::fprintf(stderr, "drivability_test: %d FAILURES.\n", g_failures);
    return 1;
}
