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

// A third, read from the StandingsHud's own session-info row rather than the
// snapshot (the row is rendered text, not a JSON field):
//
//   3. An UNLIMITED session — no clock and no lap target, which is what Testing
//      and an open practice are — shows the game's clock COUNTING UP, because
//      that is the direction it runs when there is nothing to count down to.
//      "Waiting" is the exception: between sessions the game sends no
//      classifications, so the value sitting in the clock belongs to the session
//      that just ended and the row states the label alone.
//
// One lifecycle (singletons persist across TEST_CASEs). The non-arming formats run
// first; overtime (a sticky flag) is driven last. Self-contained doctest.
// ============================================================================
#define DOCTEST_CONFIG_IMPLEMENT
#include "doctest.h"
#include "integration_main.h"
#include "plugin_host.h"
#include "assertions.h"

// PiBoSo session enum: 0=Waiting, 1=Practice, 6=Race1. state 16 = running.
static constexpr int WAITING = 0, PRACTICE = 1, RACE1 = 6;

static std::string sessTime(const nlohmann::json& d) {
    return d["session"].value("time", std::string());
}
static std::string sessFormat(const nlohmann::json& d) {
    return d["session"].value("format", std::string());
}

// The StandingsHud's session-info row ("<session>: <clock / lap / label>"), by its
// label rather than by string index: what the row SAYS is the assertion, and an
// index would also pass on the title above it.
static std::string sessionInfoRow(PluginHost& host, const std::string& label) {
    for (const auto& r : host.hudStringRows(PluginHost::HUD_STANDINGS)) {
        if (r.text.rfind(label, 0) == 0) return r.text;
    }
    return {};
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

    // --- Unlimited session: the clock counts UP -------------------------------
    // Neither limit set, which is what a Testing event and an open practice send.
    // The game's own clock is then elapsed time, and the row shows it; the
    // format string has nothing to state.
    REQUIRE(host.hasStringRows());
    host.session(PRACTICE, /*numLaps=*/0, /*lengthMs=*/0);
    host.classify(PRACTICE, 300000, {
        { .num = 10, .best = 88000, .laps = 3 },
        { .num = 22, .best = 89000, .laps = 3 },
    });
    {
        CHECK(sessFormat(host.snapshot()) == "");
        CHECK(sessionInfoRow(host, "Practice") == "Practice: 05:00");
    }
    // Ascending, not a frozen or counted-down value: a later classification reads
    // higher. (The countdown cases above go the other way, which is the point.)
    host.classify(PRACTICE, 425000, {
        { .num = 10, .best = 88000, .laps = 4 },
        { .num = 22, .best = 89000, .laps = 4 },
    });
    CHECK(sessionInfoRow(host, "Practice") == "Practice: 07:05");

    // --- Waiting: the label alone -------------------------------------------
    // Same unlimited shape, but the clock is a leftover from the session that
    // ended, so nothing is shown after the label.
    host.session(WAITING, /*numLaps=*/0, /*lengthMs=*/0);
    host.classify(WAITING, 425000, {
        { .num = 10, .best = 88000, .laps = 4 },
        { .num = 22, .best = 89000, .laps = 4 },
    });
    CHECK(sessionInfoRow(host, "Waiting") == "Waiting");

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

    // --- The two labels the count-up was added for ---------------------------
    // Everything above runs in a Race event, so the label reads "Practice". The
    // sessions a player actually meets this in are a TESTING event's, and their
    // labels are the event type's doing, not the session's: eventType arrives on
    // EventInit (m_iType = 1), and the same session reads "Open Practice" when
    // that event is online (serverType 2) and "Testing" when it is not.
    // Driven last so re-initialising the event cannot disturb the overtime
    // machine above, which is a sticky flag.
    host.eventInit("TestTrack", "Alice", 1600.0f, /*type=*/1);
    host.raceEvent("TestTrack", /*type=*/1);
    host.session(PRACTICE, /*numLaps=*/0, /*lengthMs=*/0);
    host.classify(PRACTICE, 610000, {
        { .num = 10, .best = 88000, .laps = 6 },
        { .num = 22, .best = 89000, .laps = 6 },
    });
    CHECK(sessionInfoRow(host, "Testing") == "Testing: 10:10");

    host.eventInit("TestTrack", "Alice", 1600.0f, /*type=*/1, "Test 450", "MX1", "",
                   /*serverType=*/2);
    host.raceEvent("TestTrack", /*type=*/1);
    host.session(PRACTICE, /*numLaps=*/0, /*lengthMs=*/0);
    host.classify(PRACTICE, 610000, {
        { .num = 10, .best = 88000, .laps = 6 },
        { .num = 22, .best = 89000, .laps = 6 },
    });
    CHECK(sessionInfoRow(host, "Open Practice") == "Open Practice: 10:10");

    host.shutdown();
}
