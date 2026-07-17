// ============================================================================
// core/hud_manager_input.cpp
// HudManager keyboard/button input handling — the director & settings corner
// buttons and processKeyboardInput (hotkey dispatch, drag routing, settings
// toggle). Extracted verbatim from hud_manager.cpp when that file grew past
// ~1.6k lines; class definition, members, and public API are unchanged — only
// where these method bodies live moves. Same byte-identical-extraction pattern
// as the plugin_data / http_server splits. The include set mirrors
// hud_manager.cpp's so every referenced HUD type stays visible.
// ============================================================================

#include "hud_manager.h"
#include "../diagnostics/logger.h"
#include "../diagnostics/timer.h"
#include "asset_manager.h"
#include "companion_window.h"
#include "input_manager.h"
#include "xinput_reader.h"
#include "plugin_data.h"
#include "plugin_manager.h"
#include "settings_manager.h"
#include "director_manager.h"
#include "profile_manager.h"
#include "ui_config.h"
#include "../hud/base_hud.h"
#include "../hud/standings_hud.h"
#include "../hud/performance_hud.h"
#include "../hud/telemetry_hud.h"
#include "../hud/ideal_lap_hud.h"
#include "../hud/lap_log_hud.h"
#include "../hud/friends_hud.h"
#include "../hud/time_widget.h"
#include "../hud/position_widget.h"
#include "../hud/lap_widget.h"
#include "../hud/session_hud.h"
#include "../hud/speed_widget.h"
#include "../hud/gear_widget.h"
#include "../hud/speedo_widget.h"
#include "../hud/tacho_widget.h"
#include "../hud/timing_hud.h"
#include "../hud/bars_widget.h"
#include "../hud/version_widget.h"
#include "../hud/notices_hud.h"
#include "../hud/settings_hud.h"
#include "../hud/settings_button_widget.h"
#include "../hud/map_hud.h"
#include "../hud/radar_hud.h"
#include "../hud/pitboard_hud.h"
#include "../hud/fuel_widget.h"
#if GAME_HAS_RECORDS_PROVIDER
#include "../hud/records_hud.h"
#endif
#include "../hud/gap_bar_hud.h"
#include "../hud/pointer_widget.h"
#include "../hud/rumble_hud.h"
#include "../hud/director_widget.h"
#include "../hud/gamepad_widget.h"
#include "../hud/lean_widget.h"
#include "../hud/gforce_widget.h"
#include "../hud/compass_widget.h"
#include "../hud/clock_widget.h"
#if GAME_HAS_TYRE_TEMP
#include "../hud/tyre_temp_widget.h"
#endif
#if GAME_HAS_ECU
#include "../hud/ecu_widget.h"
#endif
#include "../hud/session_charts_hud.h"
#include "../hud/helmet_overlay_hud.h"
#include "../hud/fmx_hud.h"
#include "../hud/stats_hud.h"
#include "../hud/event_log_hud.h"
#include "../hud/benchmark_widget.h"
#include "hotkey_manager.h"
#if GAME_HAS_HTTP_SERVER
#include "http_server.h"
#endif
#include "../handlers/draw_handler.h"
#include "color_config.h"
#include <windows.h>
#include <algorithm>
#include <memory>
#include <cstring>
#if defined(MXBMRP3_TEST_BUILD)
#include <atomic>
#endif

void HudManager::handleDirectorButton() {
    if (!m_pDirector) return;
    if (m_pDirector->isClicked()) {
        // Click = turn the director on / off (the icon is a true on/off switch, matching
        // its off/auto/manual/paused tint). Pause/hold stays on the Director Hold hotkey.
        DirectorManager& dir = DirectorManager::getInstance();
        dir.toggleEnabled();
        DEBUG_INFO_F("Director: %s (status button)", dir.isEnabled() ? "enabled" : "disabled");
        // Enabled is a persisted MODE (unlike transient HUD-visibility toggles), so save
        // the choice - matching the settings-tab toggle's auto-save (respect the setting).
        persistDirectorEnabled();
    }
}

void HudManager::persistDirectorEnabled() {
    // Mark settings dirty; the write is deferred to a leave-track transition / Save button.
    SettingsManager::getInstance().markDirty();
}

void HudManager::handleSettingsButton() {
    if (!m_pSettingsHud || !m_pSettingsButton) return;

    // Check if settings button was clicked
    if (m_pSettingsButton->isClicked()) {
        // Toggle SettingsHud visibility
        if (m_pSettingsHud->isVisible()) {
            m_pSettingsHud->hide();
            DEBUG_INFO("SettingsHud hidden (button clicked)");
        } else {
            m_pSettingsHud->show();
            DEBUG_INFO("SettingsHud shown (button clicked)");
        }
    }
}

