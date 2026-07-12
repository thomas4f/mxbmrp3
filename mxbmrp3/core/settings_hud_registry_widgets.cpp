// ============================================================================
// mxbmrp3/core/settings_hud_registry_widgets.cpp
// Per-HUD settings serializers (cap_*/app_*), group 2: widgets, simpler HUDs, and Global. The registry TABLE and group-1 (full-HUD) serializers stay in settings_hud_registry.cpp.
// (extracted verbatim from settings_hud_registry.cpp; no behavior change)
// ============================================================================

#include "settings_hud_registry.h"
// profile-level operations built on them (switch, copy-to-all, and the reset
// family). Split out of settings_manager.cpp, which keeps global-section
// serialization, the file serialize/build helpers, and save/load orchestration.
// ============================================================================
#include "settings_manager.h"
#include "settings_keys.h"
#include "settings_serde.h"
#include "atomic_file_writer.h"
#include "hud_manager.h"
#include "profile_manager.h"
#include "../diagnostics/logger.h"
#include "../hud/ideal_lap_hud.h"
#include "../hud/lap_log_hud.h"
#include "../hud/friends_hud.h"
#include "../hud/session_charts_hud.h"
#include "../hud/standings_hud.h"
#include "../hud/performance_hud.h"
#include "../hud/telemetry_hud.h"
#include "../hud/time_widget.h"
#include "../hud/clock_widget.h"
#include "../hud/position_widget.h"
#include "../hud/lap_widget.h"
#include "../hud/session_hud.h"
#include "../hud/speed_widget.h"
#include "../hud/gear_widget.h"
#include "../hud/speedo_widget.h"
#include "../hud/tacho_widget.h"
#include "../hud/timing_hud.h"
#include "../hud/gap_bar_hud.h"
#include "../hud/bars_widget.h"
#include "../hud/version_widget.h"
#include "../hud/notices_hud.h"
#include "../hud/fuel_widget.h"
#include "../hud/settings_button_widget.h"
#include "../hud/pointer_widget.h"
#include "../hud/map_hud.h"
#include "../hud/radar_hud.h"
#include "../hud/pitboard_hud.h"
// settings_hud.h is core (every game has the settings menu, and getSettingsHud() is
// used unconditionally below), and it pulls records_hud.h itself; both .cpp files are
// compiled on every game, so neither include may be gated on GAME_HAS_RECORDS_PROVIDER
// — gating it broke the GPB/KRP builds (SettingsHud left incomplete -> C2027). The
// *provider* feature stays runtime/registration-gated; only these includes are always on.
#include "../hud/records_hud.h"
#include "../hud/settings_hud.h"
#include "../hud/rumble_hud.h"
#include "../hud/helmet_overlay_hud.h"
#include "../hud/benchmark_widget.h"
#include "../hud/gamepad_widget.h"
#include "../hud/lean_widget.h"
#include "../hud/gforce_widget.h"
#include "../hud/compass_widget.h"
#if GAME_HAS_TYRE_TEMP
#include "../hud/tyre_temp_widget.h"
#endif
#if GAME_HAS_ECU
#include "../hud/ecu_widget.h"
#endif
#include "../hud/fmx_hud.h"
#include "../hud/stats_hud.h"
#include "../hud/event_log_hud.h"
#include "fmx_manager.h"
#include "color_config.h"
#include "font_config.h"
#include "ui_config.h"
#include "update_checker.h"
#include "update_downloader.h"
#if GAME_HAS_DISCORD
#include "discord_manager.h"
#endif
#if GAME_HAS_STEAM_FRIENDS
#include "steam_friends_manager.h"
#endif
#if GAME_HAS_HTTP_SERVER
#include "http_server.h"
#endif
#if GAME_HAS_RECORDER
#include "event_recorder.h"
#endif
#if GAME_HAS_ANALYTICS
#include "analytics_manager.h"
#endif
#include "xinput_reader.h"
#include "hotkey_manager.h"
#include "director_manager.h"
#include "companion_window.h"
#include "../hud/director_widget.h"
#include "tracked_riders_manager.h"
#include "asset_manager.h"
#include "../game/game_config.h"
#include <fstream>
#include <sstream>
#include <array>
#include <vector>

// Bring the centralized INI key names / serde helpers into scope.
using namespace Settings;

void SettingsManager::cap_LapWidget(const HudManager& hudManager, SettingsManager::ProfileCache& cache, const char* name) {
    HudSettings settings;
    const BaseHud& hud = hudManager.getLapWidget();
    captureBaseHudSettings(settings, hud);
    cache[name] = std::move(settings);
}

void SettingsManager::app_LapWidget(HudManager& hudManager, const SettingsManager::ProfileCache& cache, const char* name) {
    auto it = cache.find(name);
    if (it == cache.end()) return;
    BaseHud& hud = hudManager.getLapWidget();
    applyBaseHudSettings(hud, it->second);
    hud.setDataDirty();
}

