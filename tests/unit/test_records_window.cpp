// ============================================================================
// tests/unit/test_records_window.cpp
// Unit tests for hud/records_window.h — the records table's context window.
//
// This arithmetic used to live inline in RecordsHud::rebuildRenderData(), where
// exercising it meant a DLL under Wine AND a live records fetch — which is what
// kept the interesting cases untested. They are all edge cases: the player just
// below the pinned block, the player on the last fetched record, the player
// slower than every record fetched, and fewer records than there are rows. A
// mid-table player (where a developer's own PB usually sits) exercises none of
// the clamps.
//
// THE STANDING CONTRACT the table depends on: whenever there are enough records
// to fill it, the pinned block plus the window plus the player's own row must
// come to exactly rowCount. `fillsTheTable` sweeps that so a future edit cannot
// satisfy the hand-written cases and still render short at some size in between.
// ============================================================================
#include "doctest.h"

#include "hud/records_window.h"

using namespace RecordsWindow;

namespace {

// The table's real shape: 3 pinned positions, 8 data rows.
constexpr int kTop = 3;
constexpr int kRows = 8;

}  // namespace

TEST_CASE("records window: a mid-table player is centred") {
    // 50 records, player at index 25, 8 rows: 3 pinned + 1 player + 4 context.
    Range r = computeContext(50, 25, kRows, kTop);
    CHECK(r.count() == 4);
    CHECK(r.start == 23);   // playerPosition - availableRows/2
    CHECK(r.end == 26);
    // The player's own row is inserted at index 25 by the caller, inside this slice.
    CHECK(r.start <= 25);
    CHECK(25 <= r.end);
}

// Clamp 1: the player is just below the pinned block, so the rows "above" them
// are already taken. Those rows must move DOWN or the table renders short.
TEST_CASE("records window: a player just below the pinned block pushes rows downward") {
    Range r = computeContext(50, kTop, kRows, kTop);  // player at index 3
    CHECK(r.start == kTop);        // cannot go above the pinned block
    CHECK(r.count() == 4);         // still gets all four context rows
    CHECK(r.end == kTop + 3);      // they moved below the player
}

TEST_CASE("records window: a player one row below the block still fills") {
    Range r = computeContext(50, kTop + 1, kRows, kTop);
    CHECK(r.start == kTop);
    CHECK(r.count() == 4);
}

// Clamp 2: the player is at the end of the fetched records, so the rows "below"
// them do not exist. Those rows must move UP.
TEST_CASE("records window: a player on the last record pushes rows upward") {
    Range r = computeContext(50, 49, kRows, kTop);
    CHECK(r.end == 49);
    CHECK(r.count() == 4);
    CHECK(r.start == 46);
}

// The branch that has no counterpart in the standings window: the player's PB is
// slower than everything fetched, so their row goes after the last record.
TEST_CASE("records window: a player slower than every fetched record shows the tail") {
    Range r = computeContext(50, 50, kRows, kTop);
    CHECK(r.end == 49);
    CHECK(r.count() == 4);
    CHECK(r.start == 46);
}

TEST_CASE("records window: a player far past the end still shows the tail") {
    // playerPosition can exceed totalRecords when the fetch returned a short list.
    Range r = computeContext(50, 200, kRows, kTop);
    CHECK(r.end == 49);
    CHECK(r.start == 46);
}

TEST_CASE("records window: fewer records than rows never runs past the end") {
    SUBCASE("player inside a short list") {
        Range r = computeContext(5, 4, kRows, kTop);
        CHECK(r.end <= 4);
        CHECK(r.start >= kTop);
    }
    SUBCASE("player past the end of a short list") {
        Range r = computeContext(5, 5, kRows, kTop);
        CHECK(r.end == 4);
        CHECK(r.start == kTop);   // clamped: cannot reach above the pinned block
    }
    SUBCASE("records exactly equal to the pinned block") {
        Range r = computeContext(kTop, kTop, kRows, kTop);
        CHECK(r.empty());          // nothing left below the block to show
    }
}

// The window must never overlap the pinned block (those rows are drawn already)
// and must never point past the fetched records.
TEST_CASE("records window: fillsTheTable — bounds hold and the table fills when it can") {
    for (int totalRecords = 0; totalRecords <= 60; ++totalRecords) {
        for (int rowCount = kTop + 2; rowCount <= 14; ++rowCount) {
            for (int playerPos = kTop; playerPos <= totalRecords + 2; ++playerPos) {
                Range r = computeContext(totalRecords, playerPos, rowCount, kTop);
                CAPTURE(totalRecords);
                CAPTURE(rowCount);
                CAPTURE(playerPos);

                if (!r.empty()) {
                    CHECK(r.start >= kTop);            // never into the pinned block
                    CHECK(r.end <= totalRecords - 1);  // never past the records
                }

                const int availableRows = rowCount - kTop - 1;
                // When there are enough records below the pinned block to fill the
                // window, it must actually be full — that is the whole point of the
                // two clamp compensations.
                const int recordsBelowBlock = totalRecords - kTop;
                if (recordsBelowBlock >= availableRows && availableRows > 0) {
                    CHECK(r.count() == availableRows);
                }
            }
        }
    }
}
