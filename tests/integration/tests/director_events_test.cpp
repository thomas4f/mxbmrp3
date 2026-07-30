// ============================================================================
// tests/integration/tests/director_events_test.cpp
// Director transparency: the auto-director surfaces each shot decision (and its
// state changes) as an Event Log entry so a broadcaster can see the director's
// logic in the feed. Drives a spectated race with the director enabled, then
// asserts the /api/state event log carries Director-typed entries. Also pins the
// raw-data contract: the plugin emits director events UNCONDITIONALLY (the in-game
// "Director" event toggle and the web overlay filter them at DISPLAY time), so the
// snapshot carries them even with the in-game display filter OFF. Uses the
// injectable director clock so cuts fire deterministically. Self-contained doctest.
// ============================================================================
#define DOCTEST_CONFIG_IMPLEMENT
#include "doctest.h"
#include "integration_main.h"
#include "plugin_host.h"
#include "assertions.h"

namespace {

// EventLogType::Director is the last (append-only) enum value; the HTTP snapshot
// emits the raw integer. Keep in step with core/event_log_types.h.
constexpr int DIRECTOR_EVENT_TYPE = 18;

// ColorSlot ordinals (keep in step with core/color_config.h). Director state-transition
// event-log entries are tinted with the director button's state colors; cuts keep the
// per-type default (override slot -1). MXBMRP3_Test_EventLogIconColorSlot returns these.
constexpr int SLOT_MUTED = 3;
constexpr int SLOT_POSITIVE = 5;
constexpr int SLOT_WARNING = 6;
constexpr int SLOT_NEUTRAL = 7;
constexpr int SLOT_DEFAULT = -1;   // no override -> per-type default color

int countDirectorEvents(const nlohmann::json& d) {
    int n = 0;
    for (const auto& e : d.value("events", nlohmann::json::array()))
        if (e.value("type", -1) == DIRECTOR_EVENT_TYPE) ++n;
    return n;
}

// Stand up a spectated 4-rider race with the director enabled, then settle it over
// a few evals so it acquires an opening shot (and, with battles on, a battle cut).
void driveSpectatedRace(PluginHost& host) {
    host.eventInit("TestTrack", "Cam");
    host.raceEvent("TestTrack");
    host.session(/*session=*/6, /*numLaps=*/10, /*lengthMs=*/0);
    for (int num : { 10, 22, 7, 3 }) {
        char nm[16]; snprintf(nm, sizeof(nm), "R%d", num);
        host.addEntry(num, nm);
    }
    host.draw();                       // state 1 = spectate, so the director directs
    host.directorSetEnabled(true);     // (logs "Auto-director enabled" iff the type is on)

    auto classify = [&]() {
        host.classify(6, 200000, {
            { .num = 10, .best = 90000, .laps = 3, .gap = 0 },
            { .num = 22, .best = 90500, .laps = 3, .gap = 1200 },  // <= 2500 -> battle
            { .num = 7,  .best = 91000, .laps = 3, .gap = 2600 },
            { .num = 3,  .best = 91500, .laps = 3, .gap = 5000 },
        });
    };
    auto positions = [&]() {
        host.raceTrackPosition({
            { .num = 10, .trackPos = 0.50f, .crashed = 0 },
            { .num = 22, .trackPos = 0.49f, .crashed = 0 },
            { .num = 7,  .trackPos = 0.40f, .crashed = 0 },
            { .num = 3,  .trackPos = 0.30f, .crashed = 0 },
        });
    };

    long long t = 1000;
    host.directorSetNowMs(t); classify(); positions();
    for (int i = 0; i < 4; ++i) { t += 500; host.directorSetNowMs(t); positions(); classify(); }
    host.directorToggleLock();          // logs "Locked on #N" (a subject is framed by now)
    host.directorSetNowMs(-1);          // restore the real clock
}

}  // namespace

TEST_CASE("director events: shot decisions and state changes reach the event log") {
    PluginHost host(dllPath());
    REQUIRE(host.loaded());
    host.startup("Z:\\tmp\\mxbmrp3-tests\\director_events\\");

    host.eventLogEnableDirector(true);  // show them in-game too (display filter)
    driveSpectatedRace(host);

    auto d = host.snapshot();
    REQUIRE(d.is_object());

    // Multiple director decisions reached the event log, tagged with the Director type:
    // enabling logs "Auto-director enabled", plus at least one cut and the lock.
    CHECK(countDirectorEvents(d) >= 2);
    CHECK(hasEvent(d, "Auto-director enabled"));
    // The opening acquire phrases as "Following #N"; a close-running pair as "Battle #N vs #M".
    CHECK((hasEvent(d, "Following") || hasEvent(d, "Battle")));
    CHECK(hasEvent(d, "Locked on"));

    host.shutdown();
}

