// ============================================================================
// core/settings_keys.h
// Centralized INI key-name constants and INI-only setting descriptors for the
// settings layer. Extracted from settings_manager.cpp so the serializer
// (settings_serde), the per-HUD registry (settings_hud_registry), and the
// SettingsManager translation units can all share one definition.
//
// Everything here is constexpr (internal linkage per TU) — safe to include
// widely. Consumers do `using namespace Settings;` so existing `Keys::...`
// and `IniOnly::...` references resolve unchanged.
// ============================================================================
#pragma once

#include "../game/game_config.h"

namespace Settings {

    // ========================================================================
    // Settings Key Constants
    // Centralizes all INI file keys to prevent typos and enable refactoring
    // ========================================================================
    namespace Keys {
        // Base HUD properties (shared across all HUDs)
        namespace Base {
            constexpr const char* VISIBLE = "visible";
            constexpr const char* SHOW_TITLE = "showTitle";
            constexpr const char* SHOW_BG_TEXTURE = "showBackgroundTexture";
            constexpr const char* TEXTURE_VARIANT = "textureVariant";
            constexpr const char* BG_OPACITY = "backgroundOpacity";
            constexpr const char* SCALE = "scale";
            constexpr const char* OFFSET_X = "offsetX";
            constexpr const char* OFFSET_Y = "offsetY";
            // Companion-surface instance (decoupled on/off + position). Only written
            // when the HUD's companion has been configured (diverged from the game).
            constexpr const char* COMPANION_CONFIGURED = "companionConfigured";
            constexpr const char* COMPANION_VISIBLE = "companionVisible";
            constexpr const char* COMPANION_X = "companionX";
            constexpr const char* COMPANION_Y = "companionY";
        }

        // Shared keys used by multiple HUDs
        namespace Common {
            constexpr const char* ENABLED_COLUMNS = "enabledColumns";
            constexpr const char* ENABLED_ROWS = "enabledRows";
            constexpr const char* ENABLED_ELEMENTS = "enabledElements";
            constexpr const char* DISPLAY_MODE = "displayMode";
            constexpr const char* LABEL_MODE = "labelMode";
            constexpr const char* RIDER_COLOR_MODE = "riderColorMode";
            constexpr const char* RIDER_SHAPE = "riderShape";
        }

        // StandingsHud-specific keys
        namespace Standings {
            constexpr const char* DISPLAY_ROW_COUNT = "displayRowCount";
            constexpr const char* GAP_MODE = "gapMode";
            constexpr const char* POSGAIN_MODE = "posGainMode";
            constexpr const char* GAP_REFERENCE_MODE = "gapReferenceMode";
            constexpr const char* ANIMATION_MODE = "animationMode";
            constexpr const char* SHOW_HEADERS = "showHeaders";
            constexpr const char* SHOW_SESSION_INFO = "showSessionInfo";
            constexpr const char* LIVE_GAPS = "liveGaps";
        }

        // MapHud-specific keys
        namespace Map {
            constexpr const char* ROTATE_TO_PLAYER = "rotateToPlayer";
            constexpr const char* SHOW_OUTLINE = "showOutline";
            constexpr const char* TRACK_WIDTH_SCALE = "trackWidthScale";
            constexpr const char* TRACK_LINE_WIDTH = "trackLineWidthMeters";  // Legacy key (meters)
            constexpr const char* ANCHOR_POINT = "anchorPoint";
            constexpr const char* ANCHOR_X = "anchorX";
            constexpr const char* ANCHOR_Y = "anchorY";
            constexpr const char* ZOOM_ENABLED = "zoomEnabled";
            constexpr const char* ZOOM_DISTANCE = "zoomDistance";
            constexpr const char* COLORIZE_RIDERS = "colorizeRiders";  // Legacy key
        }

        // RadarHud-specific keys
        namespace Radar {
            constexpr const char* RADAR_RANGE = "radarRange";
            constexpr const char* RADAR_MODE = "radarMode";
            constexpr const char* ALERT_DISTANCE = "alertDistance";
        }

    #if GAME_HAS_RECORDS_PROVIDER
        // RecordsHud-specific keys
        namespace Records {
            constexpr const char* PROVIDER = "provider";
            constexpr const char* RECORDS_TO_SHOW = "recordsToShow";
        }
    #endif

        // LapLogHud-specific keys
        namespace LapLog {
            constexpr const char* MAX_DISPLAY_LAPS = "maxDisplayLaps";
        }

