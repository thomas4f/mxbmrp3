// ============================================================================
// hud/settings_hud.cpp
// Settings interface for configuring which columns/rows are visible in HUDs
// ============================================================================
#include "settings_hud.h"
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

// Mark settings dirty after a settings-panel edit. Unconditional (not gated on auto-save) so
// the Save button reflects unsaved changes even in manual mode; the write itself is deferred
// (see SettingsManager::markDirty) so it never spikes a gameplay frame.
void SettingsHud::markSettingsDirty() {
    SettingsManager::getInstance().markDirty();
}

// Helper template to cycle enum values forward or backward with wrap-around
// EnumT must be an enum class with sequential values starting from 0
template<typename EnumT>
EnumT cycleEnum(EnumT current, int enumCount, bool forward) {
    int val = static_cast<int>(current);
    if (forward) {
        val = (val + 1) % enumCount;
    } else {
        val = (val - 1 + enumCount) % enumCount;
    }
    return static_cast<EnumT>(val);
}

// Hold-to-repeat: determines which click region types support auto-repeat when held
bool SettingsHud::isRepeatableRegionType(ClickRegion::Type type) {
    switch (type) {
        // Value cycling controls (all _UP/_DOWN pairs)
        case ClickRegion::STEPPED_UP:
        case ClickRegion::STEPPED_DOWN:
        case ClickRegion::CYCLE_UP:
        case ClickRegion::CYCLE_DOWN:
        case ClickRegion::COPY_TARGET_UP:
        case ClickRegion::COPY_TARGET_DOWN:
        case ClickRegion::TEXTURE_VARIANT_UP:
        case ClickRegion::TEXTURE_VARIANT_DOWN:
        case ClickRegion::HUD_THEME_UP:
        case ClickRegion::HUD_THEME_DOWN:
        case ClickRegion::BACKGROUND_OPACITY_UP:
        case ClickRegion::BACKGROUND_OPACITY_DOWN:
        case ClickRegion::SCALE_UP:
        case ClickRegion::SCALE_DOWN:
        case ClickRegion::ROW_COUNT_UP:
        case ClickRegion::ROW_COUNT_DOWN:
        case ClickRegion::MAP_RANGE_UP:
        case ClickRegion::MAP_RANGE_DOWN:
        case ClickRegion::MAP_RIDER_SHAPE_UP:
        case ClickRegion::MAP_RIDER_SHAPE_DOWN:
        case ClickRegion::MAP_OUTLINE_UP:
        case ClickRegion::MAP_OUTLINE_DOWN:
        case ClickRegion::RADAR_PROXIMITY_SHAPE_UP:
        case ClickRegion::RADAR_PROXIMITY_SHAPE_DOWN:
        case ClickRegion::RADAR_RIDER_SHAPE_UP:
        case ClickRegion::RADAR_RIDER_SHAPE_DOWN:
        case ClickRegion::GAPBAR_ICON_UP:
        case ClickRegion::GAPBAR_ICON_DOWN:
        case ClickRegion::COLOR_CYCLE_PREV:
        case ClickRegion::COLOR_CYCLE_NEXT:
        case ClickRegion::FONT_CATEGORY_PREV:
        case ClickRegion::FONT_CATEGORY_NEXT:
        case ClickRegion::PROFILE_CYCLE_DOWN:
        case ClickRegion::PROFILE_CYCLE_UP:
        case ClickRegion::RUMBLE_CONTROLLER_UP:
        case ClickRegion::RUMBLE_CONTROLLER_DOWN:
        case ClickRegion::UPDATE_CHANNEL_UP:
        case ClickRegion::UPDATE_CHANNEL_DOWN:
        case ClickRegion::RIDER_COLOR_PREV:
        case ClickRegion::RIDER_COLOR_NEXT:
        case ClickRegion::RIDER_SHAPE_PREV:
        case ClickRegion::RIDER_SHAPE_NEXT:
        case ClickRegion::SERVER_PAGE_PREV:
        case ClickRegion::SERVER_PAGE_NEXT:
        case ClickRegion::TRACKED_PAGE_PREV:
        case ClickRegion::TRACKED_PAGE_NEXT:
        case ClickRegion::EVENT_LOG_ICONS_TOGGLE:
#if GAME_HAS_HTTP_SERVER
        case ClickRegion::WEB_SERVER_PORT_DOWN:
        case ClickRegion::WEB_SERVER_PORT_UP:
#endif
        // Helmet overlay controls
        case ClickRegion::HELMET_UPPER_TEX_DOWN:
        case ClickRegion::HELMET_UPPER_TEX_UP:
        case ClickRegion::HELMET_LOWER_TEX_DOWN:
        case ClickRegion::HELMET_LOWER_TEX_UP:
        case ClickRegion::HELMET_UPPER_OFFSET_DOWN:
        case ClickRegion::HELMET_UPPER_OFFSET_UP:
        case ClickRegion::HELMET_LOWER_OFFSET_DOWN:
        case ClickRegion::HELMET_LOWER_OFFSET_UP:
        case ClickRegion::HELMET_TILT_DOWN:
        case ClickRegion::HELMET_TILT_UP:
        case ClickRegion::HELMET_VIBRATION_DOWN:
        case ClickRegion::HELMET_VIBRATION_UP:
        case ClickRegion::HELMET_VIB_SENS_DOWN:
        case ClickRegion::HELMET_VIB_SENS_UP:
        case ClickRegion::HELMET_ZOOM_DOWN:
        case ClickRegion::HELMET_ZOOM_UP:
        case ClickRegion::HELMET_VISOR_MODE_DOWN:
        case ClickRegion::HELMET_VISOR_MODE_UP:
        case ClickRegion::HELMET_VISOR_TINT_COLOR_DOWN:
        case ClickRegion::HELMET_VISOR_TINT_COLOR_UP:
        case ClickRegion::HELMET_VISOR_TINT_OPACITY_DOWN:
        case ClickRegion::HELMET_VISOR_TINT_OPACITY_UP:
        // Director tab steppers + cycle controls (consistent with the other tabs).
        case ClickRegion::DIRECTOR_MINSHOT_UP:
        case ClickRegion::DIRECTOR_MINSHOT_DOWN:
        case ClickRegion::DIRECTOR_MAXSHOT_UP:
        case ClickRegion::DIRECTOR_MAXSHOT_DOWN:
        case ClickRegion::DIRECTOR_BATTLEGAP_UP:
        case ClickRegion::DIRECTOR_BATTLEGAP_DOWN:
        case ClickRegion::DIRECTOR_BATTLEMAXPOS_UP:
        case ClickRegion::DIRECTOR_BATTLEMAXPOS_DOWN:
        case ClickRegion::DIRECTOR_RESUME_UP:
        case ClickRegion::DIRECTOR_RESUME_DOWN:
        case ClickRegion::DIRECTOR_VARIETY_UP:
        case ClickRegion::DIRECTOR_VARIETY_DOWN:
        case ClickRegion::DIRECTOR_HOLD_UP:
        case ClickRegion::DIRECTOR_HOLD_DOWN:
        case ClickRegion::DIRECTOR_CAM_FENDER_UP:
        case ClickRegion::DIRECTOR_CAM_FENDER_DOWN:
        case ClickRegion::DIRECTOR_CAM_HELMET_UP:
        case ClickRegion::DIRECTOR_CAM_HELMET_DOWN:
            return true;
        default:
            return false;
    }
}

