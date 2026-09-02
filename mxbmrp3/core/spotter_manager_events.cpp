// ============================================================================
// core/spotter_manager_events.cpp
// SpotterManager's race-event cue decisions: session reset, race events, lap
// and split reports, personal/session bests, crashes, gate drop, spectate
// target, the deferred-cue queue, and the behind-gap stopwatch cues. Split
// from spotter_manager.cpp; every method body is unchanged.
// ============================================================================
// file-budget: 1350 one handler per race event, already a split product; next carve-out is the gap cues
#include "spotter_manager.h"

#include "fuel_estimate.h"
#include "plugin_data.h"
#include "hud_manager.h"
#include "../hud/fuel_widget.h"
#if GAME_HAS_RECORDS_PROVIDER
#include "../hud/records_hud.h"   // the track record, MX Bikes only
#endif
#include "stats_manager.h"
#include "spotter_manager_internal.h"
#include "../diagnostics/logger.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#if MXBMRP3_SPOTTER_PROBE
static void debugLogStandingsAt(const PluginData& pd, const char* where);
#endif

// Everything one session must not carry into the next. Called from the
// SessionStarted branch of onRaceEvent — and, separately, from that function's
// spotter-is-off early return.
void SpotterManager::resetSessionState() {
    m_pendingSessionEnd = PendingSessionEnd{};
    m_sessionEndSpoken = false;
    m_finishSpokenFocused = false;
    m_sessionEndHidePosition = false;
    // A fresh green flag re-arms the once-per-session milestones (the time
    // path also self-resets on a clock rewind, but the lap path has no clock
    // to notice) and wipes the pace tracker's crossings — stale timing points
    // must not resolve gaps across sessions.
    m_milestones.reset();
    m_pace.reset();
    m_penaltyTotalMs = 0;
    m_penaltyColumnStale = false;
    m_fuelLowSaid = m_fuelCriticalSaid = false;
    m_lastReportedPos = 0;
    m_lastReportedPosNum = -1;
    // Everything else the previous session left behind. m_pace.reset() wipes
    // the TRACKER's memory, but these are the manager's own caches of the same
    // run, and each of them speaks if it survives:
    //   - the last resolved gaps still carry hasTrend, so race 2's opening
    //     cue reports race 1's "gaining";
    //   - the last split is what the next sector time is subtracted from, so a
    //     matching lap number across a restart yields a plausible, wrong
    //     sector (or a negative one, and silence);
    //   - a lap armed at the end of one session and never flushed (no
    //     classification followed it) flushes into the next session's first
    //     one, reporting the old lap's time and position.
    m_lastAhead = SpotterPace::Gap{};
    m_lastBehind = SpotterPace::Gap{};
    m_lastSplitLap = -1;
    m_lastSplitRider = -1;
    m_lastSplitCumMs = 0;
    m_pendingLap.armed = false;
    m_pendingFastest = PendingFastest{};
}

