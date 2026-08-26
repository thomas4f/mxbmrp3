// ============================================================================
// tests/integration/tests/spotter_test.cpp
// Pins the spotter cue pipeline end to end through the REAL callback path:
// race events -> PluginData::addEventLogEntry -> SpotterManager (enabled +
// category gate) -> composed phrase in the cue log. Audio playback is NOT
// asserted (a Wine prefix has no SAPI voice; the worker logs and drops) —
// the cue log records the DECISION, which is the testable part, and is the
// same text the subtitle widget shows.
//
// What a plausible edit breaks silently, and is pinned here:
//  - the tap living in addEventLogEntry (moving detection elsewhere loses
//    events the log still shows — subtitle and event log would disagree);
//  - "you" phrasing following the SPECTATED rider, not the player, while
//    spectating (what makes tape replays and broadcasts speak sensibly);
//  - category toggles filtering their events without muting the rest;
//  - disabled spotter costing nothing and logging nothing.
// ============================================================================
#define DOCTEST_CONFIG_IMPLEMENT
#include "doctest.h"
#include "integration_main.h"
#include "plugin_host.h"
#include <algorithm>
#include <set>
#include <sstream>
#include <string>
#include <vector>

static constexpr int RACE1 = 6;  // PiBoSo session 6 = Race1; state 16 = running

