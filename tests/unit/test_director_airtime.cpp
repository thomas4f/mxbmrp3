// ============================================================================
// tests/unit/test_director_airtime.cpp
// Unit tests for DirectorManager::pickNextAirtimeNum — the lull round-robin that
// dips the auto-director's camera to the "next" rider so a quiet race spreads
// airtime across the field instead of gluing to the leader. The key property is
// that the cursor keys on RACE NUMBER (stable rider identity), NOT grid position,
// so a mid-race order shuffle never re-seeds the walk (re-showing or skipping
// riders). Header-only helper, no game engine. See tests/unit/run_tests.sh.
// ============================================================================
// The doctest implementation + main() live in test_plugin_utils.cpp
// (DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN); this TU only registers more tests.
#include "doctest.h"

#include "core/director_manager.h"

#include <vector>

using Field = std::vector<int>;

// Convenience: run one dip and return the pick, advancing the cursor in place.
static int dip(const Field& nums, int cur, int baseline, int& cursor) {
    return DirectorManager::pickNextAirtimeNum(nums, cur, baseline, cursor);
}

TEST_CASE("pickNextAirtimeNum: cycles the whole field in race-number order and wraps") {
    // A field with non-contiguous, out-of-order race numbers. The current subject
    // is the leader (#7) and there is no distinct baseline (-1). Every OTHER rider
    // should get exactly one turn per cycle, ascending by race number, then wrap.
    const Field field = { 7, 3, 22, 9, 14 };
    int cursor = -1;  // nothing aired yet

    // First dip: smallest race number greater than -1, skipping the subject (#7).
    CHECK(dip(field, /*cur=*/7, /*baseline=*/-1, cursor) == 3);
    CHECK(dip(field, 7, -1, cursor) == 9);
    CHECK(dip(field, 7, -1, cursor) == 14);
    CHECK(dip(field, 7, -1, cursor) == 22);
    // Exhausted (nothing > 22 except the skipped subject) -> wrap to the smallest.
    CHECK(dip(field, 7, -1, cursor) == 3);
    CHECK(dip(field, 7, -1, cursor) == 9);
}

TEST_CASE("pickNextAirtimeNum: a mid-race order shuffle does NOT re-seed the walk") {
    // The whole point of keying on race number: the cursor must be immune to the
    // grid churning. We feed the SAME race numbers in a totally different order
    // (as if positions swapped every frame) and assert the sequence is identical
    // to the sorted-order run above — the walk depends only on the cursor value.
    const Field sorted   = { 3, 9, 14, 22, 7 };
    const Field shuffled = { 22, 7, 3, 14, 9 };  // same riders, churned order

    int ca = -1, cb = -1;
    for (int i = 0; i < 8; ++i) {
        const int a = dip(sorted,   /*cur=*/7, /*baseline=*/-1, ca);
        const int b = dip(shuffled, /*cur=*/7, /*baseline=*/-1, cb);
        CHECK(a == b);  // identical pick regardless of iteration order
    }
}

TEST_CASE("pickNextAirtimeNum: skips both the current subject and the baseline") {
    const Field field = { 1, 2, 3, 4, 5 };
    int cursor = -1;
    // Subject #2, baseline #4 (the rider we'd return to). Neither is ever picked.
    for (int i = 0; i < 12; ++i) {
        const int pick = dip(field, /*cur=*/2, /*baseline=*/4, cursor);
        CHECK(pick != 2);
        CHECK(pick != 4);
    }
    // And the picks it DOES make cover exactly {1, 3, 5}.
    int c2 = -1;
    CHECK(dip(field, 2, 4, c2) == 1);
    CHECK(dip(field, 2, 4, c2) == 3);
    CHECK(dip(field, 2, 4, c2) == 5);
    CHECK(dip(field, 2, 4, c2) == 1);  // wrap
}

TEST_CASE("pickNextAirtimeNum: a fully-excluded field yields -1 without moving the cursor") {
    int cursor = 5;
    // Only two riders, and both are excluded (one is the subject, one the baseline).
    CHECK(DirectorManager::pickNextAirtimeNum({ 4, 9 }, /*cur=*/4, /*baseline=*/9, cursor) == -1);
    CHECK(cursor == 5);  // unchanged on a -1 pick

    // An empty field is likewise a no-op.
    CHECK(DirectorManager::pickNextAirtimeNum({}, /*cur=*/-1, /*baseline=*/-1, cursor) == -1);
    CHECK(cursor == 5);
}

TEST_CASE("pickNextAirtimeNum: a single eligible rider is picked every time (no thrash source)") {
    // Solo-ish field: subject #4, only #9 is eligible. It's picked repeatedly — the
    // caller (director) is what avoids thrash by holding min-shot; the walk itself
    // just keeps returning the one available rider.
    int cursor = -1;
    CHECK(DirectorManager::pickNextAirtimeNum({ 4, 9 }, 4, -1, cursor) == 9);
    CHECK(DirectorManager::pickNextAirtimeNum({ 4, 9 }, 4, -1, cursor) == 9);  // wrap to the only one
}
