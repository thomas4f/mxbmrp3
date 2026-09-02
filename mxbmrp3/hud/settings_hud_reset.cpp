// ============================================================================
// hud/settings_hud_reset.cpp
// The RESET operations behind the settings menu's three buttons: "Reset all
// settings", the per-tab "Reset <tab>", and the profile reset. Split out of
// settings_hud_input.cpp (which keeps hit-testing and click dispatch) when that
// file went over its budget; the class and the export surface are unchanged.
//
// They belong together because they answer one question at three scopes - what
// does this button put back - and share one rule: replay the factory snapshot
// captured at startup through the same applier loadSettings() uses, never a
// hand-written list of setters. See CLAUDE.md ("don't add a third list"), the
// snapshot fields in settings_manager.h, and reset_tab_test.cpp, which presses
// every tab's Reset and requires the saved file back at factory.
// ============================================================================
#include "settings_hud.h"
#include "event_log_hud.h"
#include "fmx_hud.h"
#include "gap_bar_hud.h"
#include "ideal_lap_hud.h"
#include "lap_log_hud.h"
#include "map_hud.h"
#include "notices_hud.h"
#include "performance_hud.h"
#include "pitboard_hud.h"
#include "radar_hud.h"
#include "session_charts_hud.h"
#include "session_hud.h"
#include "timing_hud.h"
#include "standings_hud.h"
#include "time_widget.h"
#include "position_widget.h"
#include "lap_widget.h"
#include "speed_widget.h"
#include "gear_widget.h"
#include "crash_widget.h"
#include "speedo_widget.h"
#include "tacho_widget.h"
#include "bars_widget.h"
#include "version_widget.h"
#include "fuel_widget.h"
#include "pointer_widget.h"
#include "settings_button_widget.h"
#include "gamepad_widget.h"
#include "lean_widget.h"
#include "gforce_widget.h"
#include "compass_widget.h"
#include "clock_widget.h"
#if GAME_HAS_TYRE_TEMP
#include "tyre_temp_widget.h"
#endif
#if GAME_HAS_ECU
#include "ecu_widget.h"
#endif
#include "helmet_overlay_hud.h"
#include "telemetry_hud.h"
#include "stats_hud.h"
#include "records_hud.h"
#include "gl_confirm_hud.h"     // resetTabGeneral cancels the Direct GL prompt
#include "../diagnostics/logger.h"
#include "../core/settings_manager.h"
#include "../core/settings_keys.h"   // IniOnly::Advanced key spellings (General tab)
#include "../core/hud_manager.h"
#include "../core/profile_manager.h"
#include "../core/update_checker.h"
#include "../core/director_manager.h"
#include "../core/spotter_manager.h"
#include "../core/tracked_riders_manager.h"
#include "../core/ui_config.h"
#include "../core/plugin_data.h"
#include "../core/xinput_reader.h"
#include <string>
#include <vector>

using namespace PluginConstants;

void SettingsHud::resetToDefaults() {
    // Everything that lives OUTSIDE the per-profile HUD snapshot — colors, fonts, hotkeys,
    // rumble, helmet overlay, display units, the controller index, and every
    // [General]/[Advanced] tunable and toggle (hazard params, update checker, web server,
    // Discord, records provider, drop shadow, etc.) — is restored in one shot from the
    // factory-default snapshot captured at startup. This replaces a long hand-maintained
    // list of per-setting resets that used to drift whenever a new global setting was added:
    // the snapshot reuses the exact same serialization as save/load, so it can't fall out of
    // sync. (Developer mode is an INI-only power-user flag and is intentionally preserved.)
    SettingsManager::getInstance().resetGlobalsToFactoryDefaults(HudManager::getInstance());

    // autoSwitch lives in [Profiles] (session/navigation state, outside the global snapshot),
    // so reset it explicitly. The active profile itself is intentionally left unchanged.
    ProfileManager::getInstance().setAutoSwitchEnabled(false);

    // The widgets master toggle and all per-profile HUD/widget state are restored below by
    // resetAllToFactoryDefaults() (widgetsEnabled lives in the per-profile "Global" snapshot).

    // Reset every profile to the pristine factory snapshot and save. This forces even
    // INI-only overrides that a HUD's resetToDefaults() doesn't touch back to defaults, and
    // (unlike a plain reload) re-seeds the save baseline so user-edited base-section keys are
    // replaced with this build's defaults — a full factory reset intentionally discards them.
    SettingsManager::getInstance().resetAllToFactoryDefaults(HudManager::getInstance());

    // Rebuild AFTER all state is reset — globals AND the per-profile HUD/widget visibility
    // above — so the tab toggle icons reflect the reverted enabled/disabled state
    // immediately instead of only after the next mouse-move (hover) rebuild.
    rebuildRenderData();
}

