// ============================================================================
// core/rumble_profile_manager.cpp
// Manages per-bike rumble profiles stored in JSON
// ============================================================================
#include "rumble_profile_manager.h"
#include "atomic_file_writer.h"
#include "plugin_utils.h"
#include "../diagnostics/logger.h"
#include "../vendor/nlohmann/json.hpp"

#include <fstream>
#include <cstdio>
#include <cmath>
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

        // Version is informational only — load known fields with defaults so an
        // older/newer file carries forward rather than being discarded.
        int version = j.value("version", 0);
        if (version != FILE_VERSION) {
            DEBUG_INFO_F("[RumbleProfileManager] File version %d differs from expected %d — loading known fields with defaults",
                         version, FILE_VERSION);
        }

        // Parse profiles
        if (j.contains("profiles") && j["profiles"].is_object()) {
            for (auto& [bikeName, profileJson] : j["profiles"].items()) {
              // Isolate each profile: a single malformed entry must not discard the
              // rest (the outer catch clears m_bikeConfigs entirely).
              try {
                RumbleConfig config;
                // Note: enabled, additiveBlend, rumbleWhenCrashed are NOT stored per-bike
                // They always come from global config in INI

                // Parse effects
                if (profileJson.contains("effects") && profileJson["effects"].is_object()) {
                    auto& effects = profileJson["effects"];

                    // Read defensively: only accept finite numbers, keeping the current
                    // default otherwise. A null/non-number field (e.g. a NaN previously
                    // serialized as `null`) would throw in e.value(...) and abort the whole
                    // load, discarding every bike's tune.
                    auto readFinite = [](const nlohmann::json& obj, const char* key, float current) {
                        if (obj.contains(key) && obj[key].is_number()) {
                            float v = obj[key].get<float>();
                            if (std::isfinite(v)) return v;
                        }
                        return current;
                    };
                    auto parseEffect = [&effects, &readFinite](const char* name, RumbleEffect& effect) {
                        if (effects.contains(name) && effects[name].is_object()) {
                            auto& e = effects[name];
                            effect.minInput = readFinite(e, "minInput", effect.minInput);
                            effect.maxInput = readFinite(e, "maxInput", effect.maxInput);
                            effect.lightStrength = readFinite(e, "lightStrength", effect.lightStrength);
                            effect.heavyStrength = readFinite(e, "heavyStrength", effect.heavyStrength);
                        }
                    };

                    parseEffect("suspension", config.suspensionEffect);
                    parseEffect("suspensionFront", config.suspensionEffectFront);
                    parseEffect("suspensionRear", config.suspensionEffectRear);
                    parseEffect("wheelspin", config.wheelspinEffect);
                    parseEffect("brakeLockup", config.brakeLockupEffect);
                    parseEffect("brakeLockupFront", config.brakeLockupEffectFront);
                    parseEffect("brakeLockupRear", config.brakeLockupEffectRear);
                    parseEffect("wheelie", config.wheelieEffect);
                    parseEffect("rpm", config.rpmEffect);
                    parseEffect("slide", config.slideEffect);
                    parseEffect("surface", config.surfaceEffect);
                    parseEffect("steer", config.steerEffect);
                    parseEffect("revLimiter", config.revLimiterEffect);
                    parseEffect("pitLimiter", config.pitLimiterEffect);
                }

                // Front/rear split flags (default off so older profiles stay linked).
                // Combined/front/rear are independent, so no mirroring is needed.
                config.suspensionSplit = profileJson.value("suspensionSplit", false);
                config.brakeLockupSplit = profileJson.value("brakeLockupSplit", false);
                config.suspensionSplitInitialized = profileJson.value("suspensionSplitInitialized", false);
                config.brakeLockupSplitInitialized = profileJson.value("brakeLockupSplitInitialized", false);

                m_bikeConfigs[bikeName] = config;
              } catch (const std::exception& e) {
                DEBUG_INFO_F("[RumbleProfileManager] Skipping malformed profile '%s': %s",
                             bikeName.c_str(), e.what());
              }
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
    if (!m_dirty) return;

    std::string filePath = getFilePath();

    try {
        nlohmann::json j;
        j["version"] = FILE_VERSION;

        nlohmann::json profiles = nlohmann::json::object();
        for (const auto& [bikeName, config] : m_bikeConfigs) {
            nlohmann::json profileJson;
            // Note: enabled, additiveBlend, rumbleWhenCrashed are NOT stored per-bike
            // They always come from global config in INI

            auto serializeEffect = [](const RumbleEffect& effect) {
                // isfinite-guard: nlohmann serializes NaN/Inf as JSON `null`, which then
                // throws on the next load (null -> float) and wipes EVERY bike's profile.
                // Persist 0 (effect off) for a non-finite value instead.
                auto fin = [](float v) { return std::isfinite(v) ? v : 0.0f; };
                nlohmann::json e;
                e["minInput"] = fin(effect.minInput);
                e["maxInput"] = fin(effect.maxInput);
                e["lightStrength"] = fin(effect.lightStrength);
                e["heavyStrength"] = fin(effect.heavyStrength);
                return e;
            };

            nlohmann::json effects;
            effects["suspension"] = serializeEffect(config.suspensionEffect);
            effects["suspensionFront"] = serializeEffect(config.suspensionEffectFront);
            effects["suspensionRear"] = serializeEffect(config.suspensionEffectRear);
            effects["wheelspin"] = serializeEffect(config.wheelspinEffect);
            effects["brakeLockup"] = serializeEffect(config.brakeLockupEffect);
            effects["brakeLockupFront"] = serializeEffect(config.brakeLockupEffectFront);
            effects["brakeLockupRear"] = serializeEffect(config.brakeLockupEffectRear);
            effects["wheelie"] = serializeEffect(config.wheelieEffect);
            effects["rpm"] = serializeEffect(config.rpmEffect);
            effects["slide"] = serializeEffect(config.slideEffect);
            effects["surface"] = serializeEffect(config.surfaceEffect);
            effects["steer"] = serializeEffect(config.steerEffect);
            effects["revLimiter"] = serializeEffect(config.revLimiterEffect);
            effects["pitLimiter"] = serializeEffect(config.pitLimiterEffect);

            profileJson["effects"] = effects;
            profileJson["suspensionSplit"] = config.suspensionSplit;
            profileJson["brakeLockupSplit"] = config.brakeLockupSplit;
            profileJson["suspensionSplitInitialized"] = config.suspensionSplitInitialized;
            profileJson["brakeLockupSplitInitialized"] = config.brakeLockupSplitInitialized;
            profiles[bikeName] = profileJson;
        }
        j["profiles"] = profiles;

        // Write via the shared atomic writer (temp file + MoveFileExA replace). Synchronous:
        // rumble profiles are saved on discrete edits / bike switches, not the per-frame
        // path — so keep immediate durability while sharing the one atomic-write helper.
        // Only clear the dirty flag on a successful write.
        if (AtomicFileWriter::writeFileAtomic(filePath, j.dump(2))) {
            m_dirty = false;
            DEBUG_INFO_F("[RumbleProfileManager] Saved %zu rumble profiles to %s",
                         m_bikeConfigs.size(), filePath.c_str());
        } else {
            DEBUG_WARN_F("[RumbleProfileManager] Failed to save rumble profiles to %s", filePath.c_str());
        }

    } catch (const std::exception& e) {
        DEBUG_INFO_F("[RumbleProfileManager] Error saving rumble profiles: %s", e.what());
    }
}

void RumbleProfileManager::setCurrentBike(const std::string& bikeName) {
    if (m_currentBikeName == bikeName) {
        return;  // No change
    }

    // Save previous bike's profile if dirty
    if (m_dirty && !m_currentBikeName.empty()) {
        save();
    }

    m_currentBikeName = bikeName;
    DEBUG_INFO_F("[RumbleProfileManager] Current bike set to: %s", bikeName.c_str());
}

const std::string& RumbleProfileManager::getCurrentBike() const {
    return m_currentBikeName;
}

RumbleConfig* RumbleProfileManager::getProfileForCurrentBike() {
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
    if (m_currentBikeName.empty()) {
        return false;
    }

    return m_bikeConfigs.find(m_currentBikeName) != m_bikeConfigs.end();
}

void RumbleProfileManager::createProfileForCurrentBike(const RumbleConfig& baseConfig) {
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
    m_dirty = true;
}

bool RumbleProfileManager::isDirty() const {
    return m_dirty;
}
