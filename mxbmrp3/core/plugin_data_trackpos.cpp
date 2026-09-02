// ============================================================================
// core/plugin_data_trackpos.cpp
// Real-time track position + proximity detection: per-rider track positions,
// the live-gap "active in last batch" set, wrong-way/crash state, blue-flag
// caches, hazard detection (stationary/wrong-way riders ahead), pit-exit grace.
// (Standings/classification are in plugin_data_standings.cpp; the real-time
//  gap engine is plugin_data_livegaps.cpp.)
// ============================================================================

#include "plugin_data.h"
#include "spotter_manager.h"
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


void PluginData::updateTrackPosition(int raceNum, float trackPos, int numLaps, bool crashed, int sessionTime) {
    auto it = m_trackPositions.find(raceNum);

    if (it != m_trackPositions.end()) {
        RiderTrackState& data = it->second;

        // Detect teleport (reset to track / pit exit) by checking single-frame position jump.
        // Wrapped so a normal start/finish crossing doesn't read as a jump.
        const float rawDelta = alongTrackDelta(trackPos, data.trackPos);
        if (std::abs(rawDelta) > RiderTrackState::TELEPORT_THRESHOLD) {
            // Large non-wraparound jump = teleport. Reset state to prevent false wrong-way.
            data.previousTrackPos = trackPos;
            data.wrongWay = false;
            data.wrongWaySince = {};
            data.trackPos = trackPos;
            data.numLaps = numLaps;
            data.sessionTime = sessionTime;
            data.crashed = crashed;
            // Don't count crashes across a teleport — a reset back onto the
            // track is a crash recovery, not a new crash. Just refresh the
            // prev-state so the next edge is measured from here.
            data.prevCrashedState = crashed;

            // Reset hazard state on teleport (clean state reset, no cooldown)
            data.lastSignificantTrackPos = trackPos;
            data.stationarySince = {};
            data.hazardClearedAt = {};
            data.hazardType = HazardType::None;
            data.hazardConfirmed = false;
            // Teleport moves the rider away from the pit exit area, so re-arm
            // the stationary hazard — normal rules apply at the new position.
            data.movedSincePitExit = true;

            m_currentSessionTime = sessionTime;
            m_blueFlagsDirty = true;
            m_hazardsDirty = true;
            m_hazardTypesDirty = true;
            return;
        }

        // Detect wrong-way using per-sample direction + timestamp confirmation.
        // rawDelta is already wraparound-aware (computed above for teleport check).
        auto now = std::chrono::steady_clock::now();
        bool goingBackward = (rawDelta < 0.0f);

        if (goingBackward) {
            // Start or maintain wrong-way timer
            if (data.wrongWaySince.time_since_epoch().count() == 0) {
                data.wrongWaySince = now;
            }
        } else {
            // Moving forward (or stationary) — reset wrong-way timer
            data.wrongWaySince = {};
        }

        bool wrongWay = (data.wrongWaySince.time_since_epoch().count() != 0) &&
            (std::chrono::duration_cast<std::chrono::milliseconds>(now - data.wrongWaySince).count() >= m_proximity.hazardWrongWayDurationMs);
        data.previousTrackPos = trackPos;

        // === Hazard detection (stationary) ===
        // (now already declared above for wrong-way detection)

        // Stationary detection: check if rider has moved significantly
        float posDelta = std::abs(trackPos - data.lastSignificantTrackPos);
        posDelta = std::min(posDelta, 1.0f - posDelta);  // Wraparound-aware

        // Convert tolerance from meters to track percentage
        float trackLength = m_sessionData.trackLength;
        float tolerancePct = (trackLength > 0.0f)
            ? (m_proximity.hazardStationaryTolerance / trackLength)
            : 0.003f;  // ~5m on typical 1600m track

        if (posDelta > tolerancePct) {
            // Significant movement — update reference position, reset stationary timer
            data.lastSignificantTrackPos = trackPos;
            data.stationarySince = {};
            // Rider has now moved since leaving the pits; re-enable stationary hazard detection
            data.movedSincePitExit = true;
        } else if (data.stationarySince.time_since_epoch().count() == 0) {
            // Just became stationary — start timer
            data.stationarySince = now;
        }

        bool isStationary = (data.stationarySince.time_since_epoch().count() != 0) &&
            (std::chrono::duration_cast<std::chrono::milliseconds>(now - data.stationarySince).count() >= m_proximity.hazardStationaryDurationMs);
        bool isWrongWay = wrongWay;

        // Check if rider should be excluded from hazard detection
        bool excluded = false;
        auto standingIt = m_standings.find(raceNum);
        if (standingIt != m_standings.end()) {
            excluded = isRiderExcludedFromDetection(standingIt->second);
        }

        // Hazard type resolution (state transition rules)
        if (excluded) {
            // Excluded riders are never hazards
            data.hazardType = HazardType::None;
            data.hazardConfirmed = false;
            data.hazardClearedAt = {};
        } else if (crashed || isWrongWay || isStationary) {
            // Determine new type (WrongWay takes priority, crashed treated as Stationary)
            HazardType newType = isWrongWay ? HazardType::WrongWay : HazardType::Stationary;

            if (data.hazardConfirmed) {
                // Already confirmed as hazard — immediate type transition, cancel any cooldown
                data.hazardType = newType;
                data.hazardClearedAt = {};
            } else {
                // Not yet confirmed — apply duration thresholds.
                // Wrong-way: confirmed via wrongWaySince timer (duration already checked above).
                // Stationary: confirmed via the stationarySince timer above.
                if (crashed) {
                    // Crashed = immediate hazard, no timer needed
                    data.hazardType = HazardType::Stationary;
                    data.hazardConfirmed = true;
                    data.hazardClearedAt = {};
                } else if (isWrongWay) {
                    data.hazardType = HazardType::WrongWay;
                    data.hazardConfirmed = true;
                    data.hazardClearedAt = {};
                } else if (isStationary) {
                    // Stationary confirmed via stationarySince timer
                    data.hazardType = HazardType::Stationary;
                    data.hazardConfirmed = true;
                    data.hazardClearedAt = {};
                }
            }
        } else {
            // No hazard conditions active
            if (data.hazardConfirmed) {
                // If rider is motionless (stationarySince timer running but not yet expired),
                // hold the existing hazard type — they'll transition to Stationary once
                // the timer expires, avoiding a gap in icon display.
                // Note: don't reset hazardClearedAt here — stationarySince oscillates for
                // moving riders (per-frame delta < tolerance every few frames), so resetting
                // would prevent the cooldown from ever completing.
                bool isMotionless = data.stationarySince.time_since_epoch().count() != 0;
                bool cooldownActive = data.hazardClearedAt.time_since_epoch().count() != 0;
                if (!isMotionless && !cooldownActive) {
                    // Moving and no cooldown yet — start cooldown
                    data.hazardClearedAt = now;
                } else if (cooldownActive && std::chrono::duration_cast<std::chrono::milliseconds>(now - data.hazardClearedAt).count() >= m_proximity.hazardCooldownMs) {
                    // Cooldown expired — clear hazard
                    data.hazardType = HazardType::None;
                    data.hazardConfirmed = false;
                    data.hazardClearedAt = {};
                }
                // Otherwise: still in cooldown, keep hazardType and hazardConfirmed
            }
        }

        // Rising-edge crash count (not-crashed -> crashed). Mirrors the
        // StatsManager player-only pattern, but applied per rider.
        if (crashed && !data.prevCrashedState) {
            data.sessionCrashCount++;
            // Same edge, spoken. Tapped here rather than polled because this
            // is the only place the transition exists — a consumer reading
            // the counter would have to keep its own previous value, which is
            // the state this already owns.
            SpotterManager::getInstance().onRiderCrash(
                raceNum, getDisplayRaceNum(), getSessionElapsedTime());
        }
        data.prevCrashedState = crashed;

        // Update position data
        data.trackPos = trackPos;
        data.numLaps = numLaps;
        data.sessionTime = sessionTime;
        data.crashed = crashed;
        data.wrongWay = wrongWay;
    } else {
        // Create new position entry
        RiderTrackState posData;
        posData.trackPos = trackPos;
        posData.numLaps = numLaps;
        posData.sessionTime = sessionTime;
        posData.crashed = crashed;
        posData.wrongWay = false;
        posData.previousTrackPos = trackPos;
        posData.lastSignificantTrackPos = trackPos;  // Initialize hazard reference position
        // First observation of this rider — seed the edge detector from the
        // current state so we don't count an already-crashed rider as a new
        // crash on first sight. (sessionCrashCount defaults to 0 inline.)
        posData.prevCrashedState = crashed;

        m_trackPositions[raceNum] = posData;
    }

    // Store current session time
    setSessionTime(sessionTime);

    // Invalidate caches (depend on track positions)
    m_blueFlagsDirty = true;
    m_hazardsDirty = true;
    m_hazardTypesDirty = true;
}

