// ============================================================================
// hud/settings_hud_input.cpp
// SettingsHud user-interaction handling: click hit-testing and dispatch
// (findClickRegionAt / handleClick / dispatchRegion / handleRightClick) and the
// individual control handlers (checkbox / toggle / opacity / scale / display-
// mode / tab / close). Split out of settings_hud.cpp, which keeps menu
// construction (rebuildRenderData) and lifecycle; the reset operations are in
// settings_hud_reset.cpp. Per-tab layout lives in hud/settings/settings_tab_*.cpp.
// ============================================================================
// file-budget: 1100 click dispatch for every settings tab; shrinks as tabs move to SteppedControl
#include "settings_hud.h"
#include "clock_widget.h"
#include "version_widget.h"
#include "pitboard_hud.h"
#include "gamepad_widget.h"
#include "settings/whats_new.h"
#include "settings/settings_layout.h"
#include "telemetry_hud.h"
#include "rumble_hud.h"
#include "helmet_overlay_hud.h"
#include "fmx_hud.h"
#include "stats_hud.h"
#include "settings_button_widget.h"
#include "tacho_widget.h"      // cycleGaugesPack reaches both gauge widgets
#include "speedo_widget.h"
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
#include "../core/spotter_manager.h"
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
    // Try the active tab's handler first (implemented in settings_tab_*.cpp,
    // routed via the descriptor registry; null click = common handlers only)
    bool handled = false;
    if (const TabDescriptor* tabDesc = findTabDescriptor(m_activeTab); tabDesc && tabDesc->click) {
        handled = (this->*(tabDesc->click))(region);
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

        // Shared data-driven stepped-value controls (see SteppedControl): the
        // region's steppedIndex selects the descriptor registered at layout time.
        case ClickRegion::STEPPED_UP:
        case ClickRegion::STEPPED_DOWN:
            applySteppedControl(region, region.type == ClickRegion::STEPPED_UP);
            break;

        // Shared data-driven mod-N cycle controls (see CycleControl): the
        // region's cycleIndex selects the descriptor registered at layout time.
        case ClickRegion::CYCLE_UP:
        case ClickRegion::CYCLE_DOWN:
            applyCycleControl(region, region.type == ClickRegion::CYCLE_UP);
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
        // Spotter spoken-audio master. Common (not tab-scoped) because the
        // tab list's row checkbox emits this same region from any tab.
        case ClickRegion::SPOTTER_ENABLED_TOGGLE:
            {
                SpotterManager& spotter = SpotterManager::getInstance();
                spotter.setEnabled(!spotter.isEnabled());
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
        case ClickRegion::DIRECTOR_MAXSHOT_DOWN: {
            // Off sits one step below the low end of the range. The setter only treats an
            // explicit 0 as Off (so a hand-edited out-of-range INI value can't mean it),
            // which leaves both ends of the jump to the steppers.
            DirectorManager& d = DirectorManager::getInstance();
            int v = d.getMaxShotSec();
            d.setMaxShotSec(v <= DirectorManager::MAX_SHOT_LO ? 0 : v - 1);
            rebuildRenderData();
            break;
        }
        case ClickRegion::DIRECTOR_MAXSHOT_UP: {
            // ...and back up out of Off, to the meaningful minimum.
            DirectorManager& d = DirectorManager::getInstance();
            int v = d.getMaxShotSec();
            d.setMaxShotSec(v <= 0 ? DirectorManager::MAX_SHOT_LO : v + 1);
            rebuildRenderData();
            break;
        }
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
            // DirectorWidget is an ordinary positioned widget, so its on/off decouples
            // per surface like every other HUD's.
            toggleHudOnActiveSurface(HudManager::getInstance().getDirectorWidget());
            rebuildRenderData();
            break;
        case ClickRegion::HELMET_OVERLAY_TOGGLE:
            if (m_helmetOverlay) {
                // Visibility gate only — doesn't touch individual enable flags
                // (same pattern as WIDGETS_TOGGLE).
                //
                // setVisible(), NOT toggleHudOnActiveSurface(): the helmet never
                // renders on the companion (BaseHud::rendersOnCompanion), so the game
                // flag is its only visibility and editing a companion one would create
                // a control with no effect. This is the one per-HUD toggle where
                // reaching for the surface-aware helper would be wrong.
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
        case ClickRegion::GAMEPAD_PACK_UP:
        case ClickRegion::GAMEPAD_PACK_DOWN:
            if (region.targetHud) {
                cycleGamepadPack(region.type == ClickRegion::GAMEPAD_PACK_UP);
                rebuildRenderData();
            }
            break;
        case ClickRegion::PITBOARD_PACK_UP:
        case ClickRegion::PITBOARD_PACK_DOWN:
            if (region.targetHud) {
                cyclePitboardPack(region.type == ClickRegion::PITBOARD_PACK_UP);
                rebuildRenderData();
            }
            break;
        case ClickRegion::GAUGES_PACK_UP:
        case ClickRegion::GAUGES_PACK_DOWN:
            // Through the region's targetHud, unlike the two above: the tacho and
            // the speedo each hold their own selection, so the click has to say
            // which row it came from.
            if (region.targetHud) {
                cycleGaugesPack(region.targetHud,
                                region.type == ClickRegion::GAUGES_PACK_UP);
                rebuildRenderData();
            }
            break;
        case ClickRegion::HUD_THEME_UP:
        case ClickRegion::HUD_THEME_DOWN:
            if (region.targetHud) {
                cycleHudThemeOverride(region.targetHud,
                                      region.type == ClickRegion::HUD_THEME_UP);
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
        // Note: ROW_COUNT, MAP_*, RADAR_* handlers moved to tab files
        // (Performance/Telemetry display style is a shared CYCLE control now.)

        // Profile cycle controls are in sidebar, must work from ALL tabs
        case ClickRegion::PROFILE_CYCLE_UP:
            {
                ProfileType nextProfile = ProfileManager::getNextProfile(
                    ProfileManager::getInstance().getActiveProfile());
                SettingsManager::getInstance().switchProfile(HudManager::getInstance(), nextProfile);
                rebuildRenderData();
            }
            return;  // No save here - switchProfile marks settings dirty; the deferred save flushes it
        case ClickRegion::PROFILE_CYCLE_DOWN:
            {
                ProfileType prevProfile = ProfileManager::getPreviousProfile(
                    ProfileManager::getInstance().getActiveProfile());
                SettingsManager::getInstance().switchProfile(HudManager::getInstance(), prevProfile);
                rebuildRenderData();
            }
            return;  // No save here - switchProfile marks settings dirty; the deferred save flushes it
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
                // OPEN ABOUT, AND KEEP COUNTING. This used to branch: with an update
                // available it jumped to the Updates tab and returned before the
                // counter, so the easter egg was unreachable for anyone who had an
                // update pending -- and the destination changed under the player
                // depending on state they could not see. The update notice lives on
                // the Updates row's own tag now, so this button has one job and one
                // destination.
                //
                // No early return: navigation and the counter both happen on every
                // click. Clicks two through five land while About is already open
                // (the footer is drawn on every tab), so the sequence still completes
                // -- setting the tab again is idempotent.
                m_activeTab = TAB_ABOUT;

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
                        return;   // hide() already tore the panel down; don't rebuild
                    }
                }
                rebuildRenderData();
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


// Shared handler for STEPPED_UP/STEPPED_DOWN: apply the registered descriptor's
// step (with the usual hold-to-repeat acceleration) and mark the target HUD +
// this menu dirty - the exact archetype the per-control enum pairs used to
// hand-roll. Only calls setDataDirty (never rebuildRenderData) so the region
// reference and descriptor vector stay valid for the caller.
void SettingsHud::applySteppedControl(const ClickRegion& region, bool increase) {
    if (region.steppedIndex < 0 ||
        region.steppedIndex >= static_cast<int>(m_steppedControls.size())) {
        DEBUG_WARN_F("Stepped region with invalid descriptor index: %d", region.steppedIndex);
        return;
    }
    const SteppedControl& control = m_steppedControls[region.steppedIndex];
    if (control.valid && !control.valid()) {
        // The state this descriptor bound to at layout time is no longer the
        // active target (e.g. the per-bike rumble profile changed under an open
        // menu - the pointers would edit the PREVIOUS bike's profile). Swallow
        // the click and mark the layout dirty so the next frame rebuilds the
        // controls against the right target.
        DEBUG_INFO("Stepped control target changed since layout; click ignored");
        setDataDirty();
        return;
    }
    switch (control.kind) {
        case SteppedControl::Kind::WRAP_INT:
            if (control.intValue) {
                *control.intValue = applyAcceleratedWrap(
                    *control.intValue, control.step, control.lo, control.hi, increase);
            }
            break;
        case SteppedControl::Kind::CLAMP_INT:
            if (control.intValue) {
                *control.intValue = applyAcceleratedClamp(
                    *control.intValue, control.step, control.lo, control.hi, increase);
            }
            break;
        case SteppedControl::Kind::FIXED_INT:
            if (control.intValue) {
                // Fixed integer step with deliberately NO hold acceleration (matches
                // the old plain ++/-- count handlers), clamped to [lo, hi].
                int next = *control.intValue + (increase ? control.step : -control.step);
                if (next < control.lo) next = control.lo;
                if (next > control.hi) next = control.hi;
                *control.intValue = next;
            }
            break;
        case SteppedControl::Kind::STEP_FLOAT:
            if (control.floatValue) {
                float next = applyAcceleratedStep(*control.floatValue, control.fstep, increase);
                // Clamp toward the pressed direction only (matches the old
                // per-control handlers, which bounded UP at max and DOWN at min).
                if (increase) { if (next > control.fhi) next = control.fhi; }
                else          { if (next < control.flo) next = control.flo; }
                *control.floatValue = next;
            }
            break;
        case SteppedControl::Kind::PERCENT_FLOAT:
            if (control.floatValue) {
                // Verbatim rumble-strength semantics (the old adjustEffectStrength):
                // accelerated 1% step, clamp, THEN round to hundredths. Deliberately
                // not STEP_FLOAT - its snap-to-accelerated-grid would change the
                // value sequences under hold acceleration.
                float step = control.fstep * getHoldStepMultiplier();
                float value = *control.floatValue;
                if (increase) {
                    value = std::round(std::min(value + step, control.fhi) * 100.0f) / 100.0f;
                } else {
                    value = std::round(std::max(value - step, control.flo) * 100.0f) / 100.0f;
                }
                *control.floatValue = value;
            }
            break;
        case SteppedControl::Kind::FIXED_FLOAT:
            if (control.floatValue) {
                // Fixed step with deliberately NO hold acceleration (matches the old
                // rumble Min/Max input steppers). The lower bound can be linked to a
                // live value (a rumble effect's Max input clamps at its current Min).
                if (increase) {
                    *control.floatValue = std::min(*control.floatValue + control.fstep, control.fhi);
                } else {
                    float lo = control.loLink ? *control.loLink : control.flo;
                    *control.floatValue = std::max(*control.floatValue - control.fstep, lo);
                }
            }
            break;
    }
    if (control.postStep) control.postStep();
    if (control.dirtyHud) control.dirtyHud->setDataDirty();
    setDataDirty();
}

// Shared handler for CYCLE_UP/CYCLE_DOWN: step the registered descriptor's
// 0-based state index forward/backward mod count and mark the target HUD +
// this menu dirty - the archetype the per-control enum pairs (label/color/mode
// cycles) used to hand-roll. Deliberately NO hold acceleration: a held cycle
// repeats at the base cadence with step 1, exactly like the old handlers
// (which ignored the hold tier). Only calls setDataDirty (never
// rebuildRenderData) so the region reference and descriptor vector stay valid
// for the caller.
void SettingsHud::applyCycleControl(const ClickRegion& region, bool forward) {
    if (region.cycleIndex < 0 ||
        region.cycleIndex >= static_cast<int>(m_cycleControls.size())) {
        DEBUG_WARN_F("Cycle region with invalid descriptor index: %d", region.cycleIndex);
        return;
    }
    const CycleControl& control = m_cycleControls[region.cycleIndex];
    if (control.get && control.set && control.count > 0) {
        const int current = control.get();
        const int next = forward ? (current + 1) % control.count
                                 : (current + control.count - 1) % control.count;
        control.set(next);
    }
    if (control.postStep) control.postStep();
    if (control.dirtyHud) control.dirtyHud->setDataDirty();
    setDataDirty();
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

// Toggle the FOCUSED surface's instance: on the companion window this edits the
// companion visibility, in game the game visibility.
//
// EVERY per-HUD on/off must come through here. A handler that calls setVisible()
// directly silently edits the GAME surface no matter which window the click landed
// in, so on the companion the HUD stays on screen and the checkbox reports the
// other surface's state -- which is exactly what the helmet toggle did, and what
// the Director widget's row did until this was factored out.
void SettingsHud::toggleHudOnActiveSurface(BaseHud* hud) {
    if (!hud) return;
    bool companion = InputManager::getInstance().getActiveSurface() == InputManager::Surface::Companion;
    if (companion) {
        hud->setCompanionVisible(!hud->getCompanionVisible());
        DEBUG_INFO_F("HUD companion visibility toggled: %s",
            hud->getCompanionVisible() ? "visible" : "hidden");
    } else {
        hud->setVisible(!hud->isVisible());
        DEBUG_INFO_F("HUD visibility toggled: %s", hud->isVisible() ? "visible" : "hidden");
    }
}

void SettingsHud::handleHudToggleClick(const ClickRegion& region) {
    if (!region.targetHud) return;
    toggleHudOnActiveSurface(region.targetHud);
    rebuildRenderData();
}

// Step a HUD's theme override through [Default, None, <each installed theme>].
// The list is built fresh each click rather than cached: themes are discovered once
// at startup, but the CURRENT value may name a theme that is no longer installed,
// and that has to resolve to a position in the list before stepping.
// Step the gamepad widget through OFF plus the installed packs, by name.
//
// THE "OFF" ENTRY IS LOAD-BEARING, and leaving it out shipped a bug: nothing else
// in the plugin toggles the widget's show-background-texture flag. For every other HUD it is
// driven by the texture cycle's own Off entry (setTextureVariant(0) is what clears
// it), so replacing this widget's texture cycle with a pack cycle and dropping Off
// removed the ONLY control that could turn the pad art back on. A user whose flag
// was already false -- e.g. they had Texture on Off under the previous build, which
// persisted showBackgroundTexture=0 -- got a black panel with the buttons floating
// on it and no way back. Reported in-game; reproduced with showBackgroundTexture=0.
//
// So the cycle is Off, then each pack, exactly as cycleTextureVariant's was.
void SettingsHud::cycleGamepadPack(bool forward) {
    const auto& packs = AssetManager::getInstance().getGamepads();
    if (packs.empty()) return;

    GamepadWidget& hud = HudManager::getInstance().getGamepadWidget();

    // PACKS ONLY -- no Off entry. The pad artwork IS this widget; without it the
    // buttons, sticks and triggers hang in mid-air on an empty panel. See
    // BaseHud::m_textureRequired, which also makes a stale showBackgroundTexture=0
    // heal itself rather than stranding someone in that state.
    //
    // Step from the pack actually IN USE, not from the stored name: if the stored
    // name names a pack this install does not have, the widget is already drawing
    // the shipped default, so stepping from the missing name would jump somewhere
    // unrelated to what is on screen.
    int index = 0;
    const GamepadAsset* active = hud.activePack();
    for (size_t i = 0; i < packs.size(); ++i) {
        if (active && packs[i].name == active->name) { index = static_cast<int>(i); break; }
    }

    const int count = static_cast<int>(packs.size());
    index += forward ? 1 : -1;
    if (index < 0) index = count - 1;
    if (index >= count) index = 0;

    hud.setGamepadPack(packs[static_cast<size_t>(index)].name);
    hud.setDataDirty();

    // Pads differ in aspect ratio (the shipped two are 750x630 and 806x599), so the
    // panel changes SIZE here and one parked against an edge can grow off-screen --
    // the same reason the theme cycle below asks for revalidation.
    HudManager::getInstance().requestPositionValidation();
}

// Step the pitboard through the installed board packs, by name. Same shape as
// cycleGamepadPack above, and no Off entry for the same reason: the board artwork
// is the HUD, and without it the rows sit on an empty panel.
void SettingsHud::cyclePitboardPack(bool forward) {
    const auto& packs = AssetManager::getInstance().getPitboards();
    if (packs.empty()) return;

    PitboardHud& hud = HudManager::getInstance().getPitboardHud();
    int index = 0;
    const PitboardAsset* active = hud.activePack();
    for (size_t i = 0; i < packs.size(); ++i) {
        if (active && packs[i].name == active->name) { index = static_cast<int>(i); break; }
    }

    const int count = static_cast<int>(packs.size());
    index += forward ? 1 : -1;
    if (index < 0) index = count - 1;
    if (index >= count) index = 0;

    hud.setPitboardPack(packs[static_cast<size_t>(index)].name);
    hud.setDataDirty();

    // Boards differ in aspect, so the panel changes SIZE here and one parked
    // against an edge can grow off-screen.
    HudManager::getInstance().requestPositionValidation();
}

void SettingsHud::cycleGaugesPack(BaseHud* hud, bool forward) {
    const auto& packs = AssetManager::getInstance().getGauges();
    if (packs.empty() || !hud) return;

    // Two widgets, two unrelated classes, one accessor pair -- see
    // activeGaugesDisplayName in settings_layout.cpp for why the cast lives at the
    // point of use rather than behind a base-class virtual.
    TachoWidget* tacho = dynamic_cast<TachoWidget*>(hud);
    SpeedoWidget* speedo = tacho ? nullptr : dynamic_cast<SpeedoWidget*>(hud);
    if (!tacho && !speedo) return;

    const GaugesAsset* active = tacho ? tacho->activePack() : speedo->activePack();
    int index = 0;
    for (size_t i = 0; i < packs.size(); ++i) {
        if (active && packs[i].name == active->name) { index = static_cast<int>(i); break; }
    }

    const int count = static_cast<int>(packs.size());
    index += forward ? 1 : -1;
    if (index < 0) index = count - 1;
    if (index >= count) index = 0;

    const std::string& picked = packs[static_cast<size_t>(index)].name;
    if (tacho) tacho->setGaugesPack(picked); else speedo->setGaugesPack(picked);
    hud->setDataDirty();

    // No requestPositionValidation() here, unlike the pit board's cycle: a dial is
    // drawn as a circle at the widget's own size, so switching packs cannot change
    // the panel's dimensions and cannot push a parked gauge off-screen.
}

void SettingsHud::cycleHudThemeOverride(BaseHud* hud, bool forward) {
    const auto& themes = AssetManager::getInstance().getThemes();
    const int count = static_cast<int>(themes.size()) + 2;   // Default + None + themes

    const std::string& cur = hud->getThemeOverride();
    int index = 0;                                            // Default
    if (cur == BaseHud::THEME_NONE) {
        index = 1;
    } else if (!cur.empty()) {
        for (size_t i = 0; i < themes.size(); ++i) {
            if (themes[i].name == cur) { index = static_cast<int>(i) + 2; break; }
        }
        // Unknown name leaves index at 0 (Default), which is also what it renders
        // as -- so stepping from it goes somewhere predictable.
    }

    index += forward ? 1 : -1;
    if (index < 0) index = count - 1;
    if (index >= count) index = 0;

    if (index == 0)      hud->setThemeOverride("");
    else if (index == 1) hud->setThemeOverride(BaseHud::THEME_NONE);
    else                 hud->setThemeOverride(themes[static_cast<size_t>(index) - 2].name);

    // A theme RESIZES the panel, so a HUD parked flush against an edge can grow off the
    // display -- and nothing pulls it back, because validateAllHudPositions() only runs
    // on a cursor or window transition. The GLOBAL theme cycle has carried this call and
    // a paragraph explaining it since it shipped; the per-HUD override steps exactly the
    // same geometry from nearly every HUD tab and did not.
    HudManager::getInstance().requestPositionValidation();
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
// Note: the Performance/Telemetry display style is a shared CYCLE control now
// (see applyCycleControl); the old handleDisplayModeClick is gone.

// Note: handlePitboardShowModeClick moved to settings_tab_pitboard.cpp
// Note: handleColorCycleClick moved to settings_tab_appearance.cpp

void SettingsHud::handleTabClick(const ClickRegion& region) {
    m_activeTab = region.tabIndex;
    // OPENING a marked tab clears its "New" tag -- the tag's only claim is that
    // there is something here you have not looked at, and now you have. The rows
    // keep their bands until hovered; finding the row is a separate thing from
    // knowing the tab is worth opening. See settings/whats_new.h.
    WhatsNew::dismissTab(m_activeTab);
    disarmResets();
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


#if defined(MXBMRP3_TEST_BUILD)
// Headless click seam - see the declaration comment in settings_hud.h.
int SettingsHud::testSteppedRegionCount(bool up) const {
    const auto want = up ? ClickRegion::STEPPED_UP : ClickRegion::STEPPED_DOWN;
    int n = 0;
    for (const auto& r : m_clickRegions) {
        if (r.type == want) ++n;
    }
    return n;
}

bool SettingsHud::testClickStepped(int index, bool up, int holdRepeats) {
    const auto want = up ? ClickRegion::STEPPED_UP : ClickRegion::STEPPED_DOWN;
    int n = 0;
    for (const auto& r : m_clickRegions) {
        if (r.type != want) continue;
        if (n++ < index) continue;
        // Copy the center out first: dispatch can dirty the layout, and a rebuild
        // would invalidate the reference mid-call.
        const float cx = r.x + r.width * 0.5f;
        const float cy = r.y + r.height * 0.5f;
        m_holdRepeatCount = holdRepeats;
        handleClick(cx, cy);
        m_holdRepeatCount = 0;
        return true;
    }
    return false;
}

// Cycle-control twin of the stepped seam above (no hold tier - cycles never
// accelerate, so there is nothing to force).
int SettingsHud::testCycleRegionCount(bool up) const {
    const auto want = up ? ClickRegion::CYCLE_UP : ClickRegion::CYCLE_DOWN;
    int n = 0;
    for (const auto& r : m_clickRegions) {
        if (r.type == want) ++n;
    }
    return n;
}

void SettingsHud::testRegionSignature(char* out, int cap) const {
    if (!out || cap <= 0) return;
    out[0] = '\0';
    int used = 0;
    for (const auto& r : m_clickRegions) {
        char part[96];
        const int n = snprintf(part, sizeof(part), "%d:%s;",
            static_cast<int>(r.type), r.tooltipId.empty() ? "-" : r.tooltipId.c_str());
        if (n < 0 || used + n >= cap) break;
        memcpy(out + used, part, static_cast<size_t>(n));
        used += n;
        out[used] = '\0';
    }
    char tail[64];
    // typecount lets the test detect an ordinal SHIFT (a value inserted into
    // ClickRegion::Type) and say so, instead of drowning it in a golden diff.
    const int n = snprintf(tail, sizeof(tail), "typecount=%d;strings=%d",
        static_cast<int>(ClickRegion::COUNT), static_cast<int>(m_strings.size()));
    if (n > 0 && used + n < cap) {
        memcpy(out + used, tail, static_cast<size_t>(n));
        out[used + n] = '\0';
    }
}

// Regions the perturbation sweep must NOT click. Everything else is a setting, so a
// control type added later is clicked by default: if that turns out to be unsafe the
// sweep breaks loudly, which is what keeps this list honest. The reverse default -
// an allow-list - would have every new control silently uncovered, which is exactly
// the failure this test exists to catch.
static bool isPerturbSafe(SettingsHud::ClickRegion::Type t) {
    using CR = SettingsHud::ClickRegion;
    switch (t) {
        // The controls under test, and the ones that reset far more than a tab.
        case CR::RESET_TAB_BUTTON:
        case CR::RESET_BUTTON:
        case CR::RESET_PROFILE_CHECKBOX:
        case CR::RESET_ALL_CHECKBOX:
        // Navigation and file I/O: they change no setting, and leaving the tab would
        // perturb one page while resetting another.
        case CR::TAB:
        case CR::CLOSE_BUTTON:
        case CR::SAVE_BUTTON:
        case CR::TOOLTIP_ROW:
        case CR::VERSION_CLICK:
        case CR::PROFILE_CYCLE_UP:
        case CR::PROFILE_CYCLE_DOWN:
        // Writes to ANOTHER profile, which no per-tab reset claims to undo.
        case CR::COPY_TARGET_UP:
        case CR::COPY_TARGET_DOWN:
        case CR::COPY_BUTTON:
        // Leaves the menu waiting for a keypress that never comes.
        case CR::HOTKEY_KEYBOARD_BIND:
        case CR::HOTKEY_CONTROLLER_BIND:
        // Reach outside the process: a browser, the update server, a minute-long
        // sweep that deliberately tanks the frame rate.
        case CR::OPEN_LINK_DOCS:
        case CR::OPEN_LINK_COMMUNITY:
        case CR::OPEN_LINK_KOFI:
        case CR::OPEN_LINK_OVERLAY:
        case CR::UPDATE_CHECK_NOW:
        case CR::UPDATE_INSTALL:
        case CR::PROBE_SWEEP:
            return false;
        default:
            return true;
    }
}

int SettingsHud::testPerturbActiveTab() {
    int clicked = 0;
    // ONE CLICK PER CONTROL, not per region. Every row helper emits its control
    // TWICE - the "<" and ">" arrows are separate regions - and for the two-state
    // rows both carry the same type, so clicking every region flipped each setting
    // there and back and perturbed nothing at all. The pair is always adjacent
    // within a row, so skipping the region after each click is enough, and it still
    // reaches each control of a multi-control row (a widget row's Visible, Opacity
    // and Scale are three pairs in a row).
    bool skipNext = false;
    // From the content pass only: the sidebar's per-tab checkboxes are click regions
    // on every tab, and they are not this tab's to reset.
    // By INDEX, re-reading the vector each time: a click can rebuild the layout (a
    // toggle that reveals a row), which invalidates any iterator or reference held
    // across it. Regions inserted below the cursor are still reached; one inserted
    // above is not, and that is an acceptable miss for a sweep whose job is breadth.
    for (size_t i = static_cast<size_t>(m_testContentRegionBegin); i < m_clickRegions.size(); ++i) {
        const ClickRegion& r = m_clickRegions[i];
        // A row-wide tooltip region marks a row boundary (it is emitted first by
        // every row helper), so a pending skip cannot leak into the next row.
        if (r.type == ClickRegion::TOOLTIP_ROW) { skipNext = false; continue; }
        if (!isPerturbSafe(r.type)) continue;
        if (skipNext) { skipNext = false; continue; }
        skipNext = true;
        const float cx = r.x + r.width * 0.5f;
        const float cy = r.y + r.height * 0.5f;
        handleClick(cx, cy);
        ++clicked;
    }
    return clicked;
}

bool SettingsHud::testClickResetTab() {
    for (const auto& r : m_clickRegions) {
        if (r.type != ClickRegion::RESET_TAB_BUTTON) continue;
        const float cx = r.x + r.width * 0.5f;
        const float cy = r.y + r.height * 0.5f;
        handleClick(cx, cy);
        return true;
    }
    return false;
}

bool SettingsHud::testClickDirectorHudVisible() {
    for (const auto& r : m_clickRegions) {
        if (r.type != ClickRegion::DIRECTOR_HUD_VISIBLE) continue;
        handleClick(r.x + r.width * 0.5f, r.y + r.height * 0.5f);
        return true;
    }
    return false;
}

bool SettingsHud::testClickCycle(int index, bool up) {
    const auto want = up ? ClickRegion::CYCLE_UP : ClickRegion::CYCLE_DOWN;
    int n = 0;
    for (const auto& r : m_clickRegions) {
        if (r.type != want) continue;
        if (n++ < index) continue;
        const float cx = r.x + r.width * 0.5f;
        const float cy = r.y + r.height * 0.5f;
        handleClick(cx, cy);
        return true;
    }
    return false;
}
#endif
