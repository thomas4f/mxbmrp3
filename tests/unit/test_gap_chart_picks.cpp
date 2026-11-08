// ============================================================================
// tests/unit/test_gap_chart_picks.cpp
// Who the Dash's Gap chart page draws (hud/gap_chart_picks.h): the leader on
// every line-up, the displayed rider, and the riders nearest them filling the
// four lines. Before, the page drew the rider ahead, the rider and the rider
// behind, so the leader showed only from P2, and P1 got two lines.
// ============================================================================
#include "doctest.h"
#include "hud/gap_chart_picks.h"

#include <vector>

namespace {
using GapChartPicks::Pick;
using GapChartPicks::Role;

struct Picked {
    std::vector<int> nums;
    std::vector<Role> roles;
};

Picked pickFrom(const std::vector<int>& order, int mine) {
    Pick out[GapChartPicks::MAX_LINES];
    const int n = GapChartPicks::pick(order.data(), order.size(), mine, out);
    Picked p;
    for (int k = 0; k < n; ++k) {
        p.nums.push_back(order[static_cast<size_t>(out[k].index)]);
        p.roles.push_back(out[k].role);
    }
    return p;
}

// Race numbers 101..110 in classification order 1..10.
const std::vector<int> kTen = { 101, 102, 103, 104, 105, 106, 107, 108, 109, 110 };
} // namespace

TEST_CASE("gap chart picks: the leader is on the chart wherever the rider runs") {
    for (size_t pos = 0; pos < kTen.size(); ++pos) {
        CAPTURE(pos);
        const Picked p = pickFrom(kTen, kTen[pos]);
        REQUIRE(p.nums.size() == 4);
        CHECK(p.nums[0] == 101);
        // Strictly in classification order, no rider twice.
        for (size_t k = 1; k < p.nums.size(); ++k) CHECK(p.nums[k] > p.nums[k - 1]);
    }
}

TEST_CASE("gap chart picks: mid-field is the leader, ahead, rider, behind") {
    const Picked p = pickFrom(kTen, 105);
    CHECK(p.nums == std::vector<int>{ 101, 104, 105, 106 });
    CHECK(p.roles == std::vector<Role>{ Role::LEADER, Role::AHEAD, Role::MINE, Role::BEHIND });
}

TEST_CASE("gap chart picks: leading, the next three riders fill the lines") {
    const Picked p = pickFrom(kTen, 101);
    CHECK(p.nums == std::vector<int>{ 101, 102, 103, 104 });
    CHECK(p.roles == std::vector<Role>{ Role::MINE, Role::BEHIND, Role::BEHIND, Role::BEHIND });
}

TEST_CASE("gap chart picks: in P2 the leader is the rider ahead, and two behind fill in") {
    const Picked p = pickFrom(kTen, 102);
    CHECK(p.nums == std::vector<int>{ 101, 102, 103, 104 });
    CHECK(p.roles == std::vector<Role>{ Role::LEADER, Role::MINE, Role::BEHIND, Role::BEHIND });
}

TEST_CASE("gap chart picks: last, the riders ahead fill in") {
    const Picked p = pickFrom(kTen, 110);
    CHECK(p.nums == std::vector<int>{ 101, 108, 109, 110 });
    CHECK(p.roles == std::vector<Role>{ Role::LEADER, Role::AHEAD, Role::AHEAD, Role::MINE });
}

TEST_CASE("gap chart picks: a small field draws everyone") {
    CHECK(pickFrom({ 7 }, 7).nums == std::vector<int>{ 7 });
    CHECK(pickFrom({ 7, 8 }, 8).nums == std::vector<int>{ 7, 8 });
    const Picked three = pickFrom({ 7, 8, 9 }, 9);
    CHECK(three.nums == std::vector<int>{ 7, 8, 9 });
    CHECK(three.roles == std::vector<Role>{ Role::LEADER, Role::AHEAD, Role::MINE });
}

TEST_CASE("gap chart picks: a rider not in the classification gets no lines") {
    CHECK(pickFrom(kTen, 999).nums.empty());
    CHECK(pickFrom({}, 101).nums.empty());
}
