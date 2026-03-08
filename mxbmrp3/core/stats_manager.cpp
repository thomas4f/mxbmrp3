// ============================================================================
// core/stats_manager.cpp
// Unified stats system — tracks per-track/bike stats, global race stats,
// personal bests, and odometer data in a single JSON file
// ============================================================================
#include "stats_manager.h"
#include "plugin_data.h"
#include "plugin_utils.h"
#include "../diagnostics/logger.h"
#include "../vendor/nlohmann/json.hpp"

#include <fstream>
#include <cstdio>
#include <algorithm>
#include <windows.h>

static constexpr const char* STATS_SUBDIRECTORY = "mxbmrp3";
static constexpr const char* STATS_FILENAME = "mxbmrp3_stats.json";

// Old file names for migration
static constexpr const char* OLD_PB_FILENAME = "mxbmrp3_personal_bests.json";
static constexpr const char* OLD_ODOMETER_FILENAME = "mxbmrp3_odometer_data.json";

// Minimum speed to count as movement (filters out noise when stationary)
static constexpr float MIN_MOVEMENT_SPEED_MS = 0.1f;  // ~0.36 km/h

StatsManager& StatsManager::getInstance() {
    static StatsManager instance;
    return instance;
}

std::string StatsManager::makeKey(const std::string& trackId, const std::string& bikeName) {
    return trackId + "|" + bikeName;
}

const std::string& StatsManager::getFilePath() const {
    if (!m_cachedFilePath.empty()) return m_cachedFilePath;

    std::string path;
    if (m_savePath.empty()) {
        path = std::string(".\\") + STATS_SUBDIRECTORY;
    } else {
        path = m_savePath;
        if (!path.empty() && path.back() != '/' && path.back() != '\\') {
            path += '\\';
        }
        path += STATS_SUBDIRECTORY;
    }

    // Ensure directory exists (once)
    if (!m_directoryEnsured) {
        if (!CreateDirectoryA(path.c_str(), NULL)) {
            DWORD error = GetLastError();
            if (error != ERROR_ALREADY_EXISTS) {
                DEBUG_INFO_F("[StatsManager] Failed to create directory: %s (error %lu)", path.c_str(), error);
            }
        }
        m_directoryEnsured = true;
    }

    m_cachedFilePath = path + "\\" + STATS_FILENAME;
    return m_cachedFilePath;
}