TEST_CASE("director events: state-transition entries carry the director button's state colors") {
    PluginHost host(dllPath());
    REQUIRE(host.loaded());
    host.startup("Z:\\tmp\\mxbmrp3-tests\\director_events_color\\");

    host.eventLogEnableDirector(true);
    driveSpectatedRace(host);

    // State transitions are tinted to match director_widget.cpp stateColor(): auto-on is
    // POSITIVE (running), the rider lock is WARNING (matches the standings lock icon).
    CHECK(host.eventLogIconColorSlot("Auto-director enabled") == SLOT_POSITIVE);
    CHECK(host.eventLogIconColorSlot("Locked on") == SLOT_WARNING);

    // A cut is an ordinary log line: no override, so it uses the per-type default color.
    const char* cut = host.eventLogIconColorSlot("Following") != -2 ? "Following" : "Battle";
    CHECK(host.eventLogIconColorSlot(cut) == SLOT_DEFAULT);

    host.shutdown();
}

TEST_CASE("director events: disabling tints the entry with the off (muted) state color") {
    PluginHost host(dllPath());
    REQUIRE(host.loaded());
    host.startup("Z:\\tmp\\mxbmrp3-tests\\director_events_off_color\\");

    host.eventLogEnableDirector(true);
    driveSpectatedRace(host);
    host.directorSetEnabled(false);   // logs "Auto-director disabled"

    CHECK(host.eventLogIconColorSlot("Auto-director disabled") == SLOT_MUTED);

    host.shutdown();
}

TEST_CASE("director events: a gamepad takeover tints its entry yellow (Manual state)") {
    PluginHost host(dllPath());
    REQUIRE(host.loaded());
    host.startup("Z:\\tmp\\mxbmrp3-tests\\director_events_takeover\\");
    host.eventLogEnableDirector(true);

    // Spectated race with the director enabled and a subject acquired.
    host.eventInit("TestTrack", "Cam");
    host.raceEvent("TestTrack");
    host.session(/*session=*/6, /*numLaps=*/10, /*lengthMs=*/0);
    for (int num : { 10, 22, 7, 3 }) {
        char nm[16]; snprintf(nm, sizeof(nm), "R%d", num);
        host.addEntry(num, nm);
    }
    host.draw();
    host.directorSetEnabled(true);

    auto positions = [&]() {
        host.raceTrackPosition({
            { .num = 10, .trackPos = 0.50f, .crashed = 0 },
            { .num = 22, .trackPos = 0.49f, .crashed = 0 },
            { .num = 7,  .trackPos = 0.40f, .crashed = 0 },
            { .num = 3,  .trackPos = 0.30f, .crashed = 0 },
        });
    };
    long long t = 1000;
    host.directorSetNowMs(t); positions();
    for (int i = 0; i < 4; ++i) { t += 500; host.directorSetNowMs(t); positions(); host.draw(); }

    // Push the stick (forced controller) and pump frames past the ~30Hz manual-poll
    // throttle so pollManualControl() trips the takeover.
    host.fakeGamepad(true);
    for (int i = 0; i < 6; ++i) { t += 100; host.directorSetNowMs(t); host.draw(); }

    auto d = host.snapshot();
    REQUIRE(d.is_object());
    REQUIRE(hasEvent(d, "Manual control (gamepad)"));
    // The gamepad-takeover entry is the Manual state -> NEUTRAL (yellow), matching the
    // director button. (Regression: this call site was previously left uncolored.)
    CHECK(host.eventLogIconColorSlot("Manual control (gamepad)") == SLOT_NEUTRAL);

    host.directorSetNowMs(-1);
    host.shutdown();
}

TEST_CASE("director events: emitted regardless of the display filter (raw-data contract)") {
    PluginHost host(dllPath());
    REQUIRE(host.loaded());
    host.startup("Z:\\tmp\\mxbmrp3-tests\\director_events_off\\");

    // Display filter left OFF (the default). Like every other event producer, the plugin
    // still emits the raw entries — the filter applies only at display time — so the
    // snapshot (which sends all events for the web overlay to filter) still carries them.
    host.eventLogEnableDirector(false);
    driveSpectatedRace(host);

    auto d = host.snapshot();
    REQUIRE(d.is_object());
    CHECK(countDirectorEvents(d) >= 1);

    host.shutdown();
}

