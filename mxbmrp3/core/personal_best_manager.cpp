// ============================================================================
// core/personal_best_manager.cpp
// Manages persistent storage of personal best lap times per track/bike combo
// ============================================================================
#include "personal_best_manager.h"
#include "plugin_utils.h"
#include "../diagnostics/logger.h"
#include "../vendor/nlohmann/json.hpp"

#include <fstream>
#include <cstdio>
#include <windows.h>

// Subdirectory and file name (matches SettingsManager pattern)
static constexpr const char* PB_SUBDIRECTORY = "mxbmrp3";
static constexpr const char* PB_FILENAME = "mxbmrp3_personal_bests.json";

PersonalBestManager& PersonalBestManager::getInstance() {
    static PersonalBestManager instance;
    return instance;
}

std::string PersonalBestManager::makeKey(const std::string& trackId, const std::string& bikeName) {
    return trackId + "|" + bikeName;
}

std::string PersonalBestManager::getFilePath() const {
    std::string path;

    if (m_savePath.empty()) {
        // Use relative path when savePath is not provided
        path = std::string(".\\") + PB_SUBDIRECTORY;
    } else {
        path = m_savePath;
        if (!path.empty() && path.back() != '/' && path.back() != '\\') {
            path += '\\';
        }
        path += PB_SUBDIRECTORY;
    }

    // Ensure directory exists
    if (!CreateDirectoryA(path.c_str(), NULL)) {
        DWORD error = GetLastError();
        if (error != ERROR_ALREADY_EXISTS) {
            DEBUG_INFO_F("[PersonalBestManager] Failed to create directory: %s (error %lu)", path.c_str(), error);
        }
    }

    return path + "\\" + PB_FILENAME;
}

void PersonalBestManager::load(const char* savePath) {
    std::lock_guard<std::mutex> lock(m_mutex);

    m_savePath = savePath ? savePath : "";
    m_entries.clear();

    std::string filePath = getFilePath();

    std::ifstream file(filePath);
    if (!file.is_open()) {
        DEBUG_INFO_F("[PersonalBestManager] No personal bests file found at %s", filePath.c_str());
        return;
    }

    try {
        nlohmann::json j;
        file >> j;

        // Check version
        int version = j.value("version", 0);
        if (version != FILE_VERSION) {
            DEBUG_INFO_F("[PersonalBestManager] Version mismatch: file=%d, expected=%d. Starting fresh.",
                         version, FILE_VERSION);
            return;
        }

        // Parse entries
        if (j.contains("entries") && j["entries"].is_object()) {
            for (auto& [key, value] : j["entries"].items()) {
                PersonalBestEntry entry;
                entry.trackId = value.value("trackId", "");
                entry.bikeName = value.value("bikeName", "");
                entry.lapTime = value.value("lapTime", -1);
                entry.sector1 = value.value("sector1", -1);
                entry.sector2 = value.value("sector2", -1);
                entry.sector3 = value.value("sector3", -1);
                entry.sector4 = value.value("sector4", -1);
                entry.setupName = value.value("setupName", "");
                entry.conditions = value.value("conditions", -1);
                entry.timestamp = value.value("timestamp", static_cast<std::time_t>(0));

                if (entry.isValid()) {
                    m_entries[key] = entry;
                }
            }
        }

        DEBUG_INFO_F("[PersonalBestManager] Loaded %zu personal bests from %s",
                     m_entries.size(), filePath.c_str());

    } catch (const nlohmann::json::exception& e) {
        DEBUG_INFO_F("[PersonalBestManager] Failed to parse JSON: %s", e.what());
        m_entries.clear();
    } catch (const std::exception& e) {
        DEBUG_INFO_F("[PersonalBestManager] Error loading personal bests: %s", e.what());
        m_entries.clear();
    }
}

