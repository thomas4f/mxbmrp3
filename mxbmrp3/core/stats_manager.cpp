// ============================================================================
// core/stats_manager.cpp
// Unified stats system — tracks per-track/bike stats, global race stats,
// personal bests, and odometer data in a single JSON file
// ============================================================================
#include "stats_manager.h"
#include "achievement_manager.h"
#include "atomic_file_writer.h"
#include "finish_margin.h"
#include "plugin_data.h"
#include "plugin_utils.h"
#include "ui_config.h"
#include "../diagnostics/logger.h"
#include "../vendor/nlohmann/json.hpp"

#include <fstream>
#include <cstdio>
#include <algorithm>
#include <cmath>
#include <windows.h>

// What counts as finishing on fumes, as a fraction of tank capacity: the band
// from just above EMPTY up to this. Empty is literally zero litres, at the
// telemetry edge in stats_manager_telemetry.cpp, so the two fuel rows describe
// two different moments - crossing the line with a splash left, and the tank
// actually running out - and either can be earned without the other.
static constexpr float FUEL_FUMES_FRACTION = 0.01f;

StatsManager& StatsManager::getInstance() {
    static StatsManager instance;
    return instance;
}

std::string StatsManager::makeKey(const std::string& trackId, const std::string& bikeName) {
    return trackId + "|" + bikeName;
}

// Context
// ============================================================================

void StatsManager::setCurrentContext(const std::string& trackId, const std::string& bikeName,
                                      const std::string& category,
                                      const std::string& trackName) {
    m_currentTrackId = trackId;
    m_currentBikeName = bikeName;
    m_currentKey = makeKey(trackId, bikeName);
    m_currentCategory = category;

    // Update bike-to-category mapping (persisted for category-scoped PB lookups)
    if (!bikeName.empty() && !category.empty()) {
        m_bikeCategories[bikeName] = category;
        m_dirty = true;
    }

    // Learn this track's display name. Overwrites, rather than filling a gap:
    // a track renamed between releases should read by the name it has now.
    if (!trackId.empty() && !trackName.empty()) {
        std::string& stored = m_trackNames[trackId];
        if (stored != trackName) {
            stored = trackName;
            m_dirty = true;
        }
    }

    // Ensure entries exist for telemetry-rate lookups (avoids operator[] creating entries at 100Hz)
    m_trackBikeStats[m_currentKey];
    m_bikeOdometers[bikeName];
    m_distinctDirty = true;   // a first visit is a new key (Globetrotter / Collector)
    // A new track, bike or class is earned at the event, not at the next lap.
    AchievementManager::getInstance().onStatsChanged();

    // Reset odometer time tracking for new context
    m_hasLastOdometerUpdateTime = false;

    DEBUG_INFO_F("[StatsManager] Context set: %s", m_currentKey.c_str());
}

void StatsManager::clearCurrentContext() {
    m_currentTrackId.clear();
    m_currentBikeName.clear();
    m_currentKey.clear();
    m_currentCategory.clear();
    m_lastSessionType = -1;
    m_hasLastOdometerUpdateTime = false;
    consumeRaceLeft();   // the event is over: an unfinished race stays unfinished
}

void StatsManager::consumeRaceLeft() {
    if (m_raceLeftArmed) m_exploration.onRaceLeft();
    m_raceLeftArmed = false;
}

std::string StatsManager::getCurrentTrackId() const {
    return m_currentTrackId;
}

std::string StatsManager::getCurrentBikeName() const {
    return m_currentBikeName;
}

// ============================================================================
// Recording
// ============================================================================