        // TimingHud-specific keys
        namespace Timing {
            constexpr const char* TIME_MODE = "timeMode";
            constexpr const char* GAP_MODE = "gapMode";
            constexpr const char* DISPLAY_DURATION = "displayDuration";
            constexpr const char* PRIMARY_GAP = "primaryGap";
            constexpr const char* SECONDARY_GAPS = "secondaryGaps";
        }

        // SpeedWidget-specific keys
        namespace Speed {
            constexpr const char* SPEED_UNIT = "speedUnit";
        }

        // FuelWidget-specific keys
        namespace Fuel {
            constexpr const char* FUEL_UNIT = "fuelUnit";
        }

        // FmxHud-specific keys
        namespace Fmx {
            constexpr const char* ENABLED_ROWS = "enabledRows";
            constexpr const char* MAX_CHAIN_DISPLAY_ROWS = "maxChainDisplayRows";
            constexpr const char* SHOW_DEBUG_LOGGING = "showDebugLogging";
            // Prefix for per-trick disable flags (e.g. trickEnabled_Pivot=0).
            // The suffix comes from Fmx::getTrickIniKey(); L/R variants share
            // one key (e.g. PivotLeft + PivotRight both serialize as "Pivot").
            // INI-only, no UI.
            constexpr const char* TRICK_ENABLED_PREFIX = "trickEnabled_";
        }

        // StatsHud-specific keys
        namespace Stats {
            constexpr const char* VISIBILITY_MODE = "visibilityMode";
            constexpr const char* SHOW_LAP = "showLap";
            constexpr const char* SHOW_SESSION = "showSession";
            constexpr const char* SHOW_ALLTIME = "showAllTime";
        }

        // ====================================================================
        // Named keys for bitmask fields (replaces positional bit storage)
        // These are stable identifiers that won't break when options are added
        // ====================================================================

        // StandingsHud columns
        namespace StandingsCols {
            constexpr const char* TRACKED = "col_tracked";
            constexpr const char* POS = "col_pos";
            constexpr const char* POSGAIN = "col_posgain";
            constexpr const char* RACENUM = "col_racenum";
            constexpr const char* NAME = "col_name";
            constexpr const char* BIKE = "col_bike";
            constexpr const char* PENALTY = "col_penalty";
            constexpr const char* BEST_LAP = "col_best_lap";
            constexpr const char* LAST_LAP = "col_last_lap";
            constexpr const char* GAP = "col_gap";
        }

    #if GAME_HAS_RECORDS_PROVIDER
        // RecordsHud columns
        namespace RecordsCols {
            constexpr const char* POS = "col_pos";
            constexpr const char* RIDER = "col_rider";
            constexpr const char* BIKE = "col_bike";
            constexpr const char* LAPTIME = "col_laptime";
            constexpr const char* SECTORS = "col_sectors";  // Combined S1+S2+S3
            constexpr const char* DATE = "col_date";
        }
    #endif

        // LapLogHud columns (only sectors are configurable)
        namespace LapLogCols {
            constexpr const char* SECTORS = "col_sectors";
        }

        // IdealLapHud rows
        namespace IdealLapRows {
            constexpr const char* SECTORS = "row_sectors";
            constexpr const char* LAPS = "row_laps";
        }

        // PitboardHud rows
        namespace PitboardRows {
            constexpr const char* RIDER_ID = "row_rider_id";
            constexpr const char* SESSION = "row_session";
            constexpr const char* POSITION = "row_position";
            constexpr const char* TIME = "row_time";
            constexpr const char* LAP = "row_lap";
            constexpr const char* LAST_LAP = "row_last_lap";
            constexpr const char* GAP = "row_gap";
        }

        // SpeedWidget rows
        namespace SpeedRows {
            constexpr const char* UNITS = "row_units";
        }

        // FuelWidget rows
        namespace FuelRows {
            constexpr const char* FUEL = "row_fuel";
            constexpr const char* USED = "row_used";
            constexpr const char* AVG = "row_avg";
            constexpr const char* EST = "row_est";
        }

        // SessionHud rows (row_type retired — session type now shows in the StandingsHud)
        namespace SessionRows {
            constexpr const char* TRACK = "row_track";
            constexpr const char* FORMAT = "row_format";
            constexpr const char* SERVER = "row_server";
            constexpr const char* WEATHER = "row_weather";
        }

