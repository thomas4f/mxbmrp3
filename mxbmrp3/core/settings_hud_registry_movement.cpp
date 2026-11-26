// ============================================================================
// mxbmrp3/core/settings_hud_registry_movement.cpp
// Per-HUD settings serializers (cap_*/app_*) for the movement widgets: Lean,
// Slide, Wheelie, Stance, G-Force and Compass. Split out of
// settings_hud_registry_widgets.cpp, which had reached its line budget; the
// registry TABLE stays in settings_hud_registry.cpp.
// ============================================================================
#include "settings_manager.h"
#include "settings_keys.h"
#include "settings_serde_hud.h"
#include "hud_manager.h"
#include "../diagnostics/logger.h"
#include "../game/game_config.h"
#include "../hud/lean_widget.h"
#include "../hud/slide_widget.h"
#include "../hud/wheelie_widget.h"
#if GAME_HAS_RIDER_STANCE
#include "../hud/stance_widget.h"
#endif
#include "../hud/gforce_widget.h"
#include "../hud/compass_widget.h"
#include <algorithm>
#include <string>

// Bring the centralized INI key names / serde helpers into scope.
using namespace Settings;

void SettingsManager::cap_LeanWidget(const HudManager& hudManager, SettingsManager::ProfileCache& cache, const char* name) {
        HudSettings settings;
        const auto& hud = hudManager.getLeanWidget();
        captureBaseHudSettings(settings, hud);
        saveLeanRows(settings, hud.m_enabledRows);  // Named keys instead of bitmask
        settings["showMaxMarkers"] = hud.m_bShowMaxMarkers ? "1" : "0";
        settings["maxMarkerLingerFrames"] = std::to_string(hud.m_maxMarkerLingerFrames);
        settings[IniOnly::Lean::FILL_COLOR_MODE.key] = std::to_string(static_cast<int>(hud.getFillColorMode()));
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
                if (auto v = readBool(settings, "showMaxMarkers")) hud.m_bShowMaxMarkers = *v;
                if (auto v = readInt(settings, "maxMarkerLingerFrames")) hud.m_maxMarkerLingerFrames = *v;
                if (auto v = readInt(settings, IniOnly::Lean::FILL_COLOR_MODE.key)) {
                    hud.setFillColorMode(*v == 1 ? LeanWidget::FillColorMode::FIXED
                                                 : LeanWidget::FillColorMode::RAMP);
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

void SettingsManager::cap_SlideWidget(const HudManager& hudManager, SettingsManager::ProfileCache& cache, const char* name) {
        HudSettings settings;
        const auto& hud = hudManager.getSlideWidget();
        captureBaseHudSettings(settings, hud);
        saveSlideRows(settings, hud.m_enabledRows);  // Named keys instead of bitmask
        settings[IniOnly::Slide::SHOW_MAX_MARKERS.key] = hud.m_bShowMaxMarkers ? "1" : "0";
        settings[IniOnly::Slide::MAX_MARKER_LINGER_FRAMES.key] = std::to_string(hud.m_maxMarkerLingerFrames);
        cache[name] = std::move(settings);
}

void SettingsManager::app_SlideWidget(HudManager& hudManager, const SettingsManager::ProfileCache& cache, const char* name) {
        auto it = cache.find(name);
        if (it != cache.end()) {
            auto& hud = hudManager.getSlideWidget();
            applyBaseHudSettings(hud, it->second);

            const auto& settings = it->second;
            try {
                loadSlideRows(settings, hud.m_enabledRows);  // Named keys instead of bitmask
                if (auto v = readBool(settings, IniOnly::Slide::SHOW_MAX_MARKERS.key)) hud.m_bShowMaxMarkers = *v;
                if (auto v = readInt(settings, IniOnly::Slide::MAX_MARKER_LINGER_FRAMES.key)) {
                    hud.m_maxMarkerLingerFrames = std::max(0, *v);
                }
            } catch (const std::exception& e) {
                DEBUG_WARN_F("SlideWidget: Failed to parse settings: %s", e.what());
            }
            hud.setDataDirty();
        }
}

#if GAME_HAS_WHEELIE
void SettingsManager::cap_WheelieWidget(const HudManager& hudManager, SettingsManager::ProfileCache& cache, const char* name) {
        HudSettings settings;
        const auto& hud = hudManager.getWheelieWidget();
        captureBaseHudSettings(settings, hud);
        settings[IniOnly::Wheelie::SHOW_VALUE.key] = hud.m_bShowValue ? "1" : "0";
        settings[IniOnly::Wheelie::SHOW_MAX_MARKER.key] = hud.m_bShowMaxMarker ? "1" : "0";
        settings[IniOnly::Wheelie::MAX_MARKER_LINGER_FRAMES.key] = std::to_string(hud.m_maxMarkerLingerFrames);
        cache[name] = std::move(settings);
}

void SettingsManager::app_WheelieWidget(HudManager& hudManager, const SettingsManager::ProfileCache& cache, const char* name) {
        auto it = cache.find(name);
        if (it != cache.end()) {
            auto& hud = hudManager.getWheelieWidget();
            applyBaseHudSettings(hud, it->second);

            const auto& settings = it->second;
            try {
                if (auto v = readBool(settings, IniOnly::Wheelie::SHOW_VALUE.key)) hud.m_bShowValue = *v;
                if (auto v = readBool(settings, IniOnly::Wheelie::SHOW_MAX_MARKER.key)) hud.m_bShowMaxMarker = *v;
                if (auto v = readInt(settings, IniOnly::Wheelie::MAX_MARKER_LINGER_FRAMES.key)) {
                    hud.m_maxMarkerLingerFrames = std::max(0, *v);
                }
            } catch (const std::exception& e) {
                DEBUG_WARN_F("WheelieWidget: Failed to parse settings: %s", e.what());
            }
            hud.setDataDirty();
        }
}
#endif

#if GAME_HAS_RIDER_STANCE
// The Stance widget has nothing of its own to serialize: position, scale,
// opacity and visibility.
void SettingsManager::cap_StanceWidget(const HudManager& hudManager, SettingsManager::ProfileCache& cache, const char* name) {
        const StanceWidget* hud = hudManager.getStanceWidget();
        if (!hud) return;
        HudSettings settings;
        captureBaseHudSettings(settings, *hud);
        settings[IniOnly::Stance::MODE.key] = stanceModeToString(hud->m_mode);
        // By NAME, like Map's riderShape and Gap Bar's riderIcon. 0 is the default.
        settings[IniOnly::Stance::ICON.key] = (hud->m_iconShape > 0)
            ? shapeIndexToFilename(hud->m_iconShape) : StanceWidget::DEFAULT_ICON;
        cache[name] = std::move(settings);
}

void SettingsManager::app_StanceWidget(HudManager& hudManager, const SettingsManager::ProfileCache& cache, const char* name) {
        StanceWidget* hud = hudManager.getStanceWidget();
        if (!hud) return;
        auto it = cache.find(name);
        if (it != cache.end()) {
            const auto& settings = it->second;
            applyBaseHudSettings(*hud, settings);
            if (auto v = readStr(settings, IniOnly::Stance::MODE.key)) hud->m_mode = stringToStanceMode(*v, hud->m_mode);
            // An unknown name draws the default icon (filenameToShapeIndex -> 0).
            if (auto v = readStr(settings, IniOnly::Stance::ICON.key)) hud->m_iconShape = filenameToShapeIndex(*v, 0);
            hud->setDataDirty();
        }
}
#endif

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
                if (auto v = readFloat(settings, IniOnly::GForce::MAX_SCALE.key)) {
                    if (*v > 0.1f) hud.m_maxScale = *v;
                }
                if (auto v = readBool(settings, IniOnly::GForce::SHOW_MAX_TEXT.key)) hud.m_bShowMaxText = *v;
                if (auto v = readBool(settings, IniOnly::GForce::SHOW_MAX_MARKER.key)) hud.m_bShowMaxMarker = *v;
                if (auto v = readInt(settings, IniOnly::GForce::MAX_MARKER_LINGER_FRAMES.key)) hud.m_maxMarkerLingerFrames = *v;
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