void StatsManager::recordLap(int lapTime, int sector1, int sector2, int sector3, int sector4,
                              bool isValid, bool isFastestLap, bool isRace) {
    if (m_currentKey.empty()) return;

    // Snapshot current lap telemetry to "last completed lap" before resetting
    m_lastLapTimeMs = lapTime;
    m_lastLapCrashes = m_curLapCrashes;
    m_lastLapGearShifts = m_curLapGearShifts;
    m_lastLapTopSpeedMs = m_curLapTopSpeedMs;
    m_lastLapPenaltyCount = m_curLapPenaltyCount;
    m_lastLapPenaltyTimeMs = m_curLapPenaltyTimeMs;
    m_lastLapDistance = m_curLapDistance;
    m_hasLastLapData = true;

    // Reset current lap accumulators
    m_curLapStartTime = std::chrono::steady_clock::now();
    m_curLapPausedMs = 0;
    m_curLapCrashes = 0;
    m_curLapGearShifts = 0;
    m_curLapTopSpeedMs = 0.0f;
    m_curLapPenaltyCount = 0;
    m_curLapPenaltyTimeMs = 0;
    m_curLapDistance = 0.0;

    auto& stats = m_trackBikeStats[m_currentKey];
    m_globalTotalsDirty = true;

    if (isValid && lapTime > 0) {
        // THE FIRST valid lap on this track+bike is what makes it count toward
        // Globetrotter, Collector and Class Act -- those read keys with laps on
        // them, not keys that exist. Nothing else dirties the distinct cache
        // here: setCurrentContext dirties it when the KEY appears, and at that
        // moment the key has no laps, so without this the count stayed one
        // event behind (a track rode, left and came back before it was
        // counted). Only on the 0 -> 1 edge, so a lap is not an O(records) walk.
        if (stats.validLaps == 0) m_distinctDirty = true;
        stats.validLaps++;
        m_sessionLaps++;
        if (m_sessionLaps > m_globalStats.maxSessionLaps) m_globalStats.maxSessionLaps = m_sessionLaps;
        stats.totalLapTimeMs += lapTime;

        // Update best lap
        if (stats.bestLapTimeMs < 0 || lapTime < stats.bestLapTimeMs) {
            stats.bestLapTimeMs = lapTime;
        }

        // Update best sectors
        if (sector1 > 0 && (stats.bestSector1Ms < 0 || sector1 < stats.bestSector1Ms))
            stats.bestSector1Ms = sector1;
        if (sector2 > 0 && (stats.bestSector2Ms < 0 || sector2 < stats.bestSector2Ms))
            stats.bestSector2Ms = sector2;
        if (sector3 > 0 && (stats.bestSector3Ms < 0 || sector3 < stats.bestSector3Ms))
            stats.bestSector3Ms = sector3;
        if (sector4 > 0 && (stats.bestSector4Ms < 0 || sector4 < stats.bestSector4Ms))
            stats.bestSector4Ms = sector4;

        // Session best
        if (m_sessionBestLapMs < 0 || lapTime < m_sessionBestLapMs) {
            m_sessionBestLapMs = lapTime;
        }
    }

    // Track whether player holds fastest lap — only credited at race finish
    if (isFastestLap && isRace) {
        m_playerHasFastestLapInRace = true;
    }

    // Spotless: a lap with nothing on its sheet. Read from the snapshot at the
    // top of this function, which is the lap that just closed - the current-lap
    // accumulators have already been reset by here. An invalid lap still counts
    // as a lap; only a crash or a penalty breaks the run.
    m_exploration.onLapCompleted(m_lastLapCrashes == 0 && m_lastLapPenaltyCount == 0);
    if (isValid) m_exploration.onLapTime(lapTime);   // palindrome, deja vu, metronome
    m_dirty = true;
    AchievementManager::getInstance().onStatsChanged();
}

void StatsManager::onRaceStart(int starters) {
    // Everything a single RACE owns, as opposed to the session around it. The
    // session counters (crashes, penalties, laps) are deliberately NOT reset:
    // "clean race" has always meant no crash since the session began, and a
    // restart does not un-crash the one before it.
    //
    // AND THE RAGE QUIT ARM IS LEFT ALONE, deliberately. A restart pulls every
    // rider off the track, so RunDeinit arms it for a race nobody chose to
    // leave; consuming it here credited the row to a player who went on to
    // finish the restarted race. The arm needs no help - a finish clears it
    // (tryRecordRaceFinish) and a real session change consumes it - so the
    // question "did they come back?" answers itself either way.
    m_raceFinishRecorded = false;
    m_pendingMarginPosition = 0;
    m_playerHasFastestLapInRace = false;
    m_exploration.onGateDrop(starters);
}

void StatsManager::recordSessionStart(int sessionType) {
    if (m_currentKey.empty()) {
        DEBUG_WARN("[StatsManager] recordSessionStart called before setCurrentContext — session stats may be incomplete");
    }
    // Only reset session stats when the session type actually changes
    // (not on pit stop re-entries within the same session)
    bool sessionChanged = (sessionType != m_lastSessionType);
    m_lastSessionType = sessionType;

    // The exploration scratch follows the same rule: a pit visit must not
    // restart Steady Hands' clock or forget lap one's position (Charger, Wire
    // to Wire). The clock reading (Night Owl, the day) happens on every run.
    if (sessionChanged) m_exploration.onSessionStart();
    else                m_exploration.onRunStart();

    if (sessionChanged) {
        m_sessionLaps = 0;
        m_sessionBestLapMs = -1;
        m_sessionCrashes = 0;
        m_sessionGearShifts = 0;
        m_sessionPenaltyCount = 0;
        m_sessionPenaltyTimeMs = 0;
        m_sessionTopSpeedMs = 0.0f;
        m_sessionTripDistance = 0.0;
        m_cachedSessionDurationMs = 0;
        m_isPaused = false;
        m_hasLastLapData = false;
        // Favorite Spot's run. A place on the centreline only means anything
        // against the track it was measured on, and a session change is where
        // the track can have moved under it.
        m_lastCrashTrackPos = -1.0f;
        m_sameSpotCrashRun = 0;
    }

    // Always reset time tracking on session (re-)entry to prevent double-counting
    // time across pit stop cycles
    m_sessionStartTime = std::chrono::steady_clock::now();
    m_totalPausedMs = 0;

    // Only reset race finish tracking on actual session changes.
    if (sessionChanged) {
        consumeRaceLeft();
        m_raceFinishRecorded = false;
        m_pendingMarginPosition = 0;
        m_playerHasFastestLapInRace = false;
    }

    m_sessionActive = true;
    m_wasCrashed = false;
    m_lastGear = -1;
    m_hasLastOdometerUpdateTime = false;

    // The tank level is not continuous across a trip to the pits: the fuel load
    // is part of the setup, so coming back out with LESS in the tank differences
    // as a tank's worth of burn (more re-references harmlessly). Every track
    // entry drops the reference, not just a new bike - setTankCapacity() only
    // fires at EventInit, which a pit stop does not. Costs one tick's burn.
    m_hasLastFuel = false;

    // Reset per-lap tracking
    m_curLapStartTime = std::chrono::steady_clock::now();
    m_curLapPausedMs = 0;
    m_curLapCrashes = 0;
    m_curLapGearShifts = 0;
    m_curLapTopSpeedMs = 0.0f;
    m_curLapPenaltyCount = 0;
    m_curLapPenaltyTimeMs = 0;
    m_curLapDistance = 0.0;

    if (!m_currentKey.empty()) {
        auto& stats = m_trackBikeStats[m_currentKey];
        if (stats.firstSessionTimestamp == 0) {
            stats.firstSessionTimestamp = std::time(nullptr);
        }
        stats.lastSessionTimestamp = std::time(nullptr);
        m_dirty = true;
    }
}