void SettingsManager::cap_PositionWidget(const HudManager& hudManager, SettingsManager::ProfileCache& cache, const char* name) {
    HudSettings settings;
    const BaseHud& hud = hudManager.getPositionWidget();
    captureBaseHudSettings(settings, hud);
    cache[name] = std::move(settings);
}

void SettingsManager::app_PositionWidget(HudManager& hudManager, const SettingsManager::ProfileCache& cache, const char* name) {
    auto it = cache.find(name);
    if (it == cache.end()) return;
    BaseHud& hud = hudManager.getPositionWidget();
    applyBaseHudSettings(hud, it->second);
    hud.setDataDirty();
}

void SettingsManager::cap_TimeWidget(const HudManager& hudManager, SettingsManager::ProfileCache& cache, const char* name) {
    HudSettings settings;
    const BaseHud& hud = hudManager.getTimeWidget();
    captureBaseHudSettings(settings, hud);
    cache[name] = std::move(settings);
}

void SettingsManager::app_TimeWidget(HudManager& hudManager, const SettingsManager::ProfileCache& cache, const char* name) {
    auto it = cache.find(name);
    if (it == cache.end()) return;
    BaseHud& hud = hudManager.getTimeWidget();
    applyBaseHudSettings(hud, it->second);
    hud.setDataDirty();
}

void SettingsManager::cap_ClockWidget(const HudManager& hudManager, SettingsManager::ProfileCache& cache, const char* name) {
        HudSettings settings;
        const auto& hud = hudManager.getClockWidget();
        captureBaseHudSettings(settings, hud);
        settings["showUtc"] = hud.getShowUtc() ? "1" : "0";
        settings["utcOnTop"] = hud.getUtcOnTop() ? "1" : "0";
        cache[name] = std::move(settings);
}

void SettingsManager::app_ClockWidget(HudManager& hudManager, const SettingsManager::ProfileCache& cache, const char* name) {
        auto it = cache.find(name);
        if (it != cache.end()) {
            auto& hud = hudManager.getClockWidget();
            applyBaseHudSettings(hud, it->second);

            const auto& settings = it->second;
            try {
                if (settings.count("showUtc")) {
                    hud.setShowUtc(std::stoi(settings.at("showUtc")) != 0);
                }
                if (settings.count("utcOnTop")) {
                    hud.setUtcOnTop(std::stoi(settings.at("utcOnTop")) != 0);
                }
            } catch (const std::exception& e) {
                DEBUG_WARN_F("ClockWidget: Failed to parse settings: %s", e.what());
            }
            hud.setDataDirty();
        }
}

void SettingsManager::cap_SessionHud(const HudManager& hudManager, SettingsManager::ProfileCache& cache, const char* name) {
        HudSettings settings;
        const auto& hud = hudManager.getSessionHud();
        captureBaseHudSettings(settings, hud);
        saveSessionRows(settings, hud.m_enabledRows);
        settings[Keys::Session::SHOW_ICONS] = std::to_string(hud.m_bShowIcons ? 1 : 0);
        cache[name] = std::move(settings);
}

void SettingsManager::app_SessionHud(HudManager& hudManager, const SettingsManager::ProfileCache& cache, const char* name) {
        auto it = cache.find(name);
        if (it == cache.end()) {
            it = cache.find("SessionWidget");  // Backwards compatibility
        }
        if (it != cache.end()) {
            auto& hud = hudManager.getSessionHud();
            applyBaseHudSettings(hud, it->second);

            const auto& settings = it->second;
            try {
                loadSessionRows(settings, hud.m_enabledRows);
                if (settings.count(Keys::Session::SHOW_ICONS)) {
                    hud.m_bShowIcons = (std::stoi(settings.at(Keys::Session::SHOW_ICONS)) != 0);
                }
            } catch (const std::exception& e) {
                DEBUG_WARN_F("SessionHud: Failed to parse settings: %s", e.what());
            }
            hud.setDataDirty();
        }
}

void SettingsManager::cap_SpeedWidget(const HudManager& hudManager, SettingsManager::ProfileCache& cache, const char* name) {
        HudSettings settings;
        const auto& hud = hudManager.getSpeedWidget();
        captureBaseHudSettings(settings, hud);
        saveSpeedRows(settings, hud.m_enabledRows);  // Named keys instead of bitmask
        cache[name] = std::move(settings);
}

void SettingsManager::app_SpeedWidget(HudManager& hudManager, const SettingsManager::ProfileCache& cache, const char* name) {
        auto it = cache.find(name);
        if (it != cache.end()) {
            auto& hud = hudManager.getSpeedWidget();
            applyBaseHudSettings(hud, it->second);

            const auto& settings = it->second;
            try {
                loadSpeedRows(settings, hud.m_enabledRows);  // Named keys instead of bitmask
            } catch (const std::exception& e) {
                DEBUG_WARN_F("SpeedWidget: Failed to parse settings: %s", e.what());
            }
            hud.setDataDirty();
        }
}

