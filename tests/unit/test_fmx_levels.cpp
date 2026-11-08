// ============================================================================
// tests/unit/test_fmx_levels.cpp
// Pins the trick LEVELS (core/fmx_levels.h): every threshold edge, every word,
// which tricks have levels at all, and the names the trick stack shows.
//
// Doctest impl lives in test_plugin_utils.cpp; this TU only adds cases.
// ============================================================================
#include "doctest.h"

#include "core/fmx_levels.h"
#include <algorithm>
#include <cmath>
#include <cstring>
#include <limits>

using namespace Fmx;

namespace {

// A trick with one measurement set; the rest stays default.
TrickInstance makeTrick(TrickType type) {
    TrickInstance t;
    t.type = type;
    return t;
}

const char* nameFor(const TrickInstance& t, char* buf, size_t size) {
    formatTrickName(t.type, t.multiplier, getTrickLevelWord(t), buf, size);
    return buf;
}

}  // namespace

TEST_CASE("fmx levels: whip reads peak yaw, either sign") {
    TrickInstance t = makeTrick(TrickType::WHIP_LEFT);
    t.peakYaw = -44.9f; CHECK(getTrickLevel(t) == 0);
    t.peakYaw = -45.0f; CHECK(getTrickLevel(t) == 1);
    t.peakYaw = 89.9f;  CHECK(getTrickLevel(t) == 1);
    t.peakYaw = 90.0f;  CHECK(getTrickLevel(t) == 2);
    t.peakYaw = 200.0f; CHECK(getTrickLevel(t) == 2);

    char buf[64];
    t.peakYaw = 30.0f; CHECK(std::strcmp(nameFor(t, buf, sizeof(buf)), "Whip L") == 0);
    t.peakYaw = 50.0f; CHECK(std::strcmp(nameFor(t, buf, sizeof(buf)), "Big Whip L") == 0);
    t.peakYaw = 95.0f; CHECK(std::strcmp(nameFor(t, buf, sizeof(buf)), "Flat Whip L") == 0);
    t.type = TrickType::WHIP_RIGHT;
    CHECK(std::strcmp(nameFor(t, buf, sizeof(buf)), "Flat Whip R") == 0);
}

TEST_CASE("fmx levels: scrub reads the world lean, lip lean plus roll added") {
    TrickInstance t = makeTrick(TrickType::SCRUB_RIGHT);
    t.peakRoll = 44.0f; CHECK(getTrickLevel(t) == 0);
    t.peakRoll = 45.0f; CHECK(getTrickLevel(t) == 1);
    t.peakRoll = -75.0f; CHECK(getTrickLevel(t) == 2);

    // peakRoll is roll ADDED since take-off: 40 at the lip and 40 more is 80.
    t.startRoll = 40.0f;
    t.peakRoll = 40.0f;
    CHECK(getTrickLevel(t) == 2);

    // 40 at the lip rolled back 60 is 20 of lean: the lip's 40 is the deepest,
    // which is plain - never Big off the 60 degrees of roll alone.
    t.peakRoll = -60.0f;
    CHECK(getTrickLevel(t) == 0);

    // Laid over at the lip and held there.
    t.startRoll = -80.0f;
    t.peakRoll = 5.0f;
    CHECK(getTrickLevel(t) == 2);

    char buf[64];
    CHECK(std::strcmp(nameFor(t, buf, sizeof(buf)), "Deep Scrub R") == 0);
}

TEST_CASE("fmx levels: oppo and turn down read peak yaw on the 90/150 ladder") {
    for (TrickType type : { TrickType::OPPO_LEFT, TrickType::OPPO_RIGHT,
                            TrickType::TURN_DOWN_LEFT, TrickType::TURN_DOWN_RIGHT }) {
        TrickInstance t = makeTrick(type);
        t.peakYaw = 89.0f;  CHECK(getTrickLevel(t) == 0);
        t.peakYaw = 90.0f;  CHECK(getTrickLevel(t) == 1);
        t.peakYaw = 150.0f; CHECK(getTrickLevel(t) == 2);
        CHECK(std::strcmp(getTrickLevelWord(t), "Huge") == 0);
    }
}

