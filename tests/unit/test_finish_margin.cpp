// ============================================================================
// tests/unit/test_finish_margin.cpp
// Unit tests for core/finish_margin.h — the winner-to-runner-up margin behind
// Photo Finish.
//
// Regression test for a shipped bug with two halves, both from reading
// StandingsData::gap. That field is CACHED across the flag (the API zeroes gaps
// as the leader crosses and PluginData substitutes the last nonzero value to
// stop the standings flickering), so the "margin" it reported was the gap from
// before the runner-up closed it — Photo Finish unlocked once in 1728 installs.
// And gap is the sub-lap remainder, so a rider a full lap down with gap == 50
// read as a 50ms finish; the old exploration_test asserted exactly that race as
// both photo_finish AND lapped_field, which is how the false positive survived.
//
// The lap log is exact and unaffected by either: RaceLap gives every rider's
// lap times in ms directly. See the header for the full why.
//
// test_plugin_utils.cpp provides the doctest impl + main
// (DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN); this TU only registers more tests.
// ============================================================================
#include "doctest.h"

#include "core/finish_margin.h"

#include <deque>

namespace {

// A rider's log as PluginData holds it: newest lap at the FRONT, 0-based lap
// numbers, complete.
std::deque<LapLogEntry> log(std::initializer_list<int> lapTimesMs) {
    std::deque<LapLogEntry> out;
    int lapNum = 0;
    for (int ms : lapTimesMs) {
        out.push_front(LapLogEntry(lapNum++, -1, -1, -1, -1, ms, true, true));
    }
    return out;
}

}  // namespace

TEST_CASE("finish margin: the margin is the difference of the two race times") {
    // Three laps each, the runner-up 43ms adrift over the race. THE case the
    // old gap-reading code could not see.
    const auto winner = log({ 92000, 91500, 91800 });
    const auto second = log({ 92100, 91600, 91643 });
    CHECK(FinishMargin::marginMs(winner, 3, second, 3) == 43);

    // A comfortable win is still measured, just not close.
    CHECK(FinishMargin::marginMs(log({ 90000, 90000 }), 2, log({ 95000, 95000 }), 2) == 10000);

    // Dead heat to the millisecond: a real number, and zero. Photo Finish wants
    // a positive margin, so this is the caller's call, not ours.
    CHECK(FinishMargin::marginMs(log({ 90000 }), 1, log({ 90000 }), 1) == 0);
}

TEST_CASE("finish margin: a lapped runner-up is not a close finish") {
    // The false positive. Three laps to two: whatever the sub-lap remainder
    // looks like in the classification, this is a lap down.
    CHECK(FinishMargin::marginMs(log({ 90000, 90000, 90000 }), 3, log({ 90050, 90000 }), 2) == -1);
    // And the other way round, defensively.
    CHECK(FinishMargin::marginMs(log({ 90000, 90000 }), 2, log({ 90000, 90000, 90000 }), 3) == -1);
}

TEST_CASE("finish margin: an incomplete log is unknown, never a guess") {
    const auto three = log({ 90000, 90000, 90000 });

    // Joined late / the log rolled over (MAX_LAP_LOG_STORAGE): fewer entries
    // than the classification's lap count. Summing what is there would invent a
    // huge margin, or a tiny one.
    CHECK(FinishMargin::marginMs(three, 4, three, 4) == -1);
    CHECK(FinishMargin::totalRaceTimeMs(three, 4) == -1);
    // More complete laps than expected is just as wrong.
    CHECK(FinishMargin::totalRaceTimeMs(three, 2) == -1);

    // A lap still open contributes nothing and leaves the count short.
    std::deque<LapLogEntry> open = three;
    open.push_front(LapLogEntry(3, -1, -1, -1, -1, -1, true, false));
    CHECK(FinishMargin::totalRaceTimeMs(open, 4) == -1);
    CHECK(FinishMargin::totalRaceTimeMs(open, 3) == 270000);   // the three below it still sum

    // A complete lap with no time is a broken record, not a zero.
    std::deque<LapLogEntry> zeroed = three;
    zeroed.push_front(LapLogEntry(3, -1, -1, -1, -1, 0, true, true));
    CHECK(FinishMargin::totalRaceTimeMs(zeroed, 4) == -1);

    CHECK(FinishMargin::totalRaceTimeMs({}, 0) == -1);
    CHECK(FinishMargin::marginMs({}, 0, {}, 0) == -1);
}

TEST_CASE("finish margin: an invalid lap still counts as time on track") {
    // Cutting the track costs a penalty and invalidates the lap for timing, but
    // the rider still spent those seconds getting round, so the race time
    // includes it. Dropping it would fabricate a huge margin.
    std::deque<LapLogEntry> cut;
    cut.push_front(LapLogEntry(0, -1, -1, -1, -1, 90000, true,  true));
    cut.push_front(LapLogEntry(1, -1, -1, -1, -1, 88000, false, true));   // invalid
    CHECK(FinishMargin::totalRaceTimeMs(cut, 2) == 178000);
    CHECK(FinishMargin::marginMs(log({ 90000, 88000 }), 2, cut, 2) == 0);
}