SettingsHud::SettingsHud(IdealLapHud* idealLap, LapLogHud* lapLog, FriendsHud* friends, SessionChartsHud* sessionCharts,
                         StandingsHud* standings,
                         PerformanceHud* performance,
                         TelemetryHud* telemetry,
                         TimeWidget* time, PositionWidget* position, LapWidget* lap, SessionHud* session, MapHud* mapHud, RadarHud* radarHud, SpeedWidget* speed, GearWidget* gear, CrashWidget* crash, SpeedoWidget* speedo, TachoWidget* tacho, TimingHud* timing, GapBarHud* gapBar, BarsWidget* bars, VersionWidget* version, NoticesHud* notices, PitboardHud* pitboard, RecordsHud* records, FuelWidget* fuel, PointerWidget* pointer, RumbleHud* rumble, GamepadWidget* gamepad, LeanWidget* lean, GForceWidget* gforce, CompassWidget* compass,
                         FmxHud* fmxHud,
                         StatsHud* statsHud,
                         EventLogHud* eventLog,
                         ClockWidget* clock,
                         HelmetOverlayHud* helmetOverlay,
                         SettingsButtonWidget* settingsButton
#if GAME_HAS_TYRE_TEMP
                         , TyreTempWidget* tyreTemp
#endif
#if GAME_HAS_ECU
                         , EcuWidget* ecu
#endif
                         )
    : m_idealLap(idealLap),
      m_lapLog(lapLog),
      m_friends(friends),
      m_sessionCharts(sessionCharts),
      m_standings(standings),
      m_performance(performance),
      m_telemetry(telemetry),
      m_time(time),
      m_position(position),
      m_lap(lap),
      m_session(session),
      m_mapHud(mapHud),
      m_radarHud(radarHud),
      m_speed(speed),
      m_gear(gear),
      m_crash(crash),
      m_speedo(speedo),
      m_tacho(tacho),
      m_timing(timing),
      m_gapBar(gapBar),
      m_bars(bars),
      m_version(version),
      m_notices(notices),
      m_pitboard(pitboard),
      m_records(records),
      m_fuel(fuel),
      m_pointer(pointer),
      m_settingsButton(settingsButton),
      m_rumble(rumble),
      m_helmetOverlay(helmetOverlay),
      m_gamepad(gamepad),
      m_lean(lean),
      m_gforce(gforce),
      m_compass(compass),
      m_clock(clock),
#if GAME_HAS_TYRE_TEMP
      m_tyreTemp(tyreTemp),
#endif
#if GAME_HAS_ECU
      m_ecu(ecu),
#endif
      m_fmxHud(fmxHud),
      m_statsHud(statsHud),
      m_eventLog(eventLog),
      m_bVisible(false),
      m_copyTargetProfile(-1),  // -1 = no target selected
      m_resetProfileConfirmed(false),
      m_resetAllConfirmed(false),
      m_cachedWindowWidth(0),
      m_cachedWindowHeight(0),