void HudManager::processKeyboardInput() {
    // Skip hotkey processing if in capture mode or if capture just completed this frame
    // Use didCaptureCompleteThisFrame() to avoid consuming the flag (settings UI needs it)
    HotkeyManager& hotkeyMgr = HotkeyManager::getInstance();
    if (hotkeyMgr.isCapturing() || hotkeyMgr.didCaptureCompleteThisFrame()) {
        return;
    }

    // Settings toggle - handle based on configured key
    const HotkeyBinding& settingsBinding = hotkeyMgr.getBinding(HotkeyAction::TOGGLE_SETTINGS);
    uint8_t configuredKey = settingsBinding.keyboard.keyCode;
    bool settingsTriggered = false;

    if ((configuredKey == VK_OEM_3 || configuredKey == VK_OEM_5) &&
        settingsBinding.keyboard.modifiers == ModifierFlags::NONE) {
        // For ` and \ keys without modifiers, use InputManager directly (handles keyboard layout differences)
        // Check both keys as fallback, but only trigger if no modifiers are held
        bool noModifiers = !(GetAsyncKeyState(VK_CONTROL) & 0x8000) &&
                           !(GetAsyncKeyState(VK_SHIFT) & 0x8000) &&
                           !(GetAsyncKeyState(VK_MENU) & 0x8000);  // VK_MENU = Alt
        const InputManager& input = InputManager::getInstance();
        if (noModifiers &&
            (input.getOem3Key().isClicked() || input.getOem5Key().isClicked())) {
            settingsTriggered = true;
        }
    } else if (configuredKey != 0) {
        // For other keys, use HotkeyManager
        settingsTriggered = hotkeyMgr.wasActionTriggered(HotkeyAction::TOGGLE_SETTINGS);
    }
    // If cleared (configuredKey == 0), nothing triggers

    if (settingsTriggered && m_pSettingsHud) {
        if (m_pSettingsHud->isVisible()) {
            m_pSettingsHud->hide();
            DEBUG_INFO("Hotkey: Settings hidden");
        } else {
            m_pSettingsHud->show();
            DEBUG_INFO("Hotkey: Settings shown");
        }
    }

    if (hotkeyMgr.wasActionTriggered(HotkeyAction::TOGGLE_ALL_HUDS)) {
        m_bAllHudsToggledOff = !m_bAllHudsToggledOff;
        DEBUG_INFO_F("Hotkey: All HUDs temporarily %s", m_bAllHudsToggledOff ? "hidden" : "shown");
    }

    if (hotkeyMgr.wasActionTriggered(HotkeyAction::TOGGLE_STANDINGS) && m_pStandings) {
        m_pStandings->setVisible(!m_pStandings->isVisible());
        DEBUG_INFO_F("Hotkey: Standings %s", m_pStandings->isVisible() ? "shown" : "hidden");
    }

    if (hotkeyMgr.wasActionTriggered(HotkeyAction::TOGGLE_MAP) && m_pMapHud) {
        m_pMapHud->setVisible(!m_pMapHud->isVisible());
        DEBUG_INFO_F("Hotkey: Map %s", m_pMapHud->isVisible() ? "shown" : "hidden");
    }

    if (hotkeyMgr.wasActionTriggered(HotkeyAction::TOGGLE_RADAR) && m_pRadarHud) {
        m_pRadarHud->setVisible(!m_pRadarHud->isVisible());
        DEBUG_INFO_F("Hotkey: Radar %s", m_pRadarHud->isVisible() ? "shown" : "hidden");
    }

    if (hotkeyMgr.wasActionTriggered(HotkeyAction::TOGGLE_LAP_LOG) && m_pLapLog) {
        m_pLapLog->setVisible(!m_pLapLog->isVisible());
        DEBUG_INFO_F("Hotkey: Lap Log %s", m_pLapLog->isVisible() ? "shown" : "hidden");
    }

    if (hotkeyMgr.wasActionTriggered(HotkeyAction::TOGGLE_IDEAL_LAP) && m_pIdealLap) {
        m_pIdealLap->setVisible(!m_pIdealLap->isVisible());
        DEBUG_INFO_F("Hotkey: Ideal Lap %s", m_pIdealLap->isVisible() ? "shown" : "hidden");
    }

    if (hotkeyMgr.wasActionTriggered(HotkeyAction::TOGGLE_TELEMETRY) && m_pTelemetry) {
        m_pTelemetry->setVisible(!m_pTelemetry->isVisible());
        DEBUG_INFO_F("Hotkey: Telemetry %s", m_pTelemetry->isVisible() ? "shown" : "hidden");
    }

    // TOGGLE_INPUT removed - individual widget toggles not supported (use TOGGLE_WIDGETS)

#if GAME_HAS_RECORDS_PROVIDER
    if (hotkeyMgr.wasActionTriggered(HotkeyAction::TOGGLE_RECORDS) && m_pRecords) {
        m_pRecords->setVisible(!m_pRecords->isVisible());
        DEBUG_INFO_F("Hotkey: Records %s", m_pRecords->isVisible() ? "shown" : "hidden");
    }
#endif

    if (hotkeyMgr.wasActionTriggered(HotkeyAction::TOGGLE_WIDGETS)) {
        m_bAllWidgetsToggledOff = !m_bAllWidgetsToggledOff;
        DEBUG_INFO_F("Hotkey: Widgets temporarily %s", m_bAllWidgetsToggledOff ? "hidden" : "shown");
    }

    if (hotkeyMgr.wasActionTriggered(HotkeyAction::TOGGLE_PITBOARD) && m_pPitboard) {
        m_pPitboard->setVisible(!m_pPitboard->isVisible());
        DEBUG_INFO_F("Hotkey: Pitboard %s", m_pPitboard->isVisible() ? "shown" : "hidden");
    }

    if (hotkeyMgr.wasActionTriggered(HotkeyAction::TOGGLE_TIMING) && m_pTiming) {
        m_pTiming->setVisible(!m_pTiming->isVisible());
        DEBUG_INFO_F("Hotkey: Timing %s", m_pTiming->isVisible() ? "shown" : "hidden");
    }

    if (hotkeyMgr.wasActionTriggered(HotkeyAction::TOGGLE_GAP_BAR) && m_pGapBar) {
        m_pGapBar->setVisible(!m_pGapBar->isVisible());
        DEBUG_INFO_F("Hotkey: Gap Bar %s", m_pGapBar->isVisible() ? "shown" : "hidden");
    }

    if (hotkeyMgr.wasActionTriggered(HotkeyAction::TOGGLE_PERFORMANCE) && m_pPerformance) {
        m_pPerformance->setVisible(!m_pPerformance->isVisible());
        DEBUG_INFO_F("Hotkey: Performance %s", m_pPerformance->isVisible() ? "shown" : "hidden");
    }

    if (hotkeyMgr.wasActionTriggered(HotkeyAction::TOGGLE_RUMBLE) && m_pRumble) {
        m_pRumble->setVisible(!m_pRumble->isVisible());
        DEBUG_INFO_F("Hotkey: Rumble %s", m_pRumble->isVisible() ? "shown" : "hidden");
    }

    if (hotkeyMgr.wasActionTriggered(HotkeyAction::TOGGLE_SESSION_CHARTS) && m_pSessionCharts) {
        m_pSessionCharts->setVisible(!m_pSessionCharts->isVisible());
        DEBUG_INFO_F("Hotkey: Session Charts %s", m_pSessionCharts->isVisible() ? "shown" : "hidden");
    }

    if (hotkeyMgr.wasActionTriggered(HotkeyAction::TOGGLE_FMX) && m_pFmxHud) {
        m_pFmxHud->setVisible(!m_pFmxHud->isVisible());
        DEBUG_INFO_F("Hotkey: FMX %s", m_pFmxHud->isVisible() ? "shown" : "hidden");
    }

    if (hotkeyMgr.wasActionTriggered(HotkeyAction::TOGGLE_STATS) && m_pStatsHud) {
        m_pStatsHud->setVisible(!m_pStatsHud->isVisible());
        DEBUG_INFO_F("Hotkey: Stats %s", m_pStatsHud->isVisible() ? "shown" : "hidden");
    }

    if (hotkeyMgr.wasActionTriggered(HotkeyAction::TOGGLE_SESSION) && m_pSession) {
        m_pSession->setVisible(!m_pSession->isVisible());
        DEBUG_INFO_F("Hotkey: Session %s", m_pSession->isVisible() ? "shown" : "hidden");
    }

    if (hotkeyMgr.wasActionTriggered(HotkeyAction::TOGGLE_NOTICES) && m_pNotices) {
        m_pNotices->setVisible(!m_pNotices->isVisible());
        DEBUG_INFO_F("Hotkey: Notices %s", m_pNotices->isVisible() ? "shown" : "hidden");
    }

    if (hotkeyMgr.wasActionTriggered(HotkeyAction::TOGGLE_EVENT_LOG) && m_pEventLog) {
        m_pEventLog->setVisible(!m_pEventLog->isVisible());
        DEBUG_INFO_F("Hotkey: Event Log %s", m_pEventLog->isVisible() ? "shown" : "hidden");
    }

    if (hotkeyMgr.wasActionTriggered(HotkeyAction::TOGGLE_HELMET) && m_pHelmetOverlay) {
        m_pHelmetOverlay->setVisible(!m_pHelmetOverlay->isVisible());
        DEBUG_INFO_F("Hotkey: Helmet %s", m_pHelmetOverlay->isVisible() ? "shown" : "hidden");
    }

    if (hotkeyMgr.wasActionTriggered(HotkeyAction::TOGGLE_FRIENDS) && m_pFriends) {
        m_pFriends->setVisible(!m_pFriends->isVisible());
        DEBUG_INFO_F("Hotkey: Friends %s", m_pFriends->isVisible() ? "shown" : "hidden");
    }

    // Auto-director (spectate broadcast tool): toggle on/off, and hold current shot.
    if (hotkeyMgr.wasActionTriggered(HotkeyAction::DIRECTOR_TOGGLE)) {
        DirectorManager::getInstance().toggleEnabled();
        if (m_pSettingsHud) m_pSettingsHud->setDataDirty();  // refresh the tab checkbox if open
        persistDirectorEnabled();  // enabled is a persisted mode (see handleDirectorButton)
    }
    if (hotkeyMgr.wasActionTriggered(HotkeyAction::DIRECTOR_LOCK)) {
        DirectorManager::getInstance().toggleLock();  // transient - not persisted
    }

    // Custom segment timer: Add drops a boundary point at the current position,
    // Remove deletes the last one. PluginData owns the state and emits the notice.
    // Nudge the map so the boundary markers appear/clear immediately (it only
    // rebuilds on dirty; changing the points otherwise wouldn't trigger it).
    if (hotkeyMgr.wasActionTriggered(HotkeyAction::SEGMENT_ADD)) {
        PluginData::getInstance().addSegmentPoint();
        if (m_pMapHud) m_pMapHud->setDataDirty();
        DEBUG_INFO("Hotkey: Segment point added");
    }
    if (hotkeyMgr.wasActionTriggered(HotkeyAction::SEGMENT_REMOVE)) {
        PluginData::getInstance().removeSegmentPoint();
        if (m_pMapHud) m_pMapHud->setDataDirty();
        DEBUG_INFO("Hotkey: Segment point removed");
    }

#if GAME_HAS_HTTP_SERVER
    // Web overlay broadcaster controls: force a bottom-slot panel to slide in now.
    {
        using OP = HttpServer::OverlayPanel;
        struct { HotkeyAction action; OP panel; const char* name; } kOverlayForces[] = {
            { HotkeyAction::OVERLAY_FORCE_LAST_LAP,    OP::LAST_LAP,    "fastest-last-lap" },
            { HotkeyAction::OVERLAY_FORCE_FASTEST_LAP, OP::FASTEST_LAP, "session-best" },
            { HotkeyAction::OVERLAY_FORCE_DOWN_ORDER,  OP::DOWN_ORDER,  "down-the-order" },
            { HotkeyAction::OVERLAY_FORCE_SECTORS,     OP::SECTORS,     "best-sectors" },
            { HotkeyAction::OVERLAY_FORCE_CHARTS,      OP::CHARTS,      "session-charts" },
        };
        for (const auto& f : kOverlayForces) {
            if (hotkeyMgr.wasActionTriggered(f.action)) {
                HttpServer::getInstance().forceOverlayPanel(f.panel);
                DEBUG_INFO_F("Hotkey: Overlay force %s", f.name);
            }
        }
    }
#endif

    // Reload config from file
    if (hotkeyMgr.wasActionTriggered(HotkeyAction::RELOAD_CONFIG)) {
        SettingsManager& settingsMgr = SettingsManager::getInstance();
        const std::string& savePath = settingsMgr.getSavePath();
        if (!savePath.empty()) {
            DEBUG_INFO("Hotkey: Reloading config from file");
            settingsMgr.loadSettings(*this, savePath.c_str());
            // Mark HUDs with per-texture layouts dirty to force rebuild
            if (m_pGamepad) m_pGamepad->setDataDirty();
            if (m_pPitboard) m_pPitboard->setDataDirty();
            if (m_pSettingsHud) m_pSettingsHud->setDataDirty();
        }
    }

    // If any visibility toggle happened while settings is open, refresh it
    if (m_pSettingsHud && m_pSettingsHud->isVisible()) {
        for (uint8_t i = 0; i < static_cast<uint8_t>(HotkeyAction::COUNT); ++i) {
            auto action = static_cast<HotkeyAction>(i);
            if (action == HotkeyAction::TOGGLE_SETTINGS ||
                action == HotkeyAction::RELOAD_CONFIG) continue;
            if (hotkeyMgr.wasActionTriggered(action)) {
                m_pSettingsHud->setDataDirty();
                break;
            }
        }

        // Refresh when controller connection state changes
        if (XInputReader::getInstance().didConnectionStateChange()) {
            m_pSettingsHud->setDataDirty();
        }
    }
}

bool HudManager::isSettingsVisible() const {
    return m_pSettingsHud && m_pSettingsHud->isVisible();
}
