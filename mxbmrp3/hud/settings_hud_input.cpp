// ============================================================================
// hud/settings_hud_input.cpp
// SettingsHud user-interaction handling: click hit-testing and dispatch
// (findClickRegionAt / handleClick / dispatchRegion / handleRightClick) and the
// individual control handlers (checkbox / toggle / opacity / scale / display-
// mode / tab / close). Split out of settings_hud.cpp, which keeps menu
// construction (rebuildRenderData) and lifecycle; the reset operations are in
// settings_hud_reset.cpp. Per-tab layout lives in hud/settings/settings_tab_*.cpp.
// ============================================================================
#include "settings_hud.h"
#include "../core/stats_manager.h"
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
#include "../core/achievement_manager.h"
#include "../core/system_messages.h"
#include "achievement_widget.h"
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
#include <cstdio>
#include <cstring>
#include <algorithm>
#include <cmath>

using namespace PluginConstants;

int SettingsHud::findClickRegionAt(float x, float y) const {
    // An open dropdown list's entries first: they come last, over the rows they cover.
    for (int i = std::max(m_dropdown.firstRegion, 0); m_dropdown.firstRegion >= 0 &&
         i < static_cast<int>(m_clickRegions.size()); ++i) {
        const auto& r = m_clickRegions[i];
        if (isPointInRect(x, y, r.x, r.y, r.width, r.height)) return i;
    }
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
    // One click per frame, on the region findClickRegionAt picks (TOOLTIP_ROW is hover-only).
    const int idx = findClickRegionAt(mouseX, mouseY);
    if (idx >= 0) dispatchRegion(m_clickRegions[idx]);
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
    // Link rows (General's Docs/Discussion/overlay, About's three thanks links)
    // open a browser and change no setting: settings/settings_links.cpp.
    if (handleLinkClick(region)) return;

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
        case ClickRegion::SLIDER: case ClickRegion::DROPDOWN: case ClickRegion::DROPDOWN_OPTION:
            handleControlRegion(region);   // settings_controls.cpp
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
        // Achievement-toast master: the same region from the tab list's checkbox
        // and the tab's own row, so it lives here for the spotter's reason.
        case ClickRegion::ACHIEVEMENTS_TOASTS_TOGGLE:
            {
                AchievementManager& ach = AchievementManager::getInstance();
                ach.setToastsEnabled(!ach.isToastsEnabled());
                if (auto* w = HudManager::getInstance().getAchievementWidget()) w->setDataDirty();
                rebuildRenderData();
            }
            break;
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
        // SPEED_UNIT, FUEL_UNIT,
        // GRID_SNAP, UPDATE_CHECK, COPY_*, RESET_*
        // Clock widget toggles (used from Widgets tab and General tab)
        case ClickRegion::CLOCK_FORMAT_TOGGLE:
            if (m_clock) {
                m_clock->setFormat24h(!m_clock->getFormat24h());
                rebuildRenderData();
            }
            break;
        case ClickRegion::RESET_TAB_BUTTON:
            // ARM, then PERFORM, like General's Reset pair: a tab's whole tuning
            // is gone on the second click and there is no undo.
            if (!m_resetTabConfirmed) {
                disarmResets();
                m_resetTabConfirmed = true;
                rebuildRenderData();
                return;   // nothing changed yet: no save
            }
            m_resetTabConfirmed = false;
            resetCurrentTab();
            DEBUG_INFO_F("Tab %d reset to defaults", m_activeTab);
            {
                // Which profile took it is the part the tab does not show: a HUD
                // tab resets the ACTIVE profile's copy only (resetHud), a global
                // tab has one copy.
                const TabDescriptor* desc = findTabDescriptor(m_activeTab);
                SystemMessages::Toast t;
                snprintf(t.title, sizeof(t.title), "%s reset", getTabName(m_activeTab));
                if (desc && desc->resetHud) {
                    snprintf(t.detail, sizeof(t.detail), "Defaults restored in %s",
                             ProfileManager::getProfileName(ProfileManager::getInstance().getActiveProfile()));
                } else {
                    snprintf(t.detail, sizeof(t.detail), "Defaults restored");
                }
                snprintf(t.icon, sizeof(t.icon), "arrow-rotate-left");
                t.key = SystemMessages::KEY_TAB_RESET;
                SystemMessages::getInstance().post(t);
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
    // Right-click clears a hotkey binding (Hotkeys)
    for (const auto& region : m_clickRegions) {
        if (isPointInRect(mouseX, mouseY, region.x, region.y, region.width, region.height)) {
            if (handleRightClickTabHotkeys(region)) return;
        }
    }
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

void SettingsHud::handleTitleToggleClick(const ClickRegion& region) {
    if (!region.targetHud) return;

    region.targetHud->setShowTitle(!region.targetHud->getShowTitle());
    rebuildRenderData();
    DEBUG_INFO_F("HUD title toggled: %s", region.targetHud->getShowTitle() ? "shown" : "hidden");
}

// Tab-specific handlers live in their settings_tab_*.cpp files.

// The sidebar's counterpart: a tab whose news bands its row (no room for the
// tag) is dismissed by hovering it, like any banded row. Gated on the band so
// hovering a TAGGED tab leaves its tag alone -- that one clears on open.
void SettingsHud::dismissMarkedTab(int tabIndex) {
    if (WhatsNew::tabHighlightsRow(tabIndex) && WhatsNew::dismissTab(tabIndex)) markSettingsDirty();
}

void SettingsHud::handleTabClick(const ClickRegion& region) {
    // A More row names its group's first tab; it opens that group's More page.
    if (region.flagBit == GROUP_HEADER) {
        m_moreGroupRow = groupRowOf(region.tabIndex);
        m_activeTab = TAB_MORE;
    } else {
        m_activeTab = region.tabIndex;
    }
    recordTabOpened(m_activeTab);   // Grand Tour
    // OPENING a marked tab clears its "New" tag -- the tag's only claim is that
    // there is something here you have not looked at, and now you have. The rows
    // keep their bands until hovered; finding the row is a separate thing from
    // knowing the tab is worth opening. See settings/whats_new.h.
    WhatsNew::dismissTab(m_activeTab);
    disarmResets();
    // A capture belongs to the tab that armed it: left armed, a chat channel
    // field keeps swallowing the keyboard with no field on screen, and Enter
    // commits the invisible text to a channel. Pinned by hotkey_capture_test.cpp.
    HotkeyManager::getInstance().cancelCapture();
    m_textField = TextField::NONE;
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

int SettingsHud::testOnceCycleCount() const {
    int n = 0;
    for (const auto& r : m_clickRegions) {
        if (r.type == ClickRegion::CYCLE_UP && !isRepeatableRegion(r)) ++n;
    }
    return n;
}

int SettingsHud::testUnnamedListCount() const {
    int n = 0;
    for (const auto& c : m_cycleControls) {
        if (c.count >= 3 && !c.nameOf) ++n;
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
        // Not a setting at all: it trades the whole achievement ladder and the
        // lifetime counters under it for a prestige level.
        case CR::ACHIEVEMENTS_PRESTIGE:
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
        case CR::COPY_BUTTON:
        // Leaves the menu waiting for a keypress that never comes.
        case CR::HOTKEY_KEYBOARD_BIND:
        case CR::HOTKEY_CONTROLLER_BIND:
        // Reach outside the process: a browser, the update server, a minute-long
        // sweep that deliberately tanks the frame rate.
        case CR::OPEN_LINK_DOCS:
        case CR::OPEN_LINK_COMMUNITY:
        case CR::OPEN_LINK_KOFI:
        case CR::OPEN_LINK_GITHUB:
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

bool SettingsHud::testClickResetTab(int clicks) {
    // Two clicks by default: the first arms "Confirm?", the second resets. The
    // arm rebuilds the panel, so the region is looked up again per click.
    for (int i = 0; i < clicks; ++i) {
        bool found = false;
        for (const auto& r : m_clickRegions) {
            if (r.type != ClickRegion::RESET_TAB_BUTTON) continue;
            handleClick(r.x + r.width * 0.5f, r.y + r.height * 0.5f);
            found = true;
            break;
        }
        if (!found) return false;
    }
    return true;
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