void PersonalBestManager::save() {
    std::lock_guard<std::mutex> lock(m_mutex);

    std::string filePath = getFilePath();
    std::string tempPath = filePath + ".tmp";

    try {
        nlohmann::json j;
        j["version"] = FILE_VERSION;

        nlohmann::json entries = nlohmann::json::object();
        for (const auto& [key, entry] : m_entries) {
            nlohmann::json entryJson;
            entryJson["trackId"] = entry.trackId;
            entryJson["bikeName"] = entry.bikeName;
            entryJson["lapTime"] = entry.lapTime;
            entryJson["sector1"] = entry.sector1;
            entryJson["sector2"] = entry.sector2;
            entryJson["sector3"] = entry.sector3;
            entryJson["sector4"] = entry.sector4;
            entryJson["setupName"] = entry.setupName;
            entryJson["conditions"] = entry.conditions;
            entryJson["timestamp"] = entry.timestamp;
            entries[key] = entryJson;
        }
        j["entries"] = entries;

        // Write to temp file first
        std::ofstream tempFile(tempPath);
        if (!tempFile.is_open()) {
            DEBUG_INFO_F("[PersonalBestManager] Failed to open temp file for writing: %s", tempPath.c_str());
            return;
        }

        tempFile << j.dump(2);  // Pretty print with 2-space indent
        tempFile.close();

        if (tempFile.fail()) {
            DEBUG_INFO_F("[PersonalBestManager] Failed to write temp file: %s", tempPath.c_str());
            std::remove(tempPath.c_str());
            return;
        }

        // Atomic rename using Windows API (handles existing file automatically)
        if (!MoveFileExA(tempPath.c_str(), filePath.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
            DEBUG_WARN_F("[PersonalBestManager] Failed to save file (error %lu): %s", GetLastError(), filePath.c_str());
            std::remove(tempPath.c_str());
            return;
        }

        DEBUG_INFO_F("[PersonalBestManager] Saved %zu personal bests to %s",
                     m_entries.size(), filePath.c_str());

    } catch (const std::exception& e) {
        DEBUG_INFO_F("[PersonalBestManager] Error saving personal bests: %s", e.what());
        std::remove(tempPath.c_str());
    }
}

const PersonalBestEntry* PersonalBestManager::getPersonalBest(const std::string& trackId,
                                                               const std::string& bikeName) const {
    std::lock_guard<std::mutex> lock(m_mutex);

    std::string key = makeKey(trackId, bikeName);
    auto it = m_entries.find(key);
    if (it != m_entries.end()) {
        return &it->second;
    }
    return nullptr;
}

bool PersonalBestManager::updatePersonalBest(const PersonalBestEntry& entry) {
    if (!entry.isValid()) {
        return false;
    }

    std::string key = makeKey(entry.trackId, entry.bikeName);

    {
        std::lock_guard<std::mutex> lock(m_mutex);

        // Check if we already have a faster time
        auto it = m_entries.find(key);
        if (it != m_entries.end() && it->second.lapTime <= entry.lapTime) {
            // Existing PB is faster or equal, don't update
            return false;
        }

        // New PB!
        m_entries[key] = entry;

        DEBUG_INFO_F("[PersonalBestManager] New PB for %s|%s: %d ms (sectors: %d/%d/%d)",
                     entry.trackId.c_str(), entry.bikeName.c_str(),
                     entry.lapTime, entry.sector1, entry.sector2, entry.sector3);
    }

    // Save immediately (outside lock to avoid holding it during I/O)
    save();
    return true;
}

bool PersonalBestManager::clearEntry(const std::string& trackId, const std::string& bikeName) {
    std::string key = makeKey(trackId, bikeName);

    {
        std::lock_guard<std::mutex> lock(m_mutex);
        auto it = m_entries.find(key);
        if (it == m_entries.end()) {
            return false;
        }
        m_entries.erase(it);
    }

    save();
    return true;
}

void PersonalBestManager::clearAll() {
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_entries.clear();
    }

    save();
}
