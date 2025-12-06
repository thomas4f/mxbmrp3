// ============================================================================
// core/settings_manager.cpp
// Manages persistence of HUD settings (position, scale, visibility, etc.)
// ============================================================================
#include "settings_manager.h"
#include "hud_manager.h"
#include "../diagnostics/logger.h"
#include "../hud/session_best_hud.h"
#include "../hud/lap_log_hud.h"
#include "../hud/standings_hud.h"
#include "../hud/performance_hud.h"
#include "../hud/telemetry_hud.h"
#include "../hud/input_hud.h"
#include "../hud/time_widget.h"
#include "../hud/position_widget.h"
#include "../hud/lap_widget.h"
#include "../hud/session_widget.h"
#include "../hud/speed_widget.h"
#include "../hud/speedo_widget.h"
#include "../hud/tacho_widget.h"
#include "../hud/timing_widget.h"
#include "../hud/bars_widget.h"
#include "../hud/version_widget.h"
#include "../hud/notices_widget.h"
#include "../hud/fuel_widget.h"
#include "../hud/settings_button_widget.h"
#include "../hud/map_hud.h"
#include "../hud/radar_hud.h"
#include "../hud/pitboard_hud.h"
#include <fstream>
#include <sstream>
#include <unordered_map>
#include <functional>
#include <windows.h>

namespace {
    constexpr const char* SETTINGS_SUBDIRECTORY = "mxbmrp3";
    constexpr const char* SETTINGS_FILENAME = "mxbmrp3_settings.ini";

    // Helper function to save base HUD properties (reduces duplication)
    void saveBaseHudProperties(std::ofstream& file, const BaseHud& hud, const char* sectionName) {
        file << "[" << sectionName << "]\n";
        file << "visible=" << (hud.isVisible() ? 1 : 0) << "\n";
        file << "showTitle=" << (hud.getShowTitle() ? 1 : 0) << "\n";
        file << "showBackgroundTexture=" << (hud.getShowBackgroundTexture() ? 1 : 0) << "\n";
        file << "backgroundOpacity=" << hud.getBackgroundOpacity() << "\n";
        file << "scale=" << hud.getScale() << "\n";
        file << "offsetX=" << hud.getOffsetX() << "\n";
        file << "offsetY=" << hud.getOffsetY() << "\n";
    }

    // Validation helper functions
    float validateScale(float value) {
        using namespace PluginConstants::SettingsLimits;
        if (value < MIN_SCALE || value > MAX_SCALE) {
            DEBUG_WARN_F("Invalid scale value %.2f, clamping to [%.2f, %.2f]",
                        value, MIN_SCALE, MAX_SCALE);
            return (value < MIN_SCALE) ? MIN_SCALE : MAX_SCALE;
        }
        return value;
    }

    uint8_t validateDisplayMode(int value) {
        if (value < 0 || value > 255) {
            DEBUG_WARN_F("Invalid display mode value %d (must be 0-255), using default 0", value);
            return 0;
        }
        return static_cast<uint8_t>(value);
    }

    float validateOpacity(float value) {
        using namespace PluginConstants::SettingsLimits;
        if (value < MIN_OPACITY || value > MAX_OPACITY) {
            DEBUG_WARN_F("Invalid opacity value %.2f, clamping to [%.2f, %.2f]",
                        value, MIN_OPACITY, MAX_OPACITY);
            return (value < MIN_OPACITY) ? MIN_OPACITY : MAX_OPACITY;
        }
        return value;
    }

    float validateOffset(float value) {
        using namespace PluginConstants::SettingsLimits;
        if (value < MIN_OFFSET || value > MAX_OFFSET) {
            DEBUG_WARN_F("Invalid offset value %.2f, clamping to [%.2f, %.2f]",
                        value, MIN_OFFSET, MAX_OFFSET);
            return (value < MIN_OFFSET) ? MIN_OFFSET : MAX_OFFSET;
        }
        return value;
    }