void SpotterManager::onRaceEvent(EventLogType type, int raceNum,
                                 int focusedRaceNum, int sessionTimeMs,
                                 const EventNumbers& nums) {
    // Subtitles-only mode is real: intake runs for either switch, and only
    // the audio dispatch at the end of emitCue() checks m_enabled.
    if (!m_enabled && !m_subtitles) {
        // ...with the session wipe as the ONE exception, for the same reason
        // the category gate is applied in emitCue rather than here: both
        // switches are live settings, so turning the spotter off, crossing a
        // session boundary and turning it back on would otherwise carry the
        // previous session's pace, gaps, split and pending lap into the new
        // one — the three bugs resetSessionState()'s own comment enumerates,
        // reached by a different door.
        if (type == EventLogType::SessionStarted) resetSessionState();
        return;
    }

#if MXBMRP3_SPOTTER_PROBE
    // TEMP-DEBUG(spotter-vs-standings): the FINISH is the one moment the split
    // and S/F probes never reach — flushDeferredCues returns before it once your
    // race is over — and it is where {gap_to_leader} is spoken twice
    // (finished_you, then session_ended). What the official column says at that
    // instant is the open question the next log has to answer.
    if (raceNum >= 0 && raceNum == focusedRaceNum &&
        (type == EventLogType::RiderFinished ||
         type == EventLogType::SessionComplete)) {
        debugLogStandingsAt(PluginData::getInstance(),
                            type == EventLogType::RiderFinished
                                ? "your finish" : "session complete");
    }
#endif

    // Filed by SUBJECT first (see SpotterPhrase::Category): your own pit
    // call is not "other riders", and a rival's fastest lap is.
    const SpotterPhrase::Category cat = SpotterPhrase::categoryFor(
        type, SpotterPhrase::subjectOf(raceNum, focusedRaceNum));
    // NO early return on the category here, deliberately. emitCue applies the
    // gate at the end, and returning up here would skip the SessionStarted
    // branch below — the ONLY place per-session state is wiped (milestones,
    // the pace tracker, the cached gaps either side, the last split, the
    // pending lap and fastest-lap holds, the fuel latches, the last reported
    // position). SessionStarted is a General cue, so an early return here
    // would let muting General carry every one of those across a session
    // boundary: race 2's first crossing reporting race 1's trend, a sector
    // subtracted across a restart, a stale lap flushing into the next session.
    //
    // The work between here and there is per-EVENT, not per-frame, so nothing
    // is bought by skipping it.

    const bool focused = raceNum >= 0 && raceNum == focusedRaceNum;

    // The lap-quality LADDER, which the on-screen notices have had all along
    // (race_lap_handler.cpp: "All-time PB supersedes fastest lap and session
    // PB") and the spotter sat outside of. Only your own fastest lap leaks: it
    // arrives through the EVENT LOG, which logs it unconditionally because the
    // race feed wants it, while the notices are chosen by the ladder. So a lap
    // that was both spoke twice, back to back, saying the same time — "New
    // personal best, one oh six point six." / "Fastest lap, nice work, one oh
    // six point six." Both mxbclub and the demo weekend do it.
    //
    // Keyed by the LAP TIME rather than a bare flag. Within one crossing the
    // handler calls onPersonalBest, then logs FastestLap, then
    // onRaceLapCompleted — set, consume, clear — so a flag would work, but it
    // would work only for as long as that order holds, and nothing enforces
    // the order. Matching the time means a latch that somehow outlived its lap
    // can still only suppress a lap of exactly the same time, which is the lap
    // it was set for.
    if (type == EventLogType::FastestLap && focused &&
        m_higherLapCueTimeMs > 0 &&
        nums.lapTimeMs == m_higherLapCueTimeMs) {
        return;
    }

    // HELD until the next classification, and only one is held (see
    // PendingFastest).
    // Joining a lobby mid-session replays every rider's whole lap history in a
    // single instant; each replayed lap that improved the overall best spoke,
    // so a real join announced seven fastest laps in one millisecond, none of
    // them a moment the player was there for. Holding one collapses the replay
    // to the line that is still true — the session's fastest — because a
    // classification cannot arrive inside a callback batch.
    //
    // BELOW the ladder check, not above it: that latch is set and cleared
    // within one crossing (onPersonalBest, then this, then
    // onRaceLapCompleted), so by the time a held cue flushes it is long gone
    // and a lap that is both a PB and the fastest would announce itself twice.
    // Everything above this point is a decision about the instant the lap
    // arrived; only the emission waits.
    if (type == EventLogType::FastestLap && !m_emittingPendingFastest) {
        m_pendingFastest = { true, raceNum, focusedRaceNum, sessionTimeMs,
                             nums };
        return;
    }

    if (type == EventLogType::SessionComplete && !m_emittingPendingSessionEnd) {
        // ONCE per session: RACE_OVER and FINISHED both map to SessionComplete
        // in the session handler, so a session that walks both states would
        // speak "That's Race 2 done" twice, seconds apart.
        if (m_sessionEndSpoken || m_pendingSessionEnd.armed) return;
        // In a race the game can complete the session BEFORE the subject's own
        // finish reaches us -- for the WINNER the two arrive in one batch, in
        // tape order [SessionComplete, RiderFinished], which spoke "That's
        // Race 2 done, P one" and then "That's the flag, P one" -- the flag
        // after the wrap-up, with the position twice. Held to the next
        // classification (the lap report's pattern), the finish cue speaks
        // first and the wrap-up follows it. Non-race sessions have no finish
        // cue and speak immediately, as before.
        if (PluginData::getInstance().isRaceSession() && !subjectRaceOver()) {
            m_pendingSessionEnd = { true, raceNum, sessionTimeMs, nums };
            return;
        }
    }

    const char* key = SpotterCuePack::cueKeyFor(type, focused);
    if (!key) return;  // never-spoken, never-overridable (Director)

    // Session-kind refinements the event alone cannot make: non-race green
    // flags get their own keys, and the P1 rider taking the flag (not you) is
    // its own moment. Both are a change of KEY — the words for each live in
    // the shipped pack like every other cue's.
    if (type == EventLogType::SessionStarted) {
        resetSessionState();
        // session_started means "this session is active", for every
        // session kind — the generic key says which via {session_name}, so a
        // pack writes ONE line. The
        // per-kind keys stay as optional refinements (they fall back to
        // session_started), for a recorded voice that cannot stitch a session
        // name, or wording you want different per kind.
        //
        // It does NOT mean "green flag": a standing start holds on the grid
        // after this fires, and the gate falling is gate_drop.
        switch (Game::Adapter::toCanonicalSession(
            PluginData::getInstance().getSessionData().session,
            PluginData::getInstance().getSessionData().eventType)) {
            case Unified::Session::Practice:
                key = "practice_started";
                break;
            case Unified::Session::PreQualify:
            case Unified::Session::QualifyPractice:
            case Unified::Session::Qualify:
                key = "quali_started";
                break;
            case Unified::Session::Warmup:
                key = "warmup_started";
                break;
            default:
                break;  // races and everything else keep session_started
        }
    } else if (type == EventLogType::SessionStateChange) {
        // "Waiting" is not a state anything entered — it is what
        // getSessionStateString() returns when NO known bit is set, i.e. the
        // idle gap between sessions. A race that had just said "Race 2
        // complete, you finished P eleven" followed it ten seconds later with
        // "Race 2, WAITING", which is the plugin narrating its own enum. The
        // states worth hearing all have a bit, and most have a cue of their
        // own; this leaves Cancelled as the one session_state still speaks for.
        namespace St = PluginConstants::SessionState;
        if (!(m_sessionState & (St::CANCELLED | St::RACE_OVER | St::PRE_START |
                                St::SIGHTING_LAP | St::FINISHED |
                                St::IN_PROGRESS))) {
            return;
        }
        // "Session update" told you something changed but not to what, which
        // is the only interesting part; {session_state} carries it, and
        // {session_name} the label the Session HUD shows.
    } else if (type == EventLogType::RiderFinished && !focused &&
               nums.position == 1) {
        key = "finished_leader";
    } else if (type == EventLogType::RiderFinished && focused) {
        // finished_you is about to speak, and it carries the position -- the
        // session wrap-up that follows moments later must not read it back.
        m_finishSpokenFocused = true;
    } else if (type == EventLogType::SessionComplete) {
        m_sessionEndSpoken = true;
        m_sessionEndHidePosition = m_finishSpokenFocused;
    }

    const SpotterPhrase::LapTimeParts timeParts =
        SpotterPhrase::lapTimePartsMs(nums.lapTimeMs);
    const int penaltySecs = SpotterPhrase::penaltyWholeSeconds(nums);
    // The BONUS lap count — the one number that is exactly true wherever the
    // leader is when the clock expires. The template says what it counts
    // ("after this one"); see SpotterPhrase::lapsWords.
    const int lapsLeft = nums.bonusLaps;
    SpotterVars::Vars vars;
    vars.eventRider = SpotterPhrase::riderRef(raceNum, focusedRaceNum);
    vars.eventTime = SpotterPhrase::lapTimeWordsMs(nums.lapTimeMs);
    // A rival's lap measured against yours. Only for ANOTHER rider's lap:
    // for your own the {gap_to_*} family already answers it, and comparing
    // your new fastest lap with the best it just replaced is a race against
    // yourself that reads as noise.
    if (!focused && timeParts.valid && timeParts.totalMs > 0) {
        const PluginData& pdata = PluginData::getInstance();
        const StandingsData* me = pdata.getStanding(pdata.getDisplayRaceNum());
        // -1 while your best is your opening lap: a gate start is not a
        // reference, however true the arithmetic. See bestLapIsFirstLap().
        const int myBest =
            (me && !bestLapIsFirstLap()) ? me->bestLap : -1;
        const IdealLapData* myIdeal = pdata.getIdealLapData();
        const int myLast = myIdeal ? myIdeal->lastLapTime : -1;
        auto against = [&](int ref, std::string& out) {
            if (ref <= 0) return;
            const int d = timeParts.totalMs - ref;
            // A dead heat says nothing: "zero point zero slower" is the exact
            // wording the reference() helper suppresses for your own laps,
            // and a rival matching you to the millisecond earns the same
            // silence — the optional group drops.
            if (d == 0) return;
            out = SpotterPhrase::gapWordsMs(d) +
                  (d < 0 ? " quicker" : " slower");
        };
        against(myBest, vars.eventGapToBestLap);
        against(myLast, vars.eventGapToLastLap);
    }
    vars.penaltySeconds = SpotterPhrase::secondsWords(penaltySecs);
    vars.overtimeLaps = SpotterPhrase::lapsWords(lapsLeft);

    // YOUR PENALTY TOTAL, at the instant the penalty lands — which the
    // standings cannot give you, because this callback IS how the plugin
    // learns about it and the classification column catches up a beat later.
    // fillAmbientVars serves that column for every other cue, and it is right
    // there; here it would be short by exactly the penalty being announced.
    //
    // MAX, not addition: the classification is authoritative when it HAS
    // absorbed the penalty, and our own tally is when it has not, and a total
    // only ever grows. Adding unconditionally would double-count whenever the
    // classification won the race; taking the column alone under-reports
    // whenever it lost, which is the common case. This is right either way,
    // and self-corrects on the next penalty if a session was joined with some
    // already served.
    if (type == EventLogType::Penalty && focused && nums.penaltyMs > 0) {
        const StandingsData* me = PluginData::getInstance().getStanding(raceNum);
        // After a CLEAR or a REVISION the column is stale until the next
        // classification absorbs it — a fresh 5s penalty right after a clear
        // took max(stale 10s, 5s) and announced "ten seconds in total" for a
        // real total of five. While the latch is up the tally is the only
        // truth; the flush drops the latch once a classification has landed.
        const int column =
            (me && !m_penaltyColumnStale) ? me->penalty : 0;
        const int tallied = m_penaltyTotalMs + nums.penaltyMs;
        m_penaltyTotalMs = column > tallied ? column : tallied;
        // Only once there is more than the one just announced: on a first
        // penalty the total IS the amount, and "five seconds, five seconds in
        // total" is exactly the redundancy an optional group exists to drop.
        if (m_penaltyTotalMs > nums.penaltyMs) {
            vars.penaltyTotal =
                SpotterPhrase::secondsWords((m_penaltyTotalMs + 500) / 1000);
        }
    }
    // A total that only grows is right for penalties and wrong for the two
    // events that take one back, so both hand authority to the standings
    // again: a CLEAR zeroes the column by definition, and a REVISION settles
    // it to whatever the game decided. Without this the max() above would
    // defend a figure that no longer exists.
    if (type == EventLogType::PenaltyClear && focused) {
        m_penaltyTotalMs = 0;
        m_penaltyColumnStale = true;
    }
    if (type == EventLogType::PenaltyChange && focused) {
        const StandingsData* me = PluginData::getInstance().getStanding(raceNum);
        m_penaltyTotalMs = me && me->penalty > 0 ? me->penalty : 0;
        m_penaltyColumnStale = true;
    }
    emitCue(key, cat, std::move(vars), sessionTimeMs,
            (raceNum >= 0 && raceNum <= 999) ? raceNum : -1,
            timeParts.valid ? timeParts.composed : -1,
            timeParts.valid ? timeParts.tenths : -1, penaltySecs, lapsLeft);
    m_sessionEndHidePosition = false;
}