void StatsManager::recordSessionEnd() {
    if (!m_sessionActive) return;

    // If still paused at session end, accumulate the final pause segment
    if (m_isPaused) {
        auto pauseElapsed = std::chrono::steady_clock::now() - m_pauseStartTime;
        m_totalPausedMs += std::chrono::duration_cast<std::chrono::milliseconds>(pauseElapsed).count();
        m_isPaused = false;
    }
    auto elapsed = std::chrono::steady_clock::now() - m_sessionStartTime;
    int64_t rawDuration = std::chrono::duration_cast<std::chrono::milliseconds>(elapsed).count() - m_totalPausedMs;
    m_cachedSessionDurationMs = rawDuration > 0 ? rawDuration : 0;
    m_sessionActive = false;

    if (!m_currentKey.empty()) {
        m_trackBikeStats[m_currentKey].totalTimeOnTrackMs += m_cachedSessionDurationMs;
        m_globalTotalsDirty = true;
        m_dirty = true;
        AchievementManager::getInstance().onStatsChanged();
    }

    m_unsavedDistance = 0.0;
    m_hasLastOdometerUpdateTime = false;
}

void StatsManager::notifyPause() {
    if (!m_sessionActive || m_isPaused) return;
    m_isPaused = true;
    m_pauseStartTime = std::chrono::steady_clock::now();
}

void StatsManager::notifyResume() {
    if (!m_sessionActive || !m_isPaused) return;
    int64_t pauseElapsedMs = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now() - m_pauseStartTime).count();
    m_totalPausedMs += pauseElapsedMs;
    m_curLapPausedMs += pauseElapsedMs;
    m_isPaused = false;
}

// The finishing margin, once more, for the two rows that need it when the lap
// logs were still filling at the flag. Everything else the finish moved has
// already counted; these are marks, so a retry that finds the same answer costs
// nothing. Cleared either way -- one retry, not a standing request.
void StatsManager::retryFinishMargin(const PluginData& pd) {
    const int position = m_pendingMarginPosition;
    if (position == 0) return;
    m_pendingMarginPosition = 0;
    const int playerRaceNum = pd.getPlayerRaceNum();
    const auto& classOrder = pd.getClassificationOrder();
    const StandingsData* own = pd.getStanding(playerRaceNum);
    const std::deque<LapLogEntry>* ownLaps = own ? pd.getLapLog(playerRaceNum) : nullptr;
    if (!ownLaps) return;
    const int otherNum = (position == 1)
        ? (classOrder.size() > 1 ? classOrder[1] : -1)
        : (!classOrder.empty() ? classOrder[0] : -1);
    if (otherNum < 0) return;
    const StandingsData* other = pd.getStanding(otherNum);
    const std::deque<LapLogEntry>* otherLaps = other ? pd.getLapLog(otherNum) : nullptr;
    if (!otherLaps) return;
    const int margin = (position == 1)
        ? FinishMargin::marginMs(*ownLaps, own->numLaps, *otherLaps, other->numLaps)
        : FinishMargin::marginMs(*otherLaps, other->numLaps, *ownLaps, own->numLaps);
    if (margin < 0) return;
    if (m_exploration.onFinishMargin(position, position == 1 ? margin : -1,
                                     position == 2 ? margin : -1)) {
        m_dirty = true;
        AchievementManager::getInstance().onStatsChanged();
    }
}

