// ============================================================================
// tests/unit/test_hud_defaults.cpp
// The stored value of a HUD's reference key (hud/hud_defaults.h). Pins:
//   - "Default" is stored as 3 and always was: a saved Default keeps its
//     meaning after Server best was added. (Stored as one past the count of
//     references, it would have been read back as Server best.)
//   - the first three references keep their stored values, Server best is
//     stored past Default, and every reference round-trips
//   - a value that is no reference is rejected and changes nothing
// ============================================================================
#include "doctest.h"
#include "hud/hud_defaults.h"

using Ref = PbGapTracker::Ref;

TEST_CASE("a saved Default keeps its meaning; Server best is stored past it") {
    CHECK(HudDefaults::REFERENCE_FOLLOW == 3);
    CHECK(HudDefaults::storeReference(true, Ref::SERVER_BEST) == 3);
    CHECK(HudDefaults::storeReference(false, Ref::SESSION_PB) == 0);
    CHECK(HudDefaults::storeReference(false, Ref::ALLTIME_PB) == 1);
    CHECK(HudDefaults::storeReference(false, Ref::LAST_LAP) == 2);
    CHECK(HudDefaults::storeReference(false, Ref::SERVER_BEST) == 4);

    bool follow = false;
    Ref ref = Ref::LAST_LAP;
    REQUIRE(HudDefaults::loadReference(3, &follow, &ref));
    CHECK(follow);
    CHECK(ref == Ref::LAST_LAP);   // left alone: the HUD keeps its own for when it stops following
}

TEST_CASE("every reference round-trips; anything else is rejected") {
    for (int i = 0; i < PbGapTracker::REF_COUNT; ++i) {
        const Ref want = static_cast<Ref>(i);
        bool follow = true;
        Ref got = Ref::SESSION_PB;
        REQUIRE(HudDefaults::loadReference(HudDefaults::storeReference(false, want), &follow, &got));
        CHECK_FALSE(follow);
        CHECK(got == want);
    }
    for (int bad : { -1, PbGapTracker::REF_COUNT + 1, 99 }) {
        bool follow = true;
        Ref ref = Ref::ALLTIME_PB;
        CHECK_FALSE(HudDefaults::loadReference(bad, &follow, &ref));
        CHECK(follow);
        CHECK(ref == Ref::ALLTIME_PB);
    }
}
