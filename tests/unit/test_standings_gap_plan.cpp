// ============================================================================
// tests/unit/test_standings_gap_plan.cpp
// Unit tests for hud/standings_gap_plan.h — the standings gap column decision.
//
// This tree used to be ~170 lines of nested if/else inside
// StandingsHud::rebuildRenderData(), only reachable by cross-compiling the DLL
// and driving real callbacks under Wine. The rules worth pinning are the ones a
// plausible-looking edit breaks silently (see the header):
//
//   - LAPPED and FINISHED riders must fall back to the OFFICIAL gap. A live gap
//     between riders on different laps is meaningless — it reads as a small
//     time difference when the truth is "+1 lap".
//   - A rider outside the game's ~10-closest track-position batch has a STALE
//     realTimeGap. The leader is the exception: its live gap is 0 by definition,
//     so it is always fresh.
//   - PLAYER reference subtracts the player's own live gap from every row, so a
//     stale player sample would skew the entire column, not one cell.
//
// The formatting half (formatLapTime / formatTimeDiff) is covered by
// test_plugin_utils.cpp, and the end-to-end strings by the Wine integration
// tests (race_test.cpp, livegaps_test.cpp). This file covers the decision only.
// ============================================================================
#include "doctest.h"

#include "hud/standings_gap_plan.h"
#include <initializer_list>

using namespace StandingsGap;

namespace {

// A mid-race table with live gaps on, leader reference, everything shown.
Table raceTable() {
    Table t;
    t.reference = Reference::LEADER;
    t.scope = Scope::ALL;
    t.isRace = true;
    t.liveGapsEnabled = true;
    t.playerRowIndex = 5;
    t.playerHasGapData = true;
    t.leaderFinishTime = -1;  // race still running
    return t;
}

// An ordinary running rider a few seconds down the road, sampled this frame.
Row runningRow() {
    Row r;
    r.index = 2;
    r.stateNormal = true;
    r.officialGap = 4200;
    r.realTimeGap = 3900;
    r.bestLap = 95000;
    r.hasActiveTrackPos = true;
    return r;
}

}  // namespace

// --- non-participants -------------------------------------------------------

TEST_CASE("non-participant shows its state abbreviation as a label") {
    Table t = raceTable();
    Row r = runningRow();
    r.stateNormal = false;
    r.hasStateAbbr = true;

    Plan p = planGap(t, r);
    CHECK(p.kind == Kind::StateAbbr);
    CHECK(p.style == Style::LABEL);
}

TEST_CASE("non-participant with no abbreviation blanks the cell") {
    // Deliberately Empty rather than Placeholder: an unrecognised state reads as
    // "nothing to say", not as missing data.
    Table t = raceTable();
    Row r = runningRow();
    r.stateNormal = false;
    r.hasStateAbbr = false;

    Plan p = planGap(t, r);
    CHECK(p.kind == Kind::Empty);
    CHECK(p.style == Style::OFFICIAL);
}

TEST_CASE("state outranks everything, including the reference row") {
    // A retired leader must not show "Leader" as though still racing.
    Table t = raceTable();
    Row r = runningRow();
    r.isLeaderRow = true;
    r.stateNormal = false;
    r.hasStateAbbr = true;

    CHECK(planGap(t, r).kind == Kind::StateAbbr);
}

// --- the reference row ------------------------------------------------------

TEST_CASE("leader row mid-race shows the Leader label") {
    Table t = raceTable();
    Row r = runningRow();
    r.isLeaderRow = true;

    Plan p = planGap(t, r);
    CHECK(p.kind == Kind::Label);
    CHECK(p.style == Style::LABEL);
}

TEST_CASE("leader row after the finish shows the finish time") {
    Table t = raceTable();
    t.leaderFinishTime = 1234567;
    Row r = runningRow();
    r.isLeaderRow = true;

    Plan p = planGap(t, r);
    CHECK(p.kind == Kind::LapTime);
    CHECK(p.value == 1234567);
}

