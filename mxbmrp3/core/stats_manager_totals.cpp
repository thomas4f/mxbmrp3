// ============================================================================
// core/stats_manager_totals.cpp
// StatsManager global queries — the whole-file totals and maxima (laps, time,
// crashes, penalties, odometer, the most laps at one track and the longest
// odometer of one bike) recomputed from the per-track/bike records behind a
// dirty flag, the distinct track/bike counts, and the two global-stat writers
// (breakout high score, crash tally reset). Moved verbatim out of
// stats_manager.cpp when it crossed the file budget; the class and its API
// are unchanged.
// ============================================================================
#include "stats_manager.h"
#include "achievement_manager.h"

#include <string>
#include <unordered_map>

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
    AchievementManager::getInstance().onStatsChanged();
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
    m_cachedMaxTrackLaps = 0;
    m_cachedMaxBikeOdometer = 0.0;
    // Laps per track across its bikes, for the max: keys are "trackId|bikeName".
    std::unordered_map<std::string, int> trackLaps;
    for (const auto& [key, stats] : m_trackBikeStats) {
        const size_t bar = key.find('|');
        trackLaps[bar == std::string::npos ? key : key.substr(0, bar)] += stats.validLaps;
        m_cachedTotalLaps += stats.validLaps;
        m_cachedTotalTimeMs += stats.totalTimeOnTrackMs;
        m_cachedTotalCrashes += stats.crashCount;
        m_cachedTotalGearShifts += stats.gearShiftCount;
        m_cachedTotalPenalties += stats.penaltyCount;
        m_cachedTotalPenaltyTimeMs += stats.penaltyTimeMs;
    }
    for (const auto& [_, distance] : m_bikeOdometers) {
        m_cachedTotalOdometer += distance;
        if (distance > m_cachedMaxBikeOdometer) m_cachedMaxBikeOdometer = distance;
    }
    for (const auto& [_, laps] : trackLaps) {
        if (laps > m_cachedMaxTrackLaps) m_cachedMaxTrackLaps = laps;
    }
    m_globalTotalsDirty = false;
}

int StatsManager::getMaxLapsAtOneTrack() const {
    if (m_globalTotalsDirty) recomputeGlobalTotals();
    return m_cachedMaxTrackLaps;
}

double StatsManager::getMaxOdometerOnOneBike() const {
    if (m_globalTotalsDirty) recomputeGlobalTotals();
    return m_cachedMaxBikeOdometer;
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

// Back to zero, and PERSISTED at once rather than at the next autosave: the
// button exists so a streamer can start a run clean on camera, and a count that
// came back after a crash-to-desktop would be the one failure that matters.
void StatsManager::resetCrashTally() {
    if (m_globalStats.crashTally == 0) return;
    m_globalStats.crashTally = 0;
    m_dirty = true;
    save();
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

void StatsManager::recomputeDistinctCounts() const {
    // Keys are "trackId|bikeName"; a track counts once however many bikes were
    // ridden on it, and a bike counts once from its odometer entry (created on
    // the first context set, like the track+bike record).
    std::unordered_map<std::string, bool> tracks;
    for (const auto& [key, _] : m_trackBikeStats) {
        const size_t bar = key.find('|');
        tracks[bar == std::string::npos ? key : key.substr(0, bar)] = true;
    }
    tracks.erase("");
    m_cachedDistinctTracks = static_cast<int>(tracks.size());
    int bikes = 0;
    for (const auto& [name, _] : m_bikeOdometers) {
        if (!name.empty()) ++bikes;
    }
    m_cachedDistinctBikes = bikes;
    std::unordered_map<std::string, bool> classes;
    for (const auto& [_, category] : m_bikeCategories) {
        if (!category.empty()) classes[category] = true;
    }
    m_cachedDistinctBikeClasses = static_cast<int>(classes.size());
    m_distinctDirty = false;
}

int StatsManager::getDistinctTrackCount() const {
    if (m_distinctDirty) recomputeDistinctCounts();
    return m_cachedDistinctTracks;
}

int StatsManager::getDistinctBikeCount() const {
    if (m_distinctDirty) recomputeDistinctCounts();
    return m_cachedDistinctBikes;
}

int StatsManager::getDistinctBikeClassCount() const {
    if (m_distinctDirty) recomputeDistinctCounts();
    return m_cachedDistinctBikeClasses;
}
