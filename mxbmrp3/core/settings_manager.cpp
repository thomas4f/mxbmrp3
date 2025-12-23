// ============================================================================
// core/settings_manager.cpp
// Manages persistence of HUD settings (position, scale, visibility, etc.)
// Supports per-profile settings (Practice, Race, Spectate)
// ============================================================================
#include "settings_manager.h"
#include "hud_manager.h"
#include "profile_manager.h"
#include "../diagnostics/logger.h"
#include "../hud/ideal_lap_hud.h"
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
#include "../hud/timing_hud.h"
#include "../hud/gap_bar_hud.h"
#include "../hud/bars_widget.h"
#include "../hud/version_widget.h"
#include "../hud/notices_widget.h"
#include "../hud/fuel_widget.h"
#include "../hud/settings_button_widget.h"
#include "../hud/pointer_widget.h"
#include "../hud/map_hud.h"
#include "../hud/radar_hud.h"
#include "../hud/pitboard_hud.h"
#include "../hud/records_hud.h"
#include "../hud/rumble_hud.h"
#include "color_config.h"
#include "font_config.h"
#include "update_checker.h"
#include "xinput_reader.h"
#include "hotkey_manager.h"
#include "tracked_riders_manager.h"
#include <fstream>
#include <sstream>
#include <array>
#include <windows.h>

namespace {
    constexpr const char* SETTINGS_SUBDIRECTORY = "mxbmrp3";
    constexpr const char* SETTINGS_FILENAME = "mxbmrp3_settings.ini";

    // ========================================================================
    // Settings Key Constants
    // Centralizes all INI file keys to prevent typos and enable refactoring
    // ========================================================================
    namespace Keys {
        // Base HUD properties (shared across all HUDs)
        namespace Base {
            constexpr const char* VISIBLE = "visible";
            constexpr const char* SHOW_TITLE = "showTitle";
            constexpr const char* SHOW_BG_TEXTURE = "showBackgroundTexture";
            constexpr const char* TEXTURE_VARIANT = "textureVariant";
            constexpr const char* BG_OPACITY = "backgroundOpacity";
            constexpr const char* SCALE = "scale";
            constexpr const char* OFFSET_X = "offsetX";
            constexpr const char* OFFSET_Y = "offsetY";
        }

        // Shared keys used by multiple HUDs
        namespace Common {
            constexpr const char* ENABLED_COLUMNS = "enabledColumns";
            constexpr const char* ENABLED_ROWS = "enabledRows";
            constexpr const char* ENABLED_ELEMENTS = "enabledElements";
            constexpr const char* DISPLAY_MODE = "displayMode";
            constexpr const char* LABEL_MODE = "labelMode";
            constexpr const char* RIDER_COLOR_MODE = "riderColorMode";
            constexpr const char* RIDER_SHAPE = "riderShape";
        }

        // StandingsHud-specific keys
        namespace Standings {
            constexpr const char* DISPLAY_ROW_COUNT = "displayRowCount";
            constexpr const char* OFFICIAL_GAP_MODE = "officialGapMode";
            constexpr const char* LIVE_GAP_MODE = "liveGapMode";
            constexpr const char* GAP_INDICATOR_MODE = "gapIndicatorMode";
            constexpr const char* GAP_REFERENCE_MODE = "gapReferenceMode";
        }

        // MapHud-specific keys
        namespace Map {
            constexpr const char* ROTATE_TO_PLAYER = "rotateToPlayer";
            constexpr const char* SHOW_OUTLINE = "showOutline";
            constexpr const char* TRACK_WIDTH_SCALE = "trackWidthScale";
            constexpr const char* TRACK_LINE_WIDTH = "trackLineWidthMeters";  // Legacy key (meters)
            constexpr const char* ANCHOR_POINT = "anchorPoint";
            constexpr const char* ANCHOR_X = "anchorX";
            constexpr const char* ANCHOR_Y = "anchorY";
            constexpr const char* ZOOM_ENABLED = "zoomEnabled";
            constexpr const char* ZOOM_DISTANCE = "zoomDistance";
            constexpr const char* COLORIZE_RIDERS = "colorizeRiders";  // Legacy key
        }

        // RadarHud-specific keys
        namespace Radar {
            constexpr const char* RADAR_RANGE = "radarRange";
            constexpr const char* SHOW_PLAYER_ARROW = "showPlayerArrow";
            constexpr const char* FADE_WHEN_EMPTY = "fadeWhenEmpty";
            constexpr const char* ALERT_DISTANCE = "alertDistance";
        }

        // RecordsHud-specific keys
        namespace Records {
            constexpr const char* PROVIDER = "provider";
            constexpr const char* RECORDS_TO_SHOW = "recordsToShow";
        }

        // LapLogHud-specific keys
        namespace LapLog {
            constexpr const char* MAX_DISPLAY_LAPS = "maxDisplayLaps";
        }

        // TimingHud-specific keys
        namespace Timing {
            constexpr const char* TIME_MODE = "timeMode";
            constexpr const char* GAP_MODE = "gapMode";
            constexpr const char* DISPLAY_DURATION = "displayDuration";
        }

        // SpeedWidget-specific keys
        namespace Speed {
            constexpr const char* SPEED_UNIT = "speedUnit";
        }

        // FuelWidget-specific keys
        namespace Fuel {
            constexpr const char* FUEL_UNIT = "fuelUnit";
        }

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

    float validateTrackWidthScale(float value) {
        if (value < MapHud::MIN_TRACK_WIDTH_SCALE || value > MapHud::MAX_TRACK_WIDTH_SCALE) {
            DEBUG_WARN_F("Invalid track width scale %.2f, clamping to [%.2f, %.2f]",
                        value, MapHud::MIN_TRACK_WIDTH_SCALE, MapHud::MAX_TRACK_WIDTH_SCALE);
            return (value < MapHud::MIN_TRACK_WIDTH_SCALE) ? MapHud::MIN_TRACK_WIDTH_SCALE : MapHud::MAX_TRACK_WIDTH_SCALE;
        }
        return value;
    }

    float validateZoomDistance(float value) {
        if (value < MapHud::MIN_ZOOM_DISTANCE || value > MapHud::MAX_ZOOM_DISTANCE) {
            DEBUG_WARN_F("Invalid zoom distance %.2f, clamping to [%.2f, %.2f]",
                        value, MapHud::MIN_ZOOM_DISTANCE, MapHud::MAX_ZOOM_DISTANCE);
            return (value < MapHud::MIN_ZOOM_DISTANCE) ? MapHud::MIN_ZOOM_DISTANCE : MapHud::MAX_ZOOM_DISTANCE;
        }
        return value;
    }

    // Helper to format a section name with profile index
    std::string formatSectionName(const char* hudName, ProfileType profile) {
        return std::string(hudName) + ":" + std::to_string(static_cast<int>(profile));
    }

    // Parse section name to extract HUD name and profile index
    // Returns true if successfully parsed, false if no profile index (global section)
    bool parseSectionName(const std::string& section, std::string& hudName, int& profileIndex) {
        size_t colonPos = section.find(':');
        if (colonPos == std::string::npos) {
            hudName = section;
            profileIndex = -1;  // Global section
            return false;
        }
        hudName = section.substr(0, colonPos);
        try {
            profileIndex = std::stoi(section.substr(colonPos + 1));
            return true;
        } catch (...) {
            hudName = section;
            profileIndex = -1;
            return false;
        }
    }

    // Helper to capture base HUD properties to a settings map
    void captureBaseHudSettings(SettingsManager::HudSettings& settings, const BaseHud& hud) {
        using namespace Keys::Base;
        settings[VISIBLE] = std::to_string(hud.isVisible() ? 1 : 0);
        settings[SHOW_TITLE] = std::to_string(hud.getShowTitle() ? 1 : 0);
        settings[SHOW_BG_TEXTURE] = std::to_string(hud.getShowBackgroundTexture() ? 1 : 0);
        settings[TEXTURE_VARIANT] = std::to_string(hud.getTextureVariant());
        settings[BG_OPACITY] = std::to_string(hud.getBackgroundOpacity());
        settings[SCALE] = std::to_string(hud.getScale());
        settings[OFFSET_X] = std::to_string(hud.getOffsetX());
        settings[OFFSET_Y] = std::to_string(hud.getOffsetY());
    }