void SpotterManager::onRaceLapCompleted(int raceNum, int completedLaps,
                                        int lapTimeMs, bool lapValid) {
    if (!m_enabled && !m_subtitles) return;
    // Closes the lap-quality ladder armed by onPersonalBest / onSessionBest:
    // this runs last of the three calls one crossing makes, so the latch never
    // outlives the lap it was set for. See onRaceEvent's FastestLap branch.
    // Read before clearing -- the lap REPORT flushes later still, and needs to
    // know whether this lap's time has already been said out loud.
    const bool lapTimeAlreadySpoken =
        m_higherLapCueTimeMs > 0 && m_higherLapCueTimeMs == lapTimeMs;
    m_higherLapCueTimeMs = -1;
    const PluginData& pd = PluginData::getInstance();

    // Only the RACE-shaped parts of this callback are race-gated — the same
    // split onRaceSplit needed. Your position and your lap time are as real in
    // practice and qualifying as they are in a race (position is the index in
    // classification order, which every session has), and those are the
    // sessions you are most likely to want them read back in. Gating the whole
    // callback silenced lap_completed and the position gained/lost pair
    // everywhere except a race.
    //
    // The gap cues and the halfway milestone stay race-only below: a gap to
    // the rider behind is a race notion, and a practice session has no
    // half-distance.

    // Lap-race halfway_point rides the LEADER's crossings (a timed race gets its
    // milestones from the clock instead — see onTrackPositions).
    const int position = pd.getPositionForRaceNum(raceNum);
    if (pd.isRaceSession() && position == 1 &&
        pd.getSessionData().sessionLength <= 0 &&
        isCategoryEnabled(SpotterPhrase::Category::Timing)) {
        if (const char* cue = m_milestones.updateLaps(
                completedLaps, pd.getSessionData().sessionNumLaps)) {
            emitCue(cue, SpotterPhrase::Category::Timing, {},
                    pd.getSessionElapsedTime());
        }
    }

    // Every rider's S/F crossing is a timing point: record it, and see
    // whether it resolves a pending behind-gap report.
    const int nowMs = pd.getSessionElapsedTime();
    const long long sfKey = SpotterPace::pointKey(completedLaps,
                                                  SpotterPace::kSfPoint);
    const bool wantPace = pd.isRaceSession() &&
                          isCategoryEnabled(SpotterPhrase::Category::Timing);
    if (wantPace && raceNum == m_pace.pendingBehind()) {
        SpotterPace::Gap gap;
        if (m_pace.behindPoint(raceNum, sfKey, nowMs, gap)) {
            emitGapCue(gap, nowMs);
        }
    }
    if (pd.isRaceSession()) m_pace.otherPoint(raceNum, sfKey, nowMs);

    // The pit-board moment. Everything here is ONE event — your crossing —
    // so it is one set of variables handed to the cues that mark it, rather
    // than a separate cue per number.
    //
    // The gap to the rider ahead is NOT its own cue: it resolves at exactly
    // this instant and marks no moment of its own, so it is a value —
    // {gap_to_ahead} / {trend_ahead} / {gained_on_ahead} on the cues
    // below. The BEHIND gap is not like that and stays a cue — it resolves
    // when that rider reaches a point you already crossed, which is a moment
    // nothing else marks.
    if (raceNum != pd.getDisplayRaceNum() || raceNum < 0) return;

    // Fuel. Checked HERE rather than per frame: the estimate only moves when a
    // lap completes, so a lap crossing is both the cheapest place to look and
    // the moment the number actually changed.
    //
    // Read from FuelWidget rather than tracked here, so the laps spoken are the
    // laps shown — one history, not two that could drift apart. Same thresholds
    // as its colours, so the voice and the amber/red agree.
    //
    // ABOVE the Timing gate on purpose: fuel is a General cue, and below the
    // gate it would go with the lap report's deferred flush and never fire —
    // both keys registered, documented and shipped, silent. `every key in the
    // registry is emitted by something` (test_spotter_pack_census.cpp) is what
    // notices.
    {
        // Same reach as the track record above: a HUD accessor on the game
        // thread, which is where every caller of this function already is.
        const float laps =
            HudManager::getInstance().getFuelWidget().getLapsRemaining();
        if (laps >= 0.0f) {
            const bool critical = laps < FuelEstimate::kCriticalLaps;
            const bool low = laps < FuelEstimate::kWarnLaps;
            // `fuel_critical` ships commented out, so on the stock pack it has
            // no phrase and emitting it says nothing -- while still latching
            // BOTH flags, swallowing the `fuel_low` that would have
            // spoken. Pick the key that actually exists, so a pack decides what
            // it hears rather than what it is silenced by.
            const bool haveCritical = m_pack.phrases.count("fuel_critical") ||
                                      m_pack.wavs.count("fuel_critical") ||
                                      m_pack.mixes.count("fuel_critical");
            const bool sayCritical = critical && haveCritical;
            // Edge-triggered, and only ever downward: a warning that repeated
            // every lap would be the noisiest cue there is, and one that
            // re-fired after a splash of fuel would be lying about direction.
            if ((sayCritical && !m_fuelCriticalSaid) || (low && !m_fuelLowSaid)) {
                if (sayCritical) m_fuelCriticalSaid = true;
                m_fuelLowSaid = true;
                SpotterVars::Vars fv;
                // lapsWords, not numberWords: it carries the noun and agrees
                // with it, so a one-lap warning cannot say "Fuel getting low,
                // one laps."
                fv.fuelLaps = SpotterPhrase::lapsWords(static_cast<int>(laps));
                emitCue(sayCritical ? "fuel_critical" : "fuel_low", SpotterPhrase::Category::General, std::move(fv), nowMs);
            }
        }
    }

    if (!isCategoryEnabled(SpotterPhrase::Category::Timing)) return;
    if (position <= 0) return;
    const StandingsData* st = pd.getStanding(raceNum);
    if (st && pd.getSessionData().isRiderFinished(
                  st->numLaps, st->numLapsAtLeaderFinish)) {
        return;  // the checkered-flag cue owns the last crossing
    }

    // Measure the gap ahead FIRST, so the cues below carry the stopwatch value
    // — time between their crossing of this line and yours — rather than the
    // live estimate fillAmbientVars would otherwise supply.
    SpotterVars::Vars lapVars;
    const std::vector<int>& order = pd.getClassificationOrder();
    if (wantPace) {
        m_pace.myPoint(sfKey, nowMs);
        measureAheadGap(position, order, sfKey, nowMs, lapVars);
    }

    // DEFERRED, not emitted here. Position and the standings-derived gaps come
    // from the classification order, and at THIS callback that order has not
    // yet been rebuilt for the lap you just completed — so reading it now
    // gives the standings from before the crossing. That is a whole lap stale,
    // and it sounds plausible: "P four" when you just took third.
    //
    // The "finished P#" event log entry lives in batchUpdateStandings rather
    // than this handler for exactly this reason.
    //
    // What IS measured here is the ahead gap, above: that comes from the
    // spotter's own timing points, not from standings, and it is a stopwatch
    // reading that belongs to this instant. Measure at the crossing, speak
    // after the classification.
    m_pendingLap.armed = true;
    m_pendingLap.raceNum = raceNum;
    m_pendingLap.lapTimeMs = lapTimeMs;
    m_pendingLap.nowMs = nowMs;
    m_pendingLap.vars = lapVars;
    m_pendingLap.lapValid = lapValid;
    m_pendingLap.timeAlreadySpoken = lapTimeAlreadySpoken;

    if (!wantPace) return;
    if (static_cast<size_t>(position) < order.size()) {
        m_pace.armBehind(order[position]);
    }
}


