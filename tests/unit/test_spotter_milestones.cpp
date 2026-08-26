// ============================================================================
// tests/unit/test_spotter_milestones.cpp
// Pins the session-progress milestone rules (core/spotter_milestones.h).
// The behaviors a plausible edit breaks silently:
//  - CROSSING-edged: a mid-session join (first tick deep into the race) must
//    swallow already-passed thresholds, not announce them all at once;
//  - existence rules: "ten to go" in a ten-minute race is the start, not a
//    milestone, and a 10-minute race's halfway IS "five left" — collapsed;
//  - once per session, re-armed by reset() / a clock rewind;
//  - short lap sprints have no halfway.
// ============================================================================
#include "doctest.h"

#include <cstring>
#include <set>
#include <string>

#include "core/spotter_milestones.h"
#include "core/spotter_cue_pack.h"

using SpotterMilestones::State;

namespace {
bool is(const char* cue, const char* want) {
    return cue != nullptr && std::strcmp(cue, want) == 0;
}
}  // namespace

TEST_CASE("timed: thresholds cross once, in order, when they exist") {
    State s;
    // 24 minutes: halfway (12:00), ten-to-go (14:00) and five-left (19:00)
    // are all distinct. (A 20-minute race's halfway IS ten-to-go — that
    // collapse is pinned below.)
    const int len = 24 * 60000;
    CHECK(s.updateTime(0, len) == nullptr);            // arming tick
    CHECK(s.updateTime(60000, len) == nullptr);
    CHECK(is(s.updateTime(12 * 60000, len), "halfway_point"));
    CHECK(s.updateTime(12 * 60000 + 5000, len) == nullptr);   // once
    CHECK(is(s.updateTime(14 * 60000 + 1000, len), "ten_minutes_remaining"));
    CHECK(is(s.updateTime(19 * 60000 + 1000, len), "five_minutes_remaining"));
    CHECK(s.updateTime(20 * 60000, len) == nullptr);

    // 20 minutes: halfway == ten-to-go; the fixed call wins.
    State s20;
    const int len20 = 20 * 60000;
    CHECK(s20.updateTime(0, len20) == nullptr);
    CHECK(is(s20.updateTime(10 * 60000 + 1000, len20), "ten_minutes_remaining"));
    CHECK(s20.updateTime(11 * 60000, len20) == nullptr);
}

TEST_CASE("timed: existence rules trim the list for short races") {
    // 10-minute race: no "ten to go"; halfway (5:00) IS "five left" — the
    // fixed call wins and halfway collapses into it.
    State s;
    const int len = 10 * 60000;
    CHECK(s.updateTime(0, len) == nullptr);
    CHECK(is(s.updateTime(5 * 60000 + 1000, len), "five_minutes_remaining"));
    CHECK(s.updateTime(6 * 60000, len) == nullptr);   // no separate halfway

    // 90-second sprint: only halfway exists.
    State s2;
    CHECK(s2.updateTime(0, 90000) == nullptr);
    CHECK(is(s2.updateTime(46000, 90000), "halfway_point"));
    CHECK(s2.updateTime(60000, 90000) == nullptr);
}

TEST_CASE("timed: mid-session join swallows passed thresholds silently") {
    State s;
    const int len = 24 * 60000;
    // First tick lands past halfway (12:00): arm only, swallow it.
    CHECK(s.updateTime(13 * 60000, len) == nullptr);
    CHECK(s.updateTime(13 * 60000 + 1000, len) == nullptr);
    // The calls still ahead fire normally.
    CHECK(is(s.updateTime(14 * 60000 + 1000, len), "ten_minutes_remaining"));
    CHECK(is(s.updateTime(19 * 60000 + 1000, len), "five_minutes_remaining"));
}

TEST_CASE("timed: clock rewind (restart) re-arms everything") {
    State s;
    const int len = 90000;
    CHECK(s.updateTime(0, len) == nullptr);
    CHECK(is(s.updateTime(46000, len), "halfway_point"));
    CHECK(s.updateTime(0, len) == nullptr);   // rewind: reset + re-arm
    CHECK(is(s.updateTime(46000, len), "halfway_point"));
}