#if GAME_HAS_DISCORD
      m_cachedDiscordState(-1),
      m_cachedDiscordEnabled(false),
#endif
      m_activeTab(TAB_GENERAL),
      m_hoveredRegionIndex(-1),
      m_hoveredHotkeyRow(-1),
      m_hoveredHotkeyColumn(HotkeyColumn::NONE),
      m_hotkeyContentStartY(0.0f),
      m_hotkeyRowHeight(0.0f),
      m_hotkeyKeyboardX(0.0f),
      m_hotkeyControllerX(0.0f),
      m_hotkeyFieldCharWidth(0.0f),
      m_hoveredTrackedRiderIndex(-1),
      m_trackedRidersStartY(0.0f),
      m_trackedRidersCellHeight(0.0f),
      m_trackedRidersCellWidth(0.0f),
      m_trackedRidersStartX(0.0f),
      m_trackedRidersPerRow(0),
      m_serverPlayersPage(0),
      m_trackedRidersPage(0),
      m_wasUpdateCheckerOnCooldown(false),
      m_cachedUpdateCheckerStatus(-1),
      m_cachedUpdateDownloaderState(-1),
      m_holdRegionIndex(-1),
      m_holdRepeatCount(0),
      m_holdSavePending(false)
{
    // No caption on this panel -- see BaseHud::m_titleSupported.
    disableTitle();
    DEBUG_INFO("SettingsHud created");
    // The one panel of this kind, and the only thing that makes a theme's
    // [card] settings-title-band / settings-content reachable. Without it this
    // panel inherits PanelKind::Hud and BOTH those keys parse, store and then
    // govern nothing -- while the menu silently follows the hud-* pair instead.
    // Found by perturbing every theme key and diffing the render: they were the
    // only two that moved no pixels for a reason other than scene coverage.
    m_panelKind = PanelKind::Settings;
    // THIS PANEL'S CONTENT IS CARDED, which it always was -- it just used to emit
    // the cards itself. Now that its body is declared to PanelBox, the pair says
    // the same thing to the engine: `settings-content` decides whether the cards
    // draw (contentCardKind reads the Settings family), and the engine draws one
    // per section of BOTH columns.
    m_bContentCard = true;
    m_bContentSections = true;
    // THE MENU IS ALWAYS CAPTIONED. m_bShowTitle is a per-HUD setting with a row in
    // this very panel; the panel itself has no such row and no section in the
    // profile, so its flag is whatever the base class left -- and the old code
    // never asked, drawing the caption with a bare addString. Going through
    // addPlanTitle asks, so the answer has to be stated rather than inherited.
    m_bShowTitle = true;
    // This panel's cards sit in two COLUMNS (tab groups left, section cards
    // right), so the fill complement is gutters plus per-column gaps rather than
    // full-width strips -- see NineSlice::cutFill. Budget: 3 gutters + 4 tab-column
    // gaps + a gap per content section + 1, comfortably under 24 on the busiest
    // tab today. Outrunning it degrades to the uniform stacked fill, not a hole.
    m_fillReserve = 24;
    setDraggable(true);

    // Pre-allocate vectors
    m_quads.reserve(1);
    m_strings.reserve(60);  // Less text now with tabs
    m_clickRegions.reserve(90);  // Sized for largest tab (TAB_RIDERS: 30 server + 48 tracked cells + pagination)

    // Start hidden
    hide();
}

void SettingsHud::show() {
    // vis-gate: the settings MENU renders only on the ACTIVE surface (special-
    // cased in HudManager::collectSurface); it has no companion instance, so
    // the game flag alone is correct here and in update()/rebuildLayout().
    if (m_bVisible) return;

    m_bVisible = true;

    // Re-measure on OPEN as well as on layout changes: a tab whose rows follow live
    // data (Riders lists the session's entries) can have changed while the menu was
    // closed, and none of that dirties the layout.
    invalidateTallestTab();

    // Rebuild UI
    rebuildRenderData();
}

// Dismiss a hovered row's what's-new band, and mark the settings dirty when that
// actually changed something.
//
// ONE OWNER for the pair, because the mark is the half that is easy to leave out
// and impossible to see: hovering a row touches nothing else in the panel, so
// without it the flag stays clear, flushIfDirty is a no-op at the next pit stop,
// and every marker the player dismissed is back on the next launch, forever. The
// test hook calls THIS rather than WhatsNew::dismissRow -- it used to call the
// latter, which meant the persistence test drove a path with no mark in it and
// went green against exactly the bug it exists to catch.
//
// Guarded on the return so the pointer crossing rows does not rewrite the settings
// file once per row.
void SettingsHud::dismissMarkedRow(const char* tooltipId) {
    if (WhatsNew::dismissRow(m_activeTab, tooltipId)) markSettingsDirty();
}