#if MXBMRP3_SPOTTER_PROBE
// TEMP-DEBUG(spotter-vs-standings): dump what the STANDINGS TABLE holds at a
// timing point — the OFFICIAL, split-derived gaps the classification carries,
// not the live position-derived estimate. The live figure is printed beside it
// (rt=) purely for contrast: they are different measurements and are meant to
// differ, and seeing both is the point of the probe.
//
// Paired with the SPOTTER SAY lines, so one log answers "the spotter said X —
// what did the table say at that instant?". Three rows only (ahead / you /
// behind): a full grid is unreadable and the neighbours are what the cues talk
// about.
//
// KEPT ON PURPOSE while the spotter's timing is still being retraced against the
// standings table -- the decision is recorded at the SPOTTER SAY line this pairs
// with.
//
// REMOVE THIS FUNCTION, ITS FORWARD DECLARATION above onRaceEvent, and its THREE
// call sites (the event tap, the split tap, the lap report): grep
// TEMP-DEBUG(spotter-vs-standings).
//
// THE FORWARD DECLARATION IS THE ONE THAT KEEPS GETTING MISSED, and it is named
// here rather than only at the other note because this is the copy a remover
// reads first -- they arrive at the function, not at the log line. Leave it
// behind and a static is declared and never defined, which the cross build takes
// as an error, so the mistake is at least loud. Every count so far has been low
// ("two", then "four", then "five"); the grep is the authority.
static void debugLogStandingsAt(const PluginData& pd, const char* where) {
    const int me = pd.getDisplayRaceNum();
    if (me <= 0) return;
    const std::vector<int>& order = pd.getClassificationOrder();
    int myIdx = -1;
    for (size_t i = 0; i < order.size(); ++i) {
        if (order[i] == me) { myIdx = static_cast<int>(i); break; }
    }
    if (myIdx < 0) return;

    auto row = [&](int idx, const char* label, char* buf, size_t cap) {
        if (idx < 0 || idx >= static_cast<int>(order.size())) {
            snprintf(buf, cap, " | %s -", label);
            return;
        }
        const int rn = order[idx];
        const StandingsData* s = pd.getStanding(rn);
        if (!s) { snprintf(buf, cap, " | %s #%d ?", label, rn); return; }
        snprintf(buf, cap, " | %s P%d #%d laps=%d gap=%d gapLaps=%d rt=%d",
                 label, idx + 1, rn, s->numLaps, s->gap, s->gapLaps,
                 s->realTimeGap);
    };
    char aheadBuf[128] = {0}, meBuf[128] = {0}, behindBuf[128] = {0};
    row(myIdx - 1, "ahead", aheadBuf, sizeof(aheadBuf));
    row(myIdx, "you", meBuf, sizeof(meBuf));
    row(myIdx + 1, "behind", behindBuf, sizeof(behindBuf));
    DEBUG_INFO_F("STANDINGS [%s]%s%s%s", where, meBuf, aheadBuf, behindBuf);
}
#endif