void StatsManager::tryRecordRaceFinish(const PluginData& pd, bool final) {
    if (m_raceFinishRecorded) {
        // The race is in. The MARGIN and the last-lap pass may not be: both wait
        // on callbacks that can arrive after the classification settles. See
        // retryFinishMargin and ExplorationStats::retryLastGasp.
        if (final) {
            retryFinishMargin(pd);
            if (m_exploration.retryLastGasp()) {
                m_dirty = true;
                AchievementManager::getInstance().onStatsChanged();
            }
        }
        return;
    }
    if (!pd.isRaceSession()) return;

    int playerRaceNum = pd.getPlayerRaceNum();
    if (playerRaceNum < 0) return;

    // finishTime defaults to -1 and only transitions to >= 0 when PluginData
    // detects the rider completed enough laps (via isRiderFinished). This safely
    // excludes disconnects, DNFs, mid-race quits, and pit exits.
    const StandingsData* standing = pd.getStanding(playerRaceNum);
    if (!standing || standing->finishTime < 0) return;

    const auto& classOrder = pd.getClassificationOrder();
    for (int i = 0; i < static_cast<int>(classOrder.size()); ++i) {
        if (classOrder[i] == playerRaceNum) {
            int position = i + 1;  // 1-indexed
            m_raceFinishRecorded = true;
            m_raceLeftArmed = false;
            m_globalStats.raceCount++;
            if (position == 1) m_globalStats.firstPositions++;
            else if (position == 2) m_globalStats.secondPositions++;
            else if (position == 3) m_globalStats.thirdPositions++;
            if (m_playerHasFastestLapInRace) m_globalStats.fastestLapCount++;
            // Clean = no crash since this session began (m_sessionCrashes resets
            // on the session-type change, so practice spills are not counted
            // against the race). Only meaningful with GAME_HAS_CRASH_STATE; the
            // catalogue hides the row elsewhere, so an always-zero count is inert.
            if (m_sessionCrashes == 0) m_globalStats.cleanRaceCount++;
            // The penalty-free run: a finish with no penalty this session
            // extends it, one with a penalty ends it. The row reads the best.
            if (m_sessionPenaltyCount == 0) {
                m_globalStats.penaltyFreeStreak++;
                if (m_globalStats.penaltyFreeStreak > m_globalStats.bestPenaltyFreeStreak) {
                    m_globalStats.bestPenaltyFreeStreak = m_globalStats.penaltyFreeStreak;
                }
            } else {
                m_globalStats.penaltyFreeStreak = 0;
            }
            if (pd.getSessionData().conditions == static_cast<int>(Unified::WeatherCondition::Rainy)) {
                m_globalStats.rainRaceCount++;
            }
            if (static_cast<int>(pd.getRaceEntries().size()) >= BIG_GRID_ENTRIES) {
                m_globalStats.bigGridRaceCount++;
            }
            // The race-shaped signals: the margin over second, the field's laps
            // down, the player's own laps down.
            {
                // Photo Finish, and So Close. Differenced from the two riders'
                // lap logs, not read from StandingsData::gap - finish_margin.h
                // has the why. marginMs is (second argument's time - first
                // argument's time), so the winner goes first in both and each
                // margin comes out POSITIVE: only the pairing differs.
                int gapToSecond = -1;   // P1: how far the runner-up finished behind us
                int gapToWinner = -1;   // P2: how far we finished behind the winner
                const std::deque<LapLogEntry>* ownLaps = pd.getLapLog(playerRaceNum);
                if (position == 1 && classOrder.size() > 1) {
                    const StandingsData* second = pd.getStanding(classOrder[1]);
                    const std::deque<LapLogEntry>* secondLaps = second ? pd.getLapLog(classOrder[1]) : nullptr;
                    if (second && ownLaps && secondLaps) {
                        gapToSecond = FinishMargin::marginMs(*ownLaps, standing->numLaps,
                                                             *secondLaps, second->numLaps);
                    }
                } else if (position == 2 && !classOrder.empty()) {
                    const StandingsData* winner = pd.getStanding(classOrder[0]);
                    const std::deque<LapLogEntry>* winnerLaps = winner ? pd.getLapLog(classOrder[0]) : nullptr;
                    if (winner && ownLaps && winnerLaps) {
                        gapToWinner = FinishMargin::marginMs(*winnerLaps, winner->numLaps,
                                                             *ownLaps, standing->numLaps);
                    }
                }
                // Lapped the Field. Only riders who actually raced can be
                // lapped: a DNS never left the grid and a rider who retired or
                // was disqualified stops being classified against the leader,
                // so counting them against you means one quitter in a public
                // lobby vetoes the row no matter how far ahead you finished.
                int racedOthers = 0;
                bool everyOtherLapped = true;
                // Survivor counts the same rows from the other side: who took
                // the start, and how many of them did not see the end of it.
                int starters = 0;
                int retired = 0;
                for (int other : classOrder) {
                    const StandingsData* so = pd.getStanding(other);
                    if (!so) continue;
                    const bool didNotStart = (so->state == static_cast<int>(Unified::EntryState::DNS));
                    if (!didNotStart) ++starters;
                    if (so->state == static_cast<int>(Unified::EntryState::Retired) ||
                        so->state == static_cast<int>(Unified::EntryState::DSQ)) {
                        ++retired;
                    }
                    if (other == playerRaceNum) continue;
                    if (everyOtherLapped) {
                        if (so->state != static_cast<int>(Unified::EntryState::Racing)) continue;
                        ++racedOthers;
                        if (so->gapLaps < 1) everyOtherLapped = false;
                    }
                }
                if (racedOthers == 0) everyOtherLapped = false;
                // Running on Fumes: what was left in the tank at the flag. The
                // capacity gate is what makes an unknown tank read as "cannot
                // be known" rather than "empty" - with fuel consumption off the
                // level never falls and this never fires, which is correct.
                // STRICTLY ABOVE EMPTY: zero is Long Walk Home's moment, and the
                // two are meant to be different ones. Without the lower bound a
                // tank that ran dry on the last straight and coasted over the line
                // fired both, which is the overlap that separating them removed.
                if (m_hasLastFuel && m_tankCapacityL > 0.0f && m_lastFuel > 0.0f &&
                    m_lastFuel <= m_tankCapacityL * FUEL_FUMES_FRACTION) {
                    m_exploration.onFinishedOnFumes();
                }
                // Field by field, not a designated initializer: every shipping
                // target is CXX_STANDARD 17 with /permissive- /WX, and MSVC
                // rejects C++20 designated init outright (C7555). GCC takes it
                // as an extension in C++17, so the mingw gates stay green and
                // only the Windows build breaks - which is why this is spelled
                // out rather than left to be caught downstream.
                ExplorationStats::RaceFinish race;
                race.position = position;
                race.gapToSecondMs = gapToSecond;
                race.gapToWinnerMs = gapToWinner;
                race.everyOtherRiderLapped = everyOtherLapped;
                race.ownGapLaps = standing->gapLaps;
                race.starters = starters;
                race.retired = retired;
                race.lapsAtFinish = standing->numLaps;
                race.hadFastestLap = m_playerHasFastestLapInRace;
                m_exploration.onRaceFinished(race);
                // THE MARGIN ROWS NEED THE LAP LOGS, and the classification can
                // settle before the last RaceLap reaches them: the two come from
                // different callbacks, and "settled" only asks that every racing
                // rider has a finishTime. Everything else above has counted, so
                // only the margin is owed - remembered here and retried once at
                // RunDeinit, by which point the logs are complete.
                m_pendingMarginPosition =
                    (position == 1 && gapToSecond < 0) || (position == 2 && gapToWinner < 0)
                        ? position : 0;
            }
            m_dirty = true;
            AchievementManager::getInstance().onStatsChanged();
            return;
        }
    }
}

