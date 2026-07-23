// Verifies Phase 6b's (PORT_PROGRESS.md) sim-side spotter-message trigger
// logic and c.hitFx accumulation, ported into src/sim/race.cpp's tick()
// (index.html:1067,1227,4238-4247,4549-4567). Exercises the real tick()
// function end-to-end with a full 20-car gridStart() field -- the trigger
// conditions read post-physics car state (c.s/c.lat via track.project()),
// so this places cars via the same pointAt()+lateral-offset convention
// gridStart() itself uses, far away in track-position from the real grid
// rows, so the rest of the field can't interfere.

#include "../src/sim/car.h"
#include "../src/sim/race.h"
#include "../src/sim/rng.h"
#include "../src/sim/tracks_data.h"

#include <cmath>
#include <cstdio>

namespace {
int g_failures = 0;

void expectTrue(const char* label, bool cond) {
    if (!cond) {
        std::fprintf(stderr, "%s: FAILED\n", label);
        ++g_failures;
    }
}
void expectEqStr(const char* label, const std::string& got, const std::string& expected) {
    if (got != expected) {
        std::fprintf(stderr, "%s: got '%s' expected '%s'\n", label, got.c_str(), expected.c_str());
        ++g_failures;
    }
}

// Places `c` at track-position `s`, lateral offset `lat`, matching
// gridStart()'s own x/y/hdg/s/lat convention (race.cpp:37-40) so a fresh
// tick()'s stepCar()/track.project() round-trip reprojects back to
// approximately the same s/lat.
void placeAt(const Track& track, Car& c, double s, double lat) {
    PointResult p = track.pointAt(s);
    c.x = p.x - std::sin(p.hdg) * lat;
    c.y = p.y + std::cos(p.hdg) * lat;
    c.hdg = c.vdir = p.hdg;
    c.s = s;
    c.lat = lat;
}

struct Field {
    Track track;
    Mulberry32 rng{12345};
    RaceState state;
    PaceCar pace;
    std::vector<Car> cars;
    std::vector<Car*> finishOrder;
    Car* player = nullptr;
    Car* neighborA = nullptr; // cars[0], idx 1
    Car* neighborB = nullptr; // cars[1], idx 2

    Field() : track(TRACKS[0]) {
        gridStart(track, rng, state, pace, cars, finishOrder, nullptr);
        state.mode = "race";
        state.flag = "green";
        for (auto& c : cars) {
            if (c.isPlayer) player = &c;
        }
        neighborA = &cars[0];
        neighborB = &cars[1];
        // Move the whole synthetic scenario to s=400 -- far from gridStart()'s
        // own back-straight rows (s ~= 786-886 on TRACKS[0]), so the other 17
        // AI cars can't wander into range during the single tick() call below.
        placeAt(track, *player, 400.0, 0.0);
    }
};
} // namespace