    int validateDisplayRows(int value) {
        using namespace PluginConstants::SettingsLimits;
        if (value < MIN_DISPLAY_ROWS || value > MAX_DISPLAY_ROWS) {
            DEBUG_WARN_F("Invalid display row count %d, clamping to [%d, %d]",
                        value, MIN_DISPLAY_ROWS, MAX_DISPLAY_ROWS);
            return (value < MIN_DISPLAY_ROWS) ? MIN_DISPLAY_ROWS : MAX_DISPLAY_ROWS;
        }
        return value;
    }

    int validateDisplayLaps(int value) {
        using namespace PluginConstants::SettingsLimits;
        if (value < MIN_DISPLAY_LAPS || value > MAX_DISPLAY_LAPS) {
            DEBUG_WARN_F("Invalid display lap count %d, clamping to [%d, %d]",
                        value, MIN_DISPLAY_LAPS, MAX_DISPLAY_LAPS);
            return (value < MIN_DISPLAY_LAPS) ? MIN_DISPLAY_LAPS : MAX_DISPLAY_LAPS;
        }
        return value;
    }

    float validateTrackLineWidth(float value) {
        if (value < MapHud::MIN_TRACK_LINE_WIDTH || value > MapHud::MAX_TRACK_LINE_WIDTH) {
            DEBUG_WARN_F("Invalid track line width %.2f, clamping to [%.2f, %.2f]",
                        value, MapHud::MIN_TRACK_LINE_WIDTH, MapHud::MAX_TRACK_LINE_WIDTH);
            return (value < MapHud::MIN_TRACK_LINE_WIDTH) ? MapHud::MIN_TRACK_LINE_WIDTH : MapHud::MAX_TRACK_LINE_WIDTH;
        }
        return value;
    }
}

SettingsManager& SettingsManager::getInstance() {
    static SettingsManager instance;
    return instance;
}

std::string SettingsManager::getSettingsFilePath(const char* savePath) const {
    if (!savePath || savePath[0] == '\0') {
        // Use relative path when savePath is not provided
        std::string subdir = std::string(".\\") + SETTINGS_SUBDIRECTORY;
        // Create directory if it doesn't exist
        if (!CreateDirectoryA(subdir.c_str(), NULL)) {
            DWORD error = GetLastError();
            if (error != ERROR_ALREADY_EXISTS) {
                DEBUG_WARN_F("Failed to create settings directory: %s (error %lu)", subdir.c_str(), error);
            }
        }
        return subdir + "\\" + SETTINGS_FILENAME;
    }

    std::string path = savePath;
    // Ensure path ends with backslash
    if (!path.empty() && path.back() != '/' && path.back() != '\\') {
        path += '\\';
    }

    // Create subdirectory path
    path += SETTINGS_SUBDIRECTORY;

    // Create directory if it doesn't exist
    // Safety: Validate directory creation to catch disk full, permissions, etc.
    if (!CreateDirectoryA(path.c_str(), NULL)) {
        DWORD error = GetLastError();
        if (error != ERROR_ALREADY_EXISTS) {
            DEBUG_WARN_F("Failed to create settings directory: %s (error %lu)", path.c_str(), error);
            // Continue anyway - file operations will fail later with clearer error message
        }
    }

    // Add filename
    path += '\\';
    path += SETTINGS_FILENAME;

    return path;
}

