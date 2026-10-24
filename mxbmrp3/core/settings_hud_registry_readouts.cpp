// ============================================================================
// mxbmrp3/core/settings_hud_registry_readouts.cpp
// Per-HUD settings serializers (cap_*/app_*) for the riding readouts that tile
// together under the widget rail: Speed, Gear, Crashes and RPM, and the Dash
// that sits beside them. Split out of settings_hud_registry_widgets.cpp,
// which had reached its line budget; the registry TABLE stays in
// settings_hud_registry.cpp.
// ============================================================================
#include "settings_manager.h"
#include "settings_keys.h"
#include "settings_serde_hud.h"
#include "hud_manager.h"
#include "../diagnostics/logger.h"
#include "../hud/speed_widget.h"
#include "../hud/gear_widget.h"
#include "../hud/crash_widget.h"
#include "../hud/rpm_widget.h"
#include "../hud/dash_hud.h"
#include <algorithm>
#include <string>

// Bring the centralized INI key names / serde helpers into scope.
using namespace Settings;

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
                if (auto v = readBool(settings, IniOnly::Gear::SHOW_SHIFT_COLOR.key)) hud.m_bShowShiftColor = *v;
                if (auto v = readBool(settings, IniOnly::Gear::SHOW_LIMITER_CIRCLE.key)) hud.m_bShowLimiterCircle = *v;
            } catch (const std::exception& e) {
                DEBUG_WARN_F("GearWidget: Failed to parse settings: %s", e.what());
            }
            hud.setDataDirty();
        }
}

void SettingsManager::cap_CrashWidget(const HudManager& hudManager, SettingsManager::ProfileCache& cache, const char* name) {
        HudSettings settings;
        const auto& hud = hudManager.getCrashWidget();
        captureBaseHudSettings(settings, hud);
        settings[IniOnly::Crash::SHOW_RESET_BUTTON.key] = hud.m_bShowResetButton ? "1" : "0";
        cache[name] = std::move(settings);
}

void SettingsManager::app_CrashWidget(HudManager& hudManager, const SettingsManager::ProfileCache& cache, const char* name) {
        auto it = cache.find(name);
        if (it != cache.end()) {
            auto& hud = hudManager.getCrashWidget();
            applyBaseHudSettings(hud, it->second);

            const auto& settings = it->second;
            try {
                if (auto v = readBool(settings, IniOnly::Crash::SHOW_RESET_BUTTON.key)) hud.m_bShowResetButton = *v;
            } catch (const std::exception& e) {
                DEBUG_WARN_F("CrashWidget: Failed to parse settings: %s", e.what());
            }
            hud.setDataDirty();
        }
}

void SettingsManager::cap_RpmWidget(const HudManager& hudManager, SettingsManager::ProfileCache& cache, const char* name) {
        HudSettings settings;
        const auto& hud = hudManager.getRpmWidget();
        captureBaseHudSettings(settings, hud);
        settings[IniOnly::Rpm::SEGMENTS.key] = std::to_string(hud.m_segments);
        settings[IniOnly::Rpm::SEGMENT_GAPS.key] = hud.m_bSegmentGaps ? "1" : "0";
        settings[IniOnly::Rpm::VERTICAL.key] = hud.m_bVertical ? "1" : "0";
        settings[IniOnly::Rpm::WIDTH.key] = std::to_string(hud.m_widthChars);
        cache[name] = std::move(settings);
}

void SettingsManager::app_RpmWidget(HudManager& hudManager, const SettingsManager::ProfileCache& cache, const char* name) {
        auto it = cache.find(name);
        if (it != cache.end()) {
            auto& hud = hudManager.getRpmWidget();
            applyBaseHudSettings(hud, it->second);

            const auto& settings = it->second;
            try {
                if (auto v = readInt(settings, IniOnly::Rpm::SEGMENTS.key))
                    hud.m_segments = std::clamp(*v, RpmWidget::MIN_SEGMENTS, RpmWidget::MAX_SEGMENTS);
                if (auto v = readBool(settings, IniOnly::Rpm::SEGMENT_GAPS.key)) hud.m_bSegmentGaps = *v;
                if (auto v = readBool(settings, IniOnly::Rpm::VERTICAL.key)) hud.m_bVertical = *v;
                if (auto v = readInt(settings, IniOnly::Rpm::WIDTH.key))
                    hud.m_widthChars = std::clamp(*v, RpmWidget::MIN_WIDTH, RpmWidget::MAX_WIDTH);
            } catch (const std::exception& e) {
                DEBUG_WARN_F("RpmWidget: Failed to parse settings: %s", e.what());
            }
            hud.setDataDirty();
        }
}

// Dash: the standard HUD controls, the pages in the cycle (one named key each), its options and
// the page showing and the strip's place, saved by NAME (DashHud::pageKey, stripPlaceKey) so
// reordering either moves nobody's choice.
void SettingsManager::cap_DashHud(const HudManager& hudManager, SettingsManager::ProfileCache& cache, const char* name) {
        HudSettings settings;
        const auto& hud = hudManager.getDashHud();
        captureBaseHudSettings(settings, hud);
        for (int i = 0; i < DashHud::CYCLE_PAGES; ++i) {
            const auto page = static_cast<DashHud::Page>(i);
            saveBitAsKey(settings, DashHud::pageEnabledKey(page), hud.m_enabledPages, DashHud::pageBit(page));
        }
        for (const auto& option : DashHud::kOptionKeys)
            saveBitAsKey(settings, option.key, hud.m_options, option.bit);
        settings["page"] = DashHud::pageKey(hud.getPage());
        settings["strip"] = DashHud::stripPlaceKey(hud.m_stripPlace);
        cache[name] = std::move(settings);
}

void SettingsManager::app_DashHud(HudManager& hudManager, const SettingsManager::ProfileCache& cache, const char* name) {
        auto it = cache.find(name);
        if (it != cache.end()) {
            auto& hud = hudManager.getDashHud();
            applyBaseHudSettings(hud, it->second);
            for (int i = 0; i < DashHud::CYCLE_PAGES; ++i) {
                const auto page = static_cast<DashHud::Page>(i);
                loadBitFromKey(it->second, DashHud::pageEnabledKey(page), hud.m_enabledPages, DashHud::pageBit(page));
            }
            if ((hud.m_enabledPages & DashHud::ALL_PAGES) == 0) hud.m_enabledPages = DashHud::ALL_PAGES;
            for (const auto& option : DashHud::kOptionKeys)
                loadBitFromKey(it->second, option.key, hud.m_options, option.bit);
            if (const std::string* v = readStr(it->second, "page")) hud.m_page = DashHud::pageFromKey(v->c_str());
            // Absent = the default, so a file without it never keeps the last one's.
            const std::string* strip = readStr(it->second, "strip");
            hud.m_stripPlace = DashHud::stripPlaceFromKey(strip ? strip->c_str() : nullptr);
            hud.m_page = hud.firstEnabledFrom(hud.m_page);
            hud.m_noticeHold = false;
            hud.stopFade();
            hud.setDataDirty();
        }
}