TEST_CASE("leader row finished with an unusable time falls back to the label") {
    // leaderFinishTime == 0 means "finished" without a usable duration; showing
    // a 0:00.000 finish time would be worse than the label.
    Table t = raceTable();
    t.leaderFinishTime = 0;
    Row r = runningRow();
    r.isLeaderRow = true;

    Plan p = planGap(t, r);
    CHECK(p.kind == Kind::Label);
    CHECK(p.style == Style::LABEL);
}

TEST_CASE("player row under PLAYER reference shows the Player label") {
    Table t = raceTable();
    t.reference = Reference::PLAYER;
    Row r = runningRow();
    r.isPlayerRow = true;

    CHECK(planGap(t, r).kind == Kind::Label);
}

TEST_CASE("player row under PLAYER reference after the finish shows its own time") {
    Table t = raceTable();
    t.reference = Reference::PLAYER;
    t.leaderFinishTime = 1000000;  // the LEADER finishing is what ends the race
    t.playerFinishTime = 1050000;
    Row r = runningRow();
    r.isPlayerRow = true;

    Plan p = planGap(t, r);
    CHECK(p.kind == Kind::LapTime);
    CHECK(p.value == 1050000);  // the player's time, not the leader's
}

TEST_CASE("reference row outside a race shows its best lap") {
    Table t = raceTable();
    t.isRace = false;
    Row r = runningRow();
    r.isLeaderRow = true;
    r.bestLap = 92345;

    Plan p = planGap(t, r);
    CHECK(p.kind == Kind::LapTime);
    CHECK(p.value == 92345);
}

TEST_CASE("reference row outside a race with no lap yet is a placeholder") {
    Table t = raceTable();
    t.isRace = false;
    Row r = runningRow();
    r.isLeaderRow = true;
    r.bestLap = 0;

    CHECK(planGap(t, r).kind == Kind::Placeholder);
}

TEST_CASE("the leader row is only the reference under LEADER reference") {
    // Under PLAYER reference the leader is an ordinary row and gets a gap. Its
    // realTimeGap is 0 (it IS the live-gap origin), and a zero live gap does not
    // qualify for the live path, so the row reads from the official gap.
    Table t = raceTable();
    t.reference = Reference::PLAYER;
    t.playerLiveGap = 5000;
    t.playerOfficialGap = 5200;
    Row r = runningRow();
    r.isLeaderRow = true;
    r.realTimeGap = 0;
    r.officialGap = 0;  // the leader has no official gap either

    Plan p = planGap(t, r);
    CHECK(p.kind == Kind::TimeDiff);
    CHECK(p.value == -5200);  // 0 - the player's own official gap
    CHECK(p.style == Style::OFFICIAL);
}

// --- live vs official (leader reference) ------------------------------------

TEST_CASE("a fresh same-lap rider uses the live gap") {
    Plan p = planGap(raceTable(), runningRow());
    CHECK(p.kind == Kind::TimeDiff);
    CHECK(p.value == 3900);  // live, not the 4200 official
    CHECK(p.style == Style::LIVE);
}

TEST_CASE("a LAPPED rider uses whole laps, never the live gap") {
    // The trap: gapLaps > 0 means a live time difference is meaningless.
    Row r = runningRow();
    r.gapLaps = 1;

    Plan p = planGap(raceTable(), r);
    CHECK(p.kind == Kind::LapDiff);
    CHECK(p.value == 1);
    CHECK(p.style == Style::OFFICIAL);  // not LIVE
}

TEST_CASE("a FINISHED rider uses the official gap, never the live gap") {
    Row r = runningRow();
    r.isFinished = true;

    Plan p = planGap(raceTable(), r);
    CHECK(p.kind == Kind::TimeDiff);
    CHECK(p.value == 4200);  // official
    CHECK(p.style == Style::OFFICIAL);
}

TEST_CASE("a rider outside the track-position batch keeps the official gap") {
    // The ~10-closest-vehicles rule: realTimeGap is present but STALE.
    Row r = runningRow();
    r.hasActiveTrackPos = false;

    Plan p = planGap(raceTable(), r);
    CHECK(p.kind == Kind::TimeDiff);
    CHECK(p.value == 4200);
    CHECK(p.style == Style::OFFICIAL);
}