TEST_CASE("spotter: events become phrased cues under enable/category gates") {
    PluginHost host(dllPath());
    REQUIRE(host.loaded());
    host.startup("Z:\\tmp\\mxbmrp3-tests\\spotter\\");
    // Load the SHIPPED pack, which is where the spotter's words live — the
    // plugin has no built-in wording behind it, so an empty pack would make
    // every cue below silent. Installed explicitly rather than left to the
    // load path: the pack root resolves against the process working
    // directory, which the harness does not stage.
    host.spotterInstallShippedPack();

    host.eventInit("TestTrack", "Player");
    host.raceEvent("TestTrack");
    host.addEntry(12, "Player");
    host.addEntry(476, "Rival");

    SUBCASE("disabled spotter records nothing") {
        host.session(RACE1, 10);
        host.raceLap(RACE1, 476, 1, 108231, /*best=*/2);
        CHECK(host.spotterCueLog() == "");
    }

    SUBCASE("subtitles-only mode: cues are logged with spoken audio off") {
        // The settings tab promises 'Subtitles work without it' about the
        // spoken-audio master — this is the mode that promise describes.
        // Intake (and therefore the subtitle widget's cue log) must run on
        // the subtitles switch alone; only audio dispatch needs m_enabled.
        host.spotterSubtitles(true);   // spoken audio stays off
        host.session(RACE1, 10);
        host.raceLap(RACE1, 476, 1, 108231, /*best=*/2);
        // The fastest lap is held until the clock moves on (see
        // PendingFastest); a classification is what moves it, and the game
        // sends one about every second.
        host.classify(RACE1, 479000, { { .num = 476, .laps = 1 },
                                       { .num = 12, .laps = 0 } });
        const std::string log = host.spotterCueLog();
        CHECK(log.find("Race 1 underway") != std::string::npos);
        CHECK(log.find("Fastest lap, rider four seventy six") !=
              std::string::npos);
    }

    SUBCASE("session start and fastest laps, phrased by focus") {
        host.spotterEnable(true);
        host.spotterCategoryMask(0x1F);

        host.session(RACE1, 10);
        std::string log = host.spotterCueLog();
        CHECK(log.find("Race 1 underway") != std::string::npos);

        // Rival's fastest lap: third-person, racing-style number, tenths.
        host.raceLap(RACE1, 476, 1, 108231, /*best=*/2);
        host.classify(RACE1, 479000, { { .num = 476, .laps = 1 },
                                       { .num = 12, .laps = 0 } });
        log = host.spotterCueLog();
        CHECK(log.find("Fastest lap, rider four seventy six, one forty eight"
                       " point two.") != std::string::npos);

        // ONLINE, because that is where "fastest lap" is the right words for
        // your own session-leading lap; offline the ladder speaks it as a
        // session best instead (see eventInit's serverType).
        host.eventInit("TestTrack", "Player", 1600.0f, 2, "Test 450", "MX1",
                       "", /*serverType=*/1);

        // Bank a quicker all-time best first, so the lap below is the session's
        // fastest WITHOUT being a personal best — which is what the game sends
        // whenever your standard is set and the session is merely going well.
        // Without it the lap is a first-ever PB, the lap-quality ladder speaks
        // that instead, and this stops testing the phrasing it is here for.
        // (Deterministic either way: if an earlier case already banked a faster
        // time on this track, this is a no-op and the lap is still not a PB.)
        host.raceLap(RACE1, 12, 1, 100000, /*best=*/1);

        // Player takes it back: "you" phrasing (the harness player is #12).
        host.raceLap(RACE1, 12, 2, 107904, /*best=*/2);
        host.classify(RACE1, 478000, { { .num = 12, .laps = 2 },
                                       { .num = 476, .laps = 1 } });
        log = host.spotterCueLog();
        CHECK(log.find("Fastest lap, nice work, one forty seven point nine")
              != std::string::npos);
    }

    // A split IS a timing point, so the gap to the rider ahead is measurable
    // there exactly as it is at the line — three or four times a lap instead
    // of once. Same call, same per-rider trend memory.
    SUBCASE("the ahead gap is measured at splits, not only at the line") {
        host.spotterEnable(true);
        host.spotterCategoryMask(0x1F);
        host.spotterInstallPack(
            "[Cues]\nsector_completed = Sector {sector_number}"
            "[, {gap_to_ahead} to {rider_ahead}].\n");
        host.session(RACE1, /*numLaps=*/5, /*lengthMs=*/480000);
        // The gap is the difference in SESSION-elapsed time between the two
        // crossings, not in the split times the game reports — so the clock
        // has to move between them, which is what a classification carries.
        host.classify(RACE1, 480000, {          // elapsed 0
            { .num = 476, .laps = 0 }, { .num = 12, .laps = 0 },
        });
        host.raceSplit(RACE1, 476, 0, 0, 30000);   // they cross split 1
        host.classify(RACE1, 478500, {          // elapsed 1500
            { .num = 476, .laps = 0 }, { .num = 12, .laps = 0 },
        });
        host.raceSplit(RACE1, 12, 0, 0, 31500);    // you cross it 1.5s later
        CHECK(host.spotterCueLog().find(
                  "Sector one, one point five to rider four seventy six.") !=
              std::string::npos);
    }

    // The two cues that mark a MOMENT at a split rather than describing every
    // one of them. Both were asked for by name: "a great sector inside a bad
    // lap is worth knowing about", and "tell me at the last split that I'm on
    // for a record".
    //
    // WHY THEY NEED THEIR OWN CASE. Neither can be derived from
    // sector_completed, which compares ACCUMULATED time against your best lap
    // — down on the lap but up on this sector reads as "slower" there, and
    // that is the case both of these exist for.
    SUBCASE("a split can beat one sector's best, and can be on for a lap best") {
        host.spotterEnable(true);
        host.spotterCategoryMask(0x1F);
        const int PRACTICE = 1;
        // Only the keys under test, so a match cannot come from a shipped
        // sibling and the numbers are readable in the assertion.
        host.spotterInstallPack(
            "[Cues]\n"
            "sector_best = BEST {sector_number} off {sector_best_delta}\n"
            "on_pace_session_best = ONPACE up {pace_margin}\n");
        host.session(PRACTICE, /*numLaps=*/0, /*lengthMs=*/600000);
        // Lap one sets the references: three even 30s sectors. Splits are
        // CUMULATIVE, which is what the game sends and what the manager has to
        // difference back into sectors.
        // bestFlag=1, because a first clean lap IS your best so far -- and it is
        // bestFlag that makes the handler store the lap as the best-lap entry,
        // which is the reference the on-pace call is measured against.
        host.raceLap(PRACTICE, 12, 1, 90000, /*best=*/1,
                     /*split0=*/30000, /*split1=*/60000);

        // Nothing has been called yet — a split before any completed lap has
        // no best to beat, and this is the floor that keeps lap one from
        // announcing every sector as a personal best.
        CHECK(host.spotterCueLog().find("BEST") == std::string::npos);

        // Lap two, sector 1 in 29.0 against a 30.0 best.
        host.raceSplit(PRACTICE, 12, 1, 0, 29000);
        std::string log = host.spotterCueLog();
        // The sector NUMBER is a spoken word, like every number the spotter says.
        CHECK(log.find("BEST one off one point zero") != std::string::npos);
        // ...and NOT the on-pace call: sector 1 is not the last split. In MX
        // Bikes that is index 1 of three sectors, in GP Bikes index 2 of four.
        CHECK(log.find("ONPACE") == std::string::npos);

        // Sector 2 in 29.0 as well: a second sector best, AND the last split
        // before the line, 2.0 up on the best lap's 60.0 at this point.
        host.raceSplit(PRACTICE, 12, 1, 1, 58000);
        log = host.spotterCueLog();
        CHECK(log.find("BEST two off one point zero") != std::string::npos);
        CHECK(log.find("ONPACE up two point zero") != std::string::npos);
    }

    // The margin is what keeps the on-pace call from firing on most laps of any
    // decent run: a few hundredths up with a sector still to go is decided by
    // that sector, not by the split. [Spotter] on_pace_margin_ms, 200ms shipped.
    SUBCASE("on-pace stays quiet inside the margin") {
        host.spotterEnable(true);
        host.spotterCategoryMask(0x1F);
        const int PRACTICE = 1;
        host.spotterInstallPack(
            "[Cues]\non_pace_session_best = ONPACE up {pace_margin}\n");
        host.session(PRACTICE, /*numLaps=*/0, /*lengthMs=*/600000);
        // bestFlag=1, because a first clean lap IS your best so far -- and it is
        // bestFlag that makes the handler store the lap as the best-lap entry,
        // which is the reference the on-pace call is measured against.
        host.raceLap(PRACTICE, 12, 1, 90000, /*best=*/1,
                     /*split0=*/30000, /*split1=*/60000);

        // Both splits of each lap, in order: the split cues run off the SECTOR,
        // which is this split's cumulative time minus the previous one's, so a
        // last split arriving without its predecessor is skipped rather than
        // reported off a cumulative figure.
        //
        // 0.1s up at the last split — inside the 0.2s floor, so silent.
        host.raceSplit(PRACTICE, 12, 1, 0, 30000);
        host.raceSplit(PRACTICE, 12, 1, 1, 59900);
        CHECK(host.spotterCueLog().find("ONPACE") == std::string::npos);

        // 0.3s up on the next lap clears it.
        host.raceSplit(PRACTICE, 12, 2, 0, 30000);
        host.raceSplit(PRACTICE, 12, 2, 1, 59700);
        CHECK(host.spotterCueLog().find("ONPACE up zero point three") !=
              std::string::npos);
    }

    // ONE lap, ONE telling of its time. From a real log: crossing the line on a
    // session best said "Session best, forty point seven." then "That's the
    // fastest of the session, forty point seven." then "P one, forty point
    // seven." -- the same number three times in one crossing.
    //
    // Two independent causes, both pinned here. The ladder in
    // race_lap_handler picks between the on-SCREEN notices only; the spotter's
    // fastest lap arrives separately through the event log, which logs it
    // unconditionally. And the lap report names {last_lap_time} in its own
    // right, knowing nothing about what just spoke.
    SUBCASE("a best lap speaks its time once, not three times") {
        host.spotterEnable(true);
        host.spotterCategoryMask(0x1F);
        const int PRACTICE = 1;
        // All three cues that can speak a lap's time at one crossing. Which
        // QUALITY cue fires depends on what StatsManager has banked -- it is a
        // singleton outliving PluginHost, so an 85s lap is an all-time PB or
        // merely a session best depending on what ran first. That is exactly
        // why the assertion below counts the time rather than naming a cue.
        host.spotterInstallPack(
            "[Cues]\n"
            "personal_best = PB {event_time}\n"
            "session_best = SESSIONBEST {event_time}\n"
            "fastest_lap_you = FASTEST {event_time}\n"
            "lap_completed = LAP P {position}[, {last_lap_time}]\n");
        host.session(PRACTICE, /*numLaps=*/0, /*lengthMs=*/600000);
        host.classify(PRACTICE, 0, { { .num = 12, .laps = 0 } });

        // A first clean lap, then a quicker one: the second is a session best
        // AND the session's fastest lap.
        host.raceLap(PRACTICE, 12, 1, 90000, /*best=*/1);
        host.classify(PRACTICE, 90000, { { .num = 12, .best = 90000, .laps = 1 } });
        host.raceLap(PRACTICE, 12, 2, 85000, /*best=*/1);
        host.classify(PRACTICE, 175000, { { .num = 12, .best = 85000, .laps = 2 } });

        const std::string log = host.spotterCueLog();

        // ONCE. Not "by this cue" -- the invariant is that one lap's time is
        // read out one time, whichever rung of the ladder claims it.
        const std::string t = "one twenty five point zero";
        int spoken = 0;
        for (size_t at = log.find(t); at != std::string::npos;
             at = log.find(t, at + t.size())) ++spoken;
        CHECK(spoken == 1);

        // The fastest-lap rung is the one that used to double up.
        CHECK(log.find("FASTEST") == std::string::npos);
        // The report still names the position -- only the redundant time went.
        CHECK(log.find("LAP P one") != std::string::npos);
        CHECK(log.find("LAP P one, " + t) == std::string::npos);
    }

    // Outside a race the classification's gap is not a rider-to-rider figure
    // at all. A real logged warmup had it NEGATIVE at the start (-61925), then
    // frozen at the same value across six crossings spanning two and a half
    // minutes, while gapLaps stayed 0 for a rider who was two laps up — so the
    // difference of two of them was spoken as "P six, zero point one to rider
    // two forty seven" between riders two laps apart.
    // A gap in SECONDS is only ever the stopwatch — their crossing of a timing
    // point against yours. Subtracting two riders' official gaps to the leader
    // used to stand in when no crossing had been measured, and it is a
    // different measurement wearing the same words: each column is refreshed
    // at that rider's OWN crossing, so the difference means something only
    // while both are fresh and on the same lap, which nothing can check. This
    // is the shape a real warmup had — figures a couple of seconds apart for
    // riders two laps apart, spoken as "zero point one to rider two forty
    // seven".
    SUBCASE("a gap in seconds is a measurement or it is nothing") {
        host.spotterEnable(true);
        host.spotterCategoryMask(0x1F);
        host.spotterInstallPack(
            "[Cues]\nlap_completed = P {position}"
            "[, {gap_to_ahead} to {rider_ahead}]"
            "[, {gap_to_leader} off the lead].\n");
        const int PRACTICE = 1;
        host.session(PRACTICE, 0);
        host.classify(PRACTICE, 0, {
            { .num = 476, .best = 0, .laps = 3, .gap = 2259 },
            { .num = 12,  .best = 0, .laps = 1, .gap = 2407 },
        });
        host.raceLap(PRACTICE, 12, 1, 95000);
        host.classify(PRACTICE, 95000, {
            { .num = 476, .best = 0, .laps = 3, .gap = 2259 },
            { .num = 12,  .best = 0, .laps = 1, .gap = 2407 },
        });
        std::string log = host.spotterCueLog();
        // The position is real and still spoken; the gap was never measured,
        // so the whole optional group drops rather than quoting a difference.
        CHECK(log.find("P two.") != std::string::npos);
        CHECK(log.find("to rider four seventy six") == std::string::npos);
        // {gap_to_leader} is that same column read one rider at a time, and it
        // is race-gated: a logged warmup signed off with "you finished P
        // eleven, sixty six point four off the lead" — the fastest lap of that
        // warmup, handed back as a gap because the player had set none.
        CHECK(log.find("off the lead") == std::string::npos);

        // A RACE does not make the subtraction true either — same table, same
        // silence. What a race adds is {gap_to_leader}, which is one rider's
        // own column rather than a difference.
        host.session(RACE1, 5);
        host.classify(RACE1, 0, {
            { .num = 476, .best = 0, .laps = 1, .gap = 2259 },
            { .num = 12,  .best = 0, .laps = 1, .gap = 2407 },
        });
        host.raceLap(RACE1, 12, 2, 95000);
        host.classify(RACE1, 190000, {
            { .num = 476, .best = 0, .laps = 2, .gap = 2259 },
            { .num = 12,  .best = 0, .laps = 2, .gap = 2407 },
        });
        log = host.spotterCueLog();
        CHECK(log.find("to rider four seventy six") == std::string::npos);
        CHECK(log.find("two point four off the lead") != std::string::npos);

        // Being a LAP down is a different question, answered directly by the
        // standings rather than by subtraction — so it still speaks, and only
        // in a race.
        host.classify(RACE1, 200000, {
            { .num = 476, .best = 0, .laps = 3, .gap = 0 },
            { .num = 12,  .best = 0, .laps = 2, .gap = 2407, .gapLaps = 1 },
        });
        host.raceLap(RACE1, 12, 3, 95000);
        host.classify(RACE1, 295000, {
            { .num = 476, .best = 0, .laps = 3, .gap = 0 },
            { .num = 12,  .best = 0, .laps = 3, .gap = 2407, .gapLaps = 1 },
        });
        CHECK(host.spotterCueLog().find("one lap to rider four seventy six") !=
              std::string::npos);
    }

    // In a RACE an invalid lap also brings a penalty, so you hear about it.
    // Practice and qualifying issue no penalties at all — the lap is struck
    // out silently and you find out by looking at the timing screen. The
    // game's only signal is the invalid flag on the crossing itself (the
    // cutting flag rides RaceCommunication, which practice never sends), so
    // this fires at the line and not a moment sooner.
    SUBCASE("an invalidated lap says so, in the session that never penalises") {
        host.spotterEnable(true);
        host.spotterCategoryMask(0x1F);
        const int PRACTICE = 1;
        host.session(PRACTICE, 0);
        host.classify(PRACTICE, 0, { { .num = 12, .laps = 0 } });

        // A clean lap says nothing about validity.
        host.raceLap(PRACTICE, 12, 1, 95000);
        host.classify(PRACTICE, 95000, { { .num = 12, .laps = 1 } });
        CHECK(host.spotterCueLog().find("didn't count") == std::string::npos);

        // The next one is struck out.
        host.raceLap(PRACTICE, 12, 2, 93000, /*best=*/0, /*split0=*/-1,
                     /*split1=*/-1, /*invalid=*/true);
        host.classify(PRACTICE, 188000, { { .num = 12, .laps = 2 } });
        CHECK(host.spotterCueLog().find("That lap didn't count.") !=
              std::string::npos);
    }

    // The white flag is the LEADER's moment; yours is a different one, and in
    // a spread-out field lands a lap or more later. Both used to collapse onto
    // final_lap, which meant a backmarker heard "Final lap" while still having
    // two to run and nothing at all when their own last lap began.
    SUBCASE("your final lap is its own cue, separate from the leader's") {
        host.spotterEnable(true);
        host.spotterCategoryMask(0x1F);
        host.session(RACE1, /*numLaps=*/3, /*lengthMs=*/0);
        host.classify(RACE1, 0, {
            { .num = 476, .laps = 0 }, { .num = 12, .laps = 0 },
        });

        // The leader starts their last lap: the white flag, about the race —
        // still its own EVENT (the race feed logs it), but DEFAULT-QUIET in
        // the shipped pack. Down the order you would hear it and then your own
        // last lap a lap later; on the lead lap the two land seconds apart.
        // One actionable moment, one callout: final_lap_you.
        host.raceLap(RACE1, 476, 2, 90000);
        host.classify(RACE1, 180000, {
            { .num = 476, .laps = 2 }, { .num = 12, .laps = 1 },
        });
        std::string log = host.spotterCueLog();
        CHECK(log.find("Final lap.") == std::string::npos);
        CHECK(log.find("Last lap, make it count.") == std::string::npos);
        // ...and a pack that DOES want it gets it: the event still reaches the
        // spotter, so writing the row is all it takes.
        host.spotterInstallPackOver("[Cues]\nfinal_lap = Leader's last lap.\n");
        host.raceLap(RACE1, 476, 2, 90000);
        host.classify(RACE1, 181000, {
            { .num = 476, .laps = 2 }, { .num = 12, .laps = 1 },
        });
        CHECK(host.spotterCueLog().find("Leader's last lap.") !=
              std::string::npos);
        host.spotterInstallShippedPack();

        // A lap later YOU start yours — the one that decides how you ride it.
        host.raceLap(RACE1, 12, 2, 95000);
        host.classify(RACE1, 275000, {
            { .num = 476, .laps = 3 }, { .num = 12, .laps = 2 },
        });
        CHECK(host.spotterCueLog().find("Last lap, make it count.") !=
              std::string::npos);
    }

    // ...and when you ARE the leader the two coincide, so you get the one that
    // is about you. The branches used to test position first and you second,
    // which meant a player leading the race only ever heard the session-level
    // cue — and a pack that muted final_lap left the leader silent on their
    // own last lap, the one case where it matters most.
    SUBCASE("leading the race, your final lap is still YOUR cue") {
        host.spotterEnable(true);
        host.spotterCategoryMask(0x1F);
        host.session(RACE1, /*numLaps=*/3, /*lengthMs=*/0);
        host.classify(RACE1, 0, {
            { .num = 12, .laps = 0 }, { .num = 476, .laps = 0 },
        });
        // You lead, and start the last lap.
        host.raceLap(RACE1, 12, 2, 90000);
        host.classify(RACE1, 180000, {
            { .num = 12, .laps = 2 }, { .num = 476, .laps = 1 },
        });
        const std::string log = host.spotterCueLog();
        CHECK(log.find("Last lap, make it count.") != std::string::npos);
        // Not both: the same fact twice in one breath is the redundancy the
        // split was supposed to remove.
        CHECK(log.find("Final lap.") == std::string::npos);
    }

    // Your opening lap carries the gate hold and a standing start, so it is a
    // real lap time and a real personal best but not something to measure a
    // flying lap against. Farm 14's opener was 3:05.1 into a 1:26 field, and
    // three rivals' laps came back as "ninety six point six quicker than your
    // best", "ninety seven point five quicker", "ninety eight point six
    // quicker" — arithmetic that is true and tells you nothing.
    SUBCASE("a rival's lap is not measured against your gate-start lap") {
        host.spotterEnable(true);
        host.spotterCategoryMask(0x1F);
        host.spotterInstallPack(
            "[Cues]\n"
            "fastest_lap_other = Fastest lap, {event_rider}, {event_time}"
            "[, {event_gap_to_best_lap} than your best].\n");
        host.session(RACE1, 10);

        // Your opening lap: slow, and for now your best.
        host.raceLap(RACE1, 12, 1, 185100, /*best=*/1);
        host.classify(RACE1, 185100, {
            { .num = 12, .best = 185100, .laps = 1, .bestLapNum = 1 },
            { .num = 476, .laps = 1 },
        });
        // A rival's flying lap. The delta is real — and is not spoken.
        host.raceLap(RACE1, 476, 1, 88400, /*best=*/2);
        host.classify(RACE1, 185000, {
            { .num = 12, .best = 185100, .laps = 1, .bestLapNum = 1 },
            { .num = 476, .laps = 1 },
        });
        std::string log = host.spotterCueLog();
        CHECK(log.find("Fastest lap, rider four seventy six, one twenty eight"
                       " point four.") != std::string::npos);
        CHECK(log.find("than your best") == std::string::npos);

        // Now a repeatable lap of your own replaces it, and the comparison
        // comes back — this is the half that stops the fix being "never
        // compare", which would have passed the assertion above for free.
        host.raceLap(RACE1, 12, 2, 93900, /*best=*/1);
        host.classify(RACE1, 279000, {
            { .num = 12, .best = 93900, .laps = 2, .bestLapNum = 2 },
            { .num = 476, .laps = 2 },
        });
        host.raceLap(RACE1, 476, 2, 87000, /*best=*/2);
        host.classify(RACE1, 278000, {
            { .num = 12, .best = 93900, .laps = 2, .bestLapNum = 2 },
            { .num = 476, .laps = 2 },
        });
        log = host.spotterCueLog();
        CHECK(log.find("six point nine quicker than your best") !=
              std::string::npos);
    }

    // One lap, one moment. The on-screen notices have had a ladder since long
    // before the spotter existed — "All-time PB supersedes fastest lap and
    // session PB" (race_lap_handler.cpp) — but the spotter reaches its own
    // fastest lap through the EVENT LOG, which logs it unconditionally because
    // the race feed wants it there. So a lap that was both spoke twice, back to
    // back, reading out the same time: "New personal best, one oh six point
    // six." / "Fastest lap, nice work, one oh six point six." Both the mxbclub
    // and demo-weekend tapes do it.
    SUBCASE("a lap that is both a PB and the fastest is one cue, not two") {
        host.spotterEnable(true);
        host.spotterCategoryMask(0x1F);
        // ONLINE: this case is about your own FASTEST LAP wording, which is
        // what an online session-leading lap says. See eventInit's serverType.
        host.eventInit("TestTrack", "Player", 1600.0f, 2, "Test 450", "MX1",
                       "", /*serverType=*/1);
        host.session(RACE1, 10);

        // Twenty-second laps: StatsManager is a singleton that outlives a
        // PluginHost and every other case here runs 90s+, so these beat
        // whatever is banked no matter what ran first. Anything slower would
        // make "is this an all-time PB" depend on test ORDER.
        host.raceLap(RACE1, 12, 1, 21000, /*best=*/1);
        REQUIRE(host.spotterCueLog().find("New personal best") !=
                std::string::npos);
        const std::string banked = host.spotterCueLog();

        // Both at once: quicker than the 21.0 just banked AND the session's
        // outright fastest. The higher rung speaks, and only the higher rung.
        host.raceLap(RACE1, 12, 2, 20000, /*best=*/2);
        std::string log = host.spotterCueLog();
        CHECK(log.find("Fastest lap, nice work, twenty point zero.") ==
              std::string::npos);
        size_t pb = banked.size();   // only what lap 2 added
        CHECK(log.find("New personal best", pb) != std::string::npos);

        // And the ladder suppresses only when a higher rung actually fired: a
        // lap that leads the session without beating the 20.0 is a plain
        // fastest lap and still speaks. This is what the game sends once your
        // standard is set and the session is merely going well.
        host.raceLap(RACE1, 12, 3, 20500, /*best=*/2);
        host.classify(RACE1, 400000, { { .num = 12, .laps = 3 } });
        log = host.spotterCueLog();
        CHECK(log.find("Fastest lap, nice work, twenty point five.") !=
              std::string::npos);
    }

    SUBCASE("proximity: rider behind then clear, crashed riders excluded") {
        host.spotterEnable(true);
        host.spotterCategoryMask(0x1F);
        host.session(RACE1, 10);
        host.classify(RACE1, 60000, {
            { .num = 12, .laps = 2 },
            { .num = 476, .laps = 2 },
        });

        // Default trackLength 1600m: 0.006 of a lap = 9.6m — inside the 12m
        // behind-on threshold. But CRASHED close behind is not a rival.
        host.raceTrackPosition({
            { .num = 12, .trackPos = 0.500f },
            { .num = 476, .trackPos = 0.494f, .crashed = 1 },
        });
        std::string log = host.spotterCueLog();
        CHECK(log.find("Rider behind.") == std::string::npos);

        // Upright and close: announce. The override pins the wording of BOTH
        // cues to something this case owns, so neither assertion below depends
        // on what the shipped pack happens to say today — it ships alternates
        // for both of these, and "Clear." is one substring of several.
        // (spotterInstallPackOver pins the base row; the roll has its own case.)
        host.spotterInstallPackOver(
            "[Cues]\n"
            "rider_behind = Rider behind.\n"
            "rider_behind_clear = Clear.\n");
        host.raceTrackPosition({
            { .num = 12, .trackPos = 0.500f },
            { .num = 476, .trackPos = 0.494f },
        });
        log = host.spotterCueLog();
        CHECK(log.find("Rider behind.") != std::string::npos);
        CHECK(log.find("Clear.") == std::string::npos);

        // Still close (hold band): quiet. Then, once the episode has LASTED
        // past the clear-min gate (classify time is REMAINING, so advancing
        // the clock means less of it), dropped back 32m: "clear".
        host.raceTrackPosition({
            { .num = 12, .trackPos = 0.500f },
            { .num = 476, .trackPos = 0.490f },
        });
        host.classify(RACE1, 54000, {
            { .num = 12, .laps = 2 },
            { .num = 476, .laps = 2 },
        });
        host.raceTrackPosition({
            { .num = 12, .trackPos = 0.500f },
            { .num = 476, .trackPos = 0.480f },
        });
        log = host.spotterCueLog();
        CHECK(log.find("Clear.") != std::string::npos);
    }

    SUBCASE("muting Opponents mid-track suppresses the synthetic clear") {
        // A rival is tracked behind, then the user disables the Opponents
        // category: the next batch's skipped proximity scan reads as a
        // release, and that synthetic 'Clear.' must not escape a category
        // the user just muted.
        host.spotterEnable(true);
        host.spotterCategoryMask(0x1F);
        host.session(RACE1, 10);
        host.classify(RACE1, 60000, {
            { .num = 12, .laps = 2 },
            { .num = 476, .laps = 2 },
        });
        host.raceTrackPosition({
            { .num = 12, .trackPos = 0.500f },
            { .num = 476, .trackPos = 0.494f },
        });
        CHECK(host.spotterCueLog().find("Rider behind.") != std::string::npos);

        host.spotterCategoryMask(
            0x1F & ~(1u << 3));  // Proximity off (SpotterPhrase::Category)
        host.raceTrackPosition({
            { .num = 12, .trackPos = 0.500f },
            { .num = 476, .trackPos = 0.494f },
        });
        CHECK(host.spotterCueLog().find("Clear.") == std::string::npos);
    }

    SUBCASE("blue flag: a closing lapper announces once, held state is quiet") {
        host.spotterEnable(true);
        host.spotterCategoryMask(0x1F);
        host.session(RACE1, 10);
        // blueflag_test geometry: the player (#12) a lap down at 0.53, the
        // lapper (#476) closing at 0.50 — within the 100m awareness reach.
        host.classify(RACE1, 120000, {
            { .num = 476, .laps = 3 },
            { .num = 12, .laps = 1 },
        });
        host.raceTrackPosition({
            { .num = 476, .trackPos = 0.50f },
            { .num = 12, .trackPos = 0.53f },
        });
        std::string log = host.spotterCueLog();
        auto countOf = [](const std::string& s, const char* sub) {
            size_t n = 0, p = 0;
            const std::string needle(sub);
            while ((p = s.find(needle, p)) != std::string::npos) {
                ++n;
                p += needle.size();
            }
            return n;
        };
        // NAMED: the shipped line asks for {event_rider}, and for this cue that
        // is whoever is closing to lap you (PluginData::getRiderLappedBy) —
        // the actionable half of a blue flag, since it tells you which bike to
        // expect. Asserting the number rather than just the wording is what
        // stops the lookup silently returning -1 and the group dropping.
        const char* kBlue = "Blue flag, faster rider closing, rider four seventy six.";
        CHECK(countOf(log, kBlue) == 1);

        // The flag holds on the next batch: no re-announcement (edge, not level).
        host.raceTrackPosition({
            { .num = 476, .trackPos = 0.505f },
            { .num = 12, .trackPos = 0.53f },
        });
        log = host.spotterCueLog();
        CHECK(countOf(log, kBlue) == 1);
    }

    SUBCASE("cue pack: override, explicit mute, and fallback coexist") {
        host.spotterEnable(true);
        host.spotterCategoryMask(0x1F);
        // The _mix line routes this cue through the chunk-mixer path; its
        // chunks don't exist in the harness, so the worker walks the
        // fallback ladder down to TTS — the cue log (asserted below) must be
        // identical either way, and this is what drives that code headless.
        host.spotterInstallPackOver(
            "[Cues]\n"
            "fastest_lap_other = Heads up, {event_rider} just went {event_time}.\n"
            "fastest_lap_other_mix = seg_fastlap.wav {event_rider} {event_time}\n"
            "session_started =\n");  // pack mutes the green flag

        host.session(RACE1, 10);                          // muted by the pack
        // One clock tick each: a held cue is replaced by a newer one, which
        // is the whole point of the hold, so these two need separate instants
        // to both be heard.
        host.raceLap(RACE1, 476, 1, 108231, /*best=*/2);  // overridden phrase
        host.classify(RACE1, 479000, { { .num = 476, .laps = 1 },
                                       { .num = 12, .laps = 1 } });
        host.raceLap(RACE1, 12, 2, 107904, /*best=*/2);   // no override: built-in
        host.classify(RACE1, 478000, { { .num = 12, .laps = 2 },
                                       { .num = 476, .laps = 1 } });
        const std::string log = host.spotterCueLog();
        CHECK(log.find("Race 1 underway.") == std::string::npos);   // muted
        CHECK(log.find("Heads up, rider four seventy six just went one forty"
                       " eight point two.") != std::string::npos);
        CHECK(log.find("Fastest lap, nice work, one forty seven point nine")
              != std::string::npos);
    }

    SUBCASE("alongside rival: side called from world position, flip re-calls") {
        // A rival OVERLAPPING along-track gets a left/right call computed
        // from world offsets against the focused rider's compass yaw
        // (forward = (sin, cos) in X/Z, +90deg = rider's right — the track
        // builder's convention). Facing north (yaw 0), +X is the right side.
        host.spotterEnable(true);
        host.spotterCategoryMask(0x1F);
        host.session(RACE1, 10);
        host.classify(RACE1, 60000, {
            { .num = 12, .laps = 2 },
            { .num = 476, .laps = 2 },
        });
        host.raceTrackPosition({
            { .num = 12, .trackPos = 0.500f, .posX = 0.0f, .posZ = 100.0f,
              .yaw = 0.0f },
            { .num = 476, .trackPos = 0.499f, .posX = 2.0f, .posZ = 99.0f,
              .yaw = 0.0f },
        });
        std::string log = host.spotterCueLog();
        CHECK(log.find("Rider right.") != std::string::npos);
        CHECK(log.find("Rider left.") == std::string::npos);

        // Crosses to the other side: immediate re-call.
        host.raceTrackPosition({
            { .num = 12, .trackPos = 0.500f, .posX = 0.0f, .posZ = 100.0f,
              .yaw = 0.0f },
            { .num = 476, .trackPos = 0.499f, .posX = -2.0f, .posZ = 99.0f,
              .yaw = 0.0f },
        });
        log = host.spotterCueLog();
        CHECK(log.find("Rider left.") != std::string::npos);

        // Falls away far behind: the episode ends with the paired "Clear." --
        // but only because it LASTED. Advance the session clock past the
        // clear-min-episode gate first; without that the release stays silent
        // (the blip case below pins that side). The classify param is time
        // REMAINING (elapsed = length - time), so advancing means LESS.
        host.classify(RACE1, 54000, {
            { .num = 12, .laps = 2 },
            { .num = 476, .laps = 2 },
        });
        host.raceTrackPosition({
            { .num = 12, .trackPos = 0.500f, .posX = 0.0f, .posZ = 100.0f,
              .yaw = 0.0f },
            { .num = 476, .trackPos = 0.475f, .posX = 0.0f, .posZ = 60.0f,
              .yaw = 0.0f },
        });
        CHECK(host.spotterCueLog().find("Clear.") != std::string::npos);
    }

    SUBCASE("proximity: a rider blipping through the band ends silently") {
        // A real session said "Rider behind." / "All clear behind." 215ms
        // apart -- a rival transiting the band while passing wide. The
        // announce is right; the instant clear is chatter, so a release
        // before clearMinEpisodeMs resets the state without a voice line.
        host.spotterEnable(true);
        host.spotterCategoryMask(0x1F);
        host.session(RACE1, 10);
        host.classify(RACE1, 60000, {
            { .num = 12, .laps = 2 },
            { .num = 476, .laps = 2 },
        });
        host.raceTrackPosition({
            { .num = 12, .trackPos = 0.500f, .posX = 0.0f, .posZ = 100.0f,
              .yaw = 0.0f },
            { .num = 476, .trackPos = 0.494f, .posX = 0.0f, .posZ = 90.4f,
              .yaw = 0.0f },
        });
        std::string log = host.spotterCueLog();
        CHECK(log.find("Rider behind.") != std::string::npos);
        // Gone two ticks later, session clock barely moved: no "Clear."
        host.raceTrackPosition({
            { .num = 12, .trackPos = 0.500f, .posX = 0.0f, .posZ = 100.0f,
              .yaw = 0.0f },
            { .num = 476, .trackPos = 0.475f, .posX = 0.0f, .posZ = 60.0f,
              .yaw = 0.0f },
        });
        CHECK(host.spotterCueLog().find("Clear.") == std::string::npos);
    }

    SUBCASE("alongside: a rival up the road is one you can see, so no call") {
        // The window reaches back into the blind spot and only a little
        // forward. Track length is 1600m here, so 0.002 of a lap is 3.2m --
        // inside the old symmetric 5m window, outside the 2m forward one.
        host.spotterEnable(true);
        host.spotterCategoryMask(0x1F);
        host.session(RACE1, 10);
        host.classify(RACE1, 60000, {
            { .num = 12, .laps = 2 },
            { .num = 476, .laps = 2 },
        });
        host.raceTrackPosition({
            { .num = 12,  .trackPos = 0.500f, .posX = 0.0f, .posZ = 100.0f,
              .yaw = 0.0f },
            { .num = 476, .trackPos = 0.502f, .posX = 2.0f, .posZ = 103.0f,
              .yaw = 0.0f },
        });
        CHECK(host.spotterCueLog().find("Rider right.") == std::string::npos);

        // Drop back to 1.6m BEHIND -- the same overlap, on the side you
        // cannot see -- and it calls.
        host.raceTrackPosition({
            { .num = 12,  .trackPos = 0.500f, .posX = 0.0f, .posZ = 100.0f,
              .yaw = 0.0f },
            { .num = 476, .trackPos = 0.499f, .posX = 2.0f, .posZ = 98.4f,
              .yaw = 0.0f },
        });
        CHECK(host.spotterCueLog().find("Rider right.") != std::string::npos);
    }

    SUBCASE("every cue is muted by the category it is EMITTED as") {
        // The gate used to live at each emitter, which is to say it was
        // written out twenty times — and three of those drifted: fuel never
        // checked at all, the hotkey cue skipped the check its sibling
        // gate_drop applies, and spectate_target checked General while the
        // registry, the shipped pack and the reference all filed it under
        // Opponents. Each left a switch in the menu that did not silence what
        // it named. emitCue applies the gate now, so this case is really
        // asking whether that single choke point holds.
        host.spotterEnable(true);
        host.session(RACE1, 10);
        host.classify(RACE1, 60000, {
            { .num = 12, .laps = 2 },
            { .num = 476, .laps = 2 },
        });

        // GENERAL off: your own status cues go quiet, including the two that
        // used to slip through.
        host.spotterCategoryMask(0x1F & ~(1u << 0));
        // Asserted as "the log did not grow", not as "this wording is absent":
        // hotkey_triggered names {position}, and a phrase assertion that
        // guesses the wrong position passes whether the gate works or not.
        const size_t mark = host.spotterCueLog().size();
        host.spotterHotkey();
        CHECK(host.spotterCueLog().size() == mark);
        host.classify(RACE1, 61000, {
            { .num = 12, .laps = 2, .pit = 1 },
            { .num = 476, .laps = 2 },
        });
        CHECK(host.spotterCueLog().substr(mark).find("Entering pit lane") ==
              std::string::npos);

        // ...and General back on, the same hotkey speaks. Without this the
        // case would pass just as well if the cue were broken outright.
        host.spotterCategoryMask(0x1F);
        const size_t mark2 = host.spotterCueLog().size();
        host.spotterHotkey();
        CHECK(host.spotterCueLog().size() > mark2);
    }

    SUBCASE("muting a category still resets the session state behind it") {
        // The per-session wipe — milestone latches, the pace tracker, the
        // cached gaps either side, the fuel flags — hangs off the
        // SessionStarted branch, and SessionStarted is a General cue.
        // onRaceEvent used to return on the category BEFORE reaching it, so
        // muting General carried all of it across a session boundary. The gate
        // lives in emitCue now precisely so a muted cue still does its
        // bookkeeping. halfway_point is the observable end of that: it fires
        // once per session, and only a reset lets it fire in the next one.
        host.spotterEnable(true);
        host.spotterSubtitles(true);
        host.spotterCategoryMask(0x1F);
        // A pure LAP race on purpose. The timed milestone re-arms itself on a
        // clock rewind, so it would pass with the reset skipped; the lap path
        // has no clock to notice a new session, which is the case the reset
        // block's own comment says it exists for.
        auto halfway = [&]() {
            host.session(RACE1, /*numLaps=*/4, /*lengthMs=*/0);
            host.classify(RACE1, 0, {{ .num = 12, .laps = 0 }});
            // Lap 1 ARMS the milestone, lap 2 crosses it. Starting at the
            // crossing would be a mid-race join, which is swallowed on
            // purpose — see the milestone unit cases.
            host.raceLap(RACE1, 12, 1, 90000);
            host.classify(RACE1, 90000, {{ .num = 12, .laps = 1 }});
            host.raceLap(RACE1, 12, 2, 90000);
            host.classify(RACE1, 180000, {{ .num = 12, .laps = 2 }});
        };
        halfway();
        REQUIRE(host.spotterCueLog().find("Halfway there.") !=
                std::string::npos);

        // Session two, whose green flag is MUTED. Its reset must still run.
        host.spotterCategoryMask(0x1F & ~(1u << 0));
        const size_t mark = host.spotterCueLog().size();
        halfway();
        CHECK(host.spotterCueLog().substr(mark).find("Halfway there.") !=
              std::string::npos);
        host.spotterCategoryMask(0x1F);
    }

    SUBCASE("boxed in: a rival on each side is its own call, not one side") {
        // The case where naming ONE side is worse than saying nothing: act on
        // "rider left" and you move into the rider nobody told you about. A
        // third entry is needed because the field fixture only has one rival.
        host.addEntry(901, "Rival2");
        host.spotterEnable(true);
        host.spotterCategoryMask(0x1F);
        host.session(RACE1, 10);
        host.classify(RACE1, 60000, {
            { .num = 12, .laps = 2 },
            { .num = 476, .laps = 2 },
            { .num = 901, .laps = 2 },
        });
        host.raceTrackPosition({
            { .num = 12,  .trackPos = 0.500f, .posX =  0.0f, .posZ = 100.0f,
              .yaw = 0.0f },
            { .num = 476, .trackPos = 0.499f, .posX =  2.0f, .posZ = 99.0f,
              .yaw = 0.0f },
            { .num = 901, .trackPos = 0.499f, .posX = -2.0f, .posZ = 99.0f,
              .yaw = 0.0f },
        });
        const std::string log = host.spotterCueLog();
        CHECK(log.find("Riders both sides") != std::string::npos);
        // And NOT a one-sided call, which is the whole point.
        CHECK(log.find("Rider right.") == std::string::npos);
        CHECK(log.find("Rider left.") == std::string::npos);
    }

    SUBCASE("behind: a rider on another lap is not somebody you are racing") {
        // Same geometry, twice, differing only in gapLaps. A rider a lap down
        // sitting on your back wheel is not racing you, and the two cases that
        // DO matter have their own cues (blue_flag when they are lapping you,
        // lapping_traffic when you are lapping them), so calling them here is
        // noise on top of a call you already got.
        host.spotterEnable(true);
        host.spotterCategoryMask(0x1F);
        host.session(RACE1, 10);
        host.classify(RACE1, 60000, {
            { .num = 12,  .laps = 2, .gapLaps = 0 },
            { .num = 476, .laps = 1, .gapLaps = 1 },   // a lap down
        });
        host.raceTrackPosition({
            { .num = 12,  .trackPos = 0.500f },
            { .num = 476, .trackPos = 0.494f },
        });
        CHECK(host.spotterCueLog().find("Rider behind.") == std::string::npos);

        // Put them on your lap and the identical geometry calls.
        host.classify(RACE1, 59000, {
            { .num = 12,  .laps = 2, .gapLaps = 0 },
            { .num = 476, .laps = 2, .gapLaps = 0 },
        });
        host.raceTrackPosition({
            { .num = 12,  .trackPos = 0.500f },
            { .num = 476, .trackPos = 0.494f },
        });
        CHECK(host.spotterCueLog().find("Rider behind.") != std::string::npos);
    }

    SUBCASE("cooldowns re-arm on the countdown clock of a timed session") {
        // Timed sessions (most races) count the raw session clock DOWN, so
        // cooldown arithmetic must run on elapsed time
        // (getSessionElapsedTime) — on the raw clock, "now - last" only
        // ever shrinks and a camped rival would be announced once per race.
        host.spotterEnable(true);
        host.spotterCategoryMask(0x1F);
        host.session(RACE1, 10);   // default 480s timed session
        auto close = [&](int remainingMs) {
            host.classify(RACE1, remainingMs, {
                { .num = 12, .laps = 2 },
                { .num = 476, .laps = 2 },
            });
            host.raceTrackPosition({
                { .num = 12, .trackPos = 0.500f },
                { .num = 476, .trackPos = 0.494f },
            });
        };
        close(400000);   // elapsed 80s: first "Rider behind."
        close(380000);   // elapsed 100s: 20s later, past the 10s repeat
        const std::string log = host.spotterCueLog();
        size_t n = 0;
        for (size_t p = log.find("Rider behind."); p != std::string::npos;
             p = log.find("Rider behind.", p + 1)) {
            ++n;
        }
        CHECK(n == 2);
    }

    SUBCASE("penalty amount rides the detail column into the phrase") {
        // The communication handler writes the seconds as the event's
        // DETAIL ("5s"), not baked into the message — this drives the real
        // callback through the adapter (m_iTime seconds -> ms) and pins the
        // handler->detail->phrase handshake end to end.
        host.spotterEnable(true);
        host.spotterCategoryMask(0x1F);
        // Another rider's penalty is DEFAULT-QUIET (on a full grid it is the
        // loudest per-rider stream there is), so a pack defining the key is
        // what opting in looks like — and what carries the seconds through.
        host.spotterInstallPackOver(
            "[Cues]\n"
            "penalty_other = Penalty for {event_rider}, {penalty_seconds}.\n");
        host.session(RACE1, 10);
        // The handler drops communications for unclassified riders.
        host.classify(RACE1, 60000, {
            { .num = 12, .laps = 2 },
            { .num = 476, .laps = 2 },
        });
        host.communication(476, 0, /*communication=*/2, /*penaltySeconds=*/5);
        CHECK(host.spotterCueLog().find(
                  "Penalty for rider four seventy six, five seconds.") !=
              std::string::npos);
    }

    SUBCASE("one session-start cue, and the name is what varies") {
        // Four keys used to differ only by session kind. Now it is one cue
        // naming the session — and crucially NOT the gate line, which is a
        // later moment a qualifying session may never reach at all.
        host.spotterSubtitles(true);
        host.session(4, 0, 900000);   // PiBoSo session 4 = Qualify
        CHECK(host.spotterCueLog().find("Qualify underway") !=
              std::string::npos);
        CHECK(host.spotterCueLog().find("Green green green.") ==
              std::string::npos);
    }

    SUBCASE("overtime_started names the bonus laps; halfway_point rides the clock") {
        host.spotterSubtitles(true);
        host.session(RACE1, 2, 90000);   // 1:30 + 2 laps
        host.classify(RACE1, 89000, {{ .num = 12, .laps = 1 }});
        // Arm the milestone baseline early (the first tick only arms — a
        // mid-session join must swallow passed thresholds, not announce).
        host.raceTrackPosition({{ .num = 12, .trackPos = 0.4f }});
        // Timed halfway_point (45s elapsed = 45000 remaining) crosses first...
        host.classify(RACE1, 44000, {{ .num = 12, .laps = 1 }});
        host.raceTrackPosition({{ .num = 12, .trackPos = 0.5f }});
        CHECK(host.spotterCueLog().find("Halfway there.") !=
              std::string::npos);
        // ...then the clock goes negative: overtime_started, phrased so it is
        // true wherever the leader happens to be. NOT a countdown — the clock
        // expires mid-lap, so with the leader 100m from the line "two laps to
        // go" undercounts and "three" overcounts. The bonus count is exact and
        // the sentence says what it counts.
        host.classify(RACE1, 1000, {{ .num = 12, .laps = 2 }});
        host.classify(RACE1, -500, {{ .num = 12, .laps = 2 }});
        CHECK(host.spotterCueLog().find("Overtime, two laps after this one")
              != std::string::npos);
        CHECK(host.spotterCueLog().find("to go") == std::string::npos);
    }

    SUBCASE("own lap crossing reads the pit board; leader's flag is its own") {
        host.spotterSubtitles(true);
        host.session(RACE1, /*numLaps=*/3, /*lengthMs=*/0);   // pure 3-lap race
        host.classify(RACE1, 30000, {
            { .num = 476, .laps = 1 },
            { .num = 12, .laps = 1 },
        });
        host.raceLap(RACE1, 12, 1, 95000);   // player completes a lap in P2
        // The report waits for the classification that includes this lap —
        // the game always sends one, and reading the order before it arrives
        // is what used to make the position a lap stale.
        host.classify(RACE1, 95000, {
            { .num = 476, .laps = 1 },
            { .num = 12, .laps = 1 },
        });
        CHECK(host.spotterCueLog().find("P two.") != std::string::npos);

        // A non-leader finishing first says nothing at all.
        host.classify(RACE1, 90000, {
            { .num = 476, .laps = 2 },
            { .num = 12, .laps = 2 },
            { .num = 90, .laps = 3 },
        });
        CHECK(host.spotterCueLog().find("has finished") == std::string::npos);

        // The rival leader reaches the lap count: their flag, phrased as
        // the leader's, not a generic finish.
        host.classify(RACE1, 95000, {
            { .num = 476, .laps = 3 },
            { .num = 12, .laps = 2 },
        });
        CHECK(host.spotterCueLog().find("Leader's taken the flag.") !=
              std::string::npos);
        // ...and NOT the generic one. finished_other is default-quiet — on a
        // full grid it is a callout per rider as the flag falls, none of them
        // about your race. The two share compose()'s RiderFinished branch, so
        // silencing one could easily have silenced the other: this asserts
        // both halves of that, which is the part a future edit would break.
    }

    SUBCASE("pace report: ahead rides your crossing, behind is its own cue") {
        host.spotterSubtitles(true);
        // The ahead gap is a VARIABLE on the crossing cues, not a cue: it is
        // measured at the same instant they fire, so a key of its own marked
        // no moment. This pack line is how a user gets it spoken.
        host.spotterInstallPackOver(
            "[Cues]\n"
            "lap_completed = P {position}[, {gap_to_ahead} ahead].\n");
        host.session(RACE1, /*numLaps=*/5, /*lengthMs=*/0);
        auto grid = [&](int sessionTimeMs) {
            host.classify(RACE1, sessionTimeMs, {
                { .num = 476, .laps = 1 },
                { .num = 12, .laps = 1 },
                { .num = 90, .laps = 1 },
            });
        };
        grid(30000);
        // Lap 2 crossings: the rider ahead (#476) at 60.0s, me at 62.1s.
        grid(60000);
        host.raceLap(RACE1, 476, 2, 90000);
        grid(62100);
        host.raceLap(RACE1, 12, 2, 92100);
        grid(62100);   // the classification that includes your lap
        std::string log = host.spotterCueLog();
        // One cue carrying both, rather than "P two." followed by a separate
        // "Ahead, two point one." at the very same instant.
        CHECK(log.find("P two, two point one ahead.") != std::string::npos);
        // And the stopwatch value, not the live estimate: 62.1 - 60.0.
        CHECK(log.find("Ahead, two point one.") == std::string::npos);
        // The behind gap is DELAYED until #90 reaches the same line — no
        // number is spoken before it exists.
        CHECK(log.find("Behind,") == std::string::npos);
        grid(64900);
        host.raceLap(RACE1, 90, 2, 94900);
        CHECK(host.spotterCueLog().find("Behind, two point eight") !=
              std::string::npos);
    }

    SUBCASE("closing on a backmarker: one call per encounter") {
        host.spotterEnable(true);
        host.spotterCategoryMask(0x1F);
        host.session(RACE1, 10);
        // Player a lap up, 48m behind the backmarker (within the blue-flag
        // detection window): the player IS the lapper.
        host.classify(RACE1, 60000, {
            { .num = 12, .laps = 3 },
            { .num = 476, .laps = 1 },
        });
        host.raceTrackPosition({
            { .num = 12, .trackPos = 0.500f },
            { .num = 476, .trackPos = 0.530f },
        });
        // ...and the backmarker is NAMED, the mirror of the blue-flag case:
        // same cache, read the other way (getRiderLappingTarget).
        CHECK(host.spotterCueLog().find("Backmarker ahead, rider four seventy six.") !=
              std::string::npos);
    }

    SUBCASE("the settings voice voice_preview never reaches the cue feed") {
        // The voice_preview is a settings-menu noise, not something that happened in
        // the race — and the cue log IS the subtitle widget's feed. A voice_preview
        // that logged would put "rider nine sixty five" on screen mid-race for
        // everyone who cycled a voice, and land in the demo-tape transcripts
        // that read this log as a subtitle track. Audio itself is unobservable
        // here (a Wine prefix has no SAPI voice), so the absence is the pin.
        host.spotterEnable(true);
        host.spotterCategoryMask(0x1F);
        host.spotterInstallShippedPack();
        host.session(RACE1, 10);
        const std::string before = host.spotterCueLog();
        REQUIRE(before.find("Race 1 underway") != std::string::npos);

        host.spotterPreview(false);
        host.spotterPreview(true);
        CHECK(host.spotterCueLog() == before);

        // ...and with spoken audio off it is a no-op rather than a silent
        // log entry: a click that made noise (or a subtitle) in a silent mode
        // would be a bug in the mode, not a voice_preview.
        host.spotterEnable(false);
        host.spotterPreview(false);
        CHECK(host.spotterCueLog() == before);
    }

    SUBCASE("category toggle mutes its events, not the rest") {
        host.spotterEnable(true);
        // Everything EXCEPT Timing (bit 1 = SpotterPhrase::Category::Timing).
        host.spotterCategoryMask(0xF & ~(1u << 1));

        host.session(RACE1, 10);                        // General: audible
        host.raceLap(RACE1, 12, 1, 107904, /*best=*/2);  // YOUR lap: Timing
        const std::string log = host.spotterCueLog();
        CHECK(log.find("Race 1 underway") != std::string::npos);
        CHECK(log.find("Fastest lap, nice work") == std::string::npos);
    }

    SUBCASE("categories follow the SUBJECT: your cue vs the same cue about a rival") {
        // The pair that proves the filing rule end to end. Same event type
        // (FastestLap), same session, different subject — so the two land in
        // different categories and each toggle silences exactly its own.
        host.spotterEnable(true);
        host.session(RACE1, 10);

        // Opponents off, Timing on: your lap speaks, the rival's does not.
        host.spotterCategoryMask(0xF & ~(1u << 2));
        // A classification between them: only ONE fastest lap is held at a
        // time, so without it the rival's would replace yours before either
        // spoke — which is exactly the join-replay behaviour, and not what
        // this case is about.
        host.raceLap(RACE1, 12, 1, 107904, /*best=*/2);
        host.classify(RACE1, 60000, { { .num = 12, .laps = 1 },
                                      { .num = 476, .laps = 0 } });
        host.raceLap(RACE1, 476, 1, 108231, /*best=*/2);
        host.classify(RACE1, 61000, { { .num = 12, .laps = 1 },
                                      { .num = 476, .laps = 1 } });
        std::string log = host.spotterCueLog();
        CHECK(log.find("Fastest lap, nice work") != std::string::npos);
        // The NUMBER words, not "rider four seventy six": expand() capitalises
        // the finished line, so an alternate opening with {event_rider} says
        // "Rider four seventy six" and a case-sensitive absence check on the
        // lowercase form would pass without proving anything.
        CHECK(log.find("four seventy six") == std::string::npos);

        // And a rival's PENALTY is opponent news too — it used to be filed
        // General and kept talking through a muted Opponents toggle. Opted in
        // by a pack (it is default-quiet), or the silence below would prove
        // nothing about the category gate.
        host.spotterInstallPackOver(
            "[Cues]\n"
            "penalty_other = Penalty for {event_rider}, {penalty_seconds}.\n");
        host.classify(RACE1, 60000, {
            { .num = 12, .laps = 2 },
            { .num = 476, .laps = 2 },
        });
        host.communication(476, 0, /*communication=*/2, /*penaltySeconds=*/10);
        CHECK(host.spotterCueLog().find("Penalty for rider") ==
              std::string::npos);

        // Your OWN status changes are General, not Opponents: with Opponents
        // still muted, your pit call and checkered flag must survive — the
        // bug the subject argument exists for.
        host.classify(RACE1, 61000, {
            { .num = 12, .laps = 2, .pit = 1 },
            { .num = 476, .laps = 2 },
        });
        CHECK(host.spotterCueLog().find("Entering pit lane.") !=
              std::string::npos);

        host.spotterCategoryMask(0x1F);   // see the note in the next case
    }
}