void SettingsManager::cap_GearWidget(const HudManager& hudManager, SettingsManager::ProfileCache& cache, const char* name) {
        HudSettings settings;
        const auto& hud = hudManager.getGearWidget();
        captureBaseHudSettings(settings, hud);
        settings[IniOnly::Gear::SHOW_SHIFT_COLOR.key] = hud.m_bShowShiftColor ? "1" : "0";
        settings[IniOnly::Gear::SHOW_LIMITER_CIRCLE.key] = hud.m_bShowLimiterCircle ? "1" : "0";
        cache[name] = std::move(settings);
}

void SettingsManager::app_GearWidget(HudManager& hudManager, const SettingsManager::ProfileCache& cache, const char* name) {
        auto it = cache.find(name);
        if (it != cache.end()) {
            auto& hud = hudManager.getGearWidget();
            applyBaseHudSettings(hud, it->second);

            const auto& settings = it->second;
            try {
                if (settings.count(IniOnly::Gear::SHOW_SHIFT_COLOR.key)) {
                    hud.m_bShowShiftColor = std::stoi(settings.at(IniOnly::Gear::SHOW_SHIFT_COLOR.key)) != 0;
                }
                if (settings.count(IniOnly::Gear::SHOW_LIMITER_CIRCLE.key)) {
                    hud.m_bShowLimiterCircle = std::stoi(settings.at(IniOnly::Gear::SHOW_LIMITER_CIRCLE.key)) != 0;
                }
            } catch (const std::exception& e) {
                DEBUG_WARN_F("GearWidget: Failed to parse settings: %s", e.what());
            }
            hud.setDataDirty();
        }
}

void SettingsManager::cap_SpeedoWidget(const HudManager& hudManager, SettingsManager::ProfileCache& cache, const char* name) {
        HudSettings settings;
        const auto& hud = hudManager.getSpeedoWidget();
        captureBaseHudSettings(settings, hud);
        settings[IniOnly::Speedo::NEEDLE_COLOR.key] = PluginUtils::formatColorHex(hud.getNeedleColor());
        settings[IniOnly::Speedo::SHOW_ODOMETER.key] = hud.getShowOdometer() ? "1" : "0";
        settings[IniOnly::Speedo::SHOW_TRIPMETER.key] = hud.getShowTripmeter() ? "1" : "0";
        cache[name] = std::move(settings);
}

void SettingsManager::app_SpeedoWidget(HudManager& hudManager, const SettingsManager::ProfileCache& cache, const char* name) {
        auto it = cache.find(name);
        if (it != cache.end()) {
            auto& hud = hudManager.getSpeedoWidget();
            applyBaseHudSettings(hud, it->second);

            const auto& settings = it->second;
            try {
                if (settings.count(IniOnly::Speedo::NEEDLE_COLOR.key)) {
                    hud.setNeedleColor(PluginUtils::parseColorHex(settings.at(IniOnly::Speedo::NEEDLE_COLOR.key), hud.getNeedleColor()));
                }
                if (settings.count(IniOnly::Speedo::SHOW_ODOMETER.key)) {
                    hud.setShowOdometer(std::stoi(settings.at(IniOnly::Speedo::SHOW_ODOMETER.key)) != 0);
                }
                if (settings.count(IniOnly::Speedo::SHOW_TRIPMETER.key)) {
                    hud.setShowTripmeter(std::stoi(settings.at(IniOnly::Speedo::SHOW_TRIPMETER.key)) != 0);
                }
            } catch (const std::exception& e) {
                DEBUG_WARN_F("SpeedoWidget: Failed to parse settings: %s", e.what());
            }
            hud.setDataDirty();
        }
}

void SettingsManager::cap_TachoWidget(const HudManager& hudManager, SettingsManager::ProfileCache& cache, const char* name) {
        HudSettings settings;
        const auto& hud = hudManager.getTachoWidget();
        captureBaseHudSettings(settings, hud);
        settings[IniOnly::Tacho::NEEDLE_COLOR.key] = PluginUtils::formatColorHex(hud.getNeedleColor());
        cache[name] = std::move(settings);
}

void SettingsManager::app_TachoWidget(HudManager& hudManager, const SettingsManager::ProfileCache& cache, const char* name) {
        auto it = cache.find(name);
        if (it != cache.end()) {
            auto& hud = hudManager.getTachoWidget();
            applyBaseHudSettings(hud, it->second);

            const auto& settings = it->second;
            try {
                if (settings.count(IniOnly::Tacho::NEEDLE_COLOR.key)) {
                    hud.setNeedleColor(PluginUtils::parseColorHex(settings.at(IniOnly::Tacho::NEEDLE_COLOR.key), hud.getNeedleColor()));
                }
            } catch (const std::exception& e) {
                DEBUG_WARN_F("TachoWidget: Failed to parse settings: %s", e.what());
            }
            hud.setDataDirty();
        }
}