        // SessionHud settings
        namespace Session {
            constexpr const char* SHOW_ICONS = "showIcons";
        }

        // LeanWidget rows
        namespace LeanRows {
            constexpr const char* ARC = "row_arc";
            constexpr const char* LEAN_VALUE = "row_lean_value";
            constexpr const char* STEER_BAR = "row_steer_bar";
            constexpr const char* STEER_VALUE = "row_steer_value";
        }

    #if GAME_HAS_TYRE_TEMP
        // TyreTempWidget rows
        namespace TyreTempRows {
            constexpr const char* BARS = "row_bars";
            constexpr const char* VALUES = "row_values";
        }
    #endif

    #if GAME_HAS_ECU
        // EcuWidget chips
        namespace EcuRows {
            constexpr const char* MAP = "row_map";
            constexpr const char* TC = "row_tc";
            constexpr const char* EB = "row_eb";
            constexpr const char* AW = "row_aw";
        }
    #endif

        // BarsWidget columns
        namespace BarsCols {
            constexpr const char* THROTTLE = "col_throttle";
            constexpr const char* BRAKE = "col_brake";
            constexpr const char* CLUTCH = "col_clutch";
            constexpr const char* RPM = "col_rpm";
            constexpr const char* SUSPENSION = "col_suspension";
            constexpr const char* FUEL = "col_fuel";
            constexpr const char* ENGINE_TEMP = "col_engine_temp";
            constexpr const char* WATER_TEMP = "col_water_temp";
        }

        // EventLogHud events
        namespace EventLog {
            constexpr const char* SESSION_STARTED = "event_session_started";
            constexpr const char* SESSION_STATE = "event_session_state";
            constexpr const char* FASTEST_LAP = "event_fastest_lap";
            constexpr const char* PENALTY = "event_penalty";
            constexpr const char* PENALTY_CLEAR = "event_penalty_clear";
            constexpr const char* RIDER_RETIRED = "event_rider_retired";
            constexpr const char* RIDER_DSQ = "event_rider_dsq";
            constexpr const char* RIDER_DNS = "event_rider_dns";
            constexpr const char* OVERTIME = "event_overtime";
            constexpr const char* FINAL_LAP = "event_final_lap";
            constexpr const char* RIDER_FINISHED = "event_rider_finished";
            constexpr const char* LEADER_CHANGE = "event_leader_change";
            constexpr const char* PIT_ENTRY = "event_pit_entry";
            constexpr const char* PIT_EXIT = "event_pit_exit";
            constexpr const char* DIRECTOR = "event_director";
        }

        // NoticesHud notices
        namespace Notices {
            constexpr const char* WRONG_WAY = "notice_wrong_way";
            constexpr const char* BLUE_FLAG = "notice_blue_flag";
            constexpr const char* LAST_LAP = "notice_last_lap";
            constexpr const char* FINISHED = "notice_finished";
            constexpr const char* ALLTIME_PB = "notice_alltime_pb";
            constexpr const char* FASTEST_LAP = "notice_fastest_lap";
            constexpr const char* SESSION_PB = "notice_session_pb";
            constexpr const char* DEFAULT_SETUP = "notice_default_setup";
            constexpr const char* OVERTIME = "notice_overtime";
            constexpr const char* HAZARD_STATIONARY = "notice_hazard_stationary";
            constexpr const char* HAZARD_WRONG_WAY = "notice_hazard_wrong_way";
        }

        // TelemetryHud elements
        namespace TelemetryElems {
            constexpr const char* THROTTLE = "elem_throttle";
            constexpr const char* FRONT_BRAKE = "elem_front_brake";
            constexpr const char* REAR_BRAKE = "elem_rear_brake";
            constexpr const char* CLUTCH = "elem_clutch";
            constexpr const char* RPM = "elem_rpm";
            constexpr const char* FRONT_SUSP = "elem_front_susp";
            constexpr const char* REAR_SUSP = "elem_rear_susp";
            constexpr const char* GEAR = "elem_gear";
        }

        // PerformanceHud elements
        namespace PerformanceElems {
            constexpr const char* FPS = "elem_fps";
            constexpr const char* CPU = "elem_cpu";
        }

