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
#include "stats_manager.h"
#include "../diagnostics/logger.h"
#include "../diagnostics/timer.h"
#include "asset_manager.h"
#include "companion_window.h"
#include "input_manager.h"
#include "xinput_reader.h"
#include "plugin_data.h"
#include "plugin_manager.h"
#include "settings_manager.h"
#include "spotter_manager.h"  // RELOAD_CONFIG re-reads the active cue pack
#include "achievement_manager.h"  // RELOAD_CONFIG counts toward Tinkerer
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
#include "../hud/crash_widget.h"
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
#include "../hud/stream_chat_hud.h"
#include "../hud/delta_trace_hud.h"
#include "../hud/benchmark_widget.h"
#include "../hud/achievement_widget.h"
#include "../hud/spotter_widget.h"
#include "../hud/gl_confirm_hud.h"
#include "hotkey_manager.h"
#include "system_messages.h"
#include "settings_manager_internal.h"
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
    // Belt to SettingsHud::hide()'s braces. A capture armed with the panel gone
    // is a total hotkey lockout (see the comment there), so rather than trust
    // every present and future close path to disarm it, treat "capturing while
    // the menu is not open" as impossible and end it here. Costs one bool test
    // on a path that already reads this manager.
    if (hotkeyMgr.isCapturing() && (!m_pSettingsHud || !m_pSettingsHud->isVisible())) {
        DEBUG_WARN("HotkeyManager: capture was armed with the settings menu closed - cancelled");
        hotkeyMgr.cancelCapture();
    }
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
        announceMasterToggle(HotkeyAction::TOGGLE_ALL_HUDS, !m_bAllHudsToggledOff);
    }

    if (hotkeyMgr.wasActionTriggered(HotkeyAction::TOGGLE_STANDINGS) && m_pStandings) {
        m_pStandings->setVisible(!m_pStandings->isVisible());
        DEBUG_INFO_F("Hotkey: Standings %s", m_pStandings->isVisible() ? "shown" : "hidden");
        announceHudToggle(HotkeyAction::TOGGLE_STANDINGS, *m_pStandings, SettingsHud::TAB_STANDINGS);
    }

    if (hotkeyMgr.wasActionTriggered(HotkeyAction::TOGGLE_MAP) && m_pMapHud) {
        m_pMapHud->setVisible(!m_pMapHud->isVisible());
        DEBUG_INFO_F("Hotkey: Map %s", m_pMapHud->isVisible() ? "shown" : "hidden");
        announceHudToggle(HotkeyAction::TOGGLE_MAP, *m_pMapHud, SettingsHud::TAB_MAP);
    }

    if (hotkeyMgr.wasActionTriggered(HotkeyAction::TOGGLE_RADAR) && m_pRadarHud) {
        m_pRadarHud->setVisible(!m_pRadarHud->isVisible());
        DEBUG_INFO_F("Hotkey: Radar %s", m_pRadarHud->isVisible() ? "shown" : "hidden");
        announceHudToggle(HotkeyAction::TOGGLE_RADAR, *m_pRadarHud, SettingsHud::TAB_RADAR);
    }

    if (hotkeyMgr.wasActionTriggered(HotkeyAction::TOGGLE_LAP_LOG) && m_pLapLog) {
        m_pLapLog->setVisible(!m_pLapLog->isVisible());
        DEBUG_INFO_F("Hotkey: Lap Log %s", m_pLapLog->isVisible() ? "shown" : "hidden");
        announceHudToggle(HotkeyAction::TOGGLE_LAP_LOG, *m_pLapLog, SettingsHud::TAB_LAP_LOG);
    }

    if (hotkeyMgr.wasActionTriggered(HotkeyAction::TOGGLE_IDEAL_LAP) && m_pIdealLap) {
        m_pIdealLap->setVisible(!m_pIdealLap->isVisible());
        DEBUG_INFO_F("Hotkey: Ideal Lap %s", m_pIdealLap->isVisible() ? "shown" : "hidden");
        announceHudToggle(HotkeyAction::TOGGLE_IDEAL_LAP, *m_pIdealLap, SettingsHud::TAB_IDEAL_LAP);
    }

    if (hotkeyMgr.wasActionTriggered(HotkeyAction::TOGGLE_TELEMETRY) && m_pTelemetry) {
        m_pTelemetry->setVisible(!m_pTelemetry->isVisible());
        DEBUG_INFO_F("Hotkey: Telemetry %s", m_pTelemetry->isVisible() ? "shown" : "hidden");
        announceHudToggle(HotkeyAction::TOGGLE_TELEMETRY, *m_pTelemetry, SettingsHud::TAB_TELEMETRY);
    }

    // TOGGLE_INPUT removed - individual widget toggles not supported (use TOGGLE_WIDGETS)

