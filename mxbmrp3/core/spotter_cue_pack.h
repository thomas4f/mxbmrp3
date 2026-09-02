// ============================================================================
// core/spotter_cue_pack.h
// The spotter's cue-pack format: user-editable phrase templates and per-event
// wav overrides, loaded from mxbmrp3_data/spotters/<name>/spotter.ini. Pure and
// Windows-free so the unit suite exercises parse/expand directly
// (test_spotter_cue_pack.cpp); SpotterManager does the file I/O and holds the
// active pack.
//
// PACK FORMAT (the whole thing - this comment is the spec pack authors read):
//
//   [Cues]
//   ; <key> = phrase template spoken by TTS (and shown as the subtitle).
//   ; <key>_wav = wav filename relative to the pack folder; when present the
//   ;             wav plays INSTEAD of TTS for that cue (the subtitle still
//   ;             shows the phrase). Dynamic cues ({event_rider}/{event_time}) in wav form
//   ;             need the chunk mixer, so today a wav override suits the
//   ;             fixed cues (green flag, final lap, ...).
//   fastest_lap_other = Fastest lap, {event_rider}, {event_time}.
//   session_started = Green green green.
//   session_started_wav = green.wav
//
//   VARIABLES. Every {variable} works in EVERY cue - they are read from live
//   state when the cue fires, not carried by the event that fired it, so
//   `overtime_started = You're {position}, {overtime_laps} to go, {gap_to_ahead} to {rider_ahead}.` is
//   a line you can just write. The full list and what each means is the table
//   in spotter_vars.h. A variable with no value RIGHT NOW (no rider ahead when
//   you lead, no last lap on lap one) expands to nothing and the expander
//   tidies the punctuation around it ("lap, , ahead." -> "lap, ahead."), so
//   one template covers both cases. A name that is not a variable at all is
//   left on screen as written - that is how you spot a typo.
//
//   Values arrive SPOKEN: {event_rider} -> "rider four seventy six" (racing-style
//   words), {event_time} -> "one forty eight point two".
//
//   OPTIONAL GROUPS `[...]` drop whole when any variable inside them is
//   empty. The tidy-up can remove an orphaned comma but not a word that
//   belongs to a pair, so this is what free combination needs:
//
//     lap_completed = P {position}[, {gap_to_ahead} to {rider_ahead}].
//       mid-pack -> "P four, one point two to rider sixty five."
//       leading  -> "P one."      (not "P one, to.")
//
//   WHEN A GROUP IS NEEDED, and when it is only noise. The tidy-up removes
//   orphaned PUNCTUATION, so "Penalty, penalty, {penalty_seconds}." reads
//   correctly with no amount ("Penalty, penalty."). It cannot remove an
//   orphaned WORD - the "to" above, or the "P" in "P {position}" - so those
//   need the brackets.
//
//   Which is why the shipped pack looks inconsistent about {position} and is
//   not: lap_completed, position_gained and position_lost use it bare because
//   their emitter returns early on an unclassified rider (`if (position <= 0)
//   return`), so it cannot be empty there. Cues with no such guarantee -
//   session_ended, overtime_started, hotkey_triggered - bracket it. The
//   guarantee is in the emitter and invisible from the file, so if you write a
//   line for a cue you did not check, bracket it.
//
//   An EMPTY value silences the cue ("fastest_lap_other =" mutes rivals'
//   fastest laps even when the Timing category is on) and, in an overlay pack,
//   overrides the shipped line rather than falling through to it.
//
//   An ABSENT key is silence too - there is no built-in wording behind a pack
//   to fall through to. What a pack does not define, the SHIPPED pack answers
//   for (SpotterManager::reloadCuePack layers the two), and what neither
//   defines is not spoken. That is why the shipped file carries a commented
//   row for every default-quiet cue: it is both the mute and the
//   documentation.
//
// KEY NAMES are cueKeyFor() below: <event>_you / <event>_other for rider
// events (the focused rider vs everyone else), a single <event> for
// session-level ones. They are the pack format's API - renaming one silently
// orphans every pack's override of it, same rule as the hotkey config names.
//
// DETECTOR CUES use the same override machinery but are not events, so they
// have no entry in cueKeyFor - their keys are the literals SpotterManager
// emits, part of the same frozen API:
//   rider_behind, rider_behind_clear  (proximity pair; Opponents)
//   rider_left, rider_right, riders_both_sides  (alongside overlap; the
//     third fires when a rival is level on BOTH sides, where naming one side
//     would send you into the other; Opponents category)
//   lapping_traffic                (backmarker ahead; Opponents category)
//   blue_flag, hazard_ahead, wrong_way_ahead        (Hazard category)
//   ten_minutes_remaining, five_minutes_remaining, halfway_point (session milestones; Timing category)
//   lap_completed                  (your own lap crossing - the ONE cue for
//     that moment. What it says is the template's business: {position},
//     {last_lap_time}, {gap_to_ahead}, any of them - a content choice, which
//     is what a variable is for, not a second key.)
//   gap_behind, gap_behind_closing, gap_behind_dropping - the one PACE cue,
//     and the only one that is genuinely its own moment: it fires when the
//     rider behind reaches a timing point YOU already crossed, so the number
//     is a stopwatch rather than an estimate (spotter_pace.h), and nothing
//     else marks that instant. There is deliberately no gap_ahead counterpart
//     - the ahead gap is measured at your own crossing, the same instant
//     lap_completed fires, so it is {gap_to_ahead} /
//     {trend_ahead} / {gained_on_ahead} on THOSE cues rather than three keys
//     of its own. A value is not an event.
//   personal_best                  (all-time PB lap; Timing category)
//   record_beaten                  (a lap under the PROVIDER's track record -
//     the tier above personal_best, and emitted INSTEAD of it, the way the
//     lap handler's notice ladder suppresses redundant tiers. MX Bikes only)
//   session_best                   (beat your session best without it being an
//     all-time PB - the third tier of the ladder the lap handler computes,
//     and the most common good-news moment in practice)
//   position_gained, position_lost ({positions_changed} against the grid, or the last
//     crossing when you joined mid-race; fires only on a change)
//   sector_completed, sector_completed_faster, sector_completed_slower
//     ({sector_number}, {event_time}, {sector_duration}, and a delta against
//     whichever reference you name: {sector_delta_best_lap}, _ideal, _last_lap,
//     _alltime, _record; keyed by direction like the gap cues.
//     DEFAULT-QUIET - three or four a lap)
//   crashed_you, crashed_other     (the rising edge of a rider's crash flag;
//     DEFAULT-QUIET both ways)
//   spectate_target                ("now watching {event_rider}"; DEFAULT-QUIET -
//     the auto-director cuts constantly)
//   hotkey_triggered               (NOT an event: the Spotter Cue hotkey says
//     this, and no race event produces it, so it is whatever line you write.
//     Also the quickest way to hear a template you are editing -
//     press the key instead of waiting for the race to produce its event.)
//   practice_started, quali_started, warmup_started (session-kind green
//     flags - SessionStarted picks by session type; races keep
//     session_started)
//   finished_leader                (the P1 rider takes the flag, not you -
//     the one finish worth hearing. finished_other is DEFAULT-QUIET for the
//     same reason the pit traffic is: on a full grid it is a callout per
//     rider as the flag falls, and none of them is about your race)
//   voice_preview                        (NOT a race cue: the sample line the
//     settings menu plays when you cycle onto this pack. Absent, the plugin
//     builds one from chunks the format guarantees - rider.wav, num_*,
//     point.wav - so every pack previews without defining anything. Define it
//     to show off your own recorded segments; an empty value mutes it. It
//     never reaches the subtitle feed.)
//
// VARIANTS: a cue may define alternates as <key>_2 .. <key>_9 (the plain key
// is variant 1), each a complete cue row - its own phrase and optionally its
// own _wav/_mix. One is picked at random per firing, so "Rider behind." /
// "On your tail." / "Pressure from behind." rotate instead of repeating. A
// variant without a phrase inherits the base phrase for its subtitle; audio
// never inherits - a variant plays only its own wav/mix,
// else TTS. Base keys never end in a digit, so the suffix is unambiguous.
//
// MIX SEQUENCES (<key>_mix) stitch a DYNAMIC cue from chunk wavs at playback
// time - the wav answer to {event_rider}/{event_time}, which a single pre-baked file
// cannot carry:
//
//   fastest_lap_other_mix = fl_head.wav {event_rider} {event_time}
//
// Tokens are wav names (same no-path rule as _wav), "{event_rider}", "{event_time}" or
// "{penalty_seconds}". {event_rider} plays rider.wav + num_<N>.wav; {event_time} plays
// num_<composed>.wav + point.wav + num_<tenths>.wav; {penalty_seconds} (penalty
// seconds) plays num_<N>.wav + seconds.wav (second.wav when 1) - the frozen
// chunk-name convention (spotter_mix.h). Precedence per cue: _mix (when
// every needed chunk loads) beats _wav beats the TTS phrase; an
// unresolvable or broken mix falls back down that ladder rather than
// dropping words. Exception: {penalty_seconds} is OPTIONAL - a penalty with no amount
// known just omits it, keeping the rest of the recipe. The phrase template
// still provides the subtitle text ({penalty_seconds} expands to words there too).
//
// JOIN TIGHTNESS is the one thing a mix has that a pre-baked clip doesn't:
// how the pieces meet. It is per-PACK, because it is a property of how that
// voice was rendered, not of any one cue:
//
//   [Mix]
//   gap_ms = -40     ; overlap the joins by 40ms
//
// Positive is silence between chunks, 0 is butt-joined, NEGATIVE overlaps them
// with a crossfade - which is what makes "nine" + "sixty five" read as one
// number instead of two words. The shipped clips are trimmed to the speech, so
// zero is as tight as plain concatenation can get and the useful range for a
// tightly-baked pack is negative. Absent means the plugin's default (60ms);
// a pack wanting no gap must say `gap_ms = 0`. Range and mechanism:
// spotter_mix.h assemble(). The value is in the voice's OWN time - playback
// speed scales it, so a faster spotter does not keep unnaturally wide joins.
//
// A pack may carry all 1000 number chunks (best prosody) or just num_0..99
// plus hundred.wav and oh.wav (~104 chunks): a missing three-digit chunk
// stitches from its hundreds split ("one | forty two") automatically - see
// spotter_mix.h's decomposeNumFile. That is the practical path for
// human-recorded packs.
// ============================================================================
#pragma once