void StatsManager::clearPlayerFastestLap() {
    m_playerHasFastestLapInRace = false;
}

void StatsManager::recordPenalty(int penaltyTimeMs, bool isRace) {
    m_sessionPenaltyCount++;
    m_curLapPenaltyCount++;

    if (penaltyTimeMs > 0) {
        m_sessionPenaltyTimeMs += penaltyTimeMs;
        m_curLapPenaltyTimeMs += penaltyTimeMs;
    }

    if (!m_currentKey.empty()) {
        m_trackBikeStats[m_currentKey].penaltyCount++;
        if (penaltyTimeMs > 0) {
            m_trackBikeStats[m_currentKey].penaltyTimeMs += penaltyTimeMs;
        }
        m_globalTotalsDirty = true;
        m_dirty = true;
    }

    if (isRace && penaltyTimeMs > 0) {
        m_globalStats.penaltyTimeMs += penaltyTimeMs;
        m_dirty = true;  // persisted stat (global["penaltyTimeMs"]); mutated even when m_currentKey is empty
    }
    AchievementManager::getInstance().onStatsChanged();
}


void StatsManager::recordFmxTrick(const FmxTrickSample& trick) {
    // Finite-guard the two floats at the write, like every persisted float here.
    const float duration = std::isfinite(trick.durationSec) && trick.durationSec > 0.0f ? trick.durationSec : 0.0f;
    const float distance = std::isfinite(trick.distanceM) && trick.distanceM > 0.0f ? trick.distanceM : 0.0f;
    m_fmx.tricksLanded++;
    if (trick.backflip) m_fmx.backflips++;
    if (trick.frontflip) m_fmx.frontflips++;
    if (trick.whip) m_fmx.whips++;
    if (trick.scrub) m_fmx.scrubs++;
    if (trick.oppo) m_fmx.oppos++;
    if (trick.turnDown) m_fmx.turnDowns++;
    if (trick.endo) {
        m_fmx.endos++;
        if (duration > m_fmx.longestEndoSec) m_fmx.longestEndoSec = duration;
    }
    if (trick.wheelie) {
        if (duration > m_fmx.longestWheelieSec) m_fmx.longestWheelieSec = duration;
        m_fmx.wheelieDistanceM += distance;
    }
    if (trick.kind && trick.kind[0]) m_fmx.kinds.insert(trick.kind);
    m_exploration.onTrickTime(trick.shred, duration);   // Tyre Shredder
    m_dirty = true;
}

void StatsManager::recordFmxChainBanked(int chainScore) {
    if (chainScore > 0) {
        m_fmx.totalScore += chainScore;
        if (chainScore > m_fmx.bestChainScore) m_fmx.bestChainScore = chainScore;
    }
    m_dirty = true;
    AchievementManager::getInstance().onStatsChanged();
}

// ============================================================================
// Query — current track+bike
// ============================================================================

const TrackBikeStats* StatsManager::getTrackBikeStats() const {
    if (m_currentKey.empty()) return nullptr;
    auto it = m_trackBikeStats.find(m_currentKey);
    return it != m_trackBikeStats.end() ? &it->second : nullptr;
}

int64_t StatsManager::getCurrentTotalTimeOnTrackMs() const {
    const auto* stats = getTrackBikeStats();
    int64_t persisted = stats ? stats->totalTimeOnTrackMs : 0;
    // Only add live session duration while session is active.
    // After recordSessionEnd(), session time is already baked into totalTimeOnTrackMs
    // and m_sessionActive is false, so this correctly avoids double-counting.
    if (m_sessionActive) persisted += getSessionDurationMs();
    return persisted;
}

double StatsManager::getCurrentTotalDistanceM() const {
    const auto* stats = getTrackBikeStats();
    // totalDistanceM is accumulated in real-time during updateTelemetry(), no need to add session
    return stats ? stats->totalDistanceM : 0.0;
}