void SettingsHud::resetCurrentTab() {
    // Reset the HUD(s) on the current tab to the captured factory-default snapshot.
    // Routing through SettingsManager (rather than each HUD's resetToDefaults())
    // guarantees every INI-controllable setting — including INI-only members and
    // per-HUD color/font overrides — returns to default, and by default it preserves
    // each HUD's current visibility so a per-tab reset doesn't hide an element the
    // user is positioning. (The Widgets tab opts out — see resetTabWidgets() — and
    // the full "Reset all settings" path resets visibility instead.)
    //
    // Routed via the descriptor registry: resetHud is the standard keep-visibility
    // HUD reset; resetExtra covers anything outside the per-HUD snapshot (see the
    // resetTab* bodies below).
    const TabDescriptor* tabDesc = findTabDescriptor(m_activeTab);
    if (tabDesc && (tabDesc->resetHud || tabDesc->resetExtra)) {
        if (tabDesc->resetHud) {
            SettingsManager::getInstance().resetHudsToFactoryDefaults(
                HudManager::getInstance(), {tabDesc->resetHud}, /*keepVisibility=*/true);
        }
        if (tabDesc->resetExtra) (this->*(tabDesc->resetExtra))();
    } else {
        DEBUG_WARN_F("Unknown tab index for reset: %d", m_activeTab);
    }

    // Update settings display
    rebuildRenderData();

    // Deferred: persisted on leave-track (auto-save) or via the Save button.
    SettingsManager::getInstance().markDirty();
}

// ----------------------------------------------------------------------------
// Per-tab custom reset bodies (TabDescriptor::resetExtra). Each covers the
// settings its tab shows that live OUTSIDE the per-HUD factory snapshot
// (global sections, manager state), so the registry's resetHud pass can't
// restore them. Simple tabs have no body here - resetHud alone is enough.
// ----------------------------------------------------------------------------