void SpotterManager::onRaceSplit(int raceNum, int lapNum, int splitIndex,
                                 int splitTimeMs) {
    if (!m_enabled && !m_subtitles) return;
    if (splitIndex < 0 || splitIndex >= SpotterPace::kSfPoint) return;
    const PluginData& pd = PluginData::getInstance();
    const int nowMs = pd.getSessionElapsedTime();
    const long long key = SpotterPace::pointKey(lapNum, splitIndex);

#if MXBMRP3_SPOTTER_PROBE
    // TEMP-DEBUG(spotter-vs-standings)
    if (raceNum == pd.getDisplayRaceNum()) {
        char where[32];
        snprintf(where, sizeof(where), "S%d lap %d", splitIndex + 1, lapNum);
        debugLogStandingsAt(pd, where);
    }
#endif

    // The PACE half is race-only, and only that half. A gap to the rider
    // behind is a race notion — in practice the field is scattered across
    // out-laps and cool-downs, where "behind by four seconds" means nothing.
    // The SECTOR cue below is the opposite: practice and qualifying are
    // exactly where you want your sector times read back, so gating the whole
    // callback on isRaceSession() would silence it in the sessions it is for.
    if (pd.isRaceSession()) {
        if (isCategoryEnabled(SpotterPhrase::Category::Timing) &&
            raceNum == m_pace.pendingBehind()) {
            SpotterPace::Gap gap;
            if (m_pace.behindPoint(raceNum, key, nowMs, gap)) {
                emitGapCue(gap, nowMs);
            }
        }
        m_pace.otherPoint(raceNum, key, nowMs);
    }
    if (raceNum != pd.getDisplayRaceNum()) return;
    SpotterVars::Vars sectorPace;
    if (pd.isRaceSession()) {
        m_pace.myPoint(key, nowMs);
        // The gap ahead AT THIS SPLIT, measured the same way the lap crossing
        // measures it: their crossing time of this exact point against yours.
        // Same call, same per-rider trend memory — a split IS a timing point,
        // and the tracker already records every rider's. So
        // {gap_to_ahead} and {gained_on_ahead} work on the sector cues too,
        // three or four times a lap instead of once.
        measureAheadGap(pd.getPositionForRaceNum(raceNum),
                        pd.getClassificationOrder(), key, nowMs, sectorPace);
    }

    // Sector cue. The split times the game sends are CUMULATIVE from the lap
    // start, so this sector is the difference from the previous split — and
    // the comparison is against your best of THAT sector, which is the same
    // number IdealLapHud shows. Default-quiet in the shipped pack: three or
    // four of these a lap is a spotter talking every twenty seconds.
    if (splitTimeMs <= 0 || !isCategoryEnabled(SpotterPhrase::Category::Timing)) {
        return;
    }
    // Your splits keep arriving on the cool-down lap. They are not sectors of
    // a race you are still in, and reading them back against your best is a
    // comparison of a roll-in to a flying lap.
    if (subjectRaceOver()) return;
    // Cumulative-to-sector needs the PREVIOUS split of the same rider's same
    // lap. Watching a different rider now (a spectate cut, or the director
    // moving on) makes the stored figure theirs, and lap numbers collide
    // across riders — so the rider is part of what has to match.
    const bool sameRun = m_lastSplitLap == lapNum && m_lastSplitRider == raceNum;
    const int prevCumulative = sameRun ? m_lastSplitCumMs : 0;
    const int sectorMs = splitTimeMs - prevCumulative;
    const bool firstOfLap = splitIndex == 0;
    m_lastSplitLap = lapNum;
    m_lastSplitRider = raceNum;
    m_lastSplitCumMs = splitTimeMs;
    // Sector 1 IS the cumulative figure, so it is right even with nothing
    // stored; any later sector without its predecessor would be a cumulative
    // time announced as a sector time, which is a plausible-sounding wrong
    // number rather than a missing one.
    if (!sameRun && !firstOfLap) return;
    if (sectorMs <= 0) return;   // out-of-order split: say nothing

    // Carries the split's own ahead-gap measurement (above), so the sector
    // cues can name it just like the lap crossing does.
    SpotterVars::Vars sv = std::move(sectorPace);
    sv.sectorNumber = SpotterPhrase::numberWords(splitIndex + 1);
    // {event_time} is the ACCUMULATED time at this split — the elapsed lap
    // time, which is what TimingHud shows ("S2: 60.00") and what a rider reads
    // a split as. The sector on its own is {sector_duration}, for a pack that
    // wants F1-style sector times instead.
    sv.eventTime = SpotterPhrase::gapWordsMs(splitTimeMs);
    sv.sectorDuration = SpotterPhrase::gapWordsMs(sectorMs);

    // Every reference stores INDIVIDUAL sectors, so its accumulated time at
    // this split is the running sum of sectors 1..N. Comparing accumulated to
    // accumulated is what makes the spoken delta the same number the screen
    // shows for this crossing.
    auto accumulated = [splitIndex](int s1, int s2, int s3) {
        const int parts[3] = { s1, s2, s3 };
        int total = 0;
        for (int i = 0; i <= splitIndex && i < 3; ++i) {
            if (parts[i] <= 0) return -1;   // a gap in the reference: no answer
            total += parts[i];
        }
        return total;
    };
    // WITH ITS DIRECTION. gapWordsMs absolute-values, which is right for a gap
    // — you are never "minus two seconds behind" someone — and wrong for a
    // comparison: "zero point three" read identically whether the sector was
    // three tenths up or three tenths down, which is the only thing a rider
    // wants to know. Every other comparison in this file says which way (see
    // the fastest-lap and reference-lap deltas); only sector_delta_best_lap
    // escaped it, and only because its CUE KEY carries the direction instead.
    auto sectorDelta = [splitTimeMs](int ref, std::string& out) {
        if (ref <= 0) return;
        const int d = splitTimeMs - ref;
        out = SpotterPhrase::gapWordsMs(d) + (d < 0 ? " quicker" : " slower");
    };

    // The OPENING lap as the session's only reference is the gate/out-lap --
    // a fact, not a reference (bestLapIsFirstLap's own doc). The lap-level
    // comparisons are already guarded; these split-level doors were not, and
    // the first flying lap announced "Best sector one... Best sector two...
    // On for your session best, up sixty five point zero" against a 3:05
    // opener. Everything derived from the opener stays silent until a real
    // lap exists to compare with.
    const bool openerIsOnlyRef = bestLapIsFirstLap();
    int bestLapRef = -1;
    if (const LapLogEntry* bl = pd.getBestLapEntry()) {
        bestLapRef = accumulated(bl->sector1, bl->sector2, bl->sector3);
        if (!openerIsOnlyRef) sectorDelta(bestLapRef, sv.sectorDeltaBestLap);
    }
    // Your best for THIS SECTOR ALONE, from completed laps. Kept for the
    // sector_best cue below; every other reference here is accumulated.
    int sectorBestMs = -1;
    if (const IdealLapData* ideal = pd.getIdealLapData()) {
        const int bests[4] = { ideal->bestSector1, ideal->bestSector2,
                               ideal->bestSector3, ideal->bestSector4 };
        if (splitIndex >= 0 && splitIndex < 4) sectorBestMs = bests[splitIndex];
        sectorDelta(accumulated(ideal->bestSector1, ideal->bestSector2,
                                ideal->bestSector3),
                    sv.sectorDeltaIdeal);
        sectorDelta(accumulated(ideal->lastLapSector1, ideal->lastLapSector2,
                                ideal->lastLapSector3),
                    sv.sectorDeltaLastLap);
    }
    int alltimeRef = -1;
    if (const StatsPersonalBestData* pb =
            StatsManager::getInstance().getPersonalBest()) {
        if (pb->isValid()) {
            alltimeRef = accumulated(pb->sector1, pb->sector2, pb->sector3);
            sectorDelta(alltimeRef, sv.sectorDeltaAlltime);
        }
    }
    int recordRef = -1;
#if GAME_HAS_RECORDS_PROVIDER
    {
        int r1 = -1, r2 = -1, r3 = -1, r4 = -1;
        if (HudManager::getInstance().getRecordsHud().getFastestRecordSectors(
                r1, r2, r3, r4)) {
            recordRef = accumulated(r1, r2, r3);
            sectorDelta(recordRef, sv.sectorDeltaRecord);
        }
    }
#endif

    // Faster or slower than your best LAP at this point, which is the
    // comparison the screen's green/red is making — not "best ever in this
    // sector alone", which is a different (and rarer) claim.
    const char* cueKey = "sector_completed";
    if (bestLapRef > 0 && !openerIsOnlyRef) {
        cueKey = splitTimeMs < bestLapRef ? "sector_completed_faster"
                                          : "sector_completed_slower";
    }
    // DEFAULT-QUIET: three or four of these a lap is a spotter talking every
    // twenty seconds, so the shipped pack leaves the row commented out.
    //
    // The split time's parts go with it, or a recorded pack's
    // `sector_completed_mix = ... {event_time}` has nothing to resolve and
    // every sector call drops to TTS.
    // SECONDS decomposition, the same ruler the text above uses: the words
    // are gapWordsMs ("seventy five point three", matching TimingHud's
    // accumulated display), so the trailer must be too. lapTimePartsMs folds
    // into minutes past 60s -- composed 115 for 75.3s -- and a recorded
    // pack's sector mix then stitched "one fifteen point three" against its
    // own subtitle saying seventy-five. One decomposition, both backends.
    const int secWhole = splitTimeMs / 1000;
    const int secTenth = (splitTimeMs % 1000) / 100;
    emitCue(cueKey, SpotterPhrase::Category::Timing, std::move(sv), nowMs, -1,
            secWhole <= 999 ? secWhole : -1, secTenth);

    // ---- the two cues that mark a MOMENT rather than describing every split --
    // Both ship ENABLED, unlike sector_completed above: that one fires three or
    // four times a lap whatever happens, these fire on something happening.

    // 1. A NEW BEST FOR THIS SECTOR ALONE.
    //
    // sector_completed compares ACCUMULATED time against your best lap, so a
    // blinding sector inside an otherwise scrappy lap never surfaces there —
    // you are still down on the lap, so it says "slower". That is exactly the
    // sector worth being told about, and it is a different question from the
    // one every other comparison in this function asks.
    //
    // SESSION SCOPE, and not by choice: bestSectorN lives in PluginData, which
    // is the live session cache, so it clears with the session. There is no
    // all-time best-per-sector anywhere in the plugin to compare against —
    // StatsPersonalBestData::sectorN is the sectors OF your best lap, which is
    // a different number. Session is also the honest comparison: another
    // session is another setup, another surface, maybe another bike.
    //
    // bestSectorN is written by updateIdealLap on the LAP event, so during
    // this lap it still holds the figure to beat rather than one this lap
    // already set. `> 0` is therefore the "you have a prior best" floor: it is
    // -1 until a valid lap completes, so lap one is silent instead of
    // announcing every sector as a best.
    //
    // KNOWN IMPRECISION, deliberate: validity is only known at the lap event,
    // so a sector set on a lap that is later invalidated is called here and
    // then not recorded. Announcing it a lap late would be worse — the news is
    // the sector, and it is stale by the time the flag lands.
    if (sectorBestMs > 0 && sectorMs < sectorBestMs && !openerIsOnlyRef) {
        SpotterVars::Vars bv;
        bv.sectorNumber = SpotterPhrase::numberWords(splitIndex + 1);
        bv.sectorDuration = SpotterPhrase::gapWordsMs(sectorMs);
        bv.sectorBestDelta = SpotterPhrase::gapWordsMs(sectorBestMs - sectorMs);
        emitCue("sector_best", SpotterPhrase::Category::Timing, std::move(bv),
                nowMs);
    }

    // 2. ON PACE, called at the LAST SPLIT BEFORE THE LINE — late enough that
    // the claim means something, early enough to be worth hearing while you can
    // still act on it. That split is GAME_SECTOR_COUNT - 2 (index 1 of three
    // sectors in MX Bikes, index 2 of four in GP Bikes), so this stays one
    // expression instead of a per-game table.
    //
    // ESCALATING: the STRONGEST reference you are actually beating wins. Being
    // up on record pace means being up on your own two as well, and "on for the
    // track record" buried under "on for a session best" would be the wrong
    // headline. Each tier is the same accumulated-vs-accumulated comparison the
    // deltas above make.
    //
    // The margin is why this is not just `splitTimeMs < ref`: a few hundredths
    // up with a sector to go is not news, and without a floor this fires on
    // most laps of any decent run. [Spotter] on_pace_margin_ms tunes it.
    //
    // Note it is your best LAP that is the reference, never the ideal: the
    // ideal is by construction no slower than your best lap, so a cue keyed on
    // beating it would essentially never fire.
    //
    // This sits BELOW the same-run guard above, so a last split whose earlier
    // splits this run never saw says nothing — even though the comparison
    // itself needs only the cumulative time. That is deliberate, not leftover
    // placement: the case it silences is a mid-lap join, where the lap did not
    // start at the line and so was never going to count as a best anyway.
    if (splitIndex == GAME_SECTOR_COUNT - 2) {
        struct Tier { int ref; const char* cue; };
        const Tier tiers[] = { { recordRef,  "on_pace_record" },
                               { alltimeRef, "on_pace_personal_best" },
                               { openerIsOnlyRef ? -1 : bestLapRef,
                                 "on_pace_session_best" } };
        for (const Tier& t : tiers) {
            if (t.ref <= 0 || splitTimeMs + m_onPaceMarginMs >= t.ref) continue;
            SpotterVars::Vars pv;
            pv.paceMargin = SpotterPhrase::gapWordsMs(t.ref - splitTimeMs);
            emitCue(t.cue, SpotterPhrase::Category::Timing, std::move(pv),
                    nowMs);
            break;
        }
    }
}