const StatsPersonalBestData* StatsManager::getPersonalBest(std::string* outBikeName) const {
    if (m_currentKey.empty()) return nullptr;

    // If PB scope is CATEGORY and we have a category, scan all bikes in that category
    if (UiConfig::getInstance().getPBScope() == PBScope::CATEGORY && !m_currentCategory.empty()) {
        return getPersonalBestForCategory(m_currentTrackId, m_currentCategory, outBikeName);
    }

    if (outBikeName) *outBikeName = m_currentBikeName;
    auto it = m_personalBests.find(m_currentKey);
    return it != m_personalBests.end() ? &it->second : nullptr;
}

PersonalBestUpdate StatsManager::updatePersonalBest(const StatsPersonalBestData& entry) {
    PersonalBestUpdate result;
    if (!entry.isValid()) return result;
    if (m_currentKey.empty()) return result;

    // Scope-aware comparison BEFORE the write — see PersonalBestUpdate. Reading it
    // after would compare the lap against itself.
    const StatsPersonalBestData* scopedBest = getPersonalBest();
    result.beatsScopedBest = (scopedBest == nullptr) || (entry.lapTime < scopedBest->lapTime);

    auto it = m_personalBests.find(m_currentKey);
    if (it != m_personalBests.end() && it->second.lapTime <= entry.lapTime) {
        return result;  // Existing PB for this bike is faster
    }

    if (it != m_personalBests.end() && it->second.lapTime - entry.lapTime >= PB_LEAP_MS) {
        m_globalStats.pbLeaps++;   // Leap Forward: a whole second off an existing PB
    }
    m_personalBests[m_currentKey] = entry;
    m_globalStats.pbCount++;
    m_dirty = true;   // deferred: persisted on leave-track (RunStop/RunDeinit). A PB is set at
                      // lap completion (start/finish) — on track — and we never write on track.
    result.stored = true;
    AchievementManager::getInstance().onStatsChanged();
    return result;
}

// Query — by explicit track+bike
const StatsPersonalBestData* StatsManager::getPersonalBest(const std::string& trackId,
                                                            const std::string& bikeName,
                                                            std::string* outBikeName) const {
    // If PB scope is CATEGORY, look up the bike's category and scan all bikes in it
    if (UiConfig::getInstance().getPBScope() == PBScope::CATEGORY) {
        auto catIt = m_bikeCategories.find(bikeName);
        if (catIt != m_bikeCategories.end() && !catIt->second.empty()) {
            return getPersonalBestForCategory(trackId, catIt->second, outBikeName);
        }
    }

    if (outBikeName) *outBikeName = bikeName;
    std::string key = makeKey(trackId, bikeName);
    auto it = m_personalBests.find(key);
    return it != m_personalBests.end() ? &it->second : nullptr;
}

const StatsPersonalBestData* StatsManager::getPersonalBestForCategory(
    const std::string& trackId, const std::string& category, std::string* outBikeName) const {
    // Scan all personal bests for this track, finding the fastest among bikes in the target category
    // NOTE: Returns pointer to m_cachedCategoryPB — only valid until the next getPersonalBest() call.
    // All current callers copy fields immediately, so this is safe.
    const StatsPersonalBestData* best = nullptr;
    std::string bestBikeName;
    std::string prefix = trackId + "|";

    for (const auto& [key, pb] : m_personalBests) {
        if (!pb.isValid()) continue;
        if (key.compare(0, prefix.size(), prefix) != 0) continue;

        // Extract bike name from key (format: "trackId|bikeName")
        std::string bikeName = key.substr(prefix.size());
        auto catIt = m_bikeCategories.find(bikeName);
        if (catIt == m_bikeCategories.end() || catIt->second != category) continue;

        if (!best || pb.lapTime < best->lapTime) {
            best = &pb;
            bestBikeName = bikeName;
        }
    }

    if (best) {
        m_cachedCategoryPB = *best;
        if (outBikeName) *outBikeName = bestBikeName;
        return &m_cachedCategoryPB;
    }
    return nullptr;
}

PersonalBestUpdate StatsManager::updatePersonalBest(const std::string& trackId, const std::string& bikeName,
                                                     const StatsPersonalBestData& entry) {
    PersonalBestUpdate result;
    if (!entry.isValid()) return result;

    // Scope-aware comparison BEFORE the write — see PersonalBestUpdate. Under
    // PBScope::CATEGORY this is the fastest lap across every bike in the class, which
    // is the reference the TimingHud "Alltime" row displays; under PBScope::BIKE it is
    // this bike's own PB and the two flags agree. Only lapTime is read, so the pointer
    // into getPersonalBestForCategory's scratch buffer is consumed immediately.
    const StatsPersonalBestData* scopedBest = getPersonalBest(trackId, bikeName);
    result.beatsScopedBest = (scopedBest == nullptr) || (entry.lapTime < scopedBest->lapTime);

    std::string key = makeKey(trackId, bikeName);
    auto it = m_personalBests.find(key);
    if (it != m_personalBests.end() && it->second.lapTime <= entry.lapTime) {
        return result;
    }
    if (it != m_personalBests.end() && it->second.lapTime - entry.lapTime >= PB_LEAP_MS) {
        m_globalStats.pbLeaps++;
    }
    m_personalBests[key] = entry;
    m_globalStats.pbCount++;
    m_dirty = true;   // deferred: persisted on leave-track (RunStop/RunDeinit) — never on track.
    result.stored = true;
    AchievementManager::getInstance().onStatsChanged();
    return result;
}