#if GAME_HAS_RECORDS_PROVIDER
    if (hotkeyMgr.wasActionTriggered(HotkeyAction::TOGGLE_RECORDS) && m_pRecords) {
        m_pRecords->setVisible(!m_pRecords->isVisible());
        DEBUG_INFO_F("Hotkey: Records %s", m_pRecords->isVisible() ? "shown" : "hidden");
        announceHudToggle(HotkeyAction::TOGGLE_RECORDS, *m_pRecords, SettingsHud::TAB_RECORDS);
    }
#endif

    if (hotkeyMgr.wasActionTriggered(HotkeyAction::TOGGLE_WIDGETS)) {
        m_bAllWidgetsToggledOff = !m_bAllWidgetsToggledOff;
        DEBUG_INFO_F("Hotkey: Widgets temporarily %s", m_bAllWidgetsToggledOff ? "hidden" : "shown");
        announceMasterToggle(HotkeyAction::TOGGLE_WIDGETS, !m_bAllWidgetsToggledOff);
    }

    if (hotkeyMgr.wasActionTriggered(HotkeyAction::TOGGLE_PITBOARD) && m_pPitboard) {
        m_pPitboard->setVisible(!m_pPitboard->isVisible());
        DEBUG_INFO_F("Hotkey: Pitboard %s", m_pPitboard->isVisible() ? "shown" : "hidden");
        announceHudToggle(HotkeyAction::TOGGLE_PITBOARD, *m_pPitboard, SettingsHud::TAB_PITBOARD);
    }

    if (hotkeyMgr.wasActionTriggered(HotkeyAction::TOGGLE_TIMING) && m_pTiming) {
        m_pTiming->setVisible(!m_pTiming->isVisible());
        DEBUG_INFO_F("Hotkey: Timing %s", m_pTiming->isVisible() ? "shown" : "hidden");
        announceHudToggle(HotkeyAction::TOGGLE_TIMING, *m_pTiming, SettingsHud::TAB_TIMING);
    }

    if (hotkeyMgr.wasActionTriggered(HotkeyAction::TOGGLE_GAP_BAR) && m_pGapBar) {
        m_pGapBar->setVisible(!m_pGapBar->isVisible());
        DEBUG_INFO_F("Hotkey: Gap Bar %s", m_pGapBar->isVisible() ? "shown" : "hidden");
        announceHudToggle(HotkeyAction::TOGGLE_GAP_BAR, *m_pGapBar, SettingsHud::TAB_GAP_BAR);
    }

    if (hotkeyMgr.wasActionTriggered(HotkeyAction::TOGGLE_PERFORMANCE) && m_pPerformance) {
        m_pPerformance->setVisible(!m_pPerformance->isVisible());
        DEBUG_INFO_F("Hotkey: Performance %s", m_pPerformance->isVisible() ? "shown" : "hidden");
        announceHudToggle(HotkeyAction::TOGGLE_PERFORMANCE, *m_pPerformance, SettingsHud::TAB_PERFORMANCE);
    }

    if (hotkeyMgr.wasActionTriggered(HotkeyAction::TOGGLE_RUMBLE) && m_pRumble) {
        m_pRumble->setVisible(!m_pRumble->isVisible());
        DEBUG_INFO_F("Hotkey: Rumble %s", m_pRumble->isVisible() ? "shown" : "hidden");
        announceHudToggle(HotkeyAction::TOGGLE_RUMBLE, *m_pRumble, SettingsHud::TAB_RUMBLE);
    }

    if (hotkeyMgr.wasActionTriggered(HotkeyAction::TOGGLE_SESSION_CHARTS) && m_pSessionCharts) {
        m_pSessionCharts->setVisible(!m_pSessionCharts->isVisible());
        DEBUG_INFO_F("Hotkey: Session Charts %s", m_pSessionCharts->isVisible() ? "shown" : "hidden");
        announceHudToggle(HotkeyAction::TOGGLE_SESSION_CHARTS, *m_pSessionCharts, SettingsHud::TAB_SESSION_CHARTS);
    }

    if (hotkeyMgr.wasActionTriggered(HotkeyAction::TOGGLE_FMX) && m_pFmxHud) {
        m_pFmxHud->setVisible(!m_pFmxHud->isVisible());
        DEBUG_INFO_F("Hotkey: FMX %s", m_pFmxHud->isVisible() ? "shown" : "hidden");
        announceHudToggle(HotkeyAction::TOGGLE_FMX, *m_pFmxHud, SettingsHud::TAB_FMX);
    }

    if (hotkeyMgr.wasActionTriggered(HotkeyAction::TOGGLE_STATS) && m_pStatsHud) {
        m_pStatsHud->setVisible(!m_pStatsHud->isVisible());
        DEBUG_INFO_F("Hotkey: Stats %s", m_pStatsHud->isVisible() ? "shown" : "hidden");
        announceHudToggle(HotkeyAction::TOGGLE_STATS, *m_pStatsHud, SettingsHud::TAB_STATS);
    }

    if (hotkeyMgr.wasActionTriggered(HotkeyAction::TOGGLE_SESSION) && m_pSession) {
        m_pSession->setVisible(!m_pSession->isVisible());
        DEBUG_INFO_F("Hotkey: Session %s", m_pSession->isVisible() ? "shown" : "hidden");
        announceHudToggle(HotkeyAction::TOGGLE_SESSION, *m_pSession, SettingsHud::TAB_SESSION);
    }

    if (hotkeyMgr.wasActionTriggered(HotkeyAction::TOGGLE_NOTICES) && m_pNotices) {
        m_pNotices->setVisible(!m_pNotices->isVisible());
        DEBUG_INFO_F("Hotkey: Notices %s", m_pNotices->isVisible() ? "shown" : "hidden");
        announceHudToggle(HotkeyAction::TOGGLE_NOTICES, *m_pNotices, SettingsHud::TAB_NOTICES);
    }

    if (hotkeyMgr.wasActionTriggered(HotkeyAction::TOGGLE_EVENT_LOG) && m_pEventLog) {
        m_pEventLog->setVisible(!m_pEventLog->isVisible());
        DEBUG_INFO_F("Hotkey: Event Log %s", m_pEventLog->isVisible() ? "shown" : "hidden");
        announceHudToggle(HotkeyAction::TOGGLE_EVENT_LOG, *m_pEventLog, SettingsHud::TAB_EVENT_LOG);
    }

    if (hotkeyMgr.wasActionTriggered(HotkeyAction::TOGGLE_HELMET) && m_pHelmetOverlay) {
        m_pHelmetOverlay->setVisible(!m_pHelmetOverlay->isVisible());
        DEBUG_INFO_F("Hotkey: Helmet %s", m_pHelmetOverlay->isVisible() ? "shown" : "hidden");
        announceHudToggle(HotkeyAction::TOGGLE_HELMET, *m_pHelmetOverlay, SettingsHud::TAB_HELMET);
    }

    // Hiding only hides: the chat stays connected (each platform manager's switch).
    if (hotkeyMgr.wasActionTriggered(HotkeyAction::TOGGLE_STREAM_CHAT) && m_pStreamChat) {
        m_pStreamChat->setVisible(!m_pStreamChat->isVisible());
        DEBUG_INFO_F("Hotkey: Stream Chat %s", m_pStreamChat->isVisible() ? "shown" : "hidden");
        announceHudToggle(HotkeyAction::TOGGLE_STREAM_CHAT, *m_pStreamChat, SettingsHud::TAB_STREAM_CHAT);
    }

    if (hotkeyMgr.wasActionTriggered(HotkeyAction::TOGGLE_DELTA_TRACE) && m_pDeltaTrace) {
        m_pDeltaTrace->setVisible(!m_pDeltaTrace->isVisible());
        DEBUG_INFO_F("Hotkey: Delta Trace %s", m_pDeltaTrace->isVisible() ? "shown" : "hidden");
        announceHudToggle(HotkeyAction::TOGGLE_DELTA_TRACE, *m_pDeltaTrace, SettingsHud::TAB_DELTA_TRACE);
    }

    if (hotkeyMgr.wasActionTriggered(HotkeyAction::TOGGLE_FRIENDS) && m_pFriends) {
        m_pFriends->setVisible(!m_pFriends->isVisible());
        DEBUG_INFO_F("Hotkey: Friends %s", m_pFriends->isVisible() ? "shown" : "hidden");
        announceHudToggle(HotkeyAction::TOGGLE_FRIENDS, *m_pFriends, SettingsHud::TAB_FRIENDS);
    }

    // A HUD switched on by hotkey counts for Tyre Kicker as it is switched on,
    // like one switched on in the menu (SettingsHud::markSettingsDirty). AFTER
    // the last toggle above: the walk reads visibility, so it must follow them all.
    {
        static constexpr HotkeyAction kHudToggles[] = {
            HotkeyAction::TOGGLE_STANDINGS, HotkeyAction::TOGGLE_MAP, HotkeyAction::TOGGLE_RADAR,
            HotkeyAction::TOGGLE_LAP_LOG, HotkeyAction::TOGGLE_IDEAL_LAP, HotkeyAction::TOGGLE_TELEMETRY,
            HotkeyAction::TOGGLE_RECORDS, HotkeyAction::TOGGLE_PITBOARD, HotkeyAction::TOGGLE_TIMING,
            HotkeyAction::TOGGLE_GAP_BAR, HotkeyAction::TOGGLE_PERFORMANCE, HotkeyAction::TOGGLE_RUMBLE,
            HotkeyAction::TOGGLE_EVENT_LOG, HotkeyAction::TOGGLE_FRIENDS, HotkeyAction::TOGGLE_HELMET,
            HotkeyAction::TOGGLE_SESSION_CHARTS, HotkeyAction::TOGGLE_FMX, HotkeyAction::TOGGLE_STATS,
            HotkeyAction::TOGGLE_SESSION, HotkeyAction::TOGGLE_NOTICES, HotkeyAction::TOGGLE_STREAM_CHAT,
            HotkeyAction::TOGGLE_DELTA_TRACE,
        };
        for (HotkeyAction a : kHudToggles) {
            if (!hotkeyMgr.wasActionTriggered(a)) continue;
            StatsManager::getInstance().exploration().observeSettings(*this);
            break;
        }
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

    // Spotter: speak the pack's `hotkey_triggered` line. Silent until a pack
    // defines one, so an unbound-by-default key that nobody has written a
    // line for does nothing rather than saying something we chose.
    if (hotkeyMgr.wasActionTriggered(HotkeyAction::SPOTTER_CUE)) {
        SpotterManager::getInstance().speakHotkeyCue();
    }

    // Crash counter: zero the streaming tally. Bound rather than button-only
    // because the number is watched live on stream -- reaching for the mouse to
    // open a cursor is the part a rider on the gate cannot do.
    if (hotkeyMgr.wasActionTriggered(HotkeyAction::CRASH_RESET)) {
        if (m_pCrash) m_pCrash->resetCounter();
        DEBUG_INFO("Hotkey: Crash tally reset");
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
            // Nothing on screen says a reload happened unless a value changed.
            SystemMessages::Toast t;
            snprintf(t.title, sizeof(t.title), "Settings reloaded");
            snprintf(t.detail, sizeof(t.detail), "From %s", SettingsInternal::SETTINGS_FILENAME);
            snprintf(t.icon, sizeof(t.icon), "arrow-rotate-right");
            t.key = SystemMessages::KEY_SETTINGS_RELOADED;
            SystemMessages::getInstance().post(t);
        }

        // The reload is itself an achievement feed (Tinkerer), and -- with the
        // hidden devToast key -- the way to put a toast on screen on demand. After
        // loadSettings, so a devToast=1 just written to the INI counts this press.
        AchievementManager::getInstance().onConfigReloaded();

        // OUTSIDE the savePath guard, deliberately. The layout files live in
        // mxbmrp3_data/themes/, not in the settings INI, so a missing or unset save
        // path has nothing to do with whether they can be re-read -- nesting this
        // inside would make the hotkey silently do nothing for the one user whose
        // settings path failed to resolve, on the feature most likely to be in use
        // while something else is misconfigured.
        //
        // Re-reads each theme's own theme.ini: its slice sizes, switches, palette
        // and font set. The layout vocabulary is NOT re-read, because there is no
        // longer a file holding it -- uiFontSize / uiLineHeight come from [Advanced]
        // and are picked up by the loadSettings() call above.
        //
        // Sprites are NOT re-registered. Their indices are pushed to the game once
        // at init and everything holds them by number, so a theme that ADDED or
        // REMOVED a .tga needs a restart; only the numbers are live. Full
        // re-discovery here would renumber under the game.
        AssetManager::getInstance().reloadThemeLayouts();

        // Re-read the active spotter cue pack from disk, whether or not the
        // settings INI named it again above — a pack author's edit-reload-listen
        // loop must work even when the pack NAME didn't change. The call above
        // has just re-copied every pack folder from the user's Documents, so
        // this reads the edit they actually made.
        //
        // A voice pack reloads MORE completely than a theme does, and the
        // asymmetry is worth knowing: changed .wav files are live here too.
        // Audio has no sprite indices — the worker opens each clip by path as
        // it plays a cue — so the restart-for-new-art rule above simply does
        // not apply to a voice.
        SpotterManager::getInstance().reloadCuePack();

        // A CHANGED .tga is a different matter, on one surface. The companion's
        // software renderer opens each file itself, so it can be told to re-read
        // them -- which is what makes iterating on theme art a hotkey rather than a
        // restart. In-game art is unchanged until the next launch, deliberately and
        // unavoidably; the window thread logs that when it acts on this.
        CompanionWindow::getInstance().requestArtReload();
        requestGlArtReload();   // the in-context GL backend caches art too

        // Every HUD, not just the three below: a layout change moves every panel,
        // and a HUD that skipped its rebuild would render at the old spacing until
        // something else happened to dirty it. setDataDirty (not layout-dirty) is
        // what matters -- only the FULL rebuild re-reads getScaledDimensions(), and
        // the layout fast path would reposition strings at the old metrics.
        for (const std::unique_ptr<BaseHud>& hud : m_huds) {
            if (hud) hud->setDataDirty();
        }
        // Mark HUDs with per-texture layouts dirty to force rebuild. Kept even
        // though the loop above covers m_huds: SettingsHud and PointerWidget are
        // composed by hand rather than through createHud(), so whether they are in
        // m_huds is a detail of HudManager::initialize() rather than a guarantee.
        if (m_pGamepad) m_pGamepad->setDataDirty();
        if (m_pPitboard) m_pPitboard->setDataDirty();
        if (m_pSettingsHud) m_pSettingsHud->setDataDirty();
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

// ---- system toasts for hotkeys (core/system_messages.h) --------------------

// "Press F3 to show it again": the binding that just fired, keyboard first.
// Empty when the action has no binding (a test hook can fire one).
static void formatHotkeyHint(HotkeyAction action, const char* what, char* out, size_t cap) {
    const HotkeyBinding& b = HotkeyManager::getInstance().getBinding(action);
    char key[32] = {};
    if (b.hasKeyboard()) formatKeyBinding(b.keyboard, key, sizeof(key));
    else if (b.hasController()) snprintf(key, sizeof(key), "%s", getControllerButtonName(b.controller));
    if (key[0]) snprintf(out, cap, "Press %s to %s", key, what);
    else out[0] = '\0';
}

// A HUD switched OFF by its hotkey vanishes, which leaves a player wondering
// what they pressed, so it gets a card naming the HUD and the way back. One
// switched ON appears, which is its own confirmation: no card, and a "hidden"
// card still up for it is withdrawn.
void HudManager::announceHudToggle(HotkeyAction action, const BaseHud& hud, int tab) {
    SystemMessages& sys = SystemMessages::getInstance();
    const int key = SystemMessages::KEY_HOTKEY_BASE + static_cast<int>(action);
    if (hud.isVisible()) {   // vis-gate: the hotkey toggles the game-surface flag
        sys.cancel(key);
        return;
    }
    SystemMessages::Toast t;
    const char* name = m_pSettingsHud ? m_pSettingsHud->tabTitle(tab) : "HUD";
    snprintf(t.title, sizeof(t.title), "%s hidden", name);
    formatHotkeyHint(action, "show it again", t.detail, sizeof(t.detail));
    snprintf(t.icon, sizeof(t.icon), "eye-slash");   // every "hidden" card wears the same glyph
    t.key = key;
    t.tab = tab;
    sys.post(t);
}

// The Widgets and hide-all hotkeys, the same rule: a card on hide, none on show.
// The hide-all card is the one toast drawn through that hotkey (see
// SystemMessages::Toast::throughHideAll), and points at no tab: the menu is not
// what brings the HUD back, the key is.
void HudManager::announceMasterToggle(HotkeyAction action, bool nowShown) {
    SystemMessages& sys = SystemMessages::getInstance();
    const int key = SystemMessages::KEY_HOTKEY_BASE + static_cast<int>(action);
    if (nowShown) {
        sys.cancel(key);
        return;
    }
    const bool all = action == HotkeyAction::TOGGLE_ALL_HUDS;
    SystemMessages::Toast t;
    snprintf(t.title, sizeof(t.title), "%s", all ? "All HUDs hidden" : "Widgets hidden");
    formatHotkeyHint(action, "show them again", t.detail, sizeof(t.detail));
    snprintf(t.icon, sizeof(t.icon), "eye-slash");
    t.key = key;
    t.tab = all ? -1 : SettingsHud::TAB_WIDGETS;
    t.throughHideAll = all;
    sys.post(t);
}

// A game started with every HUD switched off draws nothing at all, which reads
// as the plugin not loading. It may be deliberate, so this is said ONCE per
// game session, at the first Draw, never on later returns to the track.
// "Every HUD" is what the player switches on to see something while riding:
// the settings chrome and pointer are how the HUDs come back (the hide-all
// hotkey spares them too), and the card, Notices, Spotter subtitles, Director,
// Version popups and the GL confirm only appear when something happens. A
// widget under the Widgets master switch (saved) draws nothing either. The
// companion counts: a HUD shown only there is a deliberate setup.
void HudManager::announceAllHiddenAtStart() {
    for (const auto& hud : m_huds) {
        const BaseHud* h = hud.get();
        if (!h || h == m_pSettingsHud || h == m_pSettingsButton || h == m_pPointer ||
            h == m_pAchievement || h == m_pNotices || h == m_pSpotter || h == m_pDirector ||
            h == m_pVersion || h == m_pGlConfirm) continue;
        if (m_bAllWidgetsToggledOff && isWidgetHud(h)) continue;   // the saved Widgets master switch
        if (h->isVisibleAnySurface()) return;
    }
    SystemMessages::Toast t;
    snprintf(t.title, sizeof(t.title), "All HUDs hidden");
    HotkeyManager::getInstance().formatOpenSettingsHint(t.detail, sizeof(t.detail));
    snprintf(t.icon, sizeof(t.icon), "eye-slash");
    t.key = SystemMessages::KEY_ALL_HIDDEN_AT_START;
    t.tab = SettingsHud::TAB_GENERAL;
    t.durationMs = SystemMessages::WARNING_DURATION_MS;   // said once, so held a little longer
    SystemMessages::getInstance().post(t);
    DEBUG_INFO("Startup: every HUD is switched off");
}