TEST_CASE("spotter: proximity cues fire when replaying a recorded tape") {
    // The replay-tool workflow end to end: record a session with the plugin's
    // OWN recorder (the taps in mxb_api.cpp — exactly what [Recorder]
    // enabled=1 captures in-game), then replay the tape into a fresh plugin
    // instance and hear the spotter. Guards two things a direct-callback test
    // cannot: that RaceTrackPosition survives the tape round-trip into the
    // detector, and that the focused rider re-resolves from the REPLAYED
    // RaceAddEntry (the player) — there is no live game to ask.
    //
    // Note the recording must contain a PLAYER (a while-riding capture): the
    // tape format has no spectate-selection event, so a while-spectating
    // capture replays with no focused rider and the proximity/hazard
    // detectors stand down. The committed fixture tapes (farm14 etc.) are
    // additionally slimmed to the standings-only profile — no track
    // positions at all — which is why replaying them speaks event cues only.
    const std::string tapePath =
        "Z:\\tmp\\mxbmrp3-tests\\spotter\\proximity.tape";
    {
        PluginHost rec(dllPath());
        REQUIRE(rec.loaded());
        rec.startup("Z:\\tmp\\mxbmrp3-tests\\spotter\\");
        REQUIRE(rec.startRecording(tapePath));
        rec.eventInit("TestTrack", "Player");
        rec.raceEvent("TestTrack");
        rec.addEntry(12, "Player");
        rec.addEntry(476, "Rival");
        rec.session(RACE1, 10);
        rec.classify(RACE1, 60000, {
            { .num = 12, .laps = 2 },
            { .num = 476, .laps = 2 },
        });
        // Rival 9.6m behind the player (default 1600m track), then 32m back:
        // the behind -> clear edge pair, straight onto tape.
        rec.raceTrackPosition({
            { .num = 12, .trackPos = 0.500f },
            { .num = 476, .trackPos = 0.494f },
        });
        // The clock advance rides the tape too (classify time is REMAINING):
        // the clear-min-episode gate needs the contact to have lasted.
        rec.classify(RACE1, 54000, {
            { .num = 12, .laps = 2 },
            { .num = 476, .laps = 2 },
        });
        rec.raceTrackPosition({
            { .num = 12, .trackPos = 0.500f },
            { .num = 476, .trackPos = 0.480f },
        });
        rec.stopRecording();
    }  // unload — the replay below starts from plugin statics reset

    PluginHost host(dllPath());
    REQUIRE(host.loaded());
    host.startup("Z:\\tmp\\mxbmrp3-tests\\spotter\\");
    host.spotterInstallShippedPack();   // the words live there; see the case above
    // Settings too: SpotterManager is a singleton that OUTLIVES a PluginHost
    // (the DLL stays mapped across cases in one binary), so a category the
    // previous case muted is still muted here. State every gate this case
    // depends on rather than inheriting whatever ran before it.
    host.spotterCategoryMask(0x1F);
    host.spotterSubtitles(true);
    CHECK(host.replayTape(tapePath) > 0);
    const std::string log = host.spotterCueLog();
    CHECK(log.find("Rider behind.") != std::string::npos);
    CHECK(log.find("Clear.") != std::string::npos);
}

