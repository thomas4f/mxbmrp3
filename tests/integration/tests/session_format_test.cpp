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
// And three about the finish, two from in-game recordings (second TEST_CASE):
//
//   4. The game counts the bonus laps from its OWN time-up callback (a
//      RaceSessionState re-send of IN_PROGRESS), which lands one classification
//      tick after the clock first reads negative. A leader crossing the line in
//      that gap ran the lap "before expiry": a player's 1:00 + 2L qualifying was
//      flagged CHECKERED a lap early (10:32:32.032 clock -12 ms, .062 crossing,
//      .063 re-send; the game ran one more lap).
//   5. A finish time is the sum of the rider's laps, exact to the millisecond.
//      It used to be the session clock, which ticks every ~30 ms and which
//      RACE OVER restarts: the classification carrying the last crossing is
//      already on the new clock, so the last finisher's time came out as 30 ms
//      (lap race) or the session length less 30 ms (timed). The clock, held at
//      Race Over and counted on by the restarted one, stays the fallback for a
//      rider whose laps were not all seen.
//   6. With one added lap, a leader crossing in that gap completes the armed
//      finish lap, which the game counts as run before expiry: that crossing
//      is not the white flag.
//
// One lifecycle (singletons persist across TEST_CASEs). The non-arming formats run
// first; overtime (a sticky flag) is driven last. Self-contained doctest.
// ============================================================================
#include "doctest.h"
#include "integration_main.h"
#include "plugin_host.h"
#include "assertions.h"

// PiBoSo session enum: 0=Waiting, 1=Practice, 6=Race1. state 16 = running.
static constexpr int WAITING = 0, PRACTICE = 1, RACE1 = 6, RACE2 = 7;
static constexpr int IN_PROGRESS = 16, RACE_OVER = 512;

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

