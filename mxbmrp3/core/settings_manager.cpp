// ============================================================================
// core/settings_manager.cpp
// Manages persistence of HUD settings (position, scale, visibility, etc.)
// Supports per-profile settings (Practice, Race, Spectate)
// ============================================================================
#include "settings_manager.h"
#include "hud_manager.h"
#include "profile_manager.h"
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
#include "../hud/records_hud.h"
#include "color_config.h"
#include <fstream>
#include <sstream>
#include <array>
#include <windows.h>

namespace {
    constexpr const char* SETTINGS_SUBDIRECTORY = "mxbmrp3";
    constexpr const char* SETTINGS_FILENAME = "mxbmrp3_settings.ini";

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
        settings["visible"] = std::to_string(hud.isVisible() ? 1 : 0);
        settings["showTitle"] = std::to_string(hud.getShowTitle() ? 1 : 0);
        settings["showBackgroundTexture"] = std::to_string(hud.getShowBackgroundTexture() ? 1 : 0);
        settings["backgroundOpacity"] = std::to_string(hud.getBackgroundOpacity());
        settings["scale"] = std::to_string(hud.getScale());
        settings["offsetX"] = std::to_string(hud.getOffsetX());
        settings["offsetY"] = std::to_string(hud.getOffsetY());
    }

    // Helper to write base HUD properties to file
    void writeBaseHudSettings(std::ofstream& file, const SettingsManager::HudSettings& settings) {
        static const std::array<const char*, 7> baseKeys = {
            "visible", "showTitle", "showBackgroundTexture", "backgroundOpacity",
            "scale", "offsetX", "offsetY"
        };
        for (const auto& key : baseKeys) {
            auto it = settings.find(key);
            if (it != settings.end()) {
                file << key << "=" << it->second << "\n";
            }
        }
    }

    // Helper to apply base HUD settings from a map
    void applyBaseHudSettings(BaseHud& hud, const SettingsManager::HudSettings& settings) {
        float pendingOffsetX = 0, pendingOffsetY = 0;
        bool hasOffsetX = false, hasOffsetY = false;

        for (const auto& [key, value] : settings) {
            try {
                if (key == "visible") {
                    hud.setVisible(std::stoi(value) != 0);
                } else if (key == "showTitle") {
                    hud.setShowTitle(std::stoi(value) != 0);
                } else if (key == "showBackgroundTexture") {
                    hud.setShowBackgroundTexture(std::stoi(value) != 0);
                } else if (key == "backgroundOpacity") {
                    hud.setBackgroundOpacity(validateOpacity(std::stof(value)));
                } else if (key == "scale") {
                    hud.setScale(validateScale(std::stof(value)));
                } else if (key == "offsetX") {
                    pendingOffsetX = validateOffset(std::stof(value));
                    hasOffsetX = true;
                } else if (key == "offsetY") {
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
    if (profile >= ProfileType::COUNT) return;

    ProfileCache& cache = m_profileCache[static_cast<size_t>(profile)];
    cache.clear();

    // Capture StandingsHud
    {
        HudSettings settings;
        const auto& hud = hudManager.getStandingsHud();
        captureBaseHudSettings(settings, hud);
        settings["displayRowCount"] = std::to_string(hud.m_displayRowCount);
        settings["enabledColumns"] = std::to_string(hud.m_enabledColumns);
        settings["officialGapMode"] = std::to_string(static_cast<int>(hud.m_officialGapMode));
        settings["liveGapMode"] = std::to_string(static_cast<int>(hud.m_liveGapMode));
        settings["gapIndicatorMode"] = std::to_string(static_cast<int>(hud.m_gapIndicatorMode));
        cache["StandingsHud"] = std::move(settings);
    }

    // Capture MapHud
    {
        HudSettings settings;
        const auto& hud = hudManager.getMapHud();
        captureBaseHudSettings(settings, hud);
        settings["rotateToPlayer"] = std::to_string(hud.getRotateToPlayer() ? 1 : 0);
        settings["showOutline"] = std::to_string(hud.getShowOutline() ? 1 : 0);
        settings["colorizeRiders"] = std::to_string(hud.getColorizeRiders() ? 1 : 0);
        settings["trackLineWidthMeters"] = std::to_string(hud.getTrackLineWidthMeters());
        settings["labelMode"] = std::to_string(static_cast<int>(hud.getLabelMode()));
        settings["anchorPoint"] = std::to_string(static_cast<int>(hud.getAnchorPoint()));
        settings["anchorX"] = std::to_string(hud.m_fAnchorX);
        settings["anchorY"] = std::to_string(hud.m_fAnchorY);
        settings["zoomEnabled"] = std::to_string(hud.getZoomEnabled() ? 1 : 0);
        settings["zoomDistance"] = std::to_string(hud.getZoomDistance());
        cache["MapHud"] = std::move(settings);
    }

    // Capture RadarHud
    {
        HudSettings settings;
        const auto& hud = hudManager.getRadarHud();
        captureBaseHudSettings(settings, hud);
        settings["radarRange"] = std::to_string(hud.getRadarRange());
        settings["colorizeRiders"] = std::to_string(hud.getColorizeRiders() ? 1 : 0);
        settings["showPlayerArrow"] = std::to_string(hud.getShowPlayerArrow() ? 1 : 0);
        settings["fadeWhenEmpty"] = std::to_string(hud.getFadeWhenEmpty() ? 1 : 0);
        settings["alertDistance"] = std::to_string(hud.getAlertDistance());
        settings["labelMode"] = std::to_string(static_cast<int>(hud.getLabelMode()));
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

    // Capture SessionBestHud
    {
        HudSettings settings;
        const auto& hud = hudManager.getSessionBestHud();
        captureBaseHudSettings(settings, hud);
        settings["enabledRows"] = std::to_string(hud.m_enabledRows);
        cache["SessionBestHud"] = std::move(settings);
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
    captureWidget("TimingWidget", hudManager.getTimingWidget());
    captureWidget("BarsWidget", hudManager.getBarsWidget());
    captureWidget("VersionWidget", hudManager.getVersionWidget());
    captureWidget("NoticesWidget", hudManager.getNoticesWidget());
    captureWidget("SettingsButtonWidget", hudManager.getSettingsButtonWidget());

    // SpeedWidget has speedUnit
    {
        HudSettings settings;
        const auto& hud = hudManager.getSpeedWidget();
        captureBaseHudSettings(settings, hud);
        settings["speedUnit"] = std::to_string(static_cast<int>(hud.m_speedUnit));
        cache["SpeedWidget"] = std::move(settings);
    }

    // FuelWidget has fuelUnit
    {
        HudSettings settings;
        const auto& hud = hudManager.getFuelWidget();
        captureBaseHudSettings(settings, hud);
        settings["fuelUnit"] = std::to_string(static_cast<int>(hud.m_fuelUnit));
        cache["FuelWidget"] = std::move(settings);
    }

    // Capture ColorConfig (per-profile)
    {
        HudSettings settings;
        const ColorConfig& colorConfig = ColorConfig::getInstance();
        std::ostringstream oss;
        auto formatColor = [&oss](uint32_t color) -> std::string {
            oss.str("");
            oss << "0x" << std::hex << color;
            return oss.str();
        };
        settings["primary"] = formatColor(colorConfig.getPrimary());
        settings["secondary"] = formatColor(colorConfig.getSecondary());
        settings["tertiary"] = formatColor(colorConfig.getTertiary());
        settings["muted"] = formatColor(colorConfig.getMuted());
        settings["background"] = formatColor(colorConfig.getBackground());
        settings["positive"] = formatColor(colorConfig.getPositive());
        settings["warning"] = formatColor(colorConfig.getWarning());
        settings["neutral"] = formatColor(colorConfig.getNeutral());
        settings["negative"] = formatColor(colorConfig.getNegative());
        settings["accent"] = formatColor(colorConfig.getAccent());
        settings["gridSnapping"] = std::to_string(colorConfig.getGridSnapping() ? 1 : 0);
        settings["widgetsEnabled"] = std::to_string(HudManager::getInstance().areWidgetsEnabled() ? 1 : 0);
        cache["ColorConfig"] = std::move(settings);
    }

    m_cacheInitialized = true;
}

void SettingsManager::applyActiveProfile(HudManager& hudManager) {
    ProfileType activeProfile = ProfileManager::getInstance().getActiveProfile();
    applyProfile(hudManager, activeProfile);
}

void SettingsManager::applyProfile(HudManager& hudManager, ProfileType profile) {
    if (profile >= ProfileType::COUNT) return;
    if (!m_cacheInitialized) return;

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
        auto it = cache.find("StandingsHud");
        if (it != cache.end()) {
            auto& hud = hudManager.getStandingsHud();
            applyBaseHudSettings(hud, it->second);

            const auto& settings = it->second;
            try {
                if (settings.count("displayRowCount")) hud.m_displayRowCount = validateDisplayRows(std::stoi(settings.at("displayRowCount")));
                if (settings.count("enabledColumns")) hud.m_enabledColumns = static_cast<uint32_t>(std::stoul(settings.at("enabledColumns")));
                if (settings.count("officialGapMode")) hud.m_officialGapMode = static_cast<StandingsHud::GapMode>(std::stoi(settings.at("officialGapMode")));
                if (settings.count("liveGapMode")) hud.m_liveGapMode = static_cast<StandingsHud::GapMode>(std::stoi(settings.at("liveGapMode")));
                if (settings.count("gapIndicatorMode")) hud.m_gapIndicatorMode = static_cast<StandingsHud::GapIndicatorMode>(std::stoi(settings.at("gapIndicatorMode")));
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
                if (settings.count("colorizeRiders")) hud.setColorizeRiders(std::stoi(settings.at("colorizeRiders")) != 0);
                if (settings.count("trackLineWidthMeters")) hud.setTrackLineWidthMeters(validateTrackLineWidth(std::stof(settings.at("trackLineWidthMeters"))));
                if (settings.count("labelMode")) hud.setLabelMode(static_cast<MapHud::LabelMode>(std::stoi(settings.at("labelMode"))));
                if (settings.count("anchorPoint")) hud.setAnchorPoint(static_cast<MapHud::AnchorPoint>(std::stoi(settings.at("anchorPoint"))));
                if (settings.count("anchorX")) hud.m_fAnchorX = std::stof(settings.at("anchorX"));
                if (settings.count("anchorY")) hud.m_fAnchorY = std::stof(settings.at("anchorY"));
                if (settings.count("zoomEnabled")) hud.setZoomEnabled(std::stoi(settings.at("zoomEnabled")) != 0);
                if (settings.count("zoomDistance")) hud.setZoomDistance(validateZoomDistance(std::stof(settings.at("zoomDistance"))));
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
                if (settings.count("colorizeRiders")) hud.setColorizeRiders(std::stoi(settings.at("colorizeRiders")) != 0);
                if (settings.count("showPlayerArrow")) hud.setShowPlayerArrow(std::stoi(settings.at("showPlayerArrow")) != 0);
                if (settings.count("fadeWhenEmpty")) hud.setFadeWhenEmpty(std::stoi(settings.at("fadeWhenEmpty")) != 0);
                if (settings.count("alertDistance")) {
                    float distance = std::stof(settings.at("alertDistance"));
                    if (distance < RadarHud::MIN_ALERT_DISTANCE) distance = RadarHud::MIN_ALERT_DISTANCE;
                    if (distance > RadarHud::MAX_ALERT_DISTANCE) distance = RadarHud::MAX_ALERT_DISTANCE;
                    hud.setAlertDistance(distance);
                }
                if (settings.count("labelMode")) hud.setLabelMode(static_cast<RadarHud::LabelMode>(std::stoi(settings.at("labelMode"))));
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

    // Apply SessionBestHud
    {
        auto it = cache.find("SessionBestHud");
        if (it != cache.end()) {
            auto& hud = hudManager.getSessionBestHud();
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
    applyToHud("TimingWidget", hudManager.getTimingWidget());
    applyToHud("BarsWidget", hudManager.getBarsWidget());
    applyToHud("VersionWidget", hudManager.getVersionWidget());
    applyToHud("NoticesWidget", hudManager.getNoticesWidget());
    applyToHud("SettingsButtonWidget", hudManager.getSettingsButtonWidget());

    // Apply SpeedWidget with speedUnit
    {
        auto it = cache.find("SpeedWidget");
        if (it != cache.end()) {
            auto& hud = hudManager.getSpeedWidget();
            applyBaseHudSettings(hud, it->second);

            const auto& settings = it->second;
            try {
                if (settings.count("speedUnit")) {
                    int unit = std::stoi(settings.at("speedUnit"));
                    if (unit >= 0 && unit <= 1) hud.m_speedUnit = static_cast<SpeedWidget::SpeedUnit>(unit);
                }
            } catch (...) {}
            hud.setDataDirty();
        }
    }

    // Apply FuelWidget with fuelUnit
    {
        auto it = cache.find("FuelWidget");
        if (it != cache.end()) {
            auto& hud = hudManager.getFuelWidget();
            applyBaseHudSettings(hud, it->second);

            const auto& settings = it->second;
            try {
                if (settings.count("fuelUnit")) {
                    int unit = std::stoi(settings.at("fuelUnit"));
                    if (unit >= 0 && unit <= 1) hud.m_fuelUnit = static_cast<FuelWidget::FuelUnit>(unit);
                }
            } catch (...) {}
            hud.setDataDirty();
        }
    }

    // Apply ColorConfig (per-profile)
    {
        auto it = cache.find("ColorConfig");
        if (it != cache.end()) {
            ColorConfig& colorConfig = ColorConfig::getInstance();
            const auto& settings = it->second;
            try {
                if (settings.count("primary")) colorConfig.setColor(ColorSlot::PRIMARY, std::stoul(settings.at("primary"), nullptr, 0));
                if (settings.count("secondary")) colorConfig.setColor(ColorSlot::SECONDARY, std::stoul(settings.at("secondary"), nullptr, 0));
                if (settings.count("tertiary")) colorConfig.setColor(ColorSlot::TERTIARY, std::stoul(settings.at("tertiary"), nullptr, 0));
                if (settings.count("muted")) colorConfig.setColor(ColorSlot::MUTED, std::stoul(settings.at("muted"), nullptr, 0));
                if (settings.count("background")) colorConfig.setColor(ColorSlot::BACKGROUND, std::stoul(settings.at("background"), nullptr, 0));
                if (settings.count("positive")) colorConfig.setColor(ColorSlot::POSITIVE, std::stoul(settings.at("positive"), nullptr, 0));
                if (settings.count("warning")) colorConfig.setColor(ColorSlot::WARNING, std::stoul(settings.at("warning"), nullptr, 0));
                if (settings.count("neutral")) colorConfig.setColor(ColorSlot::NEUTRAL, std::stoul(settings.at("neutral"), nullptr, 0));
                if (settings.count("negative")) colorConfig.setColor(ColorSlot::NEGATIVE, std::stoul(settings.at("negative"), nullptr, 0));
                if (settings.count("accent")) colorConfig.setColor(ColorSlot::ACCENT, std::stoul(settings.at("accent"), nullptr, 0));
                if (settings.count("gridSnapping")) colorConfig.setGridSnapping(std::stoi(settings.at("gridSnapping")) != 0);
                if (settings.count("widgetsEnabled")) hudManager.setWidgetsEnabled(std::stoi(settings.at("widgetsEnabled")) != 0);
            } catch (...) {}

            // Mark all HUDs dirty so they rebuild with new colors
            hudManager.markAllHudsDirty();
        }
    }

    DEBUG_INFO_F("Applied profile: %s", ProfileManager::getProfileName(profile));
}

bool SettingsManager::switchProfile(HudManager& hudManager, ProfileType newProfile) {
    ProfileManager& profileManager = ProfileManager::getInstance();
    ProfileType oldProfile = profileManager.getActiveProfile();

    if (newProfile == oldProfile) return false;
    if (newProfile >= ProfileType::COUNT) return false;

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

void SettingsManager::saveSettings(const HudManager& hudManager, const char* savePath) {
    std::string filePath = getSettingsFilePath(savePath);
    m_savePath = savePath ? savePath : "";

    // Capture current state to active profile before saving
    const_cast<SettingsManager*>(this)->captureCurrentState(hudManager);

    std::ofstream file(filePath);
    if (!file.is_open()) {
        DEBUG_WARN_F("Failed to save settings to: %s", filePath.c_str());
        return;
    }

    DEBUG_INFO_F("Saving settings to: %s", filePath.c_str());

    // Write meta section (global, not per-profile)
    const ProfileManager& profileManager = ProfileManager::getInstance();
    file << "[Meta]\n";
    file << "activeProfile=" << static_cast<int>(profileManager.getActiveProfile()) << "\n";
    file << "autoSwitch=" << (profileManager.isAutoSwitchEnabled() ? 1 : 0) << "\n\n";

    // Write all profiles (including ColorConfig per-profile)
    static const std::array<const char*, 24> hudOrder = {
        "ColorConfig",
        "StandingsHud", "MapHud", "RadarHud", "PitboardHud", "RecordsHud",
        "LapLogHud", "SessionBestHud", "TelemetryHud", "InputHud", "PerformanceHud",
        "LapWidget", "PositionWidget", "TimeWidget", "SessionWidget", "SpeedWidget",
        "SpeedoWidget", "TachoWidget", "TimingWidget", "BarsWidget", "VersionWidget",
        "NoticesWidget", "FuelWidget", "SettingsButtonWidget"
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
                if (key == "visible" || key == "showTitle" || key == "showBackgroundTexture" ||
                    key == "backgroundOpacity" || key == "scale" || key == "offsetX" || key == "offsetY") {
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

        // Handle Meta section
        if (currentHudName == "Meta") {
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

        // Handle profile-specific HUD settings (v2 format only)
        // Note: ColorConfig is now per-profile and handled here as well
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

    DEBUG_INFO("Settings loaded successfully");
}