void SettingsHud::hide() {
    // ONLY ON A REAL CLOSE. The constructor ends with hide() to start the panel
    // hidden, and hide() dismisses the open tab -- so unguarded, every launch
    // dismissed TAB_GENERAL (m_activeTab's initial value) before the settings file
    // had even loaded. A file carrying whatsNewSeen then overwrote it on load and
    // hid the bug; a file WITHOUT that key (fresh install, or any 1.28 INI) kept it
    // and saved it, permanently spending the General tab's next marker on a player
    // who was never shown one. Latent today only because no marker names that tab.
    const bool wasVisible = m_bVisible;  // vis-gate: transition edge, not a render gate
    m_bVisible = false;
    // The tab that was open when the panel closed counts as seen, which the CLICK
    // path alone would miss: the menu reopens on the last-focused tab, so a player
    // who always lands on the same one never clicks it, and its "New" tag would
    // outlive every other. Dismissed on close rather than on open so the tag is
    // still there while they are looking at it.
    //
    // markSettingsDirty for the same reason the hover path does it: closing the menu
    // is not itself an edit, so nothing else would ever write this to disk.
    if (wasVisible && WhatsNew::dismissTab(m_activeTab)) markSettingsDirty();
    // ...and the Updates tab's "Update" tag, on the same edge and for the same
    // reason: the player has seen what it was pointing at. Keyed on the VERSION
    // (markUpdateTagSeen), so a newer release re-arms it without anything here
    // knowing that happened.
    if (wasVisible && m_activeTab == TAB_UPDATES &&
        UpdateChecker::getInstance().shouldShowUpdateTag()) {
        UpdateChecker::getInstance().markUpdateTagSeen();
        markSettingsDirty();
    }
    disarmResets();
    clearStrings();
    m_quads.clear();
    m_clickRegions.clear();
    m_steppedControls.clear();  // rebuilt in lockstep with the click regions
    m_cycleControls.clear();    // rebuilt in lockstep with the click regions
    m_holdRegionIndex = -1;  // Stop any hold-to-repeat in progress
    m_holdSavePending = false;
    setBounds(0, 0, 0, 0);  // Clear collision bounds to prevent blocking input
}

void SettingsHud::showUpdatesTab() {
    m_activeTab = TAB_UPDATES;
    setDataDirty();  // Force rebuild even if already visible
    show();
}

