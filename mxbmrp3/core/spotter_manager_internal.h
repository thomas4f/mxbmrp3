// ============================================================================
// core/spotter_manager_internal.h
// Shared internal helpers for the SpotterManager translation units
// (spotter_manager*.cpp). Extracted verbatim from spotter_manager.cpp when it
// was split into focused TUs; the values and logic are unchanged. The
// functions are header-inline (were file-local in the single TU) so every
// SpotterManager TU sees one definition without ODR conflicts. The probe
// switch lives here for the same reason: every TU's probe sites must compile
// out together when it is flipped.
// ============================================================================
#pragma once

#include "plugin_data.h"
#include "stats_manager.h"
#include "hud_manager.h"
#if GAME_HAS_RECORDS_PROVIDER
#include "../hud/records_hud.h"   // the track record, MX Bikes only
#endif
#include "spotter_phrase.h"

#include <string>
// ============================================================================
// THE SPOTTER PROBE — ON IN EVERY BUILD, DELIBERATELY.
//
// It writes the paired logs the spotter is read from: "what did the spotter
// say" beside "what did the standings table hold at that instant".
//
// It used to be gated - off unless a build set MXBMRP3_SPOTTER_PROBE - on the
// grounds that a debug probe must not ship. That was right while the spotter
// was being written and wrong for its first release: the thing we most need
// after it reaches players is their opinion of the DEFAULT wording and pacing,
// and every one of those reports arrives as a log. A log without the transcript
// cannot answer "what did it actually say, and when", which is the whole
// question. So the gate is gone rather than documented, and this is a decision
// with an expiry: once the defaults settle, the probe goes entirely.
//
// A SWITCH RATHER THAN SIX SITES, because the site list was the actual hazard:
// it has been miscounted three times, always low ("both", "four", "five"
// against a real six), and the miss that never announced itself was the
// forward declaration — delete the definition without it and a static is
// declared and never defined, an error on the cross build. Set this to 0 and
// every site compiles out together; there is nothing left to enumerate and
// nothing to forget. That is also how it gets removed later: flip it, confirm
// the build, then delete what the compiler stops reaching.
//
// The tape tooling depends on it too: tests/integration/spotter_transcript_driver
// .cpp replays a recorded weekend and greps SPOTTER SAY to produce a readable
// transcript, which is the only way the wording and ordering get reviewed.
//
// WHAT IT COSTS: event-rate, not frame-rate (about four splits, a lap report
// and a handful of cues per lap), but each line is a mutex-held write with an
// explicit flush on the GAME THREAD, so each is a frame hitch where it lands —
// and run_perf.sh drives no cues, so it cannot see them. It also puts the
// spoken transcript and every neighbour's gap in the user's own log, which is
// the point, and is worth knowing before pasting one into a public thread.
// ============================================================================
#ifndef MXBMRP3_SPOTTER_PROBE
#  define MXBMRP3_SPOTTER_PROBE 1
#endif

// Your best ever on this track/bike, from the persisted stats rather than a
// session-start snapshot: the spotter wants the CURRENT record of it, and a
// lap that has just beaten it should read as the reference from then on.
inline int allTimeBestLapTime() {
    const StatsPersonalBestData* pb = StatsManager::getInstance().getPersonalBest();
    return (pb && pb->isValid()) ? pb->lapTime : -1;
}

// The provider's record for this track. MX Bikes only — everywhere else the
// HUD does not exist, and the variables simply stay empty.
inline int trackRecordLapTime() {
#if GAME_HAS_RECORDS_PROVIDER
    return HudManager::getInstance().getRecordsHud().getFastestRecordLapTime();
#else
    return -1;
#endif
}

// A rider's most recent lap, in the lap-time words, or "" when they have not
// completed one.
//
// PluginData already keeps a lap log PER RIDER (handleRaceLap fires for
// everyone, not just you), newest first, so this is a read rather than new
// detection -- the same store StandingsHud's Last column reads. Which is also
// why it costs nothing to say: the number is already on screen for anyone who
// turned that column on.
inline std::string lastLapWordsFor(int raceNum) {
    const std::deque<LapLogEntry>* log = PluginData::getInstance().getLapLog(raceNum);
    if (!log || log->empty()) return std::string();
    return SpotterPhrase::lapTimeWordsMs(log->front().lapTime);
}