// ============================================================================
// Query — session
// ============================================================================

int StatsManager::getSessionLaps() const {
    return m_sessionLaps;
}

int StatsManager::getSessionBestLapMs() const {
    return m_sessionBestLapMs;
}

int StatsManager::getSessionCrashes() const {
    return m_sessionCrashes;
}

int StatsManager::getSessionGearShifts() const {
    return m_sessionGearShifts;
}

float StatsManager::getSessionTopSpeedMs() const {
    return m_sessionTopSpeedMs;
}

int StatsManager::getSessionPenaltyCount() const {
    return m_sessionPenaltyCount;
}

int64_t StatsManager::getSessionPenaltyTimeMs() const {
    return m_sessionPenaltyTimeMs;
}

double StatsManager::getSessionTripDistance() const {
    return m_sessionTripDistance;
}

// ========================================================================
// Query — current lap in progress (live, accumulating)
// ========================================================================

int StatsManager::getCurrentLapCrashes() const { return m_curLapCrashes; }
int StatsManager::getCurrentLapGearShifts() const { return m_curLapGearShifts; }
float StatsManager::getCurrentLapTopSpeedMs() const { return m_curLapTopSpeedMs; }
int StatsManager::getCurrentLapPenaltyCount() const { return m_curLapPenaltyCount; }
int64_t StatsManager::getCurrentLapPenaltyTimeMs() const { return m_curLapPenaltyTimeMs; }
double StatsManager::getCurrentLapDistance() const { return m_curLapDistance; }

int64_t StatsManager::getCurrentLapElapsedMs() const {
    if (!m_sessionActive) return 0;
    auto now = std::chrono::steady_clock::now();
    int64_t pausedMs = m_curLapPausedMs;
    if (m_isPaused) {
        pausedMs += std::chrono::duration_cast<std::chrono::milliseconds>(now - m_pauseStartTime).count();
    }
    int64_t elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(now - m_curLapStartTime).count() - pausedMs;
    return elapsed > 0 ? elapsed : 0;
}

// ========================================================================
// Query — last completed lap
// ========================================================================

int StatsManager::getLastLapTimeMs() const { return m_lastLapTimeMs; }
int StatsManager::getLastLapCrashes() const { return m_lastLapCrashes; }
int StatsManager::getLastLapGearShifts() const { return m_lastLapGearShifts; }
float StatsManager::getLastLapTopSpeedMs() const { return m_lastLapTopSpeedMs; }
int StatsManager::getLastLapPenaltyCount() const { return m_lastLapPenaltyCount; }
int64_t StatsManager::getLastLapPenaltyTimeMs() const { return m_lastLapPenaltyTimeMs; }
double StatsManager::getLastLapDistance() const { return m_lastLapDistance; }
bool StatsManager::hasLastLapData() const { return m_hasLastLapData; }

int64_t StatsManager::getSessionDurationMs() const {
    if (!m_sessionActive) return m_cachedSessionDurationMs;
    auto now = std::chrono::steady_clock::now();
    int64_t pausedMs = m_totalPausedMs;
    if (m_isPaused) {
        pausedMs += std::chrono::duration_cast<std::chrono::milliseconds>(now - m_pauseStartTime).count();
    }
    auto elapsed = now - m_sessionStartTime;
    int64_t result = std::chrono::duration_cast<std::chrono::milliseconds>(elapsed).count() - pausedMs;
    return result > 0 ? result : 0;
}

// ============================================================================
// Clear
// ============================================================================

bool StatsManager::clearEntry(const std::string& trackId, const std::string& bikeName) {
    std::string key = makeKey(trackId, bikeName);

    auto tbIt = m_trackBikeStats.find(key);
    auto pbIt = m_personalBests.find(key);
    if (tbIt == m_trackBikeStats.end() && pbIt == m_personalBests.end()) {
        return false;
    }
    if (tbIt != m_trackBikeStats.end()) m_trackBikeStats.erase(tbIt);
    if (pbIt != m_personalBests.end()) m_personalBests.erase(pbIt);
    m_globalTotalsDirty = true;
    m_distinctDirty = true;
    m_dirty = true;

    // Re-create entry if we just cleared the active session's track+bike combo,
    // so updateTelemetry() and recordSessionEnd() don't silently fail or
    // re-create a partial entry. Also reset session transients so HUD doesn't
    // show stale data from before the clear.
    if (m_sessionActive && key == m_currentKey) {
        m_trackBikeStats[m_currentKey];

        // Reset session transients (mirrors clearAll behavior)
        m_sessionLaps = 0;
        m_sessionBestLapMs = -1;
        m_sessionCrashes = 0;
        m_sessionGearShifts = 0;
        m_sessionTopSpeedMs = 0.0f;
        m_sessionPenaltyCount = 0;
        m_sessionPenaltyTimeMs = 0;
        m_sessionTripDistance = 0.0;

        // Reset per-lap transients
        m_curLapCrashes = 0;
        m_curLapGearShifts = 0;
        m_curLapTopSpeedMs = 0.0f;
        m_curLapPenaltyCount = 0;
        m_curLapPenaltyTimeMs = 0;
        m_curLapDistance = 0.0;
        m_curLapPausedMs = 0;
        m_lastLapTimeMs = -1;
        m_lastLapCrashes = 0;
        m_lastLapGearShifts = 0;
        m_lastLapTopSpeedMs = 0.0f;
        m_lastLapPenaltyCount = 0;
        m_lastLapPenaltyTimeMs = 0;
        m_lastLapDistance = 0.0;
        m_hasLastLapData = false;

        // Reset session timer so duration doesn't include pre-clear time
        m_sessionStartTime = std::chrono::steady_clock::now();
        m_cachedSessionDurationMs = 0;
        m_totalPausedMs = 0;
        m_unsavedDistance = 0.0;
        m_wasCrashed = false;
        m_lastGear = -1;
    }

    save();
    return true;
}

