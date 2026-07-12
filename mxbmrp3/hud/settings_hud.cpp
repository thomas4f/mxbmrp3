// ============================================================================
// hud/settings_hud.cpp
// Settings interface for configuring which columns/rows are visible in HUDs
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
        case ClickRegion::COPY_TARGET_UP:
        case ClickRegion::COPY_TARGET_DOWN:
        case ClickRegion::TEXTURE_VARIANT_UP:
        case ClickRegion::TEXTURE_VARIANT_DOWN:
        case ClickRegion::BACKGROUND_OPACITY_UP:
        case ClickRegion::BACKGROUND_OPACITY_DOWN:
        case ClickRegion::SCALE_UP:
        case ClickRegion::SCALE_DOWN:
        case ClickRegion::ROW_COUNT_UP:
        case ClickRegion::ROW_COUNT_DOWN:
        case ClickRegion::LAP_LOG_ROW_COUNT_UP:
        case ClickRegion::LAP_LOG_ROW_COUNT_DOWN:
        case ClickRegion::FRIENDS_ROW_COUNT_UP:
        case ClickRegion::FRIENDS_ROW_COUNT_DOWN:
        case ClickRegion::FRIENDS_SHOW_MODE_UP:
        case ClickRegion::FRIENDS_SHOW_MODE_DOWN:
        case ClickRegion::LAP_LOG_ORDER_UP:
        case ClickRegion::LAP_LOG_ORDER_DOWN:
        case ClickRegion::SESSION_CHARTS_COLOR_MODE_UP:
        case ClickRegion::SESSION_CHARTS_COLOR_MODE_DOWN:
        case ClickRegion::SESSION_CHARTS_TOP_COUNT_UP:
        case ClickRegion::SESSION_CHARTS_TOP_COUNT_DOWN:
        case ClickRegion::SESSION_CHARTS_ROW_COUNT_UP:
        case ClickRegion::SESSION_CHARTS_ROW_COUNT_DOWN:
        case ClickRegion::MAP_COLORIZE_UP:
        case ClickRegion::MAP_COLORIZE_DOWN:
        case ClickRegion::MAP_TRACK_WIDTH_UP:
        case ClickRegion::MAP_TRACK_WIDTH_DOWN:
        case ClickRegion::MAP_LABEL_MODE_UP:
        case ClickRegion::MAP_LABEL_MODE_DOWN:
        case ClickRegion::MAP_RANGE_UP:
        case ClickRegion::MAP_RANGE_DOWN:
        case ClickRegion::MAP_RIDER_SHAPE_UP:
        case ClickRegion::MAP_RIDER_SHAPE_DOWN:
        case ClickRegion::MAP_MARKER_SCALE_UP:
        case ClickRegion::MAP_MARKER_SCALE_DOWN:
        case ClickRegion::MAP_DETAIL_UP:
        case ClickRegion::MAP_DETAIL_DOWN:
        case ClickRegion::RADAR_RANGE_UP:
        case ClickRegion::RADAR_RANGE_DOWN:
        case ClickRegion::RADAR_COLORIZE_UP:
        case ClickRegion::RADAR_COLORIZE_DOWN:
        case ClickRegion::RADAR_ALERT_DISTANCE_UP:
        case ClickRegion::RADAR_ALERT_DISTANCE_DOWN:
        case ClickRegion::RADAR_LABEL_MODE_UP:
        case ClickRegion::RADAR_LABEL_MODE_DOWN:
        case ClickRegion::RADAR_MODE_UP:
        case ClickRegion::RADAR_MODE_DOWN:
        case ClickRegion::RADAR_PROXIMITY_ARROWS_UP:
        case ClickRegion::RADAR_PROXIMITY_ARROWS_DOWN:
        case ClickRegion::RADAR_PROXIMITY_SHAPE_UP:
        case ClickRegion::RADAR_PROXIMITY_SHAPE_DOWN:
        case ClickRegion::RADAR_PROXIMITY_SCALE_UP:
        case ClickRegion::RADAR_PROXIMITY_SCALE_DOWN:
        case ClickRegion::RADAR_PROXIMITY_COLOR_UP:
        case ClickRegion::RADAR_PROXIMITY_COLOR_DOWN:
        case ClickRegion::RADAR_RIDER_SHAPE_UP:
        case ClickRegion::RADAR_RIDER_SHAPE_DOWN:
        case ClickRegion::RADAR_MARKER_SCALE_UP:
        case ClickRegion::RADAR_MARKER_SCALE_DOWN:
        case ClickRegion::DISPLAY_MODE_UP:
        case ClickRegion::DISPLAY_MODE_DOWN:
        case ClickRegion::RECORDS_COUNT_UP:
        case ClickRegion::RECORDS_COUNT_DOWN:
        case ClickRegion::RECORDS_PROVIDER_UP:
        case ClickRegion::RECORDS_PROVIDER_DOWN:
        case ClickRegion::PITBOARD_SHOW_MODE_UP:
        case ClickRegion::PITBOARD_SHOW_MODE_DOWN:
        case ClickRegion::PITBOARD_GAP_MODE_UP:
        case ClickRegion::PITBOARD_GAP_MODE_DOWN:
        case ClickRegion::TIMING_DISPLAY_MODE_UP:
        case ClickRegion::TIMING_DISPLAY_MODE_DOWN:
        case ClickRegion::TIMING_DURATION_UP:
        case ClickRegion::TIMING_DURATION_DOWN:
        case ClickRegion::GAPBAR_FREEZE_UP:
        case ClickRegion::GAPBAR_FREEZE_DOWN:
        case ClickRegion::GAPBAR_MARKER_MODE_UP:
        case ClickRegion::GAPBAR_MARKER_MODE_DOWN:
        case ClickRegion::GAPBAR_ICON_UP:
        case ClickRegion::GAPBAR_ICON_DOWN:
        case ClickRegion::GAPBAR_RANGE_UP:
        case ClickRegion::GAPBAR_RANGE_DOWN:
        case ClickRegion::GAPBAR_WIDTH_UP:
        case ClickRegion::GAPBAR_WIDTH_DOWN:
        case ClickRegion::GAPBAR_MARKER_SCALE_UP:
        case ClickRegion::GAPBAR_MARKER_SCALE_DOWN:
        case ClickRegion::GAPBAR_LABEL_MODE_UP:
        case ClickRegion::GAPBAR_LABEL_MODE_DOWN:
        case ClickRegion::GAPBAR_COLOR_MODE_UP:
        case ClickRegion::GAPBAR_COLOR_MODE_DOWN:
        case ClickRegion::COLOR_CYCLE_PREV:
        case ClickRegion::COLOR_CYCLE_NEXT:
        case ClickRegion::FONT_CATEGORY_PREV:
        case ClickRegion::FONT_CATEGORY_NEXT:
        case ClickRegion::PROFILE_CYCLE_DOWN:
        case ClickRegion::PROFILE_CYCLE_UP:
        case ClickRegion::RUMBLE_CONTROLLER_UP:
        case ClickRegion::RUMBLE_CONTROLLER_DOWN:
        case ClickRegion::RUMBLE_SUSP_LIGHT_DOWN:
        case ClickRegion::RUMBLE_SUSP_LIGHT_UP:
        case ClickRegion::RUMBLE_SUSP_HEAVY_DOWN:
        case ClickRegion::RUMBLE_SUSP_HEAVY_UP:
        case ClickRegion::RUMBLE_SUSP_MIN_UP:
        case ClickRegion::RUMBLE_SUSP_MIN_DOWN:
        case ClickRegion::RUMBLE_SUSP_MAX_UP:
        case ClickRegion::RUMBLE_SUSP_MAX_DOWN:
        case ClickRegion::RUMBLE_SUSP_FRONT_LIGHT_DOWN:
        case ClickRegion::RUMBLE_SUSP_FRONT_LIGHT_UP:
        case ClickRegion::RUMBLE_SUSP_FRONT_HEAVY_DOWN:
        case ClickRegion::RUMBLE_SUSP_FRONT_HEAVY_UP:
        case ClickRegion::RUMBLE_SUSP_FRONT_MIN_UP:
        case ClickRegion::RUMBLE_SUSP_FRONT_MIN_DOWN:
        case ClickRegion::RUMBLE_SUSP_FRONT_MAX_UP:
        case ClickRegion::RUMBLE_SUSP_FRONT_MAX_DOWN:
        case ClickRegion::RUMBLE_SUSP_REAR_LIGHT_DOWN:
        case ClickRegion::RUMBLE_SUSP_REAR_LIGHT_UP:
        case ClickRegion::RUMBLE_SUSP_REAR_HEAVY_DOWN:
        case ClickRegion::RUMBLE_SUSP_REAR_HEAVY_UP:
        case ClickRegion::RUMBLE_SUSP_REAR_MIN_UP:
        case ClickRegion::RUMBLE_SUSP_REAR_MIN_DOWN:
        case ClickRegion::RUMBLE_SUSP_REAR_MAX_UP:
        case ClickRegion::RUMBLE_SUSP_REAR_MAX_DOWN:
        case ClickRegion::RUMBLE_WHEEL_LIGHT_DOWN:
        case ClickRegion::RUMBLE_WHEEL_LIGHT_UP:
        case ClickRegion::RUMBLE_WHEEL_HEAVY_DOWN:
        case ClickRegion::RUMBLE_WHEEL_HEAVY_UP:
        case ClickRegion::RUMBLE_WHEEL_MIN_UP:
        case ClickRegion::RUMBLE_WHEEL_MIN_DOWN:
        case ClickRegion::RUMBLE_WHEEL_MAX_UP:
        case ClickRegion::RUMBLE_WHEEL_MAX_DOWN:
        case ClickRegion::RUMBLE_LOCKUP_LIGHT_DOWN:
        case ClickRegion::RUMBLE_LOCKUP_LIGHT_UP:
        case ClickRegion::RUMBLE_LOCKUP_HEAVY_DOWN:
        case ClickRegion::RUMBLE_LOCKUP_HEAVY_UP:
        case ClickRegion::RUMBLE_LOCKUP_MIN_UP:
        case ClickRegion::RUMBLE_LOCKUP_MIN_DOWN:
        case ClickRegion::RUMBLE_LOCKUP_MAX_UP:
        case ClickRegion::RUMBLE_LOCKUP_MAX_DOWN:
        case ClickRegion::RUMBLE_LOCKUP_FRONT_LIGHT_DOWN:
        case ClickRegion::RUMBLE_LOCKUP_FRONT_LIGHT_UP:
        case ClickRegion::RUMBLE_LOCKUP_FRONT_HEAVY_DOWN:
        case ClickRegion::RUMBLE_LOCKUP_FRONT_HEAVY_UP:
        case ClickRegion::RUMBLE_LOCKUP_FRONT_MIN_UP:
        case ClickRegion::RUMBLE_LOCKUP_FRONT_MIN_DOWN:
        case ClickRegion::RUMBLE_LOCKUP_FRONT_MAX_UP:
        case ClickRegion::RUMBLE_LOCKUP_FRONT_MAX_DOWN:
        case ClickRegion::RUMBLE_LOCKUP_REAR_LIGHT_DOWN:
        case ClickRegion::RUMBLE_LOCKUP_REAR_LIGHT_UP:
        case ClickRegion::RUMBLE_LOCKUP_REAR_HEAVY_DOWN:
        case ClickRegion::RUMBLE_LOCKUP_REAR_HEAVY_UP:
        case ClickRegion::RUMBLE_LOCKUP_REAR_MIN_UP:
        case ClickRegion::RUMBLE_LOCKUP_REAR_MIN_DOWN:
        case ClickRegion::RUMBLE_LOCKUP_REAR_MAX_UP:
        case ClickRegion::RUMBLE_LOCKUP_REAR_MAX_DOWN:
        case ClickRegion::RUMBLE_WHEELIE_LIGHT_DOWN:
        case ClickRegion::RUMBLE_WHEELIE_LIGHT_UP:
        case ClickRegion::RUMBLE_WHEELIE_HEAVY_DOWN:
        case ClickRegion::RUMBLE_WHEELIE_HEAVY_UP:
        case ClickRegion::RUMBLE_WHEELIE_MIN_UP:
        case ClickRegion::RUMBLE_WHEELIE_MIN_DOWN:
        case ClickRegion::RUMBLE_WHEELIE_MAX_UP:
        case ClickRegion::RUMBLE_WHEELIE_MAX_DOWN:
        case ClickRegion::RUMBLE_RPM_LIGHT_DOWN:
        case ClickRegion::RUMBLE_RPM_LIGHT_UP:
        case ClickRegion::RUMBLE_RPM_HEAVY_DOWN:
        case ClickRegion::RUMBLE_RPM_HEAVY_UP:
        case ClickRegion::RUMBLE_RPM_MIN_UP:
        case ClickRegion::RUMBLE_RPM_MIN_DOWN:
        case ClickRegion::RUMBLE_RPM_MAX_UP:
        case ClickRegion::RUMBLE_RPM_MAX_DOWN:
        case ClickRegion::RUMBLE_REVLIM_LIGHT_DOWN:
        case ClickRegion::RUMBLE_REVLIM_LIGHT_UP:
        case ClickRegion::RUMBLE_REVLIM_HEAVY_DOWN:
        case ClickRegion::RUMBLE_REVLIM_HEAVY_UP:
        case ClickRegion::RUMBLE_REVLIM_MIN_UP:
        case ClickRegion::RUMBLE_REVLIM_MIN_DOWN:
        case ClickRegion::RUMBLE_REVLIM_MAX_UP:
        case ClickRegion::RUMBLE_REVLIM_MAX_DOWN:
        case ClickRegion::RUMBLE_PITLIM_LIGHT_DOWN:
        case ClickRegion::RUMBLE_PITLIM_LIGHT_UP:
        case ClickRegion::RUMBLE_PITLIM_HEAVY_DOWN:
        case ClickRegion::RUMBLE_PITLIM_HEAVY_UP:
        case ClickRegion::RUMBLE_PITLIM_MIN_UP:
        case ClickRegion::RUMBLE_PITLIM_MIN_DOWN:
        case ClickRegion::RUMBLE_PITLIM_MAX_UP:
        case ClickRegion::RUMBLE_PITLIM_MAX_DOWN:
        case ClickRegion::RUMBLE_SLIDE_LIGHT_DOWN:
        case ClickRegion::RUMBLE_SLIDE_LIGHT_UP:
        case ClickRegion::RUMBLE_SLIDE_HEAVY_DOWN:
        case ClickRegion::RUMBLE_SLIDE_HEAVY_UP:
        case ClickRegion::RUMBLE_SLIDE_MIN_UP:
        case ClickRegion::RUMBLE_SLIDE_MIN_DOWN:
        case ClickRegion::RUMBLE_SLIDE_MAX_UP:
        case ClickRegion::RUMBLE_SLIDE_MAX_DOWN:
        case ClickRegion::RUMBLE_SURFACE_LIGHT_DOWN:
        case ClickRegion::RUMBLE_SURFACE_LIGHT_UP:
        case ClickRegion::RUMBLE_SURFACE_HEAVY_DOWN:
        case ClickRegion::RUMBLE_SURFACE_HEAVY_UP:
        case ClickRegion::RUMBLE_SURFACE_MIN_UP:
        case ClickRegion::RUMBLE_SURFACE_MIN_DOWN:
        case ClickRegion::RUMBLE_SURFACE_MAX_UP:
        case ClickRegion::RUMBLE_SURFACE_MAX_DOWN:
        case ClickRegion::RUMBLE_STEER_LIGHT_DOWN:
        case ClickRegion::RUMBLE_STEER_LIGHT_UP:
        case ClickRegion::RUMBLE_STEER_HEAVY_DOWN:
        case ClickRegion::RUMBLE_STEER_HEAVY_UP:
        case ClickRegion::RUMBLE_STEER_MIN_UP:
        case ClickRegion::RUMBLE_STEER_MIN_DOWN:
        case ClickRegion::RUMBLE_STEER_MAX_UP:
        case ClickRegion::RUMBLE_STEER_MAX_DOWN:
        case ClickRegion::UPDATE_CHANNEL_UP:
        case ClickRegion::UPDATE_CHANNEL_DOWN:
        case ClickRegion::FMX_CHAIN_ROWS_UP:
        case ClickRegion::FMX_CHAIN_ROWS_DOWN:
        case ClickRegion::STATS_VISIBILITY_UP:
        case ClickRegion::STATS_VISIBILITY_DOWN:
        case ClickRegion::RIDER_COLOR_PREV:
        case ClickRegion::RIDER_COLOR_NEXT:
        case ClickRegion::RIDER_SHAPE_PREV:
        case ClickRegion::RIDER_SHAPE_NEXT:
        case ClickRegion::SERVER_PAGE_PREV:
        case ClickRegion::SERVER_PAGE_NEXT:
        case ClickRegion::TRACKED_PAGE_PREV:
        case ClickRegion::TRACKED_PAGE_NEXT:
        case ClickRegion::NOTICES_DURATION_UP:
        case ClickRegion::NOTICES_DURATION_DOWN:
        case ClickRegion::EVENT_LOG_MODE_UP:
        case ClickRegion::EVENT_LOG_MODE_DOWN:
        case ClickRegion::EVENT_LOG_ORDER_UP:
        case ClickRegion::EVENT_LOG_ORDER_DOWN:
        case ClickRegion::EVENT_LOG_ROW_COUNT_UP:
        case ClickRegion::EVENT_LOG_ROW_COUNT_DOWN:
        case ClickRegion::EVENT_LOG_DURATION_UP:
        case ClickRegion::EVENT_LOG_DURATION_DOWN:
        case ClickRegion::EVENT_LOG_TIMESTAMP_UP:
        case ClickRegion::EVENT_LOG_TIMESTAMP_DOWN:
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

