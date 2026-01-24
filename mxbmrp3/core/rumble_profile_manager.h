// ============================================================================
// core/rumble_profile_manager.h
// Manages per-bike rumble profiles stored in JSON
// ============================================================================
#pragma once

#include "xinput_reader.h"  // For RumbleConfig
#include <string>
#include <unordered_map>
#include <mutex>

class RumbleProfileManager {
public:
    static RumbleProfileManager& getInstance();

    // Lifecycle
    void load(const char* savePath);
    void save();

    // Bike context
    void setCurrentBike(const std::string& bikeName);
    const std::string& getCurrentBike() const;

    // Profile access (returns nullptr if no profile exists for current bike)
    RumbleConfig* getProfileForCurrentBike();
    const RumbleConfig* getProfileForCurrentBike() const;

    // Profile management
    bool hasProfileForCurrentBike() const;
    void createProfileForCurrentBike(const RumbleConfig& baseConfig);

    // Mark profiles as dirty (triggers save on next save point)
    void markDirty();
    bool isDirty() const;

private:
    RumbleProfileManager() = default;
    ~RumbleProfileManager() = default;
    RumbleProfileManager(const RumbleProfileManager&) = delete;
    RumbleProfileManager& operator=(const RumbleProfileManager&) = delete;

    std::string getFilePath() const;

    static constexpr int FILE_VERSION = 1;

    mutable std::mutex m_mutex;
    std::string m_savePath;
    std::string m_currentBikeName;
    bool m_dirty = false;

    std::unordered_map<std::string, RumbleConfig> m_bikeConfigs;
};
