// ============================================================================
// core/settings_manager.h
// Manages persistence of HUD settings (position, scale, visibility, etc.)
// ============================================================================
#pragma once

#include <string>

// Forward declarations
class HudManager;

class SettingsManager {
public:
    static SettingsManager& getInstance();

    // Load settings from disk (call during plugin initialization)
    void loadSettings(HudManager& hudManager, const char* savePath);

    // Save settings to disk (call when settings change)
    void saveSettings(const HudManager& hudManager, const char* savePath);

private:
    SettingsManager() = default;
    ~SettingsManager() = default;
    SettingsManager(const SettingsManager&) = delete;
    SettingsManager& operator=(const SettingsManager&) = delete;

    std::string getSettingsFilePath(const char* savePath) const;
};
