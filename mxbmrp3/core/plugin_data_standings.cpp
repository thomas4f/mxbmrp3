// ============================================================================
// core/plugin_data_standings.cpp
// Standings, track position, real-time gaps, hazards, blue flags
// (extracted verbatim from plugin_data.cpp; no behavior change)
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

void PluginData::updateStandings(int raceNum, int state, int bestLap, int bestLapNum,
    int numLaps, int gap, int gapLaps, int penalty, int pit, bool notify) {
    auto it = m_standings.find(raceNum);

    if (it != m_standings.end()) {
        // Entry exists - check if data changed
        // PERFORMANCE: Order comparisons by likelihood of change (gap/numLaps change most frequently)
        if (it->second.gap != gap || it->second.numLaps != numLaps ||
            it->second.state != state || it->second.bestLap != bestLap ||
            it->second.gapLaps != gapLaps || it->second.penalty != penalty ||
            it->second.bestLapNum != bestLapNum || it->second.pit != pit) {

            // Detect pit exit (pit 1→0) and start per-rider hazard grace period
            if (it->second.pit == 1 && pit == 0) {
                startPitExitGrace(raceNum);
            }

            it->second.state = state;
            it->second.bestLap = bestLap;
            it->second.bestLapNum = bestLapNum;
            it->second.numLaps = numLaps;
            it->second.gap = gap;
            it->second.gapLaps = gapLaps;
            it->second.penalty = penalty;
            it->second.pit = pit;
        }
        else {
            // No change, skip notification
            return;
        }
    }
    else {
        // New entry
        m_standings.emplace(raceNum, StandingsData(raceNum, state, bestLap, bestLapNum,
            numLaps, gap, gapLaps, penalty, pit));
    }

    // Notify HUD manager if requested
    if (notify) {
        notifyHudManager(DataChangeType::Standings);
    }
}