void SpotterManager::onSessionBest(int sessionTimeMs, int lapTimeMs) {
    if (!m_enabled && !m_subtitles) return;
    if (!isCategoryEnabled(SpotterPhrase::Category::Timing)) return;
    SpotterVars::Vars v;
    v.eventTime = SpotterPhrase::lapTimeWordsMs(lapTimeMs);
    // The parts as well as the words: without them a pack's
    // `session_best_mix = ... {event_time}` can never resolve and every such
    // cue drops to TTS. Same for personal_best/record_beaten below.
    const SpotterPhrase::LapTimeParts sbParts =
        SpotterPhrase::lapTimePartsMs(lapTimeMs);
    emitCue("session_best", SpotterPhrase::Category::Timing,
            std::move(v), sessionTimeMs, -1,
            sbParts.valid ? sbParts.composed : -1,
            sbParts.valid ? sbParts.tenths : -1);
    // ARM THE LADDER, exactly as onPersonalBest does. Without this a session
    // best and the fastest lap of the session BOTH spoke, back to back, saying
    // the same number -- "Session best, forty point seven." / "That's the
    // fastest of the session, forty point seven." The ladder in
    // race_lap_handler only picks between the on-screen NOTICES; the spotter's
    // fastest lap arrives separately through the event log, which logs it
    // unconditionally because the race feed wants it. Offline that is every
    // improved lap, since isFastestLap needs isOnline() and so is never true.
    m_higherLapCueTimeMs = lapTimeMs;
}

void SpotterManager::onRiderCrash(int raceNum, int focusedRaceNum,
                                  int sessionTimeMs) {
    if (!m_enabled && !m_subtitles) return;
    const bool you = raceNum >= 0 && raceNum == focusedRaceNum;
    // Filed by subject like every other cue: your own crash is your status,
    // someone else's is news about the field.
    const SpotterPhrase::Category cat =
        you ? SpotterPhrase::Category::General
            : SpotterPhrase::Category::Opponents;
    if (!isCategoryEnabled(cat)) return;
    SpotterVars::Vars v;
    v.eventRider = SpotterPhrase::riderRef(raceNum, focusedRaceNum);
    emitCue(you ? "crashed_you" : "crashed_other", cat,
            std::move(v), sessionTimeMs,
            (raceNum >= 0 && raceNum <= 999) ? raceNum : -1);
}

// The held fastest lap, spoken. There is no condition beyond "one is held":
// the CALLER is the condition, because it runs on the classification and a
// held cue is by definition everything that arrived since the last one. See
// PendingFastest for why that identifies a join replay.
void SpotterManager::flushPendingFastestLap() {
    if (!m_pendingFastest.armed) return;
    const PendingFastest p = m_pendingFastest;
    m_pendingFastest = PendingFastest{};
    // Focus RESOLVED AT FLUSH, exactly as the lap report re-checks its rider:
    // the camera can cut between the arm and the classification, and "you're
    // quickest" about the rider the director just left is the wrong subject.
    const int focusedNow = PluginData::getInstance().getDisplayRaceNum();
    if (p.raceNum >= 0 && p.raceNum == focusedNow) {
        // Your OPENING lap as the session's fastest is the out-lap/gate lap
        // every offline session banks first -- the notice ladder deliberately
        // shows nothing for it (race_lap_handler's hadPreviousBest), and the
        // spotter said "Fastest lap, nice work, three oh five point one" at
        // the first crossing of every session. A fact, not news.
        if (bestLapIsFirstLap()) return;
        // After your flag the finish cue has already read your best lap back
        // ("That's the flag, P one, best lap one thirty three point eight");
        // the held fastest followed with the same number. Your race is over
        // -- the same principle that silences the other post-race cues.
        if (subjectRaceOver()) return;
        // The lap REPORT flushes right after this, in the same call. When the
        // held fastest is the lap being reported, its time is spoken HERE --
        // so tell the report, or a lap_completed variant naming
        // {last_lap_time} reads the same number again one line later. The
        // crossing-time ladder latch cannot cover this: online, the fastest
        // lap has no session-best rung to arm it (isFastestLap is
        // online-only), so the report believed the time was never said.
        if (m_pendingLap.armed && m_pendingLap.raceNum == p.raceNum &&
            m_pendingLap.lapTimeMs == p.nums.lapTimeMs) {
            m_pendingLap.timeAlreadySpoken = true;
        }
    }
    m_emittingPendingFastest = true;
    onRaceEvent(EventLogType::FastestLap, p.raceNum, focusedNow, p.nowMs,
                p.nums);
    m_emittingPendingFastest = false;
}