        // TimingHud gap types
        namespace TimingGaps {
            constexpr const char* TO_PB = "gap_to_pb";
            constexpr const char* TO_IDEAL = "gap_to_ideal";
            constexpr const char* TO_OVERALL = "gap_to_overall";
            constexpr const char* TO_ALLTIME = "gap_to_alltime";
            constexpr const char* TO_RECORD = "gap_to_record";
            constexpr const char* TO_LASTLAP = "gap_to_lastlap";
        }

    }

    // ========================================================================
    // INI-Only Settings (not exposed in UI)
    // These settings can only be changed by editing the INI file directly.
    // Each entry has a key and description - descriptions are written as
    // inline comments in the INI file to help users understand the setting.
    // ========================================================================
    namespace IniOnly {
        // Helper struct to pair key with its description
        struct Setting {
            const char* key;
            const char* description;
        };

        // SpeedoWidget settings
        namespace Speedo {
            constexpr Setting NEEDLE_COLOR = {"needleColor", "Needle color (hex 0xAABBGGRR, default 0xFF0000FF = red)"};
            constexpr Setting SHOW_ODOMETER = {"showOdometer", "Show total distance traveled (1 = on, default; 0 = off)"};
            constexpr Setting SHOW_TRIPMETER = {"showTripMeter", "Show trip distance, resets on session end (1 = on; 0 = off, default)"};
        }

        // TachoWidget settings
        namespace Tacho {
            constexpr Setting NEEDLE_COLOR = {"needleColor", "Needle color (hex 0xAABBGGRR, default 0xFF0000FF = red)"};
        }

        // SpeedWidget settings
        namespace Speed {
            constexpr Setting ROW_UNITS = {"row_units", "Show speed units (1 = on, default; 0 = off)"};
        }

        // GearWidget settings
        namespace Gear {
            constexpr Setting SHOW_SHIFT_COLOR = {"showShiftColor", "Red gear text at shift RPM (1 = on, default; 0 = off)"};
            constexpr Setting SHOW_LIMITER_CIRCLE = {"showLimiterCircle", "Circle indicator at limiter RPM (1 = on, default; 0 = off)"};
        }

        // ClockWidget settings
        namespace Clock {
            constexpr Setting SHOW_UTC = {"showUtc", "Show UTC time as secondary display (1 = on; 0 = off, default)"};
            constexpr Setting UTC_ON_TOP = {"utcOnTop", "Show UTC on top, local on bottom (1 = on; 0 = off, default)"};
        }

        // RumbleHud settings
        namespace Rumble {
            constexpr Setting SHOW_MAX_MARKERS = {"showMaxMarkers", "Show max value markers (1 = on; 0 = off, default)"};
            constexpr Setting MAX_MARKER_LINGER_FRAMES = {"maxMarkerLingerFrames", "Frames to show max markers (default 60, ~1s at 60fps)"};
        }

        // LeanWidget settings
        namespace Lean {
            constexpr Setting ARC_FILL_COLOR = {"arcFillColor", "Arc fill color (hex 0xAABBGGRR, default 0xFFFFFFFF = white)"};
            constexpr Setting ROW_ARC = {"row_arc", "Show lean angle arc (1 = on, default; 0 = off)"};
            constexpr Setting ROW_LEAN_VALUE = {"row_lean_value", "Show lean angle value (1 = on, default; 0 = off)"};
            constexpr Setting ROW_STEER_BAR = {"row_steer_bar", "Show steering bar (1 = on, default; 0 = off)"};
            constexpr Setting ROW_STEER_VALUE = {"row_steer_value", "Show steering value (1 = on; 0 = off, default)"};
            constexpr Setting SHOW_MAX_MARKERS = {"showMaxMarkers", "Show max lean markers (1 = on, default; 0 = off)"};
            constexpr Setting MAX_MARKER_LINGER_FRAMES = {"maxMarkerLingerFrames", "Frames to show max markers (default 60, ~1s at 60fps)"};
        }

        // GForceWidget settings
        namespace GForce {
            constexpr Setting MAX_SCALE = {"maxScale", "Gauge full-scale in g (outer ring; must be > 0.1, default 20)"};
            constexpr Setting SHOW_MAX_TEXT = {"showMaxText", "Show recorded max-g text inside the ring (1 = on, default; 0 = off)"};
            constexpr Setting SHOW_MAX_MARKER = {"showMaxMarker", "Show lingering max-position marker on gauge (1 = on, default; 0 = off)"};
            constexpr Setting MAX_MARKER_LINGER_FRAMES = {"maxMarkerLingerFrames", "Frames to show max marker after peak (default 60, ~1s at 60fps)"};
        }