// The sector cues had NO coverage, and were dead everywhere they matter: the
// whole split callback returned early unless isRaceSession(), so practice and
// qualifying — the sessions you actually want your sectors read back in — were
// silent. The pace half of that callback IS race-only; the sector half is not.
TEST_CASE("spotter: sector cues fire in practice, not just races") {
    PluginHost host(dllPath());
    REQUIRE(host.loaded());
    host.startup("Z:\\tmp\\mxbmrp3-tests\\spotter-sector\\");
    // The sector cues are DEFAULT-QUIET (three or four a lap), so a pack that
    // defines them is what opting in looks like — and is what this asserts.
    // One line, not three: the faster/slower keys fall back to this one.
    host.spotterInstallPackOver(
        "[Cues]\n"
        "sector_completed = Sector {sector_number}, {event_time},"
        " {sector_duration} alone.\n");
    host.eventInit("TestTrack", "Player");
    host.raceEvent("TestTrack");
    host.addEntry(12, "Player");
    host.spotterEnable(true);
    host.spotterCategoryMask(0x1F);

    const int PRACTICE = 1;   // PiBoSo session 1
    host.session(PRACTICE, 0);

    // Splits arrive cumulative from the lap start, and BOTH readings have to
    // be right: {event_time} is the accumulated 68.4 that TimingHud shows for
    // this crossing, and {sector_duration} is the 38.3 that sector took on its
    // own. Quoting either where the other belongs is a plausible-sounding
    // wrong number, which is the failure worth pinning.
    host.raceSplit(PRACTICE, 12, 0, 0, 30100);
    host.raceSplit(PRACTICE, 12, 0, 1, 68400);
    const std::string log = host.spotterCueLog();
    CHECK(log.find("Sector one, thirty point one, thirty point one alone.")
          != std::string::npos);
    CHECK(log.find("Sector two, sixty eight point four,"
                   " thirty eight point three alone.") != std::string::npos);

    // Your position and lap time are the same story as the sectors: they were
    // gated on isRaceSession() too, so a practice lap said nothing at all.
    // Two things had to be true for this to fire and neither was. The
    // position comes from the classification order, so a session without one
    // resolves to 0 and the report returns early. And the handler's call to
    // the spotter sat inside an `if (sessionNumLaps > 0)` block — practice has
    // no lap count, so the call was never reached at all, whatever the manager
    // would have decided.
    host.classify(PRACTICE, 60000, { { .num = 12, .laps = 1 } });
    host.raceLap(PRACTICE, 12, 1, 107904, /*best=*/2);
    host.classify(PRACTICE, 107904, { { .num = 12, .laps = 1 } });
    CHECK(host.spotterCueLog().find("P one") != std::string::npos);

    // A split with no predecessor for that rider+lap cannot be turned into a
    // sector, and must stay quiet rather than report the cumulative figure.
    // Nothing else in this case emits a third sector, so its absence from the
    // whole log is the assertion.
    host.raceSplit(PRACTICE, 12, 4, 2, 95000);
    CHECK(host.spotterCueLog().find("Sector three") == std::string::npos);
}

// The gate drop is a LATER, separate moment from the session going active —
// a standing start holds on the grid in between — and practice never reaches
// it at all. Before this, both said "Green green green" through one key, so
// a practice session claimed a gate had dropped and a real race start was
// indistinguishable from the session merely opening.
TEST_CASE("spotter: the gate drop is its own moment, after the session starts") {
    PluginHost host(dllPath());
    REQUIRE(host.loaded());
    host.startup("Z:\\tmp\\mxbmrp3-tests\\spotter-gate\\");
    host.spotterInstallPack(
        "[Cues]\n"
        "session_started = {session_name} underway.\n"
        "gate_drop = Gate is down.\n");
    host.eventInit("TestTrack", "Player");
    host.raceEvent("TestTrack");
    host.addEntry(12, "Player");
    host.spotterEnable(true);
    host.spotterCategoryMask(0x1F);

    // A race arrives at PRE_START, then flips to IN_PROGRESS — which is the
    // session going active, NOT the gate falling.
    host.session(RACE1, 5, /*lengthMs=*/0, /*state=*/256);
    host.raceSessionState(RACE1, 16);
    std::string log = host.spotterCueLog();
    CHECK(log.find("Race 1 underway") != std::string::npos);
    CHECK(log.find("Gate is down.") == std::string::npos);

    // Then the grid HOLDS (classification reports not-racing), and only when
    // it flips to racing has the gate actually dropped.
    host.classify(RACE1, 0, { { .num = 12, .laps = 0 } }, /*sessionState=*/32);
    CHECK(host.spotterCueLog().find("Gate is down.") == std::string::npos);
    host.classify(RACE1, 0, { { .num = 12, .laps = 0 } }, /*sessionState=*/16);
    CHECK(host.spotterCueLog().find("Gate is down.") != std::string::npos);
}

// "10 minutes + 2 laps" is the standard MX Bikes race format, and it was the
// one shape the session variables got wrong: {session_length} reported only
// the clock, silently dropping the bonus laps, and {laps_remaining} did
// bonus-laps-minus-laps-completed — arithmetic on two different things, which
// goes negative and vanishes rather than looking wrong.
TEST_CASE("spotter: a time+laps session reports BOTH halves") {
    PluginHost host(dllPath());
    REQUIRE(host.loaded());
    host.startup("Z:\\tmp\\mxbmrp3-tests\\spotter-timelaps\\");
    host.spotterInstallPack(
        "[Cues]\n"
        "session_started = {session_name}, {session_length}.\n");
    host.eventInit("TestTrack", "Player");
    host.raceEvent("TestTrack");
    host.addEntry(12, "Player");
    host.spotterEnable(true);
    host.spotterCategoryMask(0x1F);

    // 10 minutes + 2 bonus laps.
    host.session(RACE1, /*numLaps=*/2, /*lengthMs=*/600000);
    const std::string log = host.spotterCueLog();
    CHECK(log.find("Race 1, ten minutes plus two laps.") != std::string::npos);
    // The clock alone would be the old, half-right answer.
    CHECK(log.find("Race 1, ten minutes.") == std::string::npos);
}

TEST_CASE("spotter: a pure lap session still reports laps") {
    PluginHost host(dllPath());
    REQUIRE(host.loaded());
    host.startup("Z:\\tmp\\mxbmrp3-tests\\spotter-laps\\");
    host.spotterInstallPack(
        "[Cues]\n"
        "session_started = {session_name}, {session_length}.\n");
    host.eventInit("TestTrack", "Player");
    host.raceEvent("TestTrack");
    host.addEntry(12, "Player");
    host.spotterEnable(true);
    host.spotterCategoryMask(0x1F);

    host.session(RACE1, /*numLaps=*/5, /*lengthMs=*/0);
    CHECK(host.spotterCueLog().find("Race 1, five laps.") != std::string::npos);
}

