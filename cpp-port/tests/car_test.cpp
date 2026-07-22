// Verifies makeCar() (src/sim/car.{h,cpp}) against ground-truth
// skill/aggr/grooveBias values captured from the original JS makeCar()
// (index.html:453-503), run under Node with the same seed-12345 rng stream
// and the same default grid order (idx 1..19 then player idx 0) gridStart()
// uses when no qualifying order is set. Regenerate via the JS algorithm
// directly if this ever needs updating -- see PORT_PROGRESS.md Phase 1c notes.

#include "../src/sim/car.h"
#include "../src/sim/tracks_data.h"

#include <cmath>
#include <cstdio>

namespace {
int g_failures = 0;

void expectNear(const char* label, double got, double expected, double tol = 1e-9) {
    if (std::fabs(got - expected) > tol) {
        std::fprintf(stderr, "%s: got %.17g expected %.17g (diff %.3g)\n",
                     label, got, expected, got - expected);
        ++g_failures;
    }
}
} // namespace

int main() {
    Track track(TRACKS[0]); // Thunder Oval; makeCar()'s spawn point doesn't
                             // depend on which track, only pointAt(0)'s formula,
                             // already verified in track_test.
    Mulberry32 rng(12345);

    // clang-format off
    struct Expect { int idx; double skill, aggr, grooveBias; };
    const Expect expected[] = {
        {1,  0.98327690275968060, 0.58405135869979863,  0.99052325291559096},
        {2,  0.96952442506328229, 0.70565702160820365,  5.9084831162821505},
        {3,  0.90626939105568460, 0.85983788040466602, -3.7019041363615544},
        {4,  0.97012691123411066, 0.67596092410385611,  1.2675065116025508},
        {5,  0.97570295850164257, 0.98160621132701631,  6.0765963168349115},
        {6,  0.92115018234471790, 0.68400570349767809, -4.1169729111250488},
        {7,  0.96565561795257970, 0.91910492605529726,  1.0878811737056822},
        {8,  0.96631271166610533, 0.87239194237627093,  6.0917494502384217},
        {9,  0.93850850166869348, 0.44402201878838243, -4.1042158514726905},
        {10, 0.91747483185841705, 0.85134098227135835,  0.76066504037007687},
        {11, 0.92373119905358181, 0.43925924650393428,  6.0775129187386483},
        {12, 0.97882043936988339, 0.60866431067697702, -4.1728068915661423},
        {13, 0.90014944485272286, 0.80872786208055913,  1.1855139343068004},
        {14, 0.90411106558516618, 0.75835230229422446,  6.1215154497418549},
        {15, 0.97964245147770268, 0.42216932978481059, -3.9612371467053888},
        {16, 0.97667035547900016, 0.75246081370860340,  1.2051401076372712},
        {17, 0.93922473837737930, 0.55654988610185685,  5.8581372377928345},
        {18, 0.94398861184250571, 0.83130577905103564, -4.1167248059529813},
        {19, 0.90804477835888975, 0.91348427408374844,  0.96103192572481932},
        {0,  1.0000000000000000,  1.0000000000000000,   0.0000000000000000},
    };
    // clang-format on

    for (const auto& e : expected) {
        Car c = makeCar(e.idx == 0, e.idx, track, rng);
        char label[64];
        std::snprintf(label, sizeof(label), "idx=%d skill", e.idx);
        expectNear(label, c.skill, e.skill);
        std::snprintf(label, sizeof(label), "idx=%d aggr", e.idx);
        expectNear(label, c.aggr, e.aggr);
        std::snprintf(label, sizeof(label), "idx=%d grooveBias", e.idx);
        expectNear(label, c.grooveBias, e.grooveBias);

        if (e.idx == 0) {
            if (!c.isPlayer || c.scheme != nullptr || c.num != 21) {
                std::fprintf(stderr, "idx=0 player-car invariants broken\n");
                ++g_failures;
            }
        } else {
            if (c.isPlayer || c.scheme == nullptr || c.name != ROSTER[e.idx - 1].name) {
                std::fprintf(stderr, "idx=%d AI-car invariants broken\n", e.idx);
                ++g_failures;
            }
        }
    }

    if (g_failures == 0) {
        std::printf("car_test: all 20 cars' makeCar() output matches JS bit-for-bit.\n");
        return 0;
    }
    std::fprintf(stderr, "car_test: %d MISMATCHES -- makeCar() port diverges from JS.\n", g_failures);
    return 1;
}