void StatsManager::load(const char* savePath) {
    m_savePath = savePath ? savePath : "";
    m_cachedFilePath.clear();
    m_directoryEnsured = false;
    m_trackBikeStats.clear();
    m_personalBests.clear();
    m_bikeOdometers.clear();
    m_globalStats = GlobalStats();
    m_dirty = false;

    std::string filePath = getFilePath();

    std::ifstream file(filePath);
    if (!file.is_open()) {
        DEBUG_INFO_F("[StatsManager] No stats file found at %s", filePath.c_str());
        migrateOldFiles();
        return;
    }

    try {
        nlohmann::json j;
        file >> j;

        int version = j.value("version", 0);
        if (version > FILE_VERSION) {
            DEBUG_INFO_F("[StatsManager] File version %d is newer than expected %d — loading with defaults for unknown fields",
                         version, FILE_VERSION);
        }

        // Parse global stats
        if (j.contains("global") && j["global"].is_object()) {
            const auto& g = j["global"];
            m_globalStats.raceCount = (std::max)(g.value("raceCount", 0), 0);
            m_globalStats.firstPositions = (std::max)(g.value("firstPositions", 0), 0);
            m_globalStats.secondPositions = (std::max)(g.value("secondPositions", 0), 0);
            m_globalStats.thirdPositions = (std::max)(g.value("thirdPositions", 0), 0);
            m_globalStats.fastestLapCount = (std::max)(g.value("fastestLapCount", 0), 0);
            m_globalStats.penaltyTimeMs = (std::max)(g.value("penaltyTimeMs", static_cast<int64_t>(0)), static_cast<int64_t>(0));
            m_globalStats.breakoutHighScore = (std::max)(g.value("breakoutHighScore", 0), 0);
        }

        // Parse bike odometers
        if (j.contains("bikes") && j["bikes"].is_object()) {
            for (auto& [bikeName, bikeJson] : j["bikes"].items()) {
                if (bikeJson.is_object()) {
                    m_bikeOdometers[bikeName] = (std::max)(bikeJson.value("odometer", 0.0), 0.0);
                }
            }
        }

        // Parse track+bike stats
        if (j.contains("trackBike") && j["trackBike"].is_object()) {
            for (auto& [key, tbJson] : j["trackBike"].items()) {
                TrackBikeStats stats;
                stats.validLaps = (std::max)(tbJson.value("validLaps", 0), 0);
                stats.totalLapTimeMs = (std::max)(tbJson.value("totalLapTimeMs", static_cast<int64_t>(0)), static_cast<int64_t>(0));
                // Sentinel -1 = no data; clamp invalid negatives to -1
                stats.bestLapTimeMs = (std::max)(tbJson.value("bestLapTimeMs", -1), -1);
                stats.bestSector1Ms = (std::max)(tbJson.value("bestSector1Ms", -1), -1);
                stats.bestSector2Ms = (std::max)(tbJson.value("bestSector2Ms", -1), -1);
                stats.bestSector3Ms = (std::max)(tbJson.value("bestSector3Ms", -1), -1);
                stats.bestSector4Ms = (std::max)(tbJson.value("bestSector4Ms", -1), -1);
                stats.totalTimeOnTrackMs = (std::max)(tbJson.value("totalTimeOnTrackMs", static_cast<int64_t>(0)), static_cast<int64_t>(0));
                stats.totalDistanceM = (std::max)(tbJson.value("totalDistanceM", 0.0), 0.0);
                stats.crashCount = (std::max)(tbJson.value("crashCount", 0), 0);
                stats.gearShiftCount = (std::max)(tbJson.value("gearShiftCount", 0), 0);
                stats.penaltyCount = (std::max)(tbJson.value("penaltyCount", 0), 0);
                stats.penaltyTimeMs = (std::max)(tbJson.value("penaltyTimeMs", static_cast<int64_t>(0)), static_cast<int64_t>(0));
                stats.topSpeedMs = (std::max)(tbJson.value("topSpeedMs", 0.0f), 0.0f);
                stats.firstSessionTimestamp = tbJson.value("firstSession", static_cast<std::time_t>(0));
                stats.lastSessionTimestamp = tbJson.value("lastSession", static_cast<std::time_t>(0));
                m_trackBikeStats[key] = stats;

                // Parse nested personal best
                if (tbJson.contains("personalBest") && tbJson["personalBest"].is_object()) {
                    const auto& pbJson = tbJson["personalBest"];
                    StatsPersonalBestData pb;
                    pb.lapTime = (std::max)(pbJson.value("lapTime", -1), -1);
                    pb.sector1 = (std::max)(pbJson.value("sector1", -1), -1);
                    pb.sector2 = (std::max)(pbJson.value("sector2", -1), -1);
                    pb.sector3 = (std::max)(pbJson.value("sector3", -1), -1);
                    pb.sector4 = (std::max)(pbJson.value("sector4", -1), -1);
                    pb.setupName = pbJson.value("setupName", "");
                    pb.conditions = (std::max)(pbJson.value("conditions", -1), -1);
                    pb.timestamp = pbJson.value("timestamp", static_cast<std::time_t>(0));
                    if (pb.isValid()) {
                        m_personalBests[key] = pb;
                    }
                }
            }
        }

        DEBUG_INFO_F("[StatsManager] Loaded stats: %zu track/bike combos, %zu bikes, %zu PBs from %s",
                     m_trackBikeStats.size(), m_bikeOdometers.size(), m_personalBests.size(), filePath.c_str());

    } catch (const nlohmann::json::exception& e) {
        DEBUG_INFO_F("[StatsManager] Failed to parse JSON: %s — starting fresh", e.what());
        m_trackBikeStats.clear();
        m_personalBests.clear();
        m_bikeOdometers.clear();
        m_globalStats = GlobalStats();
    } catch (const std::exception& e) {
        DEBUG_INFO_F("[StatsManager] Error loading stats: %s — starting fresh", e.what());
        m_trackBikeStats.clear();
        m_personalBests.clear();
        m_bikeOdometers.clear();
        m_globalStats = GlobalStats();
    }
}