void StatsManager::clearAll() {
    wipe(/*keepLapRecords=*/false);
    save();
}

// The trade. PERSONAL BESTS AND TRACK NAMES SURVIVE (see the header): a lap
// time is a record of something that happened on a track, and the prestige
// button says it costs the ladder, not the lap book.
//
// THE SAVE IS AT THE END, AND THERE IS ONLY ONE. The first cut called
// clearAll() and put the records back afterwards, which meant the file spent
// the gap between the two saves with every PB gone -- a crash to desktop in
// that window would have taken the one thing this promised to keep.
bool StatsManager::prestige() {
    if (!AchievementManager::getInstance().isPrestigeAvailable()) {
        DEBUG_WARN("[StatsManager] prestige() refused: the Platinum Sweep is not earned");
        return false;
    }
    wipe(/*keepLapRecords=*/true);
    // pbCount went with the rest, and the stored bests must NOT put it back --
    // see the load path's floor, which is why that floor now only applies to a
    // file written before the counter existed.
    //
    // THE BIKE->CLASS MAP STAYS WITH THE BESTS, and has to: the default
    // PBScope::CATEGORY reads it to find the class best across bikes
    // (getPersonalBest). Wiped, every kept PB fell back to its own bike's
    // time, so the first lap of a class fired the green ALL-TIME PB notice
    // against a reference that was not the class best -- precisely the bug
    // pb_scope_test.cpp exists to pin. It is a fact about bikes, not progress,
    // and it grants nothing back: Class Act counts classes among bikes with
    // LAPS on them, and the lap records are what this just cleared.
    ++m_prestige;
    m_dirty = true;
    DEBUG_INFO_F("[StatsManager] Prestige %d taken: counters and achievements cleared, %zu personal bests kept",
                 m_prestige, m_personalBests.size());
    save();
    return true;
}

void StatsManager::wipe(bool keepLapRecords) {
    m_trackBikeStats.clear();
    m_bikeOdometers.clear();
    if (!keepLapRecords) {
        m_personalBests.clear();
        m_trackNames.clear();
        m_bikeCategories.clear();
    }
    m_globalStats = GlobalStats();
    m_fmx = FmxLifetimeStats();
    m_exploration.clear();
    m_globalTotalsDirty = true;
    m_distinctDirty = true;
    m_dirty = true;
    // The achievements are facts about these numbers: gone with them.
    AchievementManager::getInstance().clearAll();

    // Reset session transients so HUD doesn't show stale data
    m_sessionLaps = 0;
    m_sessionBestLapMs = -1;
    m_sessionCrashes = 0;
    m_sessionGearShifts = 0;
    m_sessionTopSpeedMs = 0.0f;
    m_sessionPenaltyCount = 0;
    m_sessionPenaltyTimeMs = 0;
    m_sessionTripDistance = 0.0;

    // Reset per-lap transients
    m_curLapCrashes = 0;
    m_curLapGearShifts = 0;
    m_curLapTopSpeedMs = 0.0f;
    m_curLapPenaltyCount = 0;
    m_curLapPenaltyTimeMs = 0;
    m_curLapDistance = 0.0;
    m_curLapPausedMs = 0;
    m_lastLapTimeMs = -1;
    m_lastLapCrashes = 0;
    m_lastLapGearShifts = 0;
    m_lastLapTopSpeedMs = 0.0f;
    m_lastLapPenaltyCount = 0;
    m_lastLapPenaltyTimeMs = 0;
    m_lastLapDistance = 0.0;
    m_hasLastLapData = false;

    // Reset session timer so duration doesn't include pre-clear time
    if (m_sessionActive) {
        m_sessionStartTime = std::chrono::steady_clock::now();
    }
    m_cachedSessionDurationMs = 0;
    m_totalPausedMs = 0;
    m_isPaused = false;
    m_unsavedDistance = 0.0;
    // The other two coalescing buffers beside it. Litres and seconds measured
    // before the wipe are pre-prestige riding, and flushing them afterwards
    // would open the new ladder with them.
    m_unflushedFuelL = 0.0;
    m_roostPendingSec = 0.0;
    m_proximitySinceFlushSec = 0.0;
    m_hasLastProximityTime = false;
    m_wasCrashed = false;
    m_lastGear = -1;
    m_raceFinishRecorded = false;
    m_pendingMarginPosition = 0;
    m_playerHasFastestLapInRace = false;

    // Re-create entry if a session is active so updateTelemetry() keeps working
    if (m_sessionActive && !m_currentKey.empty()) {
        m_trackBikeStats[m_currentKey];
    }
}
