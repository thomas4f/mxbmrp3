// ============================================================================
// tests/unit/test_battle_groups.cpp
// The battle-group partitioning core (core/battle_groups.h) behind the
// auto-director's battle scoring and the overlay battle panel. Pins the gap
// semantics stated in the header:
//   - adjacency chaining: A-B-C is one battle when each consecutive delta is
//     within threshold, even though A..C exceeds it
//   - the strictly-positive delta rule: equal official gaps (the whole field
//     before the first split) must NOT fuse into one giant group
//   - lap boundaries never chain (lapping traffic is not a battle)
//   - maxLeaderPos drops whole groups by their leading rider's position
//   - solo riders form no group, and input order doesn't matter (the core
//     sorts by position itself)
// The eligibility filtering (racing/on-track/finished/opening-lap) stays in
// PluginData::getBattleGroups and is exercised by the director integration
// tests.
// ============================================================================
#include "doctest.h"

#include "core/battle_groups.h"

using BattleGroups::Rider;

namespace {
std::vector<std::vector<int>> run(std::vector<Rider> rs, int threshold, int maxLeaderPos = 0) {
    return BattleGroups::group(rs, threshold, maxLeaderPos);
}
}  // namespace

TEST_CASE("adjacent riders within threshold form a group") {
    auto groups = run({{1, 10, 0, 0}, {2, 20, 800, 0}, {3, 30, 5000, 0}}, 1500);
    REQUIRE(groups.size() == 1);
    CHECK(groups[0] == std::vector<int>{10, 20});
}

TEST_CASE("chains extend by adjacent deltas, not total spread") {
    // 0 -> 1400 -> 2800: each delta is 1400 (within 1500), total spread 2800 (beyond).
    auto groups = run({{1, 10, 0, 0}, {2, 20, 1400, 0}, {3, 30, 2800, 0}}, 1500);
    REQUIRE(groups.size() == 1);
    CHECK(groups[0] == std::vector<int>{10, 20, 30});
}

TEST_CASE("equal gaps do not chain (pre-first-split field stays ungrouped)") {
    auto groups = run({{1, 10, 0, 0}, {2, 20, 0, 0}, {3, 30, 0, 0}}, 1500);
    CHECK(groups.empty());
}

TEST_CASE("a lap boundary breaks the chain") {
    // P3 is nose-to-tail with P2 by gap but a lap down: traffic, not a battle.
    auto groups = run({{1, 10, 0, 0}, {2, 20, 900, 0}, {3, 30, 1000, 1}}, 1500);
    REQUIRE(groups.size() == 1);
    CHECK(groups[0] == std::vector<int>{10, 20});
}

TEST_CASE("same-lap riders among the lapped can still battle") {
    auto groups = run({{4, 40, 100, 1}, {5, 50, 600, 1}}, 1500);
    REQUIRE(groups.size() == 1);
    CHECK(groups[0] == std::vector<int>{40, 50});
}

TEST_CASE("two separate battles partition independently") {
    auto groups = run({{1, 10, 0, 0}, {2, 20, 500, 0},
                       {3, 30, 20000, 0}, {4, 40, 20700, 0}}, 1500);
    REQUIRE(groups.size() == 2);
    CHECK(groups[0] == std::vector<int>{10, 20});
    CHECK(groups[1] == std::vector<int>{30, 40});
}

TEST_CASE("maxLeaderPos keeps only groups led at or above the cutoff") {
    auto groups = run({{1, 10, 0, 0}, {2, 20, 500, 0},
                       {7, 70, 30000, 0}, {8, 80, 30600, 0}}, 1500, /*maxLeaderPos=*/5);
    REQUIRE(groups.size() == 1);
    CHECK(groups[0] == std::vector<int>{10, 20});
}

TEST_CASE("solo riders never form a group") {
    CHECK(run({{1, 10, 0, 0}}, 1500).empty());
    CHECK(run({}, 1500).empty());
}

TEST_CASE("input order does not matter — the core sorts by position") {
    auto groups = run({{3, 30, 2800, 0}, {1, 10, 0, 0}, {2, 20, 1400, 0}}, 1500);
    REQUIRE(groups.size() == 1);
    CHECK(groups[0] == std::vector<int>{10, 20, 30});
}