// crashed_you/crashed_other existed with no test, so "is a crash detected at
// all?" was unanswerable without going on track. It rides the SAME rising
// edge as the per-session crash counter — not-crashed to crashed — which is
// the only place that transition exists.
TEST_CASE("spotter: a crash is detected, once per crash, and filed by subject") {
    PluginHost host(dllPath());
    REQUIRE(host.loaded());
    host.startup("Z:\\tmp\\mxbmrp3-tests\\spotter-crash\\");
    // DEFAULT-QUIET both ways: your own crash you already know about, and a
    // busy first corner would be a wall of the other. A pack defining them is
    // what opting in looks like.
    host.spotterInstallPack(
        "[Cues]\n"
        "crashed_you = You went down.\n"
        "crashed_other = {event_rider} is down.\n");
    host.eventInit("TestTrack", "Player");
    host.raceEvent("TestTrack");
    host.addEntry(12, "Player");
    host.addEntry(476, "Rival");
    host.spotterEnable(true);
    host.spotterCategoryMask(0x1F);
    host.session(RACE1, 5);
    host.classify(RACE1, 30000, {
        { .num = 12, .laps = 1 },
        { .num = 476, .laps = 1 },
    });

    // Upright: nothing to say.
    host.raceTrackPosition({
        { .num = 12, .trackPos = 0.50f },
        { .num = 476, .trackPos = 0.20f },
    });
    CHECK(host.spotterCueLog().find("down") == std::string::npos);

    // You go down, and a rival does too — filed by who it is about.
    host.raceTrackPosition({
        { .num = 12, .trackPos = 0.50f, .crashed = 1 },
        { .num = 476, .trackPos = 0.20f, .crashed = 1 },
    });
    std::string log = host.spotterCueLog();
    CHECK(log.find("You went down.") != std::string::npos);
    // Capitalised: this pack's line opens with {event_rider}, and expand()
    // capitalises the finished line so the subtitle never starts lowercase.
    CHECK(log.find("Rider four seventy six is down.") != std::string::npos);

    // STILL down is not a second crash: it is a rising edge, so holding the
    // flag must stay quiet rather than repeat every position batch.
    const size_t before = log.length();
    host.raceTrackPosition({
        { .num = 12, .trackPos = 0.50f, .crashed = 1 },
        { .num = 476, .trackPos = 0.20f, .crashed = 1 },
    });
    CHECK(host.spotterCueLog().length() == before);
}

// The position report reads the CLASSIFICATION order, which is not rebuilt for
// a lap until the classification callback that follows it. Emitting at the
// crossing therefore reported the standings from BEFORE that lap — a whole lap
// stale, and plausible enough to pass unnoticed: "P three" as you cross the
// line having just taken second.
//
// The plugin already knew this: the "finished P#" event log entry was moved
// out of the lap handler into batchUpdateStandings for exactly this reason.
TEST_CASE("spotter: the position report waits for the order to include the lap") {
    PluginHost host(dllPath());
    REQUIRE(host.loaded());
    host.startup("Z:\\tmp\\mxbmrp3-tests\\spotter-stale\\");
    host.spotterInstallPack("[Cues]\nlap_completed = P {position}.\n");
    host.eventInit("TestTrack", "Player");
    host.raceEvent("TestTrack");
    host.addEntry(12, "Player");
    host.addEntry(476, "Rival");
    host.spotterEnable(true);
    host.spotterCategoryMask(0x1F);
    host.session(RACE1, 5);

    // You are P2 as the lap begins.
    host.classify(RACE1, 30000, {
        { .num = 476, .laps = 1 },
        { .num = 12, .laps = 1 },
    });

    // You cross the line and take the lead. The classification carrying that
    // has NOT arrived yet, so nothing may be said about your position — the
    // only position available right now is the stale one.
    host.raceLap(RACE1, 12, 2, 95000);
    CHECK(host.spotterCueLog().find("P two.") == std::string::npos);

    // The order arrives with you in front, and the report speaks THAT.
    host.classify(RACE1, 95000, {
        { .num = 12, .laps = 2 },
        { .num = 476, .laps = 1 },
    });
    const std::string log = host.spotterCueLog();
    CHECK(log.find("P one.") != std::string::npos);
    CHECK(log.find("P two.") == std::string::npos);
}

// position_gained/_lost measured against the RACE START position, which made it
// a running total announced on every lap rather than an event. Replaying a real
// Farm 14 race it said "Up thirteen, P seven", then "Up twelve, P eight" on the
// next lap — a lap the player had LOST a place on — then "Up twelve, P eight"
// again on a lap where nothing moved at all. The total is a number, and numbers
// are variables here ({positions_since_start}); the CUE has to be the change.
TEST_CASE("spotter: places gained are this lap's, not a running total") {
    PluginHost host(dllPath());
    REQUIRE(host.loaded());
    host.startup("Z:\\tmp\\mxbmrp3-tests\\spotter-posdelta\\");
    host.spotterInstallPack(
        "[Cues]\n"
        "position_gained = Up {positions_changed}, P {position}.\n"
        "position_lost = Down {positions_changed}, P {position}.\n"
        "lap_completed =\n");     // muted: this case is about the delta only
    host.eventInit("TestTrack", "Player");
    host.raceEvent("TestTrack");
    host.addEntry(12, "Player");
    host.addEntry(476, "Rival");
    host.addEntry(99, "Third");
    host.addEntry(7, "Fourth");
    host.spotterEnable(true);
    host.spotterCategoryMask(0x1F);
    // PRE_START, not the usual running state: the grid snapshot only happens on
    // the transition INTO in-progress, so a session that starts already green
    // leaves getRaceStartPosition() at -1 and this case would silently test
    // nothing (the old code fell through to the same S/F reference).
    host.session(RACE1, 5, /*lengthMs=*/480000, /*state=*/256);

    // Green flag with the player P4 on the grid — this is what snapshots the
    // race-start positions, and therefore what the old reference read.
    // The grid order, still under PRE_START — a classification carries the
    // session state too, so sending this one green would itself complete the
    // transition and make the raceSessionState below a no-op re-fire.
    host.classify(RACE1, 0, {
        { .num = 476, .laps = 0 }, { .num = 99, .laps = 0 },
        { .num = 7, .laps = 0 },   { .num = 12, .laps = 0 },
    }, /*sessionState=*/256);
    host.raceSessionState(RACE1, 16);
    REQUIRE(host.spotterCueLog().find("Up ") == std::string::npos);

    // Lap 1: P4 on the grid -> P2 at the line. Measured from the GRID, because
    // the opening lap's S/F reference was taken partway round it and would
    // report only the last place or two of a first-lap charge.
    host.raceLap(RACE1, 12, 1, 95000);
    host.classify(RACE1, 95000, {
        { .num = 476, .laps = 1 }, { .num = 12, .laps = 1 },
        { .num = 99, .laps = 1 },  { .num = 7, .laps = 1 },
    });
    CHECK(host.spotterCueLog().find("Up two, P two.") != std::string::npos);

    // Lap 2: still P2. Nothing happened, so nothing is said — measured from
    // the grid this lap would have repeated "Up two" for the rest of the race.
    host.raceLap(RACE1, 12, 2, 94000);
    host.classify(RACE1, 189000, {
        { .num = 476, .laps = 2 }, { .num = 12, .laps = 2 },
        { .num = 99, .laps = 2 },  { .num = 7, .laps = 2 },
    });
    const std::string afterHold = host.spotterCueLog();
    size_t upTwo = afterHold.find("Up two, P two.");
    CHECK(upTwo != std::string::npos);
    CHECK(afterHold.find("Up two, P two.", upTwo + 1) == std::string::npos);

    // Lap 3: P2 -> P3, and the place is lost MID-LAP so the classification
    // standing at the crossing already shows P3. That is the second trap: a
    // reference sampled at the crossing sees no change and says nothing, even
    // though the lap plainly cost a place. Measured from the grid instead it
    // would have called this a GAIN ("Up one", P4 on the grid vs P3 now).
    host.classify(RACE1, 240000, {
        { .num = 476, .laps = 2 }, { .num = 99, .laps = 2 },
        { .num = 12, .laps = 2 },  { .num = 7, .laps = 2 },
    });
    host.raceLap(RACE1, 12, 3, 96000);
    host.classify(RACE1, 285000, {
        { .num = 476, .laps = 3 }, { .num = 99, .laps = 3 },
        { .num = 12, .laps = 3 },  { .num = 7, .laps = 3 },
    });
    const std::string log = host.spotterCueLog();
    CHECK(log.find("Down one, P three.") != std::string::npos);
    CHECK(log.find("Up one, P three.") == std::string::npos);
}

// Everything a spotter says about YOUR race stops being true the moment your
// race ends, and the tapes were full of it: the demo weekend spoke "P two,
// twenty point zero to rider ninety nine, losing" and "Behind, five point five,
// dropping back" AFTER the checkered flag — the cool-down lap crosses the line
// and reaches timing points like any other — plus "Rider behind" / "Rider
// right" / "Clear" as still-racing riders swept past. Farm 14 did the same
// after the player RETIRED.
TEST_CASE("spotter: your race ending stops the cues about your race") {
    PluginHost host(dllPath());
    REQUIRE(host.loaded());
    host.startup("Z:\\tmp\\mxbmrp3-tests\\spotter-postflag\\");
    host.spotterInstallPack(
        "[Cues]\n"
        "lap_completed = P {position}.\n"
        "sector_completed = Sector {sector_number}, {event_time}.\n");
    host.eventInit("TestTrack", "Player");
    host.raceEvent("TestTrack");
    host.addEntry(12, "Player");
    host.addEntry(476, "Rival");
    host.spotterEnable(true);
    host.spotterCategoryMask(0x1F);
    host.session(RACE1, /*numLaps=*/5, /*lengthMs=*/0);

    // A normal lap first, so the silence below is the RETIREMENT talking and
    // not a case that was never wired up.
    host.classify(RACE1, 30000, {
        { .num = 476, .laps = 0 }, { .num = 12, .laps = 0 },
    });
    host.raceLap(RACE1, 12, 1, 95000);
    host.classify(RACE1, 95000, {
        { .num = 476, .laps = 1 }, { .num = 12, .laps = 1 },
    });
    REQUIRE(host.spotterCueLog().find("P two.") != std::string::npos);

    // Out of the race (EntryState 3 = Retired).
    host.classify(RACE1, 150000, {
        { .num = 476, .laps = 2 },
        { .num = 12, .laps = 1, .state = 3 },
    });
    const std::string atRetire = host.spotterCueLog();

    // Rolling back to the pits still crosses splits and the line.
    host.raceSplit(RACE1, 12, 1, 0, 40000);
    host.raceLap(RACE1, 12, 2, 190000);
    host.classify(RACE1, 190000, {
        { .num = 476, .laps = 2 },
        { .num = 12, .laps = 2, .state = 3 },
    });
    const std::string after = host.spotterCueLog();
    CHECK(after.find("Sector one") == std::string::npos);
    // Nothing new was said at all — the only position report in the log is
    // still the one from the racing lap above.
    CHECK(after == atRetire);
}

// {session_remaining} folds the bonus laps of a "8 minutes + 1 lap" race onto
// the clock. With no clock yet — which is where session_started fires, before
// any RaceSessionState has carried a time — it returned the bonus laps ALONE,
// so a race announced itself as "Race 2 underway, eight minutes plus one lap,
// one lap left". The bonus laps are a suffix on a time, never an answer of
// their own; unknown has to stay unknown so the optional group drops.
TEST_CASE("spotter: bonus laps never stand in for the session clock") {
    PluginHost host(dllPath());
    REQUIRE(host.loaded());
    host.startup("Z:\\tmp\\mxbmrp3-tests\\spotter-remaining\\");
    host.spotterInstallPack(
        "[Cues]\n"
        "session_started = {session_name} underway[, {session_length}]"
        "[, {session_remaining} left].\n");
    host.eventInit("TestTrack", "Player");
    host.raceEvent("TestTrack");
    host.addEntry(12, "Player");
    host.spotterEnable(true);
    host.spotterCategoryMask(0x1F);

    // A session shorter than a minute: whole-minute truncation floors at zero,
    // and the demo weekend's qualifying announced itself as "Qualify underway,
    // ZERO MINUTES". Under a minute the seconds are the answer.
    host.session(/*session=*/4, /*numLaps=*/0, /*lengthMs=*/45000);
    CHECK(host.spotterCueLog().find("forty five seconds") != std::string::npos);
    CHECK(host.spotterCueLog().find("zero minutes") == std::string::npos);

    // 8 minutes plus 1 bonus lap, exactly the Farm 14 fixture's race.
    host.session(RACE1, /*numLaps=*/1, /*lengthMs=*/480000);
    const std::string log = host.spotterCueLog();
    // The LENGTH is known and says both halves...
    CHECK(log.find("underway, eight minutes plus one lap") != std::string::npos);
    // ...and what remains is not known yet, so it is not guessed at.
    CHECK(log.find("one lap left") == std::string::npos);
    CHECK(log.find("left") == std::string::npos);
}

// Every gap the spotter speaks is the OFFICIAL classification gap. The other
// column, realTimeGap, is the plugin's own extrapolation from track positions
// (the in-game standings' "live gap" mode) — and it used to be PREFERRED over
// the official one wherever it was nonzero. On a logged race the two disagreed
// by ninety seconds on the same crossing: gap=3179 (3.2s, correct) against
// rt=91020 for the same rider on the same lap, spoken as "P two, ninety one
// point zero to rider one seventy four", and repeated by the flag and
// session-end lines. An estimate the plugin derived itself is not timing and
// has no business being read out as a fact.
TEST_CASE("spotter: gaps come from official timing, never the live estimate") {
    PluginHost host(dllPath());
    REQUIRE(host.loaded());
    host.startup("Z:\\tmp\\mxbmrp3-tests\\spotter-officialgap\\");
    host.spotterInstallPack(
        "[Cues]\nlap_completed = P {position}"
        "[, {gap_to_ahead} to {rider_ahead}].\n");
    host.eventInit("TestTrack", "Player");
    host.raceEvent("TestTrack");
    host.addEntry(12, "Player");     // first entry after EventInit is the player
    host.addEntry(476, "Rival");
    host.spotterEnable(true);
    host.spotterCategoryMask(0x1F);
    // Lap-based, so the session clock counts UP — which is what the live-gap
    // pipeline stamps its positions against (see trackpos_test.cpp).
    host.session(RACE1, /*numLaps=*/10, /*lengthMs=*/0);

    // The official gap columns say half a second between the two.
    const std::vector<ClassRow> grid = {
        { .num = 476, .laps = 0, .gap = 0 },
        { .num = 12,  .laps = 0, .gap = 500 },
    };
    // Drive the live-gap estimate to a figure that DISAGREES: the leader stamps
    // centreline 0.20 at t=1000, the player reaches it at t=3000, so the
    // estimate is 2.0s against the official 0.5s.
    host.classify(RACE1, 1000, grid);
    host.raceTrackPosition({ { 476, 0.20f }, { 12, 0.10f } });
    host.classify(RACE1, 3000, grid);
    host.raceTrackPosition({ { 476, 0.40f }, { 12, 0.20f } });
    REQUIRE(host.realTimeGap(12) == 2000);   // the wrong number is right there

    // A real stopwatch measurement: they cross the line, and 3.0s of SESSION
    // time later so do you. That is the only figure with a claim to being the
    // gap between you — and the clock only moves when a classification says so.
    host.classify(RACE1, 92000, grid);
    host.raceLap(RACE1, 476, 1, 92000);
    host.classify(RACE1, 95000, grid);
    host.raceLap(RACE1, 12, 1, 95000);
    host.classify(RACE1, 95000, {
        { .num = 476, .laps = 1, .gap = 0 },
        { .num = 12,  .laps = 1, .gap = 500 },
    });
    const std::string log = host.spotterCueLog();
    CHECK(log.find("three point zero to rider four seventy six") !=
          std::string::npos);
    // Neither of the two figures that were NOT measured: the live estimate
    // (2.0s, still sitting in the standings) and the difference between the
    // official gap columns (0.5s), which is a different measurement wearing
    // the same words.
    CHECK(log.find("two point zero") == std::string::npos);
    CHECK(log.find("zero point five") == std::string::npos);
}

// Nothing is REMAINING before the session is running: the whole distance is
// still ahead, so "distance minus laps completed" hands back the distance and a
// template reads it out as a countdown. A logged race left the pits forty-five
// seconds before the green and said "Pit exit, up to speed. Race 2, FOUR LAPS
// LEFT" — directly after session_prestart had said "Race 2 starting, four
// laps". {session_length} is the variable for that sentence.
TEST_CASE("spotter: what is left of a session waits for the session to start") {
    PluginHost host(dllPath());
    REQUIRE(host.loaded());
    host.startup("Z:\\tmp\\mxbmrp3-tests\\spotter-prestart-remaining\\");
    host.spotterInstallPack(
        "[Cues]\npit_exit_you = Pit exit[, {session_length} scheduled]"
        "[, {session_remaining} left].\n");
    host.eventInit("TestTrack", "Player");
    host.raceEvent("TestTrack");
    host.addEntry(12, "Player");
    host.spotterEnable(true);
    host.spotterCategoryMask(0x1F);

    // A four-lap race, still on the grid.
    const int PRE_START = 256, IN_PROGRESS = 16;
    host.session(RACE1, /*numLaps=*/4, /*lengthMs=*/0, /*state=*/PRE_START);
    host.classify(RACE1, 0, { { .num = 12, .laps = 0, .pit = 1 } }, PRE_START);
    host.classify(RACE1, 0, { { .num = 12, .laps = 0, .pit = 0 } }, PRE_START);
    std::string log = host.spotterCueLog();
    // The length is known and said; what is LEFT of it is not a thing yet.
    CHECK(log.find("Pit exit, four laps scheduled.") != std::string::npos);
    CHECK(log.find("left") == std::string::npos);

    // Green, and the same trip down pit lane now has an answer.
    host.raceSessionState(RACE1, IN_PROGRESS);
    host.classify(RACE1, 5000, { { .num = 12, .laps = 0, .pit = 1 } });
    host.classify(RACE1, 6000, { { .num = 12, .laps = 0, .pit = 0 } });
    log = host.spotterCueLog();
    CHECK(log.find("Pit exit, four laps scheduled, four laps left.") !=
          std::string::npos);
}