// Called after the classification order is rebuilt, and the home of BOTH
// deferred cues — they are deferred to the same moment for different reasons.
void SpotterManager::flushDeferredCues() {
    // A classification has landed, so the standings' penalty column has had
    // its chance to absorb any clear/revision — the tally may trust it again.
    m_penaltyColumnStale = false;
    // The fastest lap first, so cues stay in the order they happened, and
    // ABOVE the lap-report guard below rather than after it: a rival's fastest
    // lap never arms a lap report, so anything below that early return would
    // never flush one at all.
    flushPendingFastestLap();
    // The held session wrap-up next, ABOVE the lap-report guard for the same
    // reason the fastest lap is: nothing below this line runs unless a report
    // is armed, and a session can end without one.
    if (m_pendingSessionEnd.armed) {
        const PendingSessionEnd pse = m_pendingSessionEnd;
        m_pendingSessionEnd = PendingSessionEnd{};
        m_emittingPendingSessionEnd = true;
        onRaceEvent(EventLogType::SessionComplete, pse.raceNum,
                    PluginData::getInstance().getDisplayRaceNum(), pse.nowMs,
                    pse.nums);
        m_emittingPendingSessionEnd = false;
    }
    if (!m_pendingLap.armed) return;
    m_pendingLap.armed = false;
    if (!m_enabled && !m_subtitles) return;
    if (!isCategoryEnabled(SpotterPhrase::Category::Timing)) return;

    const PluginData& pd = PluginData::getInstance();
    const int raceNum = m_pendingLap.raceNum;
    // Re-checked rather than trusted from the crossing: the camera may have
    // moved on in between, and a report about a rider you are no longer
    // watching is not yours to hear.
    if (raceNum != pd.getDisplayRaceNum()) return;
    // The cool-down lap crosses the line too; without this gate it reports a
    // position and a gap for a race you have already finished.
    if (subjectRaceOver()) return;
    const int position = pd.getPositionForRaceNum(raceNum);
    if (position <= 0) return;

    const int nowMs = m_pendingLap.nowMs;

#if MXBMRP3_SPOTTER_PROBE
    // TEMP-DEBUG(spotter-vs-standings): the S/F half of the probe. Here the
    // classification HAS been rebuilt for the lap just finished (that is what
    // flushDeferredCues waits for), so this is the freshest the table ever is.
    {
        const StandingsData* mineDbg = pd.getStanding(raceNum);
        char where[32];
        snprintf(where, sizeof(where), "S/F lap %d",
                 mineDbg ? mineDbg->numLaps : -1);
        debugLogStandingsAt(pd, where);
    }
#endif

    // The lap did not count. Spoken FIRST, because it changes what the rest of
    // the report means: a time you are about to hear read back is not a time
    // you can compare with anything.
    //
    // This is the only feedback there is outside a race. A race tells you by
    // issuing a penalty (penalty_you, off the communication callback), but
    // practice and qualifying issue none — the lap is simply struck, silently,
    // and you find out by looking at the timing screen.
    // FRESH MEASUREMENTS ONLY for everything this flush speaks: the cached
    // neighbour gaps (m_lastAhead/m_lastBehind) are up to a lap old here, and
    // a report that reads one presents it as the crossing's fact -- both real
    // tapes show "two point zero to rider fifty six" at the line, contradicted
    // seconds later by the fresh behind cue resolving at four point zero. The
    // crossing's own measurement (measureAheadGap, already in the vars) is
    // untouched; only the ambient refill of what it could NOT measure is held
    // back, so the optional groups drop and the dedicated gap cues -- which
    // resolve moments later with the real number -- stand alone.
    m_lapReportFreshGapsOnly = true;
    if (!m_pendingLap.lapValid) {
        SpotterVars::Vars v = m_pendingLap.vars;
        emitCue("lap_invalidated", SpotterPhrase::Category::Timing, std::move(v), nowMs);
    }

    // ONE cue for the crossing, not two: your position and your lap time are
    // one moment, and since every variable works in every cue the difference
    // between them belongs to the template, not to the key: a pack that wants
    // the time says {last_lap_time}, one that wants the position says
    // {position}, and one that wants both says both.
    //
    // `lap_completed` because it names the MOMENT. A key naming a particular
    // report — a content choice — is exactly the thing being made
    // configurable, and the pack format's own rule is that a cue is a moment
    // and everything else is a variable.
    SpotterVars::Vars lapVarsOut = m_pendingLap.vars;
    lapVarsOut.eventTime = SpotterPhrase::lapTimeWordsMs(m_pendingLap.lapTimeMs);
    // THE THIRD TELLING. A lap that was a personal or session best has already
    // had its time spoken by that cue; a lap_completed variant naming
    // {last_lap_time} would then say the same number again, immediately after
    // — with the fastest-lap duplicate above, three in one crossing.
    //
    // Suppressed rather than given its own cue key: every variant that names
    // the time keeps it in an [optional group] precisely so it can drop, so
    // this costs no pack change and a pack that never mentions the time is
    // unaffected. An ordinary lap still reports it -- that is the case the
    // variable is for.
    m_lapReportHideTime = m_pendingLap.timeAlreadySpoken;
    emitCue("lap_completed", SpotterPhrase::Category::Timing, std::move(lapVarsOut), nowMs, -1,
            -1, -1, -1, -1, position);
    m_lapReportHideTime = false;

    // Places made up or lost ON THIS LAP: the position reported at the previous
    // crossing against the one just reported, with the grid standing in for the
    // opening lap. Kept here rather than read from PluginData because both of
    // its references answer a different question:
    //
    //  - getRaceStartPosition is the GRID, so measuring from it made this a
    //    running total announced every lap. On a real Farm 14 race that said
    //    "Up thirteen" on lap 1, then "Up twelve" on lap 2 — a lap the player
    //    had LOST a place on, P7 to P8 — then "Up twelve" again on a lap where
    //    nothing moved. A total is a number, not a moment;
    //    {positions_since_start} is where that number lives.
    //  - getSfReferencePosition is sampled at the crossing itself, from the
    //    classification standing at that instant. A place lost mid-lap is
    //    already in it, so the same Farm 14 lap 2 fell silent instead of
    //    saying "Down one" — the change had happened, just not in the window
    //    that reference measures.
    //
    // Comparing consecutive REPORTS has neither problem, and needs no clock.
    // Tied to the rider it was taken for: the camera can move between laps
    // while spectating, and the previous subject's position is not a reference
    // for this one's.
    int refPos = (m_lastReportedPosNum == raceNum) ? m_lastReportedPos : 0;
    if (refPos <= 0) refPos = pd.getRaceStartPosition(raceNum);
    m_lastReportedPos = position;
    m_lastReportedPosNum = raceNum;
    if (refPos > 0 && refPos != position) {
        SpotterVars::Vars v = m_pendingLap.vars;
        const int delta = refPos - position;
        // Held in a LOCAL, and not read back off `v` in the emitCue call.
        // emitCue takes its Vars BY VALUE, so `std::move(v)` gutts v when that
        // parameter is constructed — and the order function arguments are
        // evaluated in is unspecified, so whether the text below sees
        // the number or an empty moved-from string is up to the compiler.
        // MSVC evaluates right to left and saw the empty one: a shipped log
        // reads `SPOTTER SAY [position_gained] Up .` Every Linux gate passed,
        // because gcc happened to evaluate left to right.
        const std::string changed =
            SpotterPhrase::numberWords(delta < 0 ? -delta : delta);
        v.positionsChanged = changed;
        const bool gained = delta > 0;
        emitCue(gained ? "position_gained" : "position_lost", SpotterPhrase::Category::Timing, std::move(v), nowMs);
    }
    m_lapReportFreshGapsOnly = false;
}

void SpotterManager::onSessionState(int sessionState) {
    m_sessionState = sessionState;
}

// The gate physically dropping, which is NOT the same moment as the session
// becoming active: a standing start holds on the grid after the session flips
// to running, and the gate falls some seconds later. Practice and pit-start
// sessions never have one, which is exactly why it deserves its own key
// rather than a session_started that means two different things.
void SpotterManager::onGateDrop() {
    if (!m_enabled && !m_subtitles) return;
    if (!isCategoryEnabled(SpotterPhrase::Category::General)) return;
    emitCue("gate_drop", SpotterPhrase::Category::General, {},
            PluginData::getInstance().getSessionElapsedTime());
}

void SpotterManager::speakHotkeyCue() {
    // Entirely user-defined: nothing happens until a pack
    // writes `hotkey_triggered = ...`. That is the point — it is a line you
    // compose out of the variables and then hear on demand, which makes it the
    // fastest way to check a template without waiting for the race to produce
    // the event it belongs to.
    if (!m_enabled && !m_subtitles) return;
    emitCue("hotkey_triggered", SpotterPhrase::Category::General, {},
            PluginData::getInstance().getSessionElapsedTime());
}

void SpotterManager::onSpectateTarget(int raceNum) {
    if (!m_enabled && !m_subtitles) return;
    if (raceNum <= 0 || raceNum > 999) return;
    SpotterVars::Vars v;
    v.eventRider = SpotterPhrase::riderRef(raceNum, -1);
    // Opponents, not General: it is about another rider, and that is where the
    // registry, the shipped pack's heading and the reference have always put
    // it. Only the emitter said General, so muting Opponents did not stop it.
    emitCue("spectate_target", SpotterPhrase::Category::Opponents, std::move(v),
            PluginData::getInstance().getSessionElapsedTime(), raceNum);
}