void SettingsHud::resetTabGeneral() {
    // THE KEYS THIS TAB OWNS, replayed from the factory snapshot - not a list of
    // setters. Every other reset path replays the snapshot through the applier
    // loadSettings() uses; this one used to call setPBScope(CATEGORY),
    // setAutoSave(true) and friends by hand, which is the "third list" the full
    // reset was deliberately built to avoid, with both of its failure modes:
    // every default was written down twice (so a changed constructor default did
    // not reach Reset), and a row added to the tab was not reset at all until
    // someone remembered to add a line here. Four rows had quietly fallen through
    // that gap - Grid Snap, Screen Clamp, Direct GL Rendering and Analytics.
    // Pinned by reset_tab_test.cpp, which clicks every control on every tab and
    // requires its Reset to put the file back.
    //
    // The list is by KEY rather than by section because this tab does not own a
    // section outright: most of [General] is its, but so are two [Display] toggles
    // (the rest of that section is the Appearance tab's) and two [Advanced] keys.
    using GlobalKeyRef = SettingsManager::GlobalKeyRef;
    std::vector<GlobalKeyRef> keys = {
        { "General", "pbScope" },
        { "General", "controller" },
        { "General", "autoSave" },
        // Shown here, persisted in [Display] (the Appearance tab excludes them).
        { "Display", "gridSnapping" },
        { "Display", "screenClamping" },
        { "Advanced", Settings::IniOnly::Advanced::GL_IN_GAME.key },
    };
#if GAME_HAS_STEAM_FRIENDS
    keys.push_back({ "General", "steamFriends" });
#endif
#if GAME_HAS_DISCORD
    keys.push_back({ "General", "discordRichPresence" });
#endif
#if GAME_HAS_HTTP_SERVER
    keys.push_back({ "General", "webServer" });
    keys.push_back({ "Advanced", Settings::IniOnly::Advanced::WEB_SERVER_PORT.key });
#endif
    // ANALYTICS IS DELIBERATELY ABSENT. Its factory value is ON, so replaying it
    // would turn a player's opt-out back on from a button that says "Reset
    // General" - consent is not a tuning value, and the row is one click away if
    // they want it back. Same shape as every other master switch a per-tab reset
    // leaves alone (Updates' check mode, Director/Spotter enabled, Rumble
    // enabled, HUD visibility). The full "Reset all settings" does restore it,
    // like everything else.
    SettingsManager::getInstance().resetGlobalKeysToFactoryDefaults(HudManager::getInstance(), keys);

    // [Profiles] is written by saveSettings() rather than writeGlobalSettings(), so it
    // is not in the global snapshot at all - this one really is set by hand.
    ProfileManager::getInstance().setAutoSwitchEnabled(false);

    // Direct GL just went back to its factory value (off), so the "can you still read
    // this?" prompt has nothing left to confirm and the backend's fail latch is stale.
    if (!UiConfig::getInstance().getGlInGame()) {
        HudManager::getInstance().clearGlFailLatch();
        HudManager::getInstance().getGlConfirmHud().cancel();
    }

    // Mark all HUDs dirty for drop shadow / unit changes
    HudManager::getInstance().markAllHudsDirty();
}

void SettingsHud::resetTabAppearance() {
    // Appearance tab - reset display (units/clock format), fonts, and colors. These
    // map 1:1 to the [Display]/[Fonts]/[Colors] INI sections (no other tab touches
    // them), so restore them straight from the factory-default snapshot — the same
    // path the full reset uses — instead of by hand. Adding a new [Display] key no
    // longer requires updating this tab's reset.
    // ...minus the two [Display] toggles the General tab shows and resets. A tab's
    // Reset restores what that tab can change and nothing else.
    SettingsManager::getInstance().resetGlobalSectionsToFactoryDefaults(
        HudManager::getInstance(), {"Display", "Fonts", "Colors"},
        {{ "Display", "gridSnapping" }, { "Display", "screenClamping" }});
    // Mark all HUDs dirty so they pick up new colors
    if (m_idealLap) m_idealLap->setDataDirty();
    if (m_lapLog) m_lapLog->setDataDirty();
    if (m_standings) m_standings->setDataDirty();
    if (m_performance) m_performance->setDataDirty();
    if (m_telemetry) m_telemetry->setDataDirty();
    if (m_mapHud) m_mapHud->setDataDirty();
    if (m_radarHud) m_radarHud->setDataDirty();
    if (m_pitboard) m_pitboard->setDataDirty();
    if (m_records) m_records->setDataDirty();
    if (m_timing) m_timing->setDataDirty();
    if (m_gapBar) m_gapBar->setDataDirty();
    if (m_lap) m_lap->setDataDirty();
    if (m_position) m_position->setDataDirty();
    if (m_time) m_time->setDataDirty();
    if (m_session) m_session->setDataDirty();
    if (m_speed) m_speed->setDataDirty();
    if (m_speedo) m_speedo->setDataDirty();
    if (m_tacho) m_tacho->setDataDirty();
    if (m_notices) m_notices->setDataDirty();
    if (m_bars) m_bars->setDataDirty();
    if (m_version) m_version->setDataDirty();
    if (m_fuel) m_fuel->setDataDirty();
    if (m_sessionCharts) m_sessionCharts->setDataDirty();
    if (m_gear) m_gear->setDataDirty();
    if (m_lean) m_lean->setDataDirty();
    if (m_clock) m_clock->setDataDirty();
    if (m_gamepad) m_gamepad->setDataDirty();
    if (m_fmxHud) m_fmxHud->setDataDirty();
    if (m_statsHud) m_statsHud->setDataDirty();
    if (m_eventLog) m_eventLog->setDataDirty();
}

