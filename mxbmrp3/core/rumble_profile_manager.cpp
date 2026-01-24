// ============================================================================
// core/rumble_profile_manager.cpp
// Manages per-bike rumble profiles stored in JSON
// ============================================================================
#include "rumble_profile_manager.h"
#include "plugin_utils.h"
#include "../diagnostics/logger.h"
#include "../vendor/nlohmann/json.hpp"

#include <fstream>
#include <cstdio>
#include <windows.h>

// Subdirectory and file name (matches SettingsManager pattern)
static constexpr const char* RUMBLE_SUBDIRECTORY = "mxbmrp3";
static constexpr const char* RUMBLE_FILENAME = "mxbmrp3_rumble_profiles.json";

RumbleProfileManager& RumbleProfileManager::getInstance() {
    static RumbleProfileManager instance;
    return instance;
}

std::string RumbleProfileManager::getFilePath() const {
    std::string path;

    if (m_savePath.empty()) {
        // Use relative path when savePath is not provided
        path = std::string(".\\") + RUMBLE_SUBDIRECTORY;
    } else {
        path = m_savePath;
        if (!path.empty() && path.back() != '/' && path.back() != '\\') {
            path += '\\';
        }
        path += RUMBLE_SUBDIRECTORY;
    }

    // Ensure directory exists
    if (!CreateDirectoryA(path.c_str(), NULL)) {
        DWORD error = GetLastError();
        if (error != ERROR_ALREADY_EXISTS) {
            DEBUG_INFO_F("[RumbleProfileManager] Failed to create directory: %s (error %lu)", path.c_str(), error);
        }
    }

    return path + "\\" + RUMBLE_FILENAME;
}

void RumbleProfileManager::load(const char* savePath) {
    std::lock_guard<std::mutex> lock(m_mutex);

    m_savePath = savePath ? savePath : "";
    m_bikeConfigs.clear();
    m_dirty = false;

    std::string filePath = getFilePath();

    std::ifstream file(filePath);
    if (!file.is_open()) {
        DEBUG_INFO_F("[RumbleProfileManager] No rumble profiles file found at %s", filePath.c_str());
        return;
    }

    try {
        nlohmann::json j;
        file >> j;

        // Check version
        int version = j.value("version", 0);
        if (version != FILE_VERSION) {
            DEBUG_INFO_F("[RumbleProfileManager] Version mismatch: file=%d, expected=%d. Starting fresh.",
                         version, FILE_VERSION);
            return;
        }

        // Parse profiles
        if (j.contains("profiles") && j["profiles"].is_object()) {
            for (auto& [bikeName, profileJson] : j["profiles"].items()) {
                RumbleConfig config;
                // Note: enabled, additiveBlend, rumbleWhenCrashed are NOT stored per-bike
                // They always come from global config in INI

                // Parse effects
                if (profileJson.contains("effects") && profileJson["effects"].is_object()) {
                    auto& effects = profileJson["effects"];

                    auto parseEffect = [&effects](const char* name, RumbleEffect& effect) {
                        if (effects.contains(name) && effects[name].is_object()) {
                            auto& e = effects[name];
                            effect.minInput = e.value("minInput", effect.minInput);
                            effect.maxInput = e.value("maxInput", effect.maxInput);
                            effect.lightStrength = e.value("lightStrength", effect.lightStrength);
                            effect.heavyStrength = e.value("heavyStrength", effect.heavyStrength);
                        }
                    };

                    parseEffect("suspension", config.suspensionEffect);
                    parseEffect("wheelspin", config.wheelspinEffect);
                    parseEffect("brakeLockup", config.brakeLockupEffect);
                    parseEffect("wheelie", config.wheelieEffect);
                    parseEffect("rpm", config.rpmEffect);
                    parseEffect("slide", config.slideEffect);
                    parseEffect("surface", config.surfaceEffect);
                    parseEffect("steer", config.steerEffect);
                }

                m_bikeConfigs[bikeName] = config;
            }
        }

        DEBUG_INFO_F("[RumbleProfileManager] Loaded %zu rumble profiles from %s",
                     m_bikeConfigs.size(), filePath.c_str());

    } catch (const nlohmann::json::exception& e) {
        DEBUG_INFO_F("[RumbleProfileManager] Failed to parse JSON: %s", e.what());
        m_bikeConfigs.clear();
    } catch (const std::exception& e) {
        DEBUG_INFO_F("[RumbleProfileManager] Error loading rumble profiles: %s", e.what());
        m_bikeConfigs.clear();
    }
}