void PluginData::updateActiveTrackPosRiders(int numVehicles, const Unified::TrackPositionData* positions) {
    m_activeTrackPosRiders.clear();
    for (int i = 0; i < numVehicles; ++i) {
        m_activeTrackPosRiders.insert(positions[i].raceNum);
    }
}

bool PluginData::isRiderExcludedFromDetection(const StandingsData& standing) const {
    int state = standing.state;
    return state == static_cast<int>(Unified::EntryState::DNS) ||
           state == static_cast<int>(Unified::EntryState::Retired) ||
           state == static_cast<int>(Unified::EntryState::DSQ) ||
           standing.pit == 1;
}

bool PluginData::isRiderSpectatable(int raceNum) const {
    if (raceNum < 0) return false;
    auto it = m_standings.find(raceNum);
    // No standings row = not in the classification the spectate list is built from.
    if (it == m_standings.end()) return false;
    // Stated directly rather than layered on isRiderExcludedFromDetection(). That predicate
    // agrees with this one today, but it answers a different question — who is eligible to
    // be flagged as a HAZARD — and reusing it makes only its pit clause load-bearing here
    // (state == NORMAL already subsumes its DNS/Retired/DSQ tests, and additionally rejects
    // Unknown). Left coupled, a change to hazard tuning would silently move which riders are
    // clickable on four HUDs.
    return it->second.state == PluginConstants::RiderState::NORMAL &&
           it->second.pit != 1;
}