// "Waiting" is not a state anything entered — getSessionStateString() returns
// it for a state word with no known bit set, which is where the game parks
// between sessions. A logged race said "Race 2 complete, you finished P
// eleven", then ten seconds later "Race 2, WAITING": the plugin narrating its
// own enum, at the one moment the session had already been summed up.
TEST_CASE("spotter: the idle state between sessions is not an announcement") {
    PluginHost host(dllPath());
    REQUIRE(host.loaded());
    host.startup("Z:\\tmp\\mxbmrp3-tests\\spotter-idlestate\\");
    host.spotterInstallPack("[Cues]\nsession_state = {session_state}.\n");
    host.eventInit("TestTrack", "Player");
    host.raceEvent("TestTrack");
    host.addEntry(12, "Player");
    host.spotterEnable(true);
    host.spotterCategoryMask(0x1F);
    host.session(RACE1, /*numLaps=*/4, /*lengthMs=*/0, /*state=*/16);

    // Cancelled is a real state with a real bit, and still speaks.
    host.raceSessionState(RACE1, /*CANCELLED=*/2048);
    CHECK(host.spotterCueLog().find("Cancelled.") != std::string::npos);

    // 1024 has no bit in the table: the idle gap, and nothing to say about it.
    host.raceSessionState(RACE1, /*WAITING=*/1024);
    CHECK(host.spotterCueLog().find("Waiting.") == std::string::npos);
}

// {gained_on_ahead} and {trend_ahead} need the ahead gap to RESOLVE twice
// against the same rider, and the rider ahead is normally more than a sector up
// the road — so by the time you reach a point, they have already banked the
// next one. With one stored crossing per rider that was a miss every time: a
// real four-lap race resolved it on one crossing out of three, and no logged
// session ever spoke a trend or a delta. Their older crossings are kept now.
TEST_CASE("spotter: the ahead trend speaks across consecutive splits") {
    PluginHost host(dllPath());
    REQUIRE(host.loaded());
    host.startup("Z:\\tmp\\mxbmrp3-tests\\spotter-trend\\");
    host.spotterInstallPack(
        "[Cues]\nsector_completed = Sector {sector_number}"
        "[, {gap_to_ahead} to {rider_ahead}]"
        "[, {trend_ahead} {gained_on_ahead}].\n");
    host.eventInit("TestTrack", "Player");
    host.raceEvent("TestTrack");
    host.addEntry(12, "Player");     // first entry after EventInit is the player
    host.addEntry(476, "Rival");
    host.spotterEnable(true);
    host.spotterCategoryMask(0x1F);
    host.session(RACE1, /*numLaps=*/5, /*lengthMs=*/480000);
    const std::vector<ClassRow> grid = {
        { .num = 476, .laps = 0 }, { .num = 12, .laps = 0 },
    };
    // The gap is the difference in SESSION-ELAPSED time between the two
    // crossings, which a classification is what moves.
    auto at = [&](int elapsedMs) { host.classify(RACE1, 480000 - elapsedMs, grid); };

    at(0);
    host.raceSplit(RACE1, 476, 0, 0, 30000);   // they cross S1
    at(2000);
    host.raceSplit(RACE1, 476, 0, 1, 60000);   // ...and S2, well before you
    at(3000);
    host.raceSplit(RACE1, 12, 0, 0, 33000);    // your S1: 3.0s after theirs
    std::string log = host.spotterCueLog();
    CHECK(log.find("Sector one, three point zero to rider four seventy six.") !=
          std::string::npos);

    at(8000);
    host.raceSplit(RACE1, 12, 0, 1, 38000);    // your S2: 6.0s after theirs
    log = host.spotterCueLog();
    // Second resolution against the same rider, so the delta and the trend
    // have something to compare with: three seconds lost in one sector.
    // PAST tense, verb first -- the delta is a completed change between two
    // timing points, and "losing three point zero" read as a rate.
    CHECK(log.find("Sector two, six point zero to rider four seventy six, "
                   "lost three point zero.") != std::string::npos);
}

// Who "you" is comes from the CAMERA, not from whoever was last spectated.
// The spectate target is written only while spectating and cleared only when
// the event ends, so watching a rival in practice and then riding qualifying
// yourself left the spotter addressing that rival as "you" for the rest of the
// event — your own penalties and your own flag went quiet (a rival's are
// default-quiet by design) while theirs were announced as yours.
TEST_CASE("spotter: riding after spectating makes you the subject again") {
    PluginHost host(dllPath());
    REQUIRE(host.loaded());
    host.startup("Z:\\tmp\\mxbmrp3-tests\\spotter-refocus\\");
    host.spotterInstallPack(
        "[Cues]\npenalty_you = Your penalty, {penalty_seconds}.\n"
        "penalty_other = Penalty for {event_rider}.\n");
    host.eventInit("TestTrack", "Player");
    host.raceEvent("TestTrack");
    host.addEntry(12, "Player");     // first entry after EventInit is the player
    host.addEntry(476, "Rival");
    host.addEntry(7, "Other");
    host.spotterEnable(true);
    host.spotterCategoryMask(0x1F);
    host.session(RACE1, /*numLaps=*/5);
    host.classify(RACE1, 60000, { { .num = 476, .laps = 2 },
                                  { .num = 12, .laps = 2 },
                                  { .num = 7, .laps = 2 } });

    // Practice: the camera is on the rival, so a penalty of THEIRS is yours to
    // hear about in the first person — that is what spectating means here.
    host.drawWithState(/*SPECTATE=*/1);
    host.spectateVehicles({ { 476, "Rival" }, { 12, "Player" } }, 0);
    host.communication(476, 0, /*communication=*/2, /*penaltySeconds=*/5);
    CHECK(host.spotterCueLog().find("Your penalty, five seconds.") !=
          std::string::npos);

    // Now you are riding. The spectate target is never cleared, so this is
    // exactly the state the bug lived in.
    host.drawWithState(/*ON_TRACK=*/0);
    host.communication(12, 0, /*communication=*/2, /*penaltySeconds=*/10);
    const std::string log = host.spotterCueLog();
    CHECK(log.find("Your penalty, ten seconds.") != std::string::npos);
    // ...and somebody else's is somebody else's. (A fresh rider, because a
    // SECOND penalty for one who already has one is a PenaltyChange, which is
    // its own cue and default-quiet for a rival.)
    host.communication(7, 0, /*communication=*/2, /*penaltySeconds=*/15);
    CHECK(host.spotterCueLog().find("Penalty for rider seven.") !=
          std::string::npos);
}

// Everything the manager caches about a run has to end with the run. Each of
// these survived a green flag and spoke in the session after it: the last
// resolved gap still carried its trend, the last split was what the next
// sector time got subtracted from, and a lap armed at a crossing with no
// classification behind it flushed into the next session's first one.
TEST_CASE("spotter: a new session does not speak the last one's numbers") {
    PluginHost host(dllPath());
    REQUIRE(host.loaded());
    host.startup("Z:\\tmp\\mxbmrp3-tests\\spotter-sessionreset\\");
    host.spotterInstallPack(
        "[Cues]\nlap_completed = P {position}[, {last_lap_time}]"
        "[, {trend_ahead} {gained_on_ahead}].\n"
        "sector_completed = Sector {sector_number}, {event_time}.\n");
    host.eventInit("TestTrack", "Player");
    host.raceEvent("TestTrack");
    host.addEntry(12, "Player");
    host.addEntry(476, "Rival");
    host.spotterEnable(true);
    host.spotterCategoryMask(0x1F);

    // Race 1: build a trend to #476 across two crossings, then arm a lap
    // report and never deliver the classification that would speak it.
    host.session(RACE1, /*numLaps=*/5, /*lengthMs=*/480000);
    const std::vector<ClassRow> grid = {
        { .num = 476, .laps = 0 }, { .num = 12, .laps = 0 },
    };
    auto at = [&](int elapsedMs) { host.classify(RACE1, 480000 - elapsedMs, grid); };
    at(0);
    host.raceSplit(RACE1, 476, 0, 0, 30000);
    at(2000);
    host.raceSplit(RACE1, 476, 0, 1, 60000);
    at(3000);
    host.raceSplit(RACE1, 12, 0, 0, 33000);
    at(8000);
    host.raceSplit(RACE1, 12, 0, 1, 38000);
    REQUIRE(host.spotterCueLog().find("Sector two") != std::string::npos);
    host.raceLap(RACE1, 12, 1, 95000);       // armed, never flushed

    // Green flag on race 2, then your first crossing of it.
    const int RACE2 = 7;
    host.session(RACE2, /*numLaps=*/5, /*lengthMs=*/480000);
    host.classify(RACE2, 480000, { { .num = 476, .laps = 0 },
                                   { .num = 12, .laps = 0 } });
    const std::string log = host.spotterCueLog();
    // The stale lap report did not arrive with the new session's first
    // classification...
    CHECK(log.find("one thirty five") == std::string::npos);
    // ...and no trend from a race that is over.
    CHECK(log.find("losing") == std::string::npos);
    CHECK(log.find("gaining") == std::string::npos);

    // The first sector of the new session is measured from zero, not from the
    // 38.0s cumulative the last one ended on.
    host.classify(RACE2, 455000, { { .num = 476, .laps = 0 },
                                   { .num = 12, .laps = 0 } });
    host.raceSplit(RACE2, 12, 0, 0, 25000);
    CHECK(host.spotterCueLog().find("Sector one, twenty five point zero") !=
          std::string::npos);
}

// The same carry-over, reached by the OTHER door: the caller is muted across
// the boundary rather than merely uninterested in the category. onRaceEvent
// returns immediately when both switches are off, and that return sat ABOVE
// the SessionStarted branch that is the only place per-session state is wiped
// — so mute, change session, unmute, and race 1's split and armed lap were
// still loaded. The category gate's own comment warned about exactly this and
// was answered for the category only.
TEST_CASE("spotter: muting it across a session boundary still clears the session") {
    PluginHost host(dllPath());
    REQUIRE(host.loaded());
    host.startup("Z:\\tmp\\mxbmrp3-tests\\spotter-mutedreset\\");
    host.spotterInstallPack(
        "[Cues]\nlap_completed = P {position}[, {last_lap_time}].\n"
        "sector_completed = Sector {sector_number}, {event_time}.\n");
    host.eventInit("TestTrack", "Player");
    host.raceEvent("TestTrack");
    host.addEntry(12, "Player");
    host.spotterEnable(true);
    host.spotterCategoryMask(0x1F);

    const int RACE1 = 6, RACE2 = 7;
    host.session(RACE1, /*numLaps=*/5, /*lengthMs=*/480000);
    host.classify(RACE1, 480000, { { .num = 12, .laps = 0 } });
    host.raceSplit(RACE1, 12, 0, 0, 33000);
    host.raceSplit(RACE1, 12, 0, 1, 38000);
    host.raceLap(RACE1, 12, 1, 95000);       // armed, never flushed

    // Muted for the whole transition — both switches, which is what makes the
    // early return fire.
    host.spotterEnable(false);
    host.spotterSubtitles(false);
    host.session(RACE2, /*numLaps=*/5, /*lengthMs=*/480000);
    host.spotterEnable(true);

    host.classify(RACE2, 480000, { { .num = 12, .laps = 0 } });
    const std::string log = host.spotterCueLog();
    CHECK(log.find("one thirty five") == std::string::npos);   // the stale lap
    host.classify(RACE2, 455000, { { .num = 12, .laps = 0 } });
    host.raceSplit(RACE2, 12, 0, 0, 25000);
    // 25.0, not the 38.0 cumulative the muted session ended on.
    CHECK(host.spotterCueLog().find("Sector one, twenty five point zero") !=
          std::string::npos);
}

// The refinement fallback reaches the AUDIO, not only the words. A pack that
// writes the general key alone is documented to cover every kind of it
// (spotter_cue_pack.h), and it did — for the phrase, while the wav lookup
// missed and the cue dropped to TTS, i.e. to silence on Wine/Proton.
TEST_CASE("spotter: a general key's recording covers the kinds that refine it") {
    PluginHost host(dllPath());
    REQUIRE(host.loaded());
    host.startup("Z:\\tmp\\mxbmrp3-tests\\spotter-wavfallback\\");
    // No practice_started row of either sort - only the general key.
    host.spotterInstallPack(
        "[Cues]\nsession_started = {session_name} underway.\n"
        "session_started_wav = green.wav\n");
    host.eventInit("TestTrack", "Player");
    host.raceEvent("TestTrack");
    host.addEntry(12, "Player");
    host.spotterEnable(true);
    host.spotterCategoryMask(0x1F);

    const int PRACTICE = 1;   // PiBoSo session 1
    host.session(PRACTICE, /*numLaps=*/0, /*lengthMs=*/600000);
    CHECK(host.spotterCueLog().find("underway") != std::string::npos);
    const std::string route = host.spotterLastAudio();
    // The key really did refine to the practice variant (else this case proves
    // nothing about the fallback)...
    CHECK(route.find("practice_started|") == 0);
    // ...and the general key's recording came with it, rather than TTS.
    CHECK(route.find("wav:green.wav") != std::string::npos);
}

// THE ROUTE SEAM ANSWERS FOR EVERY CUE, not only the audible ones. It was
// recorded after both of emitCue's returns -- the silent one and the
// spoken-audio gate -- so "silent" was unreachable and a subtitles-only session
// reported whatever the last audible cue had chosen. A seam that goes stale is
// worse than no seam: a test reads the previous cue's answer as this cue's.
TEST_CASE("spotter: the audio route is recorded for silent and subtitles-only cues") {
    PluginHost host(dllPath());
    REQUIRE(host.loaded());
    host.startup("Z:\\tmp\\mxbmrp3-tests\\spotter-route\\");
    host.spotterInstallPack(
        "[Cues]\nsession_started = {session_name} underway.\n"
        "session_started_wav = green.wav\n"
        "gate_drop =\n");            // an EMPTY value is an explicit mute
    host.eventInit("TestTrack", "Player");
    host.raceEvent("TestTrack");
    host.addEntry(12, "Player");
    host.spotterCategoryMask(0x1F);

    // SUBTITLES ONLY: the words are shown, no audio is dispatched — and the route
    // still says what WOULD have played, which is the question a Wine user has.
    host.spotterEnable(false);
    host.spotterSubtitles(true);
    host.session(RACE1, /*numLaps=*/5, /*lengthMs=*/0, /*state=*/256);   // pre-start
    host.raceSessionState(RACE1, 16);                                    // green
    CHECK(host.spotterCueLog().find("underway") != std::string::npos);
    CHECK(host.spotterLastAudio() == "session_started|wav:green.wav");

    // A MUTED ROW resolves to nothing at all, and says so rather than leaving the
    // previous cue's answer standing.
    host.classify(RACE1, 0, { { .num = 12, .laps = 0 } }, /*sessionState=*/32);
    host.classify(RACE1, 0, { { .num = 12, .laps = 0 } }, /*sessionState=*/16);
    CHECK(host.spotterLastAudio() == "gate_drop|silent");

    // A CATEGORY-MUTED cue is the third exit, and it answers too. Nothing reads
    // this today; it is swept with the other two because "no test reads it" is a
    // fact about today's tests and not about the seam.
    host.spotterCategoryMask(0);
    host.raceSessionState(RACE1, 512);      // session over -> a General cue
    // The race wrap-up is HELD until the next classification when the subject
    // has not finished (so the finish cue can speak before "that's the race
    // done"); drive one so the held cue reaches the category gate and records
    // its route.
    host.classify(RACE1, 0, { { .num = 12, .laps = 0 } }, /*sessionState=*/512);
    CHECK(host.spotterLastAudio().find("|muted") != std::string::npos);
}