// Mode name lookup tables for debug output
static const char* getRiderColorModeName(int mode) {
    static const char* names[] = { "Uniform", "Brand", "Position" };
    return (mode >= 0 && mode < 3) ? names[mode] : "Unknown";
}

static const char* getLabelModeName(int mode) {
    static const char* names[] = { "None", "Position", "RaceNum", "Both" };
    return (mode >= 0 && mode < 4) ? names[mode] : "Unknown";
}

SettingsHud::SettingsHud(IdealLapHud* idealLap, LapLogHud* lapLog, FriendsHud* friends, SessionChartsHud* sessionCharts,
                         StandingsHud* standings,
                         PerformanceHud* performance,
                         TelemetryHud* telemetry,
                         TimeWidget* time, PositionWidget* position, LapWidget* lap, SessionHud* session, MapHud* mapHud, RadarHud* radarHud, SpeedWidget* speed, GearWidget* gear, SpeedoWidget* speedo, TachoWidget* tacho, TimingHud* timing, GapBarHud* gapBar, BarsWidget* bars, VersionWidget* version, NoticesHud* notices, PitboardHud* pitboard, RecordsHud* records, FuelWidget* fuel, PointerWidget* pointer, RumbleHud* rumble, GamepadWidget* gamepad, LeanWidget* lean, GForceWidget* gforce, CompassWidget* compass,
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
      m_fmxHud(fmxHud),
      m_statsHud(statsHud),
      m_eventLog(eventLog),
      m_clock(clock),
#if GAME_HAS_TYRE_TEMP
      m_tyreTemp(tyreTemp),
#endif
#if GAME_HAS_ECU
      m_ecu(ecu),
#endif
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
    DEBUG_INFO("SettingsHud created");
    setDraggable(true);

    // Pre-allocate vectors
    m_quads.reserve(1);
    m_strings.reserve(60);  // Less text now with tabs
    m_clickRegions.reserve(90);  // Sized for largest tab (TAB_RIDERS: 30 server + 48 tracked cells + pagination)

    // Start hidden
    hide();
}

void SettingsHud::show() {
    if (m_bVisible) return;

    m_bVisible = true;

    // Rebuild UI
    rebuildRenderData();
}

void SettingsHud::hide() {
    m_bVisible = false;
    clearStrings();
    m_quads.clear();
    m_clickRegions.clear();
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
    if (!m_bVisible) return;

    // Process dirty flag first (e.g., from showUpdatesTab() or external tab switch)
    if (isDataDirty()) {
        rebuildRenderData();
        clearDataDirty();
    }

    // Refresh the Save button when the unsaved-changes state flips (a HUD dragged while the
    // panel is open, an auto-save/leave-track flush clearing it), so it lights up / greys out.
    bool dirtyNow = SettingsManager::getInstance().isDirty();
    if (dirtyNow != m_lastSettingsDirty) {
        m_lastSettingsDirty = dirtyNow;
        rebuildRenderData();
    }

    // Check for window resize (need to rebuild click regions with new coordinates)
    const InputManager& input = InputManager::getInstance();
    int currentWidth = input.getWindowWidth();
    int currentHeight = input.getWindowHeight();

    if (currentWidth != m_cachedWindowWidth || currentHeight != m_cachedWindowHeight) {
        // Window resized - rebuild everything to update click regions
        m_cachedWindowWidth = currentWidth;
        m_cachedWindowHeight = currentHeight;
        rebuildRenderData();
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
                } else {
                    const char* tooltipId = getTooltipIdForRegion(region.type, m_activeTab);
                    m_hoveredTooltipId = tooltipId ? tooltipId : "";
                }
            } else {
                m_hoveredTooltipId.clear();
            }
            rebuildRenderData();  // Rebuild to update button backgrounds and tooltip
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
                rebuildRenderData();
            }
        }

        // For riders tab, track which tracked rider cell is hovered
        if (m_activeTab == TAB_RIDERS && m_trackedRidersCellHeight > 0.0f && m_trackedRidersPerRow > 0) {
            int newHoveredIndex = -1;

            // Apply offset to stored coordinates for comparison with cursor
            float ridersStartY = m_trackedRidersStartY + m_fOffsetY;
            float ridersStartX = m_trackedRidersStartX + m_fOffsetX;

            if (cursor.y >= ridersStartY && cursor.x >= ridersStartX) {
                float relativeY = cursor.y - ridersStartY;
                float relativeX = cursor.x - ridersStartX;

                int row = static_cast<int>(relativeY / m_trackedRidersCellHeight);
                int col = static_cast<int>(relativeX / m_trackedRidersCellWidth);

                if (col >= 0 && col < m_trackedRidersPerRow) {
                    newHoveredIndex = row * m_trackedRidersPerRow + col;
                }
            }

            if (newHoveredIndex != m_hoveredTrackedRiderIndex) {
                m_hoveredTrackedRiderIndex = newHoveredIndex;
                rebuildRenderData();
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
            rebuildRenderData();
        }
        // Rebuild every frame during capture to show real-time modifier feedback
        else {
            rebuildRenderData();
        }
    }
    // Check if capture completed (must be outside isCapturing block - capture ends same frame)
    if (hotkeyMgr.wasCaptureCompleted()) {
        rebuildRenderData();
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
    if (m_bVisible) {
        rebuildRenderData();
    }
}