TEST_CASE("fmx levels: wheelie, endo and stoppie read the hold time") {
    TrickInstance w = makeTrick(TrickType::WHEELIE);
    w.duration = 4.9f;  CHECK(getTrickLevel(w) == 0);
    w.duration = 5.0f;  CHECK(getTrickLevel(w) == 1);
    w.duration = 20.0f; CHECK(getTrickLevel(w) == 2);
    CHECK(std::strcmp(getTrickLevelWord(w), "Endless") == 0);

    for (TrickType type : { TrickType::ENDO, TrickType::STOPPIE }) {
        TrickInstance t = makeTrick(type);
        t.duration = 0.9f; CHECK(getTrickLevel(t) == 0);
        t.duration = 1.0f; CHECK(getTrickLevel(t) == 1);
        t.duration = 3.0f; CHECK(getTrickLevel(t) == 2);
    }

    char buf[64];
    TrickInstance s = makeTrick(TrickType::STOPPIE);
    s.duration = 3.5f;
    CHECK(std::strcmp(nameFor(s, buf, sizeof(buf)), "Endless Stoppie") == 0);
}

TEST_CASE("fmx levels: air reads airtime from the lip, not the trick's duration") {
    TrickInstance t = makeTrick(TrickType::AIR);
    t.duration = 10.0f;   // classification is backdated; never the measure
    t.airTime = 1.4f; CHECK(getTrickLevel(t) == 0);
    t.airTime = 1.5f; CHECK(getTrickLevel(t) == 1);
    t.airTime = 3.5f; CHECK(getTrickLevel(t) == 2);

    char buf[64];
    CHECK(std::strcmp(nameFor(t, buf, sizeof(buf)), "Huge Air") == 0);
}

TEST_CASE("fmx levels: tricks without levels never get a word") {
    // Full rotations keep Double/Triple; Coaster Wheelie is too long for the
    // large row; shredding has none by design.
    for (TrickType type : { TrickType::BACKFLIP, TrickType::FRONTFLIP,
                            TrickType::BARREL_ROLL_LEFT, TrickType::SPIN_RIGHT,
                            TrickType::FLAT_360_LEFT, TrickType::COASTER_WHEELIE,
                            TrickType::BURNOUT, TrickType::DONUT,
                            TrickType::DRIFT_LEFT, TrickType::PIVOT_RIGHT,
                            TrickType::NONE }) {
        TrickInstance t = makeTrick(type);
        t.peakPitch = t.peakYaw = t.peakRoll = 999.0f;
        t.duration = t.airTime = 999.0f;
        CHECK(getTrickLevelRule(type) == nullptr);
        CHECK(getTrickLevel(t) == 0);
        CHECK(getTrickLevelWord(t) == nullptr);
    }

    char buf[64];
    TrickInstance flip = makeTrick(TrickType::BACKFLIP);
    flip.multiplier = 2;
    CHECK(std::strcmp(nameFor(flip, buf, sizeof(buf)), "Double Backflip") == 0);
}

TEST_CASE("fmx levels: a non-finite measurement is plain, never a level") {
    TrickInstance t = makeTrick(TrickType::WHIP_LEFT);
    t.peakYaw = std::numeric_limits<float>::quiet_NaN();
    CHECK(getTrickLevel(t) == 0);
    t.peakYaw = std::numeric_limits<float>::infinity();
    CHECK(getTrickLevel(t) == 0);
}

TEST_CASE("fmx levels: no levelled name is longer than one the large row already shows") {
    // The active row is the large title font on a standard-width HUD, and the
    // longest name it shows today is a doubled full rotation ("Double Barrel
    // Roll L"). A level word may not push a name past that; it is why Coaster
    // Wheelie has no levels ("Endless Coaster Wheelie" is 23).
    char buf[64];
    size_t longestShown = 0;
    for (int i = 1; i < static_cast<int>(TrickType::COUNT); ++i) {
        const TrickType type = static_cast<TrickType>(i);
        if (getPrimaryAxis(type) == RotationAxis::NONE || !isAirTrick(type)) continue;
        for (int mult = 2; mult <= 4; ++mult) {
            formatTrickName(type, mult, nullptr, buf, sizeof(buf));
            longestShown = std::max(longestShown, std::strlen(buf));
        }
    }
    REQUIRE(longestShown == 20);   // "Double Barrel Roll L"; re-read this test if it moves

    for (int i = 1; i < static_cast<int>(TrickType::COUNT); ++i) {
        const TrickType type = static_cast<TrickType>(i);
        for (int level = 1; level <= TRICK_LEVEL_COUNT; ++level) {
            const char* word = getTrickLevelWord(type, level);
            if (!word) continue;
            formatTrickName(type, 1, word, buf, sizeof(buf));
            INFO(buf);
            CHECK(std::strlen(buf) <= longestShown);
        }
    }

    TrickInstance coaster = makeTrick(TrickType::COASTER_WHEELIE);
    formatTrickName(coaster.type, 1, "Endless", buf, sizeof(buf));
    CHECK(std::strlen(buf) > longestShown);
}