void SettingsHud::update() {
    if (!m_bVisible) return;  // vis-gate: menu is active-surface-only (see show())

    // Process dirty flag first (e.g., from showUpdatesTab() or external tab switch)
    if (isDataDirty()) {
        rebuildAndRecord();
        clearDataDirty();
    }

    // Refresh the Save button when the unsaved-changes state flips (a HUD dragged while the
    // panel is open, an auto-save/leave-track flush clearing it), so it lights up / greys out.
    bool dirtyNow = SettingsManager::getInstance().isDirty();
    if (dirtyNow != m_lastSettingsDirty) {
        m_lastSettingsDirty = dirtyNow;
        rebuildAndRecord();
    }

    // Check for window resize (need to rebuild click regions with new coordinates)
    const InputManager& input = InputManager::getInstance();
    int currentWidth = input.getWindowWidth();
    int currentHeight = input.getWindowHeight();

    if (currentWidth != m_cachedWindowWidth || currentHeight != m_cachedWindowHeight) {
        // Window resized - rebuild everything to update click regions
        m_cachedWindowWidth = currentWidth;
        m_cachedWindowHeight = currentHeight;
        rebuildAndRecord();
        DEBUG_INFO_F("SettingsHud rebuilt after window resize: %dx%d", currentWidth, currentHeight);
        return;  // Skip other processing this frame
    }

    // Periodic refresh for Stats tab (live session data: distance, time, speed, etc.)
    if (m_activeTab == TAB_STATS) {
        auto now = std::chrono::steady_clock::now();
        auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(now - m_lastStatsRefresh).count();
        if (elapsed >= 1000) {
            m_lastStatsRefresh = now;
            setDataDirty();
        }
    }

#if GAME_HAS_DISCORD
    // Check for Discord state changes (for live status updates in General tab)
    if (m_activeTab == TAB_GENERAL) {
        const DiscordManager& discord = DiscordManager::getInstance();
        int currentState = static_cast<int>(discord.getState());
        bool currentEnabled = discord.isEnabled();
        if (currentState != m_cachedDiscordState || currentEnabled != m_cachedDiscordEnabled) {
            m_cachedDiscordState = currentState;
            m_cachedDiscordEnabled = currentEnabled;
            setDataDirty();
        }
    }
#endif

    // Track hover state for button backgrounds. Copy the cursor and shift it into this
    // HUD's build space, so the click/hover regions line up when the menu is dragged to
    // a different spot on the companion surface (no-op in-game / when not diverged).
    CursorPosition cursor = input.getCursorPosition();
    mapCursorToHudSpace(cursor.x, cursor.y);
    if (cursor.isValid) {
        int newHoveredIndex = -1;
        for (size_t i = 0; i < m_clickRegions.size(); ++i) {
            const auto& region = m_clickRegions[i];
            if (isPointInRect(cursor.x, cursor.y, region.x, region.y, region.width, region.height)) {
                newHoveredIndex = static_cast<int>(i);
                break;
            }
        }
        if (newHoveredIndex != m_hoveredRegionIndex) {
            m_hoveredRegionIndex = newHoveredIndex;
            // Update tooltip ID for the hovered region
            if (newHoveredIndex >= 0 && newHoveredIndex < static_cast<int>(m_clickRegions.size())) {
                const ClickRegion& region = m_clickRegions[newHoveredIndex];
                // Use tooltipId from region if set (Phase 3), otherwise fall back to type-based lookup
                if (!region.tooltipId.empty()) {
                    m_hoveredTooltipId = region.tooltipId;
                    // Hovering a marked row is what dismisses its band: the player
                    // has the pointer on it, which is all the marker was asking for.
                    // Here because this is the ONE place a hovered row's identity is
                    // resolved -- doing it per row helper would be the same test in
                    // a dozen places, drifting.
                    //
                    dismissMarkedRow(region.tooltipId.c_str());
                } else {
                    const char* tooltipId = getTooltipIdForRegion(region.type, m_activeTab);
                    m_hoveredTooltipId = tooltipId ? tooltipId : "";
                }
            } else {
                m_hoveredTooltipId.clear();
            }
            rebuildAndRecord();  // Rebuild to update button backgrounds and tooltip
        }

        // For hotkeys tab, track row and column hover
        if (m_activeTab == TAB_HOTKEYS && m_hotkeyRowHeight > 0.0f) {
            int newHoveredRow = -1;
            HotkeyColumn newHoveredColumn = HotkeyColumn::NONE;

            // Apply offset to stored coordinates for comparison with cursor
            float contentStartY = m_hotkeyContentStartY + m_fOffsetY;
            float keyboardX = m_hotkeyKeyboardX + m_fOffsetX;
            float controllerX = m_hotkeyControllerX + m_fOffsetX;

            if (cursor.y >= contentStartY) {
                // Find which row the cursor is over using the recorded per-row tops.
                // This handles the half-row spacers between groups exactly (a purely
                // geometric reconstruction drifts past every gap), and leaves
                // newHoveredRow at -1 while the cursor is in a gap.
                for (size_t r = 0; r < m_hotkeyRowTops.size(); ++r) {
                    float rowTop = m_hotkeyRowTops[r] + m_fOffsetY;
                    if (cursor.y >= rowTop && cursor.y < rowTop + m_hotkeyRowHeight) {
                        newHoveredRow = static_cast<int>(r);
                        break;
                    }
                }

                // Check which column the cursor is in (only if on a valid row)
                if (newHoveredRow >= 0) {
                    constexpr int kbFieldWidth = 16;
                    constexpr int ctrlFieldWidth = 12;
                    float kbFieldEnd = keyboardX + m_hotkeyFieldCharWidth * (kbFieldWidth + 2);
                    float ctrlFieldEnd = controllerX + m_hotkeyFieldCharWidth * (ctrlFieldWidth + 2);

                    if (cursor.x >= keyboardX && cursor.x < kbFieldEnd) {
                        newHoveredColumn = HotkeyColumn::KEYBOARD;
                    } else if (cursor.x >= controllerX && cursor.x < ctrlFieldEnd) {
                        newHoveredColumn = HotkeyColumn::CONTROLLER;
                    }
                }
            }

            if (newHoveredRow != m_hoveredHotkeyRow || newHoveredColumn != m_hoveredHotkeyColumn) {
                m_hoveredHotkeyRow = newHoveredRow;
                m_hoveredHotkeyColumn = newHoveredColumn;
                rebuildAndRecord();
            }
        }

        // For riders tab, track which tracked rider cell is hovered
        if (m_activeTab == TAB_RIDERS && m_trackedRidersCellHeight > 0.0f && m_trackedRidersPerRow > 0) {
            // newHoveredRiderIndex: an outer `newHoveredIndex` (the hovered click
            // region) is still in scope here.
            int newHoveredRiderIndex = -1;

            // Apply offset to stored coordinates for comparison with cursor
            float ridersStartY = m_trackedRidersStartY + m_fOffsetY;
            float ridersStartX = m_trackedRidersStartX + m_fOffsetX;

            if (cursor.y >= ridersStartY && cursor.x >= ridersStartX) {
                float relativeY = cursor.y - ridersStartY;
                float relativeX = cursor.x - ridersStartX;

                int row = static_cast<int>(relativeY / m_trackedRidersCellHeight);
                int col = static_cast<int>(relativeX / m_trackedRidersCellWidth);

                if (col >= 0 && col < m_trackedRidersPerRow) {
                    newHoveredRiderIndex = row * m_trackedRidersPerRow + col;
                }
            }

            if (newHoveredRiderIndex != m_hoveredTrackedRiderIndex) {
                m_hoveredTrackedRiderIndex = newHoveredRiderIndex;
                rebuildAndRecord();
            }
        }
    }

    // Handle mouse input. Repeatable steppers (+/-) fire on press and then hold-repeat;
    // every other button/toggle fires on RELEASE, so the user can press and slide off to
    // abort. (See findClickRegionAt + m_leftPressArmed.)
    const auto& leftButton = input.getLeftButton();
    if (leftButton.isClicked()) {
        m_leftPressArmed = false;
        if (cursor.isValid) {
            int idx = findClickRegionAt(cursor.x, cursor.y);
            if (idx >= 0) {
                if (isRepeatableRegionType(m_clickRegions[idx].type)) {
                    // Stepper: fire immediately and start hold-to-repeat tracking.
                    m_holdRepeatCount = 0;  // handlers see 0 on the initial click
                    handleClick(cursor.x, cursor.y);
                    m_holdRegionIndex = idx;
                    m_holdRepeatCount = 0;
                    m_holdStartTime = std::chrono::steady_clock::now();
                    m_holdLastRepeat = m_holdStartTime;
                } else {
                    // Button/toggle: arm now, dispatch on release if still over this region.
                    m_leftPressArmed = true;
                    m_pressX = cursor.x;
                    m_pressY = cursor.y;
                }
            }
        }
    } else if (leftButton.isPressed && m_holdRegionIndex >= 0) {
        // Button still held - check for repeat firing
        auto now = std::chrono::steady_clock::now();
        auto holdDurationMs = std::chrono::duration_cast<std::chrono::milliseconds>(now - m_holdStartTime).count();

        // Initial delay before repeating starts (400ms)
        constexpr long long HOLD_INITIAL_DELAY_MS = 400;
        // Repeat interval starts at 200ms and accelerates down to 30ms
        constexpr long long HOLD_REPEAT_SLOW_MS = 200;
        const long long HOLD_REPEAT_FAST_MS = UiConfig::getInstance().getHoldRepeatFastMs();
        // Number of repeats before reaching max speed
        constexpr int HOLD_ACCEL_REPEATS = 15;

        if (holdDurationMs >= HOLD_INITIAL_DELAY_MS) {
            // Calculate current repeat interval (linear interpolation from slow to fast)
            float accelFactor = std::min(static_cast<float>(m_holdRepeatCount) / HOLD_ACCEL_REPEATS, 1.0f);
            long long repeatIntervalMs = static_cast<long long>(
                HOLD_REPEAT_SLOW_MS + accelFactor * (HOLD_REPEAT_FAST_MS - HOLD_REPEAT_SLOW_MS));

            auto timeSinceLastRepeat = std::chrono::duration_cast<std::chrono::milliseconds>(now - m_holdLastRepeat).count();
            if (timeSinceLastRepeat >= repeatIntervalMs) {
                // Verify cursor is still over the same region
                if (cursor.isValid && m_holdRegionIndex < static_cast<int>(m_clickRegions.size())) {
                    const auto& region = m_clickRegions[m_holdRegionIndex];
                    if (isPointInRect(cursor.x, cursor.y, region.x, region.y, region.width, region.height)) {
                        // Dispatch the held region directly (skip save - deferred until release)
                        dispatchRegion(region, /*skipSave=*/true);
                        m_holdSavePending = true;
                        m_holdLastRepeat = now;
                        m_holdRepeatCount++;
                    } else {
                        // Cursor moved off the region - stop repeating
                        m_holdRegionIndex = -1;
                    }
                } else {
                    m_holdRegionIndex = -1;
                }
            }
        }
    } else if (leftButton.isReleased()) {
        // Fire an armed button/toggle only if released over the SAME region it was pressed
        // on (so sliding off before releasing aborts the action).
        if (m_leftPressArmed && cursor.isValid) {
            int relIdx = findClickRegionAt(cursor.x, cursor.y);
            if (relIdx >= 0 && relIdx == findClickRegionAt(m_pressX, m_pressY)) {
                m_holdRepeatCount = 0;
                handleClick(cursor.x, cursor.y);
            }
        }
        m_leftPressArmed = false;

        // Restart web server after port hold-cycle ends (debounced)
#if GAME_HAS_HTTP_SERVER
        if (m_holdRegionIndex >= 0) {
            auto holdType = m_clickRegions[m_holdRegionIndex].type;
            if (holdType == ClickRegion::WEB_SERVER_PORT_DOWN ||
                holdType == ClickRegion::WEB_SERVER_PORT_UP) {
                auto& server = HttpServer::getInstance();
                if (server.isEnabled() && !server.isRunning()) {
                    server.start();
                    setDataDirty();
                }
            }
        }
#endif
        // Button released - stop hold tracking
        m_holdRegionIndex = -1;
    }

    // Deferred auto-save: mark dirty once when hold ends (instead of every repeat tick)
    if (m_holdRegionIndex < 0 && m_holdSavePending) {
        m_holdSavePending = false;
        markSettingsDirty();
    }

    // Handle right-click for shape cycling (TAB_RIDERS only)
    if (input.getRightButton().isClicked()) {
        if (cursor.isValid && m_activeTab == TAB_RIDERS) {
            handleRightClick(cursor.x, cursor.y);
        }
    }

    // Handle hotkey capture mode
    HotkeyManager& hotkeyMgr = HotkeyManager::getInstance();
    if (hotkeyMgr.isCapturing()) {
        // Check for ESC to cancel capture
        if ((GetAsyncKeyState(VK_ESCAPE) & 0x8000) != 0) {
            hotkeyMgr.cancelCapture();
            rebuildAndRecord();
        }
        // Rebuild every frame during capture to show real-time modifier feedback
        else {
            rebuildAndRecord();
        }
    }
    // Check if capture completed (must be outside isCapturing block - capture ends same frame)
    if (hotkeyMgr.wasCaptureCompleted()) {
        rebuildAndRecord();
        // Mark dirty after a binding change (persisted on leave-track).
        markSettingsDirty();
    }

    // Check for Updates tab state changes (status, downloader state, cooldown)
    if (m_activeTab == TAB_UPDATES) {
        UpdateChecker& checker = UpdateChecker::getInstance();
        UpdateDownloader& downloader = UpdateDownloader::getInstance();

        // Check cooldown expiry
        bool wasOnCooldown = m_wasUpdateCheckerOnCooldown;
        bool isOnCooldown = checker.isOnCooldown();
        m_wasUpdateCheckerOnCooldown = isOnCooldown;

        // Check status changes (CHECKING → UPDATE_AVAILABLE, etc.)
        int currentStatus = static_cast<int>(checker.getStatus());
        int currentDownloaderState = static_cast<int>(downloader.getState());

        bool statusChanged = (currentStatus != m_cachedUpdateCheckerStatus);
        bool downloaderChanged = (currentDownloaderState != m_cachedUpdateDownloaderState);
        bool cooldownExpired = (wasOnCooldown && !isOnCooldown);

        if (statusChanged || downloaderChanged || cooldownExpired) {
            m_cachedUpdateCheckerStatus = currentStatus;
            m_cachedUpdateDownloaderState = currentDownloaderState;
            setDataDirty();
        }
    }

    // Check if layout dirty (e.g., scale changed)
    if (isLayoutDirty()) {
        rebuildLayout();
        clearLayoutDirty();
    }
}

