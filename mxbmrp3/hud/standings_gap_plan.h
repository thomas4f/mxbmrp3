// ============================================================================
// hud/standings_gap_plan.h
// The standings gap column's DECISION tree — which value a row's gap cell shows,
// in which style, with which tint. Pure integer logic; no strings, no
// PluginData, no BaseHud.
//
// WHAT THIS IS. The gap cell is the most mode-dependent thing the standings
// table renders: the answer is a product of reference (leader/player) x source
// (live/official) x row role (reference row / lapped / finished / non-starter) x
// scope (all/player/adjacent). That product used to be a ~170-line nested
// if/else inside rebuildRenderData(), where every branch both DECIDED and
// snprintf'd, so the only way to exercise it was to build the DLL and drive the
// real callbacks under Wine.
//
// WHY IT LIVES HERE AND NOT IN StandingsHud. Splitting the decision from the
// formatting is what makes the decision testable: planGap() returns a Kind + a
// value + a style + a tint, and the caller owns the snprintf and the palette
// lookup. The formatting half was already covered (PluginUtils::formatLapTime /
// formatTimeDiff, tests/unit/test_plugin_utils.cpp); the decision half was the
// part with no cheap test, and it is the part that carries the subtle rules:
//
//   - a LAPPED or FINISHED rider always reads from the OFFICIAL gap, because a
//     live gap is only meaningful between riders on the same lap
//   - a rider outside the game's ~10-closest track-position batch has a STALE
//     realTimeGap, so the live path additionally needs a fresh sample
//     (Row::hasActiveTrackPos). The leader row is exempt from the FRESHNESS
//     requirement only — it must still carry a strictly positive realTimeGap,
//     exactly like anyone else, so a rider showing 0 reads as "no live sample
//     yet" rather than "live sample of zero". This is the `canUseLiveForRider`
//     half of the distinction CLAUDE.md draws against the overlay's
//     `liveGapValid` — the two answer different questions and must not be
//     unified. Both halves are pinned in the test.
//   - PLAYER reference subtracts the player's own live gap from every row, so
//     the player's OWN sample must be fresh too, or one stale reference skews
//     the whole column (Table::playerLiveGap is gated by the caller for exactly
//     this reason)
//
// Pinned by tests/unit/test_standings_gap_plan.cpp (~1s, no game). The
// end-to-end formatting is still pinned by tests/integration/tests/race_test.cpp
// and livegaps_test.cpp — the two layers check different things.
//
// ENUM PARITY. Reference/Scope/Style mirror StandingsHud::GapReferenceMode,
// StandingsHud::GapMode and DisplayEntry::GapStyle so the caller can cast
// instead of switch. standings_hud_build.cpp static_asserts the values match;
// a renumbering on either side fails to compile there.
// ============================================================================
#pragma once

#include <cstdint>