TEST_CASE("a zero live gap never qualifies for the live path") {
    // The rule that makes the leader exemption below narrower than it looks:
    // realTimeGap must be strictly positive, leader or not. A rider showing 0
    // has no live sample yet, rather than a live sample of zero.
    Table t = raceTable();
    Row r = runningRow();
    r.realTimeGap = 0;

    Plan p = planGap(t, r);
    CHECK(p.style == Style::OFFICIAL);
    CHECK(p.value == 4200);
}

TEST_CASE("the leader is exempt from the FRESHNESS requirement only") {
    // isLeaderRow waives hasActiveTrackPos, not realTimeGap > 0. Two rows
    // identical but for the flag: the leader still goes live without a fresh
    // sample, the ordinary rider falls back to the official gap.
    Table t = raceTable();
    t.reference = Reference::PLAYER;
    t.playerLiveGap = 2000;

    Row leader = runningRow();
    leader.isLeaderRow = true;
    leader.hasActiveTrackPos = false;
    leader.realTimeGap = 900;
    Plan lp = planGap(t, leader);
    CHECK(lp.style == Style::LIVE);
    CHECK(lp.value == 900 - 2000);

    Row other = leader;
    other.isLeaderRow = false;
    CHECK(planGap(t, other).style == Style::OFFICIAL);
}

TEST_CASE("live gaps switched off forces the official gap") {
    Table t = raceTable();
    t.liveGapsEnabled = false;

    Plan p = planGap(t, runningRow());
    CHECK(p.value == 4200);
    CHECK(p.style == Style::OFFICIAL);
}

TEST_CASE("no gap data at all is a placeholder") {
    Row r = runningRow();
    r.officialGap = 0;
    r.realTimeGap = 0;
    r.gapLaps = 0;

    CHECK(planGap(raceTable(), r).kind == Kind::Placeholder);
}

// --- player reference -------------------------------------------------------

TEST_CASE("player-relative live gap is the difference of the two live gaps") {
    Table t = raceTable();
    t.reference = Reference::PLAYER;
    t.playerLiveGap = 5000;
    Row r = runningRow();
    r.realTimeGap = 3900;  // ahead of the player

    Plan p = planGap(t, r);
    CHECK(p.kind == Kind::TimeDiff);
    CHECK(p.value == -1100);
    CHECK(p.style == Style::LIVE);
}

TEST_CASE("a stale PLAYER sample falls the whole column back to official gaps") {
    // The caller zeroes playerLiveGap when the player's own track-position
    // sample is stale. Without the guard every row would be offset by 0 and read
    // as a leader-relative gap while claiming to be player-relative.
    Table t = raceTable();
    t.reference = Reference::PLAYER;
    t.playerLiveGap = 0;       // stale -> gated to 0 by the caller
    t.playerIsLeader = false;  // and not the leader, so 0 is not legitimate
    t.playerOfficialGap = 5000;

    Plan p = planGap(t, runningRow());
    CHECK(p.style == Style::OFFICIAL);
    CHECK(p.kind == Kind::TimeDiff);
    CHECK(p.value == 4200 - 5000);
}

TEST_CASE("a player who IS the leader has a legitimate zero live gap") {
    Table t = raceTable();
    t.reference = Reference::PLAYER;
    t.playerLiveGap = 0;
    t.playerIsLeader = true;

    Plan p = planGap(t, runningRow());
    CHECK(p.style == Style::LIVE);
    CHECK(p.value == 3900);
}

TEST_CASE("a whole-lap difference outranks a time difference") {
    Table t = raceTable();
    t.reference = Reference::PLAYER;
    t.liveGapsEnabled = false;
    t.playerGapLaps = 0;
    t.playerOfficialGap = 1000;
    Row r = runningRow();
    r.gapLaps = 2;
    r.officialGap = 4200;

    Plan p = planGap(t, r);
    CHECK(p.kind == Kind::LapDiff);
    CHECK(p.value == 2);
}

