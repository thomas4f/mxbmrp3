// ============================================================================
// core/stats_manager_persistence.cpp
// StatsManager JSON persistence — file-path resolution, load, save, and the
// one-time migration of the old split personal-bests / odometer files into the
// unified stats file. Extracted verbatim from stats_manager.cpp when it grew
// past ~1.2k lines; the StatsManager class, members, and public API are
// unchanged. The file-path/migration constants and finiteOrZero helper — used
// only by this half — moved here with it.
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

// Clamp persisted floating-point values on load: a non-finite value in the
// file (corruption, or written by a pre-isfinite-guard build) must not be
// re-adopted into live state.
static double finiteOrZero(double v) {
    return std::isfinite(v) ? v : 0.0;
}

static constexpr const char* STATS_SUBDIRECTORY = "mxbmrp3";
static constexpr const char* STATS_FILENAME = "mxbmrp3_stats.json";

// Old file names for migration
static constexpr const char* OLD_PB_FILENAME = "mxbmrp3_personal_bests.json";
static constexpr const char* OLD_ODOMETER_FILENAME = "mxbmrp3_odometer_data.json";

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
    m_bikeCategories.clear();   // a reload must not keep stale bike->category mappings
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
        if (version != FILE_VERSION) {
            DEBUG_INFO_F("[StatsManager] File version %d differs from expected %d — loading known fields with defaults",
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
                    m_bikeOdometers[bikeName] = (std::max)(finiteOrZero(bikeJson.value("odometer", 0.0)), 0.0);
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
                stats.totalDistanceM = (std::max)(finiteOrZero(tbJson.value("totalDistanceM", 0.0)), 0.0);
                stats.crashCount = (std::max)(tbJson.value("crashCount", 0), 0);
                stats.gearShiftCount = (std::max)(tbJson.value("gearShiftCount", 0), 0);
                stats.penaltyCount = (std::max)(tbJson.value("penaltyCount", 0), 0);
                stats.penaltyTimeMs = (std::max)(tbJson.value("penaltyTimeMs", static_cast<int64_t>(0)), static_cast<int64_t>(0));
                stats.topSpeedMs = static_cast<float>((std::max)(finiteOrZero(tbJson.value("topSpeedMs", 0.0)), 0.0));
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

        // Parse bike-to-category mapping
        if (j.contains("bikeCategories") && j["bikeCategories"].is_object()) {
            for (auto& [bikeName, categoryJson] : j["bikeCategories"].items()) {
                if (categoryJson.is_string()) {
                    m_bikeCategories[bikeName] = categoryJson.get<std::string>();
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
        m_bikeCategories.clear();
        m_globalStats = GlobalStats();
    } catch (const std::exception& e) {
        DEBUG_INFO_F("[StatsManager] Error loading stats: %s — starting fresh", e.what());
        m_trackBikeStats.clear();
        m_personalBests.clear();
        m_bikeOdometers.clear();
        m_bikeCategories.clear();
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

        // Bike-to-category mapping
        if (!m_bikeCategories.empty()) {
            nlohmann::json bikeCategories = nlohmann::json::object();
            for (const auto& [bikeName, category] : m_bikeCategories) {
                bikeCategories[bikeName] = category;
            }
            j["bikeCategories"] = bikeCategories;
        }

        // Write via the shared atomic writer (temp file + MoveFileExA replace). Synchronous:
        // stats are saved on discrete, infrequent events (lap completion, session end,
        // shutdown), not the per-frame path, and callers/tests read the file right after —
        // so this keeps immediate durability while sharing the one atomic-write helper. Only
        // clear m_dirty on success, so a failed write is retried on the next save().
        if (AtomicFileWriter::writeFileAtomic(filePath, j.dump(2))) {
            m_dirty = false;
            DEBUG_INFO_F("[StatsManager] Saved stats to %s", filePath.c_str());
        } else {
            DEBUG_WARN_F("[StatsManager] Failed to save stats to %s", filePath.c_str());
        }

    } catch (const std::exception& e) {
        DEBUG_INFO_F("[StatsManager] Error saving stats: %s", e.what());
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
                        // Same sanitization as the normal load path: reject NaN/Inf and
                        // negatives so a corrupt legacy file can't import a poisoned odometer.
                        m_bikeOdometers[bikeName] = (std::max)(finiteOrZero(distanceJson.get<double>()), 0.0);
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
