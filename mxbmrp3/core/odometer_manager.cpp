// ============================================================================
// core/odometer_manager.cpp
// Manages per-bike odometer data (total distance traveled) stored in JSON
// ============================================================================
#include "odometer_manager.h"
#include "plugin_utils.h"
#include "../diagnostics/logger.h"
#include "../vendor/nlohmann/json.hpp"

#include <fstream>
#include <cstdio>
#include <windows.h>

// Subdirectory and file name (matches SettingsManager pattern)
static constexpr const char* ODOMETER_SUBDIRECTORY = "mxbmrp3";
static constexpr const char* ODOMETER_FILENAME = "mxbmrp3_odometer_data.json";

// Minimum speed to count as movement (filters out noise when stationary)
static constexpr float MIN_MOVEMENT_SPEED_MS = 0.1f;  // ~0.36 km/h

OdometerManager& OdometerManager::getInstance() {
    static OdometerManager instance;
    return instance;
}

std::string OdometerManager::getFilePath() const {
    std::string path;

    if (m_savePath.empty()) {
        // Use relative path when savePath is not provided
        path = std::string(".\\") + ODOMETER_SUBDIRECTORY;
    } else {
        path = m_savePath;
        if (!path.empty() && path.back() != '/' && path.back() != '\\') {
            path += '\\';
        }
        path += ODOMETER_SUBDIRECTORY;
    }

    // Ensure directory exists
    if (!CreateDirectoryA(path.c_str(), NULL)) {
        DWORD error = GetLastError();
        if (error != ERROR_ALREADY_EXISTS) {
            DEBUG_INFO_F("[OdometerManager] Failed to create directory: %s (error %lu)", path.c_str(), error);
        }
    }

    return path + "\\" + ODOMETER_FILENAME;
}

void OdometerManager::load(const char* savePath) {
    std::lock_guard<std::mutex> lock(m_mutex);

    m_savePath = savePath ? savePath : "";
    m_bikeOdometers.clear();
    m_dirty = false;
    m_sessionTripDistance = 0.0;
    m_hasLastUpdateTime = false;

    std::string filePath = getFilePath();

    std::ifstream file(filePath);
    if (!file.is_open()) {
        DEBUG_INFO_F("[OdometerManager] No odometer data file found at %s", filePath.c_str());
        return;
    }

    try {
        nlohmann::json j;
        file >> j;

        // Check version
        int version = j.value("version", 0);
        if (version != FILE_VERSION) {
            DEBUG_INFO_F("[OdometerManager] Version mismatch: file=%d, expected=%d. Starting fresh.",
                         version, FILE_VERSION);
            return;
        }

        // Parse odometers
        if (j.contains("odometers") && j["odometers"].is_object()) {
            for (auto& [bikeName, distanceJson] : j["odometers"].items()) {
                if (distanceJson.is_number()) {
                    m_bikeOdometers[bikeName] = distanceJson.get<double>();
                }
            }
        }

        DEBUG_INFO_F("[OdometerManager] Loaded odometer data for %zu bikes from %s",
                     m_bikeOdometers.size(), filePath.c_str());

    } catch (const nlohmann::json::exception& e) {
        DEBUG_INFO_F("[OdometerManager] Failed to parse JSON: %s", e.what());
        m_bikeOdometers.clear();
    } catch (const std::exception& e) {
        DEBUG_INFO_F("[OdometerManager] Error loading odometer data: %s", e.what());
        m_bikeOdometers.clear();
    }
}