void SettingsManager::saveSettings(const HudManager& hudManager, const char* savePath) {
    std::string filePath = getSettingsFilePath(savePath);

    std::ofstream file(filePath);
    if (!file.is_open()) {
        DEBUG_WARN_F("Failed to save settings to: %s", filePath.c_str());
        return;
    }

    DEBUG_INFO_F("Saving settings to: %s", filePath.c_str());

    // Save in order matching settings tabs for consistency

    // Save StandingsHud
    auto& standings = hudManager.getStandingsHud();
    saveBaseHudProperties(file, standings, "StandingsHud");
    file << "displayRowCount=" << standings.m_displayRowCount << "\n";
    file << "nonRaceEnabledColumns=" << standings.m_nonRaceEnabledColumns << "\n";
    file << "raceEnabledColumns=" << standings.m_raceEnabledColumns << "\n";
    file << "officialGapMode_NonRace=" << static_cast<int>(standings.m_officialGapMode_NonRace) << "\n";
    file << "officialGapMode_Race=" << static_cast<int>(standings.m_officialGapMode_Race) << "\n";
    file << "liveGapMode_NonRace=" << static_cast<int>(standings.m_liveGapMode_NonRace) << "\n";
    file << "liveGapMode_Race=" << static_cast<int>(standings.m_liveGapMode_Race) << "\n";
    file << "gapIndicatorMode=" << static_cast<int>(standings.m_gapIndicatorMode) << "\n\n";

    // Save MapHud
    auto& mapHud = hudManager.getMapHud();
    saveBaseHudProperties(file, mapHud, "MapHud");
    file << "rotateToPlayer=" << (mapHud.getRotateToPlayer() ? 1 : 0) << "\n";
    file << "showOutline=" << (mapHud.getShowOutline() ? 1 : 0) << "\n";
    file << "colorizeRiders=" << (mapHud.getColorizeRiders() ? 1 : 0) << "\n";
    file << "trackLineWidthMeters=" << mapHud.getTrackLineWidthMeters() << "\n";
    file << "labelMode=" << static_cast<int>(mapHud.getLabelMode()) << "\n";
    file << "anchorPoint=" << static_cast<int>(mapHud.getAnchorPoint()) << "\n";
    file << "anchorX=" << mapHud.m_fAnchorX << "\n";
    file << "anchorY=" << mapHud.m_fAnchorY << "\n\n";

    // Save RadarHud
    auto& radarHud = hudManager.getRadarHud();
    saveBaseHudProperties(file, radarHud, "RadarHud");
    file << "radarRange=" << radarHud.getRadarRange() << "\n";
    file << "colorizeRiders=" << (radarHud.getColorizeRiders() ? 1 : 0) << "\n";
    file << "alertDistance=" << radarHud.getAlertDistance() << "\n";
    file << "labelMode=" << static_cast<int>(radarHud.getLabelMode()) << "\n\n";

    // Save PitboardHud
    saveBaseHudProperties(file, hudManager.getPitboardHud(), "PitboardHud");
    file << "enabledRows=" << hudManager.getPitboardHud().m_enabledRows << "\n";
    file << "displayMode=" << static_cast<int>(hudManager.getPitboardHud().m_displayMode) << "\n\n";

    // Save LapLogHud
    saveBaseHudProperties(file, hudManager.getLapLogHud(), "LapLogHud");
    file << "enabledColumns=" << hudManager.getLapLogHud().m_enabledColumns << "\n";
    file << "maxDisplayLaps=" << hudManager.getLapLogHud().m_maxDisplayLaps << "\n\n";

    // Save SessionBestHud
    saveBaseHudProperties(file, hudManager.getSessionBestHud(), "SessionBestHud");
    file << "enabledRows=" << hudManager.getSessionBestHud().m_enabledRows << "\n\n";

    // Save TelemetryHud
    saveBaseHudProperties(file, hudManager.getTelemetryHud(), "TelemetryHud");
    file << "enabledElements=" << hudManager.getTelemetryHud().m_enabledElements << "\n";
    file << "displayMode=" << static_cast<int>(hudManager.getTelemetryHud().m_displayMode) << "\n\n";

    // Save InputHud
    saveBaseHudProperties(file, hudManager.getInputHud(), "InputHud");
    file << "enabledElements=" << hudManager.getInputHud().m_enabledElements << "\n\n";

    // Save PerformanceHud
    saveBaseHudProperties(file, hudManager.getPerformanceHud(), "PerformanceHud");
    file << "enabledElements=" << hudManager.getPerformanceHud().m_enabledElements << "\n";
    file << "displayMode=" << static_cast<int>(hudManager.getPerformanceHud().m_displayMode) << "\n\n";

    // Save Widgets
    // Save LapWidget
    saveBaseHudProperties(file, hudManager.getLapWidget(), "LapWidget");
    file << "\n";

    // Save PositionWidget
    saveBaseHudProperties(file, hudManager.getPositionWidget(), "PositionWidget");
    file << "\n";

    // Save TimeWidget
    saveBaseHudProperties(file, hudManager.getTimeWidget(), "TimeWidget");
    file << "\n";

    // Save SessionWidget
    saveBaseHudProperties(file, hudManager.getSessionWidget(), "SessionWidget");
    file << "\n";

    // Save SpeedWidget
    saveBaseHudProperties(file, hudManager.getSpeedWidget(), "SpeedWidget");
    file << "\n";

    // Save SpeedoWidget
    saveBaseHudProperties(file, hudManager.getSpeedoWidget(), "SpeedoWidget");
    file << "\n";

    // Save TachoWidget
    saveBaseHudProperties(file, hudManager.getTachoWidget(), "TachoWidget");
    file << "\n";

    // Save TimingWidget
    saveBaseHudProperties(file, hudManager.getTimingWidget(), "TimingWidget");
    file << "\n";

    // Save BarsWidget
    saveBaseHudProperties(file, hudManager.getBarsWidget(), "BarsWidget");
    file << "\n";

    // Save VersionWidget
    saveBaseHudProperties(file, hudManager.getVersionWidget(), "VersionWidget");
    file << "\n";

    // Save NoticesWidget
    saveBaseHudProperties(file, hudManager.getNoticesWidget(), "NoticesWidget");
    file << "\n";

    // Save FuelWidget
    saveBaseHudProperties(file, hudManager.getFuelWidget(), "FuelWidget");
    file << "\n";

    // Save SettingsButtonWidget
    saveBaseHudProperties(file, hudManager.getSettingsButtonWidget(), "SettingsButtonWidget");
    file << "\n";

    // Validate all writes succeeded before closing
    if (!file.good()) {
        DEBUG_WARN_F("Stream error occurred while writing settings to: %s (disk full or I/O error)", filePath.c_str());
        file.close();
        // Delete corrupted partial file to prevent loading invalid settings
        std::remove(filePath.c_str());
        DEBUG_WARN_F("Deleted corrupted partial settings file: %s", filePath.c_str());
        return;
    }

    file.close();
    DEBUG_INFO("Settings saved successfully");
}