namespace StandingsGap {

// Mirrors StandingsHud::GapReferenceMode. ALTERNATING is resolved by the caller
// before it gets here, so planGap() only ever sees LEADER or PLAYER.
enum class Reference : uint8_t {
    LEADER = 0,
    PLAYER = 1,
    ALTERNATING = 2,
    COUNT
};

// Mirrors StandingsHud::GapMode. OFF never reaches planGap() (the column is not
// rendered at all), but is present so the cast from GapMode is total.
enum class Scope : uint8_t {
    OFF = 0,
    PLAYER = 1,    // only the player's own row shows a gap
    ADJACENT = 2,  // only the player's row and its two neighbours
    ALL = 3,
    COUNT
};

// Mirrors StandingsHud::DisplayEntry::GapStyle (which palette slot the cell
// uses). OFFICIAL is the default; LIVE marks a fresh real-time gap; LABEL marks
// text rather than a number.
enum class Style : uint8_t {
    OFFICIAL = 0,
    LIVE = 1,
    LABEL = 2,
    COUNT
};

// What the cell shows. The caller maps each to a string:
//   Empty       -> ""                       (non-starter with no abbreviation)
//   Placeholder -> Placeholders::GENERIC    (nothing meaningful to show)
//   StateAbbr   -> getRiderStateAbbreviation(state)   ("DNS"/"RET"/"DSQ")
//   Label       -> "Leader" / "Player"      (chosen by Table::reference)
//   LapTime     -> formatLapTime(value)     (an absolute time in ms)
//   TimeDiff    -> formatTimeDiff(value)    (a signed delta in ms)
//   LapDiff     -> "%+dL" of value          (a signed whole-lap delta)
enum class Kind : uint8_t {
    Empty,
    Placeholder,
    StateAbbr,
    Label,
    LapTime,
    TimeDiff,
    LapDiff
};

// Adjacent-mode tint, expressed as the row's relation to the player rather than
// as a colour: the caller owns which palette slot each maps to (today Ahead ->
// ColorSlot::NEGATIVE, Behind -> ColorSlot::POSITIVE), so a theme change is a
// call-site edit and never reaches this header.
enum class Tint : uint8_t {
    None,
    Ahead,   // row sits above the player in the table
    Behind   // row sits below the player
};

struct Plan {
    Kind kind = Kind::Placeholder;
    Style style = Style::OFFICIAL;
    Tint tint = Tint::None;
    int value = 0;  // meaning depends on kind; 0 for the non-numeric kinds
};

// Table-wide inputs — the same for every row in one rebuild.
struct Table {
    Reference reference = Reference::LEADER;  // already resolved (never ALTERNATING)
    Scope scope = Scope::ALL;
    bool isRace = false;
    // isRace && the user's live-gaps toggle. False forces every row onto the
    // official gap.
    bool liveGapsEnabled = false;

    int playerRowIndex = -1;  // display-entry index of the player's row; <0 = not shown

    // The player has enough timing data for a relative comparison to mean
    // anything. False falls the whole column back to absolute best-lap times.
    // The leader counts as having data: their zero gap is a real value.
    bool playerHasGapData = false;
    bool playerIsLeader = false;

    int playerOfficialGap = 0;
    int playerGapLaps = 0;
    // The player's live gap, ALREADY gated on a fresh track-position sample by
    // the caller — 0 when stale. See the header note.
    int playerLiveGap = 0;
    int playerFinishTime = 0;  // 0 when the player has not finished / is absent

    int leaderFinishTime = -1;  // <0 = the leader has not finished
};

// Per-row inputs.
struct Row {
    int index = 0;  // display-entry index, compared against Table::playerRowIndex

    bool stateNormal = true;    // entry.state == RiderState::NORMAL
    bool hasStateAbbr = false;  // getRiderStateAbbreviation(state) is non-empty
    bool isLeaderRow = false;   // entry.position == Position::FIRST
    bool isPlayerRow = false;

    int officialGap = 0;
    int gapLaps = 0;
    int realTimeGap = 0;
    int bestLap = 0;

