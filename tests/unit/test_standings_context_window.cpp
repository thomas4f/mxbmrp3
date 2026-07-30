// ============================================================================
// tests/unit/test_standings_context_window.cpp
// Unit tests for hud/standings_context_window.h — the standings pagination.
//
// This arithmetic used to live inline in StandingsHud::rebuildRenderData(), so
// the only way to exercise it was the DLL under Wine. The cases that matter are
// the two clamps and their compensations (see the header): a rider just below
// the pinned top block, and a rider at the very back of the field. Both look
// fine in mid-pack testing and drop rows at the edges.
//
// THE STANDING CONTRACT the table depends on: whenever the field is big enough
// to fill the table, the window must hand back exactly rowCount rows. Several
// cases below assert that explicitly, and `fillsTheTable` checks it over a sweep
// so a future edit can't satisfy the handful of hand-written cases and still
// render short somewhere in between.
// ============================================================================
#include "doctest.h"

#include "hud/standings_context_window.h"
#include <vector>

using namespace StandingsWindow;

namespace {

// Flatten a window into the list of classification indices it would draw, in
// draw order — which is what the caller actually consumes.
std::vector<int> rows(const Window& w) {
    std::vector<int> out;
    for (int i = w.top.startIndex; i <= w.top.endIndex; ++i) out.push_back(i);
    for (int i = w.context.startIndex; i <= w.context.endIndex; ++i) out.push_back(i);
    return out;
}

bool contains(const std::vector<int>& v, int x) {
    for (int e : v) if (e == x) return true;
    return false;
}

}  // namespace

TEST_CASE("no context: shows the leaders from the front") {
    // Rider absent from the classification (-1), context switched off, or rider
    // already inside the pinned block — all three collapse to the same window.
    for (int riderIndex : {-1, 0, 1, 2}) {
        const bool showContext = (riderIndex >= 0);
        Window w = computeWindow(22, riderIndex, 3, 10, showContext);
        CHECK(w.context.empty());
        CHECK(rows(w) == std::vector<int>{0, 1, 2, 3, 4, 5, 6, 7, 8, 9});
    }
}

TEST_CASE("no context: a short field never runs past its end") {
    Window w = computeWindow(4, -1, 3, 10, false);
    CHECK(w.totalRows() == 4);
    CHECK(rows(w) == std::vector<int>{0, 1, 2, 3});
}

TEST_CASE("no context: an empty field draws nothing") {
    Window w = computeWindow(0, -1, 3, 10, false);
    CHECK(w.totalRows() == 0);
    CHECK(w.top.empty());
    CHECK(w.context.empty());
}

TEST_CASE("mid-pack rider is centred below the pinned top block") {
    // 22 riders, top 3 pinned, 10 rows: 7 rows left, so 3 above the rider, the
    // rider, and 3 below.
    Window w = computeWindow(22, 11, 3, 10, true);
    CHECK(rows(w) == std::vector<int>{0, 1, 2, 8, 9, 10, 11, 12, 13, 14});
    CHECK(w.totalRows() == 10);
    CHECK(contains(rows(w), 11));
}

TEST_CASE("rider just below the top block: rows lost above move below") {
    // Rider in P4 (index 3) wants 3 rows above it, but indices 0..2 are already
    // drawn by the pinned block. Those 3 rows must reappear below the rider, not
    // vanish — the table still owes the user 10 rows.
    Window w = computeWindow(22, 3, 3, 10, true);
    CHECK(w.totalRows() == 10);
    CHECK(rows(w) == std::vector<int>{0, 1, 2, 3, 4, 5, 6, 7, 8, 9});
    CHECK(contains(rows(w), 3));
}

TEST_CASE("rider at the back of the field: rows lost below move above") {
    // The mirror case. Last rider (index 21) wants 3 rows below it; there are
    // none, so the window slides up to keep 10 rows on screen.
    Window w = computeWindow(22, 21, 3, 10, true);
    CHECK(w.totalRows() == 10);
    CHECK(rows(w) == std::vector<int>{0, 1, 2, 15, 16, 17, 18, 19, 20, 21});
    CHECK(contains(rows(w), 21));
}

TEST_CASE("second-to-last rider still gets a full table") {
    Window w = computeWindow(22, 20, 3, 10, true);
    CHECK(w.totalRows() == 10);
    CHECK(contains(rows(w), 20));
    CHECK(rows(w).back() == 21);
}

TEST_CASE("both clamps at once: field barely larger than the pinned block") {
    // 5 riders, top 3 pinned, 10 rows. The rider's window can only be 3..4, and
    // neither compensation can invent rows that do not exist.
    Window w = computeWindow(5, 4, 3, 10, true);
    CHECK(rows(w) == std::vector<int>{0, 1, 2, 3, 4});
    CHECK(w.totalRows() == 5);
}

TEST_CASE("no row is drawn twice") {
    // The pinned block and the context window must not overlap: a duplicated
    // index renders the same rider on two rows.
    for (int field = 4; field <= 40; ++field) {
        for (int rider = 3; rider < field; ++rider) {
            auto r = rows(computeWindow(field, rider, 3, 10, true));
            std::vector<int> seen;
            for (int idx : r) {
                CHECK_FALSE(contains(seen, idx));
                seen.push_back(idx);
            }
        }
    }
}

TEST_CASE("fillsTheTable: a big enough field always yields exactly rowCount rows") {
    // The contract that the two compensations exist to uphold, swept rather than
    // sampled. Only fields at least as large as the table qualify — a shorter
    // field legitimately draws fewer rows.
    for (int rowCount = 4; rowCount <= 16; ++rowCount) {
        for (int topCount = 0; topCount <= 3 && topCount < rowCount; ++topCount) {
            for (int field = rowCount; field <= rowCount + 20; ++field) {
                for (int rider = topCount; rider < field; ++rider) {
                    Window w = computeWindow(field, rider, topCount, rowCount, true);
                    INFO("field=" << field << " rider=" << rider
                         << " top=" << topCount << " rows=" << rowCount);
                    CHECK(w.totalRows() == rowCount);
                    CHECK(contains(rows(w), rider));  // the rider is always on screen
                }
            }
        }
    }
}

TEST_CASE("window never runs off either end of the field") {
    for (int field = 1; field <= 30; ++field) {
        for (int rider = 0; rider < field; ++rider) {
            auto r = rows(computeWindow(field, rider, 3, 10, true));
            for (int idx : r) {
                CHECK(idx >= 0);
                CHECK(idx < field);
            }
        }
    }
}

TEST_CASE("topCount 0 degenerates to a plain window around the rider") {
    Window w = computeWindow(22, 11, 0, 10, true);
    CHECK(w.top.empty());
    CHECK(w.totalRows() == 10);
    CHECK(contains(rows(w), 11));
}

TEST_CASE("topCount >= rowCount: the pinned block wins and nothing overflows") {
    // A misconfiguration rather than a normal state, but it must not produce a
    // negative-length range or an index outside the field.
    Window w = computeWindow(22, 11, 10, 10, true);
    auto r = rows(w);
    CHECK(w.totalRows() >= 10);
    for (int idx : r) {
        CHECK(idx >= 0);
        CHECK(idx < 22);
    }
}
