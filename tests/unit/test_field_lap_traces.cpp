// ============================================================================
// tests/unit/test_field_lap_traces.cpp
// Every rider's lap traced from the position batches, and the server best lap
// kept as one (core/field_lap_traces.h). Pins:
//   - a flagged lap is kept in either callback order: RaceLap after the wrap
//     that closed it, and RaceLap before it (held until the wrap); and the
//     kept table is stretched to the official lap time
//   - THE CHECKS: a lap whose start was not seen (the first lap a rider is
//     seen on, a lap replayed on join with no samples at all), a lap with a
//     gap in its samples wider than the tracker's walk-back, a lap broken by
//     a jump (reset to track), and a lap whose wrap-to-wrap time is not the
//     official one are never kept
//   - only a FASTER flagged lap replaces the kept one; an unflagged or invalid
//     lap never does
//   - erase() forgets a rider's lap in progress and keeps the server best;
//     clear() drops both (the PerRider<> contract PluginData relies on)
//   - non-finite positions are ignored
// ============================================================================
#include "doctest.h"
#include "core/field_lap_traces.h"

#include <cstdint>
#include <limits>

namespace {

constexpr int STEP_MS = 20;   // ~50 Hz, slower than the real batches

// Ride from `fromPos` to the line and on to `toPos` (past the wrap when
// toPos < fromPos), at a pace of lapMs per lap, sampling every STEP_MS.
// Returns whether best() changed on any sample; advances `nowMs`.
bool ride(FieldLapTraces& f, int raceNum, float fromPos, float toPos, int lapMs, std::int64_t& nowMs) {
    float distance = toPos - fromPos;
    if (distance <= 0.0f) distance += 1.0f;
    const int steps = static_cast<int>(distance * lapMs / STEP_MS);
    bool changed = false;
    for (int i = 1; i <= steps; ++i) {
        float pos = fromPos + distance * static_cast<float>(i) / static_cast<float>(steps);
        if (pos >= 1.0f) pos -= 1.0f;
        nowMs += STEP_MS;
        changed |= f.onSample(raceNum, pos, nowMs);
    }
    return changed;
}

// One whole lap from just past the line, ending just past it again.
bool lap(FieldLapTraces& f, int raceNum, int lapMs, std::int64_t& nowMs) {
    return ride(f, raceNum, 0.001f, 0.001f, lapMs, nowMs);
}

// A rider seen first mid-lap, then ridden up to just past the line: the next
// lap is their first observed one.
void join(FieldLapTraces& f, int raceNum, int lapMs, std::int64_t& nowMs) {
    f.onSample(raceNum, 0.5f, nowMs);
    ride(f, raceNum, 0.5f, 0.001f, lapMs, nowMs);
}

}  // namespace

TEST_CASE("a flagged lap is kept in both callback orders, stretched to the official time") {
    FieldLapTraces f;
    std::int64_t now = 1000;
    join(f, 7, 60000, now);
    CHECK(f.best().lapTimeMs == 0);

    SUBCASE("RaceLap after the wrap") {
        lap(f, 7, 60000, now);
        // The sampled lap ran 60 000 ms wrap to wrap; the official time is 60 040.
        CHECK(f.onLapCompleted(7, 60040, true, now + 30));
    }
    SUBCASE("RaceLap before the wrap: held until the wrap closes the lap") {
        ride(f, 7, 0.001f, 0.995f, 60000, now);
        CHECK_FALSE(f.onLapCompleted(7, 60040, true, now));
        CHECK(ride(f, 7, 0.995f, 0.001f, 60000, now));
    }
    REQUIRE(f.best().lapTimeMs == 60040);
    CHECK(f.best().raceNum == 7);
    // Halfway: ~30 s sampled, read at the official pace.
    const PbGapTracker::Point& half = f.best().table[500];
    REQUIRE(half.valid);
    CHECK(half.elapsedMs == doctest::Approx(30020).epsilon(0.002));
    // The last slots end on the official lap time.
    int last = -1;
    for (int i = 0; i < FieldLapTraces::NUM_POINTS; ++i) if (f.best().table[i].valid) last = i;
    REQUIRE(last >= FieldLapTraces::NUM_POINTS - 1 - FieldLapTraces::MAX_HOLE_POINTS);
    CHECK(f.best().table[last].elapsedMs <= 60040);
    CHECK(f.best().table[last].elapsedMs >= 60040 - 2 * STEP_MS);
}