void SettingsHud::resetTabStandingsExtra() {
    // DNS filter lives in PluginData (the global [General] section), not the
    // per-HUD snapshot, so the resetHud pass can't restore it. Reset it explicitly.
    // (Live gaps is now a StandingsHud member, restored by the resetHud pass.)
    PluginData::getInstance().setFilterDnsRiders(false);
}

void SettingsHud::resetTabRecordsExtra() {
    // Provider and auto-fetch are saved in the global [General] section, not
    // the per-HUD snapshot, so the resetHud pass can't restore them. Reset them
    // explicitly to their factory defaults (CBR provider, auto-fetch off).
    if (m_records) {
        m_records->m_provider = RecordsHud::DataProvider::CBR;
        m_records->m_bAutoFetch = false;
        m_records->setDataDirty();
    }
}

void SettingsHud::resetTabWidgets() {
    // Reset all widgets in a single pass
    std::vector<std::string> widgets = {
        "LapWidget", "PositionWidget", "TimeWidget", "SpeedWidget", "GearWidget",
        "SpeedoWidget", "TachoWidget", "BarsWidget", "VersionWidget", "FuelWidget",
        "GamepadWidget", "LeanWidget", "GForceWidget", "CompassWidget", "ClockWidget",
        "PointerWidget", "SettingsButtonWidget", "CrashWidget"
    };
#if GAME_HAS_TYRE_TEMP
    widgets.push_back("TyreTempWidget");
#endif
#if GAME_HAS_ECU
    widgets.push_back("EcuWidget");
#endif
    // keepVisibility=false: the Widgets tab exposes a per-widget "Visible"
    // toggle for every row, so Reset restores those toggles to factory
    // defaults too (not just position/scale/opacity).
    SettingsManager::getInstance().resetHudsToFactoryDefaults(
        HudManager::getInstance(), widgets, /*keepVisibility=*/false);
    // The Pointer row's second control is the menu-only-cursor toggle, which is a
    // [Display] key rather than a widget setting - the pass above cannot reach it.
    SettingsManager::getInstance().resetGlobalKeysToFactoryDefaults(
        HudManager::getInstance(), {{ "Display", "menuOnlyCursor" }});
}

void SettingsHud::resetTabRumble() {
    // Reset rumble configuration from the [Rumble] snapshot (same path as the full
    // reset) plus the RumbleHud. Preserve the master "enabled" toggle, like every
    // other per-tab reset leaves its master alone. controllerIndex is configured on
    // the General tab and isn't part of [Rumble], so the replay never touches it.
    RumbleConfig& rumbleCfg = XInputReader::getInstance().getGlobalRumbleConfig();
    bool wasEnabled = rumbleCfg.enabled;
    SettingsManager::getInstance().resetGlobalSectionsToFactoryDefaults(
        HudManager::getInstance(), {"Rumble"});
    rumbleCfg.enabled = wasEnabled;
    SettingsManager::getInstance().resetHudsToFactoryDefaults(
        HudManager::getInstance(), {"RumbleHud"}, /*keepVisibility=*/true);
}

void SettingsHud::resetTabHelmet() {
    // HelmetOverlay maps 1:1 to the [HelmetOverlay] snapshot section. Replay it (same
    // path as the full reset) while preserving visibility, so a per-tab reset doesn't
    // hide the overlay the user is positioning.
    if (m_helmetOverlay) {
        bool wasVisible = m_helmetOverlay->isVisible();
        SettingsManager::getInstance().resetGlobalSectionsToFactoryDefaults(
            HudManager::getInstance(), {"HelmetOverlay"});
        m_helmetOverlay->setVisible(wasVisible);
        m_helmetOverlay->setDataDirty();
    }
}