        // CompassWidget settings
        namespace Compass {
            constexpr Setting STYLE = {"style", "Dial style: classic (fixed ring, red/white needle, default) or modern (rotating card with centered heading)"};
        }

        // BarsWidget settings
        namespace Bars {
            constexpr Setting COL_THROTTLE = {"col_throttle", "Show throttle bar (1 = on, default; 0 = off)"};
            constexpr Setting COL_BRAKE = {"col_brake", "Show brake bar (1 = on, default; 0 = off)"};
            constexpr Setting COL_CLUTCH = {"col_clutch", "Show clutch bar (1 = on, default; 0 = off)"};
            constexpr Setting COL_RPM = {"col_rpm", "Show RPM bar (1 = on, default; 0 = off)"};
            constexpr Setting COL_SUSPENSION = {"col_suspension", "Show suspension bars (1 = on, default; 0 = off)"};
            constexpr Setting COL_FUEL = {"col_fuel", "Show fuel bar (1 = on; 0 = off, default)"};
            constexpr Setting COL_ENGINE_TEMP = {"col_engine_temp", "Show engine temp bar (1 = on, default; 0 = off)"};
            constexpr Setting COL_WATER_TEMP = {"col_water_temp", "Show water temp bar (1 = on; 0 = off, default)"};
            constexpr Setting SHOW_LABELS = {"showLabels", "Show bar labels (1 = on, default; 0 = off)"};
            constexpr Setting SHOW_MAX_MARKERS = {"showMaxMarkers", "Show max value markers (1 = on; 0 = off, default)"};
            constexpr Setting MAX_MARKER_LINGER_FRAMES = {"maxMarkerLingerFrames", "Frames to show max markers (default 60, ~1s at 60fps)"};
        }

    #if GAME_HAS_TYRE_TEMP
        // TyreTempWidget settings (GP Bikes)
        namespace TyreTemp {
            constexpr Setting COLD_THRESHOLD = {"coldThreshold", "Cold tyre threshold in Celsius (default 80)"};
            constexpr Setting HOT_THRESHOLD = {"hotThreshold", "Hot tyre threshold in Celsius (default 130)"};
            constexpr Setting ROW_BARS = {"row_bars", "Show temperature bars (1 = on, default; 0 = off)"};
            constexpr Setting ROW_VALUES = {"row_values", "Show temperature values (1 = on, default; 0 = off)"};
            constexpr Setting SHOW_LABELS = {"showLabels", "Show L/M/R and F/R labels (1 = on, default; 0 = off)"};
        }
    #endif

    #if GAME_HAS_ECU
        // EcuWidget settings (GP Bikes)
        namespace Ecu {
            constexpr Setting ROW_MAP = {"row_map", "Show engine mapping chip (1 = on, default; 0 = off)"};
            constexpr Setting ROW_TC = {"row_tc", "Show traction control chip (1 = on, default; 0 = off)"};
            constexpr Setting ROW_EB = {"row_eb", "Show engine braking chip (1 = on, default; 0 = off)"};
            constexpr Setting ROW_AW = {"row_aw", "Show anti-wheeling chip (1 = on, default; 0 = off)"};
            constexpr Setting SHOW_LABELS = {"showLabels", "Show TC/EB/AW labels inside chips (1 = on, default; 0 = off)"};
        }
    #endif

        // FuelWidget settings
        namespace Fuel {
            constexpr Setting ROW_FUEL = {"row_fuel", "Show current fuel level (1 = on, default; 0 = off)"};
            constexpr Setting ROW_USED = {"row_used", "Show fuel used (1 = on; 0 = off, default)"};
            constexpr Setting ROW_AVG = {"row_avg", "Show average consumption (1 = on, default; 0 = off)"};
            constexpr Setting ROW_EST = {"row_est", "Show estimated laps remaining (1 = on, default; 0 = off)"};
        }