// Only ONE rider is the race winner.
//
// SHOT_FINISH is not "the win" — it is the whole run-in to the flag. Once the leader
// crosses, the finish lock moves down the order to whoever is still racing to the
// line, and every one of those cuts is tagged "finish" too. The log line was
// unconditionally "Race winner #N", so P2, P3, P4 each got announced as the winner
// of the same race. Reported from real use.
//
// The overlay already got this right (overlay-focus.js reads FINISHED vs FINISHING
// off rider.finished); it was only the event log that had no split.
TEST_CASE("director events: only the actual winner is logged as the race winner") {
    PluginHost host(dllPath());
    REQUIRE(host.loaded());
    host.startup("Z:\\tmp\\mxbmrp3-tests\\director_winner\\");
    host.eventLogEnableDirector(true);

    // Pure-lap race: a rider is finished once numLaps >= sessionNumLaps, so the
    // classification's lap counts alone drive the finish (no finishLap/overtime).
    host.eventInit("TestTrack", "Cam");
    host.raceEvent("TestTrack");
    host.session(/*session=*/6, /*numLaps=*/10, /*lengthMs=*/0);
    for (int num : { 10, 22, 7, 3 }) {
        char nm[16]; snprintf(nm, sizeof(nm), "R%d", num);
        host.addEntry(num, nm);
    }
    host.draw();                     // spectate, so the director directs
    host.directorSetEnabled(true);   // finish lock is on by default

    auto positions = [&]() {
        host.raceTrackPosition({
            { .num = 10, .trackPos = 0.50f, .crashed = 0 },
            { .num = 22, .trackPos = 0.40f, .crashed = 0 },
            { .num = 7,  .trackPos = 0.30f, .crashed = 0 },
            { .num = 3,  .trackPos = 0.20f, .crashed = 0 },
        });
    };
    // #10 has taken the flag (10 of 10); the rest are on their last lap. Gaps are wide
    // enough that the front pair is not a battle, so the finish lock frames one rider.
    auto classifyLeaderHome = [&]() {
        host.classify(6, 600000, {
            { .num = 10, .best = 90000, .laps = 10, .gap = 0 },
            { .num = 22, .best = 90500, .laps = 9,  .gap = 30000 },
            { .num = 7,  .best = 91000, .laps = 9,  .gap = 60000 },
            { .num = 3,  .best = 91500, .laps = 9,  .gap = 90000 },
        });
    };

    long long t = 1000;
    host.directorSetNowMs(t); positions(); classifyLeaderHome();
    for (int i = 0; i < 4; ++i) { t += 500; host.directorSetNowMs(t); positions(); classifyLeaderHome(); }

    // Positive control: without a winner line the rest of this case proves nothing —
    // "no rider was called the winner" passes trivially if the finish window never
    // opened at all.
    auto mid = host.snapshot();
    REQUIRE(mid.is_object());
    REQUIRE_MESSAGE(hasEvent(mid, "Race winner #10"),
                    "the finish lock never framed the winner, so this case cannot "
                    "discriminate (check the pure-lap finish detection)");

    // Past the 6s winner celebration: the lock moves to the front-most rider STILL
    // racing to the flag. That cut is SHOT_FINISH as well — and is where the wrong
    // label used to appear.
    for (int i = 0; i < 8; ++i) { t += 1500; host.directorSetNowMs(t); positions(); classifyLeaderHome(); }

    auto d = host.snapshot();
    REQUIRE(d.is_object());

    // Exactly one rider is announced as the winner, and it is the one who took the flag.
    int winnerLines = 0;
    for (const auto& e : d.value("events", nlohmann::json::array()))
        if (e.value("message", std::string()).rfind("Race winner", 0) == 0) ++winnerLines;
    CHECK_MESSAGE(winnerLines == 1,
                  "more than one rider was logged as the race winner — the run-in cuts "
                  "after the celebration are still borrowing the winner's line");
    CHECK(hasEvent(d, "Race winner #10"));
    CHECK_FALSE(hasEvent(d, "Race winner #22"));
    CHECK_FALSE(hasEvent(d, "Race winner #7"));
    CHECK_FALSE(hasEvent(d, "Race winner #3"));

    // And the run-in cuts still say something: a rider heading for the flag reads
    // "Finishing", matching the overlay's FINISHING caption.
    CHECK(hasEvent(d, "Finishing #22"));

    host.directorSetNowMs(-1);
    host.shutdown();
}