bool PluginData::isPlayerGoingWrongWay() const {
    int displayRaceNum = getDisplayRaceNum();
    auto it = m_trackPositions.find(displayRaceNum);
    if (it != m_trackPositions.end()) {
        return it->second.wrongWay;
    }
    return false;  // No position data = assume not wrong way
}

const RiderTrackState* PluginData::getPlayerTrackPosition() const {
    int displayRaceNum = getDisplayRaceNum();
    auto it = m_trackPositions.find(displayRaceNum);
    if (it != m_trackPositions.end()) {
        return &it->second;
    }
    return nullptr;  // No position data available
}

int PluginData::getRiderSessionCrashCount(int raceNum) const {
    auto it = m_trackPositions.find(raceNum);
    if (it != m_trackPositions.end()) {
        return it->second.sessionCrashCount;
    }
    return 0;  // No observations for this rider yet
}

void PluginData::rebuildBlueFlagCaches() const {
    m_blueFlagsDirty = false;
    m_cachedPlayerBlueFlagged = false;
    m_cachedPlayerLapping = false;
    m_cachedBlueFlaggedSet.clear();
    m_cachedLapperToLapped.clear();

    if (!isRaceSession()) {
        return;
    }

    float trackLength = m_sessionData.trackLength;
    float awarenessThreshold = (trackLength > 0.0f)
        ? (m_proximity.blueFlagAwarenessDistance / trackLength)
        : 0.06f;

    // Find the lap-count spread for early exit — if every rider shares the same lap
    // count, nobody is a lap down and the whole O(n^2) loop can be skipped (the common
    // case in most races). Track min vs max (not the second-highest): a tie at the front
    // must not mask a backmarker further down, e.g. laps [10, 10, 8] still has a lapping.
    int maxLaps = 0;
    int minLaps = 0;
    bool anyRider = false;
    for (const auto& [rn, st] : m_standings) {
        if (!anyRider) {
            maxLaps = minLaps = st.numLaps;
            anyRider = true;
        } else {
            if (st.numLaps > maxLaps) maxLaps = st.numLaps;
            if (st.numLaps < minLaps) minLaps = st.numLaps;
        }
    }
    if (!anyRider || maxLaps <= minLaps) {
        // Everyone on the same lap — no blue flags (or lappings) possible
        m_cachedPlayerBlueFlagged = false;
        m_cachedPlayerLapping = false;
        return;
    }

    // Snapshot the display rider's race state once, so the loop below can also detect the
    // mirror case (player closing on a backmarker ahead) without a second pass.
    int playerRaceNum = getDisplayRaceNum();
    int playerLaps = -1;
    float playerTrackPos = 0.0f;
    bool playerActive = false;
    bool playerFinished = false;
    {
        auto stIt = m_standings.find(playerRaceNum);
        auto posIt = m_trackPositions.find(playerRaceNum);
        if (stIt != m_standings.end() && posIt != m_trackPositions.end()
            && m_activeTrackPosRiders.count(playerRaceNum)
            && !isRiderExcludedFromDetection(stIt->second)) {
            playerLaps = stIt->second.numLaps;
            playerTrackPos = posIt->second.trackPos;
            playerActive = true;
            playerFinished = m_sessionData.isRiderFinished(
                stIt->second.numLaps, stIt->second.numLapsAtLeaderFinish);
        }
    }

    // One flat pass: collect every rider that has a track position, tagged with its
    // lap/pos and the role flags, so the pairwise loop below reads a contiguous
    // array instead of doing two hash lookups (m_activeTrackPosRiders.count +
    // m_trackPositions.find) per inner iteration. The
    // roles have DIFFERENT eligibility: a BACKMARKER (outer) must not be
    // excluded/finished; a LAPPER (inner) must be active and not finished, but an
    // otherwise-excluded rider (pit lane) with more laps can still trigger a blue
    // flag. Why finished bars the lapper role is at BlueFlag::Rider.
    m_blueFlagScratch.clear();
    for (const auto& [rn, st] : m_standings) {
        auto posIt = m_trackPositions.find(rn);
        if (posIt == m_trackPositions.end()) continue;   // both roles require a track position
        const bool finished =
            m_sessionData.isRiderFinished(st.numLaps, st.numLapsAtLeaderFinish);
        bool eligibleBackmarker = !isRiderExcludedFromDetection(st) && !finished;
        m_blueFlagScratch.push_back({ rn, st.numLaps, posIt->second.trackPos,
                                      m_activeTrackPosRiders.count(rn) > 0,
                                      eligibleBackmarker, finished });
    }

    // The pairwise proximity pass itself is pure logic over the flat array —
    // it lives in core/blue_flag_detect.h so it can be unit-tested with a plain
    // g++ (tests/unit/test_blue_flag_detect.cpp) instead of only through the DLL
    // under Wine. Iteration order is m_standings order (the scratch is built
    // in that order), which is what decides "first lapper found" /
    // last-writer-wins results.
    BlueFlag::detect(m_blueFlagScratch, maxLaps, awarenessThreshold,
                     BlueFlag::Player{ playerRaceNum, playerLaps, playerTrackPos,
                                       playerActive, playerFinished },
                     m_cachedBlueFlaggedSet, m_cachedLapperToLapped, m_cachedPlayerLapping);

    // Player blue flag is just a lookup into the per-rider set
    m_cachedPlayerBlueFlagged = m_cachedBlueFlaggedSet.count(playerRaceNum) > 0;
}