void PluginData::batchUpdateStandings(Unified::RaceClassificationEntry* entries, int numEntries) {
    // Batch update all standings AND build classification order in single pass
    // Eliminates duplicate iteration of the same array

    // Clamp to max supported entries (defensive against corrupt API data)
    if (numEntries > Unified::MAX_RACE_ENTRIES) numEntries = Unified::MAX_RACE_ENTRIES;

    bool anyChanged = false;

    // Reserve space for classification order (avoid reallocations)
    m_classificationOrder.clear();
    m_classificationOrder.reserve(numEntries);

    for (int i = 0; i < numEntries; ++i) {
        const Unified::RaceClassificationEntry& entry = entries[i];

        // Build classification order (game already sorted by position)
        m_classificationOrder.push_back(entry.raceNum);

        // Update standings data
        auto it = m_standings.find(entry.raceNum);

        // Convert unified types to internal types
        int entryState = static_cast<int>(entry.state);
        int entryPit = entry.inPit ? 1 : 0;

        if (it != m_standings.end()) {
            // Entry exists - check if data changed
            StandingsData& standing = it->second;

            // Handle official gap with caching to prevent flicker
            // The API temporarily clears gaps (sends 0) when leader crosses line
            // We cache the last valid gap and use it when API sends 0
            // Exception: leader (i==0) should always have gap=0, clear their cache
            int effectiveGap = entry.gap;
            if (i == 0) {
                // Leader's gap is always 0 - clear any stale cached gap
                m_lastValidOfficialGap.erase(entry.raceNum);
            } else if (entry.gap > 0) {
                // Valid gap from API - cache it
                m_lastValidOfficialGap[entry.raceNum] = entry.gap;
            } else if (entry.gap == 0 && entry.gapLaps == 0) {
                // API sent zero gap - check if we have cached value
                auto cachedIt = m_lastValidOfficialGap.find(entry.raceNum);
                if (cachedIt != m_lastValidOfficialGap.end()) {
                    effectiveGap = cachedIt->second;
                }
            }

            if (standing.state != entryState ||
                standing.bestLap != entry.bestLap ||
                standing.bestLapNum != entry.bestLapNum ||
                standing.numLaps != entry.numLaps ||
                standing.gap != effectiveGap ||
                standing.gapLaps != entry.gapLaps ||
                standing.penalty != entry.penalty ||
                standing.pit != entryPit) {

                // Detect pit transitions and log events
                if (standing.pit != entryPit) {
                    const RaceEntryData* raceEntry = getRaceEntry(entry.raceNum);
                    const char* riderLabel = raceEntry ? raceEntry->formattedRaceNum : "???";
                    char eventMsg[64];
                    if (standing.pit == 0 && entryPit == 1) {
                        // Pit entry (0→1)
                        snprintf(eventMsg, sizeof(eventMsg), "%s entered pits", riderLabel);
                        addEventLogEntry(EventLogType::PitEntry, eventMsg);
                    } else {
                        // Pit exit (1→0) - start per-rider hazard grace period
                        snprintf(eventMsg, sizeof(eventMsg), "%s left pits", riderLabel);
                        addEventLogEntry(EventLogType::PitExit, eventMsg);
                        startPitExitGrace(entry.raceNum);
                        // Reset the display rider's live lap timer to the placeholder (like a
                        // fresh track entry) instead of letting the in-progress dead lap keep
                        // ticking through pit exit; the next S/F crossing re-anchors it.
                        invalidateLapTimerAnchor(entry.raceNum);
                    }
                }

                standing.state = entryState;
                standing.bestLap = entry.bestLap;
                standing.bestLapNum = entry.bestLapNum;
                standing.numLaps = entry.numLaps;
                standing.gap = effectiveGap;
                standing.gapLaps = entry.gapLaps;
                standing.penalty = entry.penalty;
                standing.pit = entryPit;

                anyChanged = true;
            }
        }
        else {
            // New entry
            int effectiveGap = entry.gap;
            // Only cache gap for non-leaders (leader gap should always be 0)
            if (i > 0 && effectiveGap > 0) {
                m_lastValidOfficialGap[entry.raceNum] = effectiveGap;
            }
            m_standings.emplace(entry.raceNum,
                StandingsData(entry.raceNum, entryState, entry.bestLap,
                    entry.bestLapNum, entry.numLaps, effectiveGap,
                    entry.gapLaps, entry.penalty, entryPit));
            anyChanged = true;
        }

    }

    // Mark position cache dirty now that classification order is rebuilt,
    // so any position lookups below use the fresh order
    m_bPositionCacheDirty = true;
    m_bFilteredOrderDirty = true;

    // Detect leader change (race sessions only)
    // Skip when leader has finished (lead changes after checkered flag aren't meaningful)
    if (!m_classificationOrder.empty() && isRaceSession() && m_sessionData.leaderFinishTime < 0) {
        int newLeader = m_classificationOrder[0];
        if (m_lastLeaderRaceNum >= 0 && newLeader != m_lastLeaderRaceNum) {
            const RaceEntryData* entry = getRaceEntry(newLeader);
            if (entry) {
                char eventMsg[64];
                snprintf(eventMsg, sizeof(eventMsg), "#%d takes the lead", newLeader);
                addEventLogEntry(EventLogType::LeaderChange, eventMsg);
            }
        }
        m_lastLeaderRaceNum = newLeader;
    }

    // Capture finish time for each rider when they finish
    // Calculate elapsed time based on race type (same formula for all riders)
    auto calculateElapsedTime = [&]() -> int {
        if (m_sessionData.sessionLength > 0) {
            // Timed race: elapsed = sessionLength - sessionTime
            return m_sessionData.sessionLength - m_currentSessionTime;
        } else {
            // Lap-based race: sessionTime is elapsed time
            return m_currentSessionTime > 0 ? m_currentSessionTime : 0;
        }
    };

    // Check each rider for finish
    bool leaderJustFinished = false;
    for (auto& [raceNum, standing] : m_standings) {
        // Only capture once (when finishTime transitions from -1)
        if (standing.finishTime < 0 && m_sessionData.isRiderFinished(standing.numLaps, standing.numLapsAtLeaderFinish)) {
            standing.finishTime = calculateElapsedTime();
            DEBUG_INFO_F("[RIDER FINISHED] Rider #%d finished race in %d ms", raceNum, standing.finishTime);
            anyChanged = true;

            // Event log: rider finished with position from fresh classification
            {
                const RaceEntryData* entry = getRaceEntry(raceNum);
                const char* riderLabel = entry ? entry->formattedRaceNum : "???";
                int position = getDisplayPositionForRaceNum(raceNum);
                char eventMsg[64];
                if (position > 0) {
                    snprintf(eventMsg, sizeof(eventMsg), "%s finished P%d", riderLabel, position);
                } else {
                    snprintf(eventMsg, sizeof(eventMsg), "%s finished", riderLabel);
                }
                addEventLogEntry(EventLogType::RiderFinished, eventMsg);
            }

            // Also update leader finish time if this is the leader
            if (!m_classificationOrder.empty() && raceNum == m_classificationOrder[0] && m_sessionData.leaderFinishTime < 0) {
                m_sessionData.leaderFinishTime = standing.finishTime;
                leaderJustFinished = true;
                DEBUG_INFO_F("[LEADER FINISHED] Leader #%d finished race in %d ms", raceNum, standing.finishTime);
            }
        }
    }

    // When leader just finished, snapshot each non-finished rider's current numLaps
    // so they finish on their next line crossing (handles lapped riders in both pure lap and timed+laps races)
    if (leaderJustFinished) {
        for (auto& [raceNum, standing] : m_standings) {
            if (standing.finishTime < 0 && standing.numLapsAtLeaderFinish < 0) {
                standing.numLapsAtLeaderFinish = standing.numLaps;
                DEBUG_INFO_F("[LAPPED FINISH SETUP] Rider #%d snapshot numLaps=%d at leader finish", raceNum, standing.numLaps);
                anyChanged = true;
            }
        }
    }

    // Notify once if anything changed
    if (anyChanged) {
        // Position cache already marked dirty after classification rebuild above
        notifyHudManager(DataChangeType::Standings);
    }
}