void SettingsManager::cap_TimingHud(const HudManager& hudManager, SettingsManager::ProfileCache& cache, const char* name) {
        HudSettings settings;
        const auto& hud = hudManager.getTimingHud();
        captureBaseHudSettings(settings, hud);
        settings["displayMode"] = columnModeToString(hud.m_displayMode);
        settings["showTime"] = hud.m_showTime ? "1" : "0";
        settings["displayDuration"] = std::to_string(hud.m_displayDurationMs);
        // Comparison rows (one bit per gap type) — reuses the per-key bitmask serializer.
        saveTimingSecondaryGaps(settings, hud.m_enabledComparisons);
        cache[name] = std::move(settings);
}

void SettingsManager::app_TimingHud(HudManager& hudManager, const SettingsManager::ProfileCache& cache, const char* name) {
        auto it = cache.find(name);
        if (it != cache.end()) {
            auto& hud = hudManager.getTimingHud();
            applyBaseHudSettings(hud, it->second);

            const auto& settings = it->second;
            try {
                if (settings.count("displayMode")) {
                    hud.m_displayMode = stringToColumnMode(settings.at("displayMode"));
                }
                if (settings.count("showTime")) {
                    hud.m_showTime = settings.at("showTime") == "1";
                }
                if (settings.count("displayDuration")) {
                    int duration = std::stoi(settings.at("displayDuration"));
                    if (duration >= TimingHud::MIN_DURATION_MS && duration <= TimingHud::MAX_DURATION_MS) {
                        hud.m_displayDurationMs = duration;
                    }
                }
                loadTimingSecondaryGaps(settings, hud.m_enabledComparisons);
            } catch (const std::exception& e) {
                DEBUG_WARN_F("TimingHud: Failed to parse settings: %s", e.what());
            }
            hud.setDataDirty();
        }
}

void SettingsManager::cap_GapBarHud(const HudManager& hudManager, SettingsManager::ProfileCache& cache, const char* name) {
        HudSettings settings;
        const auto& hud = hudManager.getGapBarHud();
        captureBaseHudSettings(settings, hud);
        settings["freezeDuration"] = std::to_string(hud.m_freezeDurationMs);
        settings["markerMode"] = std::to_string(static_cast<int>(hud.m_markerMode));
        // Persist by filename (not positional index) so the choice survives icon-set
        // reordering, matching map/radar. 0 = "use default icon" serializes as "Off".
        settings["riderIcon"] = shapeIndexToFilename(hud.m_riderIconIndex);
        settings["showGapText"] = hud.m_showGapText ? "1" : "0";
        settings["showGapBar"] = hud.m_showGapBar ? "1" : "0";
        settings["gapRange"] = std::to_string(hud.m_gapRangeMs);
        settings["barWidth"] = std::to_string(hud.m_barWidthPercent);
        settings["markerScale"] = std::to_string(hud.m_fMarkerScale);
        settings["labelMode"] = std::to_string(static_cast<int>(hud.m_labelMode));
        settings["colorMode"] = gapBarRiderColorModeToString(hud.m_riderColorMode);
        cache[name] = std::move(settings);
}

void SettingsManager::app_GapBarHud(HudManager& hudManager, const SettingsManager::ProfileCache& cache, const char* name) {
        auto it = cache.find(name);
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
                // New marker mode setting (replaces old showMarkers boolean)
                if (settings.count("markerMode")) {
                    int mode = std::stoi(settings.at("markerMode"));
                    if (mode >= 0 && mode <= 2) {
                        hud.m_markerMode = static_cast<GapBarHud::MarkerMode>(mode);
                    }
                }
                // Legacy compatibility: convert old showMarkers boolean to markerMode
                else if (settings.count("showMarkers") || settings.count("showMarker")) {
                    bool showMarkers = settings.count("showMarkers") ?
                        (settings.at("showMarkers") == "1") :
                        (settings.at("showMarker") == "1");
                    // Old behavior: markers on = ghost mode, off = still ghost mode (markers always shown now)
                    hud.m_markerMode = GapBarHud::MarkerMode::GHOST;
                }
                // Rider icon (name-based; 0/"Off" = use default icon)
                if (settings.count("riderIcon")) {
                    hud.m_riderIconIndex = filenameToShapeIndex(settings.at("riderIcon"), 0);
                }
                // Show gap text toggle
                if (settings.count("showGapText")) {
                    hud.m_showGapText = (settings.at("showGapText") == "1");
                }
                // Show gap bar toggle (green/red visualization)
                if (settings.count("showGapBar")) {
                    hud.m_showGapBar = (settings.at("showGapBar") == "1");
                }
                // Gap range
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
                // Marker scale
                if (settings.count("markerScale")) {
                    float scale = parseFiniteFloat(settings.at("markerScale"));
                    if (scale >= GapBarHud::MIN_MARKER_SCALE && scale <= GapBarHud::MAX_MARKER_SCALE) {
                        hud.m_fMarkerScale = scale;
                    }
                }
                // Label mode
                if (settings.count("labelMode")) {
                    int mode = std::stoi(settings.at("labelMode"));
                    if (mode >= 0 && mode <= 3) {
                        hud.m_labelMode = static_cast<GapBarHud::LabelMode>(mode);
                    }
                }
                // Color mode (string format, with backwards compatibility for integer format)
                if (settings.count("colorMode")) {
                    hud.m_riderColorMode = stringToGapBarRiderColorMode(settings.at("colorMode"));
                }
            } catch (const std::exception& e) {
                DEBUG_WARN_F("GapBarHud: Failed to parse settings: %s", e.what());
            }
            hud.setDataDirty();
        }
}