void SettingsHud::addClickRegion(ClickRegion::Type type, float x, float y, float width, float height,
                                 BaseHud* targetHud, uint32_t* bitfield, uint8_t* displayMode,
                                 uint32_t flagBit, bool isRequired, int tabIndex) {
    // Helper to create and add a ClickRegion with less boilerplate
    ClickRegion region;
    region.x = x;
    region.y = y;
    region.width = width;
    region.height = height;
    region.type = type;
    region.targetHud = targetHud;
    region.flagBit = flagBit;
    region.isRequired = isRequired;
    region.tabIndex = tabIndex;

    // Set the appropriate variant member based on type
    if (type == ClickRegion::CHECKBOX && bitfield != nullptr) {
        region.targetPointer = bitfield;
    } else if ((type == ClickRegion::DISPLAY_MODE_UP || type == ClickRegion::DISPLAY_MODE_DOWN) && displayMode != nullptr) {
        region.targetPointer = displayMode;
    } else {
        region.targetPointer = std::monostate{};
    }

    m_clickRegions.push_back(region);
}

void SettingsHud::rebuildRenderData() {
    if (!m_bVisible) return;

    clearStrings();
    m_quads.clear();
    m_clickRegions.clear();

    // Update cached window size (use actual pixel dimensions)
    const InputManager& input = InputManager::getInstance();
    m_cachedWindowWidth = input.getWindowWidth();
    m_cachedWindowHeight = input.getWindowHeight();

    auto dim = getScaledDimensions();

    // Layout constants - compact panel for single HUD
    constexpr int panelWidthChars = SettingsHud::SETTINGS_PANEL_WIDTH;
    constexpr float sectionSpacing = 0.0150f;
    constexpr float tabSpacing = 0.0050f;

    float panelWidth = PluginUtils::calculateMonospaceTextWidth(panelWidthChars, dim.fontSize) + dim.paddingH + dim.paddingH;

    // Estimate height - sized to fit all tabs + content (Friends tab added one more row;
    // the Rumble tab's expanded front/rear splits and Rev/Pit Limiter rows are the tallest case)
    int estimatedRows = 33;
    float backgroundHeight = dim.paddingV + dim.lineHeightLarge + dim.lineHeightNormal + (estimatedRows * dim.lineHeightNormal) + dim.paddingV;

    // Center the panel horizontally and vertically
    float startX = (1.0f - panelWidth) / 2.0f;
    float startY = (1.0f - backgroundHeight) / 2.0f;

    setBounds(startX, startY, startX + panelWidth, startY + backgroundHeight);
    addBackgroundQuad(startX, startY, panelWidth, backgroundHeight);

    float contentStartX = startX + dim.paddingH;
    float currentY = startY + dim.paddingV;

    // Main title
    float titleX = contentStartX + (panelWidth - dim.paddingH - dim.paddingH) / 2.0f;
    addString("MXBMRP3 SETTINGS", titleX, currentY, Justify::CENTER,
        Fonts::getTitle(), ColorConfig::getInstance().getPrimary(), dim.fontSizeLarge);

    currentY += dim.lineHeightLarge + tabSpacing;

    // Vertical tab bar on left side
    float tabStartX = contentStartX;
    float tabStartY = currentY;
    float tabWidth = PluginUtils::calculateMonospaceTextWidth(SettingsHud::SETTINGS_TAB_WIDTH, dim.fontSize);

    float checkboxWidth = PluginUtils::calculateMonospaceTextWidth(4, dim.fontSize);  // "[X] " or "    "

    // Shared dim level for "inactive" tab icons (disabled toggles + non-toggle section
    // tabs) so they read as equally subdued; enabled toggles stay at full opacity.
    constexpr float INACTIVE_ICON_OPACITY = 0.5f;

    // Draws an identity icon in a tab's checkbox cell at the given colour. Returns false
    // if no icon is assigned/available (caller can fall back to text). Icons render a bit
    // smaller than the row font (they fill their glyph box more than text fills the em) and
    // nudged up ~2px (at 1080p, scaled) so they sit optically centred on the row.
    auto drawTabIcon = [&](float x, float y, const char* iconName, unsigned long color) -> bool {
        // Same global switch that drives the title-bar icons gates the tab icons.
        int spriteIndex = (UiConfig::getInstance().getTitleIcons() && iconName && iconName[0])
            ? AssetManager::getInstance().getIconSpriteIndex(iconName) : 0;
        if (spriteIndex <= 0) return false;
        constexpr float TAB_ICON_SCALE = 0.63f;
        float cellW = checkboxWidth * 0.25f;
        float iconCenterY = y + dim.lineHeightNormal * 0.5f - (2.0f / 1080.0f) * dim.scale;
        addIcon(x + cellW * 1.5f, iconCenterY, spriteIndex, color, dim.fontSize * TAB_ICON_SCALE);
        return true;
    };

    // Draws a tab's enable/disable toggle in semantic colours: POSITIVE when enabled,
    // NEGATIVE when disabled (a disabled icon lightens 10% on hover as an affordance).
    // Falls back to the legacy "[x]"/"[ ]" text when no icon is available.
    // Call right after pushing the tab's toggle ClickRegion so the hover check targets it.
    auto drawTabToggle = [&](float x, float y, const char* iconName, bool enabled) {
        ColorConfig& cc = ColorConfig::getInstance();
        // Full-opacity semantic base: POSITIVE (enabled) / NEGATIVE (disabled).
        unsigned long base = enabled ? cc.getPositive() : cc.getNegative();
        bool hovered = (m_hoveredRegionIndex >= 0 &&
                        m_hoveredRegionIndex == static_cast<int>(m_clickRegions.size()) - 1);
        unsigned long iconColor;
        if (hovered) {
            // Clear affordance in BOTH states: full opacity + a strong lighten, so a
            // disabled icon jumps from dimmed to bright and an enabled one brightens.
            // (lightenColor keeps alpha, so build from the full-opacity base.)
            iconColor = PluginUtils::lightenColor(base, 0.25f);
        } else {
            // Enabled pops at full; disabled is dimmed to the muted section level so it
            // doesn't scream.
            iconColor = enabled ? base : PluginUtils::applyOpacity(base, INACTIVE_ICON_OPACITY);
        }
        if (!drawTabIcon(x, y, iconName, iconColor)) {
            // Text fallback (no icon assigned, or asset missing on this build)
            addString(enabled ? "[x]" : "[ ]", x, y, Justify::LEFT,
                Fonts::getNormal(), iconColor, dim.fontSize);
        }
    };

    // Define visual tab order with section markers
    static constexpr int TAB_SECTION_GLOBAL = -1;
    static constexpr int TAB_SECTION_PROFILE = -2;
    static constexpr int TAB_SECTION_ELEMENTS = -3;
    static constexpr int tabDisplayOrder[] = {
        TAB_SECTION_GLOBAL,
        TAB_GENERAL,
        TAB_APPEARANCE,
        TAB_HOTKEYS,
        TAB_RIDERS,
        TAB_RUMBLE,
        TAB_HELMET,
        TAB_DIRECTOR,
        TAB_UPDATES,
        TAB_SECTION_PROFILE,
        TAB_SECTION_ELEMENTS,
        TAB_STANDINGS,
        TAB_MAP,
        TAB_RADAR,
        TAB_LAP_LOG,
        TAB_IDEAL_LAP,
        TAB_SESSION_CHARTS,
        TAB_TELEMETRY,
        TAB_RECORDS,
        TAB_PITBOARD,
        TAB_SESSION,
        TAB_TIMING,
        TAB_GAP_BAR,
        TAB_NOTICES,
        TAB_EVENT_LOG,
        TAB_FRIENDS,
        TAB_FMX,
        TAB_STATS,
        TAB_PERFORMANCE,
        TAB_WIDGETS
    };

    for (size_t orderIdx = 0; orderIdx < sizeof(tabDisplayOrder)/sizeof(tabDisplayOrder[0]); orderIdx++) {
        int i = tabDisplayOrder[orderIdx];

        // Skip game-gated tabs whose backing HUD isn't registered on this build (Records on
        // GP Bikes, FMX on karts, Friends on non-Steam). Section headers are negative ids and
        // fall through to their own handling below. Single source of truth: isTabAvailable().
        if (i >= 0 && !isTabAvailable(i)) {
            continue;
        }

        // Section headers (bold, primary color, not clickable)
        if (i == TAB_SECTION_GLOBAL) {
            addString("Global", tabStartX, tabStartY, Justify::LEFT,
                Fonts::getStrong(), ColorConfig::getInstance().getPrimary(), dim.fontSize);
            tabStartY += dim.lineHeightNormal;
            continue;
        }
        if (i == TAB_SECTION_PROFILE) {
            tabStartY += dim.lineHeightNormal * 0.5f;  // Extra spacing before section
            addString("Profile", tabStartX, tabStartY, Justify::LEFT,
                Fonts::getStrong(), ColorConfig::getInstance().getPrimary(), dim.fontSize);
            tabStartY += dim.lineHeightNormal;

            // Profile cycle control: < Practice >
            float charWidth = PluginUtils::calculateMonospaceTextWidth(1, dim.fontSize);
            ProfileType activeProfile = ProfileManager::getInstance().getActiveProfile();
            const char* profileName = ProfileManager::getProfileName(activeProfile);

            float currentX = tabStartX;

            // Left arrow "<" with click region (cycles to previous profile)
            addString("<", currentX, tabStartY, Justify::LEFT,
                Fonts::getNormal(), ColorConfig::getInstance().getAccent(), dim.fontSize);
            m_clickRegions.push_back(ClickRegion(
                currentX, tabStartY, charWidth * 2, dim.lineHeightNormal,
                ClickRegion::PROFILE_CYCLE_DOWN, nullptr
            ));
            currentX += charWidth * 2;

            // Profile name (not clickable)
            char profileLabel[12];
            snprintf(profileLabel, sizeof(profileLabel), "%-8s", profileName);
            addString(profileLabel, currentX, tabStartY, Justify::LEFT,
                Fonts::getNormal(), ColorConfig::getInstance().getPrimary(), dim.fontSize);
            currentX += charWidth * 8;

            // Right arrow " >" with click region (cycles to next profile)
            addString(" >", currentX, tabStartY, Justify::LEFT,
                Fonts::getNormal(), ColorConfig::getInstance().getAccent(), dim.fontSize);
            m_clickRegions.push_back(ClickRegion(
                currentX, tabStartY, charWidth * 2, dim.lineHeightNormal,
                ClickRegion::PROFILE_CYCLE_UP, nullptr
            ));

            tabStartY += dim.lineHeightNormal;
            continue;
        }
        if (i == TAB_SECTION_ELEMENTS) {
            tabStartY += dim.lineHeightNormal * 0.5f;  // Extra spacing before section
            addString("Elements", tabStartX, tabStartY, Justify::LEFT,
                Fonts::getStrong(), ColorConfig::getInstance().getPrimary(), dim.fontSize);
            tabStartY += dim.lineHeightNormal;
            continue;
        }

        bool isActive = (i == m_activeTab);

        // Get the HUD for this tab (nullptr for General and Widgets)
        BaseHud* tabHud = nullptr;
        switch (i) {
            case TAB_STANDINGS:    tabHud = m_standings; break;
            case TAB_MAP:          tabHud = m_mapHud; break;
            case TAB_PITBOARD:     tabHud = m_pitboard; break;
            case TAB_SESSION:      tabHud = m_session; break;
            case TAB_LAP_LOG:      tabHud = m_lapLog; break;
            case TAB_FRIENDS:      tabHud = m_friends; break;
            case TAB_SESSION_CHARTS: tabHud = m_sessionCharts; break;
            case TAB_IDEAL_LAP: tabHud = m_idealLap; break;
            case TAB_TELEMETRY:    tabHud = m_telemetry; break;
            case TAB_PERFORMANCE:  tabHud = m_performance; break;
            case TAB_RECORDS:      tabHud = m_records; break;
            case TAB_RADAR:        tabHud = m_radarHud; break;
            case TAB_TIMING:       tabHud = m_timing; break;
            case TAB_GAP_BAR:      tabHud = m_gapBar; break;
            case TAB_FMX:          tabHud = m_fmxHud; break;
            case TAB_STATS:        tabHud = m_statsHud; break;
            case TAB_EVENT_LOG:    tabHud = m_eventLog; break;
            case TAB_NOTICES:      tabHud = m_notices; break;
            default:               tabHud = nullptr; break;  // General, Widgets, Rumble have no single HUD
        }

        // Determine if this tab's HUD/widgets are enabled. Per-HUD checkboxes show
        // the focused surface's on/off (companion vs game); the manager/global
        // toggles (widgets/rumble/helmet/updates/director) are shared, not decoupled.
        bool companionSurface = InputManager::getInstance().getActiveSurface() == InputManager::Surface::Companion;
        bool isHudEnabled;
        if (tabHud) {
            isHudEnabled = companionSurface ? tabHud->getCompanionVisible() : tabHud->isVisible();
        } else if (i == TAB_WIDGETS) {
            isHudEnabled = HudManager::getInstance().areWidgetsEnabled();
        } else if (i == TAB_RUMBLE) {
            isHudEnabled = XInputReader::getInstance().getGlobalRumbleConfig().enabled;
        } else if (i == TAB_HELMET) {
            isHudEnabled = m_helmetOverlay && m_helmetOverlay->isVisible();
        } else if (i == TAB_UPDATES) {
            isHudEnabled = UpdateChecker::getInstance().isEnabled();
        } else if (i == TAB_DIRECTOR) {
            isHudEnabled = DirectorManager::getInstance().isEnabled();
        } else {
            isHudEnabled = true;  // General is always "enabled"
        }

        // Tab color: PRIMARY if active, ACCENT if inactive
        unsigned long tabColor = isActive ? ColorConfig::getInstance().getPrimary() : ColorConfig::getInstance().getAccent();

        float currentTabX = tabStartX;

        // Add checkbox for tabs with toggleable HUDs or widgets
        if (tabHud) {
            // Checkbox click region for individual HUD
            m_clickRegions.push_back(ClickRegion(
                currentTabX, tabStartY, checkboxWidth, dim.lineHeightNormal,
                ClickRegion::HUD_TOGGLE, tabHud
            ));

            // Identity icon (or text fallback) for the individual HUD
            drawTabToggle(currentTabX, tabStartY, tabHud->getIconName(), isHudEnabled);

            currentTabX += checkboxWidth;
        } else if (i == TAB_WIDGETS) {
            // Checkbox click region for widgets master toggle
            m_clickRegions.push_back(ClickRegion(
                currentTabX, tabStartY, checkboxWidth, dim.lineHeightNormal,
                ClickRegion::WIDGETS_TOGGLE, nullptr
            ));

            drawTabToggle(currentTabX, tabStartY, "hud-widgets", isHudEnabled);

            currentTabX += checkboxWidth;
        } else if (i == TAB_RUMBLE) {
            // Checkbox click region for rumble master toggle
            m_clickRegions.push_back(ClickRegion(
                currentTabX, tabStartY, checkboxWidth, dim.lineHeightNormal,
                ClickRegion::RUMBLE_TOGGLE, nullptr
            ));

            drawTabToggle(currentTabX, tabStartY, "hud-rumble", isHudEnabled);

            currentTabX += checkboxWidth;
        } else if (i == TAB_HELMET) {
            // Checkbox click region for helmet overlay master toggle
            m_clickRegions.push_back(ClickRegion(
                currentTabX, tabStartY, checkboxWidth, dim.lineHeightNormal,
                ClickRegion::HELMET_OVERLAY_TOGGLE, m_helmetOverlay
            ));

            // Helmet icon is game-specific (the helmet shape differs per game).
#if defined(GAME_MXBIKES)
            drawTabToggle(currentTabX, tabStartY, "hud-helmet-mx", isHudEnabled);
#else
            drawTabToggle(currentTabX, tabStartY, "hud-helmet", isHudEnabled);
#endif

            currentTabX += checkboxWidth;
        } else if (i == TAB_UPDATES) {
            // Checkbox click region for update checking toggle
            m_clickRegions.push_back(ClickRegion(
                currentTabX, tabStartY, checkboxWidth, dim.lineHeightNormal,
                ClickRegion::UPDATE_CHECK_TOGGLE, nullptr
            ));

            drawTabToggle(currentTabX, tabStartY, "hud-updates", isHudEnabled);

            currentTabX += checkboxWidth;
        } else if (i == TAB_DIRECTOR) {
            // Checkbox click region for the auto-director master toggle
            m_clickRegions.push_back(ClickRegion(
                currentTabX, tabStartY, checkboxWidth, dim.lineHeightNormal,
                ClickRegion::DIRECTOR_ENABLE_TOGGLE, nullptr
            ));

            drawTabToggle(currentTabX, tabStartY, "video", isHudEnabled);

            currentTabX += checkboxWidth;
        } else {
            // Non-toggleable section tabs: an ACCENT identity icon (no on/off state, so
            // it's distinct from the positive/negative toggle icons and matches the tab
            // label's colour family rather than looking disabled).
            const char* sectionIcon =
                i == TAB_GENERAL    ? "hud-general" :
                i == TAB_APPEARANCE ? "hud-appearance" :
                i == TAB_HOTKEYS    ? "hud-hotkeys" :
                i == TAB_RIDERS     ? "hud-riders" : "";
            drawTabIcon(currentTabX, tabStartY, sectionIcon, ColorConfig::getInstance().getAccent());
            currentTabX += checkboxWidth;
        }

        // Tab click region (for selecting the tab)
        float tabLabelWidth = tabWidth - checkboxWidth;
        size_t tabRegionIndex = m_clickRegions.size();  // Track index for hover check

        // Tab ID for description lookup (lowercase)
        const char* tabId = i == TAB_GENERAL ? "general" :
                           i == TAB_APPEARANCE ? "appearance" :
                           i == TAB_STANDINGS ? "standings" :
                           i == TAB_MAP ? "map" :
                           i == TAB_LAP_LOG ? "lap_log" :
                           i == TAB_FRIENDS ? "friends" :
                           i == TAB_IDEAL_LAP ? "ideal_lap" :
                           i == TAB_TELEMETRY ? "telemetry" :
                           i == TAB_PERFORMANCE ? "performance" :
                           i == TAB_PITBOARD ? "pitboard" :
                           i == TAB_SESSION ? "session" :
                           i == TAB_RECORDS ? "records" :
                           i == TAB_TIMING ? "timing" :
                           i == TAB_GAP_BAR ? "gap_bar" :
                           i == TAB_WIDGETS ? "widgets" :
                           i == TAB_RUMBLE ? "rumble" :
                           i == TAB_HOTKEYS ? "hotkeys" :
                           i == TAB_RADAR ? "radar" :
                           i == TAB_SESSION_CHARTS ? "session_charts" :
                           i == TAB_RIDERS ? "riders" :
                           i == TAB_UPDATES ? "updates" :
                           i == TAB_FMX ? "fmx" :
                           i == TAB_STATS ? "stats" :
                           i == TAB_EVENT_LOG ? "event_log" :
                           i == TAB_HELMET ? "helmet" :
                           i == TAB_DIRECTOR ? "director" :
                           "general";

        ClickRegion tabRegion;
        tabRegion.x = currentTabX;
        tabRegion.y = tabStartY;
        tabRegion.width = tabLabelWidth;
        tabRegion.height = dim.lineHeightNormal;
        tabRegion.type = ClickRegion::TAB;
        tabRegion.targetPointer = std::monostate{};
        tabRegion.flagBit = 0;
        tabRegion.isRequired = false;
        tabRegion.targetHud = nullptr;
        tabRegion.tabIndex = i;
        tabRegion.tooltipId = tabId;  // Show tab description on hover
        m_clickRegions.push_back(tabRegion);

        // The hover/active highlight leads the label by one char — matching the
        // row-highlight and button convention (which pad the highlight ~1 char around
        // the label) instead of sitting flush against the text — WITHOUT moving the
        // text: the label stays at currentTabX and the highlight starts one char to its
        // left, keeping its right edge. The 1-char lead lands in the gap between the
        // tab's identity icon and its label, so it doesn't overlap the icon.
        float labelPad = PluginUtils::calculateMonospaceTextWidth(1, dim.fontSize);
        float highlightX = currentTabX - labelPad;
        float highlightWidth = tabLabelWidth + labelPad;

        // Active tab background
        if (isActive) {
            SPluginQuad_t bgQuad;
            float bgX = highlightX, bgY = tabStartY;
            applyOffset(bgX, bgY);
            setQuadPositions(bgQuad, bgX, bgY, highlightWidth, dim.lineHeightNormal);
            bgQuad.m_iSprite = SpriteIndex::SOLID_COLOR;
            bgQuad.m_ulColor = PluginUtils::applyOpacity(ColorConfig::getInstance().getAccent(), 128.0f / 255.0f);
            m_quads.push_back(bgQuad);
        }
        // Hover background for inactive tabs
        else if (m_hoveredRegionIndex >= 0 && static_cast<size_t>(m_hoveredRegionIndex) == tabRegionIndex) {
            SPluginQuad_t hoverQuad;
            float hoverX = highlightX, hoverY = tabStartY;
            applyOffset(hoverX, hoverY);
            setQuadPositions(hoverQuad, hoverX, hoverY, highlightWidth, dim.lineHeightNormal);
            hoverQuad.m_iSprite = SpriteIndex::SOLID_COLOR;
            hoverQuad.m_ulColor = PluginUtils::applyOpacity(ColorConfig::getInstance().getAccent(), 60.0f / 255.0f);
            m_quads.push_back(hoverQuad);
        }

        // Label stays at its original position (the highlight leads it, above).
        addString(getTabName(i), currentTabX, tabStartY, Justify::LEFT, Fonts::getNormal(), tabColor, dim.fontSize);

        tabStartY += dim.lineHeightNormal;
    }

    // Content area starts to the right of the tabs
    float contentAreaStartX = contentStartX + tabWidth + PluginUtils::calculateMonospaceTextWidth(2, dim.fontSize);  // 2-char gap after tabs
    // Content starts at the same Y as the tabs — currentY is already there (the tab
    // loop advances a separate cursor), so nothing to set here.

    // Helper lambdas for controls
    // NOTE: These lambdas are intentionally NOT extracted to member functions.
    // They capture local layout state (dim, currentY, contentAreaStartX, etc.) which
    // would require passing 8+ parameters if converted to methods. Lambdas improve
    // readability and maintainability here. See CLAUDE.md "Design Decisions".
    float leftColumnX = contentAreaStartX + PluginUtils::calculateMonospaceTextWidth(SettingsHud::SETTINGS_LEFT_COLUMN, dim.fontSize);
    float rightColumnX = contentAreaStartX + PluginUtils::calculateMonospaceTextWidth(SettingsHud::SETTINGS_RIGHT_COLUMN, dim.fontSize);

    // Helper lambda to add cycle control with < value > pattern - shared across all controls
    // If enabled is false, no click regions are added and muted color is used
    // If isOff is true, the value is muted (for "Off" state visual consistency)
    auto addCycleControl = [&](float baseX, float y, const char* value, int maxValueWidth,
                               ClickRegion::Type downType, ClickRegion::Type upType, BaseHud* targetHud,
                               bool enabled = true, bool isOff = false) {
        float charWidth = PluginUtils::calculateMonospaceTextWidth(1, dim.fontSize);
        float currentX = baseX;
        unsigned long valueColor = (enabled && !isOff) ? ColorConfig::getInstance().getPrimary() : ColorConfig::getInstance().getMuted();

        // Left arrow "<" - only show when enabled
        if (enabled) {
            addString("<", currentX, y, Justify::LEFT, Fonts::getNormal(), ColorConfig::getInstance().getAccent(), dim.fontSize);
            m_clickRegions.push_back(ClickRegion(
                currentX, y, charWidth * 2, dim.lineHeightNormal,
                downType, targetHud, 0, false, 0
            ));
        }
        currentX += charWidth * 2;  // "< " (spacing preserved even if arrow hidden)

        // Value with fixed width (left-aligned, padded)
        char paddedValue[32];
        snprintf(paddedValue, sizeof(paddedValue), "%-*s", maxValueWidth, value);
        addString(paddedValue, currentX, y, Justify::LEFT, Fonts::getNormal(), valueColor, dim.fontSize);
        currentX += PluginUtils::calculateMonospaceTextWidth(maxValueWidth, dim.fontSize);

        // Right arrow " >" - only show when enabled
        if (enabled) {
            addString(" >", currentX, y, Justify::LEFT, Fonts::getNormal(), ColorConfig::getInstance().getAccent(), dim.fontSize);
            m_clickRegions.push_back(ClickRegion(
                currentX, y, charWidth * 2, dim.lineHeightNormal,
                upType, targetHud, 0, false, 0
            ));
        }
    };

    // Helper lambda to add toggle control with < On/Off > pattern - for boolean settings
    // Both arrows trigger the same toggle action. If enabled is false, muted colors are used.
    // "Off" values are also muted for visual consistency.
    auto addToggleControl = [&](float baseX, float y, bool isOn,
                                ClickRegion::Type toggleType, BaseHud* targetHud,
                                uint32_t* bitfield = nullptr, uint32_t flag = 0,
                                bool enabled = true) {
        float charWidth = PluginUtils::calculateMonospaceTextWidth(1, dim.fontSize);
        float currentX = baseX;
        unsigned long valueColor = (enabled && isOn) ? ColorConfig::getInstance().getPrimary() : ColorConfig::getInstance().getMuted();
        const char* value = isOn ? "On" : "Off";
        constexpr int VALUE_WIDTH = 3;  // "Off" is longest

        // Left arrow "<" - only show when enabled
        if (enabled) {
            addString("<", currentX, y, Justify::LEFT, Fonts::getNormal(), ColorConfig::getInstance().getAccent(), dim.fontSize);
            if (bitfield != nullptr) {
                // CHECKBOX type with bitfield
                m_clickRegions.push_back(ClickRegion(
                    currentX, y, charWidth * 2, dim.lineHeightNormal,
                    toggleType, bitfield, flag, false, targetHud
                ));
            } else {
                // Simple toggle without bitfield
                m_clickRegions.push_back(ClickRegion(
                    currentX, y, charWidth * 2, dim.lineHeightNormal,
                    toggleType, targetHud
                ));
            }
        }
        currentX += charWidth * 2;  // "< " (spacing preserved even if arrow hidden)

        // Value with fixed width
        char paddedValue[8];
        snprintf(paddedValue, sizeof(paddedValue), "%-*s", VALUE_WIDTH, value);
        addString(paddedValue, currentX, y, Justify::LEFT, Fonts::getNormal(), valueColor, dim.fontSize);
        currentX += PluginUtils::calculateMonospaceTextWidth(VALUE_WIDTH, dim.fontSize);

        // Right arrow " >" - only show when enabled
        if (enabled) {
            addString(" >", currentX, y, Justify::LEFT, Fonts::getNormal(), ColorConfig::getInstance().getAccent(), dim.fontSize);
            if (bitfield != nullptr) {
                // CHECKBOX type with bitfield
                m_clickRegions.push_back(ClickRegion(
                    currentX, y, charWidth * 2, dim.lineHeightNormal,
                    toggleType, bitfield, flag, false, targetHud
                ));
            } else {
                // Simple toggle without bitfield
                m_clickRegions.push_back(ClickRegion(
                    currentX, y, charWidth * 2, dim.lineHeightNormal,
                    toggleType, targetHud
                ));
            }
        }
    };

    auto addHudControls = [&](BaseHud* hud, bool enableTitle = true) -> float {
        // Save starting Y for right column (data toggles)
        float sectionStartY = currentY;

        // LEFT COLUMN: Basic controls
        float controlX = leftColumnX;
        float toggleX = controlX + PluginUtils::calculateMonospaceTextWidth(12, dim.fontSize);  // Align all toggles

        // Visibility toggle
        bool isVisible = hud->isVisible();
        addString("Visible", controlX, currentY, Justify::LEFT,
            Fonts::getNormal(), ColorConfig::getInstance().getSecondary(), dim.fontSize);
        addToggleControl(toggleX, currentY, isVisible, ClickRegion::HUD_TOGGLE, hud);
        currentY += dim.lineHeightNormal;

        // Title toggle (can be disabled/grayed out)
        bool showTitle = enableTitle ? hud->getShowTitle() : false;
        addString("Title", controlX, currentY, Justify::LEFT,
            Fonts::getNormal(), enableTitle ? ColorConfig::getInstance().getSecondary() : ColorConfig::getInstance().getMuted(), dim.fontSize);
        addToggleControl(toggleX, currentY, showTitle, ClickRegion::TITLE_TOGGLE, hud, nullptr, 0, enableTitle);
        currentY += dim.lineHeightNormal;

        // Background texture variant cycle (Off, 1, 2, ...)
        // Only enable if textures are available for this HUD
        bool hasTextures = !hud->getAvailableTextureVariants().empty();
        addString("Texture", controlX, currentY, Justify::LEFT,
            Fonts::getNormal(), hasTextures ? ColorConfig::getInstance().getSecondary() : ColorConfig::getInstance().getMuted(), dim.fontSize);
        char textureValue[16];
        int variant = hud->getTextureVariant();
        if (!hasTextures || variant == 0) {
            snprintf(textureValue, sizeof(textureValue), "Off");
        } else {
            snprintf(textureValue, sizeof(textureValue), "%d", variant);
        }
        addCycleControl(toggleX, currentY, textureValue, 4,
            ClickRegion::TEXTURE_VARIANT_DOWN, ClickRegion::TEXTURE_VARIANT_UP, hud, hasTextures);
        currentY += dim.lineHeightNormal;

        // Background opacity controls
        addString("Opacity", controlX, currentY, Justify::LEFT,
            Fonts::getNormal(), ColorConfig::getInstance().getSecondary(), dim.fontSize);
        char opacityValue[16];
        int opacityPercent = static_cast<int>(std::round(hud->getBackgroundOpacity() * 100.0f));
        snprintf(opacityValue, sizeof(opacityValue), "%d%%", opacityPercent);
        addCycleControl(toggleX, currentY, opacityValue, 4,
            ClickRegion::BACKGROUND_OPACITY_DOWN, ClickRegion::BACKGROUND_OPACITY_UP, hud);
        currentY += dim.lineHeightNormal;

        // Scale controls
        addString("Scale", controlX, currentY, Justify::LEFT,
            Fonts::getNormal(), ColorConfig::getInstance().getSecondary(), dim.fontSize);
        char scaleValue[16];
        int scalePercent = static_cast<int>(std::round(hud->getScale() * 100.0f));
        snprintf(scaleValue, sizeof(scaleValue), "%d%%", scalePercent);
        addCycleControl(toggleX, currentY, scaleValue, 4,
            ClickRegion::SCALE_DOWN, ClickRegion::SCALE_UP, hud);
        currentY += dim.lineHeightNormal;

        // Return the starting Y for right column (data toggles)
        return sectionStartY;
    };

    // Data toggle control - displays "Label: < On/Off >" format
    // labelWidth should accommodate the longest label in the group for alignment
    auto addDataToggle = [&](const char* label, uint32_t* bitfield, uint32_t flag, bool isRequired, BaseHud* hud, float yPos, int labelWidth = 12) {
        float dataX = rightColumnX;
        bool isChecked = (*bitfield & flag) != 0;
        bool enabled = !isRequired;

        // Label with padding
        char paddedLabel[32];
        snprintf(paddedLabel, sizeof(paddedLabel), "%-*s", labelWidth, label);
        addString(paddedLabel, dataX, yPos, Justify::LEFT, Fonts::getNormal(),
            enabled ? ColorConfig::getInstance().getSecondary() : ColorConfig::getInstance().getMuted(), dim.fontSize);

        // Toggle control
        float toggleX = dataX + PluginUtils::calculateMonospaceTextWidth(labelWidth, dim.fontSize);
        addToggleControl(toggleX, yPos, isChecked, ClickRegion::CHECKBOX, hud, bitfield, flag, enabled);
    };

    // Helper for grouped toggles that toggle multiple bits
    auto addGroupToggle = [&](const char* label, uint32_t* bitfield, uint32_t groupFlags, bool isRequired, BaseHud* hud, float yPos, int labelWidth = 12) {
        float dataX = rightColumnX;
        // Group is checked if all bits in group are set
        bool isChecked = (*bitfield & groupFlags) == groupFlags;
        bool enabled = !isRequired;

        // Label with padding
        char paddedLabel[32];
        snprintf(paddedLabel, sizeof(paddedLabel), "%-*s", labelWidth, label);
        addString(paddedLabel, dataX, yPos, Justify::LEFT, Fonts::getNormal(),
            enabled ? ColorConfig::getInstance().getSecondary() : ColorConfig::getInstance().getMuted(), dim.fontSize);

        // Toggle control
        float toggleX = dataX + PluginUtils::calculateMonospaceTextWidth(labelWidth, dim.fontSize);
        addToggleControl(toggleX, yPos, isChecked, ClickRegion::CHECKBOX, hud, bitfield, groupFlags, enabled);
    };

    // Render controls for active tab only
    BaseHud* activeHud = nullptr;
    float dataStartY = 0.0f;

    // Create layout context for extracted tabs
    // controlX is where the toggle controls start (labelX + 24 chars for Phase 3 descriptive labels)
    float controlX = leftColumnX + PluginUtils::calculateMonospaceTextWidth(24, dim.fontSize);
    // Compute content area width (from contentAreaStartX to right edge of panel content)
    // This is used for row width calculations to ensure content doesn't extend past the panel
    float contentAreaWidth = (startX + panelWidth - dim.paddingH) - contentAreaStartX;
    SettingsLayoutContext layoutCtx(this, dim, leftColumnX, controlX, rightColumnX,
                                     contentAreaStartX, contentAreaWidth, currentY);

    switch (m_activeTab) {
        case TAB_GENERAL:
            // Use extracted tab renderer
            layoutCtx.currentY = currentY;
            activeHud = renderTabGeneral(layoutCtx);
            currentY = layoutCtx.currentY;
            break;

        case TAB_APPEARANCE:
            // Use extracted tab renderer
            layoutCtx.currentY = currentY;
            activeHud = renderTabAppearance(layoutCtx);
            currentY = layoutCtx.currentY;
            break;

        case TAB_HOTKEYS:
            // Use extracted tab renderer
            layoutCtx.currentY = currentY;
            activeHud = renderTabHotkeys(layoutCtx);
            currentY = layoutCtx.currentY;
            break;

        case TAB_STANDINGS:
            // Use extracted tab renderer
            layoutCtx.currentY = currentY;
            activeHud = renderTabStandings(layoutCtx);
            currentY = layoutCtx.currentY;
            break;

        case TAB_MAP:
            // Use extracted tab renderer
            layoutCtx.currentY = currentY;
            activeHud = renderTabMap(layoutCtx);
            currentY = layoutCtx.currentY;
            break;

        case TAB_LAP_LOG:
            // Use extracted tab renderer
            layoutCtx.currentY = currentY;
            activeHud = renderTabLapLog(layoutCtx);
            currentY = layoutCtx.currentY;
            break;

        case TAB_FRIENDS:
            layoutCtx.currentY = currentY;
            activeHud = renderTabFriends(layoutCtx);
            currentY = layoutCtx.currentY;
            break;

        case TAB_SESSION_CHARTS:
            // Use extracted tab renderer
            layoutCtx.currentY = currentY;
            activeHud = renderTabSessionCharts(layoutCtx);
            currentY = layoutCtx.currentY;
            break;

        case TAB_IDEAL_LAP:
            // Use extracted tab renderer
            layoutCtx.currentY = currentY;  // Sync context cursor
            activeHud = renderTabIdealLap(layoutCtx);
            currentY = layoutCtx.currentY;  // Sync local cursor back
            break;

        case TAB_TELEMETRY:
            // Use extracted tab renderer
            layoutCtx.currentY = currentY;
            activeHud = renderTabTelemetry(layoutCtx);
            currentY = layoutCtx.currentY;
            break;

        case TAB_PERFORMANCE:
            // Use extracted tab renderer
            layoutCtx.currentY = currentY;
            activeHud = renderTabPerformance(layoutCtx);
            currentY = layoutCtx.currentY;
            break;

        case TAB_PITBOARD:
            // Use extracted tab renderer
            layoutCtx.currentY = currentY;
            activeHud = renderTabPitboard(layoutCtx);
            currentY = layoutCtx.currentY;
            break;

        case TAB_SESSION:
            // Use extracted tab renderer
            layoutCtx.currentY = currentY;
            activeHud = renderTabSession(layoutCtx);
            currentY = layoutCtx.currentY;
            break;

        case TAB_RECORDS:
            // Use extracted tab renderer
            layoutCtx.currentY = currentY;
            activeHud = renderTabRecords(layoutCtx);
            currentY = layoutCtx.currentY;
            break;

        case TAB_TIMING:
            // Use extracted tab renderer
            layoutCtx.currentY = currentY;
            activeHud = renderTabTiming(layoutCtx);
            currentY = layoutCtx.currentY;
            break;

        case TAB_GAP_BAR:
            // Use extracted tab renderer
            layoutCtx.currentY = currentY;
            activeHud = renderTabGapBar(layoutCtx);
            currentY = layoutCtx.currentY;
            break;

        case TAB_WIDGETS:
            // Use extracted tab renderer
            layoutCtx.currentY = currentY;
            activeHud = renderTabWidgets(layoutCtx);
            currentY = layoutCtx.currentY;
            break;

        case TAB_NOTICES:
            layoutCtx.currentY = currentY;
            activeHud = renderTabNotices(layoutCtx);
            currentY = layoutCtx.currentY;
            break;

        case TAB_RADAR:
            // Use extracted tab renderer
            layoutCtx.currentY = currentY;
            activeHud = renderTabRadar(layoutCtx);
            currentY = layoutCtx.currentY;
            break;

        case TAB_RUMBLE:
            // Use extracted tab renderer
            layoutCtx.currentY = currentY;
            activeHud = renderTabRumble(layoutCtx);
            currentY = layoutCtx.currentY;
            break;

        case TAB_HELMET:
            layoutCtx.currentY = currentY;
            activeHud = renderTabHelmet(layoutCtx);
            currentY = layoutCtx.currentY;
            break;

        case TAB_DIRECTOR:
            layoutCtx.currentY = currentY;
            activeHud = renderTabDirector(layoutCtx);
            currentY = layoutCtx.currentY;
            break;

        case TAB_RIDERS:
            // Use extracted tab renderer
            layoutCtx.currentY = currentY;
            activeHud = renderTabRiders(layoutCtx);
            currentY = layoutCtx.currentY;
            break;

        case TAB_UPDATES:
            // Use extracted tab renderer
            layoutCtx.currentY = currentY;
            activeHud = renderTabUpdates(layoutCtx);
            currentY = layoutCtx.currentY;
            break;

        case TAB_FMX:
            // Use extracted tab renderer
            layoutCtx.currentY = currentY;
            activeHud = renderTabFmx(layoutCtx);
            currentY = layoutCtx.currentY;
            break;

        case TAB_STATS:
            layoutCtx.currentY = currentY;
            activeHud = renderTabStats(layoutCtx);
            currentY = layoutCtx.currentY;
            break;

        case TAB_EVENT_LOG:
            layoutCtx.currentY = currentY;
            activeHud = renderTabEventLog(layoutCtx);
            currentY = layoutCtx.currentY;
            break;

        default:
            DEBUG_WARN_F("Invalid tab index: %d, defaulting to TAB_STANDINGS", m_activeTab);
            activeHud = m_standings;
            break;
    }

    currentY += sectionSpacing;

    // Draw hover highlight for TOOLTIP_ROW regions
    if (m_hoveredRegionIndex >= 0 && m_hoveredRegionIndex < static_cast<int>(m_clickRegions.size())) {
        const ClickRegion& hoveredRegion = m_clickRegions[m_hoveredRegionIndex];
        if (hoveredRegion.type == ClickRegion::TOOLTIP_ROW) {
            // Draw highlight behind the hovered row (same opacity as tab hover). The
            // row region starts at the label text (labelX); extend the highlight one
            // char to the left so it has the same padding as the menu buttons (whose
            // background insets the text by a char). The right edge already reaches the
            // content edge, so only the left needs padding.
            SPluginQuad_t hoverQuad;
            float charPad = PluginUtils::calculateMonospaceTextWidth(1, dim.fontSize);
            float hoverX = hoveredRegion.x - charPad, hoverY = hoveredRegion.y;
            applyOffset(hoverX, hoverY);
            setQuadPositions(hoverQuad, hoverX, hoverY, hoveredRegion.width + charPad, hoveredRegion.height);
            hoverQuad.m_iSprite = SpriteIndex::SOLID_COLOR;
            hoverQuad.m_ulColor = PluginUtils::applyOpacity(ColorConfig::getInstance().getAccent(), 60.0f / 255.0f);
            m_quads.push_back(hoverQuad);
        }
    }

    // Render description or tooltip at the reserved position (replaces each other)
    // Calculate max width for word wrapping (contentAreaWidth - left margin from labels)
    float descTextWidth = layoutCtx.panelWidth - (layoutCtx.labelX - contentAreaStartX);
    int maxCharsPerLine = static_cast<int>(descTextWidth / PluginUtils::calculateMonospaceTextWidth(1, dim.fontSize));

    // Helper lambda to render up to 2 lines of word-wrapped text
    auto renderWrappedText = [&](const std::string& text, unsigned long color) {
        float lineY = layoutCtx.tooltipY;
        size_t lineStart = 0;
        int lineCount = 0;
        constexpr int MAX_LINES = 2;

        while (lineStart < text.length() && lineCount < MAX_LINES) {
            std::string wrappedLine;
            size_t lineEnd = lineStart + maxCharsPerLine;

            if (lineEnd >= text.length()) {
                // Last line - use remaining text
                wrappedLine = text.substr(lineStart);
                lineStart = text.length();
            } else {
                // Find last space before lineEnd for word wrap
                size_t lastSpace = text.rfind(' ', lineEnd);
                if (lastSpace != std::string::npos && lastSpace > lineStart) {
                    wrappedLine = text.substr(lineStart, lastSpace - lineStart);
                    lineStart = lastSpace + 1;  // Skip the space
                } else {
                    // No space found - hard break
                    wrappedLine = text.substr(lineStart, maxCharsPerLine);
                    lineStart += maxCharsPerLine;
                }

                // If this is the last line and there's more text, add ellipsis
                if (lineCount == MAX_LINES - 1 && lineStart < text.length()) {
                    if (wrappedLine.length() > 3) {
                        wrappedLine.resize(wrappedLine.length() - 3);
                        wrappedLine += "...";
                    }
                }
            }

            addString(wrappedLine.c_str(), layoutCtx.labelX, lineY, Justify::LEFT,
                Fonts::getNormal(), color, dim.fontSize);
            lineY += dim.lineHeightNormal;
            lineCount++;
        }
    };

    if (!m_hoveredTooltipId.empty()) {
        // Check if hovering a TAB region - show tab description instead of control tooltip
        bool isTabHover = (m_hoveredRegionIndex >= 0 &&
                          m_hoveredRegionIndex < static_cast<int>(m_clickRegions.size()) &&
                          m_clickRegions[m_hoveredRegionIndex].type == ClickRegion::TAB);

        if (isTabHover) {
            // Show tab tooltip for hovered tab
            const char* tabTooltip = TooltipManager::getInstance().getTabTooltip(m_hoveredTooltipId.c_str());
            if (tabTooltip && tabTooltip[0] != '\0') {
                renderWrappedText(std::string(tabTooltip), ColorConfig::getInstance().getMuted());
            }
        } else {
            // Show control tooltip
            const char* tooltipText = TooltipManager::getInstance().getControlTooltip(m_hoveredTooltipId.c_str());
            if (tooltipText && tooltipText[0] != '\0') {
                renderWrappedText(std::string(tooltipText), ColorConfig::getInstance().getMuted());
            }
        }
    } else if (!layoutCtx.currentTabId.empty()) {
        // Show tab tooltip (when not hovering)
        const char* tabTooltip = TooltipManager::getInstance().getTabTooltip(layoutCtx.currentTabId.c_str());
        if (tabTooltip && tabTooltip[0] != '\0') {
            renderWrappedText(std::string(tabTooltip), ColorConfig::getInstance().getMuted());
        }
    }

    // Bottom button row - always [Save/Saved] [Close]. The Save button reflects unsaved changes:
    // lit + clickable ("Save") when there are pending changes, grayed-out ("Saved") when
    // everything is persisted. It lets the player save manually without leaving the track,
    // regardless of the Auto-Save setting (which only controls the automatic leave-track flush).
    float buttonRowY = startY + backgroundHeight - dim.paddingV - dim.lineHeightNormal;
    float buttonAreaCenterX = contentStartX + (panelWidth - dim.paddingH - dim.paddingH) / 2.0f;
    float cw = PluginUtils::calculateMonospaceTextWidth(1, dim.fontSize);
    bool settingsDirty = SettingsManager::getInstance().isDirty();

    // Size both buttons for the widest label they can show (Saved / Close = 5 chars + padding),
    // so the row layout is stable whether the Save button reads "Save" or "Saved".
    float saveButtonWidth = PluginUtils::calculateMonospaceTextWidth(7, dim.fontSize);
    float closeButtonWidth = PluginUtils::calculateMonospaceTextWidth(7, dim.fontSize);
    float buttonGap = cw;  // 1 character gap between buttons
    float totalWidth = saveButtonWidth + buttonGap + closeButtonWidth;
    float startButtonX = buttonAreaCenterX - totalWidth / 2.0f;

    // [Save] / [Saved] button
    float saveButtonX = startButtonX;
    if (settingsDirty) {
        // Unsaved changes: lit and clickable.
        size_t saveRegionIndex = m_clickRegions.size();
        m_clickRegions.push_back(ClickRegion(
            saveButtonX, buttonRowY, saveButtonWidth, dim.lineHeightNormal,
            ClickRegion::SAVE_BUTTON, nullptr, 0, false, 0
        ));
        {
            SPluginQuad_t bgQuad;
            float bgX = saveButtonX, bgY = buttonRowY;
            applyOffset(bgX, bgY);
            setQuadPositions(bgQuad, bgX, bgY, saveButtonWidth, dim.lineHeightNormal);
            bgQuad.m_iSprite = SpriteIndex::SOLID_COLOR;
            bgQuad.m_ulColor = (m_hoveredRegionIndex == static_cast<int>(saveRegionIndex))
                ? ColorConfig::getInstance().getPositive()
                : PluginUtils::applyOpacity(ColorConfig::getInstance().getPositive(), 128.0f / 255.0f);
            m_quads.push_back(bgQuad);
        }
        unsigned long saveTextColor = (m_hoveredRegionIndex == static_cast<int>(saveRegionIndex))
            ? ColorConfig::getInstance().getPrimary()
            : ColorConfig::getInstance().getPositive();
        addString("Save", saveButtonX + saveButtonWidth / 2.0f, buttonRowY, Justify::CENTER,
            Fonts::getNormal(), saveTextColor, dim.fontSize);
    } else {
        // Nothing to save: grayed out, not clickable (no click region -> no hover/click).
        {
            SPluginQuad_t bgQuad;
            float bgX = saveButtonX, bgY = buttonRowY;
            applyOffset(bgX, bgY);
            setQuadPositions(bgQuad, bgX, bgY, saveButtonWidth, dim.lineHeightNormal);
            bgQuad.m_iSprite = SpriteIndex::SOLID_COLOR;
            bgQuad.m_ulColor = PluginUtils::applyOpacity(ColorConfig::getInstance().getMuted(), 64.0f / 255.0f);
            m_quads.push_back(bgQuad);
        }
        addString("Saved", saveButtonX + saveButtonWidth / 2.0f, buttonRowY, Justify::CENTER,
            Fonts::getNormal(), ColorConfig::getInstance().getMuted(), dim.fontSize);
    }

    // [Close] button
    float closeButtonX = saveButtonX + saveButtonWidth + buttonGap;
    size_t closeRegionIndex = m_clickRegions.size();
    m_clickRegions.push_back(ClickRegion(
        closeButtonX, buttonRowY, closeButtonWidth, dim.lineHeightNormal,
        ClickRegion::CLOSE_BUTTON, nullptr, 0, false, 0
    ));
    {
        SPluginQuad_t bgQuad;
        float bgX = closeButtonX, bgY = buttonRowY;
        applyOffset(bgX, bgY);
        setQuadPositions(bgQuad, bgX, bgY, closeButtonWidth, dim.lineHeightNormal);
        bgQuad.m_iSprite = SpriteIndex::SOLID_COLOR;
        bgQuad.m_ulColor = (m_hoveredRegionIndex == static_cast<int>(closeRegionIndex))
            ? ColorConfig::getInstance().getAccent()
            : PluginUtils::applyOpacity(ColorConfig::getInstance().getAccent(), 128.0f / 255.0f);
        m_quads.push_back(bgQuad);
    }
    unsigned long closeTextColor = (m_hoveredRegionIndex == static_cast<int>(closeRegionIndex))
        ? ColorConfig::getInstance().getPrimary()
        : ColorConfig::getInstance().getAccent();
    addString("Close", closeButtonX + closeButtonWidth / 2.0f, buttonRowY, Justify::CENTER,
        Fonts::getNormal(), closeTextColor, dim.fontSize);

    // [Reset <TabName>] button - bottom left corner
    float resetTabButtonY = buttonRowY;
    char resetTabButtonText[32];
    snprintf(resetTabButtonText, sizeof(resetTabButtonText), "Reset %s", getTabName(m_activeTab));
    int resetTabButtonChars = static_cast<int>(strlen(resetTabButtonText));
    float resetTabButtonWidth = PluginUtils::calculateMonospaceTextWidth(resetTabButtonChars + 2, dim.fontSize);  // +1 char padding each side
    float resetTabButtonX = contentStartX;

    // Add click region first for hover check
    size_t resetTabRegionIndex = m_clickRegions.size();
    m_clickRegions.push_back(ClickRegion(
        resetTabButtonX, resetTabButtonY, resetTabButtonWidth, dim.lineHeightNormal,
        ClickRegion::RESET_TAB_BUTTON, nullptr
    ));

    // Reset Tab button background
    {
        SPluginQuad_t bgQuad;
        float bgX = resetTabButtonX, bgY = resetTabButtonY;
        applyOffset(bgX, bgY);
        setQuadPositions(bgQuad, bgX, bgY, resetTabButtonWidth, dim.lineHeightNormal);
        bgQuad.m_iSprite = SpriteIndex::SOLID_COLOR;
        bgQuad.m_ulColor = (m_hoveredRegionIndex == static_cast<int>(resetTabRegionIndex))
            ? ColorConfig::getInstance().getAccent()
            : PluginUtils::applyOpacity(ColorConfig::getInstance().getAccent(), 128.0f / 255.0f);
        m_quads.push_back(bgQuad);
    }

    // Reset Tab button text - PRIMARY when hovered, ACCENT when not (purple on purple)
    unsigned long resetTabTextColor = (m_hoveredRegionIndex == static_cast<int>(resetTabRegionIndex))
        ? ColorConfig::getInstance().getPrimary()
        : ColorConfig::getInstance().getAccent();
    addString(resetTabButtonText, resetTabButtonX + resetTabButtonWidth / 2.0f, resetTabButtonY, Justify::CENTER,
        Fonts::getNormal(), resetTabTextColor, dim.fontSize);

    // Version + update status display - bottom right corner
    {
        float versionY = buttonRowY;
        float rightEdgeX = contentStartX + panelWidth - dim.paddingH - dim.paddingH;

        // Build version/status string based on update state
        char versionStr[64];
        unsigned long versionColor = ColorConfig::getInstance().getMuted();

        if (!UpdateChecker::getInstance().isEnabled()) {
            // Updates disabled - just show version
            snprintf(versionStr, sizeof(versionStr), "v%s", PluginConstants::PLUGIN_VERSION);
        } else {
            // Query UpdateChecker directly for current status (no duplicate state)
            UpdateChecker::Status status = UpdateChecker::getInstance().getStatus();

            switch (status) {
                case UpdateChecker::Status::IDLE:
                    snprintf(versionStr, sizeof(versionStr), "v%s", PluginConstants::PLUGIN_VERSION);
                    break;
                case UpdateChecker::Status::CHECKING:
                    snprintf(versionStr, sizeof(versionStr), "Checking...");
                    // Keep muted color (same as default)
                    break;
                case UpdateChecker::Status::UP_TO_DATE:
                    snprintf(versionStr, sizeof(versionStr), "v%s up-to-date", PluginConstants::PLUGIN_VERSION);
                    versionColor = ColorConfig::getInstance().getMuted();
                    break;
                case UpdateChecker::Status::UPDATE_AVAILABLE: {
                    // Get latest version directly from UpdateChecker
                    std::string latestVersion = UpdateChecker::getInstance().getLatestVersion();
                    // Show "installed" if downloader completed, otherwise "available"
                    if (UpdateDownloader::getInstance().getState() == UpdateDownloader::State::READY) {
                        snprintf(versionStr, sizeof(versionStr), "%s installed!", latestVersion.c_str());
                    } else {
                        // Drawn as a clickable green button below (like Reset / Check Now);
                        // the box + padding convey "button", no brackets needed.
                        snprintf(versionStr, sizeof(versionStr), "%s available!", latestVersion.c_str());
                    }
                    versionColor = ColorConfig::getInstance().getPositive();
                    break;
                }
                case UpdateChecker::Status::CHECK_FAILED:
                    snprintf(versionStr, sizeof(versionStr), "v%s", PluginConstants::PLUGIN_VERSION);
                    // Silent fail - just show version in muted
                    break;
            }
        }

        // versionWidth is the bare text width — used by the plain-text path below for
        // right-alignment. The clickable "available!" button adds +1 char of padding on
        // each side so its green box doesn't hug the text (matching the Reset button).
        float versionWidth = PluginUtils::calculateMonospaceTextWidth(static_cast<int>(strlen(versionStr)), dim.fontSize);
        float buttonWidth = PluginUtils::calculateMonospaceTextWidth(static_cast<int>(strlen(versionStr)) + 2, dim.fontSize);
        float versionX = rightEdgeX - buttonWidth;

        // Check if update is available and not yet installed. Gate on isEnabled() too: a
        // stale m_status (e.g. UPDATE_AVAILABLE from a prior check) survives when updates are
        // disabled — and the string above already collapses to the plain version in that case.
        // Without this gate the button branch would still draw the green "available" box/click
        // region around the plain version string (mixed state). Tying both to isEnabled() keeps
        // them consistent.
        bool updatesEnabled = UpdateChecker::getInstance().isEnabled();
        bool isUpdateAvailable = updatesEnabled &&
                           (UpdateChecker::getInstance().getStatus() == UpdateChecker::Status::UPDATE_AVAILABLE);
        bool isInstalled = (isUpdateAvailable &&
                           UpdateDownloader::getInstance().getState() == UpdateDownloader::State::READY);

        // Always add click region for easter egg (and update navigation when available)
        size_t regionIndex = m_clickRegions.size();
        ClickRegion versionRegion;
        versionRegion.type = ClickRegion::VERSION_CLICK;
        versionRegion.y = versionY;
        versionRegion.height = dim.lineHeightNormal;

        // If update is available (not yet installed), show as clickable button
        if (isUpdateAvailable && !isInstalled) {
            versionRegion.x = versionX;
            versionRegion.width = buttonWidth;
            m_clickRegions.push_back(versionRegion);

            bool isHovered = m_hoveredRegionIndex == static_cast<int>(regionIndex);

            // Button background
            SPluginQuad_t bgQuad;
            float bgX = versionX, bgY = versionY;
            applyOffset(bgX, bgY);
            setQuadPositions(bgQuad, bgX, bgY, buttonWidth, dim.lineHeightNormal);
            bgQuad.m_iSprite = SpriteIndex::SOLID_COLOR;
            bgQuad.m_ulColor = isHovered ? ColorConfig::getInstance().getPositive()
                : PluginUtils::applyOpacity(ColorConfig::getInstance().getPositive(), 0.5f);
            m_quads.push_back(bgQuad);

            // Text color: positive (green) when unhovered for contrast, primary when hovered
            versionColor = isHovered ? ColorConfig::getInstance().getPrimary()
                : ColorConfig::getInstance().getPositive();

            // Draw centered in button
            float textX = versionX + buttonWidth * 0.5f;
            addString(versionStr, textX, versionY, Justify::CENTER,
                Fonts::getNormal(), versionColor, dim.fontSize);
        } else {
            // Regular text (not a button) - right aligned, but still clickable for easter egg
            float textX = rightEdgeX - versionWidth;
            versionRegion.x = textX;
            versionRegion.width = versionWidth;
            m_clickRegions.push_back(versionRegion);

            addString(versionStr, textX, versionY, Justify::LEFT,
                Fonts::getNormal(), versionColor, dim.fontSize);
        }
    }
}

