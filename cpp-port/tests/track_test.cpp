// Verifies Track (src/sim/track.{h,cpp}) against ground-truth output
// captured from the original JS buildTrack()/pointAt()/bankAt()/project()
// (index.html:242-374) run under Node for all 4 tracks. Regenerate the
// expected values by running the exact JS algorithm directly if this ever
// needs updating -- see PORT_PROGRESS.md Phase 1b notes for the script used.

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

struct PointExpect {
    double s, x, y, hdg, curv, bank;
};
struct BankExpect {
    double s, bank;
};
struct ProjectExpect {
    double x, y, s, lat, dist, hdg;
};

void checkTrack(int idx, double expectedTotal, double expectedSFinish,
                const PointExpect* pts, int nPts,
                const ProjectExpect* projs, int nProjs) {
    Track t(TRACKS[idx]);
    char label[128];

    std::snprintf(label, sizeof(label), "track %d total", idx);
    expectNear(label, t.total(), expectedTotal, 1e-6);
    std::snprintf(label, sizeof(label), "track %d sFinish", idx);
    expectNear(label, t.sFinish(), expectedSFinish, 1e-6);

    for (int i = 0; i < nPts; ++i) {
        const auto& e = pts[i];
        PointResult p = t.pointAt(e.s);
        std::snprintf(label, sizeof(label), "track %d pointAt(%.4g).x", idx, e.s);
        expectNear(label, p.x, e.x, 1e-6);
        std::snprintf(label, sizeof(label), "track %d pointAt(%.4g).y", idx, e.s);
        expectNear(label, p.y, e.y, 1e-6);
        std::snprintf(label, sizeof(label), "track %d pointAt(%.4g).hdg", idx, e.s);
        expectNear(label, p.hdg, e.hdg, 1e-9);
        std::snprintf(label, sizeof(label), "track %d pointAt(%.4g).curv", idx, e.s);
        expectNear(label, p.curv, e.curv, 1e-9);
        std::snprintf(label, sizeof(label), "track %d pointAt(%.4g).bank", idx, e.s);
        expectNear(label, p.bank, e.bank, 1e-9);
    }

    for (int i = 0; i < nProjs; ++i) {
        const auto& e = projs[i];
        ProjectResult pr = t.project(e.x, e.y);
        std::snprintf(label, sizeof(label), "track %d project(%.4g,%.4g).s", idx, e.x, e.y);
        expectNear(label, pr.s, e.s, 1e-6);
        std::snprintf(label, sizeof(label), "track %d project(%.4g,%.4g).lat", idx, e.x, e.y);
        expectNear(label, pr.lat, e.lat, 1e-6);
        std::snprintf(label, sizeof(label), "track %d project(%.4g,%.4g).dist", idx, e.x, e.y);
        expectNear(label, pr.dist, e.dist, 1e-6);
        std::snprintf(label, sizeof(label), "track %d project(%.4g,%.4g).hdg", idx, e.x, e.y);
        expectNear(label, pr.hdg, e.hdg, 1e-9);
    }
}

} // namespace

