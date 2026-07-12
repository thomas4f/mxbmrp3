// ============================================================================
// hud/settings_hud_input.cpp
// SettingsHud user-interaction handling: click hit-testing and dispatch
// (findClickRegionAt / handleClick / dispatchRegion / handleRightClick), the
// individual control handlers (checkbox / toggle / opacity / scale / display-
// mode / tab / close), and the reset operations (resetToDefaults /
// resetCurrentTab / resetCurrentProfile). Split out of settings_hud.cpp, which
// keeps menu construction (rebuildRenderData) and lifecycle. Per-tab layout
// lives in hud/settings/settings_tab_*.cpp.
// ============================================================================
#include "settings_hud.h"
#include "settings/settings_layout.h"
#include "telemetry_hud.h"
#include "rumble_hud.h"
#include "helmet_overlay_hud.h"
#include "fmx_hud.h"
#include "stats_hud.h"
#include "settings_button_widget.h"
#include "../diagnostics/logger.h"
#include "../core/plugin_utils.h"
#include "../core/plugin_constants.h"
#include "../core/input_manager.h"
#include "../core/plugin_manager.h"
#include "../core/settings_manager.h"
#include "../core/hud_manager.h"
#include "../core/profile_manager.h"
#include "../core/update_checker.h"
#include "../core/update_downloader.h"
#include "../core/director_manager.h"
#include "director_widget.h"
#include "../core/hotkey_manager.h"
#if GAME_HAS_DISCORD
#include "../core/discord_manager.h"
#endif
#if GAME_HAS_STEAM_FRIENDS
#include "../core/steam_friends_manager.h"
#endif
#if GAME_HAS_HTTP_SERVER
#include "../core/http_server.h"
#endif
#include "../core/tracked_riders_manager.h"
#include "../core/asset_manager.h"
#include "../core/ui_config.h"
#include "../core/plugin_data.h"
#include "../core/tooltip_manager.h"
#include "../handlers/draw_handler.h"
#include <cstring>
#include <algorithm>
#include <cmath>

using namespace PluginConstants;

int SettingsHud::findClickRegionAt(float x, float y) const {
    for (size_t i = 0; i < m_clickRegions.size(); ++i) {
        const auto& region = m_clickRegions[i];
        if (region.type == ClickRegion::TOOLTIP_ROW) continue;  // hover-only
        if (isPointInRect(x, y, region.x, region.y, region.width, region.height)) {
            return static_cast<int>(i);
        }
    }
    return -1;
}

void SettingsHud::handleClick(float mouseX, float mouseY) {
    // Check each clickable region
    for (const auto& region : m_clickRegions) {
        if (isPointInRect(mouseX, mouseY, region.x, region.y, region.width, region.height)) {
            // Skip TOOLTIP_ROW regions - they're hover-only for tooltip display
            if (region.type == ClickRegion::TOOLTIP_ROW) continue;

            dispatchRegion(region);
            return;  // Only process one click per frame
        }
    }
}