void SettingsHud::rebuildLayout() {
    // Rebuild everything for layout changes (dragging, scale, etc.)
    // Given the complexity of tabs and dynamic controls, full rebuild is simplest
    //
    // NO invalidateTallestTab() HERE. This runs on every frame of a drag -- a drag
    // is a position change and arrives as a layout dirty exactly like a scale change
    // does -- and dropping the measurement made the next rebuild re-lay all
    // twenty-eight tabs, per frame, to re-derive a number that a drag cannot move.
    // The measurement is keyed on what actually moves it now (SettingsHud::TallestKey:
    // the row metrics and the theme generation), so a scale or theme change still
    // re-measures and a drag does not.
    if (m_bVisible) {  // vis-gate: menu is active-surface-only (see show())
        rebuildRenderData();
    }
}

const char* SettingsHud::getTabName(int tabIndex) const {
    const TabDescriptor* tabDesc = findTabDescriptor(tabIndex);
    return (tabDesc && tabDesc->name) ? tabDesc->name : "Unknown";
}

bool SettingsHud::isTabAvailable(int tabId) const {
    if (tabId < 0 || tabId >= TAB_COUNT) return false;
    const TabDescriptor* tabDesc = findTabDescriptor(tabId);
    if (!tabDesc) return false;
    // Game-gated tabs (Records/FMX/Friends): selectable only when their backing HUD is
    // registered on this build. The tab-list render loop and the persisted-tab restore
    // both route through here, so they can't drift.
    if (tabDesc->gameGated && !(tabDesc->hud && tabDesc->hud(*this))) return false;
    return true;
}