bool PluginData::isPlayerBlueFlagged() const {
    if (m_blueFlagsDirty) {
        rebuildBlueFlagCaches();
    }
    return m_cachedPlayerBlueFlagged;
}

bool PluginData::isRiderBlueFlagged(int raceNum) const {
    if (m_blueFlagsDirty) {
        rebuildBlueFlagCaches();
    }
    return m_cachedBlueFlaggedSet.count(raceNum) > 0;
}

bool PluginData::isRiderLapping(int raceNum) const {
    if (m_blueFlagsDirty) {
        rebuildBlueFlagCaches();
    }
    return m_cachedLapperToLapped.count(raceNum) > 0;
}

int PluginData::getRiderLappingTarget(int raceNum) const {
    if (m_blueFlagsDirty) {
        rebuildBlueFlagCaches();
    }
    auto it = m_cachedLapperToLapped.find(raceNum);
    return (it != m_cachedLapperToLapped.end()) ? it->second : -1;
}

int PluginData::getRiderLappedBy(int raceNum) const {
    if (m_blueFlagsDirty) {
        rebuildBlueFlagCaches();
    }
    // LOWEST race number when more than one lapper is closing, not the first
    // the container happens to yield: m_cachedLapperToLapped is unordered, so
    // "first" can differ between two rebuilds of identical state and the
    // callout would name a different bike each time it fired. Which of two
    // simultaneous lappers is "the" one is arbitrary either way — being
    // arbitrary the SAME way is the part that matters.
    int lapper = -1;
    for (const auto& kv : m_cachedLapperToLapped) {
        if (kv.second != raceNum) continue;
        if (lapper < 0 || kv.first < lapper) lapper = kv.first;
    }
    return lapper;
}

