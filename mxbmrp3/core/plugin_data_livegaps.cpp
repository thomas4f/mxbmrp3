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

    // The game only reports the ~10 vehicles closest to the camera in each
    // RaceTrackPosition batch, but m_trackPositions RETAINS the last-seen entry
    // for every rider (it's only erased on removeRaceEntry). So a rider outside
    // the current batch still has a stale trackPos/sessionTime here. m_currentLap
    // etc. keep advancing from RaceClassification, so recomputing a gap from that
    // frozen position drifts into garbage. Only riders in the current batch have
    // fresh data — gate the whole computation on m_activeTrackPosRiders, exactly
    // like the blue-flag/hazard code does. If the leader itself isn't in the
    // batch we can't stamp a fresh timing point this frame, so leave every gap
    // frozen at its last live value rather than baseline off a stale leader.
    if (!m_activeTrackPosRiders.count(leaderRaceNum)) {
        return;
    }

    const RiderTrackState& leaderPos = leaderPosIt->second;
    int leaderLaps = leaderStandingIt->second.numLaps;

    // Store leader's timing point at current position for current lap
    // trackPos is [0.0, 1.0], map to indices [0, NUM_TIMING_POINTS-1]
    // Clamp handles edge case where trackPos = 1.0 exactly (at finish line before lap increments)
    int positionIndex = static_cast<int>(leaderPos.trackPos * static_cast<float>(NUM_TIMING_POINTS));
    positionIndex = std::max(0, std::min(positionIndex, static_cast<int>(NUM_TIMING_POINTS - 1)));

    // Ensure lap entry exists in map
    if (m_leaderTimingPoints.find(leaderLaps) == m_leaderTimingPoints.end()) {
        m_leaderTimingPoints[leaderLaps] = std::array<LeaderTimingPoint, NUM_TIMING_POINTS>();
    }

    // Store when the current leader passed this position
    // Always update - we want the timestamp of when THE LEADER was here, regardless of who it was
    m_leaderTimingPoints[leaderLaps][positionIndex] = LeaderTimingPoint(
        leaderPos.sessionTime,
        leaderLaps
    );

    // Calculate gaps for all other riders
    bool anyUpdated = false;
    int minLapNeeded = leaderLaps;  // Track oldest lap we need to keep

    for (int raceNum : m_classificationOrder) {
        if (raceNum == leaderRaceNum) {
            // Explicitly set leader's gap to 0 (prevents stale data after lead changes)
            leaderStandingIt->second.realTimeGap = 0;
            continue;
        }

        auto posIt = m_trackPositions.find(raceNum);
        auto standingIt = m_standings.find(raceNum);

        if (posIt == m_trackPositions.end() || standingIt == m_standings.end()) {
            continue;  // Position data not available
        }

        // Not in the current batch → its m_trackPositions entry is stale (see the
        // note above the leader lookup). Freeze the last computed gap rather than
        // recomputing from a frozen position while the leader's clock advances.
        if (!m_activeTrackPosRiders.count(raceNum)) {
            continue;
        }

        const RiderTrackState& riderPos = posIt->second;
        StandingsData& standing = standingIt->second;
        int riderLap = standing.numLaps;

        // Skip lapped riders - live gap is meaningless across different laps.
        // They'll use the API's official gap (gapLaps / gap fields) instead.
        if (standing.gapLaps > 0) {
            standing.realTimeGap = 0;
            continue;
        }

        // If rider finished, freeze their gap by skipping calculation
        if (m_sessionData.isRiderFinished(riderLap, standing.numLapsAtLeaderFinish)) {
            continue;  // Gap is frozen at last calculated value
        }

        // Track the minimum lap we need to keep timing data for
        if (riderLap < minLapNeeded) {
            minLapNeeded = riderLap;
        }

        // Find rider's position index
        // trackPos is [0.0, 1.0], map to indices [0, NUM_TIMING_POINTS-1]
        int riderPosIndex = static_cast<int>(riderPos.trackPos * static_cast<float>(NUM_TIMING_POINTS));
        riderPosIndex = std::max(0, std::min(riderPosIndex, static_cast<int>(NUM_TIMING_POINTS - 1)));

        // Look up leader's timing point for the SAME lap the rider is on
        auto lapIt = m_leaderTimingPoints.find(riderLap);
        if (lapIt == m_leaderTimingPoints.end()) {
            continue;  // No timing data for this lap yet
        }

        const LeaderTimingPoint& leaderTiming = lapIt->second[riderPosIndex];

        // Verify timing point is valid
        // Note: sessionTime can be negative during overtime in time+lap races, but lapNum won't be -1
        if (leaderTiming.lapNum >= 0) {
            // Calculate gap based on race format
            // For time+lap races (countdown timer), smaller sessionTime = later in time
            // For lap races (counting-up timer), larger sessionTime = later in time
            int newGap;
            if (m_sessionData.sessionLength > 0) {
                // Time-based race: timer counts DOWN (300 → 0 → -100)
                // Leader has HIGHER sessionTime, rider has LOWER sessionTime
                newGap = leaderTiming.sessionTime - riderPos.sessionTime;
            } else {
                // Lap-based race: timer counts UP (0 → 100 → 200)
                // Leader has LOWER sessionTime, rider has HIGHER sessionTime
                newGap = riderPos.sessionTime - leaderTiming.sessionTime;
            }

            // Sanity check: gap should be positive (negative would indicate calculation error)
            if (newGap > 0) {
                // Only mark dirty if gap changed by threshold amount
                // This reduces HUD rebuild frequency while maintaining useful precision
                int oldGap = standing.realTimeGap;
                int gapChange = (newGap > oldGap) ? (newGap - oldGap) : (oldGap - newGap);

                standing.realTimeGap = newGap;  // Always update the stored value

                if (gapChange >= GAP_UPDATE_THRESHOLD_MS) {
                    anyUpdated = true;
                }
            }
        }
    }

    // Prune old laps that no rider needs anymore (keep at least 1 lap buffer)
    int oldestLapToKeep = minLapNeeded - 1;
    auto it = m_leaderTimingPoints.begin();
    while (it != m_leaderTimingPoints.end()) {
        if (it->first < oldestLapToKeep) {
            it = m_leaderTimingPoints.erase(it);
        } else {
            ++it;
        }
    }

    // Safety check: prevent excessive memory usage
    if (m_leaderTimingPoints.size() > MAX_LAPS_TO_KEEP) {
        // Erase oldest laps beyond MAX_LAPS_TO_KEEP
        while (m_leaderTimingPoints.size() > MAX_LAPS_TO_KEEP) {
            m_leaderTimingPoints.erase(m_leaderTimingPoints.begin());
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
    m_leaderTimingPoints.clear();

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