        // NoticesHud settings
        namespace Notices {
            constexpr Setting WRONG_WAY = {"notice_wrong_way", "Show wrong way warning (1 = on, default; 0 = off)"};
            constexpr Setting BLUE_FLAG = {"notice_blue_flag", "Show blue flag notice (1 = on; 0 = off, default)"};
            constexpr Setting LAST_LAP = {"notice_last_lap", "Show final lap notice (1 = on, default; 0 = off)"};
            constexpr Setting FINISHED = {"notice_finished", "Show finished notice (1 = on, default; 0 = off)"};
            constexpr Setting ALLTIME_PB = {"notice_alltime_pb", "Show all-time PB notice (1 = on, default; 0 = off)"};
            constexpr Setting FASTEST_LAP = {"notice_fastest_lap", "Show fastest lap notice (online races) (1 = on, default; 0 = off)"};
            constexpr Setting SESSION_PB = {"notice_session_pb", "Show session PB notice (1 = on, default; 0 = off)"};
            constexpr Setting DEFAULT_SETUP = {"notice_default_setup", "Show warning when using default setup (1 = on, default; 0 = off)"};
            constexpr Setting OVERTIME = {"notice_overtime", "Show notice when time+laps race enters overtime (1 = on; 0 = off, default)"};
            constexpr Setting PB_DURATION = {"pbDurationMs", "Timed notice display duration in milliseconds (1000-30000, default 5000; out-of-range values are ignored)"};
            constexpr Setting HAZARD_STATIONARY = {"notice_hazard_stationary", "Show hazard notice for stationary riders ahead (1 = on, default; 0 = off)"};
            constexpr Setting HAZARD_WRONG_WAY = {"notice_hazard_wrong_way", "Show hazard notice for wrong-way riders ahead (1 = on, default; 0 = off)"};
        }

        // StandingsHud settings
        namespace Standings {
            // Now also exposed in-game (Standings tab "Top positions"); key kept here so existing INIs still load.
            constexpr Setting TOP_POSITIONS = {"topPositions", "Top positions always shown (0-10, default 3)"};
            constexpr Setting PLAYER_ROW_HIGHLIGHT = {"playerRowHighlight", "Full-row color background on the player/spectated rider's row (1 = on, default; 0 = off, falls back to accent-colored name marker)"};
            constexpr Setting PLAYER_ROW_HIGHLIGHT_BRAND = {"playerRowHighlightBrand", "When playerRowHighlight=1, use the bike brand color instead of the default accent color (1 = on; 0 = off, default)"};
            constexpr Setting LAST_LAP_COLOR = {"lastLapColorCode", "Color the Last Lap column vs your last lap (1 = on; default 0). Green = slower than you, red = faster"};
            constexpr Setting ANIMATION_DURATION_MS = {"animationDurationMs", "Position animation duration in ms (50-1000, default 500)"};
            constexpr Setting CLASSIC_LAYOUT = {"classicLayout", "Classic layout: no number plates, no brand strip (1 = classic; 0 = modern, default)"};
            constexpr Setting NAME_MODE = {"nameMode", "Rider name display mode (0=Off, 1=Short, 2=Long; default 1)"};
            constexpr Setting SHORT_NAME_CHARS = {"shortNameChars", "Visible characters in Short name mode (1-31, default 3)"};
            constexpr Setting LONG_NAME_CHARS = {"longNameChars", "Static column width in Long name mode; longer names truncate with ellipsis (4-24, default 16)"};
        }

    #if GAME_HAS_RECORDS_PROVIDER
        // RecordsHud settings
        namespace Records {
            constexpr Setting SHOW_FOOTER = {"showFooter", "Show provider info in footer (1 = on, default; 0 = off)"};
        }
    #endif

        // GamepadWidget settings
        namespace Gamepad {
            constexpr Setting TRIGGER_FILL_MODE = {"triggerFillMode", "0=fade (brightness, default), 1=fill (bottom-up)"};
        }

        // MapHud settings
        namespace Map {
            constexpr Setting LABEL_ANCHOR = {"labelAnchor", "Rider label position relative to icon (BELOW, ABOVE, LEFT, RIGHT; default BELOW)"};
            constexpr Setting DETAIL_BASELINE = {"detailBaseline",
                "Density multiplier the 20-200% Detail scale multiplies (0.25-4.0, default 1.0)"};
        }

        // SessionChartsHud settings
        namespace SessionCharts {
            constexpr Setting OUTLIER_FACTOR = {"outlierFactor", "Pace chart: laps slower than median x this factor are filtered (1.05-5.0, default 1.4)"};
        }