void PluginData::setRiderSessionFinished(int raceNum) {
    auto it = m_standings.find(raceNum);
    if (it != m_standings.end() && !it->second.sessionFinished) {
        it->second.sessionFinished = true;
        DEBUG_INFO_F("[SESSION FINISHED] Rider #%d finished non-race session", raceNum);
        notifyHudManager(DataChangeType::Standings);
    }
}

void PluginData::resetStandingsFinishState() {
    // Reset per-rider finish tracking at the start of a new session.
    // m_standings persists across sessions within an event (the server keeps the same
    // race numbers), so these per-rider fields MUST be cleared here or they carry over.
    // finishTime/numLapsAtLeaderFinish in particular gate the finish-detection loop in
    // batchUpdateStandings (it only fires while finishTime < 0): leaving them stale makes
    // every session after the first one with finishers silently skip finish capture
    // (no finishTime, leaderFinishTime never set, no lapped-finish snapshot). The session
    // clock (getLeaderLapsToGo) recomputes from finishLap and is unaffected, so this rots
    // invisibly until you inspect the standings finish order.
    bool anyChanged = false;
    for (auto& [raceNum, standing] : m_standings) {
        if (standing.sessionFinished || standing.finishTime >= 0 || standing.numLapsAtLeaderFinish >= 0) {
            standing.sessionFinished = false;
            standing.finishTime = -1;
            standing.numLapsAtLeaderFinish = -1;
            anyChanged = true;
        }
    }
    if (anyChanged) {
        notifyHudManager(DataChangeType::Standings);
    }
}

const StandingsData* PluginData::getStanding(int raceNum) const {
    auto it = m_standings.find(raceNum);
    return (it != m_standings.end()) ? &it->second : nullptr;
}