void SettingsHud::resetTabHotkeys() {
    // Hotkey bindings map 1:1 to the [Hotkeys] snapshot section.
    SettingsManager::getInstance().resetGlobalSectionsToFactoryDefaults(
        HudManager::getInstance(), {"Hotkeys"});
}

void SettingsHud::resetTabUpdates() {
    // Update settings map 1:1 to the [Updates] snapshot section. Replay it (same
    // path as the full reset), but leave the "Check for Updates" mode (the master
    // on/off toggle) alone — like a HUD's visibility in the resetHud path, the
    // master state is preserved here; full "Reset all settings" disables it instead.
    UpdateChecker::UpdateMode mode = UpdateChecker::getInstance().getMode();
    SettingsManager::getInstance().resetGlobalSectionsToFactoryDefaults(
        HudManager::getInstance(), {"Updates"});
    UpdateChecker::getInstance().setMode(mode);
}

void SettingsHud::resetTabRiders() {
    // Clear all tracked riders
    TrackedRidersManager::getInstance().clearAll();
}

void SettingsHud::resetTabDirector() {
    // Director maps 1:1 to the [Director] snapshot section. Replay it but leave
    // the master enable alone (like Updates' check toggle); a full "Reset all
    // settings" disables it instead.
    bool wasEnabled = DirectorManager::getInstance().isEnabled();
    SettingsManager::getInstance().resetGlobalSectionsToFactoryDefaults(
        HudManager::getInstance(), {"Director"});
    DirectorManager::getInstance().setEnabled(wasEnabled);
}

void SettingsHud::resetTabSpotter() {
    // Spotter maps 1:1 to the [Spotter] snapshot section. Replay it but keep
    // the two MASTER switches, mirroring the Director tab's reset: a user
    // resetting tuning shouldn't have their spotter silently switched off (or
    // on). BOTH of them -- subtitles-only is a real mode (spotter_manager.h),
    // so preserving `enabled` alone silenced a subtitles-only user completely
    // while faithfully preserving the `enabled=false` they were already in.
    const bool wasEnabled = SpotterManager::getInstance().isEnabled();
    const bool wasSubtitles = SpotterManager::getInstance().isSubtitlesEnabled();
    SettingsManager::getInstance().resetGlobalSectionsToFactoryDefaults(
        HudManager::getInstance(), {"Spotter"});
    SpotterManager::getInstance().setEnabled(wasEnabled);
    SpotterManager::getInstance().setSubtitlesEnabled(wasSubtitles);
    // The tab's Appearance rows edit the SpotterWidget, which is a per-profile HUD and
    // therefore not in [Spotter] at all - the section replay above cannot see it.
    // (The Director tab needs no equivalent: its widget's position and scale are
    // written INTO [Director] as hudX/hudY/hudScale/hudOpacity.)
    SettingsManager::getInstance().resetHudsToFactoryDefaults(
        HudManager::getInstance(), {"SpotterWidget"}, /*keepVisibility=*/true);
}

void SettingsHud::resetCurrentProfile() {
    // Reset only Elements (HUDs and Widgets) for the current profile by re-applying
    // the factory snapshot to the active profile. Like the per-tab and full-reset
    // paths, this also clears INI-only members and per-HUD color/font overrides.
    // HelmetOverlay (global, not in the snapshot) is left untouched here — it's only
    // reset via the Helmet tab or the full "Reset all settings".
    SettingsManager::getInstance().resetActiveProfileToFactoryDefaults(HudManager::getInstance());

    // DNS filter lives in the global [General] section, not the snapshot, so reset it
    // explicitly (matches prior behavior). Other global settings (ColorConfig,
    // RumbleConfig, UpdateChecker, hazard params) are NOT reset. (Live gaps is now a
    // StandingsHud member, restored by resetActiveProfileToFactoryDefaults above.)
    PluginData::getInstance().setFilterDnsRiders(false);

    // Update settings display
    rebuildRenderData();

    // Deferred: persisted on leave-track (auto-save) or via the Save button.
    SettingsManager::getInstance().markDirty();
}
