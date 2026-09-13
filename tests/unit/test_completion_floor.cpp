// ============================================================================
// tests/unit/test_completion_floor.cpp
// The Sweep rows' numbers, over the REAL catalogue with synthetic tiers.
//
// These replaced a single row that reported the LOWEST tier reached anywhere,
// and the reason is the first case below: a floor cannot say how far along you
// are. It reads zero until the very last row moves and then jumps to Bronze,
// which is no use to the one person who would look at it.
// ============================================================================
#include "doctest.h"

#include "../../mxbmrp3/core/completion_floor.h"

#include <vector>

using namespace Achievements;

namespace {

std::vector<int> allRows() {
    std::vector<int> rows;
    rows.reserve(COUNT);
    for (int i = 0; i < COUNT; ++i) rows.push_back(i);
    return rows;
}

// Every listed, non-Sweep row at `tier`; everything else untouched.
std::vector<int> tiersAll(int tier) {
    std::vector<int> t(COUNT, 0);
    for (int i = 0; i < COUNT; ++i) {
        if (!countsTowardCompletion(kCatalogue[i].group)) continue;
        t[static_cast<size_t>(i)] = tier;
    }
    return t;
}

int pct(const std::vector<int>& rows, const std::vector<int>& tiers, int metal) {
    return completionPercentAt(rows.data(), static_cast<int>(rows.size()), metal,
                               [&](int idx) { return tiers[static_cast<size_t>(idx)]; });
}

}  // namespace

TEST_CASE("sweep: the number MOVES - a floor's whole failing") {
    const std::vector<int> rows = allRows();
    std::vector<int> tiers(COUNT, 0);
    CHECK(pct(rows, tiers, 1) == 0);

    // Take listed rows to Bronze one at a time. The old floor would have read
    // zero for every one of these until the last; a share climbs.
    int listed = 0;
    for (int i = 0; i < COUNT; ++i) {
        if (countsTowardCompletion(kCatalogue[i].group)) ++listed;
    }
    REQUIRE(listed > 10);
    int moved = 0;
    for (int i = 0; i < COUNT && moved < listed / 2; ++i) {
        if (!countsTowardCompletion(kCatalogue[i].group)) continue;
        tiers[static_cast<size_t>(i)] = 1;
        ++moved;
    }
    const int half = pct(rows, tiers, 1);
    CHECK(half > 40);
    CHECK(half < 60);
}

TEST_CASE("sweep: each metal is its own number") {
    const std::vector<int> rows = allRows();
    // Everything listed at Silver: Bronze and Silver are complete, Gold and
    // Platinum are not - except that rows which RAN OUT of tiers count as full
    // at every metal, so the top two sit above zero rather than at it.
    const std::vector<int> tiers = tiersAll(2);
    CHECK(pct(rows, tiers, 1) == 100);
    CHECK(pct(rows, tiers, 2) == 100);
    CHECK(pct(rows, tiers, 3) < 100);
    CHECK(pct(rows, tiers, TIER_COUNT) < 100);
    CHECK(pct(rows, tiers, 3) == pct(rows, tiers, TIER_COUNT));   // the one-shots, both times
}

TEST_CASE("sweep: a finished one-shot is full at every metal") {
    // A one-shot has no Silver to withhold. Counting it short would peg Silver,
    // Gold and Platinum below 100% for every player, forever.
    const std::vector<int> rows = allRows();
    std::vector<int> tiers(COUNT, 0);
    int oneShots = 0;
    for (int i = 0; i < COUNT; ++i) {
        if (!countsTowardCompletion(kCatalogue[i].group)) continue;
        tiers[static_cast<size_t>(i)] = kCatalogue[i].tierCount;      // each row's own top
        if (kCatalogue[i].tierCount == 1) ++oneShots;
    }
    REQUIRE(oneShots > 0);
    for (int metal = 1; metal <= TIER_COUNT; ++metal) CHECK(pct(rows, tiers, metal) == 100);
}

TEST_CASE("sweep: the rows do not count themselves, nor the hidden ones") {
    // Self-counting would mean Bronze Sweep could not reach 100% until Bronze
    // Sweep was at Bronze. Hidden rows are excluded because they are not on the
    // board a player can see - one un-earnable secret would peg all four at 99.
    const std::vector<int> rows = allRows();
    std::vector<int> tiers = tiersAll(TIER_COUNT);
    int sweeps = 0, hiddenRows = 0;
    for (int i = 0; i < COUNT; ++i) {
        if (isCompletionRow(kCatalogue[i])) ++sweeps;
        else if (kCatalogue[i].hidden) ++hiddenRows;
    }
    REQUIRE(sweeps == 4);
    REQUIRE(hiddenRows > 0);
    // Sweeps and hidden rows all at zero, everything listed at its top: 100%.
    CHECK(pct(rows, tiers, 1) == 100);
    CHECK(pct(rows, tiers, TIER_COUNT) == 100);
}

TEST_CASE("sweep: no listed rows at all is zero, not complete") {
    // The vacuous case an unguarded ratio gets wrong by dividing by zero or by
    // reporting success for an empty set.
    std::vector<int> hiddenOnly;
    for (int i = 0; i < COUNT; ++i) {
        if (kCatalogue[i].hidden) hiddenOnly.push_back(i);
    }
    REQUIRE(!hiddenOnly.empty());
    std::vector<int> tiers(COUNT, TIER_COUNT);
    CHECK(pct(hiddenOnly, tiers, 1) == 0);
}
