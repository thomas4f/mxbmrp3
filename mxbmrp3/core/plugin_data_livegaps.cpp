// ============================================================================
// core/plugin_data_livegaps.cpp
// The real-time gap engine: leader timing points (1%-of-lap resolution) and
// per-rider leader-relative live gaps, recomputed on every RaceTrackPosition
// batch and time-coalesced into Standings notifications (see the
// gapNotifyIntervalMs notes in plugin_data.h).
// (Split from plugin_data_standings.cpp.)
// ============================================================================

#include "plugin_data.h"
#include "plugin_utils.h"
#include "ui_config.h"
#include "xinput_reader.h"
#include "rumble_profile_manager.h"
#include "hud_manager.h"  // Direct include for notification
#if GAME_HAS_DISCORD
#include "discord_manager.h"  // Direct include for Discord presence updates
#endif
#if GAME_HAS_STEAM_FRIENDS
#include "steam_friends_manager.h"  // Steam friends rich-presence integration
#endif
#if GAME_HAS_HTTP_SERVER
#include "http_server.h"  // Direct include for web overlay updates
#endif
#include "../diagnostics/logger.h"
#include "../diagnostics/timer.h"
#include <algorithm>
#include <cmath>
#include <cstring>


void PluginData::updateRealTimeGaps() {
    // Only calculate gaps if we have classification order
    if (m_classificationOrder.empty()) {
        return;
    }

    // Find the leader (first in classification order)
    int leaderRaceNum = m_classificationOrder[0];
    auto leaderPosIt = m_trackPositions.find(leaderRaceNum);
    auto leaderStandingIt = m_standings.find(leaderRaceNum);

    if (leaderPosIt == m_trackPositions.end() || leaderStandingIt == m_standings.end()) {
        return;  // Leader position not available
    }

    // Flatten the field in classification order (leader first) for the pure
    // engine. The game only reports the ~10 vehicles closest to the camera in
    // each RaceTrackPosition batch, but m_trackPositions RETAINS the last-seen
    // entry for every rider (it's only erased on removeRaceEntry) — so a rider
    // outside the current batch still has a stale trackPos/sessionTime here
    // while m_currentLap etc. keep advancing from RaceClassification. The
    // `active` flag (from m_activeTrackPosRiders, exactly like the
    // blue-flag/hazard code) is what lets the engine freeze those riders
    // instead of drifting their gap off a frozen position; if the LEADER isn't
    // in the batch the engine freezes everyone, rather than baselining off a
    // stale leader.
    m_liveGapRiders.clear();
    m_liveGapStandings.clear();
    m_liveGapPrev.clear();
    for (int raceNum : m_classificationOrder) {
        auto posIt = m_trackPositions.find(raceNum);
        auto standingIt = m_standings.find(raceNum);
        if (posIt == m_trackPositions.end() || standingIt == m_standings.end()) {
            continue;  // Position data not available
        }
        const RiderTrackState& pos = posIt->second;
        StandingsData& standing = standingIt->second;

        LiveGap::Rider rider;
        rider.raceNum = raceNum;
        rider.trackPos = pos.trackPos;
        rider.sessionTime = pos.sessionTime;
        rider.numLaps = standing.numLaps;
        rider.active = m_activeTrackPosRiders.count(raceNum) > 0;
        // Lapped riders get no live gap — it's meaningless across different
        // laps; they use the API's official gap (gapLaps / gap fields) instead.
        rider.lapped = standing.gapLaps > 0;
        rider.finished = m_sessionData.isRiderFinished(standing.numLaps,
                                                       standing.numLapsAtLeaderFinish);
        m_liveGapRiders.push_back(rider);
        m_liveGapStandings.push_back(&standing);
        m_liveGapPrev.push_back(standing.realTimeGap);
    }
    if (m_liveGapRiders.empty() || m_liveGapRiders[0].raceNum != leaderRaceNum) {
        return;  // Leader was skipped above — nothing to baseline against
    }

    bool anyUpdated = m_liveGapEngine.update(
        m_liveGapRiders, /*timeBasedSession=*/m_sessionData.sessionLength > 0,
        m_liveGapPrev, GAP_UPDATE_THRESHOLD_MS, m_liveGapResults);

    for (size_t i = 0; i < m_liveGapResults.size(); i++) {
        if (m_liveGapResults[i].action == LiveGap::GapAction::SET) {
            m_liveGapStandings[i]->realTimeGap = m_liveGapResults[i].gap;
        }
    }

    // Only notify if something actually changed - and coalesce to at most one
    // Standings notification per GAP_NOTIFY_INTERVAL_MS (see the member
    // comment: the per-rider threshold is defeated by leader-timing
    // quantization on full grids, which otherwise dirties every table HUD on
    // every callback during close racing). A skipped notify is carried in
    // m_gapNotifyPending and flushed by a later call, so the final change is
    // never lost while callbacks keep arriving; once they stop, the session
    // transition events notify Standings consumers anyway.
    if (anyUpdated) {
        m_gapNotifyPending = true;
    }
    if (m_gapNotifyPending) {
        auto now = std::chrono::steady_clock::now();
        if (now - m_lastGapNotify >= std::chrono::milliseconds(m_gapNotifyIntervalMs)) {
            m_lastGapNotify = now;
            m_gapNotifyPending = false;
            notifyHudManager(DataChangeType::Standings);
        }
    }
}

void PluginData::clearLiveGapTimingPoints() {
    // Clear all timing points when a new session starts
    m_liveGapEngine.clear();

    // Reset session time
    m_currentSessionTime = 0;

    // Clear track positions
    m_trackPositions.clear();
    m_activeTrackPosRiders.clear();
    m_blueFlagsDirty = true;

    // Clear cached official gaps for new session
    m_lastValidOfficialGap.clear();

    // Clear realTimeGap values from standings (prevent old session data from persisting)
    for (auto& pair : m_standings) {
        pair.second.realTimeGap = 0;
    }

    DEBUG_INFO("Live gap timing points cleared for new session");
}