void RumbleProfileManager::save() {
    std::lock_guard<std::mutex> lock(m_mutex);

    // Don't save if not dirty or no profiles exist
    if (!m_dirty && m_bikeConfigs.empty()) {
        return;
    }

    // If no profiles, don't create/update file
    if (m_bikeConfigs.empty()) {
        m_dirty = false;
        return;
    }

    std::string filePath = getFilePath();
    std::string tempPath = filePath + ".tmp";

    try {
        nlohmann::json j;
        j["version"] = FILE_VERSION;

        nlohmann::json profiles = nlohmann::json::object();
        for (const auto& [bikeName, config] : m_bikeConfigs) {
            nlohmann::json profileJson;
            // Note: enabled, additiveBlend, rumbleWhenCrashed are NOT stored per-bike
            // They always come from global config in INI

            auto serializeEffect = [](const RumbleEffect& effect) {
                nlohmann::json e;
                e["minInput"] = effect.minInput;
                e["maxInput"] = effect.maxInput;
                e["lightStrength"] = effect.lightStrength;
                e["heavyStrength"] = effect.heavyStrength;
                return e;
            };

            nlohmann::json effects;
            effects["suspension"] = serializeEffect(config.suspensionEffect);
            effects["wheelspin"] = serializeEffect(config.wheelspinEffect);
            effects["brakeLockup"] = serializeEffect(config.brakeLockupEffect);
            effects["wheelie"] = serializeEffect(config.wheelieEffect);
            effects["rpm"] = serializeEffect(config.rpmEffect);
            effects["slide"] = serializeEffect(config.slideEffect);
            effects["surface"] = serializeEffect(config.surfaceEffect);
            effects["steer"] = serializeEffect(config.steerEffect);

            profileJson["effects"] = effects;
            profiles[bikeName] = profileJson;
        }
        j["profiles"] = profiles;

        // Write to temp file first
        std::ofstream tempFile(tempPath);
        if (!tempFile.is_open()) {
            DEBUG_INFO_F("[RumbleProfileManager] Failed to open temp file for writing: %s", tempPath.c_str());
            return;
        }

        tempFile << j.dump(2);  // Pretty print with 2-space indent
        tempFile.close();

        if (tempFile.fail()) {
            DEBUG_INFO_F("[RumbleProfileManager] Failed to write temp file: %s", tempPath.c_str());
            std::remove(tempPath.c_str());
            return;
        }

        // Atomic rename using Windows API (handles existing file automatically)
        if (!MoveFileExA(tempPath.c_str(), filePath.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
            DEBUG_WARN_F("[RumbleProfileManager] Failed to save file (error %lu): %s", GetLastError(), filePath.c_str());
            std::remove(tempPath.c_str());
            return;
        }

        m_dirty = false;
        DEBUG_INFO_F("[RumbleProfileManager] Saved %zu rumble profiles to %s",
                     m_bikeConfigs.size(), filePath.c_str());

    } catch (const std::exception& e) {
        DEBUG_INFO_F("[RumbleProfileManager] Error saving rumble profiles: %s", e.what());
        std::remove(tempPath.c_str());
    }
}

void RumbleProfileManager::setCurrentBike(const std::string& bikeName) {
    bool needsSave = false;

    {
        std::lock_guard<std::mutex> lock(m_mutex);

        if (m_currentBikeName == bikeName) {
            return;  // No change
        }

        // Check if we need to save previous bike's profile
        needsSave = m_dirty && !m_currentBikeName.empty();

        m_currentBikeName = bikeName;
        DEBUG_INFO_F("[RumbleProfileManager] Current bike set to: %s", bikeName.c_str());
    }

    // Save outside the lock to avoid deadlock
    if (needsSave) {
        save();
    }
}

const std::string& RumbleProfileManager::getCurrentBike() const {
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_currentBikeName;
}

RumbleConfig* RumbleProfileManager::getProfileForCurrentBike() {
    std::lock_guard<std::mutex> lock(m_mutex);

    if (m_currentBikeName.empty()) {
        return nullptr;
    }

    auto it = m_bikeConfigs.find(m_currentBikeName);
    if (it != m_bikeConfigs.end()) {
        return &it->second;
    }

    return nullptr;
}

const RumbleConfig* RumbleProfileManager::getProfileForCurrentBike() const {
    std::lock_guard<std::mutex> lock(m_mutex);

    if (m_currentBikeName.empty()) {
        return nullptr;
    }

    auto it = m_bikeConfigs.find(m_currentBikeName);
    if (it != m_bikeConfigs.end()) {
        return &it->second;
    }

    return nullptr;
}

bool RumbleProfileManager::hasProfileForCurrentBike() const {
    std::lock_guard<std::mutex> lock(m_mutex);

    if (m_currentBikeName.empty()) {
        return false;
    }

    return m_bikeConfigs.find(m_currentBikeName) != m_bikeConfigs.end();
}

void RumbleProfileManager::createProfileForCurrentBike(const RumbleConfig& baseConfig) {
    std::lock_guard<std::mutex> lock(m_mutex);

    if (m_currentBikeName.empty()) {
        DEBUG_INFO("[RumbleProfileManager] Cannot create profile: no bike set");
        return;
    }

    // Copy base config (typically global config from INI)
    m_bikeConfigs[m_currentBikeName] = baseConfig;
    m_dirty = true;

    DEBUG_INFO_F("[RumbleProfileManager] Created profile for bike: %s", m_currentBikeName.c_str());
}

void RumbleProfileManager::markDirty() {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_dirty = true;
}

bool RumbleProfileManager::isDirty() const {
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_dirty;
}