int main() {
    Mulberry32 rngR(555);
    PlayerInput input;

    // ---- INSIDE! ----
    {
        Field f;
        placeAt(f.track, *f.neighborA, 400.0 + 4.0, -2.5); // ds=4 (<5.5), lat=-2.5 (<player's 0 => inside)
        tick(f.state, f.cars, f.pace, f.track, rngR, input, f.finishOrder);
        expectEqStr("INSIDE! message", f.state.spotTxt, "INSIDE!");
        expectEqStr("INSIDE! spotState", f.state.spotState, "in");
        expectTrue("INSIDE! spotT armed", f.state.spotT > 0);
    }

    // ---- OUTSIDE! ----
    {
        Field f;
        placeAt(f.track, *f.neighborA, 400.0 - 4.0, 2.5); // ds=-4 (<5.5), lat=2.5 (>player's 0 => outside)
        tick(f.state, f.cars, f.pace, f.track, rngR, input, f.finishOrder);
        expectEqStr("OUTSIDE! message", f.state.spotTxt, "OUTSIDE!");
        expectEqStr("OUTSIDE! spotState", f.state.spotState, "out");
    }

    // ---- THREE WIDE! (both an inside and an outside car at once) ----
    {
        Field f;
        placeAt(f.track, *f.neighborA, 400.0 + 4.0, -2.5); // inside
        placeAt(f.track, *f.neighborB, 400.0 - 4.0, 2.5);  // outside
        tick(f.state, f.cars, f.pace, f.track, rngR, input, f.finishOrder);
        expectEqStr("THREE WIDE! message", f.state.spotTxt, "THREE WIDE!");
        expectEqStr("THREE WIDE! spotState", f.state.spotState, "in");
    }

    // ---- CLEAR (transition back from a prior 'in'/'out' state) ----
    {
        Field f;
        f.state.spotState = "in";
        placeAt(f.track, *f.neighborA, 400.0 + 40.0, 0.0); // far away: neither inside nor outside
        tick(f.state, f.cars, f.pace, f.track, rngR, input, f.finishOrder);
        expectEqStr("CLEAR message", f.state.spotTxt, "CLEAR");
        expectEqStr("CLEAR spotState", f.state.spotState, "clear");
    }

    // ---- one-shot laps-to-go/fuel/tire/damage warnings, each fires exactly
    // once even across repeated ticks ----
    {
        Field f;
        f.state.laps = 10;
        f.state.finishLaps = f.state.laps; // else the default finishLaps=5
                                            // would mark the player done
                                            // (lap>=finishLaps) before the
                                            // spotter block's own !done gate
        f.player->lap = f.state.laps - 3;
        f.player->fuel = 0.10;
        f.player->wear = 0.90;
        f.player->dmg = 0.70;
        tick(f.state, f.cars, f.pace, f.track, rngR, input, f.finishOrder);
        // Only the last-fired message's text survives in spotTxt (each
        // spotterSay() call overwrites it, exactly matching JS's own
        // single shared S.spotTxt), so check the four latch flags directly
        // -- that's what proves each one-shot condition actually fired.
        expectTrue("togoMsg latched", f.state.togoMsg);
        expectTrue("fuelMsg latched", f.state.fuelMsg);
        expectTrue("tireMsg latched", f.state.tireMsg);
        expectTrue("dmgMsg latched", f.state.dmgMsg);

        // A second tick with the same conditions must NOT re-fire (JS's
        // `S.togoMsg !== 1` etc. guards) -- spy via spotT: force it to 0
        // first, tick again, and confirm none of the one-shot messages
        // reset it (a stray alongside transition could also set spotT, so
        // keep the neighbors far away here, as Field's constructor does).
        f.state.spotT = 0;
        f.state.spotTxt.clear();
        tick(f.state, f.cars, f.pace, f.track, rngR, input, f.finishOrder);
        expectTrue("one-shot messages don't re-fire", f.state.spotTxt.empty() && f.state.spotT <= 0);
    }

    // ---- blowout: c.hitFx set to 1, spotterSay fired for the player.
    // Genuinely RNG-gated (rngR.next() < 0.0004 per tick), so this re-forces
    // wear/v every iteration and runs real tick()s (with the whole 20-car
    // field) until it fires -- expected around ~2500 iterations, capped far
    // higher. Only the player's own fields are asserted below, so whatever
    // the other 19 (unmodified) AI cars get up to over the run (a caution
    // flag, pit stops, laps completed) can't affect the result. ----
    {
        Field f;
        bool triggered = false;
        for (int i = 0; i < 200000 && !triggered; ++i) {
            f.player->wear = 0.95;
            f.player->v = 31;
            // With no steering input the player drives straight while the
            // track curves, eventually taking wall damage; left unchecked
            // across 200000 ticks that accumulates to a terminal DNF (c.out),
            // which would gate the blowout roll off entirely (its own guard
            // skips `c.out` cars) before the RNG ever gets a fair shot. Reset
            // both every iteration so only the wear/v/rngR gate is measured.
            f.player->dmg = 0.0;
            f.player->out = false;
            tick(f.state, f.cars, f.pace, f.track, rngR, input, f.finishOrder);
            triggered = f.player->blown;
        }
        expectTrue("blowout eventually triggers", triggered);
        expectTrue("blowout sets hitFx to 1", f.player->hitFx == 1.0);
        expectEqStr("blowout spotter message", f.state.spotTxt, "FLAT TIRE — PIT NOW!");
    }

    // ---- terminal damage: c.out flips, spotterSay fired for the player.
    // dmg=1.0 also satisfies dmgMsg's own dmg>0.6 threshold, and the
    // one-shot spotter block runs AFTER this DNF check within the same
    // tick() -- in mode='race' both genuinely fire in the same tick and
    // dmgMsg's "HEAVY DAMAGE..." legitimately overwrites spotTxt last,
    // exactly matching JS's own ordering. mode='victory' isolates just the
    // DNF message, since the one-shot block is gated to mode=='race' only. ----
    {
        Field f;
        f.state.mode = "victory";
        f.player->dmg = 1.0;
        tick(f.state, f.cars, f.pace, f.track, rngR, input, f.finishOrder);
        expectTrue("terminal damage sets out", f.player->out);
        expectEqStr("terminal damage spotter message", f.state.spotTxt, "TOO MUCH DAMAGE — WE’RE DONE");
    }

    if (g_failures == 0) {
        std::printf("spotter_test: all spotter-message trigger conditions and hitFx accumulation match.\n");
        return 0;
    }
    std::fprintf(stderr, "spotter_test: %d MISMATCHES.\n", g_failures);
    return 1;
}