void SettingsHud::dispatchRegion(const ClickRegion& region, bool skipSave) {
    // Try tab-specific handlers first (implemented in separate files)
    bool handled = false;
    switch (m_activeTab) {
        case TAB_MAP:        handled = handleClickTabMap(region); break;
        case TAB_RADAR:      handled = handleClickTabRadar(region); break;
        case TAB_TIMING:     handled = handleClickTabTiming(region); break;
        case TAB_GAP_BAR:    handled = handleClickTabGapBar(region); break;
        case TAB_STANDINGS:  handled = handleClickTabStandings(region); break;
        case TAB_RUMBLE:     handled = handleClickTabRumble(region); break;
        case TAB_HELMET:     handled = handleClickTabHelmet(region); break;
        case TAB_APPEARANCE: handled = handleClickTabAppearance(region); break;
        case TAB_GENERAL:    handled = handleClickTabGeneral(region); break;
        case TAB_HOTKEYS:    handled = handleClickTabHotkeys(region); break;
        case TAB_RIDERS:     handled = handleClickTabRiders(region); break;
        case TAB_RECORDS:    handled = handleClickTabRecords(region); break;
        case TAB_PITBOARD:   handled = handleClickTabPitboard(region); break;
        case TAB_SESSION:    handled = handleClickTabSession(region); break;
        case TAB_LAP_LOG:    handled = handleClickTabLapLog(region); break;
        case TAB_FRIENDS:    handled = handleClickTabFriends(region); break;
        case TAB_SESSION_CHARTS: handled = handleClickTabSessionCharts(region); break;
        case TAB_UPDATES:    handled = handleClickTabUpdates(region); break;
        case TAB_FMX:        handled = handleClickTabFmx(region); break;
        case TAB_STATS:      handled = handleClickTabStats(region); break;
        case TAB_EVENT_LOG:  handled = handleClickTabEventLog(region); break;
        case TAB_NOTICES:    handled = handleClickTabNotices(region); break;
        default: break;
    }

    if (handled) {
        // Tab handler processed the click - save if not deferred (auto-save gate is inside).
        if (!skipSave) markSettingsDirty();
        return;
    }

    // Fall through to common handlers for shared controls
    switch (region.type) {
        // ============================================
        // Common handlers (used across multiple tabs)
        // Tab-specific handlers are in settings_tab_*.cpp files
        // ============================================

        case ClickRegion::CHECKBOX:
            handleCheckboxClick(region);
            break;

        case ClickRegion::HUD_TOGGLE:
            handleHudToggleClick(region);
            break;

        // Pointer widget row's menu-only-cursor toggle (moved here from the General
        // tab). In the common switch so it's reachable from the Widgets tab; the
        // trailing auto-save at the end of this function persists it ([Display]).
        case ClickRegion::MENU_ONLY_CURSOR_TOGGLE:
            UiConfig::getInstance().setMenuOnlyCursor(!UiConfig::getInstance().getMenuOnlyCursor());
            rebuildRenderData();
            break;
        case ClickRegion::WIDGETS_TOGGLE:
            {
                HudManager& hudManager = HudManager::getInstance();
                hudManager.setWidgetsEnabled(!hudManager.areWidgetsEnabled());
                rebuildRenderData();
                DEBUG_INFO_F("Widgets master toggle: %s", hudManager.areWidgetsEnabled() ? "enabled" : "disabled");
            }
            break;
        case ClickRegion::UPDATE_CHECK_TOGGLE:
            {
                UpdateChecker& checker = UpdateChecker::getInstance();
                bool newState = !checker.isEnabled();
                checker.setEnabled(newState);
                if (newState && !checker.isChecking()) {
                    // Trigger an update check when enabled
                    checker.setCompletionCallback([this]() {
                        setDataDirty();
                    });
                    checker.checkForUpdates();
                }
                rebuildRenderData();
                DEBUG_INFO_F("Update checking toggle: %s", newState ? "enabled" : "disabled");
            }
            break;
        case ClickRegion::RUMBLE_TOGGLE:
            {
                RumbleConfig& globalConfig = XInputReader::getInstance().getGlobalRumbleConfig();
                globalConfig.enabled = !globalConfig.enabled;
                rebuildRenderData();
                DEBUG_INFO_F("Rumble master toggle: %s", globalConfig.enabled ? "enabled" : "disabled");
            }
            break;
        case ClickRegion::DIRECTOR_ENABLE_TOGGLE:
            {
                DirectorManager& director = DirectorManager::getInstance();
                director.setEnabled(!director.isEnabled());
                rebuildRenderData();
            }
            break;
        case ClickRegion::DIRECTOR_MINSHOT_DOWN:
            DirectorManager::getInstance().setMinShotSec(DirectorManager::getInstance().getMinShotSec() - 1);
            rebuildRenderData();
            break;
        case ClickRegion::DIRECTOR_MINSHOT_UP:
            DirectorManager::getInstance().setMinShotSec(DirectorManager::getInstance().getMinShotSec() + 1);
            rebuildRenderData();
            break;
        case ClickRegion::DIRECTOR_MAXSHOT_DOWN:
            DirectorManager::getInstance().setMaxShotSec(DirectorManager::getInstance().getMaxShotSec() - 1);
            rebuildRenderData();
            break;
        case ClickRegion::DIRECTOR_MAXSHOT_UP:
            DirectorManager::getInstance().setMaxShotSec(DirectorManager::getInstance().getMaxShotSec() + 1);
            rebuildRenderData();
            break;
        case ClickRegion::DIRECTOR_BATTLEGAP_DOWN:
            DirectorManager::getInstance().setBattleGapMs(DirectorManager::getInstance().getBattleGapMs() - DirectorManager::BATTLE_GAP_STEP);
            rebuildRenderData();
            break;
        case ClickRegion::DIRECTOR_BATTLEGAP_UP:
            DirectorManager::getInstance().setBattleGapMs(DirectorManager::getInstance().getBattleGapMs() + DirectorManager::BATTLE_GAP_STEP);
            rebuildRenderData();
            break;
        case ClickRegion::DIRECTOR_BATTLEMAXPOS_DOWN:
            DirectorManager::getInstance().setBattleMaxPos(DirectorManager::getInstance().getBattleMaxPos() - 1);
            rebuildRenderData();
            break;
        case ClickRegion::DIRECTOR_BATTLEMAXPOS_UP:
            DirectorManager::getInstance().setBattleMaxPos(DirectorManager::getInstance().getBattleMaxPos() + 1);
            rebuildRenderData();
            break;
        case ClickRegion::DIRECTOR_RESUME_DOWN:
            DirectorManager::getInstance().setManualResumeSec(DirectorManager::getInstance().getManualResumeSec() - DirectorManager::RESUME_STEP);
            rebuildRenderData();
            break;
        case ClickRegion::DIRECTOR_RESUME_UP:
            DirectorManager::getInstance().setManualResumeSec(DirectorManager::getInstance().getManualResumeSec() + DirectorManager::RESUME_STEP);
            rebuildRenderData();
            break;
        case ClickRegion::DIRECTOR_VARIETY_DOWN: {
            // Below the meaningful minimum rolls to Off (0); a plain -1 would clamp
            // back up and never reach it.
            DirectorManager& d = DirectorManager::getInstance();
            int v = d.getVarietyEvery();
            d.setVarietyEvery(v <= DirectorManager::VARIETY_LO ? 0 : v - 1);
            rebuildRenderData();
            break;
        }
        case ClickRegion::DIRECTOR_VARIETY_UP: {
            // From Off, step up to the meaningful minimum.
            DirectorManager& d = DirectorManager::getInstance();
            int v = d.getVarietyEvery();
            d.setVarietyEvery(v <= 0 ? DirectorManager::VARIETY_LO : v + 1);
            rebuildRenderData();
            break;
        }
        case ClickRegion::DIRECTOR_HOLD_DOWN:
            DirectorManager::getInstance().setHoldSec(DirectorManager::getInstance().getHoldSec() - 1);
            rebuildRenderData();
            break;
        case ClickRegion::DIRECTOR_HOLD_UP:
            DirectorManager::getInstance().setHoldSec(DirectorManager::getInstance().getHoldSec() + 1);
            rebuildRenderData();
            break;
        case ClickRegion::DIRECTOR_CAM_FENDER_UP:
        case ClickRegion::DIRECTOR_CAM_FENDER_DOWN: {
            // One cycle over the two fender bools: Off(0) > Front(1) > Rear(2) > Both(3).
            DirectorManager& d = DirectorManager::getInstance();
            int order = (d.getCamFront() ? 1 : 0) + (d.getCamRear() ? 2 : 0);
            order = (region.type == ClickRegion::DIRECTOR_CAM_FENDER_UP) ? (order + 1) % 4
                                                                         : (order + 3) % 4;
            d.setCamFront((order & 1) != 0);
            d.setCamRear((order & 2) != 0);
            rebuildRenderData();
            break;
        }
        case ClickRegion::DIRECTOR_CAM_HELMET_UP:
        case ClickRegion::DIRECTOR_CAM_HELMET_DOWN: {
            // One cycle over the two helmet bools: Off(0) > Helmet 1(1) > Helmet 2(2) > Both(3).
            DirectorManager& d = DirectorManager::getInstance();
            int order = (d.getCamHelmet() ? 1 : 0) + (d.getCamHelmet2() ? 2 : 0);
            order = (region.type == ClickRegion::DIRECTOR_CAM_HELMET_UP) ? (order + 1) % 4
                                                                         : (order + 3) % 4;
            d.setCamHelmet((order & 1) != 0);
            d.setCamHelmet2((order & 2) != 0);
            rebuildRenderData();
            break;
        }
        case ClickRegion::DIRECTOR_GAMEPAD_TAKEOVER:
            DirectorManager::getInstance().setGamepadTakeover(!DirectorManager::getInstance().getGamepadTakeover());
            rebuildRenderData();
            break;
        case ClickRegion::DIRECTOR_FOLLOW_BATTLES:
            DirectorManager::getInstance().setFollowBattles(!DirectorManager::getInstance().getFollowBattles());
            rebuildRenderData();
            break;
        case ClickRegion::DIRECTOR_FOLLOW_INCIDENTS:
            DirectorManager::getInstance().setFollowIncidents(!DirectorManager::getInstance().getFollowIncidents());
            rebuildRenderData();
            break;
        case ClickRegion::DIRECTOR_FOLLOW_DROPS:
            DirectorManager::getInstance().setFollowDrops(!DirectorManager::getInstance().getFollowDrops());
            rebuildRenderData();
            break;
        case ClickRegion::DIRECTOR_FOLLOW_PACE:
            DirectorManager::getInstance().setFollowPace(!DirectorManager::getInstance().getFollowPace());
            rebuildRenderData();
            break;
        case ClickRegion::DIRECTOR_FOLLOW_FASTEST:
            DirectorManager::getInstance().setFollowFastestLap(!DirectorManager::getInstance().getFollowFastestLap());
            rebuildRenderData();
            break;
        case ClickRegion::DIRECTOR_FINISH_LOCK:
            DirectorManager::getInstance().setFinishLock(!DirectorManager::getInstance().getFinishLock());
            rebuildRenderData();
            break;
        case ClickRegion::DIRECTOR_CATCH_OVERTAKES:
            DirectorManager::getInstance().setCatchOvertakes(!DirectorManager::getInstance().getCatchOvertakes());
            rebuildRenderData();
            break;
        case ClickRegion::DIRECTOR_FOLLOW_LAPPERS:
            DirectorManager::getInstance().setFollowLappers(!DirectorManager::getInstance().getFollowLappers());
            rebuildRenderData();
            break;
        case ClickRegion::DIRECTOR_HUD_VISIBLE:
            if (DirectorWidget* dh = HudManager::getInstance().getDirectorWidget()) {
                dh->setVisible(!dh->isVisible());
            }
            rebuildRenderData();
            break;
        case ClickRegion::HELMET_OVERLAY_TOGGLE:
            if (m_helmetOverlay) {
                // Visibility gate only — doesn't touch individual enable flags
                // (same pattern as WIDGETS_TOGGLE)
                m_helmetOverlay->setVisible(!m_helmetOverlay->isVisible());
                rebuildRenderData();
                DEBUG_INFO_F("Helmet overlay master toggle: %s",
                    m_helmetOverlay->isVisible() ? "visible" : "hidden");
            }
            break;
        case ClickRegion::TITLE_TOGGLE:
            handleTitleToggleClick(region);
            break;
        case ClickRegion::TEXTURE_VARIANT_UP:
            if (region.targetHud) {
                region.targetHud->cycleTextureVariant(true);
                rebuildRenderData();
            }
            break;
        case ClickRegion::TEXTURE_VARIANT_DOWN:
            if (region.targetHud) {
                region.targetHud->cycleTextureVariant(false);
                rebuildRenderData();
            }
            break;
        case ClickRegion::BACKGROUND_OPACITY_UP:
            handleOpacityClick(region, true);
            break;
        case ClickRegion::BACKGROUND_OPACITY_DOWN:
            handleOpacityClick(region, false);
            break;
        case ClickRegion::SCALE_UP:
            handleScaleClick(region, true);
            break;
        case ClickRegion::SCALE_DOWN:
            handleScaleClick(region, false);
            break;
        // Note: ROW_COUNT, LAP_LOG_ROW_COUNT, MAP_*, RADAR_* handlers moved to tab files

        case ClickRegion::DISPLAY_MODE_UP:
            handleDisplayModeClick(region, true);
            break;
        case ClickRegion::DISPLAY_MODE_DOWN:
            handleDisplayModeClick(region, false);
            break;
        // Profile cycle controls are in sidebar, must work from ALL tabs
        case ClickRegion::PROFILE_CYCLE_UP:
            {
                ProfileType nextProfile = ProfileManager::getNextProfile(
                    ProfileManager::getInstance().getActiveProfile());
                SettingsManager::getInstance().switchProfile(HudManager::getInstance(), nextProfile);
                rebuildRenderData();
            }
            return;  // Don't save - switchProfile already saves
        case ClickRegion::PROFILE_CYCLE_DOWN:
            {
                ProfileType prevProfile = ProfileManager::getPreviousProfile(
                    ProfileManager::getInstance().getActiveProfile());
                SettingsManager::getInstance().switchProfile(HudManager::getInstance(), prevProfile);
                rebuildRenderData();
            }
            return;  // Don't save - switchProfile already saves
        // Note: Tab-specific handlers moved to settings_tab_*.cpp files:
        // RECORDS_COUNT, PITBOARD_SHOW_MODE, TIMING_*, GAPBAR_*,
        // COLOR_CYCLE_*, FONT_CATEGORY_*, SPEED_UNIT, FUEL_UNIT,
        // GRID_SNAP, UPDATE_CHECK, COPY_*, RESET_*
        // Clock widget toggles (used from Widgets tab and General tab)
        case ClickRegion::CLOCK_FORMAT_TOGGLE:
            if (m_clock) {
                m_clock->setFormat24h(!m_clock->getFormat24h());
                rebuildRenderData();
            }
            break;
        case ClickRegion::RESET_TAB_BUTTON:
            {
                resetCurrentTab();
                DEBUG_INFO_F("Tab %d reset to defaults", m_activeTab);
            }
            break;
        case ClickRegion::TAB:
            handleTabClick(region);
            return;  // Don't save settings, just UI state change
        case ClickRegion::CLOSE_BUTTON:
            handleCloseButtonClick();
            return;  // Don't save settings, just close the menu
        case ClickRegion::SAVE_BUTTON:
            // Manual save (available regardless of Auto-Save) — persist now without leaving the
            // track. saveSettings() clears the dirty flag; rebuild so the button greys to "Saved".
            SettingsManager::getInstance().saveSettings(HudManager::getInstance(), PluginManager::getInstance().getSavePath());
            DEBUG_INFO("Settings saved manually");
            rebuildRenderData();
            return;  // Already saved
        // Note: Tab-specific handlers moved to settings_tab_*.cpp files:
        // RUMBLE_*, HOTKEY_*, RIDER_*, pagination controls

        case ClickRegion::VERSION_CLICK:
            {
                // If update is available, navigate to Updates tab. Gate on isEnabled() to
                // match the footer's render gate — a stale UPDATE_AVAILABLE status when
                // updates are disabled shouldn't hijack the version click (easter egg).
                if (UpdateChecker::getInstance().isEnabled() &&
                    UpdateChecker::getInstance().getStatus() == UpdateChecker::Status::UPDATE_AVAILABLE) {
                    m_activeTab = TAB_UPDATES;
                    rebuildRenderData();
                    return;  // Don't process easter egg
                }

                // Otherwise, easter egg logic
                long long currentTimeUs = DrawHandler::getCurrentTimeUs();
                // Reset counter if timeout elapsed
                if (m_versionClickCount > 0 && (currentTimeUs - m_lastVersionClickTimeUs) > EASTER_EGG_TIMEOUT_US) {
                    m_versionClickCount = 0;
                }
                m_versionClickCount++;
                m_lastVersionClickTimeUs = currentTimeUs;
                // Check if threshold reached
                if (m_versionClickCount >= EASTER_EGG_CLICKS) {
                    m_versionClickCount = 0;
                    if (m_version) {
                        hide();  // Close settings before starting game
                        m_version->startGame();
                    }
                }
            }
            break;

        default:
            DEBUG_WARN_F("Unknown ClickRegion type: %d", static_cast<int>(region.type));
            break;
    }

    // Save settings after any modification (except TAB, CLOSE_BUTTON, SAVE_BUTTON, DISCARD_BUTTON)
    // Only save if not deferred (during hold-to-repeat); auto-save gate is inside the helper.
    if (!skipSave) markSettingsDirty();
}