TEST_CASE("laps: leader crossing half distance, sprints excluded") {
    State s;
    CHECK(s.updateLaps(1, 6) == nullptr);
    CHECK(is(s.updateLaps(3, 6), "halfway_point"));
    CHECK(s.updateLaps(4, 6) == nullptr);     // once
    State s2;
    CHECK(s2.updateLaps(2, 3) == nullptr);    // 3-lap sprint: never
    // Odd counts round up: lap 3 of 5 is past half, lap 2 is not.
    State s3;
    CHECK(s3.updateLaps(2, 5) == nullptr);
    CHECK(is(s3.updateLaps(3, 5), "halfway_point"));
}

// This module hands SpotterManager a cue key as a STRING, so nothing links the
// two: renaming the keys left this returning "time_10min" while the shipped
// pack and the registry both said "ten_minutes_remaining", and every gate
// stayed green. Assert the keys it can emit are keys that exist.
TEST_CASE("milestone keys are real cue keys") {
    for (const char* key : { "ten_minutes_remaining", "five_minutes_remaining",
                             "halfway_point" }) {
        CAPTURE(key);
        CHECK(SpotterCuePack::isCueKey(key));
    }
    // ...and that those three strings are the only ones it produces, so a new
    // milestone cannot be added without coming through here.
    std::set<std::string> seen;
    SpotterMilestones::State s;
    const int len = 20 * 60000;
    for (int t2 = 0; t2 <= len; t2 += 1000) {
        if (const char* c = s.updateTime(t2, len)) seen.insert(c);
    }
    SpotterMilestones::State laps;
    for (int l = 1; l <= 10; ++l) {
        if (const char* c = laps.updateLaps(l, 10)) seen.insert(c);
    }
    for (const std::string& k : seen) {
        CAPTURE(k);
        CHECK(SpotterCuePack::isCueKey(k));
    }
    CHECK(seen.size() == 3);
}

// updateLaps was LEVEL-triggered where updateTime is crossing-edged, against
// the header's own promise two functions up. Joining a ten-lap race with the
// leader on lap eight announced "Halfway there" the instant the first
// classification arrived — a threshold that had passed before you were there.
TEST_CASE("milestones: joining a lap race past halfway swallows it silently") {
    State m;
    // First call ARMS. The leader is already at 8 of 10, so halfway is spent.
    CHECK(m.updateLaps(8, 10) == nullptr);
    CHECK(m.updateLaps(9, 10) == nullptr);
    CHECK(m.updateLaps(10, 10) == nullptr);
}

TEST_CASE("milestones: a lap race joined from the start still calls halfway") {
    State m;
    CHECK(m.updateLaps(1, 10) == nullptr);   // arms, nothing passed
    CHECK(m.updateLaps(2, 10) == nullptr);
    CHECK(m.updateLaps(3, 10) == nullptr);
    REQUIRE(m.updateLaps(5, 10) != nullptr);   // ...and fires on the crossing
    CHECK(m.updateLaps(6, 10) == nullptr);     // once per session
}

TEST_CASE("milestones: a jump over several thresholds speaks the LATEST one") {
    // A 24-minute race: halfway (12:00) comes BEFORE ten-to-go (14:00), so a
    // jump from 11:00 to 15:00 crosses both. The if-chain's order makes
    // ten-to-go win — the most current fact, not the first crossed — and
    // halfway is spent silently, never spoken late. This pins the order so a
    // reshuffle of the chain (or a comment claiming "earliest fires") cannot
    // change what a joining player hears.
    constexpr int kMin = 60000;
    State m;
    CHECK(m.updateTime(11 * kMin, 24 * kMin) == nullptr);   // arms
    const char* cue = m.updateTime(15 * kMin, 24 * kMin);
    REQUIRE(cue != nullptr);
    CHECK(std::string(cue) == "ten_minutes_remaining");
    // Both were consumed: neither replays on the next ticks.
    CHECK(m.updateTime(16 * kMin, 24 * kMin) == nullptr);
    // ...and five-to-go still fires at its own crossing, untouched by the jump.
    const char* five = m.updateTime(19 * kMin + 1000, 24 * kMin);
    REQUIRE(five != nullptr);
    CHECK(std::string(five) == "five_minutes_remaining");
}