bool PluginData::isPlayerLapping() const {
    if (m_blueFlagsDirty) {
        rebuildBlueFlagCaches();
    }
    return m_cachedPlayerLapping;
}

HazardType PluginData::getRiderHazardType(int raceNum) const {
    if (m_hazardTypesDirty) {
        rebuildHazardTypeCaches();
    }
    auto it = m_cachedHazardTypes.find(raceNum);
    return (it != m_cachedHazardTypes.end()) ? it->second : HazardType::None;
}

void PluginData::rebuildHazardTypeCaches() const {
    m_hazardTypesDirty = false;
    m_cachedHazardTypes.clear();

    // Suppress during pre-race grid wait
    if (isRaceSession() && !(m_sessionData.sessionState & PluginConstants::SessionState::IN_PROGRESS)) {
        return;
    }

    // Suppress the grid-launch crowd: from the green flag through the gate hold and the launch,
    // until the display rider clears the first split. Riders shuffling on the grid (stationary /
    // briefly wrong-way) aren't hazards. Sector-based so it covers races AND grid qualifying and
    // adapts to the (variable) gate hold. (Not for pit starts, which never enter this grace.)
    if (isInGridStartGrace()) {
        return;
    }

    // Single timestamp for all per-rider hazard checks
    auto now = std::chrono::steady_clock::now();
    for (const auto& [raceNum, pos] : m_trackPositions) {
        HazardType type = computeRiderHazardType(raceNum, now);
        if (type != HazardType::None) {
            m_cachedHazardTypes[raceNum] = type;
        }
    }
}

void PluginData::startPitExitGrace(int raceNum) {
    auto trackIt = m_trackPositions.find(raceNum);
    if (trackIt == m_trackPositions.end()) {
        return;
    }
    trackIt->second.pitExitGraceStart = std::chrono::steady_clock::now();
    trackIt->second.movedSincePitExit = false;
}

HazardType PluginData::computeRiderHazardType(int raceNum, std::chrono::steady_clock::time_point now) const {
    // Note: global suppression checks (pre-race, grace period) are handled by
    // rebuildHazardTypeCaches() — this only checks per-rider conditions.

    auto it = m_trackPositions.find(raceNum);
    if (it == m_trackPositions.end() || it->second.hazardType == HazardType::None) {
        return HazardType::None;
    }

    // Suppress during per-rider grace period after pit exit
    if (it->second.pitExitGraceStart.time_since_epoch().count() != 0) {
        auto elapsedMs = std::chrono::duration_cast<std::chrono::milliseconds>(now - it->second.pitExitGraceStart).count();
        if (elapsedMs < m_proximity.hazardGracePeriodMs) {
            return HazardType::None;
        }
    }

    // Suppress Stationary hazard for riders who left the pits but haven't moved yet.
    // A motionless rider sitting at pit exit can't be blue-flagged or be a real hazard,
    // so we keep the warning suppressed until they actually roll out. WrongWay is
    // unaffected (you can't be going the wrong way without moving).
    if (it->second.hazardType == HazardType::Stationary && !it->second.movedSincePitExit) {
        return HazardType::None;
    }

    // Re-check exclusion: rider may have retired/DNS/DSQ or entered pits
    auto standingIt = m_standings.find(raceNum);
    if (standingIt != m_standings.end()) {
        if (isRiderExcludedFromDetection(standingIt->second)) {
            return HazardType::None;
        }
        // Finished riders stopped near the finish line are expected, not a hazard.
        if (it->second.hazardType == HazardType::Stationary &&
            m_sessionData.isRiderFinished(standingIt->second.numLaps, standingIt->second.numLapsAtLeaderFinish) &&
            it->second.trackPos < 0.5f) {
            return HazardType::None;
        }
    }
    return it->second.hazardType;
}