    bool isFinished = false;         // sessionData.isRiderFinished(...)
    bool hasActiveTrackPos = false;  // in the current track-position batch
};

inline Plan planGap(const Table& t, const Row& r) {
    Plan p;

    // Non-participants (DNS / RET / DSQ) show their status instead of a gap.
    // A state with no abbreviation leaves the cell blank rather than showing a
    // placeholder, so an unknown state reads as "nothing to say" rather than as
    // missing data.
    if (!r.stateNormal) {
        if (r.hasStateAbbr) {
            p.kind = Kind::StateAbbr;
            p.style = Style::LABEL;
        } else {
            p.kind = Kind::Empty;
        }
        return p;
    }

    // The reference row has no gap to itself; it shows context instead.
    const bool isReferenceRow =
        (t.reference == Reference::LEADER && r.isLeaderRow) ||
        (t.reference == Reference::PLAYER && r.isPlayerRow);

    if (isReferenceRow) {
        if (!t.isRace) {
            // Practice/qualifying: the reference is a lap time, not a finish.
            if (r.bestLap > 0) {
                p.kind = Kind::LapTime;
                p.value = r.bestLap;
            }
            return p;  // else: Placeholder
        }

        const bool leaderFinished = (t.leaderFinishTime >= 0);
        if (leaderFinished) {
            // Race over: the reference row shows its own finish time, falling
            // back to the label when that time is unknown (a leaderFinishTime
            // of exactly 0 means "finished" without a usable duration).
            if (t.reference == Reference::LEADER && t.leaderFinishTime > 0) {
                p.kind = Kind::LapTime;
                p.value = t.leaderFinishTime;
                return p;
            }
            if (t.reference == Reference::PLAYER && t.playerFinishTime > 0) {
                p.kind = Kind::LapTime;
                p.value = t.playerFinishTime;
                return p;
            }
        }
        p.kind = Kind::Label;
        p.style = Style::LABEL;
        return p;
    }

    // Scope PLAYER: only the player's own row carries a gap.
    if (t.scope == Scope::PLAYER && !r.isPlayerRow) {
        return p;  // Placeholder
    }

    // Scope ADJACENT: the player's row and its immediate neighbours only. With
    // no player row on screen nothing qualifies.
    if (t.scope == Scope::ADJACENT) {
        const bool inWindow = (t.playerRowIndex >= 0) &&
            (r.index == t.playerRowIndex - 1 ||
             r.index == t.playerRowIndex ||
             r.index == t.playerRowIndex + 1);
        if (!inWindow) {
            return p;  // Placeholder
        }
    }

    // A live gap is only meaningful between riders on the same lap, and only
    // while the rider's track-position sample is fresh. The leader is always
    // "fresh": its gap is 0 by definition.
    const bool isLapped = (r.gapLaps > 0);
    const bool liveUsable = t.liveGapsEnabled && !isLapped && !r.isFinished && r.realTimeGap > 0;
    const bool sampleFresh = r.hasActiveTrackPos || r.isLeaderRow;
    const bool canUseLive = liveUsable && sampleFresh;

    if (t.reference == Reference::PLAYER && !t.playerHasGapData) {
        // No usable player reference yet (out-lap, just joined): show absolute
        // best laps so the column still carries information.
        if (r.bestLap > 0) {
            p.kind = Kind::LapTime;
            p.value = r.bestLap;
        }
    } else if (t.reference == Reference::PLAYER) {
        if (canUseLive && (t.playerLiveGap > 0 || t.playerIsLeader) &&
            (r.realTimeGap > 0 || r.isLeaderRow)) {
            p.kind = Kind::TimeDiff;
            p.value = r.realTimeGap - t.playerLiveGap;
            p.style = Style::LIVE;
        } else if (r.officialGap > 0 || r.gapLaps > 0 || r.isLeaderRow) {
            // A whole-lap difference outranks a time difference: "+1L" is the
            // truthful answer even when the raw times also differ.
            const int lapDelta = r.gapLaps - t.playerGapLaps;
            const int timeDelta = r.officialGap - t.playerOfficialGap;
            if (lapDelta != 0) {
                p.kind = Kind::LapDiff;
                p.value = lapDelta;
            } else if (timeDelta != 0) {
                p.kind = Kind::TimeDiff;
                p.value = timeDelta;
            }
            // else: Placeholder — same lap and same time as the player.
        }
    } else {
        if (canUseLive) {
            p.kind = Kind::TimeDiff;
            p.value = r.realTimeGap;
            p.style = Style::LIVE;
        } else if (isLapped) {
            // Positive by construction (isLapped), so the signed "%+dL" the
            // caller applies renders identically to the old unsigned "+%dL".
            p.kind = Kind::LapDiff;
            p.value = r.gapLaps;
        } else if (r.officialGap > 0) {
            p.kind = Kind::TimeDiff;
            p.value = r.officialGap;
        }
    }

    // Adjacent mode tints the two neighbour rows by which side of the player
    // they sit on. Only meaningful against a real player reference, and only on
    // a cell that ended up showing something.
    if (t.scope == Scope::ADJACENT && t.reference == Reference::PLAYER &&
        t.playerHasGapData && t.playerRowIndex >= 0 && r.index != t.playerRowIndex &&
        p.kind != Kind::Placeholder && p.kind != Kind::Empty) {
        p.tint = (r.index < t.playerRowIndex) ? Tint::Ahead : Tint::Behind;
    }

    return p;
}

}  // namespace StandingsGap