// THE TWO AXES ARE NOT THE SAME AXIS. The refinement fallback (practice_started
// -> session_started) must not fire when a VARIANT was chosen: applied there it
// walks past the variant's own parent to the general key, so a pack that wrote
// `practice_started_wav` and a `practice_started_2` text variant played the
// session_started clip under the practice_started_2 subtitle. Audio and words
// describing different things is worse than falling back to TTS, which is what a
// variant with no audio of its own is supposed to do.
TEST_CASE("spotter: a variant does not borrow the general key's recording") {
    PluginHost host(dllPath());
    REQUIRE(host.loaded());
    host.startup("Z:\\tmp\\mxbmrp3-tests\\spotter-variantwav\\");
    host.spotterInstallPack(
        "[Cues]\nsession_started = {session_name} underway.\n"
        "session_started_wav = green.wav\n"
        // The refined key, with its OWN clip and ONE text variant. The variant has
        // no audio, so the only two candidates are practice_started_wav (more
        // specific, and deliberately not inherited across the variant axis) and
        // session_started_wav (less specific, and must not win).
        "practice_started = Practice underway.\n"
        "practice_started_wav = practice.wav\n"
        "practice_started_2 = Practice session is go.\n");
    host.eventInit("TestTrack", "Player");
    host.raceEvent("TestTrack");
    host.addEntry(12, "Player");
    host.spotterEnable(true);
    host.spotterCategoryMask(0x1F);

    const int PRACTICE = 1;
    host.session(PRACTICE, /*numLaps=*/0, /*lengthMs=*/600000);
    const std::string route = host.spotterLastAudio();
    CHECK(host.spotterCueLog().find("ractice") != std::string::npos);
    // Whichever variant the RNG picked, the general key's clip is never the answer.
    CHECK_MESSAGE(route.find("wav:green.wav") == std::string::npos,
                  "a practice_started variant borrowed session_started's recording: " << route);
    // And the base key still gets its own, which is what stops this passing by
    // simply having broken the fallback again.
    CHECK((route == "practice_started|wav:practice.wav" ||
           route == "practice_started_2|tts"));
}

// THE CLOCK IS THE CLOCK, whatever the session is called. The timed milestones
// ("ten minutes to go", "five minutes remaining") rode a pd.isRaceSession()
// gate, so the one session they matter most in -- QUALIFYING, where the clock is
// the whole event -- never heard them. This branch un-gated the sector cue and
// the lap report for exactly that reason and missed this one, while the cue
// registry has always described the key as "ten minutes of the session left"
// with no race qualifier. The lap-race halfway_point stays race-only: it needs a
// leader and a distance, which practice has neither of.
TEST_CASE("spotter: the timed milestones fire in qualifying too") {
    PluginHost host(dllPath());
    REQUIRE(host.loaded());
    host.startup("Z:\\tmp\\mxbmrp3-tests\\spotter-qualimile\\");
    host.spotterInstallPack("[Cues]\nten_minutes_remaining = Ten minutes to go.\n");
    host.eventInit("TestTrack", "Player");
    host.raceEvent("TestTrack");
    host.addEntry(12, "Player");
    host.spotterEnable(true);
    host.spotterCategoryMask(0x1F);

    // PiBoSo session 3 = qualifying, 20 minutes on the clock. The milestone is
    // edge-triggered off the remaining time, so drive the clock across it.
    const int QUALI = 3;
    host.session(QUALI, /*numLaps=*/0, /*lengthMs=*/1200000);
    // The milestones ride the TRACK-POSITION batch for its clock, so each step is
    // classify (which sets the clock) followed by a batch (which reads it). The
    // clock counts DOWN in a timed session; the threshold is on elapsed.
    auto tick = [&](int remainingMs) {
        host.classify(QUALI, remainingMs, { { .num = 12, .laps = 0 } });
        host.raceTrackPosition({ { .num = 12, .trackPos = 0.5f } });
    };
    tick(1200000);
    tick(601000);    // 9:59 elapsed -- just short
    tick(599000);    // 10:01 elapsed -- crosses
    CHECK(host.spotterCueLog().find("Ten minutes to go") != std::string::npos);
}

// A lap on the exact minute, through the real callback path. 1:00.4 composes
// to 100, and 100 as a number is "one hundred" — so the spotter announced a
// personal best as "one hundred point four" while the lap either side of it
// read "one oh eight point nine". Both backends were wrong together, which is
// the only good thing about it.
TEST_CASE("spotter: a lap time on the minute reads like its neighbours") {
    PluginHost host(dllPath());
    REQUIRE(host.loaded());
    host.startup("Z:\\tmp\\mxbmrp3-tests\\spotter-minute\\");
    host.spotterInstallPack(
        "[Cues]\nfastest_lap_other = Fastest lap, {event_rider}, {event_time}."
        "\nlap_completed = P {position}, {last_lap_time}.\n");
    host.eventInit("TestTrack", "Player");
    host.raceEvent("TestTrack");
    host.addEntry(12, "Player");
    host.addEntry(476, "Rival");
    host.spotterEnable(true);
    host.spotterCategoryMask(0x1F);
    host.session(RACE1, /*numLaps=*/10);

    // Via the event's own number, which the fastest-lap cue reads back.
    host.raceLap(RACE1, 476, 1, 60412, /*best=*/2);
    host.classify(RACE1, 60000, { { .num = 476, .laps = 1 },
                                  { .num = 12, .laps = 0 } });
    std::string log = host.spotterCueLog();
    CHECK(log.find("one oh oh point four") != std::string::npos);
    CHECK(log.find("one hundred") == std::string::npos);

    // ...and via the standings' milliseconds, which {last_lap_time} reads.
    host.classify(RACE1, 30000, { { .num = 476, .laps = 1 },
                                  { .num = 12, .laps = 0 } });
    host.raceLap(RACE1, 12, 1, 120300);
    host.classify(RACE1, 150000, { { .num = 476, .laps = 1 },
                                   { .num = 12, .laps = 1 } });
    log = host.spotterCueLog();
    CHECK(log.find("two oh oh point three") != std::string::npos);
    CHECK(log.find("two hundred") == std::string::npos);
}

// The cue log stamps every callout with getSessionElapsedTime(), and the
// subtitle order is that stamp — so it has to ascend. Through PRE_START and
// the sighting lap the game sends a NEGATIVE countdown on a timed session, so
// `length - time` came out ABOVE the session length: a ten-minute race whose
// grid cues were stamped past its own finish, then dropped to ~0 at the green.
// Overtime's negative clock is a different thing and stays uncapped — bonus
// laps take real time, so elapsed passing the length is true there.
TEST_CASE("spotter: cue timestamps ascend across the grid and the green") {
    PluginHost host(dllPath());
    REQUIRE(host.loaded());
    host.startup("Z:\\tmp\\mxbmrp3-tests\\spotter-clock\\");
    host.spotterInstallPack(
        "[Cues]\npenalty_you = Penalty, {penalty_seconds}.\n");
    host.eventInit("TestTrack", "Player");
    host.raceEvent("TestTrack");
    host.addEntry(12, "Player");
    host.spotterEnable(true);
    host.spotterCategoryMask(0x1F);

    // Ten minutes, and thirty seconds of grid hold still to run.
    const int PRE_START = 256, IN_PROGRESS = 16;
    host.session(RACE1, /*numLaps=*/0, /*lengthMs=*/600000, PRE_START);
    host.classify(RACE1, /*sessionTimeMs=*/-30000,
                  { { .num = 12, .laps = 0 } }, PRE_START);
    host.communication(12, 0, /*communication=*/2, /*penaltySeconds=*/5);

    // Green, and a minute of the race gone.
    host.raceSessionState(RACE1, IN_PROGRESS);
    host.classify(RACE1, /*sessionTimeMs=*/540000,
                  { { .num = 12, .laps = 1 } }, IN_PROGRESS);
    host.communication(12, 0, /*communication=*/1, /*penaltySeconds=*/0);
    host.communication(12, 0, /*communication=*/2, /*penaltySeconds=*/10);

    // Every stamp in order, whatever the cues happen to be.
    const std::string log = host.spotterCueLog();
    int prev = -1, seen = 0;
    for (size_t i = 0; i < log.size();) {
        const size_t tab = log.find('\t', i);
        const size_t nl = log.find('\n', i);
        if (tab == std::string::npos || nl == std::string::npos) break;
        const int ms = std::stoi(log.substr(i, tab - i));
        CAPTURE(log.substr(tab + 1, nl - tab - 1));
        CHECK(ms >= prev);
        CHECK(ms <= 600000);   // never past a race that has not run yet
        prev = ms;
        ++seen;
        i = nl + 1;
    }
    CHECK(seen >= 2);   // a scan that matched nothing would pass vacuously
}

// Joining a lobby mid-session: the server replays every rider's whole lap
// history at once, and each replayed lap that improved the overall best raised
// a FastestLap event. A real join announced rider ten three times in ONE
// millisecond — "one twenty two point five", "one thirteen point eight", "one
// oh three point six" — then two more riders twice each. Seven callouts, none
// of them a moment the player was there for, and the first six already
// superseded by the seventh before anyone heard them.
//
// The whole replay lands inside ONE callback batch, which is what makes it
// separable from real laps at all — and the classification that follows is
// what speaks the survivor. Nothing else flushes it: not a frame, not a
// track-position batch, not another event. The clock values below are the
// cue's timestamps, not a condition; a second classification at the same
// instant would flush just the same.
TEST_CASE("spotter: a joined session's lap history is one callout, not seven") {
    PluginHost host(dllPath());
    REQUIRE(host.loaded());
    host.startup("Z:\\tmp\\mxbmrp3-tests\\spotter-join\\");
    host.spotterInstallPack(
        "[Cues]\nfastest_lap_other = Fastest lap, {event_rider}, {event_time}.\n");
    host.eventInit("TestTrack", "Player");
    host.raceEvent("TestTrack");
    host.addEntry(12, "Player");
    host.addEntry(10, "Ten");
    host.addEntry(429, "FourTwentyNine");
    host.spotterEnable(true);
    host.spotterCategoryMask(0x1F);
    host.session(RACE1, /*numLaps=*/20);

    // A classification before the replay, so "since the last one" has a start.
    host.classify(RACE1, 60000, { { .num = 429, .laps = 7 },
                                  { .num = 10, .laps = 7 },
                                  { .num = 12, .laps = 0 } });

    // The replay, exactly as the log had it: consecutive laps for one rider,
    // then another rider, with no classification between any of them.
    host.raceLap(RACE1, 10, 1, 82591, /*best=*/2);
    host.raceLap(RACE1, 10, 2, 73830, /*best=*/2);
    host.raceLap(RACE1, 10, 4, 63664, /*best=*/2);
    host.raceLap(RACE1, 429, 7, 59600, /*best=*/2);
    // Nothing said yet: no classification has arrived to carry it.
    CHECK(host.spotterCueLog().find("Fastest lap") == std::string::npos);

    host.classify(RACE1, 61000, { { .num = 429, .laps = 7 },
                                  { .num = 10, .laps = 7 },
                                  { .num = 12, .laps = 0 } });
    const std::string log = host.spotterCueLog();
    // One line, and it is the session's actual fastest — the last of the
    // replay, because the event only fires when the overall best improves.
    CHECK(log.find("Fastest lap, rider four twenty nine, fifty nine point six.")
          != std::string::npos);
    // Not the laps it superseded.
    CHECK(log.find("one twenty two point five") == std::string::npos);
    CHECK(log.find("one thirteen point eight") == std::string::npos);
    CHECK(log.find("one oh three point six") == std::string::npos);

    // And a genuine fastest lap after that still speaks, on its own
    // classification.
    host.raceLap(RACE1, 10, 8, 58000, /*best=*/2);
    host.classify(RACE1, 62000, { { .num = 10, .laps = 8 },
                                  { .num = 429, .laps = 7 },
                                  { .num = 12, .laps = 0 } });
    CHECK(host.spotterCueLog().find("Fastest lap, rider ten, fifty eight point"
                                    " zero.") != std::string::npos);
}

// Proximity is not distance along the racing line. On a wide straight or a
// split line, a rider ten metres back down the centreline can be twenty metres
// ACROSS it — the Radar HUD filters on straight-line distance and correctly
// shows them as nowhere near, while "Rider behind" measured the along-track
// figure alone and announced them anyway. That mismatch is what made the calls
// feel unrelated to what is on screen.
TEST_CASE("spotter: a rider across the track is not a rider behind you") {
    PluginHost host(dllPath());
    REQUIRE(host.loaded());
    host.startup("Z:\\tmp\\mxbmrp3-tests\\spotter-lateral\\");
    host.spotterInstallPack(
        "[Cues]\nrider_behind = Rider behind.\nrider_left = Rider left.\n"
        "rider_right = Rider right.\n");
    host.eventInit("TestTrack", "Player");
    host.raceEvent("TestTrack");
    host.addEntry(12, "Player");
    host.addEntry(476, "Rival");
    host.spotterEnable(true);
    host.spotterCategoryMask(0x1F);
    host.session(RACE1, /*numLaps=*/10);
    host.classify(RACE1, 60000, { { .num = 12, .laps = 1 },
                                  { .num = 476, .laps = 1 } });
    host.raceSessionState(RACE1, /*IN_PROGRESS=*/16);

    // The track is 1600m in the harness, so 0.005 of a lap is 8m back — well
    // inside the 12m behind threshold. Placed ACROSS the track: the player at
    // the origin facing north (yaw 0), the rival 25m to the +X side.
    host.raceTrackPosition({
        { .num = 12,  .trackPos = 0.500f, .crashed = 0, .posX = 0.0f,  .posZ = 0.0f },
        { .num = 476, .trackPos = 0.495f, .crashed = 0, .posX = 25.0f, .posZ = 0.0f },
    });
    CHECK(host.spotterCueLog().find("Rider behind.") == std::string::npos);
    CHECK(host.spotterCueLog().find("Rider right.") == std::string::npos);

    // The same along-track gap, now on your line: that is a rider behind you.
    host.raceTrackPosition({
        { .num = 12,  .trackPos = 0.500f, .crashed = 0, .posX = 0.0f, .posZ = 0.0f },
        { .num = 476, .trackPos = 0.495f, .crashed = 0, .posX = 2.0f, .posZ = 0.0f },
    });
    CHECK(host.spotterCueLog().find("Rider behind.") != std::string::npos);
}

// {penalty_total} on penalty_you is the one place the standings column cannot
// answer: the communication announcing a penalty IS how the plugin learns of
// it, and the classification that absorbs it arrives afterwards. Read straight
// off the column, "Penalty, ten seconds" would be followed by a total that
// still described the race before it. The cue keeps its own tally, and this
// pins the three things that tally has to get right.
TEST_CASE("spotter: your penalty total includes the penalty being announced") {
    PluginHost host(dllPath());
    REQUIRE(host.loaded());
    host.startup("Z:\\tmp\\mxbmrp3-tests\\spotter-penaltytotal\\");
    host.spotterInstallPack(
        "[Cues]\npenalty_you = Penalty, {penalty_seconds}"
        "[, {penalty_total} in total].\n");
    host.eventInit("TestTrack", "Player");
    host.raceEvent("TestTrack");
    host.addEntry(12, "Player");
    host.spotterEnable(true);
    host.spotterCategoryMask(0x1F);
    host.session(RACE1, /*numLaps=*/10);
    host.classify(RACE1, 60000, { { .num = 12, .laps = 1, .penalty = 0 } });
    host.raceSessionState(RACE1, /*IN_PROGRESS=*/16);

    // FIRST penalty: the total is the amount, so the optional group drops
    // rather than saying the same figure twice.
    host.communication(12, 0, /*communication=*/2, /*penaltySeconds=*/5);
    CHECK(host.spotterCueLog().find("Penalty, five seconds.") !=
          std::string::npos);
    CHECK(host.spotterCueLog().find("in total") == std::string::npos);

    // SECOND penalty with no classification in between — the column still
    // reads zero, and the answer has to come from the tally.
    host.communication(12, 0, /*communication=*/2, /*penaltySeconds=*/10);
    CHECK(host.spotterCueLog().find(
              "Penalty, ten seconds, fifteen seconds in total.") !=
          std::string::npos);

    // The column catches up. Both sources now know about the fifteen, and the
    // next penalty must be added to it ONCE.
    host.classify(RACE1, 120000, { { .num = 12, .laps = 2, .penalty = 15000 } });
    host.communication(12, 0, /*communication=*/2, /*penaltySeconds=*/5);
    CHECK(host.spotterCueLog().find(
              "Penalty, five seconds, twenty seconds in total.") !=
          std::string::npos);
}

// The other order: a classification that already absorbed the penalty arrives
// BEFORE the communication announcing it. The tally then agrees with the
// column, the total is again just the amount, and the group must still drop —
// which is why emitCue lets this cue own {penalty_total} even when it owns it
// as empty. Filled from the column by the ambient pass instead, the shipped
// line reads "Penalty, five seconds, five seconds in total".
TEST_CASE("spotter: a first penalty the standings already knew stays unsaid") {
    PluginHost host(dllPath());
    REQUIRE(host.loaded());
    host.startup("Z:\\tmp\\mxbmrp3-tests\\spotter-penaltyfirst\\");
    host.spotterInstallPack(
        "[Cues]\npenalty_you = Penalty, {penalty_seconds}"
        "[, {penalty_total} in total].\n");
    host.eventInit("TestTrack", "Player");
    host.raceEvent("TestTrack");
    host.addEntry(12, "Player");
    host.spotterEnable(true);
    host.spotterCategoryMask(0x1F);
    host.session(RACE1, /*numLaps=*/10);
    host.classify(RACE1, 60000, { { .num = 12, .laps = 1, .penalty = 5000 } });
    host.raceSessionState(RACE1, /*IN_PROGRESS=*/16);

    host.communication(12, 0, /*communication=*/2, /*penaltySeconds=*/5);
    CHECK(host.spotterCueLog().find("Penalty, five seconds.") !=
          std::string::npos);
    CHECK(host.spotterCueLog().find("in total") == std::string::npos);
}