#include <cctype>
#include <cstdint>
#include <map>
#include <string>
#include <vector>

#include "event_log_types.h"
#include "spotter_mix.h"
#include "spotter_phrase.h"
#include "spotter_vars.h"

namespace SpotterCuePack {

// Parsed pack: cue key -> phrase template, cue key -> wav filename, and cue
// key -> mix token sequence (the "_wav"/"_mix" suffixes are stripped during
// parse; all three maps use the same keys).
struct Pack {
    // [pack] name - OPTIONAL, and spelled the same way in every pack type, so
    // what a modder learns writing one pack is what they write in the next.
    // Empty means "no opinion": the picker then shows the
    // FOLDER name, which is what every pack written before this already gets,
    // so adding the key changes nothing for them. It exists because a folder
    // name cannot spell everything a voice wants to be called -- "Bill (UK)"
    // has a bracket and a space in it, and `bill-uk` reads as "bill-uk".
    std::string displayName;
    std::map<std::string, std::string> phrases;
    std::map<std::string, std::string> wavs;
    std::map<std::string, std::vector<std::string>> mixes;
    // [Mix] gap_ms - how the pack's own clips want to be joined. Absent means
    // "use the plugin's default", which is NOT the same as 0: a pack that
    // deliberately butt-joins says so, and must not be overridden by a future
    // change to that default.
    bool hasGapMs = false;
    int gapMs = 0;
    // Mix rows dropped at parse time, as "<key> (<token>)". Kept so the
    // loader can SAY so: a dropped recipe is invisible otherwise - the cue
    // still speaks, just through TTS, which on Wine means not at all.
    std::vector<std::string> rejectedMixes;
    bool empty() const {
        return phrases.empty() && wavs.empty() && mixes.empty();
    }
};

// Strip a `_2`..`_9` variant suffix, so `rider_behind_4` is recognised as the
// cue `rider_behind`. Anything else is returned unchanged.
inline std::string stripVariantSuffix(const std::string& key) {
    if (key.size() > 2 && key[key.size() - 2] == '_' &&
        key.back() >= '2' && key.back() <= '9') {
        return key.substr(0, key.size() - 2);
    }
    return key;
}

// The stable cue key for an event, or nullptr for events that are never
// spoken and not overridable (Director cuts - broadcaster tooling).
// ---------------------------------------------------------------------------
// EVERY cue key, in one place. This is the published list a pack author works
// from, and it is here rather than in prose because prose drifts (a renamed
// key leaves the shipped pack's rows dead, and nothing fails).
// test_spotter_pack_census.cpp walks this table against the shipped ini in
// BOTH directions - a key the plugin emits but the ini never mentions is
// undocumented, and a row in the ini that names no real key is a line that
// will never be spoken.
//
// Adding a cue means adding its row here. There is no on/off column: whether a
// cue ships silent is whether the shipped pack's row is commented out, which is
// the only place that can answer it.
struct CueKeyInfo {
    const char* key;
    const char* what;    // one line, for the generated reference
    // Which settings-menu switch mutes this cue. It is here so the shipped
    // pack's section headings can be CHECKED against it rather than merely
    // written to match - a cue filed under the wrong heading tells a reader
    // the wrong switch controls it, and nothing else would catch that.
    SpotterPhrase::Category category;
};

inline const std::vector<CueKeyInfo>& allCueKeys() {
    static const std::vector<CueKeyInfo> kKeys = {
        // -- session ---------------------------------------------------------
        { "session_started", "this session is now active, any kind. The shipped line does NOT state the length: a race hears session_prestart seconds earlier and it is too late to act on by then. The kind-specific rows below keep it, having no prestart before them. If you do want it here, {session_length} is the variable - {session_remaining} can only repeat it" , SpotterPhrase::Category::General },
        { "gate_drop", "the gate physically falls (standing starts only) - NOT the same moment as session_started, which fires a few seconds earlier with the grid still held" , SpotterPhrase::Category::General },
        { "practice_started", "the same moment worded for practice; falls back to session_started" , SpotterPhrase::Category::General },
        { "quali_started", "the same moment worded for qualifying; falls back to session_started" , SpotterPhrase::Category::General },
        { "warmup_started", "the same moment worded for warm up; falls back to session_started" , SpotterPhrase::Category::General },
        { "session_prestart", "the session is about to go green" , SpotterPhrase::Category::General },
        { "session_ended", "the session is over - fires whether you are still circulating or already parked" , SpotterPhrase::Category::General },
        { "session_state", "the session changed to a state with no cue of its own - cancelled, sighting lap, race over. The idle gap between sessions is deliberately not announced" , SpotterPhrase::Category::General },
        { "overtime_started", "the clock expired, bonus laps begin. {overtime_laps} counts the laps AFTER the current one: the clock stops with the leader mid-lap, so no laps-to-go count is true wherever they happen to be" , SpotterPhrase::Category::Timing },
        { "session_time_expired", "the clock hit zero in a session with no bonus laps to run" , SpotterPhrase::Category::Timing },
        { "final_lap", "the LEADER starts the last lap - a fact about the race, not about you. DEFAULT-QUIET: final_lap_you covers your own last lap from any position, and hearing both says one actionable moment twice" , SpotterPhrase::Category::Timing },
        { "final_lap_you", "YOU start your last lap - a lap or more later than the leader's if you are down the order" , SpotterPhrase::Category::Timing },
        // -- your race -------------------------------------------------------
        { "fastest_lap_you", "you set the fastest lap of the session, beating the whole field" , SpotterPhrase::Category::Timing },
        { "personal_best", "you beat your best ever here; suppresses fastest_lap_you on the same lap" , SpotterPhrase::Category::Timing },
        { "record_beaten", "you beat the track record; replaces personal_best rather than adding to it (MX Bikes only)" , SpotterPhrase::Category::Timing },
        { "session_best", "you beat your session best without it being an all-time best - the common good-news lap in practice" , SpotterPhrase::Category::Timing },
        { "lap_invalidated", "your lap was struck out - the only notice you get in practice or qualifying, which issue no penalties" , SpotterPhrase::Category::Timing },
        { "lap_completed", "you crossed the line - THE cue for your own lap, and what it says is entirely the template's choice ({position}, {last_lap_time}, the gaps either side). Spoken once the order includes the lap you just did" , SpotterPhrase::Category::Timing },
        { "position_gained", "you gained places ON THIS LAP, against where you stood at your last crossing. Fires at the same instant as lap_completed, which has just said where you are - so a line here wants to say only what CHANGED" , SpotterPhrase::Category::Timing },
        { "position_lost", "you lost places on this lap, same measure and the same instant" , SpotterPhrase::Category::Timing },
        { "leader_you", "you took the lead" , SpotterPhrase::Category::General },
        { "finished_you", "you took the flag" , SpotterPhrase::Category::General },
        { "penalty_you", "you were penalised. {penalty_seconds} is THIS penalty and {penalty_total} the running total INCLUDING it - the standings column has not absorbed it yet at this instant, so the cue keeps its own tally. The total is empty on a first penalty, where it would only repeat the amount" , SpotterPhrase::Category::General },
        { "penalty_clear_you", "your penalty was cleared" , SpotterPhrase::Category::General },
        { "penalty_change", "your penalty was revised up or down" , SpotterPhrase::Category::General },
        { "disqualified_you", "you were disqualified" , SpotterPhrase::Category::General },
        { "retired_you",  "you retired from the session" , SpotterPhrase::Category::General },
        { "crashed_you",  "you went down" , SpotterPhrase::Category::General },
        { "fuel_low", "fuel down to about four laps - the point the Fuel widget turns amber. Checked as you cross the line, once each per session and only on the way down; needs two laps first, since one lap cannot tell a rate from a tank change" , SpotterPhrase::Category::General },
        { "fuel_critical", "...and now about two, where it turns red; falls back to fuel_low" , SpotterPhrase::Category::General },
        { "pit_entry_you", "you entered the pits" , SpotterPhrase::Category::General },
        { "pit_exit_you", "you left the pits" , SpotterPhrase::Category::General },
        // -- pace ------------------------------------------------------------
        { "gap_behind", "the rider behind reaches a point you already crossed - a stopwatch gap, not an estimate" , SpotterPhrase::Category::Timing },
        { "gap_behind_closing", "...and they are closing on you" , SpotterPhrase::Category::Timing },
        { "gap_behind_dropping", "...and they are dropping back" , SpotterPhrase::Category::Timing },
        // -- sectors ---------------------------------------------------------
        { "sector_completed", "you crossed a split with no best lap yet to compare it against - the lap cue's smaller sibling, three or four times a lap, which is why the split cues ship silent" , SpotterPhrase::Category::Timing },
        { "sector_completed_faster", "ahead of your best lap at this split" , SpotterPhrase::Category::Timing },
        { "sector_completed_slower", "behind your best lap at this split" , SpotterPhrase::Category::Timing },
        { "sector_best", "that sector ON ITS OWN beat your best of it this session - so it still fires for a great sector inside a scrappy lap, which the rows above cannot say" , SpotterPhrase::Category::Timing },
        { "on_pace_session_best", "at the last split before the line, up on your best lap of the session by at least `[Spotter] on_pace_margin_ms`" , SpotterPhrase::Category::Timing },
        { "on_pace_personal_best", "...up on your best lap ever here (outranks the session one)" , SpotterPhrase::Category::Timing },
        { "on_pace_record", "...up on the track record (outranks both)" , SpotterPhrase::Category::Timing },
        // -- milestones ------------------------------------------------------
        { "ten_minutes_remaining", "ten minutes of the session left" , SpotterPhrase::Category::Timing },
        { "five_minutes_remaining", "five minutes left" , SpotterPhrase::Category::Timing },
        { "halfway_point", "half the session gone, by clock in a timed one and by the leader's laps otherwise" , SpotterPhrase::Category::Timing },
        // -- riders around you -----------------------------------------------
        { "rider_behind", "someone has closed up behind you" , SpotterPhrase::Category::Proximity },
        { "rider_behind_clear", "...and has now dropped back" , SpotterPhrase::Category::Proximity },
        { "rider_left", "someone is alongside on your left" , SpotterPhrase::Category::Proximity },
        { "rider_right", "someone is alongside on your right" , SpotterPhrase::Category::Proximity },
        { "riders_both_sides", "you are boxed in - a rival on EACH side at once, which is the one case where hearing only one side is worse than hearing nothing" , SpotterPhrase::Category::Proximity },
        { "lapping_traffic", "a backmarker is ahead of you - and {event_rider} is them. One of only two detector cues that names its subject (blue_flag is the other): both sides of the lapping pair are already known, where the proximity and hazard cues carry distances only" , SpotterPhrase::Category::Proximity },
        // -- hazards ---------------------------------------------------------
        { "blue_flag", "a faster rider is closing to lap you, and {event_rider} is that rider - which bike to expect is the half of a blue flag you can act on. Empty if the pairing has already been recomputed away, so bracket it" , SpotterPhrase::Category::Hazard },
        { "hazard_ahead", "a rider is down ahead of you" , SpotterPhrase::Category::Hazard },
        { "wrong_way_ahead", "a rider is coming the wrong way" , SpotterPhrase::Category::Hazard },
        // -- other riders ----------------------------------------------------
        { "fastest_lap_other", "someone else set the fastest lap" , SpotterPhrase::Category::Opponents },
        { "leader_other", "the lead changed hands" , SpotterPhrase::Category::Opponents },
        { "finished_other",  "someone else took the flag - one callout per rider as the flag falls, and none of them about your race; finished_leader is the one that is" , SpotterPhrase::Category::Opponents },
        { "finished_leader", "the leader took the flag" , SpotterPhrase::Category::Opponents },
        { "retired_other",  "someone else retired" , SpotterPhrase::Category::Opponents },
        { "disqualified_other",  "someone else was disqualified" , SpotterPhrase::Category::Opponents },
        { "did_not_start_other",  "someone else did not start" , SpotterPhrase::Category::Opponents },
        { "crashed_other",  "someone else crashed" , SpotterPhrase::Category::Opponents },
        { "penalty_other",  "someone else was penalised - the biggest offender of the quiet lot: replaying a real 24-rider race, other riders' cut penalties were TWENTY of the sixty callouts spoken in four laps" , SpotterPhrase::Category::Opponents },
        { "penalty_change_other",  "someone else's penalty was revised" , SpotterPhrase::Category::Opponents },
        { "penalty_clear_other",  "someone else's penalty cleared" , SpotterPhrase::Category::Opponents },
        { "pit_entry_other",  "someone else entered the pits - a full grid's pit traffic is the noisiest event stream there is and says nothing about your race" , SpotterPhrase::Category::Opponents },
        { "pit_exit_other",  "someone else left the pits" , SpotterPhrase::Category::Opponents },
        // -- not race events -------------------------------------------------
        { "spectate_target", "the camera cut to a different rider - the auto-director cuts constantly, so this ships silent" , SpotterPhrase::Category::Opponents },
        { "hotkey_triggered", "the Spotter Cue hotkey was pressed" , SpotterPhrase::Category::General },
        { "voice_preview", "cycling onto this pack in the settings. Optional even for a recorded pack - absent, the plugin builds a preview from the number clips every pack has - and it never reaches the subtitle" , SpotterPhrase::Category::General },
    };
    return kKeys;
}

// A few cues are REFINEMENTS of a more general one, and a pack that defines
// only the general key should cover them: without the fallback, a pack that
// defines only the general key looks complete and is silent, whenever a
// refinement fires. The trend keys exist for packs that want DIFFERENT words
// or a different clip per trend (a recorded voice cannot say "gaining" out of
// a template); everyone else writes one line with {trend_ahead} in it.
//
// One level only: a fallback's fallback is not consulted, so this cannot
// loop and the chain a pack author has to hold in their head stays short.
inline const char* fallbackCueKey(const std::string& key) {
    struct Pair { const char* from; const char* to; };
    static const Pair kPairs[] = {
        { "gap_behind_closing",  "gap_behind" },
        { "gap_behind_dropping", "gap_behind" },
        { "sector_completed_faster",  "sector_completed" },
        { "sector_completed_slower",  "sector_completed" },
        // The on_pace tiers fall back to the session-best wording: a pack that
        // says only "you're up, go get it" means it whichever reference it is.
        // sector_best deliberately has NO fallback to sector_completed - that
        // one ships silent, and inheriting it would make a rare cue speak in
        // the words of one the author chose to mute.
        { "on_pace_personal_best", "on_pace_session_best" },
        { "on_pace_record",        "on_pace_session_best" },
        // penalty_change_other deliberately has NO fallback, where its
        // siblings do. It ships commented out because a rival's penalty being
        // revised is not your race, and a fallback to penalty_change would
        // undo that the moment the shipped pack defines the general key -
        // which it does. The default-quiet intent outranks the convenience.
        // The session-kind green flags are the opposite case: a pack that says
        // only `session_started` means it for all of them.
        { "fuel_critical",       "fuel_low" },
        { "practice_started",    "session_started" },
        { "quali_started",       "session_started" },
        { "warmup_started",      "session_started" },
    };
    for (const Pair& p : kPairs) {
        if (key == p.from) return p.to;
    }
    return nullptr;
}

// Cues whose whole value is that they describe where a rider is RIGHT NOW.
// Spoken late these are worse than silence - "rider left" after they have
// gone asks you to leave room that is no longer needed - so the queue expires
// them instead of delivering them behind a backlog (spotter_queue.h).
//
// Deliberately NOT the hazards: a blue flag or a rider down ahead stays true
// for as long as the situation lasts, so late is still worth hearing.
inline bool isPerishableCue(const std::string& key) {
    return key == "rider_left" || key == "rider_right" ||
           key == "riders_both_sides" ||
           key == "rider_behind" || key == "rider_behind_clear" ||
           key == "lapping_traffic";
}

inline bool isCueKey(const std::string& key) {
    for (const CueKeyInfo& k : allCueKeys()) {
        if (key == k.key) return true;
    }
    return false;
}

inline const char* cueKeyFor(EventLogType type, bool focused) {
    switch (type) {
        case EventLogType::SessionStarted:    return "session_started";
        case EventLogType::SessionPreStart:   return "session_prestart";
        case EventLogType::SessionComplete:   return "session_ended";
        case EventLogType::SessionStateChange:return "session_state";
        case EventLogType::FastestLap:
            return focused ? "fastest_lap_you" : "fastest_lap_other";
        case EventLogType::Penalty:
            return focused ? "penalty_you" : "penalty_other";
        case EventLogType::PenaltyClear:
            return focused ? "penalty_clear_you" : "penalty_clear_other";
        case EventLogType::PenaltyChange:
            // Split like the other two penalty types, for the same reason: the
            // handler logs this against the OFFENDING rider, so without a
            // _you/_other pair a rival's revision would speak the unattributed
            // "Penalty changed." AND be filed under Opponents while the
            // registry, the shipped pack and the reference all call
            // penalty_change a General cue - the switch the docs name would
            // not be the switch that silences it.
            return focused ? "penalty_change" : "penalty_change_other";
        case EventLogType::RiderRetired:
            return focused ? "retired_you" : "retired_other";
        case EventLogType::RiderDSQ:
            return focused ? "disqualified_you" : "disqualified_other";
        case EventLogType::RiderDNS:          return "did_not_start_other";
        case EventLogType::OvertimeStarted:   return "overtime_started";
        case EventLogType::SessionTimeExpired:return "session_time_expired";
        // TWO moments, not one. The leader starting the last lap is a fact
        // about the RACE and fires with no raceNum, so it stays session-level
        // ("final_lap"). You starting YOUR last lap is a different moment -
        // a lap or more later if you are down the order, and the one that
        // actually concerns you - and fires with your raceNum, so it resolves
        // "you" and lands on "final_lap_you".
        //
        // The split leans on the EMITTER: the leader's fires without a number,
        // yours fires with one, and a rider who is neither fires nothing at
        // all. An emitter that passes no raceNum leaves a mix's {event_rider}
        // nothing to resolve, and packs play silence.
        case EventLogType::FinalLap:
            return focused ? "final_lap_you" : "final_lap";
        case EventLogType::RiderFinished:
            return focused ? "finished_you" : "finished_other";
        case EventLogType::LeaderChange:
            return focused ? "leader_you" : "leader_other";
        case EventLogType::PitEntry:
            return focused ? "pit_entry_you" : "pit_entry_other";
        case EventLogType::PitExit:
            return focused ? "pit_exit_you" : "pit_exit_other";
        case EventLogType::Director:          return nullptr;
        default:                              return nullptr;
    }
}

// Parse a pack ini's text. Tolerant by design (hand-edited files): unknown
// sections and malformed lines are skipped, values may be empty (an explicit
// mute), inline comments are NOT supported in values (phrases legitimately
// contain anything). Never throws.
inline Pack parse(const std::string& text) {
    Pack pack;
    std::vector<std::string>& rejected = pack.rejectedMixes;
    bool inCues = false;
    bool inMix = false;
    bool inPack = false;
    size_t pos = 0;
    while (pos <= text.size()) {
        size_t eol = text.find('\n', pos);
        if (eol == std::string::npos) eol = text.size();
        std::string line = text.substr(pos, eol - pos);
        pos = eol + 1;

        // Trim (spaces, tabs, CR).
        const char* ws = " \t\r";
        const size_t b = line.find_first_not_of(ws);
        if (b == std::string::npos) continue;
        const size_t e = line.find_last_not_of(ws);
        line = line.substr(b, e - b + 1);

        if (line[0] == ';' || line[0] == '#') continue;
        if (line[0] == '[') {
            inCues = (line == "[Cues]");
            inMix = (line == "[Mix]");
            inPack = (line == "[pack]");
            continue;
        }
        if (inPack) {
            // One key, and unknown ones are ignored rather than rejected: this
            // section is a label, not a contract, and a typo here must not cost
            // a pack its words.
            const size_t eq = line.find('=');
            if (eq != std::string::npos) {
                std::string k = line.substr(0, eq);
                std::string v = line.substr(eq + 1);
                const size_t ke = k.find_last_not_of(ws);
                k = (ke == std::string::npos) ? std::string() : k.substr(0, ke + 1);
                const size_t vb = v.find_first_not_of(ws);
                v = (vb == std::string::npos) ? std::string() : v.substr(vb);
                if (k == "name" && !v.empty()) pack.displayName = v;
            }
            continue;
        }
        if (!inCues && !inMix) continue;

        const size_t eq = line.find('=');
        if (eq == std::string::npos) continue;
        std::string key = line.substr(0, eq);
        std::string value = line.substr(eq + 1);
        const size_t ke = key.find_last_not_of(ws);
        if (ke == std::string::npos) continue;
        key = key.substr(0, ke + 1);
        const size_t vb = value.find_first_not_of(ws);
        value = (vb == std::string::npos) ? std::string() : value.substr(vb);

        if (inMix) {
            // Numeric, so an inline comment IS strippable here (unlike a cue
            // phrase, where ';' is legitimate text).
            if (key != "gap_ms") continue;
            const size_t sc = value.find_first_of(";#");
            if (sc != std::string::npos) value = value.substr(0, sc);
            const size_t ve = value.find_last_not_of(ws);
            if (ve == std::string::npos) continue;
            value = value.substr(0, ve + 1);
            // Hand-editable file: parse without stoi, which throws on junk.
            size_t i = 0;
            int sign = 1;
            if (value[0] == '-' || value[0] == '+') {
                sign = value[0] == '-' ? -1 : 1;
                i = 1;
            }
            if (i >= value.size()) continue;
            int n = 0;
            for (; i < value.size(); ++i) {
                if (value[i] < '0' || value[i] > '9') { n = -1; break; }
                n = n * 10 + (value[i] - '0');
                if (n > 100000) { n = -1; break; }
            }
            if (n < 0) continue;               // junk: keep the default
            pack.gapMs = sign * n;             // assemble() clamps the range
            pack.hasGapMs = true;
            continue;
        }

        // A wav/chunk name must not escape the pack folder: reject path
        // separators and parent references outright (trust boundary - packs
        // are shared files).
        auto safeName = [](const std::string& n) {
            return n.find('/') == std::string::npos &&
                   n.find('\\') == std::string::npos &&
                   n.find("..") == std::string::npos;
        };
        auto hasSuffix = [&key](const char* suffix) {
            const size_t len = 4;  // both suffixes are 4 chars
            return key.size() > len &&
                   key.compare(key.size() - len, len, suffix) == 0;
        };

        if (hasSuffix("_wav")) {
            if (safeName(value)) {
                pack.wavs[key.substr(0, key.size() - 4)] = value;
            }
        } else if (hasSuffix("_mix")) {
            // Whitespace-separated tokens: wav names or {event_rider}/{event_time}.
            // One unsafe token rejects the whole sequence - a mix with a
            // hole in it would drop words silently.
            std::vector<std::string> tokens;
            bool ok = true;
            size_t tb = 0;
            while (tb < value.size()) {
                const size_t te = value.find_first_of(" \t", tb);
                const std::string tok =
                    value.substr(tb, te == std::string::npos ? std::string::npos
                                                             : te - tb);
                if (!tok.empty()) {
                    // A `{...}` token is a PLACEHOLDER and must be one the
                    // mixer knows; anything else is a wav filename. Checking
                    // only two of the placeholders let the other three - and
                    // every typo - through as filenames, to fail silently at
                    // playback (see SpotterMix::isMixToken).
                    const bool braced =
                        tok.front() == '{' && tok.back() == '}';
                    if (braced ? !SpotterMix::isMixToken(tok)
                               : !safeName(tok)) {
                        ok = false;
                        rejected.push_back(key + " (" + tok + ")");
                        break;
                    }
                    tokens.push_back(tok);
                }
                if (te == std::string::npos) break;
                tb = te + 1;
            }
            if (ok && !tokens.empty()) {
                pack.mixes[key.substr(0, key.size() - 4)] = tokens;
            }
        } else {
            pack.phrases[key] = value;
        }
    }
    return pack;
}

// The selectable variant keys for a cue: the base key (always - it exists
// via the shipped pack even when this one doesn't mention it) plus each
// <key>_N the pack defines in ANY of the three maps, contiguously from 2
// (a gap ends the scan, so authors see missing numbers as "not working"
// immediately instead of a variant that silently never plays).
inline std::vector<std::string> variantKeys(const Pack& pack,
                                            const std::string& baseKey) {
    std::vector<std::string> keys;
    keys.push_back(baseKey);
    for (int n = 2; n <= 9; ++n) {
        const std::string k = baseKey + "_" + std::to_string(n);
        if (pack.phrases.count(k) || pack.wavs.count(k) ||
            pack.mixes.count(k)) {
            keys.push_back(k);
        } else {
            break;
        }
    }
    return keys;
}

// Lay a selected pack's WORDS over the shipped pack's, which is what makes a
// twenty-cue recorded voice speak the other seventy in the shipped wording. An
// EMPTY value in `sel` is an explicit mute and must survive, so this assigns
// rather than skipping blanks.
//
// ...except that a cue `sel` DEFINES owns its alternates. The variant pick is
// made across the MERGED phrases while audio comes from the selected pack
// alone, so inheriting a shipped `rider_behind_2` would let a recorded pack
// roll a variant it has no clip for and drop to TTS - silence on a machine
// with no SAPI, for a cue it recorded properly. The shipped pack carries
// alternates on its most-heard cues, so this is the live path for every
// recorded pack, not a precaution.
//
// "Defines" spans all three maps, the same test variantKeys applies, and NOT
// just the phrase map: a pack may give a cue only a `_wav` (its wording then
// inherited from here), and that pack owns the cue every bit as much as one
// that reworded it - more so, since audio is exactly what it would lose.
//
// Here rather than inline in reloadCuePack because that function is half file
// I/O: pure map arithmetic is the part worth testing, and it could not be
// reached without staging two pack folders.
inline std::map<std::string, std::string> mergePhrases(const Pack& base,
                                                       const Pack& sel) {
    auto selDefines = [&sel](const std::string& key) {
        return sel.phrases.count(key) || sel.wavs.count(key) ||
               sel.mixes.count(key);
    };
    std::map<std::string, std::string> out = base.phrases;
    for (auto it = out.begin(); it != out.end();) {
        const std::string root = stripVariantSuffix(it->first);
        const bool isVariant = root != it->first;
        it = (isVariant && selDefines(root)) ? out.erase(it) : std::next(it);
    }
    for (const auto& kv : sel.phrases) out[kv.first] = kv.second;
    return out;
}

// Expand every {variable} in a template, then tidy the punctuation an empty
// expansion leaves behind, so "lap, {event_rider}, {event_time}." degrades to
// "lap, rider five." / "lap." instead of "lap, , .".
//
// The variable set is SpotterVars' table, not a chain of compares here: any
// variable works in any cue (see that header for why that is the whole
// point). An UNKNOWN name is copied through verbatim, braces and all - a
// pack author's typo should be visible on screen rather than silently
// swallowed, which is the only way they find out from the outside.
// OPTIONAL GROUPS, `[...]`, are what make free variable combination actually
// work. The punctuation tidy-up below can drop an orphaned comma, but it
// cannot know that the word "to" in `{gap_to_ahead} to {rider_ahead}` belongs to
// the pair - leading the race, that template reads "P one, to." A group is
// dropped WHOLE when any variable inside it is empty:
//
//   lap_completed = P {position}[, {gap_to_ahead} to {rider_ahead}].
//     mid-pack -> "P four, one point two to rider sixty five."
//     leading  -> "P one."
inline std::string expand(const std::string& tmpl,
                          const SpotterVars::Vars& vars) {
    // Expand variables in [from, to), reporting whether any KNOWN variable
    // resolved to nothing - which is what makes a group drop.
    auto expandRange = [&vars, &tmpl](size_t from, size_t to,
                                      bool* sawEmpty) -> std::string {
        std::string out;
        for (size_t i = from; i < to;) {
            if (tmpl[i] == '{') {
                const size_t close = tmpl.find('}', i + 1);
                // A brace with no partner is literal text, not a broken
                // variable: stopping the scan would drop the rest of the line.
                if (close != std::string::npos && close < to) {
                    const std::string name = tmpl.substr(i + 1, close - i - 1);
                    if (const std::string* v = SpotterVars::lookup(vars, name)) {
                        if (v->empty() && sawEmpty) *sawEmpty = true;
                        out += *v;
                        i = close + 1;
                        continue;
                    }
                }
            }
            out += tmpl[i++];
        }
        return out;
    };

    std::string out;
    out.reserve(tmpl.size() + 64);
    for (size_t i = 0; i < tmpl.size();) {
        if (tmpl[i] == '[') {
            const size_t close = tmpl.find(']', i + 1);
            if (close != std::string::npos) {
                bool sawEmpty = false;
                const std::string inner =
                    expandRange(i + 1, close, &sawEmpty);
                if (!sawEmpty) out += inner;
                i = close + 1;
                continue;
            }
            // Unpaired, so literal - same rule as an unpaired brace.
        }
        if (tmpl[i] == '{') {
            const size_t close = tmpl.find('}', i + 1);
            if (close != std::string::npos) {
                const std::string name = tmpl.substr(i + 1, close - i - 1);
                if (const std::string* v = SpotterVars::lookup(vars, name)) {
                    out += *v;
                    i = close + 1;
                    continue;
                }
            }
        }
        out += tmpl[i++];
    }
    // Punctuation tidy-up passes (order matters: collapse separators first,
    // then fix separator-before-terminator, then whitespace).
    auto replaceAll = [](std::string& s, const char* from, const char* to) {
        const size_t fromLen = std::string(from).size();
        for (size_t p = s.find(from); p != std::string::npos;
             p = s.find(from, p)) {
            s.replace(p, fromLen, to);
        }
    };
    replaceAll(out, ", ,", ",");
    replaceAll(out, ",,", ",");
    replaceAll(out, ", .", ".");
    replaceAll(out, " .", ".");
    replaceAll(out, "  ", " ");
    // A terminal after ! or ? is the template's own full stop landing after a
    // dropped "{var}." tail - "New track record!." - and the bang keeps the
    // sentence. Both spellings, since the space pass above may or may not have
    // run between them.
    replaceAll(out, "! .", "!");
    replaceAll(out, "? .", "?");
    replaceAll(out, "!.", "!");
    replaceAll(out, "?.", "?");
    // Leading separator from an empty leading placeholder.
    while (!out.empty() && (out[0] == ',' || out[0] == ' ')) out.erase(0, 1);
    // ...and a TRAILING one from an empty trailing placeholder in a template
    // that ends without punctuation: "P {position}, {gap}" with the gap empty
    // left "P two, " - the comma passes only fire against a terminal.
    while (!out.empty() && (out.back() == ',' || out.back() == ' ')) {
        out.pop_back();
    }
    // OPEN WITH A CAPITAL, whatever the line turned out to start with. The
    // rider words are built for mid-sentence ("rider four seventy six" -
    // SpotterPhrase::riderRef), so a template leading with {event_rider}, or
    // one whose only capitalised words sat in a group that dropped, renders
    // lowercase. Speech does not care; the SUBTITLE shows the line as written,
    // where it reads as a bug.
    //
    // Here rather than as an authoring rule: the alternative was telling pack
    // authors never to start a row with a variable, which is a rule to
    // remember, applies to packs nobody reviews, and rules out phrasings that
    // are otherwise fine ("{event_rider} goes quickest"). One line covers
    // every pack that will ever exist instead.
    //
    // UTF-8 SAFE ONLY BECAUSE THE LOCALE IS NEVER SET. Templates carry rider
    // names, so out[0] can be a UTF-8 lead byte; under "C" std::toupper maps
    // a-z and passes everything >= 0x80 through, leaving the sequence intact.
    // Nothing in this tree calls setlocale/imbue, so that holds - but the
    // safety is in that ABSENCE, not in this line. A setlocale(LC_ALL, "")
    // added for an unrelated reason would make this mangle one lead byte of an
    // accented name; the fix then is to skip bytes >= 0x80 here.
    if (!out.empty()) {
        out[0] = static_cast<char>(
            std::toupper(static_cast<unsigned char>(out[0])));
    }
    return out;
}

}  // namespace SpotterCuePack
