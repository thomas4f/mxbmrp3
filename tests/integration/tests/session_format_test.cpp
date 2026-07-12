// ============================================================================
// tests/integration/tests/session_format_test.cpp
// Race FORMAT handling and the finish-before-timer / overtime session clock —
// the "respect the race format (laps vs time vs time+laps)" and "races finishing
// before the timer runs out" concerns.
//
// Two things are pinned end-to-end via the /api/state snapshot (session.format is
// PluginUtils::formatSessionFormat; session.time is formatSessionClock over
// PluginData::getLeaderLapsToGo() — the single source shared with the in-game
// clock):
//
//   1. The format string differs per race format: pure-time "8:00", pure-laps
//      "5L", time+laps "8:00 + 5L".
//   2. A time+laps race's clock, once it expires, stops counting and becomes a
//      leader-relative label. When the clock first goes negative but the leader
//      hasn't yet crossed into the bonus laps, the clock HOLDS at "00:00" (the
//      documented finish-before-timer freeze), then steps N TO GO → FINAL LAP →
//      CHECKERED as the leader completes the added laps.
//
// One lifecycle (singletons persist across TEST_CASEs). The non-arming formats run
// first; overtime (a sticky flag) is driven last. Self-contained doctest.
// ============================================================================
#define DOCTEST_CONFIG_IMPLEMENT
#include "doctest.h"
#include "integration_main.h"
#include "plugin_host.h"
#include "assertions.h"

// PiBoSo session enum: 1=Practice, 6=Race1. state 16 = running.
static constexpr int PRACTICE = 1, RACE1 = 6;

static std::string sessTime(const nlohmann::json& d) {
    return d["session"].value("time", std::string());
}
static std::string sessFormat(const nlohmann::json& d) {
    return d["session"].value("format", std::string());
}

TEST_CASE("session format + overtime clock: laps vs time vs time+laps") {
    PluginHost host(dllPath());
    REQUIRE(host.loaded());
    host.startup("Z:\\tmp\\mxbmrp3-tests\\session_format\\");

    host.eventInit("TestTrack", "Alice");
    host.raceEvent("TestTrack");
    host.addEntry(10, "Alice");
    host.addEntry(22, "Bob");

    // --- Pure-time (non-race): "8:00", not a race ----------------------------
    host.session(PRACTICE, /*numLaps=*/0, /*lengthMs=*/480000);
    host.classify(PRACTICE, 300000, {
        { .num = 10, .best = 88000, .laps = 3 },
        { .num = 22, .best = 89000, .laps = 3 },
    });
    {
        auto d = host.snapshot();
        REQUIRE(d.is_object());
        CHECK(sessFormat(d) == "8:00");
        CHECK(d["session"].value("isRace", true) == false);
    }

    // --- Pure-laps race: "5L", is a race -------------------------------------
    host.session(RACE1, /*numLaps=*/5, /*lengthMs=*/0);
    host.classify(RACE1, 120000, {
        { .num = 10, .laps = 2, .gap = 0 },
        { .num = 22, .laps = 2, .gap = 1500 },
    });
    {
        auto d = host.snapshot();
        CHECK(sessFormat(d) == "5L");
        CHECK(d["session"].value("isRace", false) == true);
        // A pure-laps race isn't in overtime, so the clock is the plain countup
        // (getLeaderLapsToGo == -1 → MM:SS). Just assert it's not an overtime label.
        CHECK(sessTime(d) != "CHECKERED");
        CHECK(sessTime(d) != "FINAL LAP");
    }

    // --- Time+laps race: "8:00 + 2L", then the overtime state machine --------
    // 8 minutes then 2 added laps. Overtime arms when the clock first ticks
    // negative (lastSessionTime>0 && sessionTime<0). finishLap = leader lap at
    // that moment (3) + 2 = 5, i.e. the leader finishes upon completing lap 5.
    host.session(RACE1, /*numLaps=*/2, /*lengthMs=*/480000);

    // Clock still positive: normal MM:SS, format shows both limits.
    host.classify(RACE1, 5000, {
        { .num = 10, .laps = 3, .gap = 0 },
        { .num = 22, .laps = 3, .gap = 2000 },
    });
    {
        auto d = host.snapshot();
        CHECK(sessFormat(d) == "8:00 + 2L");
        CHECK(sessTime(d) == "00:05");
    }

    // Clock goes negative, leader still on lap 3 → overtime arms, finishLap=5.
    // toGo = 5-3+1 = 3 > sessionNumLaps(2), so the clock HOLDS at 00:00 (the
    // leader hasn't started a counted bonus lap yet — finish-before-timer freeze).
    host.classify(RACE1, -1000, {
        { .num = 10, .laps = 3, .gap = 0 },
        { .num = 22, .laps = 3, .gap = 2000 },
    });
    CHECK(sessTime(host.snapshot()) == "00:00");

    // Leader crosses S/F onto the first counted bonus lap (lap 4): "2 TO GO".
    host.classify(RACE1, -3000, {
        { .num = 10, .laps = 4, .gap = 0 },
        { .num = 22, .laps = 3, .gap = 2000 },
    });
    CHECK(sessTime(host.snapshot()) == "2 TO GO");

    // Leader onto lap 5 (the last): "FINAL LAP".
    host.classify(RACE1, -5000, {
        { .num = 10, .laps = 5, .gap = 0 },
        { .num = 22, .laps = 4, .gap = 2000 },
    });
    CHECK(sessTime(host.snapshot()) == "FINAL LAP");

    // Leader completes lap 5 (numLaps 6 > finishLap 5) → finished → "CHECKERED".
    host.classify(RACE1, -7000, {
        { .num = 10, .laps = 6, .gap = 0 },
        { .num = 22, .laps = 5, .gap = 2000 },
    });
    CHECK(sessTime(host.snapshot()) == "CHECKERED");

    host.shutdown();
}
