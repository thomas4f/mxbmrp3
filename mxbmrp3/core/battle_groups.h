// ============================================================================
// core/battle_groups.h
// Battle-group partitioning — the pure grouping core.
//
// WHAT THIS IS. Given the racing field as (position, raceNum, official gap,
// laps-down) tuples, it partitions position-adjacent riders into "battles":
// greedy chains where each next rider is on the same lap and within the gap
// threshold of the rider ahead. Feeds the auto-director's battle scoring and
// the web overlay's battle panel (via PluginData::getBattleGroups, which owns
// the eligibility filtering — racing/on-track/not-finished/opening-lap).
//
// WHY IT LIVES HERE AND NOT IN PluginData. Four numbers per rider in, groups
// out — the same extraction bar as blue_flag_detect.h. Unit-tested by
// tests/unit/test_battle_groups.cpp with a plain g++ instead of only through
// the DLL under Wine.
//
// GAP SEMANTICS (load-bearing, don't "fix"):
//   - Deltas are taken between position-ADJACENT riders, so a battle chains
//     A-B-C even when A..C exceeds the threshold (that's one train, not two).
//   - A delta must be STRICTLY POSITIVE to chain. Equal official gaps mean
//     "no split has separated them yet" — chaining them would fuse the whole
//     pre-first-split field (everyone at gap 0) into one giant "battle".
//   - Riders on different laps never chain: nose-to-tail across a lapping
//     boundary is traffic, not a battle.
// ============================================================================
#pragma once

#include <algorithm>
#include <cstddef>
#include <vector>

namespace BattleGroups {

    struct Rider {
        int pos = 0;      // official position (1-based)
        int raceNum = 0;
        int gap = 0;      // official split gap to the leader, ms
        int gapLaps = 0;  // laps behind the leader
    };

    // Partitions `riders` into battle groups of raceNums, ordered by position.
    // Sorts `riders` by position in place (the caller's scratch buffer).
    // maxLeaderPos > 0 keeps only groups whose leading rider is at or above
    // that position (the director's "battles that matter" knob); <= 0 keeps all.
    inline std::vector<std::vector<int>> group(std::vector<Rider>& riders,
                                               int gapThresholdMs,
                                               int maxLeaderPos) {
        std::sort(riders.begin(), riders.end(),
                  [](const Rider& a, const Rider& b) { return a.pos < b.pos; });

        std::vector<std::vector<int>> groups;
        size_t i = 0;
        while (i < riders.size()) {
            size_t j = i;
            // Greedily chain adjacent same-lap riders within the gap threshold.
            while (j + 1 < riders.size()
                   && riders[j].gapLaps == riders[j + 1].gapLaps
                   && (riders[j + 1].gap - riders[j].gap) > 0
                   && (riders[j + 1].gap - riders[j].gap) <= gapThresholdMs) {
                ++j;
            }
            if (j > i) {
                if (maxLeaderPos <= 0 || riders[i].pos <= maxLeaderPos) {
                    std::vector<int> grp;
                    grp.reserve(j - i + 1);
                    for (size_t k = i; k <= j; ++k) grp.push_back(riders[k].raceNum);
                    groups.push_back(std::move(grp));
                }
                i = j + 1;
            } else {
                i = i + 1;
            }
        }
        return groups;
    }

}  // namespace BattleGroups