// The BEHIND gap only. The ahead gap stopped being a cue when it turned out
// to fire at exactly the same instant as the lap report — it is variables on
// lap_completed now. This one is a real event: it resolves when the rider behind
// reaches a timing point you already crossed, so the number is a stopwatch
// rather than an estimate, and no other cue marks that moment.
void SpotterManager::measureAheadGap(int myPosition,
                                     const std::vector<int>& order,
                                     long long pointKey, int nowMs,
                                     SpotterVars::Vars& out) {
    if (myPosition < 2 || static_cast<size_t>(myPosition - 2) >= order.size()) {
        return;   // you are leading, or the order does not have you placed
    }
    SpotterPace::Gap gap;
    if (!m_pace.aheadGap(order[myPosition - 2], pointKey, nowMs, gap)) return;
    m_lastAhead = gap;
    out.gapToAhead = SpotterPhrase::gapWordsMs(gap.gapMs);
    // Same rule as emitGapCue: the sentence names the rider the stopwatch
    // timed, not whoever the classification lists in that slot by the time
    // the deferred report speaks.
    out.riderAhead = SpotterPhrase::riderRef(gap.raceNum, -1);
    out.lastLapAhead = lastLapWordsFor(gap.raceNum);
    if (gap.hasTrend) {
        out.trendAhead = SpotterPhrase::trendAheadWord(gap.deltaMs);
        out.gainedOnAhead = SpotterPhrase::gapWordsMs(gap.deltaMs);
    }
}

void SpotterManager::emitGapCue(const SpotterPace::Gap& gap, int sessionTimeMs) {
    m_lastBehind = gap;
    // The rider behind keeps reaching your timing points on their own last lap
    // after you have finished, so this went on calling a gap in a race that was
    // over for you. Recorded, not suppressed: the trend variables stay current
    // for anything that asks, it is only the CUE that has nothing to say.
    if (subjectRaceOver()) return;
    // Keyed by trend so a recorded pack can voice "closing" and "dropping
    // back" differently; both fall back to gap_behind, so a text pack writes
    // one line with {trend_behind} in it.
    const char* key = "gap_behind";
    if (gap.hasTrend) {
        key = gap.deltaMs < 0 ? "gap_behind_closing" : "gap_behind_dropping";
    }
    // sec/tenth are the wav mixer's numeric arguments; the spoken string is the
    // same two numbers, so it comes from the one helper that phrases them.
    const int sec = gap.gapMs / 1000;
    const int tenth = (gap.gapMs % 1000) / 100;
    const std::string words = SpotterPhrase::gapWordsMs(gap.gapMs);
    SpotterVars::Vars vars;
    vars.gapToBehind = words;
    // The NAME travels with the number: Gap::raceNum is who the stopwatch
    // actually timed, so that is who the sentence names. Taken from the live
    // order at emit time instead, a rider passed between the arm and the
    // resolve would speak as "Behind you, rider twelve, two point one" with
    // 2.1 measured against rider forty-seven.
    vars.riderBehind = SpotterPhrase::riderRef(gap.raceNum, -1);
    vars.lastLapBehind = lastLapWordsFor(gap.raceNum);
    if (gap.hasTrend) {
        vars.trendBehind = SpotterPhrase::trendBehindWord(gap.deltaMs);
        vars.gainedOnBehind = SpotterPhrase::gapWordsMs(gap.deltaMs);
    }
    emitCue(key, SpotterPhrase::Category::Timing, std::move(vars), sessionTimeMs,
            -1, sec, tenth);
}

void SpotterManager::onPersonalBest(int sessionTimeMs, int lapTimeMs) {
    if (!m_enabled && !m_subtitles) return;
    if (!isCategoryEnabled(SpotterPhrase::Category::Timing)) return;
    SpotterVars::Vars v;
    v.eventTime = SpotterPhrase::lapTimeWordsMs(lapTimeMs);

    // A lap that beats the TRACK RECORD is its own moment, and the tier above
    // an all-time PB (it is nearly always both). Highest-applicable-only, the
    // same way the notice ladder in the lap handler works — hearing "personal
    // best" and "track record" back to back would be the redundancy that
    // ladder exists to prevent.
    const int record = trackRecordLapTime();
    const bool beatsRecord = record > 0 && lapTimeMs > 0 && lapTimeMs < record;
    const SpotterPhrase::LapTimeParts pbParts =
        SpotterPhrase::lapTimePartsMs(lapTimeMs);
    emitCue(beatsRecord ? "record_beaten" : "personal_best",
            SpotterPhrase::Category::Timing, std::move(v), sessionTimeMs, -1,
            pbParts.valid ? pbParts.composed : -1,
            pbParts.valid ? pbParts.tenths : -1);
    // Arm the ladder: whatever else THIS lap turns out to be, it has now had
    // its moment. Stored as the lap's time so the suppression can only apply
    // to that lap. See onRaceEvent's FastestLap branch.
    m_higherLapCueTimeMs = lapTimeMs;
}

// True when the subject's best lap of this session is their FIRST lap — which
// is never a lap they could repeat. In a race it carries the gate hold and a
// standing start; in practice it is the roll out of the pits. Farm 14's opener
// was 3:05.1 against a 1:26 field.
//
// It is a real lap time and a real personal best — the game says so, and every
// HUD shows the same figure — so this does NOT touch the data. What it stops is
// treating it as a REFERENCE, which produces a true number that means nothing:
// three rivals' laps were reported as "ninety six point six quicker than your
// best", "ninety seven point five quicker", "ninety eight point six quicker".
// The moment a repeatable lap replaces it the comparison comes back, which is
// exactly when a rider expects it to.
//
// Deliberately NOT applied outside the spotter. The session best comes from the
// game's own classification, and the standings, the timing HUD and the web
// overlay all show what the game reports; a plugin that quietly disagreed with
// the game about your best lap would be the worse bug. This is a judgement
// about what is worth SAYING, which is the spotter's job alone.
bool SpotterManager::bestLapIsFirstLap() const {
    const PluginData& pd = PluginData::getInstance();
    // The classification's own index, which the game fills and documents as
    // 1-based (unified_types.h) and which already excludes invalid laps.
    // Deliberately NOT the lap log's entry: LapLogEntry::lapNum says "1-based"
    // in its comment but race_lap_handler builds it from
    // completedLapNumZeroIndexed, so an opening lap reads 0 there.
    const StandingsData* mine = pd.getStanding(pd.getDisplayRaceNum());
    return mine && mine->bestLapNum == 1;
}

// True once the rider the cues are ABOUT has stopped racing — took the flag,
// retired, was disqualified or never started. Everything a spotter says about
// your race is a lie after that point: a position and gaps called on the
// cool-down lap after the checkered flag, or after you have retired.
//
// The mirror image of the pre-start grid grace in onTrackPositions, and quiet
// for the same reason — not racing, so the numbers describe nothing.
//
// Deliberately NOT applied to session-level cues (the leader's flag, the final
// lap, the session ending) or to other riders' news: those stay true whatever
// your own race did. And absent standings mean absent evidence, not a finish.
bool SpotterManager::subjectRaceOver() const {
    const PluginData& pd = PluginData::getInstance();
    const int me = pd.getDisplayRaceNum();
    if (me <= 0) return false;
    const StandingsData* s = pd.getStanding(me);
    if (!s) return false;
    if (s->finishTime >= 0 || s->sessionFinished) return true;
    // Note the deliberate absence of a pit clause, which is what makes this a
    // third question rather than a copy of PluginData's two rider-state
    // predicates: isRiderExcludedFromDetection asks who can be flagged as a
    // hazard and isRiderSpectatable who the camera can land on, and both count
    // the pits as out. You can sit in the pits and still be racing.
    return s->state == static_cast<int>(Unified::EntryState::DNS) ||
           s->state == static_cast<int>(Unified::EntryState::Retired) ||
           s->state == static_cast<int>(Unified::EntryState::DSQ);
}
