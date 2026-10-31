// ============================================================================
// tests/integration/tests/server_best_test.cpp
// The Server best gap reference: another rider's fastest lap, traced from the
// position batches (core/field_lap_traces.h) and planted as PbGapTracker's
// SERVER_BEST by PluginData.
//
// HOW. A rival (#20) laps at 60 s and the player (#10) at 66 s, both sampled in
// every batch on the lap timer's test clock (MXBMRP3_Test_LapTimerSetNowUs), so
// wrap-to-wrap times are real. When the rival's first whole lap completes as
// the overall best, the player's live gap against Server best is read two thirds
// round their own lap: 6 s slower per lap is ~4 s down by then.
//
// The cases:
//   1. The traced lap is the reference, in both orders at the line (the RaceLap
//      after the rival's wrap, and before it); the session PB is still empty,
//      so the two are told apart.
//   2. A faster overall best set by a rider never in the batches (a lap the
//      game replays on join) cannot be traced: the reference DROPS, rather than
//      keep showing the slower lap under the server best's name.
//   3. A new session drops it with the session's laps.
// ============================================================================
#include "doctest.h"
#include "integration_main.h"
#include "plugin_host.h"

#include <vector>

namespace {
constexpr int PRACTICE = 1;
constexpr int REF_SESSION = 0, REF_SERVER = 3;   // PbGapTracker::Ref
constexpr int PLAYER = 10, RIVAL = 20, LATE = 30;
constexpr long long STEP_US = 50000;   // 20 batches a second
constexpr const char* SAVE = "Z:\\tmp\\mxbmrp3-tests\\server_best\\";

struct Rider {
    int num;
    float pos;
    int lapMs;
    int laps = 0;    // wraps so far
};

struct Field {
    PluginHost& host;
    std::vector<Rider> riders;
    long long nowUs = 1000000000;

    // One batch, STEP_US on. Returns the riders that wrapped in it.
    std::vector<int> step() {
        nowUs += STEP_US;
        host.lapTimerSetNowUs(nowUs);
        std::vector<TrackRow> rows;
        std::vector<int> wrapped;
        for (Rider& r : riders) {
            r.pos += static_cast<float>(STEP_US / 1000) / static_cast<float>(r.lapMs);
            if (r.pos >= 1.0f) {
                r.pos -= 1.0f;
                ++r.laps;
                wrapped.push_back(r.num);
            }
            rows.push_back({ r.num, r.pos });
        }
        host.raceTrackPosition(rows);
        return wrapped;
    }
    const Rider& rider(int num) const {
        for (const Rider& r : riders) if (r.num == num) return r;
        return riders.front();
    }
    // Ride until `num` has wrapped `laps` times in all.
    void untilWrap(int num, int laps) {
        while (rider(num).laps < laps) step();
    }
    void untilPos(int num, float pos) {
        while (rider(num).pos < pos) step();
    }
};

void beginSession(PluginHost& host) {
    host.eventInit("TestTrack", "Alice", 1600.0f, 2, "Test 450", "MX1", /*trackId=*/"testtrack");
    host.raceEvent("TestTrack");
    host.session(PRACTICE, /*numLaps=*/0, /*lengthMs=*/1200000);
    host.addEntry(PLAYER, "Alice");
    host.addEntry(RIVAL, "Bob");
    host.addEntry(LATE, "Carol");
}

// The rival's first whole lap (its second wrap closes it), completed as the
// overall best: RaceLap after the wrap, or `lapFirst` a batch before it.
void rivalSetsServerBest(Field& field, bool lapFirst) {
    if (lapFirst) {
        field.untilWrap(RIVAL, 1);
        field.untilPos(RIVAL, 0.995f);
        field.host.raceLap(PRACTICE, RIVAL, /*lapNum=*/2, 60000, /*best=*/2);
        field.untilWrap(RIVAL, 2);
    } else {
        field.untilWrap(RIVAL, 2);
        field.host.raceLap(PRACTICE, RIVAL, /*lapNum=*/2, 60000, /*best=*/2);
    }
}
}  // namespace

TEST_CASE("the server best lap is traced and read as a reference, in both orders at the line") {
    for (const bool lapFirst : { false, true }) {
        CAPTURE(lapFirst);
        PluginHost host(dllPath());
        REQUIRE(host.loaded());
        host.startup(SAVE);
        beginSession(host);
        Field field{ host, { { PLAYER, 0.10f, 66000 }, { RIVAL, 0.30f, 60000 } } };

        int gap = 0;
        CHECK_FALSE(host.liveGapRef(REF_SERVER, gap));   // nothing set yet
        rivalSetsServerBest(field, lapFirst);

        // The player's own lap began at their wrap (~59 s in). Two thirds round
        // it, 6 s slower per lap is ~4 s down on the rival's pace.
        field.untilWrap(PLAYER, 1);
        field.untilPos(PLAYER, 0.66f);
        REQUIRE_MESSAGE(host.liveGapRef(REF_SERVER, gap), "no Server best reference after the rival's traced lap");
        CHECK_MESSAGE((gap >= 3600 && gap <= 4400), "expected ~+3960 ms against the rival's lap, got " << gap);
        CHECK_FALSE(host.liveGapRef(REF_SESSION, gap));   // the player has no lap of their own yet
        host.shutdown();
    }
}

TEST_CASE("a faster server best that could not be traced drops the reference") {
    PluginHost host(dllPath());
    REQUIRE(host.loaded());
    host.startup(SAVE);
    beginSession(host);
    Field field{ host, { { PLAYER, 0.10f, 66000 }, { RIVAL, 0.30f, 60000 } } };
    rivalSetsServerBest(field, false);
    field.untilWrap(PLAYER, 1);
    field.untilPos(PLAYER, 0.30f);
    int gap = 0;
    REQUIRE(host.liveGapRef(REF_SERVER, gap));

    // Carol was never in a batch (a lap the game replays on join): her 58 s is
    // the server best now, and the rival's 60 s trace is not it.
    host.raceLap(PRACTICE, LATE, /*lapNum=*/3, 58000, /*best=*/2);
    field.step();
    CHECK_FALSE_MESSAGE(host.liveGapRef(REF_SERVER, gap), "the slower lap still read as the server best");
    host.shutdown();
}

TEST_CASE("a new session drops the server best with its laps") {
    PluginHost host(dllPath());
    REQUIRE(host.loaded());
    host.startup(SAVE);
    beginSession(host);
    Field field{ host, { { PLAYER, 0.10f, 66000 }, { RIVAL, 0.30f, 60000 } } };
    rivalSetsServerBest(field, false);
    field.untilWrap(PLAYER, 1);
    field.untilPos(PLAYER, 0.30f);
    int gap = 0;
    REQUIRE(host.liveGapRef(REF_SERVER, gap));

    host.session(PRACTICE + 1, /*numLaps=*/0, /*lengthMs=*/1200000);
    field.untilWrap(PLAYER, 2);
    field.untilPos(PLAYER, 0.30f);
    CHECK_FALSE(host.liveGapRef(REF_SERVER, gap));
    host.shutdown();
}