const char* SettingsHud::getTabName(int tabIndex) const {
    switch (tabIndex) {
        case TAB_GENERAL:     return "General";
        case TAB_APPEARANCE:  return "Appearance";
        case TAB_STANDINGS:   return "Standings";
        case TAB_MAP:         return "Map";
        case TAB_LAP_LOG:     return "Lap Log";
        case TAB_FRIENDS:     return "Friends";
        case TAB_SESSION_CHARTS: return "Charts";
        case TAB_IDEAL_LAP:   return "Ideal Lap";
        case TAB_TELEMETRY:   return "Telemetry";
        case TAB_PERFORMANCE: return "Performance";
        case TAB_PITBOARD:    return "Pitboard";
        case TAB_SESSION:     return "Session";
        case TAB_RECORDS:     return "Records";
        case TAB_TIMING:      return "Timing";
        case TAB_GAP_BAR:     return "Gap Bar";
        case TAB_WIDGETS:     return "Widgets";
        case TAB_NOTICES:     return "Notices";
        case TAB_RUMBLE:      return "Rumble";
        case TAB_HOTKEYS:     return "Hotkeys";
        case TAB_RIDERS:      return "Riders";
        case TAB_UPDATES:     return "Updates";
        case TAB_RADAR:       return "Radar";
        case TAB_FMX:         return "FMX";
        case TAB_STATS:       return "Stats";
        case TAB_EVENT_LOG:   return "Event Log";
        case TAB_HELMET:      return "Helmet";
        case TAB_DIRECTOR:    return "Director";
        default:              return "Unknown";
    }
}