    // Helper to write base HUD properties to file
    void writeBaseHudSettings(std::ofstream& file, const SettingsManager::HudSettings& settings) {
        using namespace Keys::Base;
        static const std::array<const char*, 7> baseKeys = {
            VISIBLE, SHOW_TITLE, SHOW_BG_TEXTURE, BG_OPACITY,
            SCALE, OFFSET_X, OFFSET_Y
        };
        for (const auto& key : baseKeys) {
            auto it = settings.find(key);
            if (it != settings.end()) {
                file << key << "=" << it->second << "\n";
            }
        }
    }

    // Helper to check if a key is a base HUD property
    bool isBaseKey(const std::string& key) {
        using namespace Keys::Base;
        return key == VISIBLE || key == SHOW_TITLE || key == SHOW_BG_TEXTURE ||
               key == BG_OPACITY || key == SCALE || key == OFFSET_X || key == OFFSET_Y;
    }

    // Helper to apply base HUD settings from a map
    void applyBaseHudSettings(BaseHud& hud, const SettingsManager::HudSettings& settings) {
        using namespace Keys::Base;
        float pendingOffsetX = 0, pendingOffsetY = 0;
        bool hasOffsetX = false, hasOffsetY = false;

        for (const auto& [key, value] : settings) {
            try {
                if (key == VISIBLE) {
                    hud.setVisible(std::stoi(value) != 0);
                } else if (key == SHOW_TITLE) {
                    hud.setShowTitle(std::stoi(value) != 0);
                } else if (key == SHOW_BG_TEXTURE) {
                    hud.setShowBackgroundTexture(std::stoi(value) != 0);
                } else if (key == TEXTURE_VARIANT) {
                    hud.setTextureVariant(std::stoi(value));
                } else if (key == BG_OPACITY) {
                    hud.setBackgroundOpacity(validateOpacity(std::stof(value)));
                } else if (key == SCALE) {
                    hud.setScale(validateScale(std::stof(value)));
                } else if (key == OFFSET_X) {
                    pendingOffsetX = validateOffset(std::stof(value));
                    hasOffsetX = true;
                } else if (key == OFFSET_Y) {
                    pendingOffsetY = validateOffset(std::stof(value));
                    hasOffsetY = true;
                }
            } catch (...) {
                DEBUG_WARN_F("Failed to parse base setting '%s=%s'", key.c_str(), value.c_str());
            }
        }
        // Apply buffered position
        if (hasOffsetX || hasOffsetY) {
            float finalX = hasOffsetX ? pendingOffsetX : hud.getOffsetX();
            float finalY = hasOffsetY ? pendingOffsetY : hud.getOffsetY();
            hud.setPosition(finalX, finalY);
        }
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
        if (!CreateDirectoryA(subdir.c_str(), NULL)) {
            DWORD error = GetLastError();
            if (error != ERROR_ALREADY_EXISTS) {
                DEBUG_WARN_F("Failed to create settings directory: %s (error %lu)", subdir.c_str(), error);
            }
        }
        return subdir + "\\" + SETTINGS_FILENAME;
    }

    std::string path = savePath;
    if (!path.empty() && path.back() != '/' && path.back() != '\\') {
        path += '\\';
    }
    path += SETTINGS_SUBDIRECTORY;

    if (!CreateDirectoryA(path.c_str(), NULL)) {
        DWORD error = GetLastError();
        if (error != ERROR_ALREADY_EXISTS) {
            DEBUG_WARN_F("Failed to create settings directory: %s (error %lu)", path.c_str(), error);
        }
    }

