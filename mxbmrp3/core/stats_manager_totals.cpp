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
    m_cachedMaxTrackId.clear();
    m_cachedMaxBikeName.clear();
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
    // The holder is recorded with the maximum, not looked up afterwards: a
    // second pass would have to re-derive the same comparison and could pick a
    // different tie.
    for (const auto& [bike, distance] : m_bikeOdometers) {
        m_cachedTotalOdometer += distance;
        if (distance > m_cachedMaxBikeOdometer) {
            m_cachedMaxBikeOdometer = distance;
            m_cachedMaxBikeName = bike;
        }
    }
    for (const auto& [track, laps] : trackLaps) {
        if (laps > m_cachedMaxTrackLaps) {
            m_cachedMaxTrackLaps = laps;
            m_cachedMaxTrackId = track;
        }
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

const std::string& StatsManager::getMaxLapsTrackId() const {
    if (m_globalTotalsDirty) recomputeGlobalTotals();
    return m_cachedMaxTrackId;
}

const std::string& StatsManager::getMaxOdometerBikeName() const {
    if (m_globalTotalsDirty) recomputeGlobalTotals();
    return m_cachedMaxBikeName;
}

// BY VALUE, not by reference. The fallback is the ARGUMENT, so a reference
// return handed back whatever the caller passed - fine for today's two call
// sites, which both pass a member, and a dangle the first time someone passes
// a temporary. A track name is read once per settings rebuild, never per frame,
// so the copy costs nothing worth the trap.
std::string StatsManager::getTrackDisplayName(const std::string& trackId) const {
    const auto it = m_trackNames.find(trackId);
    return it != m_trackNames.end() && !it->second.empty() ? it->second : trackId;
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
    // A LAP, not a load. The entry below is created the moment a track and bike
    // are selected, so counting keys counted every track ever opened - which is
    // how "apparently I've ridden 328 different tracks" happened on stream. One
    // valid lap is the smallest thing that means you actually rode it.
    std::unordered_map<std::string, bool> tracks;
    std::unordered_map<std::string, bool> lappedBikes;
    for (const auto& [key, st] : m_trackBikeStats) {
        if (st.validLaps <= 0) continue;
        const size_t bar = key.find('|');
        tracks[bar == std::string::npos ? key : key.substr(0, bar)] = true;
        if (bar != std::string::npos) lappedBikes[key.substr(bar + 1)] = true;
    }
    tracks.erase("");
    m_cachedDistinctTracks = static_cast<int>(tracks.size());
    lappedBikes.erase("");
    m_cachedDistinctBikes = static_cast<int>(lappedBikes.size());
    // A class counts once one of its bikes has been round, for the same reason.
    std::unordered_map<std::string, bool> classes;
    for (const auto& [name, category] : m_bikeCategories) {
        if (category.empty() || !lappedBikes.count(name)) continue;
        classes[category] = true;
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