int PluginData::getLeaderLapsToGo() const {
    const SessionData& s = m_sessionData;
    // Only meaningful for a time+lap race that has entered overtime.
    if (!(s.sessionLength > 0 && s.sessionNumLaps > 0 && s.overtimeStarted && s.finishLap > 0)) {
        return -1;
    }
    if (m_classificationOrder.empty()) return -1;
    const StandingsData* leader = getStanding(m_classificationOrder[0]);
    if (!leader) return -1;

    // finishLap is the lap a rider must EXCEED to finish; the leader is on the
    // final lap once they've completed finishLap laps (matches the FinalLap event
    // in race_lap_handler). Reuse the canonical finish check so the "checkered"
    // threshold stays single-sourced; the "N to go" count still needs finishLap.
    if (s.isRiderFinished(leader->numLaps)) return 0;     // checkered
    int toGo = s.finishLap - leader->numLaps + 1;         // 1 == final lap

    // The clock expires partway through a lap, so the leader must still finish the
    // lap that was in progress before the bonus laps begin (that lap makes toGo ==
    // sessionNumLaps + 1). Hold at the normal clock (00:00, since the timer is at/
    // below zero) during that lap instead of showing "N+1 TO GO"; the leader-relative
    // countdown only starts once they cross S/F into the bonus laps (toGo == N).
    if (toGo > s.sessionNumLaps) return -1;
    return toGo < 1 ? 1 : toGo;
}

void PluginData::setClassificationOrder(const std::vector<int>& order) {
    m_classificationOrder = order;
    m_bPositionCacheDirty = true;  // Mark position cache dirty when classification changes
    m_bFilteredOrderDirty = true;
    // Note: We don't notify HudManager here because this is called as part of
    // the standings update, which already triggers a notification
}

int PluginData::getPositionForRaceNum(int raceNum) const {
    // Rebuild cache if dirty (only happens when classification changes)
    if (m_bPositionCacheDirty) {
        m_positionCache.clear();

        // Build position cache from classification order
        // Position is simply the index in classification order (1-based)
        // This matches how StandingsHud calculates positions
        for (size_t i = 0; i < m_classificationOrder.size(); ++i) {
            m_positionCache[m_classificationOrder[i]] = static_cast<int>(i) + 1;
        }
        m_bPositionCacheDirty = false;
    }

    // Lookup position in cache (O(1) operation)
    auto it = m_positionCache.find(raceNum);
    if (it != m_positionCache.end()) {
        return it->second;
    }
    return -1;  // Not found in standings
}