const char* SettingsHud::getActiveTabName() const {
    return getTabName(m_activeTab);
}

void SettingsHud::setActiveTabByName(const char* name) {
    if (!name || !name[0]) return;
    for (int t = 0; t < TAB_COUNT; ++t) {
        if (std::strcmp(getTabName(t), name) == 0) {
            // Ignore a tab that isn't available on this build (e.g. a saved "FMX" loaded on
            // karts) - keep the constructor default rather than landing on an empty tab.
            //
            // REBUILT, like handleTabClick does: the two are the same event arriving by
            // different routes (a click, or a restore / a test hook), and a tab change
            // is the largest layout change this panel has. Without it the panel keeps
            // drawing the PREVIOUS tab until something else dirties it -- which is what
            // made settings_fit_test report one number for all twenty-eight tabs, the
            // same stale reading twenty-eight times, and pass.
            if (isTabAvailable(t) && t != m_activeTab) {
                m_activeTab = t;
                disarmResets();
                rebuildRenderData();
            }
            return;
        }
    }
    // Unknown / renamed tab: keep the default. (getTabName never returns "Unknown" for a
    // real id, so a stored "Unknown" also falls through harmlessly.)
}

bool SettingsHud::isPointInRect(float x, float y, float rectX, float rectY, float width, float height) const {
    // Apply offset to rectangle position for dragging support
    float offsetRectX = rectX;
    float offsetRectY = rectY;
    applyOffset(offsetRectX, offsetRectY);

    return x >= offsetRectX && x <= (offsetRectX + width) &&
           y >= offsetRectY && y <= (offsetRectY + height);
}