TEST_CASE("player-relative lap difference is signed: a rider a lap ahead reads -1L") {
    Table t = raceTable();
    t.reference = Reference::PLAYER;
    t.liveGapsEnabled = false;
    t.playerGapLaps = 2;  // the player is two laps down
    Row r = runningRow();
    r.gapLaps = 1;        // this rider is only one lap down

    Plan p = planGap(t, r);
    CHECK(p.kind == Kind::LapDiff);
    CHECK(p.value == -1);
}

TEST_CASE("a rider level with the player on both laps and time is a placeholder") {
    Table t = raceTable();
    t.reference = Reference::PLAYER;
    t.liveGapsEnabled = false;
    t.playerOfficialGap = 4200;
    t.playerGapLaps = 0;

    CHECK(planGap(t, runningRow()).kind == Kind::Placeholder);
}

TEST_CASE("with no player reference yet, the column shows absolute best laps") {
    Table t = raceTable();
    t.reference = Reference::PLAYER;
    t.playerHasGapData = false;
    Row r = runningRow();
    r.bestLap = 94500;

    Plan p = planGap(t, r);
    CHECK(p.kind == Kind::LapTime);
    CHECK(p.value == 94500);
    CHECK(p.style == Style::OFFICIAL);
}

TEST_CASE("with no player reference and no lap time, the cell is a placeholder") {
    Table t = raceTable();
    t.reference = Reference::PLAYER;
    t.playerHasGapData = false;
    Row r = runningRow();
    r.bestLap = 0;

    CHECK(planGap(t, r).kind == Kind::Placeholder);
}

// --- scope ------------------------------------------------------------------

TEST_CASE("scope PLAYER blanks every row but the player's") {
    Table t = raceTable();
    t.scope = Scope::PLAYER;

    CHECK(planGap(t, runningRow()).kind == Kind::Placeholder);

    Row own = runningRow();
    own.isPlayerRow = true;
    own.index = t.playerRowIndex;
    CHECK(planGap(t, own).kind == Kind::TimeDiff);
}

TEST_CASE("scope ADJACENT shows only the player's row and its two neighbours") {
    Table t = raceTable();
    t.scope = Scope::ADJACENT;
    t.playerRowIndex = 5;

    for (int idx : {0, 1, 2, 3, 7, 8, 9}) {
        Row r = runningRow();
        r.index = idx;
        INFO("row " << idx);
        CHECK(planGap(t, r).kind == Kind::Placeholder);
    }
    for (int idx : {4, 5, 6}) {
        Row r = runningRow();
        r.index = idx;
        r.isPlayerRow = (idx == 5);
        INFO("row " << idx);
        CHECK(planGap(t, r).kind != Kind::Placeholder);
    }
}

TEST_CASE("scope ADJACENT with the player off screen blanks every row") {
    Table t = raceTable();
    t.scope = Scope::ADJACENT;
    t.playerRowIndex = -1;

    for (int idx = 0; idx < 6; ++idx) {
        Row r = runningRow();
        r.index = idx;
        INFO("row " << idx);
        CHECK(planGap(t, r).kind == Kind::Placeholder);
    }
}

TEST_CASE("the reference row keeps its label regardless of scope") {
    // Scope filtering must not blank the leader's "Leader" caption.
    Table t = raceTable();
    t.scope = Scope::PLAYER;
    Row r = runningRow();
    r.isLeaderRow = true;

    CHECK(planGap(t, r).kind == Kind::Label);
}

// --- adjacent tint ----------------------------------------------------------

TEST_CASE("adjacent tint marks the row above as Ahead and below as Behind") {
    Table t = raceTable();
    t.scope = Scope::ADJACENT;
    t.reference = Reference::PLAYER;
    t.playerRowIndex = 5;
    t.playerLiveGap = 4000;

    Row above = runningRow();
    above.index = 4;
    CHECK(planGap(t, above).tint == Tint::Ahead);

    Row below = runningRow();
    below.index = 6;
    CHECK(planGap(t, below).tint == Tint::Behind);
}