// ============================================================================
// ALTERNATES, end to end against the SHIPPED pack.
//
// The pack now carries `<key>_2..` rows on the cues you hear most, and nothing
// else in this suite exercises the roll — every other case pins the base row
// on purpose (PluginHost::pinBaseVariant), because an unpinned exact-match
// assertion on a cue with alternates is a one-in-N flake.
//
// lap_invalidated is the cue to test it with: three rows, none of which
// contains a {variable}, so two firings differ if and only if they chose
// different rows. Read from the file rather than spelled out here — a test
// that duplicates the wording breaks on every reword and teaches the next
// author to skip it.
// ============================================================================
TEST_CASE("spotter: the shipped pack's alternates are all reachable, and roll") {
    // The value of one row of the shipped ini, by exact key.
    auto shippedRow = [](const std::string& key) {
        const std::string ini = PluginHost::readShippedPack();
        std::istringstream lines(ini);
        std::string line;
        while (std::getline(lines, line)) {
            const size_t eq = line.find('=');
            if (eq == std::string::npos) continue;
            size_t b = line.find_first_not_of(" \t");
            if (b == std::string::npos || line[b] == ';') continue;
            size_t e = line.find_last_not_of(" \t\r", eq - 1);
            if (line.substr(b, e - b + 1) != key) continue;
            const size_t vb = line.find_first_not_of(" \t", eq + 1);
            if (vb == std::string::npos) return std::string();
            const size_t ve = line.find_last_not_of(" \t\r");
            return line.substr(vb, ve - vb + 1);
        }
        return std::string();
    };
    const std::vector<std::string> rows = {
        shippedRow("lap_invalidated"),
        shippedRow("lap_invalidated_2"),
        shippedRow("lap_invalidated_3"),
    };
    for (const std::string& r : rows) {
        REQUIRE_MESSAGE(!r.empty(), "shipped pack lost a lap_invalidated row");
        // A {variable} here would make "two firings differ" mean "the race
        // moved on", not "a different row was chosen".
        REQUIRE(r.find('{') == std::string::npos);
    }

    PluginHost host(dllPath());
    REQUIRE(host.loaded());
    host.startup("Z:\\tmp\\mxbmrp3-tests\\spotter-alternates\\");
    host.spotterInstallShippedPack();
    host.eventInit("TestTrack", "Player");
    host.raceEvent("TestTrack");
    host.addEntry(12, "Player");
    host.spotterEnable(true);
    host.spotterCategoryMask(0x1F);

    const int PRACTICE = 1;
    host.session(PRACTICE, 0);
    host.classify(PRACTICE, 0, { { .num = 12, .laps = 0 } });
    int lap = 0;
    auto strikeOutALap = [&host, &lap, PRACTICE]() {
        ++lap;
        host.raceLap(PRACTICE, 12, lap, 95000, /*best=*/0, /*split0=*/-1,
                     /*split1=*/-1, /*invalid=*/true);
        host.classify(PRACTICE, 95000 * lap, { { .num = 12, .laps = lap } });
    };
    // How many times a given row appears in the WHOLE log. Counting rather
    // than reading the newest line: a lap crossing emits more than one cue
    // (lap_completed rides the same moment), so "the last thing said" is not
    // reliably the one under test. All three rows are variable-free, so a
    // count is exact.
    auto timesSaid = [&host](const std::string& row) {
        const std::string log = host.spotterCueLog();
        int n = 0;
        for (size_t i = log.find(row); i != std::string::npos;
             i = log.find(row, i + row.size())) {
            ++n;
        }
        return n;
    };

    // The lap OUT of the gate is not struck out — there is no completed lap
    // behind it to invalidate — so burn one before measuring, the way the
    // lap_invalidated subcase above does. Without it the first pinned firing
    // measures nothing and reads as an unreachable variant.
    strikeOutALap();

    // Every row is REACHABLE. A `_4` with no `_3` would leave one of these
    // speaking the row below it — the census pins the numbering in the file,
    // this pins that the plugin honours it.
    for (size_t i = 0; i < rows.size(); ++i) {
        const int before = timesSaid(rows[i]);
        host.spotterPinVariant(static_cast<int>(i));
        strikeOutALap();
        CAPTURE(i);
        CHECK(timesSaid(rows[i]) == before + 1);
    }

    // ...and unpinned it actually varies. Three rows over twenty firings: the
    // one-row outcome is a 1e-9 event, and the generator is deterministic
    // anyway, so this is not a flaky assertion dressed up as a statistical one.
    host.spotterPinVariant(-1);
    std::vector<int> before;
    for (const std::string& r : rows) before.push_back(timesSaid(r));
    const int kFirings = 20;
    for (int i = 0; i < kFirings; ++i) strikeOutALap();

    int gained = 0, distinct = 0;
    for (size_t i = 0; i < rows.size(); ++i) {
        const int d = timesSaid(rows[i]) - before[i];
        gained += d;
        if (d > 0) ++distinct;
    }
    // Every firing chose SOME row of the set — nothing fell through to
    // silence or to a fourth line from somewhere else...
    CHECK(gained == kFirings);
    // ...and not always the same one.
    CHECK(distinct > 1);

    // Leave the pin where every other case expects it: the plugin is a
    // singleton and outlives this PluginHost, so an un-restored roll would
    // reach whatever case runs next.
    host.spotterPinVariant(0);
}

// ============================================================================
// A TREND BELONGS TO THE RIDER IT WAS MEASURED AGAINST.
//
// The cached pace report outlives the crossing that produced it -- the ambient
// variables serve it between timing points, which is the whole point of caching
// it. The GAPS have always checked Gap::raceNum before serving one; the TRENDS
// did not, so {gained_on_ahead} and {trend_ahead} came from the last rider
// measured while {rider_ahead} came from the live standings, and a template
// naming both described two different people.
//
// Found in a real race log, not here: five consecutive splits across two laps
// said "two point four gaining on rider twenty nine" -- one reading taken
// against rider twelve, re-attributed four times. A gap delta repeating to the
// tenth is impossible, so it was audible; nothing in the code could see it.
// ============================================================================
TEST_CASE("spotter: a stale trend is not re-attributed to a new rider ahead") {
    PluginHost host(dllPath());
    REQUIRE(host.loaded());
    host.startup("Z:\\tmp\\mxbmrp3-tests\\spotter-trendattr\\");
    // Name both halves in one line: if they can disagree, this says so.
    host.spotterInstallPackOver(
        "[Cues]\nlap_completed = P {position}"
        "[, {trend_ahead} {gained_on_ahead} on {rider_ahead}].\n");
    host.eventInit("TestTrack", "Player");
    host.raceEvent("TestTrack");
    host.addEntry(12, "Player");
    host.addEntry(29, "RivalA");
    host.addEntry(76, "RivalB");
    host.spotterEnable(true);
    host.spotterCategoryMask(0x1F);
    host.session(RACE1, /*numLaps=*/10, /*lengthMs=*/600000);

    // Two crossings against rider 29 ahead, so a trend against 29 resolves.
    for (int lap = 1; lap <= 2; ++lap) {
        host.classify(RACE1, 600000 - lap * 60000, {
            { .num = 29, .laps = lap }, { .num = 12, .laps = lap }, { .num = 76, .laps = lap - 1 },
        });
        host.raceLap(RACE1, 29, lap, 100000);
        host.classify(RACE1, 600000 - lap * 60000 - 2000, {
            { .num = 29, .laps = lap }, { .num = 12, .laps = lap }, { .num = 76, .laps = lap - 1 },
        });
        host.raceLap(RACE1, 12, lap, 100000 + lap * 400);
    }

    // Now rider 76 is the one ahead -- a DIFFERENT rider, with no resolved
    // reading of their own. The line must not hand them 29's delta.
    host.classify(RACE1, 420000, {
        { .num = 76, .laps = 3 }, { .num = 12, .laps = 3 }, { .num = 29, .laps = 2 },
    });
    host.raceLap(RACE1, 12, 3, 101200);
    host.classify(RACE1, 418000, {
        { .num = 76, .laps = 3 }, { .num = 12, .laps = 3 }, { .num = 29, .laps = 2 },
    });

    const std::string log = host.spotterCueLog();
    CAPTURE(log);
    // Whatever it says about rider seven six, it must not be a number measured
    // against rider two nine. The group drops instead.
    const size_t at76 = log.rfind("on rider seven six");
    if (at76 != std::string::npos) {
        const size_t lineStart = log.rfind('\n', at76);
        const std::string line = log.substr(lineStart == std::string::npos ? 0 : lineStart + 1);
        CAPTURE(line);
        // The && is spelled out into a named bool, not written inside the CHECK:
        // doctest decomposes its expression and static_asserts on a top-level
        // `&&` ("Expression Too Complex Please Rewrite As Binary Comparison").
        const bool saysTrend = line.find("gained") != std::string::npos
                            || line.find("lost") != std::string::npos;
        CHECK_MESSAGE(!saysTrend,
                      "a trend measured against another rider was re-attributed");
    }
}

// ============================================================================
// THE FLAG BEFORE THE WRAP-UP, and each number once. The recorded winner order
// is [SessionComplete, RiderFinished] in one batch — the game completes the
// session before your own finish reaches us — which spoke "That's Race 2 done,
// P one" and THEN "That's the flag, P one": the flag after the wrap-up, the
// position twice, and the held fastest lap following with the same number the
// flag cue had just read as your best lap. The wrap-up is held to the next
// classification now (the lap report's pattern), speaks WITHOUT the position
// once the finish cue has said it, and your own held fastest stays quiet once
// your race is over.
// ============================================================================
TEST_CASE("spotter: the winner's flag speaks before the wrap-up, position once") {
    PluginHost host(dllPath());
    REQUIRE(host.loaded());
    host.startup("Z:\\tmp\\mxbmrp3-tests\\spotter-finish\\");
    host.eventInit("TestTrack", "Player");
    host.raceEvent("TestTrack");
    host.addEntry(12, "Player");
    host.addEntry(476, "Rival");
    host.spotterInstallShippedPack();   // the singleton keeps the previous
                                        // case's one-cue pack otherwise
    host.spotterSubtitles(true);
    host.spotterCategoryMask(0x1F);
    host.session(RACE1, /*numLaps=*/2, /*lengthMs=*/0, /*state=*/16);
    host.classify(RACE1, 30000, { { .num = 12, .laps = 1 },
                                  { .num = 476, .laps = 1 } });

    host.raceLap(RACE1, 12, 2, 93800, /*best=*/2);
    host.raceSessionState(RACE1, /*RACE_OVER=*/512);
    // Nothing has wrapped up yet: the session end is HELD (a race the subject
    // has not finished), the fastest lap is held for the classification.
    CHECK(host.spotterCueLog().find("complete") == std::string::npos);

    host.classify(RACE1, 95000, { { .num = 12, .best = 93800, .laps = 2,
                                    .bestLapNum = 2 },
                                  { .num = 476, .laps = 1 } },
                  /*sessionState=*/512);
    const std::string log = host.spotterCueLog();
    const size_t flag = log.find("Checkered flag, P one");
    CHECK_MESSAGE(flag != std::string::npos, log);
    const size_t wrap = log.find("Race 1 complete");
    REQUIRE_MESSAGE(wrap != std::string::npos, log);
    CHECK_MESSAGE(flag < wrap, "the wrap-up spoke before the flag:\n" << log);
    // The position was the flag cue's to say; the wrap-up closes the session.
    CHECK(log.find("complete, you're P") == std::string::npos);
    // Your held fastest lap after your flag is the number the flag cue just
    // read back as your best lap.
    CHECK(log.find("Fastest lap, nice work") == std::string::npos);

    // FINISHED (32) maps to SessionComplete too — the wrap-up speaks once.
    host.raceSessionState(RACE1, /*FINISHED=*/32);
    host.classify(RACE1, 96000, { { .num = 12, .laps = 2 },
                                  { .num = 476, .laps = 2 } },
                  /*sessionState=*/32);
    const std::string after = host.spotterCueLog();
    size_t completes = 0;
    for (size_t p = after.find("complete"); p != std::string::npos;
         p = after.find("complete", p + 1)) {
        ++completes;
    }
    CHECK_MESSAGE(completes == 1, after);
}

// ============================================================================
// THE OPENING LAP IS NOT NEWS. Offline, every improved lap arrives as the
// session's fastest (bestFlag==2 with no isOnline), and the first crossing of
// every session improved on nothing — so the spotter said "Fastest lap, nice
// work, three oh five point one" for the roll-out lap of every offline
// session, while the on-screen notice ladder deliberately showed nothing
// (race_lap_handler's hadPreviousBest). The held fastest now stays quiet while
// the session's best IS the opening lap — the same bestLapIsFirstLap judgement
// the reference-lap comparisons already make.
// ============================================================================
TEST_CASE("spotter: the session's fastest stays quiet while it is the opener") {
    const int PRACTICE = 1;
    PluginHost host(dllPath());
    REQUIRE(host.loaded());
    host.startup("Z:\\tmp\\mxbmrp3-tests\\spotter-opener\\");
    host.eventInit("TestTrack", "Player");
    host.raceEvent("TestTrack");
    host.addEntry(12, "Player");
    host.spotterInstallShippedPack();
    host.spotterSubtitles(true);
    host.spotterCategoryMask(0x1F);

    // Session one banks an all-time PB, so nothing in session two can reach
    // the PB rung and arm the ladder — the state every later session starts
    // in, and the door the opener escaped through.
    host.session(PRACTICE, 0, 480000);
    host.raceLap(PRACTICE, 12, 1, 90000, /*best=*/2);
    host.classify(PRACTICE, 460000, { { .num = 12, .best = 90000, .laps = 1,
                                        .bestLapNum = 1 } });

    host.session(PRACTICE, 0, 480000);
    host.raceLap(PRACTICE, 12, 1, 185100, /*best=*/2);
    host.classify(PRACTICE, 460000, { { .num = 12, .best = 185100, .laps = 1,
                                        .bestLapNum = 1 } });
    const std::string log = host.spotterCueLog();
    CHECK_MESSAGE(log.find("Fastest lap, nice work, three oh five") ==
                      std::string::npos, log);

    // ...and a real flying lap after it speaks through the ladder as usual.
    host.raceLap(PRACTICE, 12, 2, 91000, /*best=*/2);
    host.classify(PRACTICE, 440000, { { .num = 12, .best = 91000, .laps = 2,
                                        .bestLapNum = 2 } });
    CHECK(host.spotterCueLog().find("Session best") != std::string::npos);
}

// ============================================================================
// THE RIDER EITHER SIDE OF YOU HAS A LAP TIME, AND THE SPOTTER CAN SAY IT.
//
// A gap says where they are; their last lap says whether you are doing anything
// about it. It needed no new detection: handleRaceLap fires for EVERY rider, so
// PluginData's per-rider lap log already had the number -- the same store the
// standings' Last column reads. Only the spotter could not reach it.
//
// The number is of the rider the CUE names. On the gap cues that is the one the
// stopwatch timed rather than whoever the classification lists in that slot by
// the time a deferred report speaks; here, on the lap report, it is the live
// order, which is where the name comes from on this path too. The two have to be
// about one rider or the sentence lies.
// ============================================================================
TEST_CASE("spotter: the rider ahead's last lap is a variable") {
    PluginHost host(dllPath());
    REQUIRE(host.loaded());
    host.startup("Z:\\tmp\\mxbmrp3-tests\\spotter-lastlap-ahead\\");
    host.spotterInstallPack(
        "[Cues]\nlap_completed = P {position}"
        "[, {rider_ahead} ran {last_lap_ahead}].\n");
    host.eventInit("TestTrack", "Player");
    host.raceEvent("TestTrack");
    host.addEntry(12, "Player");      // first entry after EventInit is the player
    host.addEntry(476, "Rival");
    host.spotterEnable(true);
    host.spotterCategoryMask(0x1F);
    host.session(RACE1, /*numLaps=*/5, /*lengthMs=*/480000);

    // The lap report waits for the classification to include the lap it is about
    // (see "the position report waits for the order to include the lap"), so each
    // phase drives the order forward after the lap that belongs to it.
    auto order = [&](int mine, int theirs) {
        host.classify(RACE1, 400000,
                      { { .num = 476, .laps = theirs }, { .num = 12, .laps = mine } });
    };

    order(0, 0);
    host.raceLap(RACE1, 12, 1, 108500);
    order(1, 0);
    const std::string first = host.spotterCueLog();
    // Nobody ahead has completed a lap yet, so the optional group DROPS rather
    // than speaking a fragment. An absent number is the normal early-race case.
    CHECK(first.find("P two.") != std::string::npos);
    CHECK(first.find("ran ") == std::string::npos);

    // Now the rider ahead completes one, and the next report can say it.
    host.raceLap(RACE1, 476, 1, 108200);
    host.raceLap(RACE1, 12, 2, 108900);
    order(2, 1);
    const std::string second = host.spotterCueLog();
    // 108.2s is a LAP time, so it folds to racing minutes -- "one forty eight
    // point two", not "one oh eight point two".
    CHECK(second.find("rider four seventy six ran one forty eight point two.") !=
          std::string::npos);
}