int main() {
    // clang-format off
    // Track 0: THUNDER OVAL
    {
        PointExpect pts[] = {
            {0, 202.12244897959184, -119.84371330400396, -3.0905500844061331, 0.0, 0.0},
            {37.5, 164.67128857209059, -121.75697861012641, -3.0905500844061331, 0.0, 0.0},
            {123.456, 78.827236732432681, -126.14248881420804, -3.0905500844061331, 0.0, 0.0},
            {391.4894634597463, -188.85714285714286, -139.81766552133794, -3.0905500844061331, -0.0071428571428571426, 0.31415926535897931},
            {845.5043543337421, -188.95701443189455, 139.82273189395013, -6.3335135906489608, -0.0071428571428571426, 0.31415926535897931},
            {1600.834719620185, 203.12092244769482, -119.78853227038853, -3.0822167510727994, -0.0083333333333333332, 0.27925268031909273},
            {-50, 250.10112714533682, -107.11240843900438, -2.6738834177394657, -0.0083333333333333332, 0.27925268031909273},
            {1681.834719620185, 122.22664011025587, -123.92534595706518, -3.0905500844061331, 0.0, 0.0},
        };
        ProjectExpect projs[] = {
            {0,0, 195.74473172987314, -129.99999999999997, 130.00000000000000, -3.0905500844061331},
            {100,50, 1138.6678267421219, -74.963078640338495, 74.963078640338495, -0.051042569183660028},
            {-300,20, 645.14526920536434, -34.094381641010173, 34.094381641010173, -4.9023772683034048},
            {250,-10, 1441.4375667783347, -65.081879129016045, 65.081879129016059, -1.7539071440573806},
            {0,200, 1031.1450044309622, 69.739522173339921, 69.739522173339921, -0.051042569183660028},
            {400,0, 1419.4642687068367, 84.000000000000000, 84.000000000000000, -1.5707963267948966},
        };
        checkTrack(0, 1601.8347196201851, 195.74473172987314, pts, 8, projs, 6);
        // bankAt checked separately below (pointAt's own `bank` field is 0 for
        // straights even mid-corner-adjacent -- see track.h comment / JS quirk)
        Track t(TRACKS[0]);
        double bankExpect[] = {0.27925268031909273, 0.20163639222287327, 0.069813170079773182,
                               0.31415926535897931, 0.31415926535897931, 0.27925268031909273,
                               0.27925268031909273, 0.076128544062981376};
        double bankS[] = {0, 37.5, 123.456, 391.4894634597463, 845.5043543337421,
                          1600.834719620185, -50, 1681.834719620185};
        for (int i = 0; i < 8; ++i) {
            char label[64]; std::snprintf(label, sizeof(label), "track 0 bankAt(%.4g)", bankS[i]);
            expectNear(label, t.bankAt(bankS[i]), bankExpect[i], 1e-9);
        }
    }
    // Track 3: BIG SABLE SPEEDWAY (symmetric RL==RR, good edge-case check)
    {
        Track t(TRACKS[3]);
        double bankExpect[] = {0.40142572795869574, 0.33161486340958363, 0.087266462599716474,
                               0.40142572795869574, 0.40142572795869574, 0.40142572795869574,
                               0.40142572795869574, 0.16580627893946132};
        double bankS[] = {0, 37.5, 123.456, 546, 1299.8822368615504, 2598.9644737231006, -50, 2679.9644737231006};
        for (int i = 0; i < 8; ++i) {
            char label[64]; std::snprintf(label, sizeof(label), "track 3 bankAt(%.4g)", bankS[i]);
            expectNear(label, t.bankAt(bankS[i]), bankExpect[i], 1e-9);
        }
        ProjectExpect projs[] = {
            {0,0, 273.00000000000000, -240.00000000000000, 240.00000000000000, 3.1415926535897931},
            {100,50, 1672.9822368615503, -190.00000000000000, 190.00000000000000, 0.0000000000000000},
            {-300,20, 1076.0028299840615, -206.39940476717709, 206.39940476717712, -5.3499377785233824},
            {250,-10, 23.000000000000000, -230.00000000000000, 230.00000000000000, 3.1415926535897931},
            {0,200, 1572.9822368615503, -40.000000000000000, 40.000000000000000, 0.0000000000000000},
            {400,0, 2222.9733552923253, -113.00000000000000, 113.00000000000000, -1.5707963267948966},
        };
        checkTrack(3, 2599.9644737231006, 273.00000000000000, nullptr, 0, projs, 6);
    }
    // clang-format on

    if (g_failures == 0) {
        std::printf("track_test: all checks match JS buildTrack() bit-for-bit (within 1e-6/1e-9 tol).\n");
        return 0;
    }
    std::fprintf(stderr, "track_test: %d MISMATCHES -- Track port diverges from JS.\n", g_failures);
    return 1;
}