void SettingsManager::cap_BarsWidget(const HudManager& hudManager, SettingsManager::ProfileCache& cache, const char* name) {
        HudSettings settings;
        const auto& hud = hudManager.getBarsWidget();
        captureBaseHudSettings(settings, hud);
        saveBarsColumns(settings, hud.m_enabledColumns);
        settings["showLabels"] = hud.m_bShowLabels ? "1" : "0";
        settings["showMaxMarkers"] = hud.m_bShowMaxMarkers ? "1" : "0";
        settings["maxMarkerLingerFrames"] = std::to_string(hud.m_maxMarkerLingerFrames);
        cache[name] = std::move(settings);
}

void SettingsManager::app_BarsWidget(HudManager& hudManager, const SettingsManager::ProfileCache& cache, const char* name) {
        auto it = cache.find(name);
        if (it != cache.end()) {
            auto& hud = hudManager.getBarsWidget();
            applyBaseHudSettings(hud, it->second);

            const auto& settings = it->second;
            try {
                loadBarsColumns(settings, hud.m_enabledColumns);
                if (settings.count("showLabels")) {
                    hud.m_bShowLabels = std::stoi(settings.at("showLabels")) != 0;
                }
                if (settings.count("showMaxMarkers")) {
                    hud.m_bShowMaxMarkers = std::stoi(settings.at("showMaxMarkers")) != 0;
                }
                if (settings.count("maxMarkerLingerFrames")) {
                    hud.m_maxMarkerLingerFrames = std::stoi(settings.at("maxMarkerLingerFrames"));
                }
            } catch (const std::exception& e) {
                DEBUG_WARN_F("BarsWidget: Failed to parse settings: %s", e.what());
            }
            hud.setDataDirty();
        }
}

void SettingsManager::cap_VersionWidget(const HudManager& hudManager, SettingsManager::ProfileCache& cache, const char* name) {
    HudSettings settings;
    const BaseHud& hud = hudManager.getVersionWidget();
    captureBaseHudSettings(settings, hud);
    cache[name] = std::move(settings);
}

void SettingsManager::app_VersionWidget(HudManager& hudManager, const SettingsManager::ProfileCache& cache, const char* name) {
    auto it = cache.find(name);
    if (it == cache.end()) return;
    BaseHud& hud = hudManager.getVersionWidget();
    applyBaseHudSettings(hud, it->second);
    hud.setDataDirty();
}

void SettingsManager::cap_NoticesHud(const HudManager& hudManager, SettingsManager::ProfileCache& cache, const char* name) {
        HudSettings settings;
        const auto& hud = hudManager.getNoticesHud();
        captureBaseHudSettings(settings, hud);
        saveNotices(settings, hud.m_enabledNotices);
        settings[IniOnly::Notices::PB_DURATION.key] = std::to_string(hud.m_noticeDurationMs);
        cache[name] = std::move(settings);
}

void SettingsManager::app_NoticesHud(HudManager& hudManager, const SettingsManager::ProfileCache& cache, const char* name) {
        auto it = cache.find(name);
        if (it != cache.end()) {
            auto& hud = hudManager.getNoticesHud();
            applyBaseHudSettings(hud, it->second);

            const auto& settings = it->second;
            try {
                loadNotices(settings, hud.m_enabledNotices);
                if (settings.count(IniOnly::Notices::PB_DURATION.key)) {
                    int duration = std::stoi(settings.at(IniOnly::Notices::PB_DURATION.key));
                    if (duration >= NoticesHud::MIN_NOTICE_DURATION_MS && duration <= NoticesHud::MAX_NOTICE_DURATION_MS) {
                        hud.m_noticeDurationMs = duration;
                    }
                }
            } catch (const std::exception& e) {
                DEBUG_WARN_F("NoticesHud: Failed to parse settings: %s", e.what());
            }
            hud.setDataDirty();
        }
}