bool PluginData::isHazardAhead() const {
    return !getHazardRaceNums().empty();
}

const std::vector<int>& PluginData::getHazardRaceNums() const {
    if (!m_hazardsDirty) {
        return m_cachedHazardRaceNums;
    }

    m_hazardsDirty = false;
    m_cachedHazardRaceNums.clear();

    // Suppress during pre-race grid wait (riders stationary on grid is not a hazard)
    if (isRaceSession() && !(m_sessionData.sessionState & PluginConstants::SessionState::IN_PROGRESS)) {
        return m_cachedHazardRaceNums;
    }

    // Get display rider's race number and track position
    int displayRaceNum = getDisplayRaceNum();
    if (displayRaceNum <= 0) {
        return m_cachedHazardRaceNums;
    }

    auto displayPosIt = m_trackPositions.find(displayRaceNum);
    if (displayPosIt == m_trackPositions.end()) {
        return m_cachedHazardRaceNums;
    }

    float displayTrackPos = displayPosIt->second.trackPos;

    // Grid-start grace: suppress all hazards from the green flag through the gate hold and launch
    // until the display rider clears the first split (see isInGridStartGrace / rebuildHazardTypeCaches).
    if (isInGridStartGrace()) {
        return m_cachedHazardRaceNums;
    }

    // Convert awareness distances from meters to track percentage. Wrong-way gets its own,
    // longer reach: a stationary bike is closed at your speed, an oncoming rider at roughly
    // double, so the same metres buy about half the warning. See ProximityTuning.
    float trackLength = m_sessionData.trackLength;
    const bool haveLength = (trackLength > 0.0f);
    float awarenessThreshold = haveLength
        ? (m_proximity.hazardAwarenessDistance / trackLength)
        : 0.06f;  // Fallback: ~6% of track
    float wrongWayThreshold = haveLength
        ? (m_proximity.hazardWrongWayAwarenessDistance / trackLength)
        : 0.15f;  // Fallback: ~15% of track (same 2.5x ratio as the defaults)

    // Check all riders in classification
    for (int otherRaceNum : m_classificationOrder) {
        if (otherRaceNum == displayRaceNum) {
            continue;  // Never flag the display rider themselves
        }

        auto otherPosIt = m_trackPositions.find(otherRaceNum);
        if (otherPosIt == m_trackPositions.end()) {
            continue;
        }

        const RiderTrackState& otherPos = otherPosIt->second;

        // Only include confirmed hazards (use getRiderHazardType for exclusion check)
        const HazardType hazardType = getRiderHazardType(otherRaceNum);
        if (hazardType == HazardType::None) {
            continue;
        }

        // Check if hazard rider is ahead within awareness distance (wraparound-aware)
        float distanceAhead = otherPos.trackPos - displayTrackPos;
        if (distanceAhead < 0.0f) {
            distanceAhead += 1.0f;
        }

        const float threshold = (hazardType == HazardType::WrongWay)
            ? wrongWayThreshold : awarenessThreshold;
        if (distanceAhead <= threshold) {
            m_cachedHazardRaceNums.push_back(otherRaceNum);
        }
    }

    return m_cachedHazardRaceNums;
}