TEST_CASE("race finish: the game's time-up callback and the Race Over clock") {
    PluginHost host(dllPath());
    REQUIRE(host.loaded());
    host.startup("Z:\\tmp\\mxbmrp3-tests\\session_format_finish\\");
    REQUIRE(host.finishLap() != -2);

    host.eventInit("TestTrack", "Alice");
    host.raceEvent("TestTrack");
    host.addEntry(10, "Alice");
    host.addEntry(22, "Bob");

    // --- 4a. Leader crosses inside the gap: one more lap than armed ----------
    host.session(RACE2, /*numLaps=*/2, /*lengthMs=*/180000);
    host.classify(RACE2, 25, {
        { .num = 10, .laps = 0, .gap = 0 },
        { .num = 22, .laps = 0, .gap = 2000 },
    });
    host.classify(RACE2, -5, {
        { .num = 10, .laps = 0, .gap = 0 },
        { .num = 22, .laps = 0, .gap = 2000 },
    });
    CHECK(host.finishLap() == 2);          // armed at the negative tick: 0 + 2
    host.raceLap(RACE2, 10, 1, 60039);     // 30 ms later, before the game's callback
    host.raceSessionState(RACE2, IN_PROGRESS, 180000);
    CHECK(host.finishLap() == 3);          // the game's count: 1 + 2

    // A later re-send of the same state is not the time-up moment.
    host.raceLap(RACE2, 10, 2, 45000);
    host.raceSessionState(RACE2, IN_PROGRESS, 180000);
    CHECK(host.finishLap() == 3);

    // Laps 2 and 3 complete: FINAL LAP, not the early CHECKERED.
    host.raceLap(RACE2, 10, 3, 43000);
    host.classify(RACE2, -61000, {
        { .num = 10, .laps = 3, .gap = 0 },
        { .num = 22, .laps = 2, .gap = 2000 },
    });
    CHECK(sessTime(host.snapshot()) == "FINAL LAP");
    CHECK(host.riderFinishTime(10) == -1);

    // --- 5a. Timed: the finish lands on the Race Over tick --------------------
    // The leader's time is the sum of the four laps, not the restarted clock.
    host.raceLap(RACE2, 10, 4, 44000);
    host.raceSessionState(RACE2, RACE_OVER, 180000);
    host.classify(RACE2, 30, {
        { .num = 10, .laps = 4, .gap = 0 },
        { .num = 22, .laps = 3, .gap = 2000 },
    }, RACE_OVER);
    CHECK(host.riderFinishTime(10) == 60039 + 45000 + 43000 + 44000);
    // Bob's laps were never seen (no RaceLap), and he crosses a minute after
    // the leader: the fallback is the clock held at its last running value plus
    // the restarted clock's minute -- not the restarted clock alone, and not the
    // held clock alone, which is the leader's finish.
    host.classify(RACE2, 60030, {
        { .num = 10, .laps = 4, .gap = 0 },
        { .num = 22, .laps = 4, .gap = 2000 },
    }, RACE_OVER);
    CHECK(host.riderFinishTime(22) == 180000 + 61000 + 60030);

    // --- 4b. Control: crossing well before expiry, the armed lap stands ------
    host.session(RACE1, /*numLaps=*/2, /*lengthMs=*/180000);
    host.classify(RACE1, 1100, {
        { .num = 10, .laps = 3, .gap = 0 },
        { .num = 22, .laps = 3, .gap = 2000 },
    });
    host.classify(RACE1, -5, {
        { .num = 10, .laps = 3, .gap = 0 },
        { .num = 22, .laps = 3, .gap = 2000 },
    });
    host.raceSessionState(RACE1, IN_PROGRESS, 180000);
    CHECK(host.finishLap() == 5);

    // --- 5b. Pure laps: the finish read "30 ms" -------------------------------
    // The recorded race: laps of 72.394 and 46.033 s; the clock's last tick
    // before the line read 118.400.
    host.session(RACE1, /*numLaps=*/2, /*lengthMs=*/0);
    host.raceLap(RACE1, 10, 1, 72394);
    host.classify(RACE1, 118400, {
        { .num = 10, .laps = 1, .gap = 0 },
        { .num = 22, .laps = 1, .gap = 2000 },
    });
    host.raceLap(RACE1, 10, 2, 46033);
    host.raceSessionState(RACE1, RACE_OVER);
    host.classify(RACE1, 30, {
        { .num = 10, .laps = 2, .gap = 0 },
        { .num = 22, .laps = 1, .gap = 2000 },
    }, RACE_OVER);
    CHECK(host.riderFinishTime(10) == 72394 + 46033);

    // --- 4c. Time + 1 lap: the crossing in the gap is not the white flag ------
    // Armed at the negative tick as 0 + 1, so the leader's crossing 30 ms later
    // completes the armed lap. The game counts that lap as run before expiry,
    // and the last lap starts at the NEXT crossing; "Final lap" (and the
    // spotter's cue) used to fire on this one, then again on the next.
    auto finalLaps = [&host]() {
        int n = 0;
        for (const auto& e : host.snapshot().value("events", nlohmann::json::array()))
            if (e.value("message", std::string()).find("inal lap") != std::string::npos) ++n;
        return n;
    };
    host.session(RACE1, /*numLaps=*/1, /*lengthMs=*/180000);
    host.classify(RACE1, 25, {
        { .num = 10, .laps = 0, .gap = 0 },
        { .num = 22, .laps = 0, .gap = 2000 },
    });
    host.classify(RACE1, -5, {
        { .num = 10, .laps = 0, .gap = 0 },
        { .num = 22, .laps = 0, .gap = 2000 },
    });
    REQUIRE(host.finishLap() == 1);
    const int whiteFlags = finalLaps();
    host.raceLap(RACE1, 10, 1, 60039);     // in the gap, before the game's callback
    CHECK_MESSAGE(finalLaps() == whiteFlags, "the white flag fired a lap early");
    host.raceSessionState(RACE1, IN_PROGRESS, 180000);
    REQUIRE(host.finishLap() == 2);
    host.raceLap(RACE1, 10, 2, 45000);     // now the last lap starts
    CHECK(finalLaps() == whiteFlags + 1);

    host.shutdown();
}
