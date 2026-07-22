// Verifies cornerSpeed()/targetSpeed() (src/sim/car.{h,cpp}) against ground
// truth captured from the original JS functions (index.html:629-649), run
// under Node against the same Track/CAR formulas already verified in
// track_test/car_test. See PORT_PROGRESS.md Phase 1d notes for the script.

#include "../src/sim/car.h"
#include "../src/sim/tracks_data.h"

#include <cmath>
#include <cstdio>

namespace {
int g_failures = 0;

void expectNear(const char* label, double got, double expected, double tol = 1e-6) {
    if (std::fabs(got - expected) > tol) {
        std::fprintf(stderr, "%s: got %.17g expected %.17g (diff %.3g)\n",
                     label, got, expected, got - expected);
        ++g_failures;
    }
}
} // namespace

int main() {
    Track t0(TRACKS[0]);
    Track t3(TRACKS[3]);

    expectNear("cornerSpeed T0 R=140,s=400,wear=0",
               cornerSpeed(t0, 140, 400, 0), 127.42794452399392);
    expectNear("cornerSpeed T0 R=100,s=900,wear=0.5",
               cornerSpeed(t0, 100, 900, 0.5), 40.435221729047200);
    expectNear("cornerSpeed T3 R=240,s=600,wear=0.2",
               cornerSpeed(t3, 240, 600, 0.2), 320.82072783979334);

    Car car1{};
    car1.s = 350;
    car1.v = 60;
    car1.wear = 0;
    car1.skill = 0.95;
    expectNear("targetSpeed T0 car1", targetSpeed(t0, car1), 95.0);

    Car car2{};
    car2.s = 1500;
    car2.v = 90;
    car2.wear = 0.3;
    car2.skill = 1.0;
    expectNear("targetSpeed T0 car2", targetSpeed(t0, car2), 62.119793139521761);

    Car car3{};
    car3.s = 100;
    car3.v = 40;
    car3.wear = 0.1;
    car3.skill = 0.9;
    expectNear("targetSpeed T3 car3", targetSpeed(t3, car3), 95.0);

    Car car4{};
    car4.s = 450;
    car4.v = 70;
    car4.wear = 0.15;
    car4.skill = 0.92;
    expectNear("targetSpeed T0 car4", targetSpeed(t0, car4), 95.0);

    Car car5{};
    car5.s = 700;
    car5.v = 85;
    car5.wear = 0;
    car5.skill = 1.0;
    expectNear("targetSpeed T3 car5", targetSpeed(t3, car5), 95.0);

    if (g_failures == 0) {
        std::printf("speed_model_test: cornerSpeed/targetSpeed match JS.\n");
        return 0;
    }
    std::fprintf(stderr, "speed_model_test: %d MISMATCHES.\n", g_failures);
    return 1;
}