void SettingsManager::cap_FuelWidget(const HudManager& hudManager, SettingsManager::ProfileCache& cache, const char* name) {
        HudSettings settings;
        const auto& hud = hudManager.getFuelWidget();
        captureBaseHudSettings(settings, hud);
        saveFuelRows(settings, hud.m_enabledRows);  // Named keys instead of bitmask
        cache[name] = std::move(settings);
}

void SettingsManager::app_FuelWidget(HudManager& hudManager, const SettingsManager::ProfileCache& cache, const char* name) {
        auto it = cache.find(name);
        if (it != cache.end()) {
            auto& hud = hudManager.getFuelWidget();
            applyBaseHudSettings(hud, it->second);

            const auto& settings = it->second;
            try {
                loadFuelRows(settings, hud.m_enabledRows);  // Named keys instead of bitmask
            } catch (const std::exception& e) {
                DEBUG_WARN_F("FuelWidget: Failed to parse settings: %s", e.what());
            }
            hud.setDataDirty();
        }
}

void SettingsManager::cap_GamepadWidget(const HudManager& hudManager, SettingsManager::ProfileCache& cache, const char* name) {
        HudSettings settings;
        const auto& hud = hudManager.getGamepadWidget();
        captureBaseHudSettings(settings, hud);
        cache[name] = std::move(settings);
}

void SettingsManager::app_GamepadWidget(HudManager& hudManager, const SettingsManager::ProfileCache& cache, const char* name) {
        auto it = cache.find(name);
        if (it != cache.end()) {
            auto& hud = hudManager.getGamepadWidget();
            applyBaseHudSettings(hud, it->second);
            hud.setDataDirty();
        }
}

void SettingsManager::cap_LeanWidget(const HudManager& hudManager, SettingsManager::ProfileCache& cache, const char* name) {
        HudSettings settings;
        const auto& hud = hudManager.getLeanWidget();
        captureBaseHudSettings(settings, hud);
        saveLeanRows(settings, hud.m_enabledRows);  // Named keys instead of bitmask
        settings["showMaxMarkers"] = hud.m_bShowMaxMarkers ? "1" : "0";
        settings["maxMarkerLingerFrames"] = std::to_string(hud.m_maxMarkerLingerFrames);
        settings[IniOnly::Lean::ARC_FILL_COLOR.key] = PluginUtils::formatColorHex(hud.getArcFillColor());
        cache[name] = std::move(settings);
}

void SettingsManager::app_LeanWidget(HudManager& hudManager, const SettingsManager::ProfileCache& cache, const char* name) {
        auto it = cache.find(name);
        if (it != cache.end()) {
            auto& hud = hudManager.getLeanWidget();
            applyBaseHudSettings(hud, it->second);

            const auto& settings = it->second;
            try {
                loadLeanRows(settings, hud.m_enabledRows);  // Named keys instead of bitmask
                if (settings.count("showMaxMarkers")) {
                    hud.m_bShowMaxMarkers = std::stoi(settings.at("showMaxMarkers")) != 0;
                }
                if (settings.count("maxMarkerLingerFrames")) {
                    hud.m_maxMarkerLingerFrames = std::stoi(settings.at("maxMarkerLingerFrames"));
                }
                if (settings.count(IniOnly::Lean::ARC_FILL_COLOR.key)) {
                    hud.setArcFillColor(PluginUtils::parseColorHex(settings.at(IniOnly::Lean::ARC_FILL_COLOR.key), hud.getArcFillColor()));
                }
            } catch (const std::exception& e) {
                DEBUG_WARN_F("LeanWidget: Failed to parse settings: %s", e.what());
            }
            hud.setDataDirty();
        }
}

void SettingsManager::cap_GForceWidget(const HudManager& hudManager, SettingsManager::ProfileCache& cache, const char* name) {
        HudSettings settings;
        const auto& hud = hudManager.getGForceWidget();
        captureBaseHudSettings(settings, hud);
        settings[IniOnly::GForce::MAX_SCALE.key] = std::to_string(hud.m_maxScale);
        settings[IniOnly::GForce::SHOW_MAX_TEXT.key] = hud.m_bShowMaxText ? "1" : "0";
        settings[IniOnly::GForce::SHOW_MAX_MARKER.key] = hud.m_bShowMaxMarker ? "1" : "0";
        settings[IniOnly::GForce::MAX_MARKER_LINGER_FRAMES.key] = std::to_string(hud.m_maxMarkerLingerFrames);
        cache[name] = std::move(settings);
}

