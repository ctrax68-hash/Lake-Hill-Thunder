#include "gap_time.h"

std::optional<double> gapTimeAt(const std::deque<ProgSample>& hist, double targetProg) {
    if (hist.size() < 2) return std::nullopt;
    for (size_t k = hist.size() - 1; k > 0; --k) {
        const ProgSample& a = hist[k - 1];
        const ProgSample& b = hist[k];
        if ((a.prog <= targetProg && b.prog >= targetProg) || (a.prog >= targetProg && b.prog <= targetProg)) {
            return b.prog == a.prog ? b.t : a.t + (targetProg - a.prog) * (b.t - a.t) / (b.prog - a.prog);
        }
    }
    return std::nullopt;
}
