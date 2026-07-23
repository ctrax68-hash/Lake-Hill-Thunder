#include "leaderboard.h"
#include "color.h"
#include "gap_time.h"
#include "ui_draw.h"

#include <bgfx/bgfx.h>

#include <algorithm>
#include <cmath>
#include <cstdio>

namespace {

constexpr int kTopN = 5; // index.html:3823's TOP_N

std::string lastWord(const std::string& name) {
    const auto pos = name.find_last_of(' ');
    return pos == std::string::npos ? name : name.substr(pos + 1);
}

std::string truncate9(const std::string& s) {
    return s.size() <= 9 ? s : s.substr(0, 9);
}

// dbgTextPrintf's _attr byte, same VGA text-mode palette as hud.cpp's own
// constants (this file doesn't share hud.cpp's anonymous-namespace copy,
// same duplication precedent as status_bars.cpp).
constexpr uint8_t kBlack = 0, kYellow = 14, kWhite = 15;

uint8_t attr(uint8_t fg, uint8_t bg) {
    return (uint8_t)((bg << 4) | fg);
}

} // namespace

std::vector<LeaderboardRow> buildLeaderboardRows(const std::vector<const Car*>& order,
                                                  const std::string& mode, const std::string& flag,
                                                  double simT, double trackTotal) {
    std::vector<LeaderboardRow> rows;
    if (order.empty()) return rows;

    int playerRank = -1;
    for (size_t i = 0; i < order.size(); ++i) {
        if (order[i]->isPlayer) { playerRank = (int)i; break; }
    }

    // index.html:3826-3830's list-building.
    std::vector<int> list;
    for (int i = 0; i < std::min(kTopN, (int)order.size()); ++i) list.push_back(i);
    const bool pinned = playerRank >= kTopN;
    if (pinned && playerRank >= 0) list.push_back(playerRank);

    // index.html:3946's broadcast-style alternating gap/name toggle.
    const bool showGaps =
        mode == "race" && flag == "green" && ((long long)std::floor(simT / 5.0)) % 2 == 1;
    // index.html:3947's lapEst fallback.
    const double lapEst = (order[0]->lastLapT > 0) ? order[0]->lastLapT : (trackTotal / 48.0);

    rows.reserve(list.size());
    for (size_t r = 0; r < list.size(); ++r) {
        const int i = list[r];
        const Car* c = order[i];

        LeaderboardRow row;
        row.rank = i + 1;
        row.carNum = c->num;
        row.isPlayerRow = c->isPlayer;
        row.col = c->col;
        row.dividerBefore = pinned && (r == list.size() - 1);

        // index.html:3959-3970's tag precedence: default name/YOU, then
        // out > pit > spinT > (gap-or-leader, only while showGaps).
        std::string tag = c->isPlayer ? "YOU" : lastWord(c->name);
        if (c->out) {
            tag = "OUT";
        } else if (c->pit > 0) {
            tag = "PIT";
        } else if (c->spinT > 0) {
            tag = "WRECK";
        } else if (showGaps && i > 0) {
            const Car* ahead = order[i - 1];
            const double dp = ahead->prog - c->prog;
            const std::optional<double> shiftT = dp < 1.0 ? gapTimeAt(ahead->progHist, c->prog) : std::nullopt;
            const double gap = shiftT.has_value() ? (simT - *shiftT) : (dp * lapEst);
            if (dp >= 1.0) {
                tag = "-" + std::to_string((long long)std::floor(dp)) + " LAP";
            } else {
                char buf[32];
                std::snprintf(buf, sizeof(buf), "+%.1f", gap);
                tag = buf;
            }
        } else if (showGaps && i == 0) {
            tag = "LEADER";
        }
        row.tag = truncate9(tag);
        rows.push_back(row);
    }
    return rows;
}

void drawLeaderboard(const LeaderboardBox& box, const std::vector<LeaderboardRow>& rows, bool flagYellow,
                      std::vector<PosColorVertex>& uiOut) {
    if (rows.empty()) return;

    constexpr float kCellW = 8.0f, kCellH = 16.0f;
    const int col = (int)(box.x / kCellW);
    const int headerRow = (int)(box.y / kCellH);

    // index.html:3941-3944's yellow header banner.
    bgfx::dbgTextPrintf(col, headerRow, attr(kBlack, kYellow), "%s", flagYellow ? "CAUTION" : "LAKE HILL 400");

    for (size_t r = 0; r < rows.size(); ++r) {
        const int row = headerRow + 1 + (int)r;
        const LeaderboardRow& lr = rows[r];

        if (lr.dividerBefore) {
            // index.html:3951-3954's thin divider above the pinned row.
            const float y = (float)row * kCellH;
            pushLineSegment(uiOut, box.x, y, box.x + box.w, y, 1.0f, packColor(Theme::kSteel));
        }

        const uint8_t textAttr = lr.isPlayerRow ? attr(kBlack, kYellow) : attr(kWhite, kBlack);
        bgfx::dbgTextPrintf(col, row, textAttr, "%2d", lr.rank);

        const float chipX = (float)(col + 3) * kCellW;
        const float chipY = (float)row * kCellH + 3.0f;
        pushQuad(uiOut, chipX, chipY, 8.0f, 10.0f, packColor((float)lr.col[0], (float)lr.col[1], (float)lr.col[2]));

        bgfx::dbgTextPrintf(col + 6, row, textAttr, "#%-3d %s", lr.carNum, lr.tag.c_str());
    }
}