void SettingsManager::app_GForceWidget(HudManager& hudManager, const SettingsManager::ProfileCache& cache, const char* name) {
        auto it = cache.find(name);
        if (it != cache.end()) {
            auto& hud = hudManager.getGForceWidget();
            applyBaseHudSettings(hud, it->second);

            const auto& settings = it->second;
            try {
                if (settings.count(IniOnly::GForce::MAX_SCALE.key)) {
                    float v = parseFiniteFloat(settings.at(IniOnly::GForce::MAX_SCALE.key));
                    if (v > 0.1f) hud.m_maxScale = v;
                }
                if (settings.count(IniOnly::GForce::SHOW_MAX_TEXT.key)) {
                    hud.m_bShowMaxText = std::stoi(settings.at(IniOnly::GForce::SHOW_MAX_TEXT.key)) != 0;
                }
                if (settings.count(IniOnly::GForce::SHOW_MAX_MARKER.key)) {
                    hud.m_bShowMaxMarker = std::stoi(settings.at(IniOnly::GForce::SHOW_MAX_MARKER.key)) != 0;
                }
                if (settings.count(IniOnly::GForce::MAX_MARKER_LINGER_FRAMES.key)) {
                    hud.m_maxMarkerLingerFrames = std::stoi(settings.at(IniOnly::GForce::MAX_MARKER_LINGER_FRAMES.key));
                }
            } catch (const std::exception& e) {
                DEBUG_WARN_F("GForceWidget: Failed to parse settings: %s", e.what());
            }
            hud.setDataDirty();
        }
}

void SettingsManager::cap_CompassWidget(const HudManager& hudManager, SettingsManager::ProfileCache& cache, const char* name) {
        HudSettings settings;
        const auto& hud = hudManager.getCompassWidget();
        captureBaseHudSettings(settings, hud);
        settings[IniOnly::Compass::STYLE.key] = compassStyleToString(hud.m_style);
        cache[name] = std::move(settings);
}

void SettingsManager::app_CompassWidget(HudManager& hudManager, const SettingsManager::ProfileCache& cache, const char* name) {
        auto it = cache.find(name);
        if (it != cache.end()) {
            auto& hud = hudManager.getCompassWidget();
            applyBaseHudSettings(hud, it->second);

            const auto& settings = it->second;
            try {
                if (settings.count(IniOnly::Compass::STYLE.key)) {
                    hud.m_style = stringToCompassStyle(settings.at(IniOnly::Compass::STYLE.key), hud.m_style);
                }
            } catch (const std::exception& e) {
                DEBUG_WARN_F("CompassWidget: Failed to parse settings: %s", e.what());
            }
            hud.setDataDirty();
        }
}

#if GAME_HAS_TYRE_TEMP
void SettingsManager::cap_TyreTempWidget(const HudManager& hudManager, SettingsManager::ProfileCache& cache, const char* name) {
        HudSettings settings;
        const auto& hud = hudManager.getTyreTempWidget();
        captureBaseHudSettings(settings, hud);
        settings["coldThreshold"] = std::to_string(hud.getColdThreshold());
        settings["hotThreshold"] = std::to_string(hud.getHotThreshold());
        saveTyreTempRows(settings, hud.m_enabledRows);  // Named keys for row toggles
        settings["showLabels"] = hud.m_bShowLabels ? "1" : "0";  // INI-only
        cache[name] = std::move(settings);
}

void SettingsManager::app_TyreTempWidget(HudManager& hudManager, const SettingsManager::ProfileCache& cache, const char* name) {
        auto it = cache.find(name);
        if (it != cache.end()) {
            auto& hud = hudManager.getTyreTempWidget();
            applyBaseHudSettings(hud, it->second);

            const auto& settings = it->second;
            try {
                if (settings.count("coldThreshold")) {
                    hud.setColdThreshold(parseFiniteFloat(settings.at("coldThreshold")));
                }
                if (settings.count("hotThreshold")) {
                    hud.setHotThreshold(parseFiniteFloat(settings.at("hotThreshold")));
                }
                loadTyreTempRows(settings, hud.m_enabledRows);  // Named keys for row toggles
                if (settings.count("showLabels")) {
                    hud.m_bShowLabels = std::stoi(settings.at("showLabels")) != 0;
                }
            } catch (const std::exception& e) {
                DEBUG_WARN_F("TyreTempWidget: Failed to parse settings: %s", e.what());
            }
            hud.setDataDirty();
        }
}
#endif

#if GAME_HAS_ECU
void SettingsManager::cap_EcuWidget(const HudManager& hudManager, SettingsManager::ProfileCache& cache, const char* name) {
        HudSettings settings;
        const auto& hud = hudManager.getEcuWidget();
        captureBaseHudSettings(settings, hud);
        saveEcuRows(settings, hud.m_enabledRows);  // Named keys for chip toggles
        settings["showLabels"] = hud.m_bShowLabels ? "1" : "0";
        cache[name] = std::move(settings);
}

