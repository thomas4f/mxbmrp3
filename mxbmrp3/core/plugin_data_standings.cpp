// ============================================================================
// core/plugin_data_standings.cpp
// Standings & classification: per-rider standings updates, classification
// order, positions, battle groups, race-start snapshot, DNS display filter.
// (Track position / hazards / blue flags: plugin_data_trackpos.cpp;
//  real-time gaps: plugin_data_livegaps.cpp.)
// ============================================================================

#include "plugin_data.h"
#include "battle_groups.h"
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
                        addEventLogEntry(EventLogType::PitEntry, eventMsg, nullptr, -1, entry.raceNum);
                    } else {
                        // Pit exit (1→0) - start per-rider hazard grace period
                        snprintf(eventMsg, sizeof(eventMsg), "%s left pits", riderLabel);
                        addEventLogEntry(EventLogType::PitExit, eventMsg, nullptr, -1, entry.raceNum);
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
                addEventLogEntry(EventLogType::LeaderChange, eventMsg, nullptr, -1, newLeader);
            }
        }
        m_lastLeaderRaceNum = newLeader;
    }

    // Capture finish time for each rider when they finish (elapsed time —
    // the shared timed/lap-race formula lives in getSessionElapsedTime).
    auto calculateElapsedTime = [&]() -> int { return getSessionElapsedTime(); };

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
                // Message + position in the DETAIL column ("P1") for the race
                // feed. The position also goes across as a NUMBER: the spotter
                // keys the leader's flag (finished_leader) off it, and used to
                // key it off this string, which made the column's format
                // load-bearing for audio. It is a display format again.
                const RaceEntryData* entry = getRaceEntry(raceNum);
                const char* riderLabel = entry ? entry->formattedRaceNum : "???";
                int position = getDisplayPositionForRaceNum(raceNum);
                char eventMsg[64];
                snprintf(eventMsg, sizeof(eventMsg), "%s finished", riderLabel);
                char posDetail[8];
                const char* detail = nullptr;
                if (position > 0) {
                    snprintf(posDetail, sizeof(posDetail), "P%d", position);
                    detail = posDetail;
                }
                addEventLogEntry(EventLogType::RiderFinished, eventMsg, detail, -1, raceNum,
                                 EventNumbers::finished(position));
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
    // Build the racing, on-track field, each rider carrying its official split
    // gap to the leader (stable; see the gap-source note below). The grouping
    // itself is the pure core in battle_groups.h — this method owns only the
    // eligibility filtering.
    std::vector<BattleGroups::Rider> rs;
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
    return BattleGroups::group(rs, gapThresholdMs, maxLeaderPos);
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