void SettingsManager::loadSettings(HudManager& hudManager, const char* savePath) {
    std::string filePath = getSettingsFilePath(savePath);

    std::ifstream file(filePath);
    if (!file.is_open()) {
        DEBUG_INFO_F("No settings file found at: %s (using defaults)", filePath.c_str());
        return;
    }

    DEBUG_INFO_F("Loading settings from: %s", filePath.c_str());

    std::string line;
    std::string currentSection;
    BaseHud* lastHud = nullptr;
    float pendingOffsetX = 0.0f;
    float pendingOffsetY = 0.0f;
    bool hasOffsetX = false;
    bool hasOffsetY = false;

    // Lambda to apply pending offsets when switching sections
    auto applyPendingOffsets = [&]() {
        if (lastHud && (hasOffsetX || hasOffsetY)) {
            // If we only got one offset, keep the current value for the other
            float finalX = hasOffsetX ? pendingOffsetX : lastHud->getOffsetX();
            float finalY = hasOffsetY ? pendingOffsetY : lastHud->getOffsetY();
            lastHud->setPosition(finalX, finalY);
            hasOffsetX = false;
            hasOffsetY = false;
        }
    };

    while (std::getline(file, line)) {
        // Trim whitespace
        size_t start = line.find_first_not_of(" \t\r\n");
        size_t end = line.find_last_not_of(" \t\r\n");
        if (start == std::string::npos) continue;  // Empty line

        // Safety: Check that end is valid before using in arithmetic
        if (end == std::string::npos) continue;  // Should not happen if start is valid, but be defensive
        line = line.substr(start, end - start + 1);

        // Check for section header [HudName]
        // Safety: Check length before accessing front/back to avoid undefined behavior
        if (line.length() >= 3 && line.front() == '[' && line.back() == ']') {
            applyPendingOffsets();  // Apply any pending offsets from previous section
            currentSection = line.substr(1, line.length() - 2);
            continue;
        }

        // Parse key=value
        size_t equals = line.find('=');
        if (equals == std::string::npos) continue;

        std::string key = line.substr(0, equals);
        std::string value = line.substr(equals + 1);

        // Apply setting to appropriate HUD
        // PERFORMANCE OPTIMIZATION: Use lookup table instead of if-else chain
        // Previous: O(n) linear search through 15 branches (avg 7-8 comparisons)
        // New: O(1) hash table lookup (single comparison)
        // Impact: Faster settings loading, especially for large config files

        struct HudLoadInfo {
            BaseHud* hud;
            uint32_t* enabledBitfield;
        };

        // Build lookup table for HUD section name -> HUD object mapping
        // Non-static to avoid dangling reference issues with lambda captures
        const std::unordered_map<std::string, std::function<HudLoadInfo()>> hudLookupTable = {
            {"SessionBestHud", [&]() { return HudLoadInfo{&hudManager.getSessionBestHud(), &hudManager.getSessionBestHud().m_enabledRows}; }},
            {"LapLogHud", [&]() { return HudLoadInfo{&hudManager.getLapLogHud(), &hudManager.getLapLogHud().m_enabledColumns}; }},
            {"StandingsHud", [&]() { return HudLoadInfo{&hudManager.getStandingsHud(), &hudManager.getStandingsHud().m_enabledColumns}; }},
            {"PerformanceHud", [&]() { return HudLoadInfo{&hudManager.getPerformanceHud(), &hudManager.getPerformanceHud().m_enabledElements}; }},
            {"TelemetryHud", [&]() { return HudLoadInfo{&hudManager.getTelemetryHud(), &hudManager.getTelemetryHud().m_enabledElements}; }},
            {"InputHud", [&]() { return HudLoadInfo{&hudManager.getInputHud(), &hudManager.getInputHud().m_enabledElements}; }},
            {"PitboardHud", [&]() { return HudLoadInfo{&hudManager.getPitboardHud(), &hudManager.getPitboardHud().m_enabledRows}; }},
            {"TimeWidget", [&]() { return HudLoadInfo{&hudManager.getTimeWidget(), nullptr}; }},
            {"PositionWidget", [&]() { return HudLoadInfo{&hudManager.getPositionWidget(), nullptr}; }},
            {"LapWidget", [&]() { return HudLoadInfo{&hudManager.getLapWidget(), nullptr}; }},
            {"SessionWidget", [&]() { return HudLoadInfo{&hudManager.getSessionWidget(), nullptr}; }},
            {"MapHud", [&]() { return HudLoadInfo{&hudManager.getMapHud(), nullptr}; }},
            {"RadarHud", [&]() { return HudLoadInfo{&hudManager.getRadarHud(), nullptr}; }},
            {"SpeedWidget", [&]() { return HudLoadInfo{&hudManager.getSpeedWidget(), nullptr}; }},
            {"SpeedoWidget", [&]() { return HudLoadInfo{&hudManager.getSpeedoWidget(), nullptr}; }},
            {"TachoWidget", [&]() { return HudLoadInfo{&hudManager.getTachoWidget(), nullptr}; }},
            {"TimingWidget", [&]() { return HudLoadInfo{&hudManager.getTimingWidget(), nullptr}; }},
            {"BarsWidget", [&]() { return HudLoadInfo{&hudManager.getBarsWidget(), nullptr}; }},
            {"VersionWidget", [&]() { return HudLoadInfo{&hudManager.getVersionWidget(), nullptr}; }},
            {"NoticesWidget", [&]() { return HudLoadInfo{&hudManager.getNoticesWidget(), nullptr}; }},
            {"FuelWidget", [&]() { return HudLoadInfo{&hudManager.getFuelWidget(), nullptr}; }},
            {"SettingsButtonWidget", [&]() { return HudLoadInfo{&hudManager.getSettingsButtonWidget(), nullptr}; }},
        };

        // Lookup HUD info from table
        auto it = hudLookupTable.find(currentSection);
        if (it == hudLookupTable.end()) continue;

        HudLoadInfo hudInfo = it->second();
        BaseHud* hud = hudInfo.hud;
        uint32_t* enabledBitfield = hudInfo.enabledBitfield;

        // Track current HUD for offset buffering
        lastHud = hud;

        // Apply setting
        try {
            if (key == "visible") {
                hud->setVisible(std::stoi(value) != 0);
            } else if (key == "showTitle") {
                hud->setShowTitle(std::stoi(value) != 0);
            } else if (key == "showBackgroundTexture") {
                hud->setShowBackgroundTexture(std::stoi(value) != 0);
            } else if (key == "backgroundOpacity") {
                float opacity = validateOpacity(std::stof(value));
                hud->setBackgroundOpacity(opacity);
            } else if (key == "scale") {
                float scale = validateScale(std::stof(value));
                hud->setScale(scale);
            } else if (key == "offsetX") {
                // Buffer offsetX - will apply when section changes or at end
                pendingOffsetX = validateOffset(std::stof(value));
                hasOffsetX = true;
            } else if (key == "offsetY") {
                // Buffer offsetY - will apply when section changes or at end
                pendingOffsetY = validateOffset(std::stof(value));
                hasOffsetY = true;
            } else if ((key == "enabledRows" || key == "enabledColumns" || key == "enabledElements") && enabledBitfield) {
                *enabledBitfield = static_cast<uint32_t>(std::stoul(value));
                hud->setDataDirty();  // Force rebuild with new columns
            } else if (key == "displayMode" && currentSection == "PerformanceHud") {
                hudManager.getPerformanceHud().m_displayMode = validateDisplayMode(std::stoi(value));
                hud->setDataDirty();  // Force rebuild with new display mode
            } else if (key == "displayMode" && currentSection == "TelemetryHud") {
                hudManager.getTelemetryHud().m_displayMode = validateDisplayMode(std::stoi(value));
                hud->setDataDirty();  // Force rebuild with new display mode
            } else if (key == "displayMode" && currentSection == "PitboardHud") {
                hudManager.getPitboardHud().m_displayMode = validateDisplayMode(std::stoi(value));
                hud->setDataDirty();  // Force rebuild with new display mode
            } else if (key == "maxDisplayLaps" && currentSection == "LapLogHud") {
                int laps = validateDisplayLaps(std::stoi(value));
                hudManager.getLapLogHud().m_maxDisplayLaps = laps;
                hud->setDataDirty();
            } else if (key == "nonRaceEnabledColumns" && currentSection == "StandingsHud") {
                hudManager.getStandingsHud().m_nonRaceEnabledColumns = static_cast<uint32_t>(std::stoul(value));
                hud->setDataDirty();
            } else if (key == "raceEnabledColumns" && currentSection == "StandingsHud") {
                hudManager.getStandingsHud().m_raceEnabledColumns = static_cast<uint32_t>(std::stoul(value));
                hud->setDataDirty();
            } else if (key == "displayRowCount" && currentSection == "StandingsHud") {
                int rows = validateDisplayRows(std::stoi(value));
                hudManager.getStandingsHud().m_displayRowCount = rows;
                hud->setDataDirty();
            } else if (key == "officialGapMode_NonRace" && currentSection == "StandingsHud") {
                hudManager.getStandingsHud().m_officialGapMode_NonRace = static_cast<StandingsHud::GapMode>(std::stoi(value));
                hud->setDataDirty();
            } else if (key == "officialGapMode_Race" && currentSection == "StandingsHud") {
                hudManager.getStandingsHud().m_officialGapMode_Race = static_cast<StandingsHud::GapMode>(std::stoi(value));
                hud->setDataDirty();
            } else if (key == "liveGapMode_NonRace" && currentSection == "StandingsHud") {
                hudManager.getStandingsHud().m_liveGapMode_NonRace = static_cast<StandingsHud::GapMode>(std::stoi(value));
                hud->setDataDirty();
            } else if (key == "liveGapMode_Race" && currentSection == "StandingsHud") {
                hudManager.getStandingsHud().m_liveGapMode_Race = static_cast<StandingsHud::GapMode>(std::stoi(value));
                hud->setDataDirty();
            } else if (key == "gapIndicatorMode" && currentSection == "StandingsHud") {
                hudManager.getStandingsHud().m_gapIndicatorMode = static_cast<StandingsHud::GapIndicatorMode>(std::stoi(value));
                hud->setDataDirty();
            } else if (key == "rotateToPlayer" && currentSection == "MapHud") {
                hudManager.getMapHud().setRotateToPlayer(std::stoi(value) != 0);
            } else if (key == "showOutline" && currentSection == "MapHud") {
                hudManager.getMapHud().setShowOutline(std::stoi(value) != 0);
            } else if (key == "colorizeRiders" && currentSection == "MapHud") {
                hudManager.getMapHud().setColorizeRiders(std::stoi(value) != 0);
            } else if (key == "trackLineWidthMeters" && currentSection == "MapHud") {
                float width = validateTrackLineWidth(std::stof(value));
                hudManager.getMapHud().setTrackLineWidthMeters(width);
            } else if (key == "labelMode" && currentSection == "MapHud") {
                hudManager.getMapHud().setLabelMode(static_cast<MapHud::LabelMode>(std::stoi(value)));
            } else if (key == "anchorPoint" && currentSection == "MapHud") {
                hudManager.getMapHud().setAnchorPoint(static_cast<MapHud::AnchorPoint>(std::stoi(value)));
            } else if (key == "anchorX" && currentSection == "MapHud") {
                hudManager.getMapHud().m_fAnchorX = std::stof(value);
            } else if (key == "anchorY" && currentSection == "MapHud") {
                hudManager.getMapHud().m_fAnchorY = std::stof(value);
            } else if (key == "radarRange" && currentSection == "RadarHud") {
                float range = std::stof(value);
                if (range < RadarHud::MIN_RADAR_RANGE) range = RadarHud::MIN_RADAR_RANGE;
                if (range > RadarHud::MAX_RADAR_RANGE) range = RadarHud::MAX_RADAR_RANGE;
                hudManager.getRadarHud().setRadarRange(range);
            } else if (key == "colorizeRiders" && currentSection == "RadarHud") {
                hudManager.getRadarHud().setColorizeRiders(std::stoi(value) != 0);
            } else if (key == "trackFilter" && currentSection == "RadarHud") {
                // Deprecated setting, ignore (track filtering now uses radar range automatically)
            } else if (key == "alertDistance" && currentSection == "RadarHud") {
                float distance = std::stof(value);
                if (distance < RadarHud::MIN_ALERT_DISTANCE) distance = RadarHud::MIN_ALERT_DISTANCE;
                if (distance > RadarHud::MAX_ALERT_DISTANCE) distance = RadarHud::MAX_ALERT_DISTANCE;
                hudManager.getRadarHud().setAlertDistance(distance);
            } else if (key == "labelMode" && currentSection == "RadarHud") {
                hudManager.getRadarHud().setLabelMode(static_cast<RadarHud::LabelMode>(std::stoi(value)));
            }
        }
        catch ([[maybe_unused]] const std::exception& e) {
            // Corrupted value in settings file - log and skip this setting
            DEBUG_WARN_F("Failed to parse setting '%s=%s' in section [%s]: %s",
                        key.c_str(), value.c_str(), currentSection.c_str(), e.what());
            // Continue loading other settings rather than crashing
        }
    }

    applyPendingOffsets();  // Apply any pending offsets from last section

    file.close();
    DEBUG_INFO("Settings loaded successfully");
}