static nlohmann::json serializePersonalBest(const StatsPersonalBestData& pb) {
    nlohmann::json pbJson;
    pbJson["lapTime"] = pb.lapTime;
    pbJson["sector1"] = pb.sector1;
    pbJson["sector2"] = pb.sector2;
    pbJson["sector3"] = pb.sector3;
    pbJson["sector4"] = pb.sector4;
    pbJson["setupName"] = pb.setupName;
    pbJson["conditions"] = pb.conditions;
    pbJson["timestamp"] = pb.timestamp;
    return pbJson;
}

void StatsManager::save() {
    if (!m_dirty) return;

    std::string filePath = getFilePath();
    std::string tempPath = filePath + ".tmp";

    try {
        nlohmann::json j;
        j["version"] = FILE_VERSION;

        // Global stats
        nlohmann::json global;
        global["raceCount"] = m_globalStats.raceCount;
        global["firstPositions"] = m_globalStats.firstPositions;
        global["secondPositions"] = m_globalStats.secondPositions;
        global["thirdPositions"] = m_globalStats.thirdPositions;
        global["fastestLapCount"] = m_globalStats.fastestLapCount;
        global["penaltyTimeMs"] = m_globalStats.penaltyTimeMs;
        if (m_globalStats.breakoutHighScore > 0) {
            global["breakoutHighScore"] = m_globalStats.breakoutHighScore;
        }
        j["global"] = global;

        // Bike odometers
        nlohmann::json bikes = nlohmann::json::object();
        for (const auto& [bikeName, odometer] : m_bikeOdometers) {
            nlohmann::json bikeJson;
            bikeJson["odometer"] = odometer;
            bikes[bikeName] = bikeJson;
        }
        j["bikes"] = bikes;

        // Track+bike stats with nested personal bests
        nlohmann::json trackBike = nlohmann::json::object();
        for (const auto& [key, stats] : m_trackBikeStats) {
            nlohmann::json tbJson;
            tbJson["validLaps"] = stats.validLaps;
            tbJson["totalLapTimeMs"] = stats.totalLapTimeMs;
            tbJson["bestLapTimeMs"] = stats.bestLapTimeMs;
            tbJson["bestSector1Ms"] = stats.bestSector1Ms;
            tbJson["bestSector2Ms"] = stats.bestSector2Ms;
            tbJson["bestSector3Ms"] = stats.bestSector3Ms;
            tbJson["bestSector4Ms"] = stats.bestSector4Ms;
            tbJson["totalTimeOnTrackMs"] = stats.totalTimeOnTrackMs;
            tbJson["totalDistanceM"] = stats.totalDistanceM;
            tbJson["crashCount"] = stats.crashCount;
            tbJson["gearShiftCount"] = stats.gearShiftCount;
            tbJson["penaltyCount"] = stats.penaltyCount;
            tbJson["penaltyTimeMs"] = stats.penaltyTimeMs;
            tbJson["topSpeedMs"] = stats.topSpeedMs;
            tbJson["firstSession"] = stats.firstSessionTimestamp;
            tbJson["lastSession"] = stats.lastSessionTimestamp;

            // Nest personal best if it exists
            auto pbIt = m_personalBests.find(key);
            if (pbIt != m_personalBests.end() && pbIt->second.isValid()) {
                tbJson["personalBest"] = serializePersonalBest(pbIt->second);
            }

            trackBike[key] = tbJson;
        }

        // Also save PBs that exist without corresponding TrackBikeStats
        for (const auto& [key, pb] : m_personalBests) {
            if (m_trackBikeStats.find(key) == m_trackBikeStats.end() && pb.isValid()) {
                nlohmann::json tbJson;
                tbJson["personalBest"] = serializePersonalBest(pb);
                trackBike[key] = tbJson;
            }
        }

        j["trackBike"] = trackBike;

        // Write to temp file first
        std::ofstream tempFile(tempPath);
        if (!tempFile.is_open()) {
            DEBUG_INFO_F("[StatsManager] Failed to open temp file for writing: %s", tempPath.c_str());
            return;
        }

        tempFile << j.dump(2);
        tempFile.close();

        if (tempFile.fail()) {
            DEBUG_INFO_F("[StatsManager] Failed to write temp file: %s", tempPath.c_str());
            std::remove(tempPath.c_str());
            return;
        }

        // Atomic rename
        if (!MoveFileExA(tempPath.c_str(), filePath.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
            DEBUG_WARN_F("[StatsManager] Failed to save file (error %lu): %s", GetLastError(), filePath.c_str());
            std::remove(tempPath.c_str());
            return;
        }

        m_dirty = false;
        DEBUG_INFO_F("[StatsManager] Saved stats to %s", filePath.c_str());

    } catch (const std::exception& e) {
        DEBUG_INFO_F("[StatsManager] Error saving stats: %s", e.what());
        std::remove(tempPath.c_str());
    }
}

void StatsManager::migrateOldFiles() {
    // One-shot migration from old PB/odometer files into unified stats.
    // Called only when no stats file exists. If parsing fails, we skip and start fresh.

    std::string basePath;
    if (m_savePath.empty()) {
        basePath = std::string(".\\") + STATS_SUBDIRECTORY;
    } else {
        basePath = m_savePath;
        if (!basePath.empty() && basePath.back() != '/' && basePath.back() != '\\') {
            basePath += '\\';
        }
        basePath += STATS_SUBDIRECTORY;
    }

    bool migrated = false;

    // Import personal bests
    std::string pbPath = basePath + "\\" + OLD_PB_FILENAME;
    try {
        std::ifstream pbFile(pbPath);
        if (pbFile.is_open()) {
            nlohmann::json j;
            pbFile >> j;
            if (j.contains("entries") && j["entries"].is_object()) {
                for (auto& [key, value] : j["entries"].items()) {
                    StatsPersonalBestData pb;
                    pb.lapTime = (std::max)(value.value("lapTime", -1), -1);
                    pb.sector1 = (std::max)(value.value("sector1", -1), -1);
                    pb.sector2 = (std::max)(value.value("sector2", -1), -1);
                    pb.sector3 = (std::max)(value.value("sector3", -1), -1);
                    pb.sector4 = (std::max)(value.value("sector4", -1), -1);
                    pb.setupName = value.value("setupName", "");
                    pb.conditions = (std::max)(value.value("conditions", -1), -1);
                    pb.timestamp = value.value("timestamp", static_cast<std::time_t>(0));

                    if (pb.isValid()) {
                        m_personalBests[key] = pb;

                        auto& stats = m_trackBikeStats[key];
                        if (stats.bestLapTimeMs < 0 || pb.lapTime < stats.bestLapTimeMs)
                            stats.bestLapTimeMs = pb.lapTime;
                        if (pb.sector1 > 0 && (stats.bestSector1Ms < 0 || pb.sector1 < stats.bestSector1Ms))
                            stats.bestSector1Ms = pb.sector1;
                        if (pb.sector2 > 0 && (stats.bestSector2Ms < 0 || pb.sector2 < stats.bestSector2Ms))
                            stats.bestSector2Ms = pb.sector2;
                        if (pb.sector3 > 0 && (stats.bestSector3Ms < 0 || pb.sector3 < stats.bestSector3Ms))
                            stats.bestSector3Ms = pb.sector3;
                        if (pb.sector4 > 0 && (stats.bestSector4Ms < 0 || pb.sector4 < stats.bestSector4Ms))
                            stats.bestSector4Ms = pb.sector4;
                    }
                }
                DEBUG_INFO_F("[StatsManager] Imported %zu personal bests from old file", m_personalBests.size());
                migrated = true;
            }
        }
    } catch (const std::exception& e) {
        DEBUG_INFO_F("[StatsManager] Failed to import personal bests: %s", e.what());
    }

    // Import odometer data
    std::string odomPath = basePath + "\\" + OLD_ODOMETER_FILENAME;
    try {
        std::ifstream odomFile(odomPath);
        if (odomFile.is_open()) {
            nlohmann::json j;
            odomFile >> j;
            if (j.contains("odometers") && j["odometers"].is_object()) {
                for (auto& [bikeName, distanceJson] : j["odometers"].items()) {
                    if (distanceJson.is_number()) {
                        m_bikeOdometers[bikeName] = distanceJson.get<double>();
                    }
                }
                DEBUG_INFO_F("[StatsManager] Imported odometer data for %zu bikes", m_bikeOdometers.size());
                migrated = true;
            }
        }
    } catch (const std::exception& e) {
        DEBUG_INFO_F("[StatsManager] Failed to import odometer data: %s", e.what());
    }

    if (migrated) {
        m_dirty = true;
        save();
        // Only delete old files if save succeeded (m_dirty cleared on success)
        if (!m_dirty) {
            DeleteFileA(pbPath.c_str());
            DeleteFileA(odomPath.c_str());
            DEBUG_INFO("[StatsManager] Migration complete, old files deleted");
        } else {
            DEBUG_WARN("[StatsManager] Migration save failed — keeping old files for retry");
        }
    }
}

// ============================================================================
// Context
// ============================================================================

void StatsManager::setCurrentContext(const std::string& trackId, const std::string& bikeName) {
    m_currentTrackId = trackId;
    m_currentBikeName = bikeName;
    m_currentKey = makeKey(trackId, bikeName);

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
        auto now = std::chrono::steady_clock::now();

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

    // Only reset penalty baseline and race finish tracking on actual session changes.
    // Resetting on pit re-entry would cause updatePenaltyFromStandings() to re-add
    // the full penalty total as a delta, and lose the race finish guard.
    if (sessionChanged) {
        m_lastKnownStandingsPenaltyMs = 0;
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

void StatsManager::recordPenalty() {
    m_sessionPenaltyCount++;
    m_curLapPenaltyCount++;

    // Track+bike penalty count (time is handled by updatePenaltyFromStandings)
    if (!m_currentKey.empty()) {
        m_trackBikeStats[m_currentKey].penaltyCount++;
        m_globalTotalsDirty = true;
        m_dirty = true;
    }
}

void StatsManager::updatePenaltyFromStandings(int64_t currentTotalPenaltyMs, bool isRace) {
    // Detect delta from last known standings penalty total
    int64_t delta = currentTotalPenaltyMs - m_lastKnownStandingsPenaltyMs;
    if (delta <= 0) return;

    m_lastKnownStandingsPenaltyMs = currentTotalPenaltyMs;

    // Apply delta to all accumulators
    m_sessionPenaltyTimeMs += delta;
    m_curLapPenaltyTimeMs += delta;

    if (!m_currentKey.empty()) {
        m_trackBikeStats[m_currentKey].penaltyTimeMs += delta;
        m_globalTotalsDirty = true;
        m_dirty = true;
    }

    if (isRace) {
        m_globalStats.penaltyTimeMs += delta;
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

const StatsPersonalBestData* StatsManager::getPersonalBest() const {
    if (m_currentKey.empty()) return nullptr;
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
    m_dirty = true;
    save();  // PBs are rare and high-value — persist immediately
    return true;
}

// Query — by explicit track+bike
const StatsPersonalBestData* StatsManager::getPersonalBest(const std::string& trackId,
                                                            const std::string& bikeName) const {
    std::string key = makeKey(trackId, bikeName);
    auto it = m_personalBests.find(key);
    return it != m_personalBests.end() ? &it->second : nullptr;
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
    m_dirty = true;
    save();  // PBs are rare and high-value — persist immediately
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
        m_lastKnownStandingsPenaltyMs = 0;
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
    m_lastKnownStandingsPenaltyMs = 0;
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