bool SettingsHud::isTabAvailable(int tabId) const {
    if (tabId < 0 || tabId >= TAB_COUNT) return false;
    // Game-gated tabs: selectable only when their backing HUD is registered on this build
    // (mirrors the skips in the tab-list render loop - keep the two in lockstep).
    if (tabId == TAB_RECORDS && !m_records) return false;
    if (tabId == TAB_FMX && !m_fmxHud) return false;
    if (tabId == TAB_FRIENDS && !m_friends) return false;
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
            if (isTabAvailable(t)) m_activeTab = t;
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
                case ClickRegion::GAP_COLUMN_TOGGLE:
                case ClickRegion::GAP_SCOPE_TOGGLE:
                    return "standings.gap_mode";
                case ClickRegion::GAP_REFERENCE_TOGGLE:
                case ClickRegion::GAP_REFERENCE_BACK:
                    return "standings.gap_reference";
                case ClickRegion::FILTER_DNS_TOGGLE:
                    return "standings.filter_dns";
                case ClickRegion::HEADERS_TOGGLE:
                    return "standings.headers";
                case ClickRegion::ANIMATION_MODE_UP:
                case ClickRegion::ANIMATION_MODE_DOWN:
                    return "standings.animate_positions";
                case ClickRegion::NAME_MODE_UP:
                case ClickRegion::NAME_MODE_DOWN:
                    return "standings.col_name";
                default:
                    break;
            }
            break;

        case TAB_MAP:
            switch (type) {
                case ClickRegion::MAP_ROTATION_TOGGLE:
                    return "map.rotation";
                case ClickRegion::MAP_OUTLINE_TOGGLE:
                    return "map.outline";
                case ClickRegion::MAP_COLORIZE_UP:
                case ClickRegion::MAP_COLORIZE_DOWN:
                    return "map.colorize";
                case ClickRegion::MAP_TRACK_WIDTH_UP:
                case ClickRegion::MAP_TRACK_WIDTH_DOWN:
                    return "map.track_width";
                case ClickRegion::MAP_LABEL_MODE_UP:
                case ClickRegion::MAP_LABEL_MODE_DOWN:
                    return "map.labels";
                case ClickRegion::MAP_RANGE_UP:
                case ClickRegion::MAP_RANGE_DOWN:
                    return "map.range";
                case ClickRegion::MAP_RIDER_SHAPE_UP:
                case ClickRegion::MAP_RIDER_SHAPE_DOWN:
                    return "map.rider_shape";
                case ClickRegion::MAP_MARKER_SCALE_UP:
                case ClickRegion::MAP_MARKER_SCALE_DOWN:
                    return "map.marker_scale";
                case ClickRegion::MAP_DETAIL_UP:
                case ClickRegion::MAP_DETAIL_DOWN:
                    return "map.detail";
                default:
                    break;
            }
            break;

        case TAB_RADAR:
            switch (type) {
                case ClickRegion::RADAR_RANGE_UP:
                case ClickRegion::RADAR_RANGE_DOWN:
                    return "radar.range";
                case ClickRegion::RADAR_COLORIZE_UP:
                case ClickRegion::RADAR_COLORIZE_DOWN:
                    return "radar.colorize";
                case ClickRegion::RADAR_ALERT_DISTANCE_UP:
                case ClickRegion::RADAR_ALERT_DISTANCE_DOWN:
                    return "radar.alert_distance";
                case ClickRegion::RADAR_LABEL_MODE_UP:
                case ClickRegion::RADAR_LABEL_MODE_DOWN:
                    return "radar.labels";
                case ClickRegion::RADAR_MODE_UP:
                case ClickRegion::RADAR_MODE_DOWN:
                    return "radar.mode";
                case ClickRegion::RADAR_RIDER_SHAPE_UP:
                case ClickRegion::RADAR_RIDER_SHAPE_DOWN:
                    return "radar.rider_shape";
                case ClickRegion::RADAR_MARKER_SCALE_UP:
                case ClickRegion::RADAR_MARKER_SCALE_DOWN:
                    return "radar.marker_scale";
                default:
                    break;
            }
            break;

        case TAB_LAP_LOG:
            switch (type) {
                case ClickRegion::LAP_LOG_ROW_COUNT_UP:
                case ClickRegion::LAP_LOG_ROW_COUNT_DOWN:
                    return "lap_log.rows";
                case ClickRegion::LAP_LOG_ORDER_UP:
                case ClickRegion::LAP_LOG_ORDER_DOWN:
                    return "lap_log.order";
                case ClickRegion::LAP_LOG_GAP_ROW_TOGGLE:
                    return "lap_log.gap_row";
                default:
                    break;
            }
            break;

        case TAB_FRIENDS:
            switch (type) {
                case ClickRegion::FRIENDS_ROW_COUNT_UP:
                case ClickRegion::FRIENDS_ROW_COUNT_DOWN:
                    return "friends.rows";
                case ClickRegion::FRIENDS_HEADERS_TOGGLE:
                    return "friends.headers";
                case ClickRegion::FRIENDS_SHOW_MODE_UP:
                case ClickRegion::FRIENDS_SHOW_MODE_DOWN:
                    return "friends.showmode";
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
                case ClickRegion::TIMING_DISPLAY_MODE_UP:
                case ClickRegion::TIMING_DISPLAY_MODE_DOWN:
                    return "timing.show";
                case ClickRegion::TIMING_DURATION_UP:
                case ClickRegion::TIMING_DURATION_DOWN:
                    return "timing.freeze";
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
                case ClickRegion::GAPBAR_FREEZE_UP:
                case ClickRegion::GAPBAR_FREEZE_DOWN:
                    return "gap_bar.freeze";
                case ClickRegion::GAPBAR_MARKER_MODE_UP:
                case ClickRegion::GAPBAR_MARKER_MODE_DOWN:
                    return "gap_bar.marker_mode";
                case ClickRegion::GAPBAR_ICON_UP:
                case ClickRegion::GAPBAR_ICON_DOWN:
                    return "gap_bar.icon";
                case ClickRegion::GAPBAR_GAP_TEXT_TOGGLE:
                    return "gap_bar.show_gap";
                case ClickRegion::GAPBAR_GAP_BAR_TOGGLE:
                    return "gap_bar.show_gap_bar";
                case ClickRegion::GAPBAR_RANGE_UP:
                case ClickRegion::GAPBAR_RANGE_DOWN:
                    return "gap_bar.range";
                case ClickRegion::GAPBAR_WIDTH_UP:
                case ClickRegion::GAPBAR_WIDTH_DOWN:
                    return "gap_bar.width";
                case ClickRegion::GAPBAR_MARKER_SCALE_UP:
                case ClickRegion::GAPBAR_MARKER_SCALE_DOWN:
                    return "gap_bar.marker_scale";
                case ClickRegion::GAPBAR_LABEL_MODE_UP:
                case ClickRegion::GAPBAR_LABEL_MODE_DOWN:
                    return "gap_bar.labels";
                default:
                    break;
            }
            break;

        case TAB_RECORDS:
            switch (type) {
                case ClickRegion::RECORDS_COUNT_UP:
                case ClickRegion::RECORDS_COUNT_DOWN:
                    return "records.count";
                default:
                    break;
            }
            break;

        case TAB_PITBOARD:
            switch (type) {
                case ClickRegion::PITBOARD_SHOW_MODE_UP:
                case ClickRegion::PITBOARD_SHOW_MODE_DOWN:
                    return "pitboard.show_mode";
                case ClickRegion::PITBOARD_GAP_MODE_UP:
                case ClickRegion::PITBOARD_GAP_MODE_DOWN:
                    return "pitboard.gap_compare";
                default:
                    break;
            }
            break;

        case TAB_PERFORMANCE:
        case TAB_TELEMETRY:
            switch (type) {
                case ClickRegion::DISPLAY_MODE_UP:
                case ClickRegion::DISPLAY_MODE_DOWN:
                    return activeTab == TAB_PERFORMANCE ? "performance.display" : "telemetry.display";
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

        case TAB_NOTICES:
            switch (type) {
                case ClickRegion::NOTICES_DURATION_UP:
                case ClickRegion::NOTICES_DURATION_DOWN:
                    return "notices.duration";
                default:
                    break;
            }
            break;

        default:
            break;
    }

    return "";  // No tooltip for this region
}
