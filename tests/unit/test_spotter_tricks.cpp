// ============================================================================
// tests/unit/test_spotter_tricks.cpp
// Pins the two opt-in spotter categories' pure halves:
//  - core/spotter_tricks.h: which tricks are ever said (never plain Air or
//    Drift - on a motocross track those are every jump and every corner), how
//    a trick is said (no side, multiplier as a word), and that every trick the
//    detector can produce has an answer;
//  - the achievement_<id> family in core/spotter_cue_pack.h: exactly one cue
//    per catalogue row, all in the Achievements category, each a real key, and
//    none that the variant-suffix rule would misread as an alternate.
// The end-to-end half (FmxManager and AchievementManager reaching the cue log)
// is tests/integration/tests/spotter_tricks_test.cpp.
// ============================================================================
#include "doctest.h"

#include <set>
#include <string>

#include "core/achievements.h"
#include "core/spotter_cue_pack.h"
#include "core/spotter_tricks.h"

using Fmx::TrickType;

TEST_CASE("spotter tricks: plain Air, Drift and no trick are never called") {
    CHECK_FALSE(SpotterTricks::isCallable(TrickType::NONE));
    CHECK_FALSE(SpotterTricks::isCallable(TrickType::AIR));
    CHECK_FALSE(SpotterTricks::isCallable(TrickType::DRIFT_LEFT));
    CHECK_FALSE(SpotterTricks::isCallable(TrickType::DRIFT_RIGHT));
    CHECK(SpotterTricks::spokenTrick(TrickType::AIR, 2).empty());
}

TEST_CASE("spotter tricks: every other trick has a spoken name") {
    // A new TrickType would otherwise be silent with nothing to say so.
    for (int i = 1; i < static_cast<int>(TrickType::COUNT); ++i) {
        const auto t = static_cast<TrickType>(i);
        if (t == TrickType::AIR || t == TrickType::DRIFT_LEFT || t == TrickType::DRIFT_RIGHT) continue;
        CAPTURE(i);
        CHECK(SpotterTricks::isCallable(t));
    }
}

TEST_CASE("spotter tricks: the side is dropped and the multiplier is a word") {
    CHECK(SpotterTricks::spokenTrick(TrickType::BARREL_ROLL_LEFT, 1) == "barrel roll");
    CHECK(SpotterTricks::spokenTrick(TrickType::BARREL_ROLL_RIGHT, 1) == "barrel roll");
    CHECK(SpotterTricks::spokenTrick(TrickType::BACKFLIP, 1) == "backflip");
    CHECK(SpotterTricks::spokenTrick(TrickType::BACKFLIP, 2) == "double backflip");
    CHECK(SpotterTricks::spokenTrick(TrickType::FRONTFLIP, 3) == "triple frontflip");
    CHECK(SpotterTricks::spokenTrick(TrickType::SPIN_LEFT, 6) == "quad spin");
    CHECK(SpotterTricks::spokenTrick(TrickType::WHEELIE, 0) == "wheelie");
}

TEST_CASE("spotter tricks: the level is said the way the HUD shows it") {
    // Level 1 is Big for every trick, level 2 the trick's own word.
    CHECK(SpotterTricks::spokenTrick(TrickType::WHIP_LEFT, 1, 1) == "big whip");
    CHECK(SpotterTricks::spokenTrick(TrickType::WHIP_RIGHT, 1, 2) == "flat whip");
    CHECK(SpotterTricks::spokenTrick(TrickType::SCRUB_LEFT, 1, 2) == "deep scrub");
    CHECK(SpotterTricks::spokenTrick(TrickType::OPPO_LEFT, 1, 2) == "huge oppo");
    CHECK(SpotterTricks::spokenTrick(TrickType::TURN_DOWN_RIGHT, 1, 2) == "huge turn down");
    CHECK(SpotterTricks::spokenTrick(TrickType::WHEELIE, 0, 2) == "endless wheelie");
    CHECK(SpotterTricks::spokenTrick(TrickType::STOPPIE, 0, 1) == "big stoppie");
    CHECK(SpotterTricks::spokenTrick(TrickType::WHIP_LEFT, 1, 0) == "whip");
    // A trick without levels ignores one, and keeps its multiplier.
    CHECK(SpotterTricks::spokenTrick(TrickType::BACKFLIP, 2, 2) == "double backflip");
    // Every level word any trick can show has a spoken word: a new top word
    // in fmx_levels.h would otherwise be silently dropped from the call.
    for (int i = 1; i < static_cast<int>(TrickType::COUNT); ++i) {
        const auto t = static_cast<TrickType>(i);
        for (int level = 1; level <= Fmx::TRICK_LEVEL_COUNT; ++level) {
            CAPTURE(i);
            CAPTURE(level);
            CHECK((Fmx::getTrickLevelWord(t, level) == nullptr) ==
                  (SpotterTricks::spokenLevel(t, level) == nullptr));
        }
    }
}

TEST_CASE("spotter tricks: the moment-of-truth cues expire rather than play late") {
    CHECK(SpotterCuePack::isPerishableCue("trick_started"));
    CHECK(SpotterCuePack::isPerishableCue("trick_landed"));
    // A boo after the fact is still a boo; a combo still happened.
    CHECK_FALSE(SpotterCuePack::isPerishableCue("trick_crashed"));
    CHECK_FALSE(SpotterCuePack::isPerishableCue("trick_combo"));
}

TEST_CASE("spotter achievements: one cue per catalogue row, all real keys") {
    std::set<std::string> family;
    for (const SpotterCuePack::CueKeyInfo& info : SpotterCuePack::allCueKeys()) {
        if (info.category == SpotterPhrase::Category::Achievements) family.insert(info.key);
    }
    int rows = 0;
    for (const Achievements::Entry& e : Achievements::kCatalogue) {
        const std::string key = std::string(SpotterCuePack::kAchievementCuePrefix) + e.id;
        CAPTURE(key);
        CHECK(family.count(key) == 1);
        CHECK(SpotterCuePack::isCueKey(key));
        // stripVariantSuffix reads a trailing _2.._9 as an alternate, so an id
        // shaped like that could never be given its own line.
        CHECK(SpotterCuePack::stripVariantSuffix(key) == key);
        ++rows;
    }
    CHECK(rows >= 100);   // the catalogue, not an empty walk
    CHECK(family.size() == static_cast<size_t>(rows));
    CHECK_FALSE(SpotterCuePack::isCueKey("achievement_not_a_row"));
}