    path += '\\';
    path += SETTINGS_FILENAME;
    return path;
}

void SettingsManager::captureCurrentState(const HudManager& hudManager) {
    ProfileType activeProfile = ProfileManager::getInstance().getActiveProfile();
    captureToProfile(hudManager, activeProfile);
}

void SettingsManager::captureToProfile(const HudManager& hudManager, ProfileType profile) {
    if (profile >= ProfileType::COUNT) {
        DEBUG_WARN_F("captureToProfile called with invalid profile index: %d", static_cast<int>(profile));
        return;
    }

    ProfileCache& cache = m_profileCache[static_cast<size_t>(profile)];
    cache.clear();

    // Capture StandingsHud
    {
        using namespace Keys::Standings;
        HudSettings settings;
        const auto& hud = hudManager.getStandingsHud();
        captureBaseHudSettings(settings, hud);
        settings[DISPLAY_ROW_COUNT] = std::to_string(hud.m_displayRowCount);
        settings[Keys::Common::ENABLED_COLUMNS] = std::to_string(hud.m_enabledColumns);
        settings[OFFICIAL_GAP_MODE] = std::to_string(static_cast<int>(hud.m_officialGapMode));
        settings[LIVE_GAP_MODE] = std::to_string(static_cast<int>(hud.m_liveGapMode));
        settings[GAP_INDICATOR_MODE] = std::to_string(static_cast<int>(hud.m_gapIndicatorMode));
        settings[GAP_REFERENCE_MODE] = std::to_string(static_cast<int>(hud.m_gapReferenceMode));
        cache["StandingsHud"] = std::move(settings);
    }

    // Capture MapHud
    {
        HudSettings settings;
        const auto& hud = hudManager.getMapHud();
        captureBaseHudSettings(settings, hud);
        settings["rotateToPlayer"] = std::to_string(hud.getRotateToPlayer() ? 1 : 0);
        settings["showOutline"] = std::to_string(hud.getShowOutline() ? 1 : 0);
        settings["riderColorMode"] = std::to_string(static_cast<int>(hud.getRiderColorMode()));
        settings["trackWidthScale"] = std::to_string(hud.getTrackWidthScale());
        settings["labelMode"] = std::to_string(static_cast<int>(hud.getLabelMode()));
        settings["riderShape"] = std::to_string(static_cast<int>(hud.getRiderShape()));
        settings["anchorPoint"] = std::to_string(static_cast<int>(hud.getAnchorPoint()));
        settings["anchorX"] = std::to_string(hud.m_fAnchorX);
        settings["anchorY"] = std::to_string(hud.m_fAnchorY);
        settings["zoomEnabled"] = std::to_string(hud.getZoomEnabled() ? 1 : 0);
        settings["zoomDistance"] = std::to_string(hud.getZoomDistance());
        settings["markerScale"] = std::to_string(hud.getMarkerScale());
        cache["MapHud"] = std::move(settings);
    }

    // Capture RadarHud
    {
        HudSettings settings;
        const auto& hud = hudManager.getRadarHud();
        captureBaseHudSettings(settings, hud);
        settings["radarRange"] = std::to_string(hud.getRadarRange());
        settings["riderColorMode"] = std::to_string(static_cast<int>(hud.getRiderColorMode()));
        settings["showPlayerArrow"] = std::to_string(hud.getShowPlayerArrow() ? 1 : 0);
        settings["fadeWhenEmpty"] = std::to_string(hud.getFadeWhenEmpty() ? 1 : 0);
        settings["alertDistance"] = std::to_string(hud.getAlertDistance());
        settings["labelMode"] = std::to_string(static_cast<int>(hud.getLabelMode()));
        settings["riderShape"] = std::to_string(static_cast<int>(hud.getRiderShape()));
        settings["markerScale"] = std::to_string(hud.getMarkerScale());
        cache["RadarHud"] = std::move(settings);
    }

    // Capture PitboardHud
    {
        HudSettings settings;
        const auto& hud = hudManager.getPitboardHud();
        captureBaseHudSettings(settings, hud);
        settings["enabledRows"] = std::to_string(hud.m_enabledRows);
        settings["displayMode"] = std::to_string(static_cast<int>(hud.m_displayMode));
        cache["PitboardHud"] = std::move(settings);
    }

    // Capture RecordsHud
    {
        HudSettings settings;
        const auto& hud = hudManager.getRecordsHud();
        captureBaseHudSettings(settings, hud);
        settings["provider"] = std::to_string(static_cast<int>(hud.m_provider));
        settings["enabledColumns"] = std::to_string(hud.m_enabledColumns);
        settings["recordsToShow"] = std::to_string(hud.m_recordsToShow);
        cache["RecordsHud"] = std::move(settings);
    }

    // Capture LapLogHud
    {
        HudSettings settings;
        const auto& hud = hudManager.getLapLogHud();
        captureBaseHudSettings(settings, hud);
        settings["enabledColumns"] = std::to_string(hud.m_enabledColumns);
        settings["maxDisplayLaps"] = std::to_string(hud.m_maxDisplayLaps);
        cache["LapLogHud"] = std::move(settings);
    }

    // Capture IdealLapHud (key preserved for backward compatibility)
    {
        HudSettings settings;
        const auto& hud = hudManager.getIdealLapHud();
        captureBaseHudSettings(settings, hud);
        settings["enabledRows"] = std::to_string(hud.m_enabledRows);
        cache["IdealLapHud"] = std::move(settings);
    }

    // Capture TelemetryHud
    {
        HudSettings settings;
        const auto& hud = hudManager.getTelemetryHud();
        captureBaseHudSettings(settings, hud);
        settings["enabledElements"] = std::to_string(hud.m_enabledElements);
        settings["displayMode"] = std::to_string(static_cast<int>(hud.m_displayMode));
        cache["TelemetryHud"] = std::move(settings);
    }

    // Capture InputHud
    {
        HudSettings settings;
        const auto& hud = hudManager.getInputHud();
        captureBaseHudSettings(settings, hud);
        settings["enabledElements"] = std::to_string(hud.m_enabledElements);
        cache["InputHud"] = std::move(settings);
    }

    // Capture PerformanceHud
    {
        HudSettings settings;
        const auto& hud = hudManager.getPerformanceHud();
        captureBaseHudSettings(settings, hud);
        settings["enabledElements"] = std::to_string(hud.m_enabledElements);
        settings["displayMode"] = std::to_string(static_cast<int>(hud.m_displayMode));
        cache["PerformanceHud"] = std::move(settings);
    }

    // Capture Widgets (base properties only for most)
    auto captureWidget = [&](const char* name, const BaseHud& hud) {
        HudSettings settings;
        captureBaseHudSettings(settings, hud);
        cache[name] = std::move(settings);
    };

    captureWidget("LapWidget", hudManager.getLapWidget());
    captureWidget("PositionWidget", hudManager.getPositionWidget());
    captureWidget("TimeWidget", hudManager.getTimeWidget());
    captureWidget("SessionWidget", hudManager.getSessionWidget());
    captureWidget("SpeedoWidget", hudManager.getSpeedoWidget());
    captureWidget("TachoWidget", hudManager.getTachoWidget());
    captureWidget("BarsWidget", hudManager.getBarsWidget());
    captureWidget("VersionWidget", hudManager.getVersionWidget());
    captureWidget("NoticesWidget", hudManager.getNoticesWidget());
    captureWidget("SettingsButtonWidget", hudManager.getSettingsButtonWidget());
    captureWidget("PointerWidget", hudManager.getPointerWidget());
    captureWidget("RumbleHud", hudManager.getRumbleHud());

    // SpeedWidget has enabledRows - speedUnit is now global (in Preferences section)
    {
        HudSettings settings;
        const auto& hud = hudManager.getSpeedWidget();
        captureBaseHudSettings(settings, hud);
        settings["enabledRows"] = std::to_string(hud.m_enabledRows);
        cache["SpeedWidget"] = std::move(settings);
    }

    // FuelWidget has enabledRows - fuelUnit is now global (in Preferences section)
    {
        HudSettings settings;
        const auto& hud = hudManager.getFuelWidget();
        captureBaseHudSettings(settings, hud);
        settings["enabledRows"] = std::to_string(hud.m_enabledRows);
        cache["FuelWidget"] = std::move(settings);
    }

    // TimingHud has per-column modes and displayDuration
    {
        HudSettings settings;
        const auto& hud = hudManager.getTimingHud();
        captureBaseHudSettings(settings, hud);
        settings["labelMode"] = std::to_string(static_cast<int>(hud.m_columnModes[TimingHud::COL_LABEL]));
        settings["timeMode"] = std::to_string(static_cast<int>(hud.m_columnModes[TimingHud::COL_TIME]));
        settings["gapMode"] = std::to_string(static_cast<int>(hud.m_columnModes[TimingHud::COL_GAP]));
        settings["displayDuration"] = std::to_string(hud.m_displayDurationMs);
        settings["gapTypes"] = std::to_string(hud.getGapTypes());
        cache["TimingHud"] = std::move(settings);
    }

    // GapBarHud with freeze, marker, and range settings
    {
        HudSettings settings;
        const auto& hud = hudManager.getGapBarHud();
        captureBaseHudSettings(settings, hud);
        settings["freezeDuration"] = std::to_string(hud.m_freezeDurationMs);
        settings["showMarkers"] = hud.m_showMarkers ? "1" : "0";
        settings["gapRange"] = std::to_string(hud.m_gapRangeMs);
        settings["barWidth"] = std::to_string(hud.m_barWidthPercent);
        cache["GapBarHud"] = std::move(settings);
    }

    // Note: ColorConfig is now global (not per-profile) - saved/loaded in saveSettings/loadSettings directly

    m_cacheInitialized = true;
}

void SettingsManager::applyActiveProfile(HudManager& hudManager) {
    ProfileType activeProfile = ProfileManager::getInstance().getActiveProfile();
    applyProfile(hudManager, activeProfile);
}

void SettingsManager::applyProfile(HudManager& hudManager, ProfileType profile) {
    if (profile >= ProfileType::COUNT) {
        DEBUG_WARN_F("applyProfile called with invalid profile index: %d", static_cast<int>(profile));
        return;
    }
    if (!m_cacheInitialized) {
        DEBUG_INFO("applyProfile skipped - cache not yet initialized (normal during first load)");
        return;
    }

    const ProfileCache& cache = m_profileCache[static_cast<size_t>(profile)];

    // Helper to apply settings to a HUD
    auto applyToHud = [&](const char* hudName, BaseHud& hud) {
        auto it = cache.find(hudName);
        if (it == cache.end()) return;
        applyBaseHudSettings(hud, it->second);
        hud.setDataDirty();
    };

    // Apply StandingsHud
    {
        using namespace Keys::Standings;
        auto it = cache.find("StandingsHud");
        if (it != cache.end()) {
            auto& hud = hudManager.getStandingsHud();
            applyBaseHudSettings(hud, it->second);

            const auto& settings = it->second;
            try {
                if (settings.count(DISPLAY_ROW_COUNT)) hud.m_displayRowCount = validateDisplayRows(std::stoi(settings.at(DISPLAY_ROW_COUNT)));
                if (settings.count(Keys::Common::ENABLED_COLUMNS)) hud.m_enabledColumns = static_cast<uint32_t>(std::stoul(settings.at(Keys::Common::ENABLED_COLUMNS)));
                if (settings.count(OFFICIAL_GAP_MODE)) hud.m_officialGapMode = static_cast<StandingsHud::GapMode>(std::stoi(settings.at(OFFICIAL_GAP_MODE)));
                if (settings.count(LIVE_GAP_MODE)) hud.m_liveGapMode = static_cast<StandingsHud::GapMode>(std::stoi(settings.at(LIVE_GAP_MODE)));
                if (settings.count(GAP_INDICATOR_MODE)) hud.m_gapIndicatorMode = static_cast<StandingsHud::GapIndicatorMode>(std::stoi(settings.at(GAP_INDICATOR_MODE)));
                if (settings.count(GAP_REFERENCE_MODE)) hud.m_gapReferenceMode = static_cast<StandingsHud::GapReferenceMode>(std::stoi(settings.at(GAP_REFERENCE_MODE)));
            } catch (...) {}
            hud.setDataDirty();
        }
    }

    // Apply MapHud
    {
        auto it = cache.find("MapHud");
        if (it != cache.end()) {
            auto& hud = hudManager.getMapHud();
            applyBaseHudSettings(hud, it->second);

            const auto& settings = it->second;
            try {
                if (settings.count("rotateToPlayer")) hud.setRotateToPlayer(std::stoi(settings.at("rotateToPlayer")) != 0);
                if (settings.count("showOutline")) hud.setShowOutline(std::stoi(settings.at("showOutline")) != 0);
                // New riderColorMode key; fallback to old colorizeRiders for backwards compatibility
                if (settings.count("riderColorMode")) {
                    hud.setRiderColorMode(static_cast<MapHud::RiderColorMode>(std::stoi(settings.at("riderColorMode"))));
                } else if (settings.count("colorizeRiders")) {
                    // Legacy: colorizeRiders=0 -> UNIFORM, colorizeRiders=1 -> BRAND
                    hud.setRiderColorMode(std::stoi(settings.at("colorizeRiders")) != 0
                        ? MapHud::RiderColorMode::BRAND : MapHud::RiderColorMode::UNIFORM);
                }
                // New trackWidthScale key; fallback to old trackLineWidthMeters for backwards compatibility
                if (settings.count("trackWidthScale")) {
                    hud.setTrackWidthScale(validateTrackWidthScale(std::stof(settings.at("trackWidthScale"))));
                } else if (settings.count("trackLineWidthMeters")) {
                    // Legacy: convert meters to scale factor (old default 10m = 1.0 scale)
                    float legacyMeters = std::stof(settings.at("trackLineWidthMeters"));
                    float scale = legacyMeters / 10.0f;  // 10m was the old default
                    hud.setTrackWidthScale(validateTrackWidthScale(scale));
                }
                if (settings.count("labelMode")) hud.setLabelMode(static_cast<MapHud::LabelMode>(std::stoi(settings.at("labelMode"))));
                if (settings.count("riderShape")) hud.setRiderShape(static_cast<MapHud::RiderShape>(std::stoi(settings.at("riderShape"))));
                if (settings.count("anchorPoint")) hud.setAnchorPoint(static_cast<MapHud::AnchorPoint>(std::stoi(settings.at("anchorPoint"))));
                if (settings.count("anchorX")) hud.m_fAnchorX = std::stof(settings.at("anchorX"));
                if (settings.count("anchorY")) hud.m_fAnchorY = std::stof(settings.at("anchorY"));
                if (settings.count("zoomEnabled")) hud.setZoomEnabled(std::stoi(settings.at("zoomEnabled")) != 0);
                if (settings.count("zoomDistance")) hud.setZoomDistance(validateZoomDistance(std::stof(settings.at("zoomDistance"))));
                if (settings.count("markerScale")) hud.setMarkerScale(std::stof(settings.at("markerScale")));
            } catch (...) {}
            hud.setDataDirty();
        }
    }

    // Apply RadarHud
    {
        auto it = cache.find("RadarHud");
        if (it != cache.end()) {
            auto& hud = hudManager.getRadarHud();
            applyBaseHudSettings(hud, it->second);

            const auto& settings = it->second;
            try {
                if (settings.count("radarRange")) {
                    float range = std::stof(settings.at("radarRange"));
                    if (range < RadarHud::MIN_RADAR_RANGE) range = RadarHud::MIN_RADAR_RANGE;
                    if (range > RadarHud::MAX_RADAR_RANGE) range = RadarHud::MAX_RADAR_RANGE;
                    hud.setRadarRange(range);
                }
                // New riderColorMode key; fallback to old colorizeRiders for backwards compatibility
                if (settings.count("riderColorMode")) {
                    hud.setRiderColorMode(static_cast<RadarHud::RiderColorMode>(std::stoi(settings.at("riderColorMode"))));
                } else if (settings.count("colorizeRiders")) {
                    // Legacy: colorizeRiders=0 -> UNIFORM, colorizeRiders=1 -> BRAND
                    hud.setRiderColorMode(std::stoi(settings.at("colorizeRiders")) != 0
                        ? RadarHud::RiderColorMode::BRAND : RadarHud::RiderColorMode::UNIFORM);
                }
                if (settings.count("showPlayerArrow")) hud.setShowPlayerArrow(std::stoi(settings.at("showPlayerArrow")) != 0);
                if (settings.count("fadeWhenEmpty")) hud.setFadeWhenEmpty(std::stoi(settings.at("fadeWhenEmpty")) != 0);
                if (settings.count("alertDistance")) {
                    float distance = std::stof(settings.at("alertDistance"));
                    if (distance < RadarHud::MIN_ALERT_DISTANCE) distance = RadarHud::MIN_ALERT_DISTANCE;
                    if (distance > RadarHud::MAX_ALERT_DISTANCE) distance = RadarHud::MAX_ALERT_DISTANCE;
                    hud.setAlertDistance(distance);
                }
                if (settings.count("labelMode")) hud.setLabelMode(static_cast<RadarHud::LabelMode>(std::stoi(settings.at("labelMode"))));
                if (settings.count("riderShape")) hud.setRiderShape(static_cast<RadarHud::RiderShape>(std::stoi(settings.at("riderShape"))));
                if (settings.count("markerScale")) hud.setMarkerScale(std::stof(settings.at("markerScale")));
            } catch (...) {}
            hud.setDataDirty();
        }
    }

    // Apply PitboardHud
    {
        auto it = cache.find("PitboardHud");
        if (it != cache.end()) {
            auto& hud = hudManager.getPitboardHud();
            applyBaseHudSettings(hud, it->second);

            const auto& settings = it->second;
            try {
                if (settings.count("enabledRows")) hud.m_enabledRows = static_cast<uint32_t>(std::stoul(settings.at("enabledRows")));
                if (settings.count("displayMode")) hud.m_displayMode = validateDisplayMode(std::stoi(settings.at("displayMode")));
            } catch (...) {}
            hud.setDataDirty();
        }
    }

    // Apply RecordsHud
    {
        auto it = cache.find("RecordsHud");
        if (it != cache.end()) {
            auto& hud = hudManager.getRecordsHud();
            applyBaseHudSettings(hud, it->second);

            const auto& settings = it->second;
            try {
                if (settings.count("provider")) {
                    int provider = std::stoi(settings.at("provider"));
                    if (provider >= 0 && provider < static_cast<int>(RecordsHud::DataProvider::COUNT)) {
                        hud.m_provider = static_cast<RecordsHud::DataProvider>(provider);
                    }
                }
                if (settings.count("enabledColumns")) hud.m_enabledColumns = static_cast<uint32_t>(std::stoul(settings.at("enabledColumns")));
                if (settings.count("recordsToShow")) {
                    int count = std::stoi(settings.at("recordsToShow"));
                    if (count >= 1 && count <= 10) hud.m_recordsToShow = count;
                }
            } catch (...) {}
            hud.setDataDirty();
        }
    }

    // Apply LapLogHud
    {
        auto it = cache.find("LapLogHud");
        if (it != cache.end()) {
            auto& hud = hudManager.getLapLogHud();
            applyBaseHudSettings(hud, it->second);

            const auto& settings = it->second;
            try {
                if (settings.count("enabledColumns")) hud.m_enabledColumns = static_cast<uint32_t>(std::stoul(settings.at("enabledColumns")));
                if (settings.count("maxDisplayLaps")) hud.m_maxDisplayLaps = validateDisplayLaps(std::stoi(settings.at("maxDisplayLaps")));
            } catch (...) {}
            hud.setDataDirty();
        }
    }

    // Apply IdealLapHud
    {
        auto it = cache.find("IdealLapHud");
        if (it != cache.end()) {
            auto& hud = hudManager.getIdealLapHud();
            applyBaseHudSettings(hud, it->second);

            const auto& settings = it->second;
            try {
                if (settings.count("enabledRows")) hud.m_enabledRows = static_cast<uint32_t>(std::stoul(settings.at("enabledRows")));
            } catch (...) {}
            hud.setDataDirty();
        }
    }

    // Apply TelemetryHud
    {
        auto it = cache.find("TelemetryHud");
        if (it != cache.end()) {
            auto& hud = hudManager.getTelemetryHud();
            applyBaseHudSettings(hud, it->second);

            const auto& settings = it->second;
            try {
                if (settings.count("enabledElements")) hud.m_enabledElements = static_cast<uint32_t>(std::stoul(settings.at("enabledElements")));
                if (settings.count("displayMode")) hud.m_displayMode = validateDisplayMode(std::stoi(settings.at("displayMode")));
            } catch (...) {}
            hud.setDataDirty();
        }
    }

    // Apply InputHud
    {
        auto it = cache.find("InputHud");
        if (it != cache.end()) {
            auto& hud = hudManager.getInputHud();
            applyBaseHudSettings(hud, it->second);

            const auto& settings = it->second;
            try {
                if (settings.count("enabledElements")) hud.m_enabledElements = static_cast<uint32_t>(std::stoul(settings.at("enabledElements")));
            } catch (...) {}
            hud.setDataDirty();
        }
    }

    // Apply PerformanceHud
    {
        auto it = cache.find("PerformanceHud");
        if (it != cache.end()) {
            auto& hud = hudManager.getPerformanceHud();
            applyBaseHudSettings(hud, it->second);

            const auto& settings = it->second;
            try {
                if (settings.count("enabledElements")) hud.m_enabledElements = static_cast<uint32_t>(std::stoul(settings.at("enabledElements")));
                if (settings.count("displayMode")) hud.m_displayMode = validateDisplayMode(std::stoi(settings.at("displayMode")));
            } catch (...) {}
            hud.setDataDirty();
        }
    }

    // Apply simple widgets
    applyToHud("LapWidget", hudManager.getLapWidget());
    applyToHud("PositionWidget", hudManager.getPositionWidget());
    applyToHud("TimeWidget", hudManager.getTimeWidget());
    applyToHud("SessionWidget", hudManager.getSessionWidget());
    applyToHud("SpeedoWidget", hudManager.getSpeedoWidget());
    applyToHud("TachoWidget", hudManager.getTachoWidget());
    applyToHud("BarsWidget", hudManager.getBarsWidget());
    applyToHud("VersionWidget", hudManager.getVersionWidget());
    applyToHud("NoticesWidget", hudManager.getNoticesWidget());
    applyToHud("SettingsButtonWidget", hudManager.getSettingsButtonWidget());
    applyToHud("PointerWidget", hudManager.getPointerWidget());
    applyToHud("RumbleHud", hudManager.getRumbleHud());

    // Apply SpeedWidget with enabledRows - speedUnit is now global (in Preferences section)
    {
        auto it = cache.find("SpeedWidget");
        if (it != cache.end()) {
            auto& hud = hudManager.getSpeedWidget();
            applyBaseHudSettings(hud, it->second);

            const auto& settings = it->second;
            try {
                if (settings.count("enabledRows")) hud.m_enabledRows = static_cast<uint32_t>(std::stoul(settings.at("enabledRows")));
            } catch (...) {}
            hud.setDataDirty();
        }
    }

    // Apply FuelWidget with enabledRows - fuelUnit is now global (in Preferences section)
    {
        auto it = cache.find("FuelWidget");
        if (it != cache.end()) {
            auto& hud = hudManager.getFuelWidget();
            applyBaseHudSettings(hud, it->second);

            const auto& settings = it->second;
            try {
                if (settings.count("enabledRows")) hud.m_enabledRows = static_cast<uint32_t>(std::stoul(settings.at("enabledRows")));
            } catch (...) {}
            hud.setDataDirty();
        }
    }

    // Apply TimingHud with per-column modes and displayDuration
    {
        auto it = cache.find("TimingHud");
        if (it != cache.end()) {
            auto& hud = hudManager.getTimingHud();
            applyBaseHudSettings(hud, it->second);

            const auto& settings = it->second;
            try {
                if (settings.count("labelMode")) {
                    int mode = std::stoi(settings.at("labelMode"));
                    if (mode >= 0 && mode <= 2) hud.m_columnModes[TimingHud::COL_LABEL] = static_cast<ColumnMode>(mode);
                }
                if (settings.count("timeMode")) {
                    int mode = std::stoi(settings.at("timeMode"));
                    if (mode >= 0 && mode <= 2) hud.m_columnModes[TimingHud::COL_TIME] = static_cast<ColumnMode>(mode);
                }
                if (settings.count("gapMode")) {
                    int mode = std::stoi(settings.at("gapMode"));
                    if (mode >= 0 && mode <= 2) hud.m_columnModes[TimingHud::COL_GAP] = static_cast<ColumnMode>(mode);
                }
                if (settings.count("displayDuration")) {
                    int duration = std::stoi(settings.at("displayDuration"));
                    if (duration >= TimingHud::MIN_DURATION_MS && duration <= TimingHud::MAX_DURATION_MS) {
                        hud.m_displayDurationMs = duration;
                    }
                }
                if (settings.count("gapTypes")) {
                    int gapTypes = std::stoi(settings.at("gapTypes"));
                    // Validate: only GAP_TO_PB, GAP_TO_IDEAL, GAP_TO_SESSION bits are valid (0-7)
                    if (gapTypes >= 0 && gapTypes <= 7) {
                        hud.m_gapTypes = static_cast<uint8_t>(gapTypes);
                    }
                }
                // Migration: handle old settings format
                if (settings.count("displayMode") && !settings.count("labelMode")) {
                    int mode = std::stoi(settings.at("displayMode"));
                    // Old MODE_ALWAYS=0, MODE_SPLITS=1
                    // Migrate to: if ALWAYS, set all to ALWAYS; if SPLITS, set Label/Gap to SPLITS, Time to ALWAYS
                    if (mode == 0) {
                        hud.m_columnModes[TimingHud::COL_LABEL] = ColumnMode::ALWAYS;
                        hud.m_columnModes[TimingHud::COL_TIME] = ColumnMode::ALWAYS;
                        hud.m_columnModes[TimingHud::COL_GAP] = ColumnMode::ALWAYS;
                    } else {
                        hud.m_columnModes[TimingHud::COL_LABEL] = ColumnMode::SPLITS;
                        hud.m_columnModes[TimingHud::COL_TIME] = ColumnMode::ALWAYS;
                        hud.m_columnModes[TimingHud::COL_GAP] = ColumnMode::SPLITS;
                    }
                }
            } catch (...) {}
            hud.setDataDirty();
        }
    }

    // Apply GapBarHud with freeze, marker, and range settings
    {
        auto it = cache.find("GapBarHud");
        if (it != cache.end()) {
            auto& hud = hudManager.getGapBarHud();
            applyBaseHudSettings(hud, it->second);
            const auto& settings = it->second;
            try {
                if (settings.count("freezeDuration")) {
                    int freeze = std::stoi(settings.at("freezeDuration"));
                    if (freeze >= GapBarHud::MIN_FREEZE_MS && freeze <= GapBarHud::MAX_FREEZE_MS) {
                        hud.m_freezeDurationMs = freeze;
                    }
                }
                // Try new key first, fall back to old key for compatibility
                if (settings.count("showMarkers")) {
                    hud.m_showMarkers = (settings.at("showMarkers") == "1");
                } else if (settings.count("showMarker")) {
                    hud.m_showMarkers = (settings.at("showMarker") == "1");
                }
                // Try new key first, fall back to old key for compatibility
                if (settings.count("gapRange")) {
                    int range = std::stoi(settings.at("gapRange"));
                    if (range >= GapBarHud::MIN_RANGE_MS && range <= GapBarHud::MAX_RANGE_MS) {
                        hud.m_gapRangeMs = range;
                    }
                } else if (settings.count("legacyRange")) {
                    int range = std::stoi(settings.at("legacyRange"));
                    if (range >= GapBarHud::MIN_RANGE_MS && range <= GapBarHud::MAX_RANGE_MS) {
                        hud.m_gapRangeMs = range;
                    }
                }
                if (settings.count("barWidth")) {
                    int width = std::stoi(settings.at("barWidth"));
                    if (width >= GapBarHud::MIN_WIDTH_PERCENT && width <= GapBarHud::MAX_WIDTH_PERCENT) {
                        hud.m_barWidthPercent = width;
                    }
                }
            } catch (...) {}
            hud.setDataDirty();
        }
    }

    // Note: ColorConfig is now global (not per-profile) - loaded once in loadSettings

    DEBUG_INFO_F("Applied profile: %s", ProfileManager::getProfileName(profile));
}

bool SettingsManager::switchProfile(HudManager& hudManager, ProfileType newProfile) {
    ProfileManager& profileManager = ProfileManager::getInstance();
    ProfileType oldProfile = profileManager.getActiveProfile();

    if (newProfile == oldProfile) return false;
    if (newProfile >= ProfileType::COUNT) {
        DEBUG_WARN_F("switchProfile called with invalid profile index: %d", static_cast<int>(newProfile));
        return false;
    }

    // Capture current state to old profile
    captureToProfile(hudManager, oldProfile);

    // Switch active profile
    profileManager.setActiveProfile(newProfile);

    // Apply new profile settings
    applyProfile(hudManager, newProfile);

    // Save to disk
    if (!m_savePath.empty()) {
        saveSettings(hudManager, m_savePath.c_str());
    }

    return true;
}

void SettingsManager::applyToAllProfiles(HudManager& hudManager) {
    ProfileType activeProfile = ProfileManager::getInstance().getActiveProfile();

    // Capture current HUD state to active profile (ensure it's up to date)
    captureToProfile(hudManager, activeProfile);

    // Copy active profile's cache to all other profiles
    const ProfileCache& sourceCache = m_profileCache[static_cast<size_t>(activeProfile)];
    for (int i = 0; i < static_cast<int>(ProfileType::COUNT); ++i) {
        ProfileType targetProfile = static_cast<ProfileType>(i);
        if (targetProfile != activeProfile) {
            m_profileCache[i] = sourceCache;
        }
    }

    // Save to disk
    if (!m_savePath.empty()) {
        saveSettings(hudManager, m_savePath.c_str());
    }

    DEBUG_INFO_F("Applied %s profile settings to all profiles", ProfileManager::getProfileName(activeProfile));
}

void SettingsManager::copyToProfile(HudManager& hudManager, ProfileType targetProfile) {
    ProfileType activeProfile = ProfileManager::getInstance().getActiveProfile();

    // Don't copy to self
    if (targetProfile == activeProfile) {
        DEBUG_WARN("Cannot copy profile to itself");
        return;
    }

    // Capture current HUD state to active profile (ensure it's up to date)
    captureToProfile(hudManager, activeProfile);

    // Copy to target profile
    m_profileCache[static_cast<size_t>(targetProfile)] = m_profileCache[static_cast<size_t>(activeProfile)];

    // Save to disk
    if (!m_savePath.empty()) {
        saveSettings(hudManager, m_savePath.c_str());
    }

    DEBUG_INFO_F("Copied %s profile settings to %s",
        ProfileManager::getProfileName(activeProfile),
        ProfileManager::getProfileName(targetProfile));
}

void SettingsManager::saveSettings(const HudManager& hudManager, const char* savePath) {
    std::string filePath = getSettingsFilePath(savePath);
    m_savePath = savePath ? savePath : "";

    // Capture current state to active profile before saving
    // Note: This modifies m_profileCache, which is why saveSettings is non-const
    captureCurrentState(hudManager);

    std::ofstream file(filePath);
    if (!file.is_open()) {
        DEBUG_WARN_F("Failed to save settings to: %s", filePath.c_str());
        return;
    }

    DEBUG_INFO_F("Saving settings to: %s", filePath.c_str());

    // Write Profiles section
    const ProfileManager& profileManager = ProfileManager::getInstance();
    file << "[Profiles]\n";
    file << "activeProfile=" << static_cast<int>(profileManager.getActiveProfile()) << "\n";
    file << "autoSwitch=" << (profileManager.isAutoSwitchEnabled() ? 1 : 0) << "\n\n";

    // Write General section (global preferences)
    file << "[General]\n";
    file << "widgetsEnabled=" << (HudManager::getInstance().areWidgetsEnabled() ? 1 : 0) << "\n";
    file << "gridSnapping=" << (ColorConfig::getInstance().getGridSnapping() ? 1 : 0) << "\n";
    file << "checkForUpdates=" << (UpdateChecker::getInstance().isEnabled() ? 1 : 0) << "\n";
    file << "controller=" << XInputReader::getInstance().getRumbleConfig().controllerIndex << "\n";
    file << "speedUnit=" << static_cast<int>(hudManager.getSpeedWidget().m_speedUnit) << "\n";
    file << "fuelUnit=" << static_cast<int>(hudManager.getFuelWidget().m_fuelUnit) << "\n\n";

    // Write Colors section
    const ColorConfig& colorConfig = ColorConfig::getInstance();
    file << "[Colors]\n";
    file << "primary=" << PluginUtils::formatColorHex(colorConfig.getPrimary()) << "\n";
    file << "secondary=" << PluginUtils::formatColorHex(colorConfig.getSecondary()) << "\n";
    file << "tertiary=" << PluginUtils::formatColorHex(colorConfig.getTertiary()) << "\n";
    file << "muted=" << PluginUtils::formatColorHex(colorConfig.getMuted()) << "\n";
    file << "background=" << PluginUtils::formatColorHex(colorConfig.getBackground()) << "\n";
    file << "positive=" << PluginUtils::formatColorHex(colorConfig.getPositive()) << "\n";
    file << "warning=" << PluginUtils::formatColorHex(colorConfig.getWarning()) << "\n";
    file << "neutral=" << PluginUtils::formatColorHex(colorConfig.getNeutral()) << "\n";
    file << "negative=" << PluginUtils::formatColorHex(colorConfig.getNegative()) << "\n";
    file << "accent=" << PluginUtils::formatColorHex(colorConfig.getAccent()) << "\n\n";

    // Write Fonts section
    const FontConfig& fontConfig = FontConfig::getInstance();
    file << "[Fonts]\n";
    file << "title=" << fontConfig.getFontName(FontCategory::TITLE) << "\n";
    file << "normal=" << fontConfig.getFontName(FontCategory::NORMAL) << "\n";
    file << "strong=" << fontConfig.getFontName(FontCategory::STRONG) << "\n";
    file << "marker=" << fontConfig.getFontName(FontCategory::MARKER) << "\n";
    file << "small=" << fontConfig.getFontName(FontCategory::SMALL) << "\n\n";

    // Write Rumble section (effect configuration)
    const RumbleConfig& rumbleConfig = XInputReader::getInstance().getRumbleConfig();
    file << "[Rumble]\n";
    file << "enabled=" << (rumbleConfig.enabled ? 1 : 0) << "\n";
    file << "additive_blend=" << (rumbleConfig.additiveBlend ? 1 : 0) << "\n";
    file << "rumble_when_crashed=" << (rumbleConfig.rumbleWhenCrashed ? 1 : 0) << "\n";
    // Suspension effect
    file << "susp_min_input=" << rumbleConfig.suspensionEffect.minInput << "\n";
    file << "susp_max_input=" << rumbleConfig.suspensionEffect.maxInput << "\n";
    file << "susp_light_strength=" << rumbleConfig.suspensionEffect.lightStrength << "\n";
    file << "susp_heavy_strength=" << rumbleConfig.suspensionEffect.heavyStrength << "\n";
    // Wheelspin effect
    file << "wheel_min_input=" << rumbleConfig.wheelspinEffect.minInput << "\n";
    file << "wheel_max_input=" << rumbleConfig.wheelspinEffect.maxInput << "\n";
    file << "wheel_light_strength=" << rumbleConfig.wheelspinEffect.lightStrength << "\n";
    file << "wheel_heavy_strength=" << rumbleConfig.wheelspinEffect.heavyStrength << "\n";
    // Brake lockup effect
    file << "lockup_min_input=" << rumbleConfig.brakeLockupEffect.minInput << "\n";
    file << "lockup_max_input=" << rumbleConfig.brakeLockupEffect.maxInput << "\n";
    file << "lockup_light_strength=" << rumbleConfig.brakeLockupEffect.lightStrength << "\n";
    file << "lockup_heavy_strength=" << rumbleConfig.brakeLockupEffect.heavyStrength << "\n";
    // RPM effect
    file << "rpm_min_input=" << rumbleConfig.rpmEffect.minInput << "\n";
    file << "rpm_max_input=" << rumbleConfig.rpmEffect.maxInput << "\n";
    file << "rpm_light_strength=" << rumbleConfig.rpmEffect.lightStrength << "\n";
    file << "rpm_heavy_strength=" << rumbleConfig.rpmEffect.heavyStrength << "\n";
    // Slide effect
    file << "slide_min_input=" << rumbleConfig.slideEffect.minInput << "\n";
    file << "slide_max_input=" << rumbleConfig.slideEffect.maxInput << "\n";
    file << "slide_light_strength=" << rumbleConfig.slideEffect.lightStrength << "\n";
    file << "slide_heavy_strength=" << rumbleConfig.slideEffect.heavyStrength << "\n";
    // Surface effect
    file << "surface_min_input=" << rumbleConfig.surfaceEffect.minInput << "\n";
    file << "surface_max_input=" << rumbleConfig.surfaceEffect.maxInput << "\n";
    file << "surface_light_strength=" << rumbleConfig.surfaceEffect.lightStrength << "\n";
    file << "surface_heavy_strength=" << rumbleConfig.surfaceEffect.heavyStrength << "\n";
    // Steer effect
    file << "steer_min_input=" << rumbleConfig.steerEffect.minInput << "\n";
    file << "steer_max_input=" << rumbleConfig.steerEffect.maxInput << "\n";
    file << "steer_light_strength=" << rumbleConfig.steerEffect.lightStrength << "\n";
    file << "steer_heavy_strength=" << rumbleConfig.steerEffect.heavyStrength << "\n";
    // Wheelie effect
    file << "wheelie_min_input=" << rumbleConfig.wheelieEffect.minInput << "\n";
    file << "wheelie_max_input=" << rumbleConfig.wheelieEffect.maxInput << "\n";
    file << "wheelie_light_strength=" << rumbleConfig.wheelieEffect.lightStrength << "\n";
    file << "wheelie_heavy_strength=" << rumbleConfig.wheelieEffect.heavyStrength << "\n\n";

    // Write Hotkeys section
    const HotkeyManager& hotkeyMgr = HotkeyManager::getInstance();
    file << "[Hotkeys]\n";
    for (int i = 0; i < static_cast<int>(HotkeyAction::COUNT); ++i) {
        HotkeyAction action = static_cast<HotkeyAction>(i);
        const HotkeyBinding& binding = hotkeyMgr.getBinding(action);

        // Save keyboard binding
        file << "action" << i << "_key=" << static_cast<int>(binding.keyboard.keyCode) << "\n";
        file << "action" << i << "_mod=" << static_cast<int>(binding.keyboard.modifiers) << "\n";
        // Save controller binding
        file << "action" << i << "_btn=" << static_cast<int>(binding.controller) << "\n";
    }
    file << "\n";

    // Write TrackedRiders section
    file << "[TrackedRiders]\n";
    file << "data=" << TrackedRidersManager::getInstance().serializeToString() << "\n";
    file << "\n";

    // Write all profiles (HUDs and Widgets)
    static const std::array<const char*, 26> hudOrder = {
        "StandingsHud", "MapHud", "RadarHud", "PitboardHud", "RecordsHud",
        "LapLogHud", "IdealLapHud", "TelemetryHud", "InputHud", "PerformanceHud",
        "LapWidget", "PositionWidget", "TimeWidget", "SessionWidget", "SpeedWidget",
        "SpeedoWidget", "TachoWidget", "TimingHud", "GapBarHud", "BarsWidget", "VersionWidget",
        "NoticesWidget", "FuelWidget", "SettingsButtonWidget", "PointerWidget", "RumbleHud"
    };

    for (int profileIdx = 0; profileIdx < static_cast<int>(ProfileType::COUNT); ++profileIdx) {
        ProfileType profile = static_cast<ProfileType>(profileIdx);
        const ProfileCache& cache = m_profileCache[profileIdx];

        file << "# Profile: " << ProfileManager::getProfileName(profile) << "\n";

        for (const char* hudName : hudOrder) {
            auto it = cache.find(hudName);
            if (it == cache.end()) continue;

            file << "[" << hudName << ":" << profileIdx << "]\n";

            // Write base properties first
            writeBaseHudSettings(file, it->second);

            // Write HUD-specific properties
            for (const auto& [key, value] : it->second) {
                // Skip base properties (already written)
                if (isBaseKey(key)) {
                    continue;
                }
                file << key << "=" << value << "\n";
            }
            file << "\n";
        }
    }

    if (!file.good()) {
        DEBUG_WARN_F("Stream error occurred while writing settings to: %s", filePath.c_str());
        file.close();
        std::remove(filePath.c_str());
        return;
    }

    file.close();
    DEBUG_INFO("Settings saved successfully");
}

void SettingsManager::loadSettings(HudManager& hudManager, const char* savePath) {
    std::string filePath = getSettingsFilePath(savePath);
    m_savePath = savePath ? savePath : "";

    std::ifstream file(filePath);
    if (!file.is_open()) {
        DEBUG_INFO_F("No settings file found at: %s (using defaults)", filePath.c_str());
        // Initialize cache with current (default) state for all profiles
        for (int i = 0; i < static_cast<int>(ProfileType::COUNT); ++i) {
            captureToProfile(hudManager, static_cast<ProfileType>(i));
        }
        return;
    }

    DEBUG_INFO_F("Loading settings from: %s", filePath.c_str());

    // Clear existing cache
    for (auto& cache : m_profileCache) {
        cache.clear();
    }

    std::string line;
    std::string currentSection;
    std::string currentHudName;
    int currentProfileIndex = -1;

    while (std::getline(file, line)) {
        // Trim whitespace
        size_t start = line.find_first_not_of(" \t\r\n");
        size_t end = line.find_last_not_of(" \t\r\n");
        if (start == std::string::npos) continue;
        if (end == std::string::npos) continue;
        line = line.substr(start, end - start + 1);

        // Skip comments
        if (line[0] == '#') continue;

        // Check for section header
        if (line.length() >= 3 && line.front() == '[' && line.back() == ']') {
            currentSection = line.substr(1, line.length() - 2);
            parseSectionName(currentSection, currentHudName, currentProfileIndex);
            continue;
        }

        // Parse key=value
        size_t equals = line.find('=');
        if (equals == std::string::npos) continue;

        std::string key = line.substr(0, equals);
        std::string value = line.substr(equals + 1);

        // Handle Profiles section
        if (currentHudName == "Profiles") {
            try {
                if (key == "activeProfile") {
                    int profileIdx = std::stoi(value);
                    if (profileIdx >= 0 && profileIdx < static_cast<int>(ProfileType::COUNT)) {
                        ProfileManager::getInstance().setActiveProfile(static_cast<ProfileType>(profileIdx));
                    }
                } else if (key == "autoSwitch") {
                    ProfileManager::getInstance().setAutoSwitchEnabled(std::stoi(value) != 0);
                }
            } catch (...) {}
            continue;
        }

        // Handle General section (global preferences)
        if (currentHudName == "General") {
            try {
                if (key == "widgetsEnabled") {
                    HudManager::getInstance().setWidgetsEnabled(std::stoi(value) != 0);
                } else if (key == "gridSnapping") {
                    ColorConfig::getInstance().setGridSnapping(std::stoi(value) != 0);
                } else if (key == "checkForUpdates") {
                    UpdateChecker::getInstance().setEnabled(std::stoi(value) != 0);
                } else if (key == "controller") {
                    int idx = std::stoi(value);
                    XInputReader::getInstance().getRumbleConfig().controllerIndex = idx;
                    XInputReader::getInstance().setControllerIndex(idx);
                } else if (key == "speedUnit") {
                    int unit = std::stoi(value);
                    if (unit >= 0 && unit <= 1) {
                        hudManager.getSpeedWidget().m_speedUnit = static_cast<SpeedWidget::SpeedUnit>(unit);
                    }
                } else if (key == "fuelUnit") {
                    int unit = std::stoi(value);
                    if (unit >= 0 && unit <= 1) {
                        hudManager.getFuelWidget().m_fuelUnit = static_cast<FuelWidget::FuelUnit>(unit);
                    }
                }
            } catch (...) {}
            continue;
        }

        // Handle Colors section
        if (currentHudName == "Colors") {
            ColorConfig& colorConfig = ColorConfig::getInstance();
            try {
                if (key == "primary") {
                    colorConfig.setColor(ColorSlot::PRIMARY, PluginUtils::parseColorHex(value));
                } else if (key == "secondary") {
                    colorConfig.setColor(ColorSlot::SECONDARY, PluginUtils::parseColorHex(value));
                } else if (key == "tertiary") {
                    colorConfig.setColor(ColorSlot::TERTIARY, PluginUtils::parseColorHex(value));
                } else if (key == "muted") {
                    colorConfig.setColor(ColorSlot::MUTED, PluginUtils::parseColorHex(value));
                } else if (key == "background") {
                    colorConfig.setColor(ColorSlot::BACKGROUND, PluginUtils::parseColorHex(value));
                } else if (key == "positive") {
                    colorConfig.setColor(ColorSlot::POSITIVE, PluginUtils::parseColorHex(value));
                } else if (key == "warning") {
                    colorConfig.setColor(ColorSlot::WARNING, PluginUtils::parseColorHex(value));
                } else if (key == "neutral") {
                    colorConfig.setColor(ColorSlot::NEUTRAL, PluginUtils::parseColorHex(value));
                } else if (key == "negative") {
                    colorConfig.setColor(ColorSlot::NEGATIVE, PluginUtils::parseColorHex(value));
                } else if (key == "accent") {
                    colorConfig.setColor(ColorSlot::ACCENT, PluginUtils::parseColorHex(value));
                }
            } catch (...) {}
            continue;
        }

        // Handle Fonts section
        if (currentHudName == "Fonts") {
            FontConfig& fontConfig = FontConfig::getInstance();
            if (key == "title") {
                fontConfig.setFont(FontCategory::TITLE, value);
            } else if (key == "normal") {
                fontConfig.setFont(FontCategory::NORMAL, value);
            } else if (key == "strong") {
                fontConfig.setFont(FontCategory::STRONG, value);
            } else if (key == "marker") {
                fontConfig.setFont(FontCategory::MARKER, value);
            } else if (key == "small") {
                fontConfig.setFont(FontCategory::SMALL, value);
            }
            continue;
        }

        // Handle Rumble section (effect configuration)
        if (currentHudName == "Rumble") {
            RumbleConfig& config = XInputReader::getInstance().getRumbleConfig();
            try {
                if (key == "enabled") {
                    config.enabled = std::stoi(value) != 0;
                } else if (key == "additive_blend") {
                    config.additiveBlend = std::stoi(value) != 0;
                } else if (key == "rumble_when_crashed") {
                    config.rumbleWhenCrashed = std::stoi(value) != 0;
                } else if (key == "disable_on_crash") {
                    // Backward compatibility: invert the old setting
                    config.rumbleWhenCrashed = std::stoi(value) == 0;
                }
                // Suspension effect - new format
                else if (key == "susp_min_input") {
                    config.suspensionEffect.minInput = std::stof(value);
                } else if (key == "susp_max_input") {
                    config.suspensionEffect.maxInput = std::stof(value);
                } else if (key == "susp_light_strength") {
                    config.suspensionEffect.lightStrength = std::stof(value);
                } else if (key == "susp_heavy_strength") {
                    config.suspensionEffect.heavyStrength = std::stof(value);
                }
                // Wheelspin effect
                else if (key == "wheel_min_input") {
                    config.wheelspinEffect.minInput = std::stof(value);
                } else if (key == "wheel_max_input") {
                    config.wheelspinEffect.maxInput = std::stof(value);
                } else if (key == "wheel_light_strength") {
                    config.wheelspinEffect.lightStrength = std::stof(value);
                } else if (key == "wheel_heavy_strength") {
                    config.wheelspinEffect.heavyStrength = std::stof(value);
                }
                // Brake lockup effect
                else if (key == "lockup_min_input") {
                    config.brakeLockupEffect.minInput = std::stof(value);
                } else if (key == "lockup_max_input") {
                    config.brakeLockupEffect.maxInput = std::stof(value);
                } else if (key == "lockup_light_strength") {
                    config.brakeLockupEffect.lightStrength = std::stof(value);
                } else if (key == "lockup_heavy_strength") {
                    config.brakeLockupEffect.heavyStrength = std::stof(value);
                }
                // RPM effect
                else if (key == "rpm_min_input") {
                    config.rpmEffect.minInput = std::stof(value);
                } else if (key == "rpm_max_input") {
                    config.rpmEffect.maxInput = std::stof(value);
                } else if (key == "rpm_light_strength") {
                    config.rpmEffect.lightStrength = std::stof(value);
                } else if (key == "rpm_heavy_strength") {
                    config.rpmEffect.heavyStrength = std::stof(value);
                }
                // Slide effect
                else if (key == "slide_min_input") {
                    config.slideEffect.minInput = std::stof(value);
                } else if (key == "slide_max_input") {
                    config.slideEffect.maxInput = std::stof(value);
                } else if (key == "slide_light_strength") {
                    config.slideEffect.lightStrength = std::stof(value);
                } else if (key == "slide_heavy_strength") {
                    config.slideEffect.heavyStrength = std::stof(value);
                }
                // Surface effect
                else if (key == "surface_min_input") {
                    config.surfaceEffect.minInput = std::stof(value);
                } else if (key == "surface_max_input") {
                    config.surfaceEffect.maxInput = std::stof(value);
                } else if (key == "surface_light_strength") {
                    config.surfaceEffect.lightStrength = std::stof(value);
                } else if (key == "surface_heavy_strength") {
                    config.surfaceEffect.heavyStrength = std::stof(value);
                }
                // Steer effect
                else if (key == "steer_min_input") {
                    config.steerEffect.minInput = std::stof(value);
                } else if (key == "steer_max_input") {
                    config.steerEffect.maxInput = std::stof(value);
                } else if (key == "steer_light_strength") {
                    config.steerEffect.lightStrength = std::stof(value);
                } else if (key == "steer_heavy_strength") {
                    config.steerEffect.heavyStrength = std::stof(value);
                }
                // Wheelie effect
                else if (key == "wheelie_min_input") {
                    config.wheelieEffect.minInput = std::stof(value);
                } else if (key == "wheelie_max_input") {
                    config.wheelieEffect.maxInput = std::stof(value);
                } else if (key == "wheelie_light_strength") {
                    config.wheelieEffect.lightStrength = std::stof(value);
                } else if (key == "wheelie_heavy_strength") {
                    config.wheelieEffect.heavyStrength = std::stof(value);
                }
            } catch (...) {}
            continue;
        }

        // Handle Hotkeys section
        if (currentHudName == "Hotkeys") {
            HotkeyManager& hotkeyMgr = HotkeyManager::getInstance();
            try {
                // Parse action index from key name (e.g., "action0_key" -> 0)
                if (key.length() > 7 && key.substr(0, 6) == "action") {
                    size_t underscorePos = key.find('_', 6);
                    if (underscorePos != std::string::npos) {
                        int actionIdx = std::stoi(key.substr(6, underscorePos - 6));
                        if (actionIdx >= 0 && actionIdx < static_cast<int>(HotkeyAction::COUNT)) {
                            HotkeyAction action = static_cast<HotkeyAction>(actionIdx);
                            std::string suffix = key.substr(underscorePos + 1);

                            HotkeyBinding binding = hotkeyMgr.getBinding(action);
                            if (suffix == "key") {
                                binding.keyboard.keyCode = static_cast<uint8_t>(std::stoi(value));
                            } else if (suffix == "mod") {
                                binding.keyboard.modifiers = static_cast<ModifierFlags>(std::stoi(value));
                            } else if (suffix == "btn") {
                                binding.controller = static_cast<ControllerButton>(std::stoi(value));
                            }
                            hotkeyMgr.setBinding(action, binding);
                        }
                    }
                }
            } catch (...) {}
            continue;
        }

        // Handle TrackedRiders section
        if (currentHudName == "TrackedRiders") {
            try {
                if (key == "data") {
                    TrackedRidersManager::getInstance().deserializeFromString(value);
                }
            } catch (...) {}
            continue;
        }

        // Handle profile-specific HUD settings
        if (currentProfileIndex >= 0 && currentProfileIndex < static_cast<int>(ProfileType::COUNT)) {
            m_profileCache[currentProfileIndex][currentHudName][key] = value;
        }
        // Legacy v1 format (no profile index) is ignored - users start fresh
    }

    file.close();

    // If cache is empty (v1 file or corrupted), initialize all profiles with current defaults
    bool anyProfileEmpty = false;
    for (const auto& cache : m_profileCache) {
        if (cache.empty()) {
            anyProfileEmpty = true;
            break;
        }
    }
    if (anyProfileEmpty) {
        DEBUG_INFO("Initializing profiles with defaults (legacy or empty settings file)");
        for (int i = 0; i < static_cast<int>(ProfileType::COUNT); ++i) {
            captureToProfile(hudManager, static_cast<ProfileType>(i));
        }
    }

    m_cacheInitialized = true;

    // Apply active profile to HUDs
    applyActiveProfile(hudManager);

    // Trigger update check on startup if enabled
    if (UpdateChecker::getInstance().isEnabled()) {
        DEBUG_INFO("Update check enabled, checking for updates on startup");
        UpdateChecker::getInstance().checkForUpdates();
    }

    DEBUG_INFO("Settings loaded successfully");
}