TEST_CASE("THE CHECKS: no lap that is not the flagged lap whole is kept") {
    FieldLapTraces f;
    std::int64_t now = 1000;

    SUBCASE("the first lap a rider is seen on has no seen start") {
        f.onSample(3, 0.2f, now);
        ride(f, 3, 0.2f, 0.001f, 60000, now);
        CHECK_FALSE(f.onLapCompleted(3, 48000, true, now));
    }
    SUBCASE("a lap replayed on join: the rider was never sampled") {
        CHECK_FALSE(f.onLapCompleted(3, 60000, true, now));
    }
    SUBCASE("a gap in the samples wider than the tracker's walk-back") {
        join(f, 3, 60000, now);
        ride(f, 3, 0.001f, 0.40f, 60000, now);
        // Out of the batches from 0.40 to 0.42 (2% of the lap) -- no samples.
        now += 1200;
        ride(f, 3, 0.42f, 0.001f, 60000, now);
        CHECK_FALSE(f.onLapCompleted(3, 60000, true, now));
    }
    SUBCASE("a gap within the walk-back is fine") {
        join(f, 3, 60000, now);
        ride(f, 3, 0.001f, 0.40f, 60000, now);
        now += 300;   // 0.5% of the lap
        ride(f, 3, 0.405f, 0.001f, 60000, now);
        CHECK(f.onLapCompleted(3, 60000, true, now));
    }
    SUBCASE("a jump (reset to track) breaks the lap") {
        join(f, 3, 60000, now);
        ride(f, 3, 0.001f, 0.50f, 60000, now);
        now += STEP_MS;
        f.onSample(3, 0.44f, now);   // put back 6% of the lap
        ride(f, 3, 0.44f, 0.001f, 60000, now);
        CHECK_FALSE(f.onLapCompleted(3, 63600, true, now));
    }
    SUBCASE("wrap to wrap is not the official time") {
        join(f, 3, 60000, now);
        lap(f, 3, 60000, now);
        CHECK_FALSE(f.onLapCompleted(3, 60000 + FieldLapTraces::MAX_TIME_ERROR_MS + 50, true, now));
    }
    SUBCASE("a RaceLap long after the wrap is not that lap's") {
        join(f, 3, 60000, now);
        lap(f, 3, 60000, now);
        CHECK_FALSE(f.onLapCompleted(3, 60000, true, now + FieldLapTraces::MATCH_WINDOW_MS + 1));
    }
}

TEST_CASE("only a faster flagged lap replaces the kept one") {
    FieldLapTraces f;
    std::int64_t now = 1000;
    // Riders are ridden one after the other here, so each is joined right
    // before its laps: a rider left unsampled meanwhile would carry the wait.
    join(f, 1, 60000, now);
    lap(f, 1, 60000, now);
    REQUIRE(f.onLapCompleted(1, 60000, true, now));
    join(f, 2, 59000, now);

    // Not flagged, or not valid: never a candidate, however fast.
    lap(f, 2, 59000, now);
    CHECK_FALSE(f.onLapCompleted(2, 59000, false, now));
    CHECK(f.best().raceNum == 1);

    // Flagged and faster: replaces it.
    lap(f, 2, 59000, now);
    CHECK(f.onLapCompleted(2, 59000, true, now));
    CHECK(f.best().raceNum == 2);
    CHECK(f.best().lapTimeMs == 59000);

    // Flagged but slower than the kept one (the game would not flag it): kept as is.
    join(f, 1, 60000, now);
    lap(f, 1, 60000, now);
    CHECK_FALSE(f.onLapCompleted(1, 60000, true, now));
    CHECK(f.best().raceNum == 2);
}

TEST_CASE("erase forgets a rider's lap, keeps the server best; clear drops both") {
    FieldLapTraces f;
    std::int64_t now = 1000;
    join(f, 5, 60000, now);
    lap(f, 5, 60000, now);
    REQUIRE(f.onLapCompleted(5, 60000, true, now));

    // A reused race number does not inherit the departed rider's lap in progress.
    ride(f, 5, 0.001f, 0.5f, 58000, now);
    f.erase(5);
    CHECK(f.best().lapTimeMs == 60000);
    f.onSample(5, 0.5f, now);
    ride(f, 5, 0.5f, 0.001f, 58000, now);
    CHECK_FALSE(f.onLapCompleted(5, 58000, true, now));   // its start was not seen

    f.clear();
    CHECK(f.best().lapTimeMs == 0);
    CHECK(f.best().raceNum == -1);
}

TEST_CASE("non-finite positions are ignored") {
    FieldLapTraces f;
    std::int64_t now = 1000;
    join(f, 9, 60000, now);
    ride(f, 9, 0.001f, 0.3f, 60000, now);
    CHECK_FALSE(f.onSample(9, std::numeric_limits<float>::quiet_NaN(), now));
    CHECK_FALSE(f.onSample(9, std::numeric_limits<float>::infinity(), now));
    ride(f, 9, 0.3f, 0.001f, 60000, now);
    CHECK(f.onLapCompleted(9, 60000, true, now));
}