std::vector<std::vector<int>> PluginData::getBattleGroups(int gapThresholdMs, int maxLeaderPos) const {
    // Build the racing, on-track field ordered by position, each rider carrying its
    // official split gap to the leader (stable; see the gap-source note below).
    struct R { int pos; int raceNum; int gap; int gapLaps; };
    std::vector<R> rs;
    rs.reserve(m_standings.size());
    // Defer battles during the opening lap of a race: off the start the whole field is
    // bunched nose-to-tail and "everyone is battling", so gap-based groups there are just
    // noise. A rider still on their first lap (numLaps == 0) is left out of the grouping;
    // as the front of the field crosses the line, real battles form naturally. Race-only:
    // a non-race rank is by best lap, and its out/flying laps aren't a first-lap scramble.
    const bool raceSession = isRaceSession();
    for (const auto& kv : m_standings) {
        const StandingsData& s = kv.second;
        if (s.state != 0) continue;   // racing only (skip DNS/Retired/DSQ)
        if (s.pit != 0) continue;     // on track only
        if (raceSession && s.numLaps < 1) continue;  // opening lap: no battles yet
        // A rider who has crossed the line for good is still "Racing" state on the
        // slow-down lap; a close gap among finishers isn't a battle, so drop them
        // ("Battle for Nth" shouldn't list finished riders). Always false in non-race.
        if (m_sessionData.isRiderFinished(s.numLaps, s.numLapsAtLeaderFinish)) continue;
        int pos = getPositionForRaceNum(s.raceNum);
        if (pos <= 0) continue;
        // Group by the OFFICIAL split gap, not realTimeGap. realTimeGap is unstable for
        // grouping: it's frozen for riders outside the active track-pos batch, and the
        // batch itself (m_activeTrackPosRiders) is recomputed every RaceTrackPosition
        // callback, so a per-rider "use live gap if in batch" rule flips the gap source
        // frame-to-frame and the grouping shuffles -> the overlay battle panel flickers.
        // The official gap only changes at splits, so groups stay stable. (Live gaps
        // belong to near-camera OVERTAKE detection, which is separate and hysteretic.)
        int g = s.gap;
        rs.push_back({ pos, s.raceNum, g, s.gapLaps });
    }
    std::sort(rs.begin(), rs.end(), [](const R& a, const R& b) { return a.pos < b.pos; });

    std::vector<std::vector<int>> groups;
    size_t i = 0;
    while (i < rs.size()) {
        size_t j = i;
        // Greedily chain adjacent same-lap riders within the gap threshold.
        while (j + 1 < rs.size()
               && rs[j].gapLaps == rs[j + 1].gapLaps
               && (rs[j + 1].gap - rs[j].gap) > 0
               && (rs[j + 1].gap - rs[j].gap) <= gapThresholdMs) {
            ++j;
        }
        if (j > i) {
            if (maxLeaderPos <= 0 || rs[i].pos <= maxLeaderPos) {
                std::vector<int> grp;
                grp.reserve(j - i + 1);
                for (size_t k = i; k <= j; ++k) grp.push_back(rs[k].raceNum);
                groups.push_back(std::move(grp));
            }
            i = j + 1;
        } else {
            i = i + 1;
        }
    }
    return groups;
}

void PluginData::snapshotRaceStartPositions() {
    // Capture the official starting order so the standings HUD / web overlay can show
    // how many positions each rider has gained or lost since the race went green.
    // Positions mirror getPositionForRaceNum() (1-based index into classification order).
    m_raceStartPositions.clear();

    // Defensive secondary check. The primary protection for mid-race joins lives in the
    // caller: a joining spectator gets RaceSession already IN_PROGRESS (which sets the
    // cached state), so the green-flag transition in handleRaceSessionState self-skips and
    // this is never called. This guard only matters if the snapshot site is somehow reached
    // when the race is already underway — if any rider has a completed lap, leave the
    // snapshot empty so the column shows its placeholder instead of a mid-race order.
    // (Note: it does NOT catch a join during lap 1, when numLaps is still 0 for everyone;
    // that case is covered by the cached-state gating in the caller, not here.)
    for (const auto& entry : m_standings) {
        if (entry.second.numLaps > 0) {
            return;
        }
    }

    m_raceStartPositions.reserve(m_classificationOrder.size());
    for (size_t i = 0; i < m_classificationOrder.size(); ++i) {
        m_raceStartPositions[m_classificationOrder[i]] = static_cast<int>(i) + 1;
    }
}

void PluginData::setFilterDnsRiders(bool enabled) {
    if (m_filterDnsRiders != enabled) {
        m_filterDnsRiders = enabled;
        m_bFilteredOrderDirty = true;
        notifyHudManager(DataChangeType::Standings);
    }
}

const std::vector<int>& PluginData::getDisplayClassificationOrder() const {
    // If DNS filtering is off, return official order directly
    if (!m_filterDnsRiders) {
        return m_classificationOrder;
    }

    // Rebuild filtered cache if dirty
    if (m_bFilteredOrderDirty) {
        m_filteredClassificationOrder.clear();
        m_filteredClassificationOrder.reserve(m_classificationOrder.size());
        for (int raceNum : m_classificationOrder) {
            auto it = m_standings.find(raceNum);
            if (it == m_standings.end() || it->second.state != PluginConstants::RiderState::DNS) {
                m_filteredClassificationOrder.push_back(raceNum);
            }
        }
        // Also rebuild filtered position cache
        m_filteredPositionCache.clear();
        for (size_t i = 0; i < m_filteredClassificationOrder.size(); ++i) {
            m_filteredPositionCache[m_filteredClassificationOrder[i]] = static_cast<int>(i) + 1;
        }
        m_bFilteredOrderDirty = false;
    }

    return m_filteredClassificationOrder;
}

