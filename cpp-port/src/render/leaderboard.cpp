#include "leaderboard.h"
#include "color.h"
#include "gap_time.h"
#include "hud_text.h"
#include "ui_draw.h"

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

// G27: the row grid, in pixels. The 16px pitch is kept from the dbgText era
// because hud.cpp sizes this panel as `16 + 16 * rows` and decides how many
// rows fit against the lap-time plate below -- the pitch is load-bearing
// outside this file.
constexpr float kRowH = 16.0f;
constexpr float kHeaderH = 16.0f;
constexpr float kPadX = 6.0f;
constexpr float kRankW = 22.0f;  // right-aligned two-digit rank
constexpr float kChipW = 8.0f, kChipH = 10.0f;
constexpr float kNumW = 34.0f;   // "#12"

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
                     std::vector<PosColorVertex>& uiOut, std::vector<PosColorUvVertex>& textOut) {
    if (rows.empty()) return;

    // index.html:3941-3944's yellow header banner. dbgText could only invert a
    // cell's background attribute to get this; with a real font the banner is
    // an actual quad, so it spans the panel rather than just the glyphs.
    pushQuad(uiOut, box.x, box.y, box.w, kHeaderH, packColor(Theme::kYellow, 0.92f));
    font::pushText(textOut, box.x + kPadX, box.y + 12.0f,
                   flagYellow ? "CAUTION" : "LAKE HILL 400", hudtext::kCaption,
                   packColor(Theme::kBlack));

    for (size_t r = 0; r < rows.size(); ++r) {
        const LeaderboardRow& lr = rows[r];
        const float rowY = box.y + kHeaderH + (float)r * kRowH;
        const float baseline = rowY + 12.0f;

        if (lr.dividerBefore) {
            // index.html:3951-3954's thin divider above the pinned row.
            pushLineSegment(uiOut, box.x, rowY, box.x + box.w, rowY, 1.0f, packColor(Theme::kSteel));
        }

        // The player's row was a colour-attribute inversion before; now it is
        // a real highlight behind the whole row, which is what makes it findable
        // at a glance in a list of six.
        if (lr.isPlayerRow) {
            pushQuad(uiOut, box.x, rowY, box.w, kRowH, packColor(Theme::kYellow, 0.85f));
        }
        const uint32_t fg = lr.isPlayerRow ? packColor(Theme::kBlack) : packColor(Theme::kWhite);

        // Right-aligned rank: a proportional font makes "1" and "18" different
        // widths, so left-aligning them would leave the numbers ragged.
        hudtext::drawRight(textOut, box.x + kPadX + kRankW, baseline, std::to_string(lr.rank),
                           hudtext::kBody, fg);

        const float chipX = box.x + kPadX + kRankW + 8.0f;
        pushQuad(uiOut, chipX, rowY + 3.0f, kChipW, kChipH,
                 packColor((float)lr.col[0], (float)lr.col[1], (float)lr.col[2]));

        const float numX = chipX + kChipW + 6.0f;
        hudtext::draw(textOut, numX, baseline, "#" + std::to_string(lr.carNum), hudtext::kBody, fg);
        // The tag (name or gap, alternating -- see buildLeaderboardRows) is
        // right-aligned to the panel edge, so the gap column lines up down the
        // list the way a broadcast timing tower does.
        hudtext::drawRight(textOut, box.x + box.w - kPadX, baseline, lr.tag, hudtext::kBody, fg);
    }
}
