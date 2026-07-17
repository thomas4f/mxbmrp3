// ============================================================================
// core/stats_manager.cpp
// Unified stats system — tracks per-track/bike stats, global race stats,
// personal bests, and odometer data in a single JSON file
// ============================================================================
#include "stats_manager.h"
#include "atomic_file_writer.h"
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

// Minimum speed to count as movement (filters out noise when stationary)
static constexpr float MIN_MOVEMENT_SPEED_MS = 0.1f;  // ~0.36 km/h

StatsManager& StatsManager::getInstance() {
    static StatsManager instance;
    return instance;
}

#if defined(MXBMRP3_TEST_BUILD)
// Injectable simulated clock for the headless odometer test. -1 = real
// steady_clock (production path). Never compiled into a shipping DLL.
static long long s_statsTestNowUs = -1;
void StatsManager::testSetNowUs(long long us) { s_statsTestNowUs = us; }
#endif

std::chrono::steady_clock::time_point StatsManager::odometerNow() {
#if defined(MXBMRP3_TEST_BUILD)
    if (s_statsTestNowUs >= 0) {
        return std::chrono::steady_clock::time_point(
            std::chrono::duration_cast<std::chrono::steady_clock::duration>(
                std::chrono::microseconds(s_statsTestNowUs)));
    }
#endif
    return std::chrono::steady_clock::now();
}

std::string StatsManager::makeKey(const std::string& trackId, const std::string& bikeName) {
    return trackId + "|" + bikeName;
}

// Context
// ============================================================================

void StatsManager::setCurrentContext(const std::string& trackId, const std::string& bikeName,
                                      const std::string& category) {
    m_currentTrackId = trackId;
    m_currentBikeName = bikeName;
    m_currentKey = makeKey(trackId, bikeName);
    m_currentCategory = category;

    // Update bike-to-category mapping (persisted for category-scoped PB lookups)
    if (!bikeName.empty() && !category.empty()) {
        m_bikeCategories[bikeName] = category;
        m_dirty = true;
    }

    // Ensure entries exist for telemetry-rate lookups (avoids operator[] creating entries at 100Hz)
    m_trackBikeStats[m_currentKey];
    m_bikeOdometers[bikeName];

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
        stats.validLaps++;
        m_sessionLaps++;
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

    m_dirty = true;
}

void StatsManager::updateTelemetry(float speedMs, bool isCrashed, int currentGear) {
    // Sanitize the speed sample: NaN is rejected by the comparisons below
    // anyway, but +Inf passes them and would poison the odometer / top-speed
    // values, which are PERSISTED - one bad physics sample would corrupt the
    // stats file with no recovery path.
    if (!std::isfinite(speedMs)) {
        speedMs = 0.0f;
    }

    // Single lookup for the entire method — setCurrentContext() guarantees entry exists
    TrackBikeStats* stats = nullptr;
    if (!m_currentKey.empty()) {
        auto it = m_trackBikeStats.find(m_currentKey);
        if (it != m_trackBikeStats.end()) stats = &it->second;
    }

    // Crash edge detection — only count rising edges (not-crashed -> crashed)
    if (isCrashed && !m_wasCrashed && stats) {
        stats->crashCount++;
        m_globalTotalsDirty = true;
        m_sessionCrashes++;
        m_curLapCrashes++;
        m_dirty = true;
    }
    m_wasCrashed = isCrashed;

    // Gear shift edge detection — count any gear change (including neutral transitions)
    if (m_lastGear >= 0 && currentGear >= 0 && currentGear != m_lastGear && stats) {
        stats->gearShiftCount++;
        m_globalTotalsDirty = true;
        m_sessionGearShifts++;
        m_curLapGearShifts++;
        m_dirty = true;
    }
    if (currentGear >= 0) {
        m_lastGear = currentGear;
    }

    if (!stats) return;

    // Top speed (session + per-lap)
    if (speedMs > m_sessionTopSpeedMs) {
        m_sessionTopSpeedMs = speedMs;
    }
    if (speedMs > m_curLapTopSpeedMs) {
        m_curLapTopSpeedMs = speedMs;
    }
    if (speedMs > stats->topSpeedMs) {
        stats->topSpeedMs = speedMs;
        m_dirty = true;
    }

    // Distance (integrated from speed * deltaTime)
    if (!m_currentBikeName.empty()) {
        auto now = odometerNow();

        if (!m_hasLastOdometerUpdateTime) {
            m_lastOdometerUpdateTime = now;
            m_hasLastOdometerUpdateTime = true;
        } else {
            auto duration = std::chrono::duration_cast<std::chrono::microseconds>(now - m_lastOdometerUpdateTime);
            float deltaTime = duration.count() / 1000000.0f;
            m_lastOdometerUpdateTime = now;

            if (deltaTime > 0.0f && deltaTime <= 0.5f && speedMs >= MIN_MOVEMENT_SPEED_MS) {
                float distanceMeters = speedMs * deltaTime;
                m_sessionTripDistance += distanceMeters;
                m_curLapDistance += distanceMeters;
                m_bikeOdometers[m_currentBikeName] += distanceMeters;
                stats->totalDistanceM += distanceMeters;
                m_unsavedDistance += distanceMeters;
                // Only mark dirty every ~100m to avoid per-frame save overhead
                if (m_unsavedDistance >= 100.0) {
                    m_dirty = true;
                    m_globalTotalsDirty = true;
                    m_unsavedDistance = 0.0;
                }
            }
        }
    }
}