const char* SettingsHud::getTooltipIdForRegion(ClickRegion::Type type, int activeTab) {
    // Map click region types to tooltip IDs
    // Common controls (used across all tabs)
    switch (type) {
        case ClickRegion::HUD_TOGGLE:
            return "common.visible";
        case ClickRegion::TITLE_TOGGLE:
            return "common.title";
        case ClickRegion::TEXTURE_VARIANT_UP:
        case ClickRegion::TEXTURE_VARIANT_DOWN:
            return "common.texture";
        case ClickRegion::BACKGROUND_OPACITY_UP:
        case ClickRegion::BACKGROUND_OPACITY_DOWN:
            return "common.opacity";
        case ClickRegion::SCALE_UP:
        case ClickRegion::SCALE_DOWN:
            return "common.scale";
        default:
            break;
    }

    // Tab-specific controls
    switch (activeTab) {
        case TAB_STANDINGS:
            switch (type) {
                case ClickRegion::ROW_COUNT_UP:
                case ClickRegion::ROW_COUNT_DOWN:
                    return "standings.rows";
                case ClickRegion::GAP_REFERENCE_TOGGLE:
                case ClickRegion::GAP_REFERENCE_BACK:
                    return "standings.gap_reference";
                case ClickRegion::FILTER_DNS_TOGGLE:
                    return "standings.filter_dns";
                case ClickRegion::HEADERS_TOGGLE:
                    return "standings.headers";
                default:
                    break;
            }
            break;

        case TAB_MAP:
            switch (type) {
                case ClickRegion::MAP_ROTATION_TOGGLE:
                    return "map.rotation";
                case ClickRegion::MAP_OUTLINE_UP:
                case ClickRegion::MAP_OUTLINE_DOWN:
                    return "map.outline";
                case ClickRegion::MAP_RANGE_UP:
                case ClickRegion::MAP_RANGE_DOWN:
                    return "map.range";
                case ClickRegion::MAP_RIDER_SHAPE_UP:
                case ClickRegion::MAP_RIDER_SHAPE_DOWN:
                    return "map.rider_shape";
                case ClickRegion::MAP_DETAIL_ADAPTIVE_TOGGLE:
                    return "map.detail_adaptive";
                default:
                    break;
            }
            break;

        case TAB_RADAR:
            switch (type) {
                case ClickRegion::RADAR_RIDER_SHAPE_UP:
                case ClickRegion::RADAR_RIDER_SHAPE_DOWN:
                    return "radar.rider_shape";
                default:
                    break;
            }
            break;

        case TAB_LAP_LOG:
            switch (type) {
                case ClickRegion::LAP_LOG_GAP_ROW_TOGGLE:
                    return "lap_log.gap_row";
                default:
                    break;
            }
            break;

        case TAB_FRIENDS:
            switch (type) {
                case ClickRegion::FRIENDS_HEADERS_TOGGLE:
                    return "friends.headers";
                case ClickRegion::FRIENDS_SELF_TOGGLE:
                    return "friends.self";
                default:
                    break;
            }
            break;

        case TAB_TIMING:
            switch (type) {
                case ClickRegion::TIMING_TIME_TOGGLE:
                    return "timing.time";
                case ClickRegion::TIMING_GAP_PB_TOGGLE:
                    return "timing.gap_pb";
                case ClickRegion::TIMING_GAP_IDEAL_TOGGLE:
                    return "timing.gap_ideal";
                case ClickRegion::TIMING_GAP_OVERALL_TOGGLE:
                    return "timing.gap_overall";
                case ClickRegion::TIMING_GAP_ALLTIME_TOGGLE:
                    return "timing.gap_alltime";
                case ClickRegion::TIMING_GAP_RECORD_TOGGLE:
                    return "timing.gap_record";
                case ClickRegion::TIMING_GAP_LASTLAP_TOGGLE:
                    return "timing.gap_lastlap";
                default:
                    break;
            }
            break;

        case TAB_GAP_BAR:
            switch (type) {
                case ClickRegion::GAPBAR_ICON_UP:
                case ClickRegion::GAPBAR_ICON_DOWN:
                    return "gap_bar.icon";
                case ClickRegion::GAPBAR_GAP_TEXT_TOGGLE:
                    return "gap_bar.show_gap";
                case ClickRegion::GAPBAR_GAP_BAR_TOGGLE:
                    return "gap_bar.show_gap_bar";
                default:
                    break;
            }
            break;

        case TAB_GENERAL:
            switch (type) {
                case ClickRegion::RUMBLE_CONTROLLER_UP:
                case ClickRegion::RUMBLE_CONTROLLER_DOWN:
                    return "general.controller";
                default:
                    break;
            }
            break;

        case TAB_RUMBLE:
            switch (type) {
                case ClickRegion::RUMBLE_TOGGLE:
                    return "rumble.enabled";
                case ClickRegion::RUMBLE_SUSP_SPLIT_TOGGLE:
                case ClickRegion::RUMBLE_LOCKUP_SPLIT_TOGGLE:
                    return "rumble.split";
                default:
                    break;
            }
            break;

        default:
            break;
    }

    return "";  // No tooltip for this region
}