int PluginData::getDisplayPositionForRaceNum(int raceNum) const {
    // If DNS filtering is on, use filtered cache
    if (m_filterDnsRiders) {
        // Ensure filtered order is up to date (rebuilds caches if dirty)
        getDisplayClassificationOrder();
        auto it = m_filteredPositionCache.find(raceNum);
        if (it != m_filteredPositionCache.end()) {
            return it->second;
        }
        return -1;  // DNS rider filtered out, or not found
    }

    return getPositionForRaceNum(raceNum);
}

void PluginData::updateTrackPosition(int raceNum, float trackPos, int numLaps, bool crashed, int sessionTime) {
    auto it = m_trackPositions.find(raceNum);

    if (it != m_trackPositions.end()) {
        TrackPositionData& data = it->second;

        // Detect teleport (reset to track / pit exit) by checking single-frame position jump.
        // Compute wrapped delta to handle normal start/finish crossing correctly.
        float rawDelta = trackPos - data.trackPos;
        if (rawDelta > 0.5f) rawDelta -= 1.0f;   // wrapped backward through S/F
        if (rawDelta < -0.5f) rawDelta += 1.0f;   // wrapped forward through S/F
        if (std::abs(rawDelta) > TrackPositionData::TELEPORT_THRESHOLD) {
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
            (std::chrono::duration_cast<std::chrono::milliseconds>(now - data.wrongWaySince).count() >= m_hazardWrongWayDurationMs);
        data.previousTrackPos = trackPos;

        // === Hazard detection (stationary) ===
        // (now already declared above for wrong-way detection)

        // Stationary detection: check if rider has moved significantly
        float posDelta = std::abs(trackPos - data.lastSignificantTrackPos);
        posDelta = std::min(posDelta, 1.0f - posDelta);  // Wraparound-aware

        // Convert tolerance from meters to track percentage
        float trackLength = m_sessionData.trackLength;
        float tolerancePct = (trackLength > 0.0f)
            ? (m_hazardStationaryToleranceMeters / trackLength)
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
            (std::chrono::duration_cast<std::chrono::milliseconds>(now - data.stationarySince).count() >= m_hazardStationaryDurationMs);
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
                } else if (cooldownActive && std::chrono::duration_cast<std::chrono::milliseconds>(now - data.hazardClearedAt).count() >= m_hazardCooldownMs) {
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
        TrackPositionData posData;
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

bool PluginData::isPlayerGoingWrongWay() const {
    int displayRaceNum = getDisplayRaceNum();
    auto it = m_trackPositions.find(displayRaceNum);
    if (it != m_trackPositions.end()) {
        return it->second.wrongWay;
    }
    return false;  // No position data = assume not wrong way
}

const TrackPositionData* PluginData::getPlayerTrackPosition() const {
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
        ? (m_blueFlagAwarenessDistance / trackLength)
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
    {
        auto stIt = m_standings.find(playerRaceNum);
        auto posIt = m_trackPositions.find(playerRaceNum);
        if (stIt != m_standings.end() && posIt != m_trackPositions.end()
            && m_activeTrackPosRiders.count(playerRaceNum)
            && !isRiderExcludedFromDetection(stIt->second)) {
            playerLaps = stIt->second.numLaps;
            playerTrackPos = posIt->second.trackPos;
            playerActive = true;
        }
    }

    // Build per-rider blue flag set: for each rider, check if any rider with 1+ more laps
    // is approaching from behind within awareness distance
    for (const auto& [riderRaceNum, riderStanding] : m_standings) {
        if (isRiderExcludedFromDetection(riderStanding)) continue;
        if (m_sessionData.isRiderFinished(riderStanding.numLaps, riderStanding.numLapsAtLeaderFinish)) continue;

        int riderLaps = riderStanding.numLaps;

        // Skip riders at the leader's lap count (they can't be blue flagged)
        if (riderLaps >= maxLaps) continue;

        auto riderPosIt = m_trackPositions.find(riderRaceNum);
        if (riderPosIt == m_trackPositions.end()) continue;

        float riderTrackPos = riderPosIt->second.trackPos;

        // Mirror case: is the display rider the lapper closing on this backmarker from behind?
        // (Same proximity test as below, but with the player fixed as the approaching rider.)
        if (playerActive && !m_cachedPlayerLapping
            && playerRaceNum != riderRaceNum && playerLaps > riderLaps) {
            float playerDistanceBehind = (playerTrackPos < riderTrackPos)
                ? (riderTrackPos - playerTrackPos)
                : ((1.0f - playerTrackPos) + riderTrackPos);
            if (playerDistanceBehind <= awarenessThreshold) {
                m_cachedPlayerLapping = true;
            }
        }

        for (const auto& [otherRaceNum, otherStanding] : m_standings) {
            if (otherRaceNum == riderRaceNum) continue;
            if (otherStanding.numLaps < riderLaps + 1) continue;

            // Skip approaching riders with stale track positions — their trackPos
            // may be from a previous lap, causing false proximity detection
            if (!m_activeTrackPosRiders.count(otherRaceNum)) continue;

            auto otherPosIt = m_trackPositions.find(otherRaceNum);
            if (otherPosIt == m_trackPositions.end()) continue;

            float otherTrackPos = otherPosIt->second.trackPos;
            float distanceBehind = (otherTrackPos < riderTrackPos)
                ? (riderTrackPos - otherTrackPos)
                : ((1.0f - otherTrackPos) + riderTrackPos);

            if (distanceBehind <= awarenessThreshold) {
                m_cachedBlueFlaggedSet.insert(riderRaceNum);
                // otherRaceNum is the lapper closing on this backmarker; record the pair
                // so the director can follow the front-runner working through traffic.
                m_cachedLapperToLapped[otherRaceNum] = riderRaceNum;
                break;
            }
        }
    }

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
    // adapts to the (variable) gate hold, replacing the old fixed time window. (Not for pit
    // starts, which never enter this grace.)
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
        if (elapsedMs < m_hazardGracePeriodMs) {
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

    // Convert awareness distance from meters to track percentage
    float trackLength = m_sessionData.trackLength;
    float awarenessThreshold = (trackLength > 0.0f)
        ? (m_hazardAwarenessDistance / trackLength)
        : 0.06f;  // Fallback: ~6% of track

    // Check all riders in classification
    for (int otherRaceNum : m_classificationOrder) {
        if (otherRaceNum == displayRaceNum) {
            continue;  // Never flag the display rider themselves
        }

        auto otherPosIt = m_trackPositions.find(otherRaceNum);
        if (otherPosIt == m_trackPositions.end()) {
            continue;
        }

        const TrackPositionData& otherPos = otherPosIt->second;

        // Only include confirmed hazards (use getRiderHazardType for exclusion check)
        if (getRiderHazardType(otherRaceNum) == HazardType::None) {
            continue;
        }

        // Check if hazard rider is ahead within awareness distance (wraparound-aware)
        float distanceAhead = otherPos.trackPos - displayTrackPos;
        if (distanceAhead < 0.0f) {
            distanceAhead += 1.0f;
        }

        if (distanceAhead <= awarenessThreshold) {
            m_cachedHazardRaceNums.push_back(otherRaceNum);
        }
    }

    return m_cachedHazardRaceNums;
}

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

    const TrackPositionData& leaderPos = leaderPosIt->second;
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

        const TrackPositionData& riderPos = posIt->second;
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