void SettingsHud::handleRightClick(float mouseX, float mouseY) {
    // Right-click handling for TAB_RIDERS - cycles shape on icon
    for (const auto& region : m_clickRegions) {
        if (isPointInRect(mouseX, mouseY, region.x, region.y, region.width, region.height)) {
            // On right-click, treat RIDER_COLOR_NEXT as shape cycle
            if (region.type == ClickRegion::RIDER_COLOR_NEXT) {
                auto* namePtr = std::get_if<std::string>(&region.targetPointer);
                if (namePtr) {
                    TrackedRidersManager::getInstance().cycleTrackedRiderShape(*namePtr, true);
                    rebuildRenderData();
                    markSettingsDirty();
                }
                return;
            }
        }
    }
}

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
    // user is positioning. (The Widgets tab opts out — see its case below — and the
    // full "Reset all settings" path resets visibility instead.)
    auto resetHuds = [](const std::vector<std::string>& names, bool keepVisibility = true) {
        SettingsManager::getInstance().resetHudsToFactoryDefaults(HudManager::getInstance(), names, keepVisibility);
    };

    // Reset only the HUD(s) associated with the current tab
    switch (m_activeTab) {
        case TAB_GENERAL:
            // General tab - reset all settings displayed on the General tab.
            // (Display section — units/clock format, grid snap, screen clamp — moved to
            // the Appearance tab; reset there via the [Display] factory snapshot.)
            // Preferences section
            UiConfig::getInstance().setPBScope(PBScope::CATEGORY);
            XInputReader::getInstance().getRumbleConfig().controllerIndex = 0;
            XInputReader::getInstance().setControllerIndex(0);
            UiConfig::getInstance().setAutoSave(true);
#if GAME_HAS_STEAM_FRIENDS
            SteamFriendsManager::getInstance().setEnabled(true);  // default on, unlike Discord/HTTP
#endif
#if GAME_HAS_DISCORD
            DiscordManager::getInstance().setEnabled(false);
#endif
#if GAME_HAS_HTTP_SERVER
            HttpServer::getInstance().setEnabled(false);
            HttpServer::getInstance().resetPortToDefault();
#endif
            // Profiles section
            ProfileManager::getInstance().setAutoSwitchEnabled(false);
            // Mark all HUDs dirty for drop shadow / unit changes
            HudManager::getInstance().markAllHudsDirty();
            break;
        case TAB_APPEARANCE:
            // Appearance tab - reset display (units/clock format), fonts, and colors. These
            // map 1:1 to the [Display]/[Fonts]/[Colors] INI sections (no other tab touches
            // them), so restore them straight from the factory-default snapshot — the same
            // path the full reset uses — instead of by hand. Adding a new [Display] key no
            // longer requires updating this tab's reset.
            SettingsManager::getInstance().resetGlobalSectionsToFactoryDefaults(
                HudManager::getInstance(), {"Display", "Fonts", "Colors"});
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
            break;
        case TAB_STANDINGS:
            resetHuds({"StandingsHud"});
            // DNS filter lives in PluginData (the global [General] section), not the
            // per-HUD snapshot, so resetHuds() can't restore it. Reset it explicitly.
            // (Live gaps is now a StandingsHud member, restored by resetHuds above.)
            PluginData::getInstance().setFilterDnsRiders(false);
            break;
        case TAB_MAP:
            resetHuds({"MapHud"});
            break;
        case TAB_RADAR:
            resetHuds({"RadarHud"});
            break;
        case TAB_LAP_LOG:
            resetHuds({"LapLogHud"});
            break;
        case TAB_FRIENDS:
            resetHuds({"FriendsHud"});
            break;
        case TAB_SESSION_CHARTS:
            resetHuds({"SessionChartsHud"});
            break;
        case TAB_IDEAL_LAP:
            resetHuds({"IdealLapHud"});
            break;
        case TAB_TELEMETRY:
            resetHuds({"TelemetryHud"});
            break;
        case TAB_RECORDS:
            resetHuds({"RecordsHud"});
            // Provider and auto-fetch are saved in the global [General] section, not
            // the per-HUD snapshot, so resetHuds() can't restore them. Reset them
            // explicitly to their factory defaults (CBR provider, auto-fetch off).
            if (m_records) {
                m_records->m_provider = RecordsHud::DataProvider::CBR;
                m_records->m_bAutoFetch = false;
                m_records->setDataDirty();
            }
            break;
        case TAB_PITBOARD:
            resetHuds({"PitboardHud"});
            break;
        case TAB_SESSION:
            resetHuds({"SessionHud"});
            break;
        case TAB_PERFORMANCE:
            resetHuds({"PerformanceHud"});
            break;
        case TAB_TIMING:
            resetHuds({"TimingHud"});
            break;
        case TAB_GAP_BAR:
            resetHuds({"GapBarHud"});
            break;
        case TAB_NOTICES:
            resetHuds({"NoticesHud"});
            break;
        case TAB_EVENT_LOG:
            resetHuds({"EventLogHud"});
            break;
        case TAB_WIDGETS: {
            // Reset all widgets in a single pass
            std::vector<std::string> widgets = {
                "LapWidget", "PositionWidget", "TimeWidget", "SpeedWidget", "GearWidget",
                "SpeedoWidget", "TachoWidget", "BarsWidget", "VersionWidget", "FuelWidget",
                "GamepadWidget", "LeanWidget", "GForceWidget", "CompassWidget", "ClockWidget",
                "PointerWidget", "SettingsButtonWidget"
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
            resetHuds(widgets, false);
            break;
        }
        case TAB_RUMBLE: {
            // Reset rumble configuration from the [Rumble] snapshot (same path as the full
            // reset) plus the RumbleHud. Preserve the master "enabled" toggle, like every
            // other per-tab reset leaves its master alone. controllerIndex is configured on
            // the General tab and isn't part of [Rumble], so the replay never touches it.
            RumbleConfig& rumbleCfg = XInputReader::getInstance().getGlobalRumbleConfig();
            bool wasEnabled = rumbleCfg.enabled;
            SettingsManager::getInstance().resetGlobalSectionsToFactoryDefaults(
                HudManager::getInstance(), {"Rumble"});
            rumbleCfg.enabled = wasEnabled;
            resetHuds({"RumbleHud"});
            break;
        }
        case TAB_HELMET:
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
            break;
        case TAB_HOTKEYS:
            // Hotkey bindings map 1:1 to the [Hotkeys] snapshot section.
            SettingsManager::getInstance().resetGlobalSectionsToFactoryDefaults(
                HudManager::getInstance(), {"Hotkeys"});
            break;
        case TAB_UPDATES: {
            // Update settings map 1:1 to the [Updates] snapshot section. Replay it (same
            // path as the full reset), but leave the "Check for Updates" mode (the master
            // on/off toggle) alone — like a HUD's visibility in the resetHuds() path, the
            // master state is preserved here; full "Reset all settings" disables it instead.
            UpdateChecker::UpdateMode mode = UpdateChecker::getInstance().getMode();
            SettingsManager::getInstance().resetGlobalSectionsToFactoryDefaults(
                HudManager::getInstance(), {"Updates"});
            UpdateChecker::getInstance().setMode(mode);
            break;
        }
        case TAB_FMX:
            resetHuds({"FmxHud"});
            break;
        case TAB_STATS:
            resetHuds({"StatsHud"});
            break;
        case TAB_RIDERS:
            // Clear all tracked riders
            TrackedRidersManager::getInstance().clearAll();
            break;
        case TAB_DIRECTOR: {
            // Director maps 1:1 to the [Director] snapshot section. Replay it but leave
            // the master enable alone (like Updates' check toggle); a full "Reset all
            // settings" disables it instead.
            bool wasEnabled = DirectorManager::getInstance().isEnabled();
            SettingsManager::getInstance().resetGlobalSectionsToFactoryDefaults(
                HudManager::getInstance(), {"Director"});
            DirectorManager::getInstance().setEnabled(wasEnabled);
            break;
        }
        default:
            DEBUG_WARN_F("Unknown tab index for reset: %d", m_activeTab);
            break;
    }

    // Update settings display
    rebuildRenderData();

    // Deferred: persisted on leave-track (auto-save) or via the Save button.
    SettingsManager::getInstance().markDirty();
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

void SettingsHud::handleCheckboxClick(const ClickRegion& region) {
    if (!region.isRequired) {
        auto* bitfield = std::get_if<uint32_t*>(&region.targetPointer);
        if (bitfield && *bitfield && region.targetHud) {
            uint32_t oldValue = **bitfield;
            // For multi-bit flags (like COL_SECTORS), use set/clear instead of XOR
            // If all bits are set, clear them; otherwise set all
            if ((oldValue & region.flagBit) == region.flagBit) {
                **bitfield &= ~region.flagBit;  // Clear all flag bits
            } else {
                **bitfield |= region.flagBit;   // Set all flag bits
            }
            uint32_t newValue = **bitfield;
            region.targetHud->setDataDirty();
            rebuildRenderData();
            DEBUG_INFO_F("Data checkbox toggled: bit 0x%X, bitfield 0x%X -> 0x%X",
                region.flagBit, oldValue, newValue);
        }
    }
}

// Note: gap toggle/scope/reference click handlers moved to settings_tab_standings.cpp

void SettingsHud::handleHudToggleClick(const ClickRegion& region) {
    if (!region.targetHud) return;

    // Toggle the FOCUSED surface's instance: companion window edits companion
    // visibility, game window the game visibility.
    bool companion = InputManager::getInstance().getActiveSurface() == InputManager::Surface::Companion;
    if (companion) {
        region.targetHud->setCompanionVisible(!region.targetHud->getCompanionVisible());
        DEBUG_INFO_F("HUD companion visibility toggled: %s",
            region.targetHud->getCompanionVisible() ? "visible" : "hidden");
    } else {
        region.targetHud->setVisible(!region.targetHud->isVisible());
        DEBUG_INFO_F("HUD visibility toggled: %s", region.targetHud->isVisible() ? "visible" : "hidden");
    }
    rebuildRenderData();
}

void SettingsHud::handleTitleToggleClick(const ClickRegion& region) {
    if (!region.targetHud) return;

    region.targetHud->setShowTitle(!region.targetHud->getShowTitle());
    rebuildRenderData();
    DEBUG_INFO_F("HUD title toggled: %s", region.targetHud->getShowTitle() ? "shown" : "hidden");
}

void SettingsHud::handleOpacityClick(const ClickRegion& region, bool increase) {
    if (!region.targetHud) return;

    float currentOpacity = region.targetHud->getBackgroundOpacity();
    float newOpacity = applyAcceleratedStep(currentOpacity, 0.01f, increase);
    newOpacity = std::max(0.0f, std::min(1.0f, newOpacity));
    region.targetHud->setBackgroundOpacity(newOpacity);
    rebuildRenderData();
    DEBUG_INFO_F("HUD background opacity %s to %d%%",
        increase ? "increased" : "decreased", static_cast<int>(std::round(newOpacity * 100.0f)));
}

void SettingsHud::handleScaleClick(const ClickRegion& region, bool increase) {
    if (!region.targetHud) return;

    float currentScale = region.targetHud->getScale();
    float newScale = applyAcceleratedStep(currentScale, 0.01f, increase);
    newScale = std::max(0.1f, std::min(3.0f, newScale));
    region.targetHud->setScale(newScale);
    rebuildRenderData();
    DEBUG_INFO_F("HUD scale %s to %.2f", increase ? "increased" : "decreased", newScale);
}

// Note: handleRowCountClick, handleLapLogRowCountClick, handleMap*, handleRadar*
// moved to respective tab files (settings_tab_standings.cpp, settings_tab_lap_log.cpp,
// settings_tab_map.cpp, settings_tab_radar.cpp)

void SettingsHud::handleDisplayModeClick(const ClickRegion& region, bool increase) {
    auto* displayMode = std::get_if<uint8_t*>(&region.targetPointer);
    if (!displayMode || !*displayMode || !region.targetHud) return;

    // DisplayMode enum values are the same for PerformanceHud and TelemetryHud (0=Graphs, 1=Values, 2=Both)
    uint8_t currentMode = **displayMode;
    uint8_t newMode;

    if (increase) {
        // Cycle forward: GRAPHS(0) -> VALUES(1) -> BOTH(2) -> GRAPHS(0)
        switch (currentMode) {
            case 0: newMode = 1; break;  // GRAPHS -> VALUES
            case 1: newMode = 2; break;  // VALUES -> BOTH
            case 2: newMode = 0; break;  // BOTH -> GRAPHS
            default: newMode = 2; break; // Default to BOTH
        }
    } else {
        // Cycle backward: GRAPHS(0) -> BOTH(2) -> VALUES(1) -> GRAPHS(0)
        switch (currentMode) {
            case 0: newMode = 2; break;  // GRAPHS -> BOTH
            case 1: newMode = 0; break;  // VALUES -> GRAPHS
            case 2: newMode = 1; break;  // BOTH -> VALUES
            default: newMode = 2; break; // Default to BOTH
        }
    }

    **displayMode = newMode;
    region.targetHud->setDataDirty();
    rebuildRenderData();

    const char* modeNames[] = {"Graphs", "Numbers", "Both"};
    DEBUG_INFO_F("Display mode changed to %s", modeNames[newMode]);
}

// Note: handlePitboardShowModeClick moved to settings_tab_pitboard.cpp
// Note: handleColorCycleClick moved to settings_tab_appearance.cpp

void SettingsHud::handleTabClick(const ClickRegion& region) {
    m_activeTab = region.tabIndex;
    // Persist the focused tab so reopening the menu lands here next session. Deferred like
    // every other setting - markSettingsDirty() only sets the flag; the write happens on the
    // next leave-track flush (or the shutdown backstop / Save button), never on-track.
    markSettingsDirty();
    rebuildRenderData();
    DEBUG_INFO_F("Switched to tab %d", m_activeTab);
}

void SettingsHud::handleCloseButtonClick() {
    hide();
    DEBUG_INFO("Settings menu closed via close button");
}