void OdometerManager::save() {
    std::lock_guard<std::mutex> lock(m_mutex);

    // Don't save if not dirty or no data exists
    if (!m_dirty && m_bikeOdometers.empty()) {
        return;
    }

    // If no data, don't create/update file
    if (m_bikeOdometers.empty()) {
        m_dirty = false;
        return;
    }

    std::string filePath = getFilePath();
    std::string tempPath = filePath + ".tmp";

    try {
        nlohmann::json j;
        j["version"] = FILE_VERSION;

        nlohmann::json odometers = nlohmann::json::object();
        for (const auto& [bikeName, distance] : m_bikeOdometers) {
            odometers[bikeName] = distance;
        }
        j["odometers"] = odometers;

        // Write to temp file first
        std::ofstream tempFile(tempPath);
        if (!tempFile.is_open()) {
            DEBUG_INFO_F("[OdometerManager] Failed to open temp file for writing: %s", tempPath.c_str());
            return;
        }

        tempFile << j.dump(2);  // Pretty print with 2-space indent
        tempFile.close();

        if (tempFile.fail()) {
            DEBUG_INFO_F("[OdometerManager] Failed to write temp file: %s", tempPath.c_str());
            std::remove(tempPath.c_str());
            return;
        }

        // Atomic rename using Windows API (handles existing file automatically)
        if (!MoveFileExA(tempPath.c_str(), filePath.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
            DEBUG_WARN_F("[OdometerManager] Failed to save file (error %lu): %s", GetLastError(), filePath.c_str());
            std::remove(tempPath.c_str());
            return;
        }

        m_dirty = false;
        DEBUG_INFO_F("[OdometerManager] Saved odometer data for %zu bikes to %s",
                     m_bikeOdometers.size(), filePath.c_str());

    } catch (const std::exception& e) {
        DEBUG_INFO_F("[OdometerManager] Error saving odometer data: %s", e.what());
        std::remove(tempPath.c_str());
    }
}

void OdometerManager::setCurrentBike(const std::string& bikeName) {
    bool needsSave = false;

    {
        std::lock_guard<std::mutex> lock(m_mutex);

        if (m_currentBikeName == bikeName) {
            return;  // No change
        }

        // Check if we need to save before switching bikes
        needsSave = m_dirty && !m_currentBikeName.empty();

        m_currentBikeName = bikeName;

        // Reset session trip when bike changes
        m_sessionTripDistance = 0.0;
        m_hasLastUpdateTime = false;

        DEBUG_INFO_F("[OdometerManager] Current bike set to: %s", bikeName.c_str());
    }

    // Save outside the lock to avoid deadlock (save() acquires its own lock).
    // Note: This is safe because the game plugin runs single-threaded - no other
    // thread can modify the data between releasing the lock and calling save().
    if (needsSave) {
        save();
    }
}

std::string OdometerManager::getCurrentBike() const {
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_currentBikeName;  // Return by value to avoid data race
}

void OdometerManager::updateDistance(float speedMs) {
    std::lock_guard<std::mutex> lock(m_mutex);

    if (m_currentBikeName.empty()) {
        return;
    }

    auto now = std::chrono::steady_clock::now();

    if (!m_hasLastUpdateTime) {
        // First update - just record the time, don't add distance
        m_lastUpdateTime = now;
        m_hasLastUpdateTime = true;
        return;
    }

    // Calculate time delta in seconds
    auto duration = std::chrono::duration_cast<std::chrono::microseconds>(now - m_lastUpdateTime);
    float deltaTime = duration.count() / 1000000.0f;  // Convert microseconds to seconds
    m_lastUpdateTime = now;

    // Sanity check: skip if delta is too large (e.g., game was paused)
    // At 100Hz telemetry, normal delta is ~0.01s. Allow up to 0.5s for lag spikes.
    if (deltaTime > 0.5f || deltaTime <= 0.0f) {
        return;
    }

    // Only count distance if actually moving
    if (speedMs < MIN_MOVEMENT_SPEED_MS) {
        return;
    }

    // Calculate distance traveled: distance = speed * time
    float distanceMeters = speedMs * deltaTime;

    // Add to session trip
    m_sessionTripDistance += distanceMeters;

    // Add to bike's total odometer
    m_bikeOdometers[m_currentBikeName] += distanceMeters;
    m_dirty = true;
}

double OdometerManager::getOdometerForCurrentBike() const {
    std::lock_guard<std::mutex> lock(m_mutex);

    if (m_currentBikeName.empty()) {
        return 0.0;
    }

    auto it = m_bikeOdometers.find(m_currentBikeName);
    if (it != m_bikeOdometers.end()) {
        return it->second;
    }

    return 0.0;
}

double OdometerManager::getSessionTripDistance() const {
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_sessionTripDistance;
}

double OdometerManager::getTotalOdometer() const {
    std::lock_guard<std::mutex> lock(m_mutex);

    double total = 0.0;
    for (const auto& [bikeName, distance] : m_bikeOdometers) {
        total += distance;
    }
    return total;
}

void OdometerManager::resetSessionTrip() {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_sessionTripDistance = 0.0;
}

void OdometerManager::markDirty() {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_dirty = true;
}

bool OdometerManager::isDirty() const {
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_dirty;
}
