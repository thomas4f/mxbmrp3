// ============================================================================
// hud/gap_chart_picks.h
// Who the Dash's Gap chart page draws: the race leader, always, the displayed
// rider, and the riders nearest them filling the lines left -- one ahead and
// one behind, then further out on whichever side still has riders. Four lines,
// so the leader no longer costs the rider ahead or behind their line:
//
//   P1 of 10   ->  P1 (you), P2, P3, P4
//   P2 of 10   ->  P1, P2 (you), P3, P4
//   P5 of 10   ->  P1, P4, P5 (you), P6
//   P10 of 10  ->  P1, P8, P9, P10 (you)
//
// The order is the classification order as PluginData holds it, which is a
// strict order (a tie on time is already broken there), so the picks are too.
// Spectating, the displayed rider is the one spectated. A rider not in the
// classification has no line, so nothing is picked and the page shows its
// placeholder.
//
// Pure (no plugin state): read every frame by DashHud::readGapChart to notice
// a change of who the lines belong to, and on a rebuild by
// SessionChartsHud::gapChartLines to fetch their series. Pinned by
// tests/unit/test_gap_chart_picks.cpp.
// ============================================================================
#pragma once

#include <cstddef>
#include <cstdint>

namespace GapChartPicks {

constexpr int MAX_LINES = 4;

// A line's relation to the displayed rider, for its colour. The leader is
// LEADER unless it is the displayed rider, who is always MINE.
enum class Role : uint8_t { LEADER, AHEAD, MINE, BEHIND };

struct Pick {
    int index = -1;            // into the classification order
    Role role = Role::MINE;
};

// Fill `out` with up to MAX_LINES picks from `order` (race numbers, leader
// first), in classification order, and return how many. 0 when `mine` is not
// in `order`.
inline int pick(const int* order, size_t n, int mine, Pick (&out)[MAX_LINES]) {
    size_t me = n;
    for (size_t i = 0; i < n; ++i) {
        if (order[i] == mine) { me = i; break; }
    }
    if (me == n) return 0;

    // Which classification indices are in, by distance from the rider: the
    // leader and the rider first, then one step out each way at a time, the
    // rider ahead before the rider behind at the same distance.
    size_t chosen[MAX_LINES] = {};
    int count = 0;
    auto add = [&](size_t i) {
        for (int k = 0; k < count; ++k) if (chosen[k] == i) return;
        chosen[count++] = i;
    };
    add(0);
    add(me);
    for (size_t d = 1; count < MAX_LINES && (d <= me || me + d < n); ++d) {
        if (d <= me) add(me - d);
        if (count < MAX_LINES && me + d < n) add(me + d);
    }

    // Classification order (a handful of entries: insertion sort).
    for (int a = 1; a < count; ++a) {
        const size_t v = chosen[a];
        int b = a - 1;
        while (b >= 0 && chosen[b] > v) { chosen[b + 1] = chosen[b]; --b; }
        chosen[b + 1] = v;
    }
    for (int k = 0; k < count; ++k) {
        const size_t i = chosen[k];
        out[k].index = static_cast<int>(i);
        out[k].role = i == me ? Role::MINE : i == 0 ? Role::LEADER : i < me ? Role::AHEAD : Role::BEHIND;
    }
    return count;
}

} // namespace GapChartPicks