        // Advanced section settings
        namespace Advanced {
            constexpr Setting DEVELOPER_MODE = {"developerMode", "Enable developer options in UI (1 = on; 0 = off, default)"};
            constexpr Setting DROP_SHADOW_OFFSET_X = {"dropShadowOffsetX", "Shadow X offset as a fraction of font size (default 0.03)"};
            constexpr Setting DROP_SHADOW_OFFSET_Y = {"dropShadowOffsetY", "Shadow Y offset as a fraction of font size (default 0.04)"};
            constexpr Setting DROP_SHADOW_COLOR = {"dropShadowColor", "Shadow color (hex 0xAABBGGRR, default 0xAA000000 = semi-transparent black)"};
            constexpr Setting HOLD_REPEAT_FAST_MS = {"holdRepeatFastMs", "Hold-to-repeat max speed in ms (10-500, default 50)"};
            constexpr Setting GRID_OVERLAY = {"gridOverlay", "Draw the HUD alignment grid over the whole screen (debug aid; 1=on, 0=off default). Grid cell = the snap lattice used by gridSnapping"};
            constexpr Setting GRID_OVERLAY_MAJOR_EVERY = {"gridOverlayMajorEvery", "Emphasize every Nth grid line (thicker, major color) in the overlay (1-1000, default 10)"};
            constexpr Setting GRID_OVERLAY_COLOR = {"gridOverlayColor", "Grid overlay minor line color (hex 0xAABBGGRR, default 0x22FFFFFF)"};
            constexpr Setting GRID_OVERLAY_MAJOR_COLOR = {"gridOverlayMajorColor", "Grid overlay major (every Nth) line color (hex 0xAABBGGRR, default 0x9933CCFF)"};
            constexpr Setting CURSOR_ACTIVATION_THRESHOLD = {"cursorActivationThreshold", "Mouse travel from rest before cursor/settings button appear, normalized screen units (0.001=~2px, raise to ignore bigger bumps, lower for more responsive; 0.0001-0.5, default 0.015)"};
            constexpr Setting SEGMENT_SNAP_TO_SPLITS = {"segmentSnapToSplits", "Snap a new segment boundary onto a nearby official split (1=on default, 0=off)"};
            constexpr Setting SEGMENT_SNAP_THRESHOLD = {"segmentSnapThreshold", "Max distance to snap a segment boundary to a split, normalized fraction of the lap (0-0.25, default 0.02 = 2% of the lap)"};
            constexpr Setting HAZARD_STATIONARY_TOLERANCE = {"hazardStationaryTolerance", "Movement below this in meters = not moving (1-50, default 5.0)"};
            constexpr Setting HAZARD_STATIONARY_DURATION_MS = {"hazardStationaryDurationMs", "Time stationary before flagged in ms (1000-30000, default 2000)"};
            constexpr Setting HAZARD_WRONG_WAY_DURATION_MS = {"hazardWrongWayDurationMs", "Time going backward before flagged in ms (100-10000, default 1500)"};
            constexpr Setting HAZARD_AWARENESS_DISTANCE = {"hazardAwarenessDistance", "Distance ahead to scan for hazards in meters (10-500, default 100.0)"};
            constexpr Setting HAZARD_COOLDOWN_MS = {"hazardCooldownMs", "Hysteresis before clearing hazard state in ms (0-30000, default 1000)"};
            constexpr Setting HAZARD_GRACE_PERIOD_MS = {"hazardGracePeriodMs", "Grace period after race start in ms (0-60000, default 10000)"};
            constexpr Setting BLUE_FLAG_AWARENESS_DISTANCE = {"blueFlagAwarenessDistance", "Blue flag detection range in meters (10-500, default 100.0)"};
            constexpr Setting GAP_NOTIFY_INTERVAL_MS = {"gapNotifyIntervalMs", "Min interval between live-gap HUD refreshes in ms; 0=refresh on every change (0-1000, default 100)"};
            constexpr Setting PLUGIN_THREAD = {"pluginThread", "EXPERIMENTAL: run the plugin's callbacks + HUD render build on its own thread so hiccups never stall the game frame (1=on, 0=off default). Read once at startup"};
            constexpr Setting WEB_SERVER_PORT = {"webServerPort", "Web server port (1024-65535, default 8080)"};
            constexpr Setting WEB_SERVER_THROTTLE_MS = {"webServerThrottleMs", "Min interval between SSE pushes in ms (50-5000, default 250)"};
            constexpr Setting WEB_SERVER_BIND_ADDRESS = {"webServerBindAddress", "Bind address (default 127.0.0.1, use 0.0.0.0 for network access)"};
        }
    }

} // namespace Settings