void SettingsManager::app_EcuWidget(HudManager& hudManager, const SettingsManager::ProfileCache& cache, const char* name) {
        auto it = cache.find(name);
        if (it != cache.end()) {
            auto& hud = hudManager.getEcuWidget();
            applyBaseHudSettings(hud, it->second);

            const auto& settings = it->second;
            try {
                loadEcuRows(settings, hud.m_enabledRows);  // Named keys for chip toggles
                if (settings.count("showLabels")) {
                    hud.m_bShowLabels = std::stoi(settings.at("showLabels")) != 0;
                }
            } catch (const std::exception& e) {
                DEBUG_WARN_F("EcuWidget: Failed to parse settings: %s", e.what());
            }
            hud.setDataDirty();
        }
}
#endif

void SettingsManager::cap_SettingsButtonWidget(const HudManager& hudManager, SettingsManager::ProfileCache& cache, const char* name) {
    HudSettings settings;
    const BaseHud& hud = hudManager.getSettingsButtonWidget();
    captureBaseHudSettings(settings, hud);
    cache[name] = std::move(settings);
}

void SettingsManager::app_SettingsButtonWidget(HudManager& hudManager, const SettingsManager::ProfileCache& cache, const char* name) {
    auto it = cache.find(name);
    if (it == cache.end()) return;
    BaseHud& hud = hudManager.getSettingsButtonWidget();
    applyBaseHudSettings(hud, it->second);
    hud.setDataDirty();
}

void SettingsManager::cap_PointerWidget(const HudManager& hudManager, SettingsManager::ProfileCache& cache, const char* name) {
    HudSettings settings;
    const BaseHud& hud = hudManager.getPointerWidget();
    captureBaseHudSettings(settings, hud);
    cache[name] = std::move(settings);
}

void SettingsManager::app_PointerWidget(HudManager& hudManager, const SettingsManager::ProfileCache& cache, const char* name) {
    auto it = cache.find(name);
    if (it == cache.end()) return;
    BaseHud& hud = hudManager.getPointerWidget();
    applyBaseHudSettings(hud, it->second);
    hud.setDataDirty();
}

void SettingsManager::cap_RumbleHud(const HudManager& hudManager, SettingsManager::ProfileCache& cache, const char* name) {
        HudSettings settings;
        const auto& hud = hudManager.getRumbleHud();
        captureBaseHudSettings(settings, hud);
        settings["showMaxMarkers"] = hud.m_bShowMaxMarkers ? "1" : "0";
        settings["maxMarkerLingerFrames"] = std::to_string(hud.m_maxMarkerLingerFrames);
        cache[name] = std::move(settings);
}

void SettingsManager::app_RumbleHud(HudManager& hudManager, const SettingsManager::ProfileCache& cache, const char* name) {
        auto it = cache.find(name);
        if (it != cache.end()) {
            auto& hud = hudManager.getRumbleHud();
            applyBaseHudSettings(hud, it->second);

            const auto& settings = it->second;
            try {
                if (settings.count("showMaxMarkers")) {
                    hud.m_bShowMaxMarkers = std::stoi(settings.at("showMaxMarkers")) != 0;
                }
                if (settings.count("maxMarkerLingerFrames")) {
                    hud.m_maxMarkerLingerFrames = std::stoi(settings.at("maxMarkerLingerFrames"));
                }
            } catch (const std::exception& e) {
                DEBUG_WARN_F("RumbleHud: Failed to parse settings: %s", e.what());
            }
            hud.setDataDirty();
        }
}

void SettingsManager::cap_BenchmarkWidget(const HudManager& hudManager, SettingsManager::ProfileCache& cache, const char* name) {
    if (!hudManager.getBenchmarkWidget()) return;
    HudSettings settings;
    const BaseHud& hud = *hudManager.getBenchmarkWidget();
    captureBaseHudSettings(settings, hud);
    cache[name] = std::move(settings);
}

void SettingsManager::app_BenchmarkWidget(HudManager& hudManager, const SettingsManager::ProfileCache& cache, const char* name) {
    if (!hudManager.getBenchmarkWidget()) return;
    auto it = cache.find(name);
    if (it == cache.end()) return;
    BaseHud& hud = *hudManager.getBenchmarkWidget();
    applyBaseHudSettings(hud, it->second);
    hud.setDataDirty();
}

void SettingsManager::cap_Global(const HudManager& hudManager, SettingsManager::ProfileCache& cache, const char* name) {
        HudSettings settings;
        settings["widgetsEnabled"] = hudManager.areWidgetsEnabled() ? "1" : "0";
        cache[name] = std::move(settings);
}

void SettingsManager::app_Global(HudManager& hudManager, const SettingsManager::ProfileCache& cache, const char* name) {
        auto it = cache.find(name);
        if (it != cache.end()) {
            const auto& settings = it->second;
            try {
                if (settings.count("widgetsEnabled")) {
                    hudManager.setWidgetsEnabled(std::stoi(settings.at("widgetsEnabled")) != 0);
                }
            } catch (const std::exception& e) {
                DEBUG_WARN_F("Global: Failed to parse settings: %s", e.what());
            }
        }
}
