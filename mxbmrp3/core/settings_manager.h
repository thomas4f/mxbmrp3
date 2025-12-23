// ============================================================================
// core/settings_manager.h
// Manages persistence of HUD settings (position, scale, visibility, etc.)
// Supports per-profile settings (Practice, Race, Spectate)
// ============================================================================
#pragma once

#include "profile_manager.h"
#include <array>
#include <string>
#include <unordered_map>
#include <map>

// Forward declarations
class HudManager;

class SettingsManager {
public:
    static SettingsManager& getInstance();

    // Type alias for HUD settings (key -> value map)
    using HudSettings = std::map<std::string, std::string>;

    // Load settings from disk (call during plugin initialization)
    void loadSettings(HudManager& hudManager, const char* savePath);

    // Save settings to disk (call when settings change)
    void saveSettings(const HudManager& hudManager, const char* savePath);

    // Profile switching - captures current HUD state to old profile, applies new profile
    // Returns true if profile actually changed
    bool switchProfile(HudManager& hudManager, ProfileType newProfile);

    // Copy current profile's settings to all other profiles
    void applyToAllProfiles(HudManager& hudManager);

    // Copy current profile's settings to a specific target profile
    void copyToProfile(HudManager& hudManager, ProfileType targetProfile);

    // Capture current HUD state to the active profile's cache
    void captureCurrentState(const HudManager& hudManager);

    // Apply cached settings for active profile to HUDs
    void applyActiveProfile(HudManager& hudManager);

    // Store the save path for later use (set during loadSettings)
    const std::string& getSavePath() const { return m_savePath; }

private:
    SettingsManager() = default;
    ~SettingsManager() = default;
    SettingsManager(const SettingsManager&) = delete;
    SettingsManager& operator=(const SettingsManager&) = delete;

    std::string getSettingsFilePath(const char* savePath) const;

    // Capture HUD state to a specific profile's cache
    void captureToProfile(const HudManager& hudManager, ProfileType profile);

    // Apply a specific profile's cached settings to HUDs
    void applyProfile(HudManager& hudManager, ProfileType profile);

    // Profile settings cache: profile -> hudName -> key -> value
    using ProfileCache = std::unordered_map<std::string, HudSettings>;
    std::array<ProfileCache, static_cast<size_t>(ProfileType::COUNT)> m_profileCache;

    // Track if cache has been initialized (vs empty due to fresh install)
    bool m_cacheInitialized = false;

    // Stored save path for saving on profile switch
    std::string m_savePath;
};
