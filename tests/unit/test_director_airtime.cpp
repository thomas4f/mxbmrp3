// ============================================================================
// tests/unit/test_director_airtime.cpp
// Unit tests for the auto-director's two header-only airtime helpers, no game
// engine. See tests/unit/README.md.
//
//   pickNextAirtimeNum   — the lull round-robin that dips the camera to the
//       "next" rider so a quiet race spreads airtime across the field instead of
//       gluing to the leader. The key property is that the cursor keys on RACE
//       NUMBER (stable rider identity), NOT grid position, so a mid-race order
//       shuffle never re-seeds the walk (re-showing or skipping riders).
//   pickBaselineSubject  — the dead-air floor: who the camera falls back to with
//       no story running. That is the leader/pace-setter normally, but the
//       broadcaster's own rider once forced rotation is off ("Max shot = Off"),
//       which is what makes the director return home after a story instead of
//       drifting up the order.
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

// --- pickBaselineSubject: the dead-air floor -------------------------------
// Only the fields the helper reads matter (raceNum + finished); the rest are
// filled with plausible values so the POD is never half-initialized.
static director_detail::Rider rider(int raceNum, bool finished = false) {
    return director_detail::Rider{ /*position=*/1, raceNum, /*gapToLeaderMs=*/0,
                                   /*gapLaps=*/0, /*numLaps=*/3, /*bestLapMs=*/90000,
                                   finished };
}

TEST_CASE("pickBaselineSubject: forced rotation on keeps the leader/pace-setter floor") {
    const std::vector<director_detail::Rider> field = { rider(10), rider(22), rider(7) };
    // With the max shot running, the director paces the whole field — the home rider is
    // just history and must NOT steal the floor, even when they're right there on track.
    CHECK(DirectorManager::pickBaselineSubject(field, /*forcedRotation=*/true,
                                               /*homeSubject=*/22, /*fallback=*/10) == 10);
}

TEST_CASE("pickBaselineSubject: forced rotation off hands the floor to the home rider") {
    const std::vector<director_detail::Rider> field = { rider(10), rider(22), rider(7) };
    CHECK(DirectorManager::pickBaselineSubject(field, /*forcedRotation=*/false,
                                               /*homeSubject=*/22, /*fallback=*/10) == 22);
    // The leader can be the home rider too — nothing special about that case.
    CHECK(DirectorManager::pickBaselineSubject(field, false, /*home=*/10, /*fallback=*/10) == 10);
}

TEST_CASE("pickBaselineSubject: falls back rather than ever showing dead air") {
    const std::vector<director_detail::Rider> field = { rider(10), rider(22) };
    // Home rider never established (director enabled with nobody spectated).
    CHECK(DirectorManager::pickBaselineSubject(field, false, /*home=*/-1, /*fallback=*/10) == 10);
    // Home rider off the track: retired, in the pits, or otherwise not in the pass's
    // rider set (collectRiders drops all of those) -> hold the front instead.
    CHECK(DirectorManager::pickBaselineSubject(field, false, /*home=*/99, /*fallback=*/10) == 10);
    // Home rider present but done for the day — sitting on a slow-down lap isn't a shot.
    const std::vector<director_detail::Rider> done = { rider(10), rider(22, /*finished=*/true) };
    CHECK(DirectorManager::pickBaselineSubject(done, false, /*home=*/22, /*fallback=*/10) == 10);
    // Empty field (every rider pitted/retired): the caller's fallback stands.
    CHECK(DirectorManager::pickBaselineSubject({}, false, /*home=*/22, /*fallback=*/10) == 10);
}