void StatsManager::recordSessionStart(int sessionType) {
    if (m_currentKey.empty()) {
        DEBUG_WARN("[StatsManager] recordSessionStart called before setCurrentContext — session stats may be incomplete");
    }

    // Only reset session stats when the session type actually changes
    // (not on pit stop re-entries within the same session)
    bool sessionChanged = (sessionType != m_lastSessionType);
    m_lastSessionType = sessionType;

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
    }

    // Always reset time tracking on session (re-)entry to prevent double-counting
    // time across pit stop cycles
    m_sessionStartTime = std::chrono::steady_clock::now();
    m_totalPausedMs = 0;

    // Only reset race finish tracking on actual session changes.
    if (sessionChanged) {
        m_raceFinishRecorded = false;
        m_playerHasFastestLapInRace = false;
    }

    m_sessionActive = true;
    m_wasCrashed = false;
    m_lastGear = -1;
    m_hasLastOdometerUpdateTime = false;

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

void StatsManager::tryRecordRaceFinish(const PluginData& pd) {
    if (m_raceFinishRecorded) return;  // Guard against double-counting
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
            m_globalStats.raceCount++;
            if (position == 1) m_globalStats.firstPositions++;
            else if (position == 2) m_globalStats.secondPositions++;
            else if (position == 3) m_globalStats.thirdPositions++;
            if (m_playerHasFastestLapInRace) m_globalStats.fastestLapCount++;
            m_dirty = true;
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

bool StatsManager::updatePersonalBest(const StatsPersonalBestData& entry) {
    if (!entry.isValid()) return false;
    if (m_currentKey.empty()) return false;

    auto it = m_personalBests.find(m_currentKey);
    if (it != m_personalBests.end() && it->second.lapTime <= entry.lapTime) {
        return false;  // Existing PB is faster
    }

    m_personalBests[m_currentKey] = entry;
    m_dirty = true;   // deferred: persisted on leave-track (RunStop/RunDeinit). A PB is set at
                      // lap completion (start/finish) — on track — and we never write on track.
    return true;
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

bool StatsManager::updatePersonalBest(const std::string& trackId, const std::string& bikeName,
                                       const StatsPersonalBestData& entry) {
    if (!entry.isValid()) return false;

    std::string key = makeKey(trackId, bikeName);
    auto it = m_personalBests.find(key);
    if (it != m_personalBests.end() && it->second.lapTime <= entry.lapTime) {
        return false;
    }
    m_personalBests[key] = entry;
    m_dirty = true;   // deferred: persisted on leave-track (RunStop/RunDeinit) — never on track.
    return true;
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
// Query — global
// ============================================================================

GlobalStats StatsManager::getGlobalStats() const {
    return m_globalStats;
}

void StatsManager::updateBreakoutHighScore(int score) {
    if (score <= m_globalStats.breakoutHighScore) return;
    m_globalStats.breakoutHighScore = score;
    m_dirty = true;
    save();
}

double StatsManager::getOdometerForBike(const std::string& bikeName) const {
    auto it = m_bikeOdometers.find(bikeName);
    return it != m_bikeOdometers.end() ? it->second : 0.0;
}

double StatsManager::getOdometerForCurrentBike() const {
    return getOdometerForBike(m_currentBikeName);
}

double StatsManager::getTotalOdometer() const {
    if (m_globalTotalsDirty) recomputeGlobalTotals();
    return m_cachedTotalOdometer;
}

void StatsManager::recomputeGlobalTotals() const {
    m_cachedTotalLaps = 0;
    m_cachedTotalTimeMs = 0;
    m_cachedTotalCrashes = 0;
    m_cachedTotalGearShifts = 0;
    m_cachedTotalPenalties = 0;
    m_cachedTotalPenaltyTimeMs = 0;
    m_cachedTotalOdometer = 0.0;
    for (const auto& [_, stats] : m_trackBikeStats) {
        m_cachedTotalLaps += stats.validLaps;
        m_cachedTotalTimeMs += stats.totalTimeOnTrackMs;
        m_cachedTotalCrashes += stats.crashCount;
        m_cachedTotalGearShifts += stats.gearShiftCount;
        m_cachedTotalPenalties += stats.penaltyCount;
        m_cachedTotalPenaltyTimeMs += stats.penaltyTimeMs;
    }
    for (const auto& [_, distance] : m_bikeOdometers) {
        m_cachedTotalOdometer += distance;
    }
    m_globalTotalsDirty = false;
}

int StatsManager::getGlobalTotalLaps() const {
    if (m_globalTotalsDirty) recomputeGlobalTotals();
    return m_cachedTotalLaps;
}

int64_t StatsManager::getGlobalTotalTimeMs() const {
    if (m_globalTotalsDirty) recomputeGlobalTotals();
    int64_t total = m_cachedTotalTimeMs;
    if (m_sessionActive) total += getSessionDurationMs();
    return total;
}

int StatsManager::getGlobalTotalCrashes() const {
    if (m_globalTotalsDirty) recomputeGlobalTotals();
    return m_cachedTotalCrashes;
}

int StatsManager::getGlobalTotalGearShifts() const {
    if (m_globalTotalsDirty) recomputeGlobalTotals();
    return m_cachedTotalGearShifts;
}

int StatsManager::getGlobalTotalPenalties() const {
    if (m_globalTotalsDirty) recomputeGlobalTotals();
    return m_cachedTotalPenalties;
}

int64_t StatsManager::getGlobalTotalPenaltyTimeMs() const {
    if (m_globalTotalsDirty) recomputeGlobalTotals();
    return m_cachedTotalPenaltyTimeMs;
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
    m_trackBikeStats.clear();
    m_personalBests.clear();
    m_bikeOdometers.clear();
    m_bikeCategories.clear();
    m_globalStats = GlobalStats();
    m_globalTotalsDirty = true;
    m_dirty = true;

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
    m_wasCrashed = false;
    m_lastGear = -1;
    m_raceFinishRecorded = false;
    m_playerHasFastestLapInRace = false;

    // Re-create entry if a session is active so updateTelemetry() keeps working
    if (m_sessionActive && !m_currentKey.empty()) {
        m_trackBikeStats[m_currentKey];
    }

    save();
}