TEST_CASE("the player's own row is never tinted") {
    Table t = raceTable();
    t.scope = Scope::ADJACENT;
    t.reference = Reference::PLAYER;
    t.playerRowIndex = 5;
    Row r = runningRow();
    r.index = 5;
    r.isPlayerRow = true;

    // (isPlayerRow makes this the reference row under PLAYER reference.)
    CHECK(planGap(t, r).tint == Tint::None);
}

TEST_CASE("no tint under LEADER reference, even in adjacent scope") {
    // The tint means "ahead of / behind YOU", which is meaningless when the
    // numbers are relative to the leader.
    Table t = raceTable();
    t.scope = Scope::ADJACENT;
    t.reference = Reference::LEADER;
    t.playerRowIndex = 5;
    Row r = runningRow();
    r.index = 4;

    CHECK(planGap(t, r).tint == Tint::None);
}

TEST_CASE("no tint on a cell that ended up empty") {
    // A tinted placeholder would colour a dash for no reason.
    Table t = raceTable();
    t.scope = Scope::ADJACENT;
    t.reference = Reference::PLAYER;
    t.playerRowIndex = 5;
    t.playerOfficialGap = 4200;
    t.liveGapsEnabled = false;
    Row r = runningRow();
    r.index = 4;  // same lap, same official gap as the player -> placeholder

    Plan p = planGap(t, r);
    REQUIRE(p.kind == Kind::Placeholder);
    CHECK(p.tint == Tint::None);
}

TEST_CASE("no tint while the player has no gap data") {
    // The column is showing absolute lap times, so "ahead/behind" does not apply.
    Table t = raceTable();
    t.scope = Scope::ADJACENT;
    t.reference = Reference::PLAYER;
    t.playerRowIndex = 5;
    t.playerHasGapData = false;
    Row r = runningRow();
    r.index = 4;

    Plan p = planGap(t, r);
    REQUIRE(p.kind == Kind::LapTime);
    CHECK(p.tint == Tint::None);
}

TEST_CASE("no tint outside adjacent scope") {
    Table t = raceTable();
    t.scope = Scope::ALL;
    t.reference = Reference::PLAYER;
    t.playerRowIndex = 5;
    t.playerLiveGap = 4000;
    Row r = runningRow();
    r.index = 4;

    CHECK(planGap(t, r).tint == Tint::None);
}

// --- totality ---------------------------------------------------------------

TEST_CASE("every input combination yields a usable plan") {
    // The caller switches on kind and casts style straight into the palette
    // index, so an unset or out-of-range field would render garbage rather than
    // fail. Sweep the flag space and assert the plan is always well-formed.
    for (int ref = 0; ref <= 1; ++ref) {
        for (int scope = 1; scope <= 3; ++scope) {
            for (int bits = 0; bits < 256; ++bits) {
                Table t = raceTable();
                t.reference = static_cast<Reference>(ref);
                t.scope = static_cast<Scope>(scope);
                t.isRace = (bits & 1) != 0;
                t.liveGapsEnabled = (bits & 2) != 0;
                t.playerHasGapData = (bits & 4) != 0;
                t.playerIsLeader = (bits & 8) != 0;

                Row r = runningRow();
                r.stateNormal = (bits & 16) != 0;
                r.hasStateAbbr = (bits & 32) != 0;
                r.isLeaderRow = (bits & 64) != 0;
                r.isFinished = (bits & 128) != 0;

                Plan p = planGap(t, r);
                INFO("ref=" << ref << " scope=" << scope << " bits=" << bits);
                CHECK(p.kind >= Kind::Empty);
                CHECK(p.kind <= Kind::LapDiff);
                CHECK(p.style >= Style::OFFICIAL);
                CHECK(p.style <= Style::LABEL);
                // A tint is only ever attached to a cell that shows something.
                if (p.tint != Tint::None) {
                    CHECK(p.kind != Kind::Placeholder);
                    CHECK(p.kind != Kind::Empty);
                }
                // The non-numeric kinds carry no value.
                if (p.kind == Kind::Empty || p.kind == Kind::Placeholder ||
                    p.kind == Kind::Label || p.kind == Kind::StateAbbr) {
                    CHECK(p.value == 0);
                }
            }
        }
    }
}
