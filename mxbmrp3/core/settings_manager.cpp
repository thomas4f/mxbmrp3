// ============================================================================
// core/settings_manager.cpp
// Manages persistence of HUD settings (position, scale, visibility, etc.)
// Supports per-profile settings (Practice, Race, Spectate)
// ============================================================================
#include "settings_manager.h"
#include "hud_manager.h"
#include "profile_manager.h"
#include "../diagnostics/logger.h"
#include "../hud/ideal_lap_hud.h"
#include "../hud/lap_log_hud.h"
#include "../hud/friends_hud.h"
#include "../hud/lap_consistency_hud.h"
#include "../hud/standings_hud.h"
#include "../hud/performance_hud.h"
#include "../hud/telemetry_hud.h"
#include "../hud/time_widget.h"
#include "../hud/clock_widget.h"
#include "../hud/position_widget.h"
#include "../hud/lap_widget.h"
#include "../hud/session_hud.h"
#include "../hud/speed_widget.h"
#include "../hud/gear_widget.h"
#include "../hud/speedo_widget.h"
#include "../hud/tacho_widget.h"
#include "../hud/timing_hud.h"
#include "../hud/gap_bar_hud.h"
#include "../hud/bars_widget.h"
#include "../hud/version_widget.h"
#include "../hud/notices_hud.h"
#include "../hud/fuel_widget.h"
#include "../hud/settings_button_widget.h"
#include "../hud/pointer_widget.h"
#include "../hud/map_hud.h"
#include "../hud/radar_hud.h"
#include "../hud/pitboard_hud.h"
#if GAME_HAS_RECORDS_PROVIDER
#include "../hud/records_hud.h"
#endif
#include "../hud/rumble_hud.h"
#include "../hud/helmet_overlay_hud.h"
#include "../hud/benchmark_widget.h"
#include "../hud/gamepad_widget.h"
#include "../hud/lean_widget.h"
#include "../hud/gforce_widget.h"
#include "../hud/compass_widget.h"
#if GAME_HAS_TYRE_TEMP
#include "../hud/tyre_temp_widget.h"
#endif
#if GAME_HAS_ECU
#include "../hud/ecu_widget.h"
#endif
#include "../hud/fmx_hud.h"
#include "../hud/stats_hud.h"
#include "../hud/event_log_hud.h"
#include "fmx_manager.h"
#include "color_config.h"
#include "font_config.h"
#include "ui_config.h"
#include "update_checker.h"
#include "update_downloader.h"
#if GAME_HAS_DISCORD
#include "discord_manager.h"
#endif
#if GAME_HAS_STEAM_FRIENDS
#include "steam_friends_manager.h"
#endif
#if GAME_HAS_HTTP_SERVER
#include "http_server.h"
#endif
#include "xinput_reader.h"
#include "hotkey_manager.h"
#include "tracked_riders_manager.h"
#include "asset_manager.h"
#include "../game/game_config.h"
#include <fstream>
#include <sstream>
#include <array>
#include <vector>
#include <algorithm>
#include <cassert>
#include <windows.h>

namespace {
    constexpr const char* SETTINGS_SUBDIRECTORY = "mxbmrp3";
    constexpr const char* SETTINGS_FILENAME = "mxbmrp3_settings.ini";

    // Settings format version - bump this when making incompatible changes
    // Version 1: Original format with bitmasks (implicit, no version field)
    // Version 2: Named keys instead of bitmasks for columns/rows/elements
    // Version 3: String enums instead of integers for all enum settings
    // Version 4: Base sections + sparse profile sections (reduced INI size)
    constexpr int SETTINGS_VERSION = 4;

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
            constexpr const char* SEGMENT = "notice_segment";
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
            constexpr Setting NEEDLE_COLOR = {"needleColor", "Needle color (0xAARRGGBB)"};
            constexpr Setting SHOW_ODOMETER = {"showOdometer", "Show total distance traveled"};
            constexpr Setting SHOW_TRIPMETER = {"showTripMeter", "Show trip distance (resets on session end)"};
        }

        // TachoWidget settings
        namespace Tacho {
            constexpr Setting NEEDLE_COLOR = {"needleColor", "Needle color (0xAARRGGBB)"};
        }

        // SpeedWidget settings
        namespace Speed {
            constexpr Setting ROW_UNITS = {"row_units", "Show speed units"};
        }

        // GearWidget settings
        namespace Gear {
            constexpr Setting SHOW_SHIFT_COLOR = {"showShiftColor", "Red gear text at shift RPM"};
            constexpr Setting SHOW_LIMITER_CIRCLE = {"showLimiterCircle", "Circle indicator at limiter RPM"};
        }

        // ClockWidget settings
        namespace Clock {
            constexpr Setting SHOW_UTC = {"showUtc", "Show UTC time as secondary display"};
            constexpr Setting UTC_ON_TOP = {"utcOnTop", "Show UTC on top, local on bottom"};
        }

        // RumbleHud settings
        namespace Rumble {
            constexpr Setting SHOW_MAX_MARKERS = {"showMaxMarkers", "Show max value markers"};
            constexpr Setting MAX_MARKER_LINGER_FRAMES = {"maxMarkerLingerFrames", "Frames to show max markers"};
        }

        // LeanWidget settings
        namespace Lean {
            constexpr Setting ARC_FILL_COLOR = {"arcFillColor", "Arc fill color (0xAARRGGBB)"};
            constexpr Setting ROW_ARC = {"row_arc", "Show lean angle arc"};
            constexpr Setting ROW_LEAN_VALUE = {"row_lean_value", "Show lean angle value"};
            constexpr Setting ROW_STEER_BAR = {"row_steer_bar", "Show steering bar"};
            constexpr Setting ROW_STEER_VALUE = {"row_steer_value", "Show steering value"};
            constexpr Setting SHOW_MAX_MARKERS = {"showMaxMarkers", "Show max lean markers"};
            constexpr Setting MAX_MARKER_LINGER_FRAMES = {"maxMarkerLingerFrames", "Frames to show max markers"};
        }

        // GForceWidget settings
        namespace GForce {
            constexpr Setting MAX_SCALE = {"maxScale", "Gauge full-scale in g (outer ring)"};
            constexpr Setting SHOW_MAX_TEXT = {"showMaxText", "Show recorded max-g text inside the ring"};
            constexpr Setting SHOW_MAX_MARKER = {"showMaxMarker", "Show lingering max-position marker on gauge"};
            constexpr Setting MAX_MARKER_LINGER_FRAMES = {"maxMarkerLingerFrames", "Frames to show max marker after peak"};
        }

        // CompassWidget settings
        namespace Compass {
            constexpr Setting STYLE = {"style", "Dial style: classic (fixed ring, red/white needle) or modern (rotating card with centered heading)"};
        }

        // BarsWidget settings
        namespace Bars {
            constexpr Setting COL_THROTTLE = {"col_throttle", "Show throttle bar"};
            constexpr Setting COL_BRAKE = {"col_brake", "Show brake bar"};
            constexpr Setting COL_CLUTCH = {"col_clutch", "Show clutch bar"};
            constexpr Setting COL_RPM = {"col_rpm", "Show RPM bar"};
            constexpr Setting COL_SUSPENSION = {"col_suspension", "Show suspension bars"};
            constexpr Setting COL_FUEL = {"col_fuel", "Show fuel bar"};
            constexpr Setting COL_ENGINE_TEMP = {"col_engine_temp", "Show engine temp bar"};
            constexpr Setting COL_WATER_TEMP = {"col_water_temp", "Show water temp bar"};
            constexpr Setting SHOW_LABELS = {"showLabels", "Show bar labels"};
            constexpr Setting SHOW_MAX_MARKERS = {"showMaxMarkers", "Show max value markers"};
            constexpr Setting MAX_MARKER_LINGER_FRAMES = {"maxMarkerLingerFrames", "Frames to show max markers"};
        }

#if GAME_HAS_TYRE_TEMP
        // TyreTempWidget settings (GP Bikes)
        namespace TyreTemp {
            constexpr Setting COLD_THRESHOLD = {"coldThreshold", "Cold tyre threshold (Celsius)"};
            constexpr Setting HOT_THRESHOLD = {"hotThreshold", "Hot tyre threshold (Celsius)"};
            constexpr Setting ROW_BARS = {"row_bars", "Show temperature bars"};
            constexpr Setting ROW_VALUES = {"row_values", "Show temperature values"};
            constexpr Setting SHOW_LABELS = {"showLabels", "Show L/M/R and F/R labels"};
        }
#endif

#if GAME_HAS_ECU
        // EcuWidget settings (GP Bikes)
        namespace Ecu {
            constexpr Setting ROW_MAP = {"row_map", "Show engine mapping chip"};
            constexpr Setting ROW_TC = {"row_tc", "Show traction control chip"};
            constexpr Setting ROW_EB = {"row_eb", "Show engine braking chip"};
            constexpr Setting ROW_AW = {"row_aw", "Show anti-wheeling chip"};
            constexpr Setting SHOW_LABELS = {"showLabels", "Show TC/EB/AW labels inside chips"};
        }
#endif

        // FuelWidget settings
        namespace Fuel {
            constexpr Setting ROW_FUEL = {"row_fuel", "Show current fuel level"};
            constexpr Setting ROW_USED = {"row_used", "Show fuel used"};
            constexpr Setting ROW_AVG = {"row_avg", "Show average consumption"};
            constexpr Setting ROW_EST = {"row_est", "Show estimated laps remaining"};
        }

        // NoticesHud settings
        namespace Notices {
            constexpr Setting WRONG_WAY = {"notice_wrong_way", "Show wrong way warning"};
            constexpr Setting BLUE_FLAG = {"notice_blue_flag", "Show blue flag notice"};
            constexpr Setting LAST_LAP = {"notice_last_lap", "Show final lap notice"};
            constexpr Setting FINISHED = {"notice_finished", "Show finished notice"};
            constexpr Setting ALLTIME_PB = {"notice_alltime_pb", "Show all-time PB notice"};
            constexpr Setting FASTEST_LAP = {"notice_fastest_lap", "Show fastest lap notice (online races)"};
            constexpr Setting SESSION_PB = {"notice_session_pb", "Show session PB notice"};
            constexpr Setting DEFAULT_SETUP = {"notice_default_setup", "Show warning when using default setup"};
            constexpr Setting OVERTIME = {"notice_overtime", "Show notice when time+laps race enters overtime"};
            constexpr Setting PB_DURATION = {"pbDurationMs", "Timed notice display duration in milliseconds (PB notices)"};
            constexpr Setting HAZARD_STATIONARY = {"notice_hazard_stationary", "Show hazard notice for stationary riders ahead"};
            constexpr Setting HAZARD_WRONG_WAY = {"notice_hazard_wrong_way", "Show hazard notice for wrong-way riders ahead"};
            constexpr Setting SEGMENT = {"notice_segment", "Show segment timer action notices (start/end set, cleared)"};
        }

        // StandingsHud settings
        namespace Standings {
            constexpr Setting TOP_POSITIONS = {"topPositions", "Top positions always shown (0-10)"};
            constexpr Setting PLAYER_ROW_HIGHLIGHT = {"playerRowHighlight", "Full-row color background on the player/spectated rider's row (1 = on, default; 0 = off, falls back to accent-colored name marker)"};
            constexpr Setting PLAYER_ROW_HIGHLIGHT_BRAND = {"playerRowHighlightBrand", "When playerRowHighlight=1, use the bike brand color instead of the default accent color"};
            constexpr Setting LAST_LAP_COLOR = {"lastLapColorCode", "Color the Last Lap column vs your last lap (1 = on; default 0). Green = slower than you, red = faster"};
            constexpr Setting ANIMATION_DURATION_MS = {"animationDurationMs", "Position animation duration in ms (50-1000)"};
            constexpr Setting CLASSIC_LAYOUT = {"classicLayout", "Classic layout: no number plates, no brand strip (0 = modern)"};
            constexpr Setting NAME_MODE = {"nameMode", "Rider name display mode (0=Off, 1=Short, 2=Long)"};
            constexpr Setting SHORT_NAME_CHARS = {"shortNameChars", "Visible characters in Short name mode (1-31, default 3)"};
            constexpr Setting LONG_NAME_CHARS = {"longNameChars", "Static column width in Long name mode; longer names truncate with ellipsis (4-24, default 16)"};
        }

#if GAME_HAS_RECORDS_PROVIDER
        // RecordsHud settings
        namespace Records {
            constexpr Setting SHOW_FOOTER = {"showFooter", "Show provider info in footer"};
        }
#endif

        // GamepadWidget settings
        namespace Gamepad {
            constexpr Setting TRIGGER_FILL_MODE = {"triggerFillMode", "0=fade (brightness), 1=fill (bottom-up)"};
        }

        // MapHud settings
        namespace Map {
            constexpr Setting LABEL_ANCHOR = {"labelAnchor", "Rider label position relative to icon (BELOW, ABOVE, LEFT, RIGHT)"};
        }

        // LapConsistencyHud settings
        namespace LapConsistency {
            constexpr Setting CONSISTENCY_SCALE_FACTOR = {"consistencyScaleFactor", "Scoring strictness: higher = stricter, lower = more forgiving (default 20)"};
            constexpr Setting TREND_THRESHOLD_PERCENT = {"trendThresholdPercent", "Sensitivity for trend detection as % of lap time (default 0.5)"};
        }

        // Advanced section settings
        namespace Advanced {
            constexpr Setting DEVELOPER_MODE = {"developerMode", "Enable developer options in UI"};
            constexpr Setting DROP_SHADOW_OFFSET_X = {"dropShadowOffsetX", "Shadow X offset (normalized)"};
            constexpr Setting DROP_SHADOW_OFFSET_Y = {"dropShadowOffsetY", "Shadow Y offset (normalized)"};
            constexpr Setting DROP_SHADOW_COLOR = {"dropShadowColor", "Shadow color (0xAARRGGBB)"};
            constexpr Setting HOLD_REPEAT_FAST_MS = {"holdRepeatFastMs", "Hold-to-repeat max speed in ms (10-500, default 50)"};
            constexpr Setting CURSOR_ACTIVATION_THRESHOLD = {"cursorActivationThreshold", "Mouse travel from rest before cursor/settings button appear, normalized 0-1 (0.001=~2px, raise to ignore bigger bumps, lower for more responsive, default 0.015)"};
            constexpr Setting SEGMENT_SNAP_TO_SPLITS = {"segmentSnapToSplits", "Snap a new segment boundary onto a nearby official split (1=on default, 0=off)"};
            constexpr Setting SEGMENT_SNAP_THRESHOLD = {"segmentSnapThreshold", "Max distance to snap a segment boundary to a split, normalized 0-1 (default 0.02 = 2% of the lap)"};
            constexpr Setting HAZARD_STATIONARY_TOLERANCE = {"hazardStationaryTolerance", "Movement below this in meters = not moving (default 5.0)"};
            constexpr Setting HAZARD_STATIONARY_DURATION_MS = {"hazardStationaryDurationMs", "Time stationary before flagged in ms (default 2000)"};
            constexpr Setting HAZARD_WRONG_WAY_DURATION_MS = {"hazardWrongWayDurationMs", "Time going backward before flagged in ms (default 1500)"};
            constexpr Setting HAZARD_AWARENESS_DISTANCE = {"hazardAwarenessDistance", "Distance ahead to scan for hazards in meters (default 100.0)"};
            constexpr Setting HAZARD_COOLDOWN_MS = {"hazardCooldownMs", "Hysteresis before clearing hazard state in ms (default 1000)"};
            constexpr Setting HAZARD_GRACE_PERIOD_MS = {"hazardGracePeriodMs", "Grace period after race start in ms (default 10000)"};
            constexpr Setting BLUE_FLAG_AWARENESS_DISTANCE = {"blueFlagAwarenessDistance", "Blue flag detection range in meters (default 100.0)"};
            constexpr Setting GAP_NOTIFY_INTERVAL_MS = {"gapNotifyIntervalMs", "Min interval between live-gap HUD refreshes in ms; 0=refresh on every change (0-1000, default 100)"};
            constexpr Setting WEB_SERVER_PORT = {"webServerPort", "Web server port (default 8080)"};
            constexpr Setting WEB_SERVER_THROTTLE_MS = {"webServerThrottleMs", "Min interval between SSE pushes in ms (default 250)"};
            constexpr Setting WEB_SERVER_BIND_ADDRESS = {"webServerBindAddress", "Bind address (default 127.0.0.1, use 0.0.0.0 for network access)"};
        }
    }

    // ========================================================================
    // Enum string conversion helpers
    // These convert enums to/from stable string representations
    // ========================================================================

    // ColumnMode (TimingHud)
    const char* columnModeToString(ColumnMode mode) {
        switch (mode) {
            case ColumnMode::OFF: return "OFF";
            case ColumnMode::SPLITS: return "SPLITS";
            case ColumnMode::ALWAYS: return "ALWAYS";
            default: return "OFF";
        }
    }

    ColumnMode stringToColumnMode(const std::string& str, ColumnMode defaultVal = ColumnMode::OFF) {
        if (str == "OFF") return ColumnMode::OFF;
        if (str == "SPLITS") return ColumnMode::SPLITS;
        if (str == "ALWAYS") return ColumnMode::ALWAYS;
        DEBUG_WARN_F("Unknown ColumnMode '%s', using default", str.c_str());
        return defaultVal;
    }

    // StandingsHud::GapMode
    const char* gapModeToString(StandingsHud::GapMode mode) {
        switch (mode) {
            case StandingsHud::GapMode::OFF: return "OFF";
            case StandingsHud::GapMode::PLAYER: return "PLAYER";
            case StandingsHud::GapMode::ADJACENT: return "ADJACENT";
            case StandingsHud::GapMode::ALL: return "ALL";
            default: return "ALL";
        }
    }

    StandingsHud::GapMode stringToGapMode(const std::string& str, StandingsHud::GapMode defaultVal = StandingsHud::GapMode::ALL) {
        if (str == "OFF") return StandingsHud::GapMode::OFF;
        if (str == "PLAYER") return StandingsHud::GapMode::PLAYER;
        if (str == "ADJACENT") return StandingsHud::GapMode::ADJACENT;
        if (str == "ALL") return StandingsHud::GapMode::ALL;
        DEBUG_WARN_F("Unknown GapMode '%s', using default", str.c_str());
        return defaultVal;
    }

    // StandingsHud::PosGainMode
    const char* posGainModeToString(StandingsHud::PosGainMode mode) {
        switch (mode) {
            case StandingsHud::PosGainMode::OFF: return "OFF";
            case StandingsHud::PosGainMode::RACE_START: return "RACE_START";
            case StandingsHud::PosGainMode::LAST_SF: return "LAST_SF";
            case StandingsHud::PosGainMode::LAST_SPLIT: return "LAST_SPLIT";
            default: return "OFF";
        }
    }

    StandingsHud::PosGainMode stringToPosGainMode(const std::string& str, StandingsHud::PosGainMode defaultVal = StandingsHud::PosGainMode::OFF) {
        if (str == "OFF") return StandingsHud::PosGainMode::OFF;
        if (str == "RACE_START") return StandingsHud::PosGainMode::RACE_START;
        if (str == "LAST_SF") return StandingsHud::PosGainMode::LAST_SF;
        if (str == "LAST_SPLIT") return StandingsHud::PosGainMode::LAST_SPLIT;
        DEBUG_WARN_F("Unknown PosGainMode '%s', using default", str.c_str());
        return defaultVal;
    }

    // StandingsHud::GapReferenceMode
    const char* gapReferenceModeToString(StandingsHud::GapReferenceMode mode) {
        switch (mode) {
            case StandingsHud::GapReferenceMode::LEADER:      return "LEADER";
            case StandingsHud::GapReferenceMode::PLAYER:      return "PLAYER";
            case StandingsHud::GapReferenceMode::ALTERNATING: return "ALTERNATING";
            default: return "LEADER";
        }
    }

    StandingsHud::GapReferenceMode stringToGapReferenceMode(const std::string& str, StandingsHud::GapReferenceMode defaultVal = StandingsHud::GapReferenceMode::LEADER) {
        if (str == "LEADER") return StandingsHud::GapReferenceMode::LEADER;
        if (str == "PLAYER") return StandingsHud::GapReferenceMode::PLAYER;
        if (str == "ALTERNATING") return StandingsHud::GapReferenceMode::ALTERNATING;
        DEBUG_WARN_F("Unknown GapReferenceMode '%s', using default", str.c_str());
        return defaultVal;
    }

    // StandingsHud::AnimationMode
    const char* animationModeToString(StandingsHud::AnimationMode mode) {
        switch (mode) {
            case StandingsHud::AnimationMode::OFF:     return "OFF";
            case StandingsHud::AnimationMode::BASIC:   return "BASIC";
            case StandingsHud::AnimationMode::COLORED: return "COLORED";
            default: return "BASIC";
        }
    }

    StandingsHud::AnimationMode stringToAnimationMode(const std::string& str, StandingsHud::AnimationMode defaultVal = StandingsHud::AnimationMode::BASIC) {
        if (str == "OFF") return StandingsHud::AnimationMode::OFF;
        if (str == "BASIC") return StandingsHud::AnimationMode::BASIC;
        if (str == "COLORED") return StandingsHud::AnimationMode::COLORED;
        DEBUG_WARN_F("Unknown AnimationMode '%s', using default", str.c_str());
        return defaultVal;
    }

    // MapHud::RiderColorMode (also used by RadarHud)
    const char* riderColorModeToString(MapHud::RiderColorMode mode) {
        switch (mode) {
            case MapHud::RiderColorMode::UNIFORM: return "UNIFORM";
            case MapHud::RiderColorMode::BRAND: return "BRAND";
            case MapHud::RiderColorMode::RELATIVE_POS: return "RELATIVE_POS";
            default: return "UNIFORM";
        }
    }

    MapHud::RiderColorMode stringToRiderColorMode(const std::string& str, MapHud::RiderColorMode defaultVal = MapHud::RiderColorMode::UNIFORM) {
        if (str == "UNIFORM") return MapHud::RiderColorMode::UNIFORM;
        if (str == "BRAND") return MapHud::RiderColorMode::BRAND;
        if (str == "RELATIVE_POS") return MapHud::RiderColorMode::RELATIVE_POS;
        DEBUG_WARN_F("Unknown RiderColorMode '%s', using default", str.c_str());
        return defaultVal;
    }

    // MapHud::LabelMode (also used by RadarHud)
    const char* labelModeToString(MapHud::LabelMode mode) {
        switch (mode) {
            case MapHud::LabelMode::NONE: return "NONE";
            case MapHud::LabelMode::POSITION: return "POSITION";
            case MapHud::LabelMode::RACE_NUM: return "RACE_NUM";
            case MapHud::LabelMode::BOTH: return "BOTH";
            default: return "NONE";
        }
    }

    MapHud::LabelMode stringToLabelMode(const std::string& str, MapHud::LabelMode defaultVal = MapHud::LabelMode::NONE) {
        if (str == "NONE") return MapHud::LabelMode::NONE;
        if (str == "POSITION") return MapHud::LabelMode::POSITION;
        if (str == "RACE_NUM") return MapHud::LabelMode::RACE_NUM;
        if (str == "BOTH") return MapHud::LabelMode::BOTH;
        DEBUG_WARN_F("Unknown LabelMode '%s', using default", str.c_str());
        return defaultVal;
    }

    // MapHud::LabelAnchor (label position relative to the rider icon)
    const char* labelAnchorToString(MapHud::LabelAnchor anchor) {
        switch (anchor) {
            case MapHud::LabelAnchor::BELOW: return "BELOW";
            case MapHud::LabelAnchor::ABOVE: return "ABOVE";
            case MapHud::LabelAnchor::LEFT:  return "LEFT";
            case MapHud::LabelAnchor::RIGHT: return "RIGHT";
            default: return "BELOW";
        }
    }

    MapHud::LabelAnchor stringToLabelAnchor(const std::string& str, MapHud::LabelAnchor defaultVal = MapHud::LabelAnchor::BELOW) {
        if (str == "BELOW") return MapHud::LabelAnchor::BELOW;
        if (str == "ABOVE") return MapHud::LabelAnchor::ABOVE;
        if (str == "LEFT")  return MapHud::LabelAnchor::LEFT;
        if (str == "RIGHT") return MapHud::LabelAnchor::RIGHT;
        DEBUG_WARN_F("Unknown LabelAnchor '%s', using default", str.c_str());
        return defaultVal;
    }

    // MapHud::Detail (track ribbon LOD)
    const char* detailToString(MapHud::Detail detail) {
        switch (detail) {
            case MapHud::Detail::AUTO: return "AUTO";
            case MapHud::Detail::HIGH: return "HIGH";
            case MapHud::Detail::LOW:  return "LOW";
            default: return "AUTO";
        }
    }

    MapHud::Detail stringToDetail(const std::string& str, MapHud::Detail defaultVal = MapHud::Detail::AUTO) {
        if (str == "AUTO") return MapHud::Detail::AUTO;
        if (str == "HIGH") return MapHud::Detail::HIGH;
        if (str == "LOW")  return MapHud::Detail::LOW;
        DEBUG_WARN_F("Unknown Detail '%s', using default", str.c_str());
        return defaultVal;
    }

    // MapHud::AnchorPoint
    const char* anchorPointToString(MapHud::AnchorPoint point) {
        switch (point) {
            case MapHud::AnchorPoint::TOP_LEFT: return "TOP_LEFT";
            case MapHud::AnchorPoint::TOP_RIGHT: return "TOP_RIGHT";
            case MapHud::AnchorPoint::BOTTOM_LEFT: return "BOTTOM_LEFT";
            case MapHud::AnchorPoint::BOTTOM_RIGHT: return "BOTTOM_RIGHT";
            default: return "TOP_LEFT";
        }
    }

    MapHud::AnchorPoint stringToAnchorPoint(const std::string& str, MapHud::AnchorPoint defaultVal = MapHud::AnchorPoint::TOP_LEFT) {
        if (str == "TOP_LEFT") return MapHud::AnchorPoint::TOP_LEFT;
        if (str == "TOP_RIGHT") return MapHud::AnchorPoint::TOP_RIGHT;
        if (str == "BOTTOM_LEFT") return MapHud::AnchorPoint::BOTTOM_LEFT;
        if (str == "BOTTOM_RIGHT") return MapHud::AnchorPoint::BOTTOM_RIGHT;
        DEBUG_WARN_F("Unknown AnchorPoint '%s', using default", str.c_str());
        return defaultVal;
    }

    // RadarHud::RiderColorMode (overload for RadarHud's type)
    const char* radarRiderColorModeToString(RadarHud::RiderColorMode mode) {
        switch (mode) {
            case RadarHud::RiderColorMode::UNIFORM: return "UNIFORM";
            case RadarHud::RiderColorMode::BRAND: return "BRAND";
            case RadarHud::RiderColorMode::RELATIVE_POS: return "RELATIVE_POS";
            default: return "UNIFORM";
        }
    }

    RadarHud::RiderColorMode stringToRadarRiderColorMode(const std::string& str, RadarHud::RiderColorMode defaultVal = RadarHud::RiderColorMode::UNIFORM) {
        if (str == "UNIFORM") return RadarHud::RiderColorMode::UNIFORM;
        if (str == "BRAND") return RadarHud::RiderColorMode::BRAND;
        if (str == "RELATIVE_POS") return RadarHud::RiderColorMode::RELATIVE_POS;
        DEBUG_WARN_F("Unknown RadarRiderColorMode '%s', using default", str.c_str());
        return defaultVal;
    }

    // RadarHud::LabelMode (overload for RadarHud's type)
    const char* radarLabelModeToString(RadarHud::LabelMode mode) {
        switch (mode) {
            case RadarHud::LabelMode::NONE: return "NONE";
            case RadarHud::LabelMode::POSITION: return "POSITION";
            case RadarHud::LabelMode::RACE_NUM: return "RACE_NUM";
            case RadarHud::LabelMode::BOTH: return "BOTH";
            default: return "NONE";
        }
    }

    RadarHud::LabelMode stringToRadarLabelMode(const std::string& str, RadarHud::LabelMode defaultVal = RadarHud::LabelMode::NONE) {
        if (str == "NONE") return RadarHud::LabelMode::NONE;
        if (str == "POSITION") return RadarHud::LabelMode::POSITION;
        if (str == "RACE_NUM") return RadarHud::LabelMode::RACE_NUM;
        if (str == "BOTH") return RadarHud::LabelMode::BOTH;
        DEBUG_WARN_F("Unknown RadarLabelMode '%s', using default", str.c_str());
        return defaultVal;
    }

    // GapBarHud::RiderColorMode
    const char* gapBarRiderColorModeToString(GapBarHud::RiderColorMode mode) {
        switch (mode) {
            case GapBarHud::RiderColorMode::UNIFORM: return "UNIFORM";
            case GapBarHud::RiderColorMode::BRAND: return "BRAND";
            case GapBarHud::RiderColorMode::RELATIVE_POS: return "RELATIVE_POS";
            default: return "RELATIVE_POS";
        }
    }

    GapBarHud::RiderColorMode stringToGapBarRiderColorMode(const std::string& str, GapBarHud::RiderColorMode defaultVal = GapBarHud::RiderColorMode::RELATIVE_POS) {
        if (str == "UNIFORM") return GapBarHud::RiderColorMode::UNIFORM;
        if (str == "BRAND") return GapBarHud::RiderColorMode::BRAND;
        if (str == "RELATIVE_POS") return GapBarHud::RiderColorMode::RELATIVE_POS;
        DEBUG_WARN_F("Unknown GapBarRiderColorMode '%s', using default", str.c_str());
        return defaultVal;
    }

    // RadarHud::ProximityArrowMode
    const char* proximityArrowModeToString(RadarHud::ProximityArrowMode mode) {
        switch (mode) {
            case RadarHud::ProximityArrowMode::OFF: return "OFF";
            case RadarHud::ProximityArrowMode::EDGE: return "EDGE";
            case RadarHud::ProximityArrowMode::CIRCLE: return "CIRCLE";
            default: return "OFF";
        }
    }

    RadarHud::ProximityArrowMode stringToProximityArrowMode(const std::string& str, RadarHud::ProximityArrowMode defaultVal = RadarHud::ProximityArrowMode::OFF) {
        if (str == "OFF") return RadarHud::ProximityArrowMode::OFF;
        if (str == "EDGE") return RadarHud::ProximityArrowMode::EDGE;
        if (str == "CIRCLE") return RadarHud::ProximityArrowMode::CIRCLE;
        DEBUG_WARN_F("Unknown ProximityArrowMode '%s', using default", str.c_str());
        return defaultVal;
    }

    // RadarHud::ProximityArrowColorMode
    const char* proximityArrowColorModeToString(RadarHud::ProximityArrowColorMode mode) {
        switch (mode) {
            case RadarHud::ProximityArrowColorMode::DISTANCE: return "DISTANCE";
            case RadarHud::ProximityArrowColorMode::POSITION: return "POSITION";
            default: return "DISTANCE";
        }
    }

    RadarHud::ProximityArrowColorMode stringToProximityArrowColorMode(const std::string& str, RadarHud::ProximityArrowColorMode defaultVal = RadarHud::ProximityArrowColorMode::DISTANCE) {
        if (str == "DISTANCE") return RadarHud::ProximityArrowColorMode::DISTANCE;
        if (str == "POSITION") return RadarHud::ProximityArrowColorMode::POSITION;
        DEBUG_WARN_F("Unknown ProximityArrowColorMode '%s', using default", str.c_str());
        return defaultVal;
    }

    // RadarHud::RadarMode
    const char* radarModeToString(RadarHud::RadarMode mode) {
        switch (mode) {
            case RadarHud::RadarMode::OFF: return "OFF";
            case RadarHud::RadarMode::ON: return "ON";
            case RadarHud::RadarMode::AUTO_HIDE: return "AUTO_HIDE";
            default: return "ON";
        }
    }

    RadarHud::RadarMode stringToRadarMode(const std::string& str, RadarHud::RadarMode defaultVal = RadarHud::RadarMode::ON) {
        if (str == "OFF") return RadarHud::RadarMode::OFF;
        if (str == "ON") return RadarHud::RadarMode::ON;
        if (str == "AUTO_HIDE") return RadarHud::RadarMode::AUTO_HIDE;
        DEBUG_WARN_F("Unknown RadarMode '%s', using default", str.c_str());
        return defaultVal;
    }

    // PitboardHud::DisplayMode
    const char* pitboardDisplayModeToString(uint8_t mode) {
        switch (mode) {
            case PitboardHud::MODE_ALWAYS: return "ALWAYS";
            case PitboardHud::MODE_PIT: return "PIT";
            case PitboardHud::MODE_SPLITS: return "SPLITS";
            default: return "ALWAYS";
        }
    }

    uint8_t stringToPitboardDisplayMode(const std::string& str, uint8_t defaultVal = PitboardHud::MODE_ALWAYS) {
        if (str == "ALWAYS") return PitboardHud::MODE_ALWAYS;
        if (str == "PIT") return PitboardHud::MODE_PIT;
        if (str == "SPLITS") return PitboardHud::MODE_SPLITS;
        DEBUG_WARN_F("Unknown PitboardDisplayMode '%s', using default", str.c_str());
        return defaultVal;
    }

    const char* pitboardGapCompareModeToString(uint8_t mode) {
        switch (mode) {
            case PitboardHud::GAP_AUTO:       return "AUTO";
            case PitboardHud::GAP_LEADER:     return "LEADER";
            case PitboardHud::GAP_SESSION_PB: return "SESSION_PB";
            case PitboardHud::GAP_IDEAL:      return "IDEAL";
            case PitboardHud::GAP_ALLTIME_PB: return "ALLTIME_PB";
            case PitboardHud::GAP_OVERALL:    return "OVERALL";
            case PitboardHud::GAP_RECORD:     return "RECORD";
            default: return "AUTO";
        }
    }

    uint8_t stringToPitboardGapCompareMode(const std::string& str, uint8_t defaultVal = PitboardHud::GAP_AUTO) {
        if (str == "AUTO") return PitboardHud::GAP_AUTO;
        if (str == "LEADER") return PitboardHud::GAP_LEADER;
        if (str == "SESSION_PB") return PitboardHud::GAP_SESSION_PB;
        if (str == "IDEAL") return PitboardHud::GAP_IDEAL;
        if (str == "ALLTIME_PB") return PitboardHud::GAP_ALLTIME_PB;
        if (str == "OVERALL") return PitboardHud::GAP_OVERALL;
        if (str == "RECORD") return PitboardHud::GAP_RECORD;
        DEBUG_WARN_F("Unknown PitboardGapCompareMode '%s', using default", str.c_str());
        return defaultVal;
    }

    // TelemetryHud::DisplayMode / PerformanceHud::DisplayMode (same values)
    const char* displayModeToString(uint8_t mode) {
        switch (mode) {
            case TelemetryHud::DISPLAY_GRAPHS: return "GRAPHS";
            case TelemetryHud::DISPLAY_VALUES: return "VALUES";
            case TelemetryHud::DISPLAY_BOTH: return "BOTH";
            default: return "BOTH";
        }
    }

    uint8_t stringToDisplayMode(const std::string& str, uint8_t defaultVal = TelemetryHud::DISPLAY_BOTH) {
        if (str == "GRAPHS") return TelemetryHud::DISPLAY_GRAPHS;
        if (str == "VALUES") return TelemetryHud::DISPLAY_VALUES;
        if (str == "BOTH") return TelemetryHud::DISPLAY_BOTH;
        DEBUG_WARN_F("Unknown DisplayMode '%s', using default", str.c_str());
        return defaultVal;
    }

#if GAME_HAS_RECORDS_PROVIDER
    // RecordsHud::DataProvider
    const char* dataProviderToString(RecordsHud::DataProvider provider) {
        switch (provider) {
            case RecordsHud::DataProvider::CBR: return "CBR";
            case RecordsHud::DataProvider::MXB_RANKED: return "MXB_RANKED";
            default: return "CBR";
        }
    }

    RecordsHud::DataProvider stringToDataProvider(const std::string& str, RecordsHud::DataProvider defaultVal = RecordsHud::DataProvider::CBR) {
        if (str == "CBR") return RecordsHud::DataProvider::CBR;
        if (str == "MXB_RANKED") return RecordsHud::DataProvider::MXB_RANKED;
        DEBUG_WARN_F("Unknown DataProvider '%s', using default", str.c_str());
        return defaultVal;
    }
#endif

    // SpeedWidget::SpeedUnit
    const char* speedUnitToString(SpeedWidget::SpeedUnit unit) {
        switch (unit) {
            case SpeedWidget::SpeedUnit::MPH: return "MPH";
            case SpeedWidget::SpeedUnit::KMH: return "KMH";
            default: return "MPH";
        }
    }

    SpeedWidget::SpeedUnit stringToSpeedUnit(const std::string& str, SpeedWidget::SpeedUnit defaultVal = SpeedWidget::SpeedUnit::MPH) {
        if (str == "MPH") return SpeedWidget::SpeedUnit::MPH;
        if (str == "KMH") return SpeedWidget::SpeedUnit::KMH;
        DEBUG_WARN_F("Unknown SpeedUnit '%s', using default", str.c_str());
        return defaultVal;
    }

    // CompassWidget::Style
    const char* compassStyleToString(CompassWidget::Style style) {
        switch (style) {
            case CompassWidget::Style::Classic: return "classic";
            case CompassWidget::Style::Modern:  return "modern";
            default: return "classic";
        }
    }

    CompassWidget::Style stringToCompassStyle(const std::string& str, CompassWidget::Style defaultVal = CompassWidget::Style::Classic) {
        if (str == "classic") return CompassWidget::Style::Classic;
        if (str == "modern") return CompassWidget::Style::Modern;
        DEBUG_WARN_F("Unknown Compass style '%s', using default", str.c_str());
        return defaultVal;
    }

    // FuelWidget::FuelUnit
    const char* fuelUnitToString(FuelWidget::FuelUnit unit) {
        switch (unit) {
            case FuelWidget::FuelUnit::LITERS: return "LITERS";
            case FuelWidget::FuelUnit::GALLONS: return "GALLONS";
            default: return "LITERS";
        }
    }

    FuelWidget::FuelUnit stringToFuelUnit(const std::string& str, FuelWidget::FuelUnit defaultVal = FuelWidget::FuelUnit::LITERS) {
        if (str == "LITERS") return FuelWidget::FuelUnit::LITERS;
        if (str == "GALLONS") return FuelWidget::FuelUnit::GALLONS;
        DEBUG_WARN_F("Unknown FuelUnit '%s', using default", str.c_str());
        return defaultVal;
    }

    // TemperatureUnit
    const char* tempUnitToString(TemperatureUnit unit) {
        switch (unit) {
            case TemperatureUnit::CELSIUS: return "CELSIUS";
            case TemperatureUnit::FAHRENHEIT: return "FAHRENHEIT";
            default: return "CELSIUS";
        }
    }

    TemperatureUnit stringToTempUnit(const std::string& str, TemperatureUnit defaultVal = TemperatureUnit::CELSIUS) {
        if (str == "CELSIUS") return TemperatureUnit::CELSIUS;
        if (str == "FAHRENHEIT") return TemperatureUnit::FAHRENHEIT;
        DEBUG_WARN_F("Unknown TemperatureUnit '%s', using default", str.c_str());
        return defaultVal;
    }

    // PBScope
    const char* pbScopeToString(PBScope scope) {
        switch (scope) {
            case PBScope::BIKE: return "BIKE";
            case PBScope::CATEGORY: return "CATEGORY";
            default: return "BIKE";
        }
    }

    PBScope stringToPBScope(const std::string& str, PBScope defaultVal = PBScope::CATEGORY) {
        if (str == "BIKE") return PBScope::BIKE;
        if (str == "CATEGORY") return PBScope::CATEGORY;
        DEBUG_WARN_F("Unknown PBScope '%s', using default", str.c_str());
        return defaultVal;
    }

    // ========================================================================

    // Validation helper functions
    float validateScale(float value) {
        using namespace PluginConstants::SettingsLimits;
        if (value < MIN_SCALE || value > MAX_SCALE) {
            DEBUG_WARN_F("Invalid scale value %.2f, clamping to [%.2f, %.2f]",
                        value, MIN_SCALE, MAX_SCALE);
            return (value < MIN_SCALE) ? MIN_SCALE : MAX_SCALE;
        }
        return value;
    }

    uint8_t validateDisplayMode(int value) {
        if (value < 0 || value > 255) {
            DEBUG_WARN_F("Invalid display mode value %d (must be 0-255), using default 0", value);
            return 0;
        }
        return static_cast<uint8_t>(value);
    }

    float validateOpacity(float value) {
        using namespace PluginConstants::SettingsLimits;
        if (value < MIN_OPACITY || value > MAX_OPACITY) {
            DEBUG_WARN_F("Invalid opacity value %.2f, clamping to [%.2f, %.2f]",
                        value, MIN_OPACITY, MAX_OPACITY);
            return (value < MIN_OPACITY) ? MIN_OPACITY : MAX_OPACITY;
        }
        return value;
    }

    float validateOffset(float value) {
        using namespace PluginConstants::SettingsLimits;
        if (value < MIN_OFFSET || value > MAX_OFFSET) {
            DEBUG_WARN_F("Invalid offset value %.2f, clamping to [%.2f, %.2f]",
                        value, MIN_OFFSET, MAX_OFFSET);
            return (value < MIN_OFFSET) ? MIN_OFFSET : MAX_OFFSET;
        }
        return value;
    }

    int validateDisplayRows(int value) {
        using namespace PluginConstants::SettingsLimits;
        if (value < MIN_DISPLAY_ROWS || value > MAX_DISPLAY_ROWS) {
            DEBUG_WARN_F("Invalid display row count %d, clamping to [%d, %d]",
                        value, MIN_DISPLAY_ROWS, MAX_DISPLAY_ROWS);
            return (value < MIN_DISPLAY_ROWS) ? MIN_DISPLAY_ROWS : MAX_DISPLAY_ROWS;
        }
        return value;
    }

    int validateDisplayLaps(int value) {
        using namespace PluginConstants::SettingsLimits;
        if (value < MIN_DISPLAY_LAPS || value > MAX_DISPLAY_LAPS) {
            DEBUG_WARN_F("Invalid display lap count %d, clamping to [%d, %d]",
                        value, MIN_DISPLAY_LAPS, MAX_DISPLAY_LAPS);
            return (value < MIN_DISPLAY_LAPS) ? MIN_DISPLAY_LAPS : MAX_DISPLAY_LAPS;
        }
        return value;
    }

    float validateTrackWidthScale(float value) {
        if (value < MapHud::MIN_TRACK_WIDTH_SCALE || value > MapHud::MAX_TRACK_WIDTH_SCALE) {
            DEBUG_WARN_F("Invalid track width scale %.2f, clamping to [%.2f, %.2f]",
                        value, MapHud::MIN_TRACK_WIDTH_SCALE, MapHud::MAX_TRACK_WIDTH_SCALE);
            return (value < MapHud::MIN_TRACK_WIDTH_SCALE) ? MapHud::MIN_TRACK_WIDTH_SCALE : MapHud::MAX_TRACK_WIDTH_SCALE;
        }
        return value;
    }

    float validateZoomDistance(float value) {
        if (value < MapHud::MIN_ZOOM_DISTANCE || value > MapHud::MAX_ZOOM_DISTANCE) {
            DEBUG_WARN_F("Invalid zoom distance %.2f, clamping to [%.2f, %.2f]",
                        value, MapHud::MIN_ZOOM_DISTANCE, MapHud::MAX_ZOOM_DISTANCE);
            return (value < MapHud::MIN_ZOOM_DISTANCE) ? MapHud::MIN_ZOOM_DISTANCE : MapHud::MAX_ZOOM_DISTANCE;
        }
        return value;
    }

    // Icon shape helpers - convert between shape index and filename
    // Shape index is 1-based offset into icon list (0 = off/none)
    std::string shapeIndexToFilename(int shapeIndex) {
        if (shapeIndex <= 0) return "Off";
        const auto& assetMgr = AssetManager::getInstance();
        int spriteIndex = assetMgr.getFirstIconSpriteIndex() + shapeIndex - 1;
        std::string filename = assetMgr.getIconFilename(spriteIndex);
        return filename.empty() ? "Off" : filename;
    }

    int filenameToShapeIndex(const std::string& filename, int defaultShape) {
        if (filename.empty() || filename == "Off") return 0;
        const auto& assetMgr = AssetManager::getInstance();
        int spriteIndex = assetMgr.getIconSpriteIndex(filename);
        if (spriteIndex <= 0) return defaultShape;
        return spriteIndex - assetMgr.getFirstIconSpriteIndex() + 1;
    }

    // Helper to format a section name with profile index
    std::string formatSectionName(const char* hudName, ProfileType profile) {
        return std::string(hudName) + ":" + std::to_string(static_cast<int>(profile));
    }

    // Parse section name to extract HUD name and profile index
    // Returns true if successfully parsed, false if no profile index (global section)
    // Convert profile name to ProfileType (returns -1 if not found)
    int profileNameToIndex(const std::string& name) {
        if (name == "Practice") return static_cast<int>(ProfileType::PRACTICE);
        if (name == "Qualify")  return static_cast<int>(ProfileType::QUALIFY);
        if (name == "Race")     return static_cast<int>(ProfileType::RACE);
        if (name == "Spectate") return static_cast<int>(ProfileType::SPECTATE);
        return -1;
    }

    // Parse section name into HUD name and profile index
    // Supports both old format [HudName:0] and new format [HudName:Practice]
    // Returns true if this is a profile-specific section, false if base/global section
    bool parseSectionName(const std::string& section, std::string& hudName, int& profileIndex) {
        size_t colonPos = section.find(':');
        if (colonPos == std::string::npos) {
            hudName = section;
            profileIndex = -1;  // Base/global section (no profile suffix)
            return false;
        }
        hudName = section.substr(0, colonPos);
        std::string suffix = section.substr(colonPos + 1);

        // Try parsing as profile name first (new format: "Practice", "Qualify", etc.)
        profileIndex = profileNameToIndex(suffix);
        if (profileIndex >= 0) {
            return true;
        }

        // Fall back to numeric index (old format: "0", "1", etc.) - for migration
        try {
            profileIndex = std::stoi(suffix);
            return true;
        } catch (...) {
            hudName = section;
            profileIndex = -1;
            return false;
        }
    }

    // Helper to capture base HUD properties to a settings map
    // Set includePosition=false for HUDs that use anchor-based positioning (e.g., MapHud)
    // Color slot key names for per-HUD INI overrides (color_primary, color_secondary, etc.)
    //
    // When ColorSlot or FontCategory grows, both the toKey() and parseKey() lookups below
    // must be updated in lock-step. These static_asserts catch the easy mistake of adding
    // a new enum value without updating the INI key mappings.
    static_assert(static_cast<int>(ColorSlot::COUNT) == 10,
                  "ColorSlot enum changed — update colorSlotToKey() and parseColorKey() below.");
    static_assert(static_cast<int>(FontCategory::COUNT) == 6,
                  "FontCategory enum changed — update fontCategoryToKey() and parseFontKey() below.");

    const char* colorSlotToKey(ColorSlot slot) {
        switch (slot) {
            case ColorSlot::PRIMARY:    return "color_primary";
            case ColorSlot::SECONDARY:  return "color_secondary";
            case ColorSlot::TERTIARY:   return "color_tertiary";
            case ColorSlot::MUTED:      return "color_muted";
            case ColorSlot::BACKGROUND: return "color_background";
            case ColorSlot::POSITIVE:   return "color_positive";
            case ColorSlot::WARNING:    return "color_warning";
            case ColorSlot::NEUTRAL:    return "color_neutral";
            case ColorSlot::NEGATIVE:   return "color_negative";
            case ColorSlot::ACCENT:     return "color_accent";
            default:                    return nullptr;
        }
    }

    // Font category key names for per-HUD INI overrides (font_title, font_normal, etc.)
    const char* fontCategoryToKey(FontCategory category) {
        switch (category) {
            case FontCategory::TITLE:   return "font_title";
            case FontCategory::NORMAL:  return "font_normal";
            case FontCategory::STRONG:  return "font_strong";
            case FontCategory::DIGITS:  return "font_digits";
            case FontCategory::MARKER:  return "font_marker";
            case FontCategory::SMALL:   return "font_small";
            default:                    return nullptr;
        }
    }

    // Parse color slot from INI key name (returns ColorSlot::COUNT if not a color key)
    ColorSlot parseColorKey(const std::string& key) {
        if (key == "color_primary")    return ColorSlot::PRIMARY;
        if (key == "color_secondary")  return ColorSlot::SECONDARY;
        if (key == "color_tertiary")   return ColorSlot::TERTIARY;
        if (key == "color_muted")      return ColorSlot::MUTED;
        if (key == "color_background") return ColorSlot::BACKGROUND;
        if (key == "color_positive")   return ColorSlot::POSITIVE;
        if (key == "color_warning")    return ColorSlot::WARNING;
        if (key == "color_neutral")    return ColorSlot::NEUTRAL;
        if (key == "color_negative")   return ColorSlot::NEGATIVE;
        if (key == "color_accent")     return ColorSlot::ACCENT;
        return ColorSlot::COUNT;
    }

    // Parse font category from INI key name (returns FontCategory::COUNT if not a font key)
    FontCategory parseFontKey(const std::string& key) {
        if (key == "font_title")   return FontCategory::TITLE;
        if (key == "font_normal")  return FontCategory::NORMAL;
        if (key == "font_strong")  return FontCategory::STRONG;
        if (key == "font_digits")  return FontCategory::DIGITS;
        if (key == "font_marker")  return FontCategory::MARKER;
        if (key == "font_small")   return FontCategory::SMALL;
        return FontCategory::COUNT;
    }

    void captureBaseHudSettings(SettingsManager::HudSettings& settings, const BaseHud& hud, bool includePosition = true) {
        using namespace Keys::Base;
        settings[VISIBLE] = std::to_string(hud.isVisible() ? 1 : 0);
        settings[SHOW_TITLE] = std::to_string(hud.getShowTitle() ? 1 : 0);
        settings[SHOW_BG_TEXTURE] = std::to_string(hud.getShowBackgroundTexture() ? 1 : 0);
        settings[TEXTURE_VARIANT] = std::to_string(hud.getTextureVariant());
        settings[BG_OPACITY] = std::to_string(hud.getBackgroundOpacity());
        settings[SCALE] = std::to_string(hud.getScale());
        if (includePosition) {
            settings[OFFSET_X] = std::to_string(hud.getOffsetX());
            settings[OFFSET_Y] = std::to_string(hud.getOffsetY());
        }

        // Capture per-HUD color overrides (only if set)
        for (int i = 0; i < static_cast<int>(ColorSlot::COUNT); ++i) {
            ColorSlot slot = static_cast<ColorSlot>(i);
            if (hud.hasColorOverride(slot)) {
                const char* key = colorSlotToKey(slot);
                if (key) {
                    settings[key] = PluginUtils::formatColorHex(hud.getColorOverrideValue(slot));
                }
            }
        }

        // Capture per-HUD font overrides (only if set)
        for (int i = 0; i < static_cast<int>(FontCategory::COUNT); ++i) {
            FontCategory category = static_cast<FontCategory>(i);
            if (hud.hasFontOverride(category)) {
                const char* key = fontCategoryToKey(category);
                if (key) {
                    settings[key] = hud.getFontOverrideName(category);
                }
            }
        }

        // Capture per-HUD drop-shadow override (only if set)
        if (hud.hasDropShadowOverride()) {
            settings["dropShadow"] = hud.getDropShadowOverrideValue() ? "1" : "0";
        }
    }

    // Helper to write base HUD properties to file
    void writeBaseHudSettings(std::ofstream& file, const SettingsManager::HudSettings& settings) {
        using namespace Keys::Base;
        static const std::array<const char*, 8> baseKeys = {
            VISIBLE, SHOW_TITLE, SHOW_BG_TEXTURE, TEXTURE_VARIANT,
            BG_OPACITY, SCALE, OFFSET_X, OFFSET_Y
        };
        for (const auto& key : baseKeys) {
            auto it = settings.find(key);
            if (it != settings.end()) {
                file << key << "=" << it->second << "\n";
            }
        }
    }

    // Helper to check if a key is a base HUD property
    bool isBaseKey(const std::string& key) {
        using namespace Keys::Base;
        return key == VISIBLE || key == SHOW_TITLE || key == SHOW_BG_TEXTURE ||
               key == TEXTURE_VARIANT || key == BG_OPACITY || key == SCALE ||
               key == OFFSET_X || key == OFFSET_Y;
    }

    // Helper to get IniOnly setting description by HUD name and key
    // Returns nullptr if not an IniOnly setting (no description available)
    const char* getIniOnlyDescription(const std::string& hudName, const std::string& key) {
        using namespace IniOnly;

        if (hudName == "SpeedoWidget") {
            if (key == Speedo::NEEDLE_COLOR.key) return Speedo::NEEDLE_COLOR.description;
            if (key == Speedo::SHOW_ODOMETER.key) return Speedo::SHOW_ODOMETER.description;
            if (key == Speedo::SHOW_TRIPMETER.key) return Speedo::SHOW_TRIPMETER.description;
        } else if (hudName == "TachoWidget") {
            if (key == Tacho::NEEDLE_COLOR.key) return Tacho::NEEDLE_COLOR.description;
        } else if (hudName == "SpeedWidget") {
            if (key == Speed::ROW_UNITS.key) return Speed::ROW_UNITS.description;
        } else if (hudName == "GearWidget") {
            if (key == Gear::SHOW_SHIFT_COLOR.key) return Gear::SHOW_SHIFT_COLOR.description;
            if (key == Gear::SHOW_LIMITER_CIRCLE.key) return Gear::SHOW_LIMITER_CIRCLE.description;
        } else if (hudName == "ClockWidget") {
            if (key == Clock::SHOW_UTC.key) return Clock::SHOW_UTC.description;
            if (key == Clock::UTC_ON_TOP.key) return Clock::UTC_ON_TOP.description;
        } else if (hudName == "RumbleHud") {
            if (key == Rumble::SHOW_MAX_MARKERS.key) return Rumble::SHOW_MAX_MARKERS.description;
            if (key == Rumble::MAX_MARKER_LINGER_FRAMES.key) return Rumble::MAX_MARKER_LINGER_FRAMES.description;
        } else if (hudName == "LeanWidget") {
            if (key == Lean::ARC_FILL_COLOR.key) return Lean::ARC_FILL_COLOR.description;
            if (key == Lean::ROW_ARC.key) return Lean::ROW_ARC.description;
            if (key == Lean::ROW_LEAN_VALUE.key) return Lean::ROW_LEAN_VALUE.description;
            if (key == Lean::ROW_STEER_BAR.key) return Lean::ROW_STEER_BAR.description;
            if (key == Lean::ROW_STEER_VALUE.key) return Lean::ROW_STEER_VALUE.description;
            if (key == Lean::SHOW_MAX_MARKERS.key) return Lean::SHOW_MAX_MARKERS.description;
            if (key == Lean::MAX_MARKER_LINGER_FRAMES.key) return Lean::MAX_MARKER_LINGER_FRAMES.description;
        } else if (hudName == "GForceWidget") {
            if (key == GForce::MAX_SCALE.key) return GForce::MAX_SCALE.description;
            if (key == GForce::SHOW_MAX_TEXT.key) return GForce::SHOW_MAX_TEXT.description;
            if (key == GForce::SHOW_MAX_MARKER.key) return GForce::SHOW_MAX_MARKER.description;
            if (key == GForce::MAX_MARKER_LINGER_FRAMES.key) return GForce::MAX_MARKER_LINGER_FRAMES.description;
        } else if (hudName == "CompassWidget") {
            if (key == Compass::STYLE.key) return Compass::STYLE.description;
        } else if (hudName == "BarsWidget") {
            if (key == Bars::COL_THROTTLE.key) return Bars::COL_THROTTLE.description;
            if (key == Bars::COL_BRAKE.key) return Bars::COL_BRAKE.description;
            if (key == Bars::COL_CLUTCH.key) return Bars::COL_CLUTCH.description;
            if (key == Bars::COL_RPM.key) return Bars::COL_RPM.description;
            if (key == Bars::COL_SUSPENSION.key) return Bars::COL_SUSPENSION.description;
            if (key == Bars::COL_FUEL.key) return Bars::COL_FUEL.description;
            if (key == Bars::COL_ENGINE_TEMP.key) return Bars::COL_ENGINE_TEMP.description;
            if (key == Bars::COL_WATER_TEMP.key) return Bars::COL_WATER_TEMP.description;
            if (key == Bars::SHOW_LABELS.key) return Bars::SHOW_LABELS.description;
            if (key == Bars::SHOW_MAX_MARKERS.key) return Bars::SHOW_MAX_MARKERS.description;
            if (key == Bars::MAX_MARKER_LINGER_FRAMES.key) return Bars::MAX_MARKER_LINGER_FRAMES.description;
        }
#if GAME_HAS_TYRE_TEMP
        else if (hudName == "TyreTempWidget") {
            if (key == TyreTemp::COLD_THRESHOLD.key) return TyreTemp::COLD_THRESHOLD.description;
            if (key == TyreTemp::HOT_THRESHOLD.key) return TyreTemp::HOT_THRESHOLD.description;
            if (key == TyreTemp::ROW_BARS.key) return TyreTemp::ROW_BARS.description;
            if (key == TyreTemp::ROW_VALUES.key) return TyreTemp::ROW_VALUES.description;
            if (key == TyreTemp::SHOW_LABELS.key) return TyreTemp::SHOW_LABELS.description;
        }
#endif
#if GAME_HAS_ECU
        else if (hudName == "EcuWidget") {
            if (key == Ecu::ROW_MAP.key) return Ecu::ROW_MAP.description;
            if (key == Ecu::ROW_TC.key) return Ecu::ROW_TC.description;
            if (key == Ecu::ROW_EB.key) return Ecu::ROW_EB.description;
            if (key == Ecu::ROW_AW.key) return Ecu::ROW_AW.description;
            if (key == Ecu::SHOW_LABELS.key) return Ecu::SHOW_LABELS.description;
        }
#endif
        else if (hudName == "FuelWidget") {
            if (key == Fuel::ROW_FUEL.key) return Fuel::ROW_FUEL.description;
            if (key == Fuel::ROW_USED.key) return Fuel::ROW_USED.description;
            if (key == Fuel::ROW_AVG.key) return Fuel::ROW_AVG.description;
            if (key == Fuel::ROW_EST.key) return Fuel::ROW_EST.description;
        } else if (hudName == "NoticesHud") {
            if (key == Notices::WRONG_WAY.key) return Notices::WRONG_WAY.description;
            if (key == Notices::BLUE_FLAG.key) return Notices::BLUE_FLAG.description;
            if (key == Notices::LAST_LAP.key) return Notices::LAST_LAP.description;
            if (key == Notices::FINISHED.key) return Notices::FINISHED.description;
            if (key == Notices::ALLTIME_PB.key) return Notices::ALLTIME_PB.description;
            if (key == Notices::FASTEST_LAP.key) return Notices::FASTEST_LAP.description;
            if (key == Notices::SESSION_PB.key) return Notices::SESSION_PB.description;
            if (key == Notices::DEFAULT_SETUP.key) return Notices::DEFAULT_SETUP.description;
            if (key == Notices::SEGMENT.key) return Notices::SEGMENT.description;
            if (key == Notices::PB_DURATION.key) return Notices::PB_DURATION.description;
        } else if (hudName == "StandingsHud") {
            if (key == Standings::TOP_POSITIONS.key) return Standings::TOP_POSITIONS.description;
            if (key == Standings::PLAYER_ROW_HIGHLIGHT.key) return Standings::PLAYER_ROW_HIGHLIGHT.description;
            if (key == Standings::PLAYER_ROW_HIGHLIGHT_BRAND.key) return Standings::PLAYER_ROW_HIGHLIGHT_BRAND.description;
            if (key == Standings::ANIMATION_DURATION_MS.key) return Standings::ANIMATION_DURATION_MS.description;
            if (key == Standings::CLASSIC_LAYOUT.key) return Standings::CLASSIC_LAYOUT.description;
            if (key == Standings::NAME_MODE.key) return Standings::NAME_MODE.description;
            if (key == Standings::SHORT_NAME_CHARS.key) return Standings::SHORT_NAME_CHARS.description;
        }
#if GAME_HAS_RECORDS_PROVIDER
        else if (hudName == "RecordsHud") {
            if (key == Records::SHOW_FOOTER.key) return Records::SHOW_FOOTER.description;
        }
#endif
        else if (hudName == "GamepadWidget") {
            if (key == Gamepad::TRIGGER_FILL_MODE.key) return Gamepad::TRIGGER_FILL_MODE.description;
        }
        else if (hudName == "MapHud") {
            if (key == Map::LABEL_ANCHOR.key) return Map::LABEL_ANCHOR.description;
        }
#if GAME_HAS_FMX
        else if (hudName == "FmxHud") {
            // Per-trick disable flags share a common prefix; one description covers them all.
            if (key.rfind(Keys::Fmx::TRICK_ENABLED_PREFIX, 0) == 0) {
                return "Track this trick (1=enabled, 0=ignore)";
            }
        }
#endif
        else if (hudName == "LapConsistencyHud") {
            if (key == LapConsistency::CONSISTENCY_SCALE_FACTOR.key) return LapConsistency::CONSISTENCY_SCALE_FACTOR.description;
            if (key == LapConsistency::TREND_THRESHOLD_PERCENT.key) return LapConsistency::TREND_THRESHOLD_PERCENT.description;
        }

        // Per-HUD color/font overrides apply to all HUDs (checked last so HUD-specific
        // keys starting with "color_" or "font_" can be matched first if ever added)
        if (key.length() > 6 && key.substr(0, 6) == "color_") return "Per-HUD color override (hex ABGR, e.g. 0xff00ff00)";
        if (key.length() > 5 && key.substr(0, 5) == "font_") return "Per-HUD font override (font filename without extension)";
        if (key == "dropShadow") return "Per-HUD drop shadow override (0=off, 1=on; absent=inherit global)";

        return nullptr;
    }

    // Helper to write a setting with optional inline comment for IniOnly settings
    void writeSettingWithComment(std::ofstream& file, const std::string& hudName,
                                 const std::string& key, const std::string& value) {
        const char* description = getIniOnlyDescription(hudName, key);
        if (description) {
            file << key << "=" << value << " ; " << description << "\n";
        } else {
            file << key << "=" << value << "\n";
        }
    }

    // ========================================================================
    // Named key helpers for bitmask fields
    // ========================================================================

    // Helper to save a single bit as a named key
    void saveBitAsKey(SettingsManager::HudSettings& settings, const char* key, uint32_t bitmask, uint32_t bit) {
        settings[key] = (bitmask & bit) ? "1" : "0";
    }

    // Helper to load a single bit from a named key
    void loadBitFromKey(const SettingsManager::HudSettings& settings, const char* key, uint32_t& bitmask, uint32_t bit) {
        auto it = settings.find(key);
        if (it != settings.end()) {
            if (it->second == "1") {
                bitmask |= bit;
            } else {
                bitmask &= ~bit;
            }
        }
        // If key is missing, leave bitmask unchanged (uses default)
    }

    // StandingsHud: save columns as named keys
    void saveStandingsColumns(SettingsManager::HudSettings& settings, uint32_t cols) {
        using namespace Keys::StandingsCols;
        saveBitAsKey(settings, TRACKED, cols, StandingsHud::COL_TRACKED);
        saveBitAsKey(settings, POS, cols, StandingsHud::COL_POS);
        // COL_POSGAIN visibility is driven entirely by posGainMode now; the bit is never
        // user-toggled, so we don't write col_posgain (avoids an inconsistent INI). It's
        // still read in loadStandingsColumns purely to migrate pre-mode configs.
        saveBitAsKey(settings, RACENUM, cols, StandingsHud::COL_RACENUM);
        saveBitAsKey(settings, NAME, cols, StandingsHud::COL_NAME);
        saveBitAsKey(settings, BIKE, cols, StandingsHud::COL_BIKE);
        saveBitAsKey(settings, PENALTY, cols, StandingsHud::COL_PENALTY);
        saveBitAsKey(settings, BEST_LAP, cols, StandingsHud::COL_BEST_LAP);
        saveBitAsKey(settings, LAST_LAP, cols, StandingsHud::COL_LAST_LAP);
        saveBitAsKey(settings, GAP, cols, StandingsHud::COL_GAP);
    }

    // StandingsHud: load columns from named keys
    void loadStandingsColumns(const SettingsManager::HudSettings& settings, uint32_t& cols) {
        using namespace Keys::StandingsCols;
        loadBitFromKey(settings, TRACKED, cols, StandingsHud::COL_TRACKED);
        loadBitFromKey(settings, POS, cols, StandingsHud::COL_POS);
        loadBitFromKey(settings, POSGAIN, cols, StandingsHud::COL_POSGAIN);  // migration-only (see saveStandingsColumns); posGainMode is the source of truth
        loadBitFromKey(settings, RACENUM, cols, StandingsHud::COL_RACENUM);
        loadBitFromKey(settings, NAME, cols, StandingsHud::COL_NAME);
        loadBitFromKey(settings, BIKE, cols, StandingsHud::COL_BIKE);
        loadBitFromKey(settings, PENALTY, cols, StandingsHud::COL_PENALTY);
        loadBitFromKey(settings, BEST_LAP, cols, StandingsHud::COL_BEST_LAP);
        loadBitFromKey(settings, LAST_LAP, cols, StandingsHud::COL_LAST_LAP);
        loadBitFromKey(settings, GAP, cols, StandingsHud::COL_GAP);
    }

#if GAME_HAS_RECORDS_PROVIDER
    // RecordsHud: save columns as named keys (only optional columns)
    void saveRecordsColumns(SettingsManager::HudSettings& settings, uint32_t cols) {
        using namespace Keys::RecordsCols;
        // Core columns (POS, RIDER, BIKE, LAPTIME) are always on - don't save
        // Only save optional columns
        saveBitAsKey(settings, SECTORS, cols, RecordsHud::COL_SECTORS);
        saveBitAsKey(settings, DATE, cols, RecordsHud::COL_DATE);
    }

    // RecordsHud: load columns from named keys (only optional columns)
    void loadRecordsColumns(const SettingsManager::HudSettings& settings, uint32_t& cols) {
        using namespace Keys::RecordsCols;
        // Core columns are always on - ensure they're set
        cols |= RecordsHud::COL_CORE;
        // Load optional columns
        loadBitFromKey(settings, SECTORS, cols, RecordsHud::COL_SECTORS);
        loadBitFromKey(settings, DATE, cols, RecordsHud::COL_DATE);
    }
#endif

    // LapLogHud: save columns (only sectors are configurable)
    void saveLapLogColumns(SettingsManager::HudSettings& settings, uint32_t cols) {
        using namespace Keys::LapLogCols;
        saveBitAsKey(settings, SECTORS, cols, LapLogHud::COL_SECTORS);
    }

    // LapLogHud: load columns (only sectors are configurable)
    void loadLapLogColumns(const SettingsManager::HudSettings& settings, uint32_t& cols) {
        using namespace Keys::LapLogCols;
        loadBitFromKey(settings, SECTORS, cols, LapLogHud::COL_SECTORS);
    }

    // IdealLapHud: save rows (sectors and laps toggled as groups)
    void saveIdealLapRows(SettingsManager::HudSettings& settings, uint32_t rows) {
        using namespace Keys::IdealLapRows;
        saveBitAsKey(settings, SECTORS, rows, IdealLapHud::ROW_SECTORS);
        saveBitAsKey(settings, LAPS, rows, IdealLapHud::ROW_LAPS);
    }

    // IdealLapHud: load rows (sectors and laps toggled as groups)
    void loadIdealLapRows(const SettingsManager::HudSettings& settings, uint32_t& rows) {
        using namespace Keys::IdealLapRows;
        loadBitFromKey(settings, SECTORS, rows, IdealLapHud::ROW_SECTORS);
        loadBitFromKey(settings, LAPS, rows, IdealLapHud::ROW_LAPS);
    }

    // PitboardHud: save rows as named keys
    void savePitboardRows(SettingsManager::HudSettings& settings, uint32_t rows) {
        using namespace Keys::PitboardRows;
        saveBitAsKey(settings, RIDER_ID, rows, PitboardHud::ROW_RIDER_ID);
        saveBitAsKey(settings, SESSION, rows, PitboardHud::ROW_SESSION);
        saveBitAsKey(settings, POSITION, rows, PitboardHud::ROW_POSITION);
        saveBitAsKey(settings, TIME, rows, PitboardHud::ROW_TIME);
        saveBitAsKey(settings, LAP, rows, PitboardHud::ROW_LAP);
        saveBitAsKey(settings, LAST_LAP, rows, PitboardHud::ROW_LAST_LAP);
        saveBitAsKey(settings, GAP, rows, PitboardHud::ROW_GAP);
    }

    // PitboardHud: load rows from named keys
    void loadPitboardRows(const SettingsManager::HudSettings& settings, uint32_t& rows) {
        using namespace Keys::PitboardRows;
        loadBitFromKey(settings, RIDER_ID, rows, PitboardHud::ROW_RIDER_ID);
        loadBitFromKey(settings, SESSION, rows, PitboardHud::ROW_SESSION);
        loadBitFromKey(settings, POSITION, rows, PitboardHud::ROW_POSITION);
        loadBitFromKey(settings, TIME, rows, PitboardHud::ROW_TIME);
        loadBitFromKey(settings, LAP, rows, PitboardHud::ROW_LAP);
        loadBitFromKey(settings, LAST_LAP, rows, PitboardHud::ROW_LAST_LAP);
        loadBitFromKey(settings, GAP, rows, PitboardHud::ROW_GAP);
    }

    // SpeedWidget: save rows as named keys
    void saveSpeedRows(SettingsManager::HudSettings& settings, uint32_t rows) {
        using namespace Keys::SpeedRows;
        saveBitAsKey(settings, UNITS, rows, SpeedWidget::ROW_UNITS);
    }

    // SpeedWidget: load rows from named keys
    void loadSpeedRows(const SettingsManager::HudSettings& settings, uint32_t& rows) {
        using namespace Keys::SpeedRows;
        loadBitFromKey(settings, UNITS, rows, SpeedWidget::ROW_UNITS);
    }

    // FuelWidget: save rows as named keys
    void saveFuelRows(SettingsManager::HudSettings& settings, uint32_t rows) {
        using namespace Keys::FuelRows;
        saveBitAsKey(settings, FUEL, rows, FuelWidget::ROW_FUEL);
        saveBitAsKey(settings, USED, rows, FuelWidget::ROW_USED);
        saveBitAsKey(settings, AVG, rows, FuelWidget::ROW_AVG);
        saveBitAsKey(settings, EST, rows, FuelWidget::ROW_EST);
    }

    // FuelWidget: load rows from named keys
    void loadFuelRows(const SettingsManager::HudSettings& settings, uint32_t& rows) {
        using namespace Keys::FuelRows;
        loadBitFromKey(settings, FUEL, rows, FuelWidget::ROW_FUEL);
        loadBitFromKey(settings, USED, rows, FuelWidget::ROW_USED);
        loadBitFromKey(settings, AVG, rows, FuelWidget::ROW_AVG);
        loadBitFromKey(settings, EST, rows, FuelWidget::ROW_EST);
    }

    // SessionHud: save rows as named keys
    void saveSessionRows(SettingsManager::HudSettings& settings, uint32_t rows) {
        using namespace Keys::SessionRows;
        saveBitAsKey(settings, TRACK, rows, SessionHud::ROW_TRACK);
        saveBitAsKey(settings, FORMAT, rows, SessionHud::ROW_FORMAT);
        saveBitAsKey(settings, SERVER, rows, SessionHud::ROW_SERVER);
        saveBitAsKey(settings, WEATHER, rows, SessionHud::ROW_WEATHER);
    }

    // SessionHud: load rows from named keys
    // Note: an older "row_players" key (player count row) was dropped in v1.23
    // along with the memory-reading subsystem. Old INIs containing it are
    // silently ignored - we never look it up. ROW_WEATHER's bit shifted from
    // 1<<5 to 1<<4 when ROW_PLAYERS was removed, but persistence is by name
    // (row_weather) not by mask, so existing profiles are unaffected.
    void loadSessionRows(const SettingsManager::HudSettings& settings, uint32_t& rows) {
        using namespace Keys::SessionRows;
        loadBitFromKey(settings, TRACK, rows, SessionHud::ROW_TRACK);
        loadBitFromKey(settings, FORMAT, rows, SessionHud::ROW_FORMAT);
        loadBitFromKey(settings, SERVER, rows, SessionHud::ROW_SERVER);
        loadBitFromKey(settings, WEATHER, rows, SessionHud::ROW_WEATHER);
    }

    // LeanWidget: save rows as named keys
    void saveLeanRows(SettingsManager::HudSettings& settings, uint32_t rows) {
        using namespace Keys::LeanRows;
        saveBitAsKey(settings, ARC, rows, LeanWidget::ROW_ARC);
        saveBitAsKey(settings, LEAN_VALUE, rows, LeanWidget::ROW_LEAN_VALUE);
        saveBitAsKey(settings, STEER_BAR, rows, LeanWidget::ROW_STEER_BAR);
        saveBitAsKey(settings, STEER_VALUE, rows, LeanWidget::ROW_STEER_VALUE);
    }

    // LeanWidget: load rows from named keys
    void loadLeanRows(const SettingsManager::HudSettings& settings, uint32_t& rows) {
        using namespace Keys::LeanRows;
        loadBitFromKey(settings, ARC, rows, LeanWidget::ROW_ARC);
        loadBitFromKey(settings, LEAN_VALUE, rows, LeanWidget::ROW_LEAN_VALUE);
        loadBitFromKey(settings, STEER_BAR, rows, LeanWidget::ROW_STEER_BAR);
        loadBitFromKey(settings, STEER_VALUE, rows, LeanWidget::ROW_STEER_VALUE);
    }

#if GAME_HAS_TYRE_TEMP
    // TyreTempWidget: save rows as named keys
    void saveTyreTempRows(SettingsManager::HudSettings& settings, uint32_t rows) {
        using namespace Keys::TyreTempRows;
        saveBitAsKey(settings, BARS, rows, TyreTempWidget::ROW_BARS);
        saveBitAsKey(settings, VALUES, rows, TyreTempWidget::ROW_VALUES);
    }

    // TyreTempWidget: load rows from named keys
    void loadTyreTempRows(const SettingsManager::HudSettings& settings, uint32_t& rows) {
        using namespace Keys::TyreTempRows;
        loadBitFromKey(settings, BARS, rows, TyreTempWidget::ROW_BARS);
        loadBitFromKey(settings, VALUES, rows, TyreTempWidget::ROW_VALUES);
    }
#endif

#if GAME_HAS_ECU
    // EcuWidget: save chips as named keys
    void saveEcuRows(SettingsManager::HudSettings& settings, uint32_t rows) {
        using namespace Keys::EcuRows;
        saveBitAsKey(settings, MAP, rows, EcuWidget::ROW_MAP);
        saveBitAsKey(settings, TC, rows, EcuWidget::ROW_TC);
        saveBitAsKey(settings, EB, rows, EcuWidget::ROW_EB);
        saveBitAsKey(settings, AW, rows, EcuWidget::ROW_AW);
    }

    // EcuWidget: load chips from named keys
    void loadEcuRows(const SettingsManager::HudSettings& settings, uint32_t& rows) {
        using namespace Keys::EcuRows;
        loadBitFromKey(settings, MAP, rows, EcuWidget::ROW_MAP);
        loadBitFromKey(settings, TC, rows, EcuWidget::ROW_TC);
        loadBitFromKey(settings, EB, rows, EcuWidget::ROW_EB);
        loadBitFromKey(settings, AW, rows, EcuWidget::ROW_AW);
    }
#endif

    // BarsWidget: save columns as named keys
    void saveBarsColumns(SettingsManager::HudSettings& settings, uint32_t cols) {
        using namespace Keys::BarsCols;
        saveBitAsKey(settings, THROTTLE, cols, BarsWidget::COL_THROTTLE);
        saveBitAsKey(settings, BRAKE, cols, BarsWidget::COL_BRAKE);
        saveBitAsKey(settings, CLUTCH, cols, BarsWidget::COL_CLUTCH);
        saveBitAsKey(settings, RPM, cols, BarsWidget::COL_RPM);
        saveBitAsKey(settings, SUSPENSION, cols, BarsWidget::COL_SUSPENSION);
        saveBitAsKey(settings, FUEL, cols, BarsWidget::COL_FUEL);
        saveBitAsKey(settings, ENGINE_TEMP, cols, BarsWidget::COL_ENGINE_TEMP);
        saveBitAsKey(settings, WATER_TEMP, cols, BarsWidget::COL_WATER_TEMP);
    }

    // BarsWidget: load columns from named keys
    void loadBarsColumns(const SettingsManager::HudSettings& settings, uint32_t& cols) {
        using namespace Keys::BarsCols;
        loadBitFromKey(settings, THROTTLE, cols, BarsWidget::COL_THROTTLE);
        loadBitFromKey(settings, BRAKE, cols, BarsWidget::COL_BRAKE);
        loadBitFromKey(settings, CLUTCH, cols, BarsWidget::COL_CLUTCH);
        loadBitFromKey(settings, RPM, cols, BarsWidget::COL_RPM);
        loadBitFromKey(settings, SUSPENSION, cols, BarsWidget::COL_SUSPENSION);
        loadBitFromKey(settings, FUEL, cols, BarsWidget::COL_FUEL);
        loadBitFromKey(settings, ENGINE_TEMP, cols, BarsWidget::COL_ENGINE_TEMP);
        loadBitFromKey(settings, WATER_TEMP, cols, BarsWidget::COL_WATER_TEMP);
    }

    // EventLogHud: save events as named keys
    void saveEventLogEvents(SettingsManager::HudSettings& settings, uint32_t events) {
        using namespace Keys::EventLog;
        saveBitAsKey(settings, SESSION_STARTED, events, EVENT_SESSION_STARTED);
        saveBitAsKey(settings, SESSION_STATE, events, EVENT_SESSION_STATE);
        saveBitAsKey(settings, FASTEST_LAP, events, EVENT_FASTEST_LAP);
        saveBitAsKey(settings, PENALTY, events, EVENT_PENALTY);
        saveBitAsKey(settings, PENALTY_CLEAR, events, EVENT_PENALTY_CLEAR);
        saveBitAsKey(settings, RIDER_RETIRED, events, EVENT_RIDER_RETIRED);
        saveBitAsKey(settings, RIDER_DSQ, events, EVENT_RIDER_DSQ);
        saveBitAsKey(settings, RIDER_DNS, events, EVENT_RIDER_DNS);
        saveBitAsKey(settings, OVERTIME, events, EVENT_OVERTIME);
        saveBitAsKey(settings, FINAL_LAP, events, EVENT_FINAL_LAP);
        saveBitAsKey(settings, RIDER_FINISHED, events, EVENT_RIDER_FINISHED);
        saveBitAsKey(settings, LEADER_CHANGE, events, EVENT_LEADER_CHANGE);
        saveBitAsKey(settings, PIT_ENTRY, events, EVENT_PIT_ENTRY);
        saveBitAsKey(settings, PIT_EXIT, events, EVENT_PIT_EXIT);
    }

    // EventLogHud: load events from named keys
    void loadEventLogEvents(const SettingsManager::HudSettings& settings, uint32_t& events) {
        using namespace Keys::EventLog;
        loadBitFromKey(settings, SESSION_STARTED, events, EVENT_SESSION_STARTED);
        loadBitFromKey(settings, SESSION_STATE, events, EVENT_SESSION_STATE);
        loadBitFromKey(settings, FASTEST_LAP, events, EVENT_FASTEST_LAP);
        loadBitFromKey(settings, PENALTY, events, EVENT_PENALTY);
        loadBitFromKey(settings, PENALTY_CLEAR, events, EVENT_PENALTY_CLEAR);
        loadBitFromKey(settings, RIDER_RETIRED, events, EVENT_RIDER_RETIRED);
        loadBitFromKey(settings, RIDER_DSQ, events, EVENT_RIDER_DSQ);
        loadBitFromKey(settings, RIDER_DNS, events, EVENT_RIDER_DNS);
        loadBitFromKey(settings, OVERTIME, events, EVENT_OVERTIME);
        loadBitFromKey(settings, FINAL_LAP, events, EVENT_FINAL_LAP);
        loadBitFromKey(settings, RIDER_FINISHED, events, EVENT_RIDER_FINISHED);
        loadBitFromKey(settings, LEADER_CHANGE, events, EVENT_LEADER_CHANGE);
        loadBitFromKey(settings, PIT_ENTRY, events, EVENT_PIT_ENTRY);
        loadBitFromKey(settings, PIT_EXIT, events, EVENT_PIT_EXIT);
    }

    // NoticesHud: save notices as named keys
    void saveNotices(SettingsManager::HudSettings& settings, uint32_t notices) {
        using namespace Keys::Notices;
        saveBitAsKey(settings, WRONG_WAY, notices, NoticesHud::NOTICE_WRONG_WAY);
        saveBitAsKey(settings, BLUE_FLAG, notices, NoticesHud::NOTICE_BLUE_FLAG);
        saveBitAsKey(settings, LAST_LAP, notices, NoticesHud::NOTICE_LAST_LAP);
        saveBitAsKey(settings, FINISHED, notices, NoticesHud::NOTICE_FINISHED);
        saveBitAsKey(settings, ALLTIME_PB, notices, NoticesHud::NOTICE_ALLTIME_PB);
        saveBitAsKey(settings, FASTEST_LAP, notices, NoticesHud::NOTICE_FASTEST_LAP);
        saveBitAsKey(settings, SESSION_PB, notices, NoticesHud::NOTICE_SESSION_PB);
        saveBitAsKey(settings, DEFAULT_SETUP, notices, NoticesHud::NOTICE_DEFAULT_SETUP);
        saveBitAsKey(settings, OVERTIME, notices, NoticesHud::NOTICE_OVERTIME);
        saveBitAsKey(settings, HAZARD_STATIONARY, notices, NoticesHud::NOTICE_HAZARD_STATIONARY);
        saveBitAsKey(settings, HAZARD_WRONG_WAY, notices, NoticesHud::NOTICE_HAZARD_WRONG_WAY);
        saveBitAsKey(settings, SEGMENT, notices, NoticesHud::NOTICE_SEGMENT);
    }

    // NoticesHud: load notices from named keys
    void loadNotices(const SettingsManager::HudSettings& settings, uint32_t& notices) {
        using namespace Keys::Notices;
        loadBitFromKey(settings, WRONG_WAY, notices, NoticesHud::NOTICE_WRONG_WAY);
        loadBitFromKey(settings, BLUE_FLAG, notices, NoticesHud::NOTICE_BLUE_FLAG);
        loadBitFromKey(settings, LAST_LAP, notices, NoticesHud::NOTICE_LAST_LAP);
        loadBitFromKey(settings, FINISHED, notices, NoticesHud::NOTICE_FINISHED);
        loadBitFromKey(settings, ALLTIME_PB, notices, NoticesHud::NOTICE_ALLTIME_PB);
        loadBitFromKey(settings, FASTEST_LAP, notices, NoticesHud::NOTICE_FASTEST_LAP);
        loadBitFromKey(settings, SESSION_PB, notices, NoticesHud::NOTICE_SESSION_PB);
        loadBitFromKey(settings, DEFAULT_SETUP, notices, NoticesHud::NOTICE_DEFAULT_SETUP);
        loadBitFromKey(settings, OVERTIME, notices, NoticesHud::NOTICE_OVERTIME);
        loadBitFromKey(settings, HAZARD_STATIONARY, notices, NoticesHud::NOTICE_HAZARD_STATIONARY);
        loadBitFromKey(settings, HAZARD_WRONG_WAY, notices, NoticesHud::NOTICE_HAZARD_WRONG_WAY);
        loadBitFromKey(settings, SEGMENT, notices, NoticesHud::NOTICE_SEGMENT);
    }

    // TelemetryHud: save elements as named keys
    void saveTelemetryElements(SettingsManager::HudSettings& settings, uint32_t elems) {
        using namespace Keys::TelemetryElems;
        saveBitAsKey(settings, THROTTLE, elems, TelemetryHud::ELEM_THROTTLE);
        saveBitAsKey(settings, FRONT_BRAKE, elems, TelemetryHud::ELEM_FRONT_BRAKE);
        saveBitAsKey(settings, REAR_BRAKE, elems, TelemetryHud::ELEM_REAR_BRAKE);
        saveBitAsKey(settings, CLUTCH, elems, TelemetryHud::ELEM_CLUTCH);
        saveBitAsKey(settings, RPM, elems, TelemetryHud::ELEM_RPM);
        saveBitAsKey(settings, FRONT_SUSP, elems, TelemetryHud::ELEM_FRONT_SUSP);
        saveBitAsKey(settings, REAR_SUSP, elems, TelemetryHud::ELEM_REAR_SUSP);
        saveBitAsKey(settings, GEAR, elems, TelemetryHud::ELEM_GEAR);
    }

    // TelemetryHud: load elements from named keys
    void loadTelemetryElements(const SettingsManager::HudSettings& settings, uint32_t& elems) {
        using namespace Keys::TelemetryElems;
        loadBitFromKey(settings, THROTTLE, elems, TelemetryHud::ELEM_THROTTLE);
        loadBitFromKey(settings, FRONT_BRAKE, elems, TelemetryHud::ELEM_FRONT_BRAKE);
        loadBitFromKey(settings, REAR_BRAKE, elems, TelemetryHud::ELEM_REAR_BRAKE);
        loadBitFromKey(settings, CLUTCH, elems, TelemetryHud::ELEM_CLUTCH);
        loadBitFromKey(settings, RPM, elems, TelemetryHud::ELEM_RPM);
        loadBitFromKey(settings, FRONT_SUSP, elems, TelemetryHud::ELEM_FRONT_SUSP);
        loadBitFromKey(settings, REAR_SUSP, elems, TelemetryHud::ELEM_REAR_SUSP);
        loadBitFromKey(settings, GEAR, elems, TelemetryHud::ELEM_GEAR);
    }

    // PerformanceHud: save elements as named keys
    void savePerformanceElements(SettingsManager::HudSettings& settings, uint32_t elems) {
        using namespace Keys::PerformanceElems;
        saveBitAsKey(settings, FPS, elems, PerformanceHud::ELEM_FPS);
        saveBitAsKey(settings, CPU, elems, PerformanceHud::ELEM_CPU);
    }

    // PerformanceHud: load elements from named keys
    void loadPerformanceElements(const SettingsManager::HudSettings& settings, uint32_t& elems) {
        using namespace Keys::PerformanceElems;
        loadBitFromKey(settings, FPS, elems, PerformanceHud::ELEM_FPS);
        loadBitFromKey(settings, CPU, elems, PerformanceHud::ELEM_CPU);
    }

    // TimingHud: convert gap type to/from string
    const char* gapTypeToString(GapTypeFlags type) {
        switch (type) {
            case GAP_TO_PB: return "SESSION_PB";
            case GAP_TO_ALLTIME: return "ALLTIME_PB";
            case GAP_TO_IDEAL: return "IDEAL";
            case GAP_TO_OVERALL: return "OVERALL";
            case GAP_TO_RECORD: return "RECORD";
            case GAP_TO_LASTLAP: return "LAST_LAP";
            default: return "SESSION_PB";
        }
    }

    GapTypeFlags stringToGapType(const std::string& str) {
        if (str == "SESSION_PB") return GAP_TO_PB;
        if (str == "ALLTIME_PB") return GAP_TO_ALLTIME;
        if (str == "IDEAL") return GAP_TO_IDEAL;
        if (str == "OVERALL") return GAP_TO_OVERALL;
        if (str == "RECORD") return GAP_TO_RECORD;
        if (str == "LAST_LAP") return GAP_TO_LASTLAP;
        return GAP_TO_PB;  // Default
    }

    // TimingHud: save secondary gap types as named keys
    void saveTimingSecondaryGaps(SettingsManager::HudSettings& settings, uint8_t gaps) {
        using namespace Keys::TimingGaps;
        saveBitAsKey(settings, TO_PB, gaps, GAP_TO_PB);
        saveBitAsKey(settings, TO_IDEAL, gaps, GAP_TO_IDEAL);
        saveBitAsKey(settings, TO_OVERALL, gaps, GAP_TO_OVERALL);
        saveBitAsKey(settings, TO_ALLTIME, gaps, GAP_TO_ALLTIME);
        saveBitAsKey(settings, TO_RECORD, gaps, GAP_TO_RECORD);
        saveBitAsKey(settings, TO_LASTLAP, gaps, GAP_TO_LASTLAP);
    }

    // TimingHud: load secondary gap types from named keys
    void loadTimingSecondaryGaps(const SettingsManager::HudSettings& settings, uint8_t& gaps) {
        using namespace Keys::TimingGaps;
        uint32_t gaps32 = gaps;
        loadBitFromKey(settings, TO_PB, gaps32, GAP_TO_PB);
        loadBitFromKey(settings, TO_IDEAL, gaps32, GAP_TO_IDEAL);
        loadBitFromKey(settings, TO_OVERALL, gaps32, GAP_TO_OVERALL);
        loadBitFromKey(settings, TO_ALLTIME, gaps32, GAP_TO_ALLTIME);
        loadBitFromKey(settings, TO_RECORD, gaps32, GAP_TO_RECORD);
        loadBitFromKey(settings, TO_LASTLAP, gaps32, GAP_TO_LASTLAP);
        gaps = static_cast<uint8_t>(gaps32);
    }

    // Helper to apply base HUD settings from a map
    // Buffers position to apply X/Y together atomically
    void applyBaseHudSettings(BaseHud& hud, const SettingsManager::HudSettings& settings) {
        using namespace Keys::Base;
        float pendingOffsetX = 0, pendingOffsetY = 0;
        bool hasOffsetX = false, hasOffsetY = false;

        // Each profile's cache is the complete intended state for the HUD (base keys
        // are merged into every profile on load), so an absent color_/font_ key means
        // "no override". Track which ones we see and clear the rest below — without
        // this, a per-HUD override would leak across profile switches and survive a
        // reset, since these private BaseHud members aren't touched by resetToDefaults().
        std::array<bool, static_cast<size_t>(ColorSlot::COUNT)> colorSeen{};
        std::array<bool, static_cast<size_t>(FontCategory::COUNT)> fontSeen{};
        bool dropShadowSeen = false;

        for (const auto& [key, value] : settings) {
            try {
                if (key == VISIBLE) {
                    hud.setVisible(std::stoi(value) != 0);
                } else if (key == SHOW_TITLE) {
                    hud.setShowTitle(std::stoi(value) != 0);
                } else if (key == SHOW_BG_TEXTURE) {
                    hud.setShowBackgroundTexture(std::stoi(value) != 0);
                } else if (key == TEXTURE_VARIANT) {
                    hud.setTextureVariant(std::stoi(value));
                } else if (key == BG_OPACITY) {
                    hud.setBackgroundOpacity(validateOpacity(std::stof(value)));
                } else if (key == SCALE) {
                    hud.setScale(validateScale(std::stof(value)));
                } else if (key == OFFSET_X) {
                    pendingOffsetX = validateOffset(std::stof(value));
                    hasOffsetX = true;
                } else if (key == OFFSET_Y) {
                    pendingOffsetY = validateOffset(std::stof(value));
                    hasOffsetY = true;
                } else {
                    // Per-HUD color/font overrides (power user INI feature)
                    ColorSlot colorSlot = parseColorKey(key);
                    if (colorSlot != ColorSlot::COUNT) {
                        hud.setColorOverride(colorSlot, PluginUtils::parseColorHex(value));
                        colorSeen[static_cast<size_t>(colorSlot)] = true;
                        continue;
                    }
                    FontCategory fontCategory = parseFontKey(key);
                    if (fontCategory != FontCategory::COUNT) {
                        hud.setFontOverride(fontCategory, value);
                        fontSeen[static_cast<size_t>(fontCategory)] = true;
                        continue;
                    }
                    if (key == "dropShadow") {
                        hud.setDropShadowOverride(std::stoi(value) != 0);
                        dropShadowSeen = true;
                        continue;
                    }
                }
            } catch (...) {
                DEBUG_WARN_F("Failed to parse base setting '%s=%s'", key.c_str(), value.c_str());
            }
        }

        // Clear any override not present in the applied settings (authoritative apply).
        for (size_t i = 0; i < colorSeen.size(); ++i) {
            if (!colorSeen[i]) hud.clearColorOverride(static_cast<ColorSlot>(i));
        }
        for (size_t i = 0; i < fontSeen.size(); ++i) {
            if (!fontSeen[i]) hud.clearFontOverride(static_cast<FontCategory>(i));
        }
        if (!dropShadowSeen) hud.clearDropShadowOverride();
        // Apply buffered position
        if (hasOffsetX || hasOffsetY) {
            float finalX = hasOffsetX ? pendingOffsetX : hud.getOffsetX();
            float finalY = hasOffsetY ? pendingOffsetY : hud.getOffsetY();
            hud.setPosition(finalX, finalY);
        }
    }
}

SettingsManager& SettingsManager::getInstance() {
    static SettingsManager instance;
    return instance;
}

std::string SettingsManager::getSettingsFilePath(const char* savePath) const {
    if (!savePath || savePath[0] == '\0') {
        // Use relative path when savePath is not provided
        std::string subdir = std::string(".\\") + SETTINGS_SUBDIRECTORY;
        if (!CreateDirectoryA(subdir.c_str(), NULL)) {
            DWORD error = GetLastError();
            if (error != ERROR_ALREADY_EXISTS) {
                DEBUG_WARN_F("Failed to create settings directory: %s (error %lu)", subdir.c_str(), error);
            }
        }
        return subdir + "\\" + SETTINGS_FILENAME;
    }

    std::string path = savePath;
    if (!path.empty() && path.back() != '/' && path.back() != '\\') {
        path += '\\';
    }
    path += SETTINGS_SUBDIRECTORY;

    if (!CreateDirectoryA(path.c_str(), NULL)) {
        DWORD error = GetLastError();
        if (error != ERROR_ALREADY_EXISTS) {
            DEBUG_WARN_F("Failed to create settings directory: %s (error %lu)", path.c_str(), error);
        }
    }

    path += '\\';
    path += SETTINGS_FILENAME;
    return path;
}

void SettingsManager::captureCurrentState(const HudManager& hudManager) {
    ProfileType activeProfile = ProfileManager::getInstance().getActiveProfile();
    captureToProfile(hudManager, activeProfile);
}

// Helper: capture all HUD settings to a cache (shared by captureToProfile and captureFactoryDefaults)
void SettingsManager::captureToCache(const HudManager& hudManager, ProfileCache& cache) {
    cache.clear();

    // Capture StandingsHud including INI-only player-row-highlight settings
    {
        using namespace Keys::Standings;
        HudSettings settings;
        const auto& hud = hudManager.getStandingsHud();
        captureBaseHudSettings(settings, hud);
        settings[DISPLAY_ROW_COUNT] = std::to_string(hud.m_displayRowCount);
        saveStandingsColumns(settings, hud.m_enabledColumns);  // Named keys instead of bitmask
        settings[GAP_MODE] = gapModeToString(hud.m_gapMode);
        settings[POSGAIN_MODE] = posGainModeToString(hud.m_posGainMode);
        settings[GAP_REFERENCE_MODE] = gapReferenceModeToString(hud.m_gapReferenceMode);
        settings[ANIMATION_MODE] = animationModeToString(hud.m_animationMode);
        settings[SHOW_HEADERS] = hud.m_bShowHeaders ? "1" : "0";
        settings[SHOW_SESSION_INFO] = hud.m_bShowSessionInfo ? "1" : "0";
        settings[LIVE_GAPS] = hud.m_bLiveGaps ? "1" : "0";
        settings[IniOnly::Standings::TOP_POSITIONS.key] = std::to_string(hud.m_topPositionsCount);
        settings[IniOnly::Standings::PLAYER_ROW_HIGHLIGHT.key] = hud.m_bPlayerRowHighlight ? "1" : "0";
        settings[IniOnly::Standings::PLAYER_ROW_HIGHLIGHT_BRAND.key] = hud.m_bPlayerRowHighlightBrand ? "1" : "0";
        settings[IniOnly::Standings::LAST_LAP_COLOR.key] = hud.m_bLastLapColorCode ? "1" : "0";
        settings[IniOnly::Standings::ANIMATION_DURATION_MS.key] = std::to_string(static_cast<int>(hud.m_animationDurationMs));
        settings[IniOnly::Standings::CLASSIC_LAYOUT.key] = hud.m_bClassicLayout ? "1" : "0";
        settings[IniOnly::Standings::NAME_MODE.key] = std::to_string(static_cast<int>(hud.m_nameMode));
        settings[IniOnly::Standings::SHORT_NAME_CHARS.key] = std::to_string(hud.m_shortNameChars);
        settings[IniOnly::Standings::LONG_NAME_CHARS.key] = std::to_string(hud.m_longNameChars);
        cache["StandingsHud"] = std::move(settings);
    }

    // Capture MapHud (uses anchor-based positioning instead of offsetX/Y)
    {
        HudSettings settings;
        const auto& hud = hudManager.getMapHud();
        captureBaseHudSettings(settings, hud, false);  // Exclude offsetX/Y - anchor is source of truth
        // Map-specific settings
        settings["rotateToPlayer"] = std::to_string(hud.getRotateToPlayer() ? 1 : 0);
        settings["showOutline"] = std::to_string(hud.getShowOutline() ? 1 : 0);
        settings["riderColorMode"] = riderColorModeToString(hud.getRiderColorMode());
        settings["trackWidthScale"] = std::to_string(hud.getTrackWidthScale());
        settings["labelMode"] = labelModeToString(hud.getLabelMode());
        settings[IniOnly::Map::LABEL_ANCHOR.key] = labelAnchorToString(hud.getLabelAnchor());
        settings["riderShape"] = shapeIndexToFilename(hud.getRiderShape());
        settings["anchorPoint"] = anchorPointToString(hud.getAnchorPoint());
        settings["anchorX"] = std::to_string(hud.m_fAnchorX);
        settings["anchorY"] = std::to_string(hud.m_fAnchorY);
        settings["zoomEnabled"] = std::to_string(hud.getZoomEnabled() ? 1 : 0);
        settings["zoomDistance"] = std::to_string(hud.getZoomDistance());
        settings["markerScale"] = std::to_string(hud.getMarkerScale());
        settings["detail"] = detailToString(hud.getDetail());
        cache["MapHud"] = std::move(settings);
    }

    // Capture RadarHud
    {
        HudSettings settings;
        const auto& hud = hudManager.getRadarHud();
        captureBaseHudSettings(settings, hud);
        settings["radarRange"] = std::to_string(hud.getRadarRange());
        settings["riderColorMode"] = radarRiderColorModeToString(hud.getRiderColorMode());
        settings["radarMode"] = radarModeToString(hud.getRadarMode());
        settings["proximityArrowMode"] = proximityArrowModeToString(hud.getProximityArrowMode());
        settings["proximityArrowShape"] = shapeIndexToFilename(hud.getProximityArrowShape());
        settings["proximityArrowScale"] = std::to_string(hud.getProximityArrowScale());
        settings["proximityArrowColorMode"] = proximityArrowColorModeToString(hud.getProximityArrowColorMode());
        settings["alertDistance"] = std::to_string(hud.getAlertDistance());
        settings["labelMode"] = radarLabelModeToString(hud.getLabelMode());
        settings["riderShape"] = shapeIndexToFilename(hud.getRiderShape());
        settings["markerScale"] = std::to_string(hud.getMarkerScale());
        cache["RadarHud"] = std::move(settings);
    }

    // Capture PitboardHud
    {
        HudSettings settings;
        const auto& hud = hudManager.getPitboardHud();
        captureBaseHudSettings(settings, hud);
        savePitboardRows(settings, hud.m_enabledRows);  // Named keys instead of bitmask
        settings["displayMode"] = pitboardDisplayModeToString(hud.m_displayMode);
        settings["gapCompareMode"] = pitboardGapCompareModeToString(hud.m_gapCompareMode);
        cache["PitboardHud"] = std::move(settings);
    }

#if GAME_HAS_RECORDS_PROVIDER
    // Capture RecordsHud (autoFetch and provider are global, in [General] section)
    {
        HudSettings settings;
        const auto& hud = hudManager.getRecordsHud();
        captureBaseHudSettings(settings, hud);
        saveRecordsColumns(settings, hud.m_enabledColumns);  // Named keys instead of bitmask
        settings["recordsToShow"] = std::to_string(hud.m_recordsToShow);
        settings["showHeaders"] = hud.m_bShowHeaders ? "1" : "0";
        settings[IniOnly::Records::SHOW_FOOTER.key] = hud.m_bShowFooter ? "1" : "0";
        cache["RecordsHud"] = std::move(settings);
    }
#endif

    // Capture LapLogHud
    {
        HudSettings settings;
        const auto& hud = hudManager.getLapLogHud();
        captureBaseHudSettings(settings, hud);
        saveLapLogColumns(settings, hud.m_enabledColumns);  // Named keys instead of bitmask
        settings["maxDisplayLaps"] = std::to_string(hud.m_maxDisplayLaps);
        settings["displayOrder"] = std::to_string(static_cast<int>(hud.m_displayOrder));
        settings["showGapRow"] = hud.m_showGapRow ? "1" : "0";
        settings["showHeaders"] = hud.m_bShowHeaders ? "1" : "0";
        cache["LapLogHud"] = std::move(settings);
    }

#if GAME_HAS_STEAM_FRIENDS
    {
        HudSettings settings;
        const auto& hud = hudManager.getFriendsHud();
        captureBaseHudSettings(settings, hud);
        saveBitAsKey(settings, "col_server", hud.m_enabledColumns, FriendsHud::COL_SERVER);
        saveBitAsKey(settings, "col_track",  hud.m_enabledColumns, FriendsHud::COL_TRACK);
        saveBitAsKey(settings, "col_info",   hud.m_enabledColumns, FriendsHud::COL_INFO);
        saveBitAsKey(settings, "col_timer",  hud.m_enabledColumns, FriendsHud::COL_TIMER);
        settings["maxDisplayRows"] = std::to_string(hud.m_maxDisplayRows);
        settings["showHeaders"] = hud.m_bShowHeaders ? "1" : "0";
        settings["showMode"] = std::to_string(static_cast<int>(hud.m_showMode));
        settings["onJoinDurationMs"] = std::to_string(hud.m_onJoinDurationMs);  // INI-only
        settings["showSelf"] = hud.m_showSelf ? "1" : "0";
        cache["FriendsHud"] = std::move(settings);
    }
#endif

    // Capture LapConsistencyHud
    {
        HudSettings settings;
        const auto& hud = hudManager.getLapConsistencyHud();
        captureBaseHudSettings(settings, hud);
        settings["displayMode"] = std::to_string(static_cast<int>(hud.m_displayMode));
        settings["referenceMode"] = std::to_string(static_cast<int>(hud.m_referenceMode));
        settings["trendMode"] = std::to_string(static_cast<int>(hud.m_trendMode));
        settings["enabledStats"] = std::to_string(static_cast<int>(hud.m_enabledStats));
        settings["lapCount"] = std::to_string(hud.m_lapCount);
        // Advanced tuning (INI-only)
        settings["consistencyScaleFactor"] = std::to_string(hud.m_consistencyScaleFactor);
        settings["trendThresholdPercent"] = std::to_string(hud.m_trendThresholdPercent);
        cache["LapConsistencyHud"] = std::move(settings);
    }

#if GAME_HAS_FMX
    // Capture FmxHud (all settings per-profile, like StandingsHud)
    {
        using namespace Keys::Fmx;
        HudSettings settings;
        const auto& hud = hudManager.getFmxHud();
        captureBaseHudSettings(settings, hud);
        settings[ENABLED_ROWS] = std::to_string(hud.m_enabledRows);
        settings[MAX_CHAIN_DISPLAY_ROWS] = std::to_string(hud.m_maxChainDisplayRows);
        settings[SHOW_DEBUG_LOGGING] = hud.m_showDebugLogging ? "1" : "0";

        // Per-trick enable flags (INI-only). Skip index 0 (NONE) and the RIGHT
        // half of L/R pairs — they share a key with the LEFT variant. The
        // gate in FmxManager indexes by full enum, so apply() mirrors the
        // value to both indices when reading back.
        const auto& fmxCfg = FmxManager::getInstance().getConfig();
        for (int i = 1; i < static_cast<int>(Fmx::TrickType::COUNT); ++i) {
            auto t = static_cast<Fmx::TrickType>(i);
            if (Fmx::getTrickDirection(t) == Fmx::TrickDirection::RIGHT) continue;
            std::string key = std::string(TRICK_ENABLED_PREFIX) + Fmx::getTrickIniKey(t);
            settings[key] = fmxCfg.tricksEnabled[i] ? "1" : "0";
        }

        cache["FmxHud"] = std::move(settings);
    }
#endif

    // Capture StatsHud
    {
        using namespace Keys::Stats;
        HudSettings settings;
        const auto& hud = hudManager.getStatsHud();
        captureBaseHudSettings(settings, hud);
        settings[VISIBILITY_MODE] = std::to_string(static_cast<int>(hud.m_visibilityMode));
        settings[SHOW_LAP] = hud.m_showLap ? "1" : "0";
        settings[SHOW_SESSION] = hud.m_showSession ? "1" : "0";
        settings[SHOW_ALLTIME] = hud.m_showAllTime ? "1" : "0";
        cache["StatsHud"] = std::move(settings);
    }

    // Capture EventLogHud
    {
        HudSettings settings;
        const auto& hud = hudManager.getEventLogHud();
        captureBaseHudSettings(settings, hud);
        settings["displayMode"] = std::to_string(static_cast<int>(hud.m_displayMode));
        settings["displayOrder"] = std::to_string(static_cast<int>(hud.m_displayOrder));
        settings["maxDisplayEvents"] = std::to_string(hud.m_maxDisplayEvents);
        settings["autoHideDurationMs"] = std::to_string(hud.m_autoHideDurationMs);
        settings["timestampMode"] = std::to_string(static_cast<int>(hud.m_timestampMode));
        settings["showIcons"] = hud.m_showIcons ? "1" : "0";
        saveEventLogEvents(settings, hud.m_enabledEvents);
        cache["EventLogHud"] = std::move(settings);
    }

    // Capture IdealLapHud (key preserved for backward compatibility)
    {
        HudSettings settings;
        const auto& hud = hudManager.getIdealLapHud();
        captureBaseHudSettings(settings, hud);
        saveIdealLapRows(settings, hud.m_enabledRows);  // Named keys instead of bitmask
        cache["IdealLapHud"] = std::move(settings);
    }

    // Capture TelemetryHud
    {
        HudSettings settings;
        const auto& hud = hudManager.getTelemetryHud();
        captureBaseHudSettings(settings, hud);
        saveTelemetryElements(settings, hud.m_enabledElements);  // Named keys instead of bitmask
        settings["displayMode"] = displayModeToString(hud.m_displayMode);
        cache["TelemetryHud"] = std::move(settings);
    }

    // Capture PerformanceHud
    {
        HudSettings settings;
        const auto& hud = hudManager.getPerformanceHud();
        captureBaseHudSettings(settings, hud);
        savePerformanceElements(settings, hud.m_enabledElements);  // Named keys instead of bitmask
        settings["displayMode"] = displayModeToString(hud.m_displayMode);
        cache["PerformanceHud"] = std::move(settings);
    }

    // Capture Widgets (base properties only for most)
    auto captureWidget = [&](const char* name, const BaseHud& hud) {
        HudSettings settings;
        captureBaseHudSettings(settings, hud);
        cache[name] = std::move(settings);
    };

    captureWidget("LapWidget", hudManager.getLapWidget());
    captureWidget("PositionWidget", hudManager.getPositionWidget());
    captureWidget("TimeWidget", hudManager.getTimeWidget());
    // ClockWidget with showUtc, utcOnTop (format24h moved to [General])
    {
        HudSettings settings;
        const auto& hud = hudManager.getClockWidget();
        captureBaseHudSettings(settings, hud);
        settings["showUtc"] = hud.getShowUtc() ? "1" : "0";
        settings["utcOnTop"] = hud.getUtcOnTop() ? "1" : "0";
        cache["ClockWidget"] = std::move(settings);
    }
    // SessionHud with enabledRows and showIcons
    {
        HudSettings settings;
        const auto& hud = hudManager.getSessionHud();
        captureBaseHudSettings(settings, hud);
        saveSessionRows(settings, hud.m_enabledRows);
        settings[Keys::Session::SHOW_ICONS] = std::to_string(hud.m_bShowIcons ? 1 : 0);
        cache["SessionHud"] = std::move(settings);
    }
    // SpeedoWidget with needleColor, showOdometer, showTripmeter
    {
        HudSettings settings;
        const auto& hud = hudManager.getSpeedoWidget();
        captureBaseHudSettings(settings, hud);
        settings[IniOnly::Speedo::NEEDLE_COLOR.key] = PluginUtils::formatColorHex(hud.getNeedleColor());
        settings[IniOnly::Speedo::SHOW_ODOMETER.key] = hud.getShowOdometer() ? "1" : "0";
        settings[IniOnly::Speedo::SHOW_TRIPMETER.key] = hud.getShowTripmeter() ? "1" : "0";
        cache["SpeedoWidget"] = std::move(settings);
    }
    // TachoWidget with needleColor
    {
        HudSettings settings;
        const auto& hud = hudManager.getTachoWidget();
        captureBaseHudSettings(settings, hud);
        settings[IniOnly::Tacho::NEEDLE_COLOR.key] = PluginUtils::formatColorHex(hud.getNeedleColor());
        cache["TachoWidget"] = std::move(settings);
    }
    // BarsWidget has enabledColumns, showLabels, and showMaxMarkers
    {
        HudSettings settings;
        const auto& hud = hudManager.getBarsWidget();
        captureBaseHudSettings(settings, hud);
        saveBarsColumns(settings, hud.m_enabledColumns);
        settings["showLabels"] = hud.m_bShowLabels ? "1" : "0";
        settings["showMaxMarkers"] = hud.m_bShowMaxMarkers ? "1" : "0";
        settings["maxMarkerLingerFrames"] = std::to_string(hud.m_maxMarkerLingerFrames);
        cache["BarsWidget"] = std::move(settings);
    }
    captureWidget("VersionWidget", hudManager.getVersionWidget());
    // NoticesHud has enabledNotices and PB duration
    {
        HudSettings settings;
        const auto& hud = hudManager.getNoticesHud();
        captureBaseHudSettings(settings, hud);
        saveNotices(settings, hud.m_enabledNotices);
        settings[IniOnly::Notices::PB_DURATION.key] = std::to_string(hud.m_noticeDurationMs);
        cache["NoticesHud"] = std::move(settings);
    }
    captureWidget("SettingsButtonWidget", hudManager.getSettingsButtonWidget());
    captureWidget("PointerWidget", hudManager.getPointerWidget());
    // BenchmarkWidget (developer mode only - may not exist)
    if (hudManager.getBenchmarkWidget()) {
        captureWidget("BenchmarkWidget", *hudManager.getBenchmarkWidget());
    }
    // RumbleHud with showMaxMarkers
    {
        HudSettings settings;
        const auto& hud = hudManager.getRumbleHud();
        captureBaseHudSettings(settings, hud);
        settings["showMaxMarkers"] = hud.m_bShowMaxMarkers ? "1" : "0";
        settings["maxMarkerLingerFrames"] = std::to_string(hud.m_maxMarkerLingerFrames);
        cache["RumbleHud"] = std::move(settings);
    }

    // Note: HelmetOverlayHud is global (not per-profile) — saved/loaded
    // in its own [HelmetOverlay] section, same pattern as [Rumble].

    // SpeedWidget has enabledRows - speedUnit is now global (in Preferences section)
    {
        HudSettings settings;
        const auto& hud = hudManager.getSpeedWidget();
        captureBaseHudSettings(settings, hud);
        saveSpeedRows(settings, hud.m_enabledRows);  // Named keys instead of bitmask
        cache["SpeedWidget"] = std::move(settings);
    }

    // GearWidget with INI-only settings
    {
        HudSettings settings;
        const auto& hud = hudManager.getGearWidget();
        captureBaseHudSettings(settings, hud);
        settings[IniOnly::Gear::SHOW_SHIFT_COLOR.key] = hud.m_bShowShiftColor ? "1" : "0";
        settings[IniOnly::Gear::SHOW_LIMITER_CIRCLE.key] = hud.m_bShowLimiterCircle ? "1" : "0";
        cache["GearWidget"] = std::move(settings);
    }

    // FuelWidget has enabledRows - fuelUnit is now global (in Preferences section)
    {
        HudSettings settings;
        const auto& hud = hudManager.getFuelWidget();
        captureBaseHudSettings(settings, hud);
        saveFuelRows(settings, hud.m_enabledRows);  // Named keys instead of bitmask
        cache["FuelWidget"] = std::move(settings);
    }

    // GamepadWidget (base settings only - layouts are saved separately)
    {
        HudSettings settings;
        const auto& hud = hudManager.getGamepadWidget();
        captureBaseHudSettings(settings, hud);
        cache["GamepadWidget"] = std::move(settings);
    }

    // LeanWidget with enabledRows, showMaxMarkers, and arcFillColor
    {
        HudSettings settings;
        const auto& hud = hudManager.getLeanWidget();
        captureBaseHudSettings(settings, hud);
        saveLeanRows(settings, hud.m_enabledRows);  // Named keys instead of bitmask
        settings["showMaxMarkers"] = hud.m_bShowMaxMarkers ? "1" : "0";
        settings["maxMarkerLingerFrames"] = std::to_string(hud.m_maxMarkerLingerFrames);
        settings[IniOnly::Lean::ARC_FILL_COLOR.key] = PluginUtils::formatColorHex(hud.getArcFillColor());
        cache["LeanWidget"] = std::move(settings);
    }

    // GForceWidget with INI-only tuning (maxScale, linger, colors, text/marker toggles)
    {
        HudSettings settings;
        const auto& hud = hudManager.getGForceWidget();
        captureBaseHudSettings(settings, hud);
        settings[IniOnly::GForce::MAX_SCALE.key] = std::to_string(hud.m_maxScale);
        settings[IniOnly::GForce::SHOW_MAX_TEXT.key] = hud.m_bShowMaxText ? "1" : "0";
        settings[IniOnly::GForce::SHOW_MAX_MARKER.key] = hud.m_bShowMaxMarker ? "1" : "0";
        settings[IniOnly::GForce::MAX_MARKER_LINGER_FRAMES.key] = std::to_string(hud.m_maxMarkerLingerFrames);
        cache["GForceWidget"] = std::move(settings);
    }

    // CompassWidget with INI-only options (dial style)
    {
        HudSettings settings;
        const auto& hud = hudManager.getCompassWidget();
        captureBaseHudSettings(settings, hud);
        settings[IniOnly::Compass::STYLE.key] = compassStyleToString(hud.m_style);
        cache["CompassWidget"] = std::move(settings);
    }

#if GAME_HAS_TYRE_TEMP
    // TyreTempWidget with temperature thresholds and row toggles
    {
        HudSettings settings;
        const auto& hud = hudManager.getTyreTempWidget();
        captureBaseHudSettings(settings, hud);
        settings["coldThreshold"] = std::to_string(hud.getColdThreshold());
        settings["hotThreshold"] = std::to_string(hud.getHotThreshold());
        saveTyreTempRows(settings, hud.m_enabledRows);  // Named keys for row toggles
        settings["showLabels"] = hud.m_bShowLabels ? "1" : "0";  // INI-only
        cache["TyreTempWidget"] = std::move(settings);
    }
#endif

#if GAME_HAS_ECU
    // EcuWidget with per-chip toggles and label toggle
    {
        HudSettings settings;
        const auto& hud = hudManager.getEcuWidget();
        captureBaseHudSettings(settings, hud);
        saveEcuRows(settings, hud.m_enabledRows);  // Named keys for chip toggles
        settings["showLabels"] = hud.m_bShowLabels ? "1" : "0";
        cache["EcuWidget"] = std::move(settings);
    }
#endif

    // TimingHud has display mode, column toggles, displayDuration, and primary/secondary gaps
    {
        HudSettings settings;
        const auto& hud = hudManager.getTimingHud();
        captureBaseHudSettings(settings, hud);
        settings["displayMode"] = columnModeToString(hud.m_displayMode);
        settings["labelEnabled"] = hud.m_columnEnabled[TimingHud::COL_LABEL] ? "1" : "0";
        settings["timeEnabled"] = hud.m_columnEnabled[TimingHud::COL_TIME] ? "1" : "0";
        settings["gapEnabled"] = hud.m_columnEnabled[TimingHud::COL_GAP] ? "1" : "0";
        settings["showReference"] = hud.m_showReference ? "1" : "0";
        settings["layoutVertical"] = hud.m_layoutVertical ? "1" : "0";
        settings["displayDuration"] = std::to_string(hud.m_displayDurationMs);
        settings["primaryGap"] = gapTypeToString(hud.m_primaryGapType);
        saveTimingSecondaryGaps(settings, hud.m_secondaryGapTypes);
        cache["TimingHud"] = std::move(settings);
    }

    // GapBarHud with freeze, marker mode, icon, gap text, and range settings
    {
        HudSettings settings;
        const auto& hud = hudManager.getGapBarHud();
        captureBaseHudSettings(settings, hud);
        settings["freezeDuration"] = std::to_string(hud.m_freezeDurationMs);
        settings["markerMode"] = std::to_string(static_cast<int>(hud.m_markerMode));
        // Persist by filename (not positional index) so the choice survives icon-set
        // reordering, matching map/radar. 0 = "use default icon" serializes as "Off".
        settings["riderIcon"] = shapeIndexToFilename(hud.m_riderIconIndex);
        settings["showGapText"] = hud.m_showGapText ? "1" : "0";
        settings["showGapBar"] = hud.m_showGapBar ? "1" : "0";
        settings["gapRange"] = std::to_string(hud.m_gapRangeMs);
        settings["barWidth"] = std::to_string(hud.m_barWidthPercent);
        settings["markerScale"] = std::to_string(hud.m_fMarkerScale);
        settings["labelMode"] = std::to_string(static_cast<int>(hud.m_labelMode));
        settings["colorMode"] = gapBarRiderColorModeToString(hud.m_riderColorMode);
        cache["GapBarHud"] = std::move(settings);
    }

    // Capture global HUD manager settings (per-profile)
    {
        HudSettings settings;
        settings["widgetsEnabled"] = hudManager.areWidgetsEnabled() ? "1" : "0";
        cache["Global"] = std::move(settings);
    }
}

void SettingsManager::captureToProfile(const HudManager& hudManager, ProfileType profile) {
    if (profile >= ProfileType::COUNT) {
        DEBUG_WARN_F("captureToProfile called with invalid profile index: %d", static_cast<int>(profile));
        return;
    }
    captureToCache(hudManager, m_profileCache[static_cast<size_t>(profile)]);
    m_cacheInitialized = true;
}

void SettingsManager::captureFactoryDefaults(const HudManager& hudManager) {
    // Runtime check for release builds, assert for debug builds
    if (m_settingsLoaded) {
        DEBUG_WARN("captureFactoryDefaults() called after loadSettings() - using current values instead of defaults");
        return;
    }
    assert(!m_settingsLoaded && "captureFactoryDefaults() must be called before loadSettings()");

    if (m_factoryDefaultsCaptured) {
        return;  // Already captured
    }

    captureToCache(hudManager, m_hudDefaults);

    // Snapshot the pristine per-HUD constructor defaults for the reset paths. m_hudDefaults
    // is about to accumulate user-edited base-section keys during loadSettings() (the fold at
    // the [HudName] base-section handler), which is correct for sparse-save round-tripping but
    // would make "reset to defaults" restore the file's baseline instead of this build's
    // defaults. Keep an untouched copy so reset means factory, symmetric with m_globalDefaultsIni.
    m_hudFactoryDefaults = m_hudDefaults;

    // Capture the global (non-per-profile) sections as INI text now, so the reset paths can
    // replay them later. Reusing writeGlobalSettings() keeps this snapshot in lockstep with
    // save. INVARIANT: every subsystem must already hold its factory-default state when this
    // runs. Most do via their constructor, but some establish defaults in an initialize()
    // instead — notably HotkeyManager::initialize() sets the default key bindings (e.g. the
    // settings-toggle key). plugin_manager.cpp calls those initialize()s before HudManager's,
    // which is what triggers loadSettings()/this capture. If init is ever reordered so this
    // runs first, the snapshot would capture "unset" defaults and reset would wipe those
    // bindings — so keep subsystem default-init ahead of loadSettings().
    {
        std::ostringstream globalDefaults;
        writeGlobalSettings(globalDefaults, hudManager);
        m_globalDefaultsIni = globalDefaults.str();
    }
    m_factoryDefaultsCaptured = true;
    DEBUG_INFO("Captured HUD factory defaults for sparse INI format");
}

// ============================================================================
// applyActiveProfile / applyProfile - Apply cached settings to HUDs
// ============================================================================

void SettingsManager::applyActiveProfile(HudManager& hudManager) {
    ProfileType activeProfile = ProfileManager::getInstance().getActiveProfile();
    applyProfile(hudManager, activeProfile);
}

void SettingsManager::applyProfile(HudManager& hudManager, ProfileType profile) {
    if (profile >= ProfileType::COUNT) {
        DEBUG_WARN_F("applyProfile called with invalid profile index: %d", static_cast<int>(profile));
        return;
    }
    if (!m_cacheInitialized) {
        DEBUG_INFO("applyProfile skipped - cache not yet initialized (normal during first load)");
        return;
    }

    const ProfileCache& cache = m_profileCache[static_cast<size_t>(profile)];

    // Helper to apply settings to a HUD
    auto applyToHud = [&](const char* hudName, BaseHud& hud) {
        auto it = cache.find(hudName);
        if (it == cache.end()) return;
        applyBaseHudSettings(hud, it->second);
        hud.setDataDirty();
    };

    // Apply StandingsHud including INI-only player-row-highlight settings
    {
        using namespace Keys::Standings;
        auto it = cache.find("StandingsHud");
        if (it != cache.end()) {
            auto& hud = hudManager.getStandingsHud();
            applyBaseHudSettings(hud, it->second);

            const auto& settings = it->second;
            try {
                if (settings.count(DISPLAY_ROW_COUNT)) hud.m_displayRowCount = validateDisplayRows(std::stoi(settings.at(DISPLAY_ROW_COUNT)));
                loadStandingsColumns(settings, hud.m_enabledColumns);  // Named keys instead of bitmask
                // Positions-gained mode. Migrate from the old col_posgain bit (a plain on/off
                // that meant "since race start") when the new key is absent.
                if (settings.count(POSGAIN_MODE)) {
                    hud.m_posGainMode = stringToPosGainMode(settings.at(POSGAIN_MODE));
                } else {
                    hud.m_posGainMode = (hud.m_enabledColumns & StandingsHud::COL_POSGAIN)
                        ? StandingsHud::PosGainMode::RACE_START
                        : StandingsHud::PosGainMode::OFF;
                }
                if (settings.count(GAP_MODE)) {
                    hud.m_gapMode = stringToGapMode(settings.at(GAP_MODE));
                } else if (settings.count("showGapColumn")) {
                    // Migrate from old showGapColumn + gapScope
                    bool wasOn = std::stoi(settings.at("showGapColumn")) != 0;
                    if (!wasOn) {
                        hud.m_gapMode = StandingsHud::GapMode::OFF;
                    } else if (settings.count("gapScope") && settings.at("gapScope") == "PLAYER") {
                        hud.m_gapMode = StandingsHud::GapMode::PLAYER;
                    } else {
                        hud.m_gapMode = StandingsHud::GapMode::ALL;
                    }
                } else if (settings.count("gapColumnMode")) {
                    // Migrate from older GapColumnMode
                    hud.m_gapMode = (settings.at("gapColumnMode") == "OFF")
                        ? StandingsHud::GapMode::OFF : StandingsHud::GapMode::ALL;
                } else if (settings.count("officialGapMode") || settings.count("liveGapMode")) {
                    // Migrate from oldest officialGapMode/liveGapMode keys
                    bool hadOfficial = settings.count("officialGapMode") && settings.at("officialGapMode") != "OFF";
                    bool hadLive = settings.count("liveGapMode") && settings.at("liveGapMode") != "OFF";
                    if (!hadOfficial && !hadLive) {
                        hud.m_gapMode = StandingsHud::GapMode::OFF;
                    } else {
                        bool wasPlayer = (settings.count("officialGapMode") && settings.at("officialGapMode") == "PLAYER") ||
                                         (settings.count("liveGapMode") && settings.at("liveGapMode") == "PLAYER");
                        hud.m_gapMode = wasPlayer ? StandingsHud::GapMode::PLAYER : StandingsHud::GapMode::ALL;
                    }
                }
                if (settings.count(GAP_REFERENCE_MODE)) {
                    hud.m_gapReferenceMode = stringToGapReferenceMode(settings.at(GAP_REFERENCE_MODE));
                    if (hud.m_gapReferenceMode == StandingsHud::GapReferenceMode::ALTERNATING) {
                        hud.m_lastGapRefToggle = std::chrono::steady_clock::now();
                        hud.m_alternatingCurrent = StandingsHud::GapReferenceMode::LEADER;
                    }
                }
                if (settings.count(IniOnly::Standings::TOP_POSITIONS.key)) {
                    int topPos = std::stoi(settings.at(IniOnly::Standings::TOP_POSITIONS.key));
                    topPos = std::max(0, std::min(topPos, static_cast<int>(StandingsHud::MAX_TOP_POSITIONS)));
                    hud.m_topPositionsCount = topPos;
                }
                if (settings.count(IniOnly::Standings::PLAYER_ROW_HIGHLIGHT.key)) {
                    hud.m_bPlayerRowHighlight = std::stoi(settings.at(IniOnly::Standings::PLAYER_ROW_HIGHLIGHT.key)) != 0;
                }
                if (settings.count(IniOnly::Standings::PLAYER_ROW_HIGHLIGHT_BRAND.key)) {
                    hud.m_bPlayerRowHighlightBrand = std::stoi(settings.at(IniOnly::Standings::PLAYER_ROW_HIGHLIGHT_BRAND.key)) != 0;
                }
                if (settings.count(IniOnly::Standings::LAST_LAP_COLOR.key)) {
                    hud.m_bLastLapColorCode = std::stoi(settings.at(IniOnly::Standings::LAST_LAP_COLOR.key)) != 0;
                }
                if (settings.count(ANIMATION_MODE)) {
                    hud.m_animationMode = stringToAnimationMode(settings.at(ANIMATION_MODE));
                } else if (settings.count("animatePositions")) {
                    // Legacy: animatePositions=0/1 → OFF/BASIC. Read on load only; not
                    // re-emitted on save, so the key drops out of the INI on first
                    // re-save (intentional — replaced by animationMode).
                    hud.m_animationMode = (std::stoi(settings.at("animatePositions")) != 0)
                        ? StandingsHud::AnimationMode::BASIC
                        : StandingsHud::AnimationMode::OFF;
                }
                if (settings.count(SHOW_HEADERS)) {
                    hud.m_bShowHeaders = std::stoi(settings.at(SHOW_HEADERS)) != 0;
                }
                if (settings.count(SHOW_SESSION_INFO)) {
                    hud.m_bShowSessionInfo = std::stoi(settings.at(SHOW_SESSION_INFO)) != 0;
                }
                if (settings.count(LIVE_GAPS)) {
                    hud.m_bLiveGaps = std::stoi(settings.at(LIVE_GAPS)) != 0;
                }
                if (settings.count(IniOnly::Standings::ANIMATION_DURATION_MS.key)) {
                    int durationMs = std::stoi(settings.at(IniOnly::Standings::ANIMATION_DURATION_MS.key));
                    hud.m_animationDurationMs = static_cast<float>(std::max(50, std::min(1000, durationMs)));
                }
                if (settings.count(IniOnly::Standings::CLASSIC_LAYOUT.key)) {
                    hud.m_bClassicLayout = std::stoi(settings.at(IniOnly::Standings::CLASSIC_LAYOUT.key)) != 0;
                }
                if (settings.count(IniOnly::Standings::NAME_MODE.key)) {
                    int mode = std::stoi(settings.at(IniOnly::Standings::NAME_MODE.key));
                    if (mode >= 0 && mode <= 2) {
                        hud.m_nameMode = static_cast<StandingsHud::NameMode>(mode);
                    }
                } else {
                    // Migrate from old col_name boolean: off -> OFF, on -> SHORT
                    hud.m_nameMode = (hud.m_enabledColumns & StandingsHud::COL_NAME)
                        ? StandingsHud::NameMode::SHORT : StandingsHud::NameMode::OFF;
                }
                if (settings.count(IniOnly::Standings::SHORT_NAME_CHARS.key)) {
                    int chars = std::stoi(settings.at(IniOnly::Standings::SHORT_NAME_CHARS.key));
                    hud.m_shortNameChars = std::max(StandingsHud::MIN_SHORT_NAME_CHARS,
                        std::min(chars, StandingsHud::MAX_SHORT_NAME_CHARS));
                }
                if (settings.count(IniOnly::Standings::LONG_NAME_CHARS.key)) {
                    int chars = std::stoi(settings.at(IniOnly::Standings::LONG_NAME_CHARS.key));
                    hud.m_longNameChars = std::max(StandingsHud::MIN_LONG_NAME_CHARS,
                        std::min(chars, StandingsHud::MAX_LONG_NAME_CHARS));
                }
            } catch (const std::exception& e) {
                DEBUG_WARN_F("StandingsHud: Failed to parse settings: %s", e.what());
            }
            hud.setDataDirty();
        }
    }

    // Apply MapHud (uses anchor-based positioning instead of offsetX/Y)
    {
        auto it = cache.find("MapHud");
        if (it != cache.end()) {
            auto& hud = hudManager.getMapHud();
            const auto& settings = it->second;
            applyBaseHudSettings(hud, settings);  // Position comes from anchor, not offsetX/Y

            try {
                // Map-specific settings
                if (settings.count("rotateToPlayer")) hud.setRotateToPlayer(std::stoi(settings.at("rotateToPlayer")) != 0);
                if (settings.count("showOutline")) hud.setShowOutline(std::stoi(settings.at("showOutline")) != 0);
                if (settings.count("riderColorMode")) hud.setRiderColorMode(stringToRiderColorMode(settings.at("riderColorMode")));
                if (settings.count("trackWidthScale")) hud.setTrackWidthScale(validateTrackWidthScale(std::stof(settings.at("trackWidthScale"))));
                if (settings.count("labelMode")) hud.setLabelMode(stringToLabelMode(settings.at("labelMode")));
                if (settings.count(IniOnly::Map::LABEL_ANCHOR.key)) hud.setLabelAnchor(stringToLabelAnchor(settings.at(IniOnly::Map::LABEL_ANCHOR.key)));
                if (settings.count("riderShape")) hud.setRiderShape(filenameToShapeIndex(settings.at("riderShape"), 1));
                if (settings.count("zoomEnabled")) hud.setZoomEnabled(std::stoi(settings.at("zoomEnabled")) != 0);
                if (settings.count("zoomDistance")) hud.setZoomDistance(validateZoomDistance(std::stof(settings.at("zoomDistance"))));
                if (settings.count("markerScale")) hud.setMarkerScale(std::stof(settings.at("markerScale")));
                if (settings.count("detail")) hud.setDetail(stringToDetail(settings.at("detail")));

                // Anchor-based positioning
                if (settings.count("anchorPoint")) hud.setAnchorPoint(stringToAnchorPoint(settings.at("anchorPoint")));
                if (settings.count("anchorX")) hud.m_fAnchorX = std::stof(settings.at("anchorX"));
                if (settings.count("anchorY")) hud.m_fAnchorY = std::stof(settings.at("anchorY"));
                hud.updatePositionFromAnchor();
            } catch (const std::exception& e) {
                DEBUG_WARN_F("MapHud: Failed to parse settings: %s", e.what());
            }
            hud.setDataDirty();
        }
    }

    // Apply RadarHud
    {
        auto it = cache.find("RadarHud");
        if (it != cache.end()) {
            auto& hud = hudManager.getRadarHud();
            applyBaseHudSettings(hud, it->second);

            const auto& settings = it->second;
            try {
                if (settings.count("radarRange")) {
                    float range = std::stof(settings.at("radarRange"));
                    if (range < RadarHud::MIN_RADAR_RANGE) range = RadarHud::MIN_RADAR_RANGE;
                    if (range > RadarHud::MAX_RADAR_RANGE) range = RadarHud::MAX_RADAR_RANGE;
                    hud.setRadarRange(range);
                }
                // riderColorMode - string enum
                if (settings.count("riderColorMode")) {
                    hud.setRiderColorMode(stringToRadarRiderColorMode(settings.at("riderColorMode")));
                }
                if (settings.count("radarMode")) {
                    hud.setRadarMode(stringToRadarMode(settings.at("radarMode")));
                }
                if (settings.count("proximityArrowMode")) {
                    hud.setProximityArrowMode(stringToProximityArrowMode(settings.at("proximityArrowMode")));
                }
                if (settings.count("alertDistance")) {
                    float distance = std::stof(settings.at("alertDistance"));
                    if (distance < RadarHud::MIN_ALERT_DISTANCE) distance = RadarHud::MIN_ALERT_DISTANCE;
                    if (distance > RadarHud::MAX_ALERT_DISTANCE) distance = RadarHud::MAX_ALERT_DISTANCE;
                    hud.setAlertDistance(distance);
                }
                if (settings.count("labelMode")) hud.setLabelMode(stringToRadarLabelMode(settings.at("labelMode")));
                if (settings.count("riderShape")) {
                    hud.setRiderShape(filenameToShapeIndex(settings.at("riderShape"), 1));
                }
                if (settings.count("proximityArrowShape")) {
                    hud.setProximityArrowShape(filenameToShapeIndex(settings.at("proximityArrowShape"), 1));
                }
                if (settings.count("proximityArrowScale")) hud.setProximityArrowScale(std::stof(settings.at("proximityArrowScale")));
                if (settings.count("proximityArrowColorMode")) {
                    hud.setProximityArrowColorMode(stringToProximityArrowColorMode(settings.at("proximityArrowColorMode")));
                }
                if (settings.count("markerScale")) hud.setMarkerScale(std::stof(settings.at("markerScale")));
            } catch (const std::exception& e) {
                DEBUG_WARN_F("RadarHud: Failed to parse settings: %s", e.what());
            }
            hud.setDataDirty();
        }
    }

    // Apply PitboardHud
    {
        auto it = cache.find("PitboardHud");
        if (it != cache.end()) {
            auto& hud = hudManager.getPitboardHud();
            applyBaseHudSettings(hud, it->second);

            const auto& settings = it->second;
            try {
                loadPitboardRows(settings, hud.m_enabledRows);  // Named keys instead of bitmask
                if (settings.count("displayMode")) hud.m_displayMode = stringToPitboardDisplayMode(settings.at("displayMode"));
                if (settings.count("gapCompareMode")) hud.m_gapCompareMode = stringToPitboardGapCompareMode(settings.at("gapCompareMode"));
            } catch (const std::exception& e) {
                DEBUG_WARN_F("PitboardHud: Failed to parse settings: %s", e.what());
            }
            hud.setDataDirty();
        }
    }

#if GAME_HAS_RECORDS_PROVIDER
    // Apply RecordsHud (provider and autoFetch are global, in [General] section)
    {
        auto it = cache.find("RecordsHud");
        if (it != cache.end()) {
            auto& hud = hudManager.getRecordsHud();
            applyBaseHudSettings(hud, it->second);

            const auto& settings = it->second;
            try {
                loadRecordsColumns(settings, hud.m_enabledColumns);  // Named keys instead of bitmask
                if (settings.count("recordsToShow")) {
                    int count = std::stoi(settings.at("recordsToShow"));
                    if (count >= 3 && count <= 30) hud.m_recordsToShow = count;
                }
                if (settings.count("showHeaders")) {
                    hud.m_bShowHeaders = std::stoi(settings.at("showHeaders")) != 0;
                }
                if (settings.count(IniOnly::Records::SHOW_FOOTER.key)) {
                    hud.m_bShowFooter = std::stoi(settings.at(IniOnly::Records::SHOW_FOOTER.key)) != 0;
                }
            } catch (const std::exception& e) {
                DEBUG_WARN_F("RecordsHud: Failed to parse settings: %s", e.what());
            }
            hud.setDataDirty();
        }
    }
#endif

    // Apply LapLogHud
    {
        auto it = cache.find("LapLogHud");
        if (it != cache.end()) {
            auto& hud = hudManager.getLapLogHud();
            applyBaseHudSettings(hud, it->second);

            const auto& settings = it->second;
            try {
                loadLapLogColumns(settings, hud.m_enabledColumns);  // Named keys instead of bitmask
                if (settings.count("maxDisplayLaps")) hud.m_maxDisplayLaps = validateDisplayLaps(std::stoi(settings.at("maxDisplayLaps")));
                if (settings.count("displayOrder")) {
                    int order = std::stoi(settings.at("displayOrder"));
                    hud.m_displayOrder = (order == 1) ? LapLogHud::DisplayOrder::NEWEST_FIRST : LapLogHud::DisplayOrder::OLDEST_FIRST;
                }
                if (settings.count("showGapRow")) hud.m_showGapRow = (settings.at("showGapRow") == "1");
                if (settings.count("showHeaders")) hud.m_bShowHeaders = std::stoi(settings.at("showHeaders")) != 0;
            } catch (const std::exception& e) {
                DEBUG_WARN_F("LapLogHud: Failed to parse settings: %s", e.what());
            }
            hud.setDataDirty();
        }
    }

#if GAME_HAS_STEAM_FRIENDS
    // Apply FriendsHud
    {
        auto it = cache.find("FriendsHud");
        if (it != cache.end()) {
            auto& hud = hudManager.getFriendsHud();
            applyBaseHudSettings(hud, it->second);

            const auto& settings = it->second;
            try {
                loadBitFromKey(settings, "col_server", hud.m_enabledColumns, FriendsHud::COL_SERVER);
                loadBitFromKey(settings, "col_track",  hud.m_enabledColumns, FriendsHud::COL_TRACK);
                loadBitFromKey(settings, "col_info",   hud.m_enabledColumns, FriendsHud::COL_INFO);
                loadBitFromKey(settings, "col_timer",  hud.m_enabledColumns, FriendsHud::COL_TIMER);
                if (settings.count("maxDisplayRows")) {
                    int r = std::stoi(settings.at("maxDisplayRows"));
                    hud.m_maxDisplayRows = std::max(FriendsHud::MIN_DISPLAY_ROWS, std::min(FriendsHud::MAX_DISPLAY_ROWS, r));
                }
                if (settings.count("showHeaders")) hud.m_bShowHeaders = std::stoi(settings.at("showHeaders")) != 0;
                if (settings.count("showMode")) {
                    int sm = std::stoi(settings.at("showMode"));
                    if (sm >= 0 && sm < static_cast<int>(FriendsHud::ShowMode::COUNT)) {
                        hud.m_showMode = static_cast<FriendsHud::ShowMode>(sm);
                    }
                }
                if (settings.count("onJoinDurationMs")) {
                    int ms = std::stoi(settings.at("onJoinDurationMs"));
                    hud.m_onJoinDurationMs = std::max(1000, std::min(120000, ms));  // 1s..2min
                }
                if (settings.count("showSelf")) hud.m_showSelf = std::stoi(settings.at("showSelf")) != 0;
            } catch (const std::exception& e) {
                DEBUG_WARN_F("FriendsHud: Failed to parse settings: %s", e.what());
            }
            hud.setDataDirty();
        }
    }
#endif

    // Apply LapConsistencyHud
    {
        auto it = cache.find("LapConsistencyHud");
        if (it != cache.end()) {
            auto& hud = hudManager.getLapConsistencyHud();
            applyBaseHudSettings(hud, it->second);

            const auto& settings = it->second;
            try {
                if (settings.count("displayMode")) {
                    int mode = std::stoi(settings.at("displayMode"));
                    if (mode >= 0 && mode <= 2) {
                        hud.m_displayMode = static_cast<uint8_t>(mode);
                    }
                }
                if (settings.count("referenceMode")) {
                    int mode = std::stoi(settings.at("referenceMode"));
                    int maxMode = static_cast<int>(LapConsistencyHud::ReferenceMode::REFERENCE_COUNT) - 1;
                    if (mode >= 0 && mode <= maxMode) {
                        hud.m_referenceMode = static_cast<LapConsistencyHud::ReferenceMode>(mode);
                    }
                }
                if (settings.count("trendMode")) {
                    int mode = std::stoi(settings.at("trendMode"));
                    int maxMode = static_cast<int>(LapConsistencyHud::TrendMode::TREND_COUNT) - 1;
                    if (mode >= 0 && mode <= maxMode) {
                        hud.m_trendMode = static_cast<LapConsistencyHud::TrendMode>(mode);
                    }
                }
                if (settings.count("enabledStats")) {
                    int stats = std::stoi(settings.at("enabledStats"));
                    hud.m_enabledStats = static_cast<uint32_t>(stats);
                }
                if (settings.count("lapCount")) {
                    int count = std::stoi(settings.at("lapCount"));
                    hud.m_lapCount = std::max(LapConsistencyHud::MIN_LAP_COUNT,
                                              std::min(count, LapConsistencyHud::MAX_LAP_COUNT));
                }
                // Advanced tuning (INI-only)
                if (settings.count("consistencyScaleFactor")) {
                    float factor = std::stof(settings.at("consistencyScaleFactor"));
                    hud.m_consistencyScaleFactor = std::max(1.0f, std::min(factor, 100.0f));
                }
                if (settings.count("trendThresholdPercent")) {
                    float percent = std::stof(settings.at("trendThresholdPercent"));
                    hud.m_trendThresholdPercent = std::max(0.1f, std::min(percent, 10.0f));
                }
            } catch (const std::exception& e) {
                DEBUG_WARN_F("LapConsistencyHud: Failed to parse settings: %s", e.what());
            }
            hud.setDataDirty();
        }
    }

#if GAME_HAS_FMX
    // Apply FmxHud (all settings per-profile, like StandingsHud)
    {
        using namespace Keys::Fmx;
        auto it = cache.find("FmxHud");
        if (it != cache.end()) {
            auto& hud = hudManager.getFmxHud();
            applyBaseHudSettings(hud, it->second);

            const auto& settings = it->second;
            try {
                if (settings.count(ENABLED_ROWS)) {
                    hud.m_enabledRows = static_cast<uint32_t>(std::stoul(settings.at(ENABLED_ROWS)));
                }
                if (settings.count(MAX_CHAIN_DISPLAY_ROWS)) {
                    int rows = std::stoi(settings.at(MAX_CHAIN_DISPLAY_ROWS));
                    hud.m_maxChainDisplayRows = std::max(0, std::min(10, rows));
                }
                if (settings.count(SHOW_DEBUG_LOGGING)) {
                    hud.m_showDebugLogging = settings.at(SHOW_DEBUG_LOGGING) == "1";
                    FmxManager::getInstance().setLoggingEnabled(hud.m_showDebugLogging);
                }

                // Per-trick enable flags (INI-only). Always reset the mask to
                // defaults first so a profile switch is deterministic — without
                // this, a profile lacking the keys would silently inherit the
                // previously applied profile's disable state. Skip RIGHT half
                // of L/R pairs (shared key); mirror the value to both indices.
                auto& fmxMgr = FmxManager::getInstance();
                Fmx::FmxConfig fmxCfg = fmxMgr.getConfig();
                for (auto& b : fmxCfg.tricksEnabled) b = true;
                for (int i = 1; i < static_cast<int>(Fmx::TrickType::COUNT); ++i) {
                    auto t = static_cast<Fmx::TrickType>(i);
                    if (Fmx::getTrickDirection(t) == Fmx::TrickDirection::RIGHT) continue;
                    std::string key = std::string(TRICK_ENABLED_PREFIX) + Fmx::getTrickIniKey(t);
                    auto entry = settings.find(key);
                    if (entry != settings.end()) {
                        bool enabled = std::stoi(entry->second) != 0;
                        fmxCfg.tricksEnabled[i] = enabled;
                        Fmx::TrickType opposite = Fmx::flipTrickDirection(t);
                        if (opposite != t) {
                            fmxCfg.tricksEnabled[static_cast<int>(opposite)] = enabled;
                        }
                    }
                }
                fmxMgr.setConfig(fmxCfg);
            } catch (const std::exception& e) {
                DEBUG_WARN_F("FmxHud: Failed to parse settings: %s", e.what());
            }
            hud.setDataDirty();
        }
    }
#endif

    // Apply StatsHud
    {
        using namespace Keys::Stats;
        auto it = cache.find("StatsHud");
        if (it != cache.end()) {
            auto& hud = hudManager.getStatsHud();
            applyBaseHudSettings(hud, it->second);

            const auto& settings = it->second;
            try {
                if (settings.count(VISIBILITY_MODE)) {
                    int mode = std::stoi(settings.at(VISIBILITY_MODE));
                    if (mode >= 0 && mode < static_cast<int>(StatsHud::VisibilityMode::COUNT)) {
                        hud.m_visibilityMode = static_cast<StatsHud::VisibilityMode>(mode);
                    }
                }
                if (settings.count(SHOW_LAP)) {
                    hud.m_showLap = settings.at(SHOW_LAP) == "1";
                }
                if (settings.count(SHOW_SESSION)) {
                    hud.m_showSession = settings.at(SHOW_SESSION) == "1";
                }
                if (settings.count(SHOW_ALLTIME)) {
                    hud.m_showAllTime = settings.at(SHOW_ALLTIME) == "1";
                }
            } catch (const std::exception& e) {
                DEBUG_WARN_F("StatsHud: Failed to parse settings: %s", e.what());
            }
            hud.setDataDirty();
        }
    }

    // Apply EventLogHud
    {
        auto it = cache.find("EventLogHud");
        if (it != cache.end()) {
            auto& hud = hudManager.getEventLogHud();
            applyBaseHudSettings(hud, it->second);

            const auto& settings = it->second;
            try {
                loadEventLogEvents(settings, hud.m_enabledEvents);
                if (settings.count("displayMode")) {
                    int mode = std::stoi(settings.at("displayMode"));
                    if (mode >= 0 && mode <= 2) {
                        hud.m_displayMode = static_cast<EventLogHud::DisplayMode>(mode);
                    }
                }
                if (settings.count("displayOrder")) {
                    int order = std::stoi(settings.at("displayOrder"));
                    hud.m_displayOrder = (order == 1)
                        ? EventLogHud::DisplayOrder::NEWEST_FIRST
                        : EventLogHud::DisplayOrder::OLDEST_FIRST;
                }
                if (settings.count("maxDisplayEvents")) {
                    int max = std::stoi(settings.at("maxDisplayEvents"));
                    hud.m_maxDisplayEvents = std::max(EventLogHud::MIN_DISPLAY_EVENTS,
                                                      std::min(max, EventLogHud::MAX_DISPLAY_EVENTS));
                }
                if (settings.count("autoHideDurationMs")) {
                    int duration = std::stoi(settings.at("autoHideDurationMs"));
                    if (duration >= EventLogHud::MIN_AUTO_HIDE_MS && duration <= EventLogHud::MAX_AUTO_HIDE_MS) {
                        hud.m_autoHideDurationMs = duration;
                    }
                }
                if (settings.count("timestampMode")) {
                    int mode = std::stoi(settings.at("timestampMode"));
                    if (mode >= 0 && mode <= 2) {
                        hud.m_timestampMode = static_cast<EventLogHud::TimestampMode>(mode);
                    }
                }
                // Legacy: migrate old useWallClock bool to new timestampMode
                else if (settings.count("useWallClock")) {
                    hud.m_timestampMode = (settings.at("useWallClock") == "1")
                        ? EventLogHud::TimestampMode::CLOCK
                        : EventLogHud::TimestampMode::SESSION;
                }
                if (settings.count("showIcons")) {
                    hud.m_showIcons = (settings.at("showIcons") == "1");
                }
            } catch (const std::exception& e) {
                DEBUG_WARN_F("EventLogHud: Failed to parse settings: %s", e.what());
            }
            hud.setDataDirty();
        }
    }

    // Apply IdealLapHud
    {
        auto it = cache.find("IdealLapHud");
        if (it != cache.end()) {
            auto& hud = hudManager.getIdealLapHud();
            applyBaseHudSettings(hud, it->second);

            const auto& settings = it->second;
            try {
                loadIdealLapRows(settings, hud.m_enabledRows);  // Named keys instead of bitmask
            } catch (const std::exception& e) {
                DEBUG_WARN_F("IdealLapHud: Failed to parse settings: %s", e.what());
            }
            hud.setDataDirty();
        }
    }

    // Apply TelemetryHud
    {
        auto it = cache.find("TelemetryHud");
        if (it != cache.end()) {
            auto& hud = hudManager.getTelemetryHud();
            applyBaseHudSettings(hud, it->second);

            const auto& settings = it->second;
            try {
                loadTelemetryElements(settings, hud.m_enabledElements);  // Named keys instead of bitmask
                if (settings.count("displayMode")) hud.m_displayMode = stringToDisplayMode(settings.at("displayMode"));
            } catch (const std::exception& e) {
                DEBUG_WARN_F("TelemetryHud: Failed to parse settings: %s", e.what());
            }
            hud.setDataDirty();
        }
    }

    // Apply PerformanceHud
    {
        auto it = cache.find("PerformanceHud");
        if (it != cache.end()) {
            auto& hud = hudManager.getPerformanceHud();
            applyBaseHudSettings(hud, it->second);

            const auto& settings = it->second;
            try {
                loadPerformanceElements(settings, hud.m_enabledElements);  // Named keys instead of bitmask
                if (settings.count("displayMode")) hud.m_displayMode = stringToDisplayMode(settings.at("displayMode"));
            } catch (const std::exception& e) {
                DEBUG_WARN_F("PerformanceHud: Failed to parse settings: %s", e.what());
            }
            hud.setDataDirty();
        }
    }

    // Apply simple widgets
    applyToHud("LapWidget", hudManager.getLapWidget());
    applyToHud("PositionWidget", hudManager.getPositionWidget());
    applyToHud("TimeWidget", hudManager.getTimeWidget());
    // Apply ClockWidget with showUtc, utcOnTop (format24h moved to [General], legacy fallback here)
    {
        auto it = cache.find("ClockWidget");
        if (it != cache.end()) {
            auto& hud = hudManager.getClockWidget();
            applyBaseHudSettings(hud, it->second);

            const auto& settings = it->second;
            try {
                if (settings.count("showUtc")) {
                    hud.setShowUtc(std::stoi(settings.at("showUtc")) != 0);
                }
                if (settings.count("utcOnTop")) {
                    hud.setUtcOnTop(std::stoi(settings.at("utcOnTop")) != 0);
                }
            } catch (const std::exception& e) {
                DEBUG_WARN_F("ClockWidget: Failed to parse settings: %s", e.what());
            }
            hud.setDataDirty();
        }
    }
    // Apply SessionHud with enabledRows and showIcons
    // Also check for legacy "SessionWidget" key for backwards compatibility
    {
        auto it = cache.find("SessionHud");
        if (it == cache.end()) {
            it = cache.find("SessionWidget");  // Backwards compatibility
        }
        if (it != cache.end()) {
            auto& hud = hudManager.getSessionHud();
            applyBaseHudSettings(hud, it->second);

            const auto& settings = it->second;
            try {
                loadSessionRows(settings, hud.m_enabledRows);
                if (settings.count(Keys::Session::SHOW_ICONS)) {
                    hud.m_bShowIcons = (std::stoi(settings.at(Keys::Session::SHOW_ICONS)) != 0);
                }
            } catch (const std::exception& e) {
                DEBUG_WARN_F("SessionHud: Failed to parse settings: %s", e.what());
            }
            hud.setDataDirty();
        }
    }
    // Apply SpeedoWidget with needleColor, showOdometer, showTripmeter
    {
        auto it = cache.find("SpeedoWidget");
        if (it != cache.end()) {
            auto& hud = hudManager.getSpeedoWidget();
            applyBaseHudSettings(hud, it->second);

            const auto& settings = it->second;
            try {
                if (settings.count(IniOnly::Speedo::NEEDLE_COLOR.key)) {
                    hud.setNeedleColor(PluginUtils::parseColorHex(settings.at(IniOnly::Speedo::NEEDLE_COLOR.key)));
                }
                if (settings.count(IniOnly::Speedo::SHOW_ODOMETER.key)) {
                    hud.setShowOdometer(std::stoi(settings.at(IniOnly::Speedo::SHOW_ODOMETER.key)) != 0);
                }
                if (settings.count(IniOnly::Speedo::SHOW_TRIPMETER.key)) {
                    hud.setShowTripmeter(std::stoi(settings.at(IniOnly::Speedo::SHOW_TRIPMETER.key)) != 0);
                }
            } catch (const std::exception& e) {
                DEBUG_WARN_F("SpeedoWidget: Failed to parse settings: %s", e.what());
            }
            hud.setDataDirty();
        }
    }
    // Apply TachoWidget with needleColor
    {
        auto it = cache.find("TachoWidget");
        if (it != cache.end()) {
            auto& hud = hudManager.getTachoWidget();
            applyBaseHudSettings(hud, it->second);

            const auto& settings = it->second;
            try {
                if (settings.count(IniOnly::Tacho::NEEDLE_COLOR.key)) {
                    hud.setNeedleColor(PluginUtils::parseColorHex(settings.at(IniOnly::Tacho::NEEDLE_COLOR.key)));
                }
            } catch (const std::exception& e) {
                DEBUG_WARN_F("TachoWidget: Failed to parse settings: %s", e.what());
            }
            hud.setDataDirty();
        }
    }
    // Apply BarsWidget with enabledColumns, showLabels, and showMaxMarkers
    {
        auto it = cache.find("BarsWidget");
        if (it != cache.end()) {
            auto& hud = hudManager.getBarsWidget();
            applyBaseHudSettings(hud, it->second);

            const auto& settings = it->second;
            try {
                loadBarsColumns(settings, hud.m_enabledColumns);
                if (settings.count("showLabels")) {
                    hud.m_bShowLabels = std::stoi(settings.at("showLabels")) != 0;
                }
                if (settings.count("showMaxMarkers")) {
                    hud.m_bShowMaxMarkers = std::stoi(settings.at("showMaxMarkers")) != 0;
                }
                if (settings.count("maxMarkerLingerFrames")) {
                    hud.m_maxMarkerLingerFrames = std::stoi(settings.at("maxMarkerLingerFrames"));
                }
            } catch (const std::exception& e) {
                DEBUG_WARN_F("BarsWidget: Failed to parse settings: %s", e.what());
            }
            hud.setDataDirty();
        }
    }
    applyToHud("VersionWidget", hudManager.getVersionWidget());
    // Apply NoticesHud with enabledNotices and PB duration
    {
        auto it = cache.find("NoticesHud");
        if (it != cache.end()) {
            auto& hud = hudManager.getNoticesHud();
            applyBaseHudSettings(hud, it->second);

            const auto& settings = it->second;
            try {
                loadNotices(settings, hud.m_enabledNotices);
                if (settings.count(IniOnly::Notices::PB_DURATION.key)) {
                    int duration = std::stoi(settings.at(IniOnly::Notices::PB_DURATION.key));
                    if (duration >= NoticesHud::MIN_NOTICE_DURATION_MS && duration <= NoticesHud::MAX_NOTICE_DURATION_MS) {
                        hud.m_noticeDurationMs = duration;
                    }
                }
            } catch (const std::exception& e) {
                DEBUG_WARN_F("NoticesHud: Failed to parse settings: %s", e.what());
            }
            hud.setDataDirty();
        }
    }
    applyToHud("SettingsButtonWidget", hudManager.getSettingsButtonWidget());
    applyToHud("PointerWidget", hudManager.getPointerWidget());
    // BenchmarkWidget (developer mode only - may not exist)
    if (hudManager.getBenchmarkWidget()) {
        applyToHud("BenchmarkWidget", *hudManager.getBenchmarkWidget());
    }
    // Apply RumbleHud with showMaxMarkers
    {
        auto it = cache.find("RumbleHud");
        if (it != cache.end()) {
            auto& hud = hudManager.getRumbleHud();
            applyBaseHudSettings(hud, it->second);

            const auto& settings = it->second;
            try {
                if (settings.count("showMaxMarkers")) {
                    hud.m_bShowMaxMarkers = std::stoi(settings.at("showMaxMarkers")) != 0;
                }
                if (settings.count("maxMarkerLingerFrames")) {
                    hud.m_maxMarkerLingerFrames = std::stoi(settings.at("maxMarkerLingerFrames"));
                }
            } catch (const std::exception& e) {
                DEBUG_WARN_F("RumbleHud: Failed to parse settings: %s", e.what());
            }
            hud.setDataDirty();
        }
    }

    // Note: HelmetOverlayHud is global — loaded from [HelmetOverlay] section in loadSettings().

    // Apply SpeedWidget with enabledRows - speedUnit is now global (in Preferences section)
    {
        auto it = cache.find("SpeedWidget");
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

    // Apply GearWidget with INI-only settings
    {
        auto it = cache.find("GearWidget");
        if (it != cache.end()) {
            auto& hud = hudManager.getGearWidget();
            applyBaseHudSettings(hud, it->second);

            const auto& settings = it->second;
            try {
                if (settings.count(IniOnly::Gear::SHOW_SHIFT_COLOR.key)) {
                    hud.m_bShowShiftColor = std::stoi(settings.at(IniOnly::Gear::SHOW_SHIFT_COLOR.key)) != 0;
                }
                if (settings.count(IniOnly::Gear::SHOW_LIMITER_CIRCLE.key)) {
                    hud.m_bShowLimiterCircle = std::stoi(settings.at(IniOnly::Gear::SHOW_LIMITER_CIRCLE.key)) != 0;
                }
            } catch (const std::exception& e) {
                DEBUG_WARN_F("GearWidget: Failed to parse settings: %s", e.what());
            }
            hud.setDataDirty();
        }
    }

    // Apply FuelWidget with enabledRows - fuelUnit is now global (in Preferences section)
    {
        auto it = cache.find("FuelWidget");
        if (it != cache.end()) {
            auto& hud = hudManager.getFuelWidget();
            applyBaseHudSettings(hud, it->second);

            const auto& settings = it->second;
            try {
                loadFuelRows(settings, hud.m_enabledRows);  // Named keys instead of bitmask
            } catch (const std::exception& e) {
                DEBUG_WARN_F("FuelWidget: Failed to parse settings: %s", e.what());
            }
            hud.setDataDirty();
        }
    }

    // Apply GamepadWidget (base settings only - layouts are loaded separately)
    {
        auto it = cache.find("GamepadWidget");
        if (it != cache.end()) {
            auto& hud = hudManager.getGamepadWidget();
            applyBaseHudSettings(hud, it->second);
            hud.setDataDirty();
        }
    }

    // Apply LeanWidget with enabledRows, showMaxMarkers, and arcFillColor
    {
        auto it = cache.find("LeanWidget");
        if (it != cache.end()) {
            auto& hud = hudManager.getLeanWidget();
            applyBaseHudSettings(hud, it->second);

            const auto& settings = it->second;
            try {
                loadLeanRows(settings, hud.m_enabledRows);  // Named keys instead of bitmask
                if (settings.count("showMaxMarkers")) {
                    hud.m_bShowMaxMarkers = std::stoi(settings.at("showMaxMarkers")) != 0;
                }
                if (settings.count("maxMarkerLingerFrames")) {
                    hud.m_maxMarkerLingerFrames = std::stoi(settings.at("maxMarkerLingerFrames"));
                }
                if (settings.count(IniOnly::Lean::ARC_FILL_COLOR.key)) {
                    hud.setArcFillColor(PluginUtils::parseColorHex(settings.at(IniOnly::Lean::ARC_FILL_COLOR.key)));
                }
            } catch (const std::exception& e) {
                DEBUG_WARN_F("LeanWidget: Failed to parse settings: %s", e.what());
            }
            hud.setDataDirty();
        }
    }

    // Apply GForceWidget (INI-only settings)
    {
        auto it = cache.find("GForceWidget");
        if (it != cache.end()) {
            auto& hud = hudManager.getGForceWidget();
            applyBaseHudSettings(hud, it->second);

            const auto& settings = it->second;
            try {
                if (settings.count(IniOnly::GForce::MAX_SCALE.key)) {
                    float v = std::stof(settings.at(IniOnly::GForce::MAX_SCALE.key));
                    if (v > 0.1f) hud.m_maxScale = v;
                }
                if (settings.count(IniOnly::GForce::SHOW_MAX_TEXT.key)) {
                    hud.m_bShowMaxText = std::stoi(settings.at(IniOnly::GForce::SHOW_MAX_TEXT.key)) != 0;
                }
                if (settings.count(IniOnly::GForce::SHOW_MAX_MARKER.key)) {
                    hud.m_bShowMaxMarker = std::stoi(settings.at(IniOnly::GForce::SHOW_MAX_MARKER.key)) != 0;
                }
                if (settings.count(IniOnly::GForce::MAX_MARKER_LINGER_FRAMES.key)) {
                    hud.m_maxMarkerLingerFrames = std::stoi(settings.at(IniOnly::GForce::MAX_MARKER_LINGER_FRAMES.key));
                }
            } catch (const std::exception& e) {
                DEBUG_WARN_F("GForceWidget: Failed to parse settings: %s", e.what());
            }
            hud.setDataDirty();
        }
    }

    // Apply CompassWidget (INI-only settings)
    {
        auto it = cache.find("CompassWidget");
        if (it != cache.end()) {
            auto& hud = hudManager.getCompassWidget();
            applyBaseHudSettings(hud, it->second);

            const auto& settings = it->second;
            try {
                if (settings.count(IniOnly::Compass::STYLE.key)) {
                    hud.m_style = stringToCompassStyle(settings.at(IniOnly::Compass::STYLE.key), hud.m_style);
                }
            } catch (const std::exception& e) {
                DEBUG_WARN_F("CompassWidget: Failed to parse settings: %s", e.what());
            }
            hud.setDataDirty();
        }
    }

#if GAME_HAS_TYRE_TEMP
    // Apply TyreTempWidget with temperature thresholds and row toggles
    {
        auto it = cache.find("TyreTempWidget");
        if (it != cache.end()) {
            auto& hud = hudManager.getTyreTempWidget();
            applyBaseHudSettings(hud, it->second);

            const auto& settings = it->second;
            try {
                if (settings.count("coldThreshold")) {
                    hud.setColdThreshold(std::stof(settings.at("coldThreshold")));
                }
                if (settings.count("hotThreshold")) {
                    hud.setHotThreshold(std::stof(settings.at("hotThreshold")));
                }
                loadTyreTempRows(settings, hud.m_enabledRows);  // Named keys for row toggles
                if (settings.count("showLabels")) {
                    hud.m_bShowLabels = std::stoi(settings.at("showLabels")) != 0;
                }
            } catch (const std::exception& e) {
                DEBUG_WARN_F("TyreTempWidget: Failed to parse settings: %s", e.what());
            }
            hud.setDataDirty();
        }
    }
#endif

#if GAME_HAS_ECU
    // Apply EcuWidget with per-chip toggles and label toggle
    {
        auto it = cache.find("EcuWidget");
        if (it != cache.end()) {
            auto& hud = hudManager.getEcuWidget();
            applyBaseHudSettings(hud, it->second);

            const auto& settings = it->second;
            try {
                loadEcuRows(settings, hud.m_enabledRows);  // Named keys for chip toggles
                if (settings.count("showLabels")) {
                    hud.m_bShowLabels = std::stoi(settings.at("showLabels")) != 0;
                }
            } catch (const std::exception& e) {
                DEBUG_WARN_F("EcuWidget: Failed to parse settings: %s", e.what());
            }
            hud.setDataDirty();
        }
    }
#endif

    // Apply TimingHud with display mode, column toggles, displayDuration, and primary/secondary gaps
    {
        auto it = cache.find("TimingHud");
        if (it != cache.end()) {
            auto& hud = hudManager.getTimingHud();
            applyBaseHudSettings(hud, it->second);

            const auto& settings = it->second;
            try {
                // New format: displayMode + column toggles
                if (settings.count("displayMode")) {
                    hud.m_displayMode = stringToColumnMode(settings.at("displayMode"));
                }
                if (settings.count("labelEnabled")) {
                    hud.m_columnEnabled[TimingHud::COL_LABEL] = settings.at("labelEnabled") == "1";
                }
                if (settings.count("timeEnabled")) {
                    hud.m_columnEnabled[TimingHud::COL_TIME] = settings.at("timeEnabled") == "1";
                }
                if (settings.count("gapEnabled")) {
                    hud.m_columnEnabled[TimingHud::COL_GAP] = settings.at("gapEnabled") == "1";
                }
                if (settings.count("showReference")) {
                    hud.m_showReference = settings.at("showReference") == "1";
                }
                if (settings.count("layoutVertical")) {
                    hud.m_layoutVertical = settings.at("layoutVertical") == "1";
                }
                // Legacy format migration: convert old per-column modes to new format
                else if (settings.count("labelMode")) {
                    // Old format had per-column modes, migrate to new display mode + toggles
                    ColumnMode labelMode = stringToColumnMode(settings.at("labelMode"));
                    ColumnMode timeMode = settings.count("timeMode") ? stringToColumnMode(settings.at("timeMode")) : ColumnMode::ALWAYS;
                    ColumnMode gapMode = settings.count("gapMode") ? stringToColumnMode(settings.at("gapMode")) : ColumnMode::SPLITS;
                    ColumnMode refMode = settings.count("referenceMode") ? stringToColumnMode(settings.at("referenceMode")) : ColumnMode::OFF;

                    // Derive display mode from the most restrictive column mode (prefer SPLITS if any uses it)
                    if (labelMode == ColumnMode::SPLITS || timeMode == ColumnMode::SPLITS || gapMode == ColumnMode::SPLITS) {
                        hud.m_displayMode = ColumnMode::SPLITS;
                    } else if (labelMode == ColumnMode::ALWAYS || timeMode == ColumnMode::ALWAYS || gapMode == ColumnMode::ALWAYS) {
                        hud.m_displayMode = ColumnMode::ALWAYS;
                    } else {
                        hud.m_displayMode = ColumnMode::OFF;
                    }

                    // Columns are enabled if their mode wasn't OFF
                    hud.m_columnEnabled[TimingHud::COL_LABEL] = (labelMode != ColumnMode::OFF);
                    hud.m_columnEnabled[TimingHud::COL_TIME] = (timeMode != ColumnMode::OFF);
                    hud.m_columnEnabled[TimingHud::COL_GAP] = (gapMode != ColumnMode::OFF);
                    hud.m_showReference = (refMode != ColumnMode::OFF);
                }
                if (settings.count("displayDuration")) {
                    int duration = std::stoi(settings.at("displayDuration"));
                    if (duration >= TimingHud::MIN_DURATION_MS && duration <= TimingHud::MAX_DURATION_MS) {
                        hud.m_displayDurationMs = duration;
                    }
                }
                // Load primary gap type
                if (settings.count("primaryGap")) {
                    hud.m_primaryGapType = stringToGapType(settings.at("primaryGap"));
                }
                // Load secondary gap types
                loadTimingSecondaryGaps(settings, hud.m_secondaryGapTypes);
            } catch (const std::exception& e) {
                DEBUG_WARN_F("TimingHud: Failed to parse settings: %s", e.what());
            }
            hud.setDataDirty();
        }
    }

    // Apply GapBarHud with freeze, marker mode, icon, gap text, and range settings
    {
        auto it = cache.find("GapBarHud");
        if (it != cache.end()) {
            auto& hud = hudManager.getGapBarHud();
            applyBaseHudSettings(hud, it->second);
            const auto& settings = it->second;
            try {
                if (settings.count("freezeDuration")) {
                    int freeze = std::stoi(settings.at("freezeDuration"));
                    if (freeze >= GapBarHud::MIN_FREEZE_MS && freeze <= GapBarHud::MAX_FREEZE_MS) {
                        hud.m_freezeDurationMs = freeze;
                    }
                }
                // New marker mode setting (replaces old showMarkers boolean)
                if (settings.count("markerMode")) {
                    int mode = std::stoi(settings.at("markerMode"));
                    if (mode >= 0 && mode <= 2) {
                        hud.m_markerMode = static_cast<GapBarHud::MarkerMode>(mode);
                    }
                }
                // Legacy compatibility: convert old showMarkers boolean to markerMode
                else if (settings.count("showMarkers") || settings.count("showMarker")) {
                    bool showMarkers = settings.count("showMarkers") ?
                        (settings.at("showMarkers") == "1") :
                        (settings.at("showMarker") == "1");
                    // Old behavior: markers on = ghost mode, off = still ghost mode (markers always shown now)
                    hud.m_markerMode = GapBarHud::MarkerMode::GHOST;
                }
                // Rider icon (name-based; 0/"Off" = use default icon)
                if (settings.count("riderIcon")) {
                    hud.m_riderIconIndex = filenameToShapeIndex(settings.at("riderIcon"), 0);
                }
                // Show gap text toggle
                if (settings.count("showGapText")) {
                    hud.m_showGapText = (settings.at("showGapText") == "1");
                }
                // Show gap bar toggle (green/red visualization)
                if (settings.count("showGapBar")) {
                    hud.m_showGapBar = (settings.at("showGapBar") == "1");
                }
                // Gap range
                if (settings.count("gapRange")) {
                    int range = std::stoi(settings.at("gapRange"));
                    if (range >= GapBarHud::MIN_RANGE_MS && range <= GapBarHud::MAX_RANGE_MS) {
                        hud.m_gapRangeMs = range;
                    }
                } else if (settings.count("legacyRange")) {
                    int range = std::stoi(settings.at("legacyRange"));
                    if (range >= GapBarHud::MIN_RANGE_MS && range <= GapBarHud::MAX_RANGE_MS) {
                        hud.m_gapRangeMs = range;
                    }
                }
                if (settings.count("barWidth")) {
                    int width = std::stoi(settings.at("barWidth"));
                    if (width >= GapBarHud::MIN_WIDTH_PERCENT && width <= GapBarHud::MAX_WIDTH_PERCENT) {
                        hud.m_barWidthPercent = width;
                    }
                }
                // Marker scale
                if (settings.count("markerScale")) {
                    float scale = std::stof(settings.at("markerScale"));
                    if (scale >= GapBarHud::MIN_MARKER_SCALE && scale <= GapBarHud::MAX_MARKER_SCALE) {
                        hud.m_fMarkerScale = scale;
                    }
                }
                // Label mode
                if (settings.count("labelMode")) {
                    int mode = std::stoi(settings.at("labelMode"));
                    if (mode >= 0 && mode <= 3) {
                        hud.m_labelMode = static_cast<GapBarHud::LabelMode>(mode);
                    }
                }
                // Color mode (string format, with backwards compatibility for integer format)
                if (settings.count("colorMode")) {
                    hud.m_riderColorMode = stringToGapBarRiderColorMode(settings.at("colorMode"));
                }
            } catch (const std::exception& e) {
                DEBUG_WARN_F("GapBarHud: Failed to parse settings: %s", e.what());
            }
            hud.setDataDirty();
        }
    }

    // Apply global HUD manager settings (per-profile)
    {
        auto it = cache.find("Global");
        if (it != cache.end()) {
            const auto& settings = it->second;
            try {
                if (settings.count("widgetsEnabled")) {
                    hudManager.setWidgetsEnabled(std::stoi(settings.at("widgetsEnabled")) != 0);
                }
            } catch (const std::exception& e) {
                DEBUG_WARN_F("Global: Failed to parse settings: %s", e.what());
            }
        }
    }

    // Note: ColorConfig is now global (not per-profile) - loaded once in loadSettings

    // Rebuild all dirty HUDs immediately so quads reflect new settings before render
    hudManager.rebuildAllIfDirty();

    DEBUG_INFO_F("Applied profile: %s", ProfileManager::getProfileName(profile));
}

bool SettingsManager::switchProfile(HudManager& hudManager, ProfileType newProfile) {
    ProfileManager& profileManager = ProfileManager::getInstance();
    ProfileType oldProfile = profileManager.getActiveProfile();

    if (newProfile == oldProfile) return false;
    if (newProfile >= ProfileType::COUNT) {
        DEBUG_WARN_F("switchProfile called with invalid profile index: %d", static_cast<int>(newProfile));
        return false;
    }

    // Capture current state to old profile
    captureToProfile(hudManager, oldProfile);

    // Switch active profile
    profileManager.setActiveProfile(newProfile);

    // Apply new profile settings
    applyProfile(hudManager, newProfile);

    // Save to disk
    if (!m_savePath.empty()) {
        saveSettings(hudManager, m_savePath.c_str());
    }

    return true;
}

void SettingsManager::applyToAllProfiles(HudManager& hudManager) {
    ProfileType activeProfile = ProfileManager::getInstance().getActiveProfile();

    // Capture current HUD state to active profile (ensure it's up to date)
    captureToProfile(hudManager, activeProfile);

    // Copy active profile's cache to all other profiles
    const ProfileCache& sourceCache = m_profileCache[static_cast<size_t>(activeProfile)];
    for (int i = 0; i < static_cast<int>(ProfileType::COUNT); ++i) {
        ProfileType targetProfile = static_cast<ProfileType>(i);
        if (targetProfile != activeProfile) {
            m_profileCache[static_cast<size_t>(i)] = sourceCache;
        }
    }

    // Save to disk
    if (!m_savePath.empty()) {
        saveSettings(hudManager, m_savePath.c_str());
    }

    DEBUG_INFO_F("Applied %s profile settings to all profiles", ProfileManager::getProfileName(activeProfile));
}

void SettingsManager::resetAllToFactoryDefaults(HudManager& hudManager) {
    if (!m_factoryDefaultsCaptured) {
        // Shouldn't happen post-load, but fall back to the legacy behavior rather
        // than wiping every profile to an empty cache.
        DEBUG_WARN("resetAllToFactoryDefaults: factory defaults not captured, falling back to applyToAllProfiles");
        applyToAllProfiles(hudManager);
        return;
    }

    // Seed every profile with the pristine factory snapshot (this build's constructor
    // defaults), not m_hudDefaults — the latter has the file's base-section edits folded in,
    // which would make a full factory reset preserve stale/old-version defaults. This covers
    // every INI-controllable per-HUD setting (including INI-only overrides) regardless of
    // whether an individual HUD's resetToDefaults() happens to reset it.
    for (auto& cache : m_profileCache) {
        cache = m_hudFactoryDefaults;
    }
    // Re-seed the sparse-save baseline to factory too, so the rewritten base [HudName]
    // sections reflect this build's defaults and every profile collapses to a minimal diff.
    // (This is the path that intentionally discards user base-section edits — a full factory
    // reset is exactly when that's wanted.)
    m_hudDefaults = m_hudFactoryDefaults;
    m_cacheInitialized = true;

    // Push the active profile's (now default) settings onto the live HUDs.
    applyActiveProfile(hudManager);

    // Persist. saveSettings re-captures the (now default) live state into the
    // active profile and rewrites the file from scratch, so stale per-profile
    // overrides are dropped (and, unlike before, stale base-section defaults are
    // replaced with this build's factory defaults).
    if (!m_savePath.empty()) {
        saveSettings(hudManager, m_savePath.c_str());
    }

    DEBUG_INFO("Reset all profiles to factory defaults");
}

void SettingsManager::resetHudsToFactoryDefaults(HudManager& hudManager, const std::vector<std::string>& hudNames, bool keepVisibility) {
    if (!m_factoryDefaultsCaptured || !m_cacheInitialized) {
        return;  // Nothing to restore from yet (shouldn't happen post-load).
    }

    ProfileType active = ProfileManager::getInstance().getActiveProfile();

    // Sync the active-profile cache with the live HUDs first. applyActiveProfile()
    // below re-applies the whole cache, so this ensures HUDs we're NOT resetting
    // keep their current (possibly unsaved) state instead of being reverted.
    captureToProfile(hudManager, active);

    ProfileCache& cache = m_profileCache[static_cast<size_t>(active)];
    for (const auto& name : hudNames) {
        // Source from the pristine factory snapshot, not m_hudDefaults (which has file
        // base-section edits folded in). A per-tab reset restores only the active profile,
        // so we deliberately leave m_hudDefaults untouched — other profiles still use it as
        // their save baseline; the reset values just get written as explicit profile diffs.
        auto defIt = m_hudFactoryDefaults.find(name);
        if (defIt == m_hudFactoryDefaults.end()) continue;  // not a per-profile HUD (e.g. HelmetOverlay)

        HudSettings& hudCache = cache[name];

        // For single-HUD tabs, a reset shouldn't hide the element the user is
        // positioning, so keep its current visibility (full "Reset all settings"
        // resets visibility instead). The Widgets tab opts out via keepVisibility=
        // false: its per-widget "Visible" toggles are tab config that Reset should
        // restore to factory defaults like everything else.
        std::string keepVisible;
        if (keepVisibility) {
            auto visIt = hudCache.find(Keys::Base::VISIBLE);
            if (visIt != hudCache.end()) keepVisible = visIt->second;
        }

        hudCache = defIt->second;  // pristine factory defaults (no base-section edits)
        if (!keepVisible.empty()) hudCache[Keys::Base::VISIBLE] = keepVisible;
    }

    // Push the updated cache onto the live HUDs. applyBaseHudSettings is authoritative,
    // so per-profile color/font overrides absent from the defaults are cleared too.
    applyActiveProfile(hudManager);
}

void SettingsManager::resetActiveProfileToFactoryDefaults(HudManager& hudManager) {
    if (!m_factoryDefaultsCaptured || !m_cacheInitialized) {
        return;  // Nothing to restore from yet (shouldn't happen post-load).
    }

    ProfileType active = ProfileManager::getInstance().getActiveProfile();

    // Replace the whole active-profile cache with the pristine factory snapshot (this build's
    // defaults), not m_hudDefaults (file base-section edits folded in). This covers every
    // per-profile HUD/widget (including INI-only members and color/font overrides) in one shot.
    // Only the active profile is reset, so m_hudDefaults (the shared save baseline for the
    // other profiles) is left untouched. Global sections (colors/fonts/hotkeys) and other
    // profiles aren't in this cache either, so they're left untouched.
    m_profileCache[static_cast<size_t>(active)] = m_hudFactoryDefaults;
    applyActiveProfile(hudManager);
}

void SettingsManager::replayGlobalDefaults(HudManager& hudManager, const std::vector<std::string>* sectionFilter) {
    if (m_globalDefaultsIni.empty()) {
        // Snapshot not captured yet (shouldn't happen post-load); nothing to restore.
        return;
    }

    // Developer mode is an INI-only power-user flag; no reset path touches it even though it
    // lives in [Advanced]. Preserve it across the snapshot replay.
    const bool developerMode = m_developerMode;

    // Replay the captured default INI through the same applier loadSettings() uses, so the
    // reset path covers every global setting that save/load cover, automatically.
    std::istringstream stream(m_globalDefaultsIni);
    std::string line;
    std::string section;
    while (std::getline(stream, line)) {
        size_t start = line.find_first_not_of(" \t\r\n");
        if (start == std::string::npos) continue;
        size_t end = line.find_last_not_of(" \t\r\n");
        line = line.substr(start, end - start + 1);
        if (line.empty() || line[0] == '#') continue;

        if (line.front() == '[' && line.back() == ']') {
            section = line.substr(1, line.length() - 2);
            continue;
        }

        // Skip lines outside the requested sections (null filter = apply everything).
        if (sectionFilter &&
            std::find(sectionFilter->begin(), sectionFilter->end(), section) == sectionFilter->end()) {
            continue;
        }

        size_t equals = line.find('=');
        if (equals == std::string::npos) continue;
        std::string key = line.substr(0, equals);
        std::string value = line.substr(equals + 1);

        // Strip inline comments (everything after ';'), matching loadSettings().
        size_t commentPos = value.find(';');
        if (commentPos != std::string::npos) {
            value = value.substr(0, commentPos);
            size_t valueEnd = value.find_last_not_of(" \t");
            value = (valueEnd != std::string::npos) ? value.substr(0, valueEnd + 1) : std::string();
        }

        applyGlobalLine(section, key, value, hudManager);
    }

    m_developerMode = developerMode;
}

void SettingsManager::resetGlobalsToFactoryDefaults(HudManager& hudManager) {
    replayGlobalDefaults(hudManager, nullptr);
    DEBUG_INFO("Reset global settings to factory defaults");
}

void SettingsManager::resetGlobalSectionsToFactoryDefaults(HudManager& hudManager, const std::vector<std::string>& sections) {
    replayGlobalDefaults(hudManager, &sections);
    DEBUG_INFO_F("Reset %zu global section(s) to factory defaults", sections.size());
}

void SettingsManager::copyToProfile(HudManager& hudManager, ProfileType targetProfile) {
    ProfileType activeProfile = ProfileManager::getInstance().getActiveProfile();

    // Don't copy to self
    if (targetProfile == activeProfile) {
        DEBUG_WARN("Cannot copy profile to itself");
        return;
    }

    // Capture current HUD state to active profile (ensure it's up to date)
    captureToProfile(hudManager, activeProfile);

    // Copy to target profile
    m_profileCache[static_cast<size_t>(targetProfile)] = m_profileCache[static_cast<size_t>(activeProfile)];

    // Save to disk
    if (!m_savePath.empty()) {
        saveSettings(hudManager, m_savePath.c_str());
    }

    DEBUG_INFO_F("Copied %s profile settings to %s",
        ProfileManager::getProfileName(activeProfile),
        ProfileManager::getProfileName(targetProfile));
}

void SettingsManager::writeGlobalSettings(std::ostream& out, const HudManager& hudManager) const {
    // Write General section (global preferences)
    out << "[General]\n";
    out << "autoSave=" << (UiConfig::getInstance().getAutoSave() ? 1 : 0) << "\n";
    out << "controller=" << XInputReader::getInstance().getRumbleConfig().controllerIndex << "\n";
    out << "pbScope=" << pbScopeToString(UiConfig::getInstance().getPBScope()) << "\n";
#if GAME_HAS_RECORDS_PROVIDER
    out << "recordsAutoFetch=" << (hudManager.getRecordsHud().m_bAutoFetch ? 1 : 0) << "\n";
    out << "recordsProvider=" << dataProviderToString(hudManager.getRecordsHud().m_provider) << "\n";
#endif
#if GAME_HAS_DISCORD
    out << "discordRichPresence=" << (DiscordManager::getInstance().isEnabled() ? 1 : 0) << "\n";
#endif
#if GAME_HAS_STEAM_FRIENDS
    out << "steamFriends=" << (SteamFriendsManager::getInstance().isEnabled() ? 1 : 0) << "\n";
#endif
    out << "filterDnsRiders=" << (PluginData::getInstance().isFilterDnsRiders() ? 1 : 0) << "\n";
#if GAME_HAS_HTTP_SERVER
    out << "webServer=" << (HttpServer::getInstance().isEnabled() ? 1 : 0) << " ; Web overlay server (port and throttle in [Advanced])\n";
#endif
    out << "\n";

    // Write Updates section (auto-update settings, owned solely by the Updates tab).
    // Consolidated here from [General] (updateMode/dismissedVersion) and [Advanced]
    // (updateChannel/updateDebugMode) so the tab maps 1:1 to one INI section and resets
    // via section replay. updateChannel is written before dismissedVersion because
    // setChannel() clears the dismissed version when the channel changes — applying it
    // first keeps a same-channel dismissal intact on load.
    {
        const char* channelStr = (UpdateChecker::getInstance().getChannel() == UpdateChecker::UpdateChannel::PRERELEASE) ? "prerelease" : "stable";
        const char* updateModeStr = "off";
        switch (UpdateChecker::getInstance().getMode()) {
            case UpdateChecker::UpdateMode::OFF: updateModeStr = "off"; break;
            case UpdateChecker::UpdateMode::NOTIFY: updateModeStr = "notify"; break;
        }
        out << "[Updates]\n";
        out << "updateChannel=" << channelStr << "\n";
        out << "updateMode=" << updateModeStr << "\n";
        out << "updateDebugMode=" << (UpdateChecker::getInstance().isDebugMode() ? 1 : 0) << "\n";
        // Dismissed version (suppresses re-notifying about an update the user dismissed).
        // Written unconditionally — even when empty — so the factory-defaults snapshot
        // (captured before load, when it is empty) carries the key, and a full reset
        // restores it to empty via the normal [Updates] replay. A conditional write would
        // omit it from the snapshot, leaving a stale dismissal stuck across "Reset all
        // settings". An empty value loads as a no-op (setDismissedVersion("")).
        out << "dismissedVersion=" << UpdateChecker::getInstance().getDismissedVersion() << "\n";
        out << "\n";
    }

    // Write Advanced section (power-user settings)
    out << "[Advanced]\n";
    out << IniOnly::Advanced::DEVELOPER_MODE.key << "=" << (m_developerMode ? 1 : 0) << " ; " << IniOnly::Advanced::DEVELOPER_MODE.description << "\n";
    // Note: updateChannel/updateMode/updateDebugMode/dismissedVersion moved to [Updates]
    // Note: mapPixelSpacing moved to [MapHud]
    // Note: speedoNeedleColor, speedoShowOdometer, speedoShowTripmeter moved to [SpeedoWidget]
    // Note: tachoNeedleColor moved to [TachoWidget]
    // Note: leanArcFillColor moved to [LeanWidget]
    // Note: recordsShowFooter moved to [RecordsHud]
    // Note: standingsTopPositions, standingsUseAccentHighlight moved to [StandingsHud]
    out << IniOnly::Advanced::DROP_SHADOW_OFFSET_X.key << "=" << UiConfig::getInstance().getDropShadowOffsetX() << " ; " << IniOnly::Advanced::DROP_SHADOW_OFFSET_X.description << "\n";
    out << IniOnly::Advanced::DROP_SHADOW_OFFSET_Y.key << "=" << UiConfig::getInstance().getDropShadowOffsetY() << " ; " << IniOnly::Advanced::DROP_SHADOW_OFFSET_Y.description << "\n";
    out << IniOnly::Advanced::DROP_SHADOW_COLOR.key << "=" << PluginUtils::formatColorHex(UiConfig::getInstance().getDropShadowColor()) << " ; " << IniOnly::Advanced::DROP_SHADOW_COLOR.description << "\n";
    out << IniOnly::Advanced::HOLD_REPEAT_FAST_MS.key << "=" << UiConfig::getInstance().getHoldRepeatFastMs() << " ; " << IniOnly::Advanced::HOLD_REPEAT_FAST_MS.description << "\n";
    out << IniOnly::Advanced::CURSOR_ACTIVATION_THRESHOLD.key << "=" << UiConfig::getInstance().getCursorActivationThreshold() << " ; " << IniOnly::Advanced::CURSOR_ACTIVATION_THRESHOLD.description << "\n";
    out << IniOnly::Advanced::SEGMENT_SNAP_TO_SPLITS.key << "=" << (UiConfig::getInstance().getSnapSegmentsToSplits() ? 1 : 0) << " ; " << IniOnly::Advanced::SEGMENT_SNAP_TO_SPLITS.description << "\n";
    out << IniOnly::Advanced::SEGMENT_SNAP_THRESHOLD.key << "=" << UiConfig::getInstance().getSegmentSnapThreshold() << " ; " << IniOnly::Advanced::SEGMENT_SNAP_THRESHOLD.description << "\n";
    out << IniOnly::Advanced::HAZARD_STATIONARY_TOLERANCE.key << "=" << PluginData::getInstance().getHazardStationaryTolerance() << " ; " << IniOnly::Advanced::HAZARD_STATIONARY_TOLERANCE.description << "\n";
    out << IniOnly::Advanced::HAZARD_STATIONARY_DURATION_MS.key << "=" << PluginData::getInstance().getHazardStationaryDurationMs() << " ; " << IniOnly::Advanced::HAZARD_STATIONARY_DURATION_MS.description << "\n";
    out << IniOnly::Advanced::HAZARD_WRONG_WAY_DURATION_MS.key << "=" << PluginData::getInstance().getHazardWrongWayDurationMs() << " ; " << IniOnly::Advanced::HAZARD_WRONG_WAY_DURATION_MS.description << "\n";
    out << IniOnly::Advanced::HAZARD_AWARENESS_DISTANCE.key << "=" << PluginData::getInstance().getHazardAwarenessDistance() << " ; " << IniOnly::Advanced::HAZARD_AWARENESS_DISTANCE.description << "\n";
    out << IniOnly::Advanced::HAZARD_COOLDOWN_MS.key << "=" << PluginData::getInstance().getHazardCooldownMs() << " ; " << IniOnly::Advanced::HAZARD_COOLDOWN_MS.description << "\n";
    out << IniOnly::Advanced::HAZARD_GRACE_PERIOD_MS.key << "=" << PluginData::getInstance().getHazardGracePeriodMs() << " ; " << IniOnly::Advanced::HAZARD_GRACE_PERIOD_MS.description << "\n";
    out << IniOnly::Advanced::BLUE_FLAG_AWARENESS_DISTANCE.key << "=" << PluginData::getInstance().getBlueFlagAwarenessDistance() << " ; " << IniOnly::Advanced::BLUE_FLAG_AWARENESS_DISTANCE.description << "\n";
    out << IniOnly::Advanced::GAP_NOTIFY_INTERVAL_MS.key << "=" << PluginData::getInstance().getGapNotifyIntervalMs() << " ; " << IniOnly::Advanced::GAP_NOTIFY_INTERVAL_MS.description << "\n";
#if GAME_HAS_HTTP_SERVER
    out << IniOnly::Advanced::WEB_SERVER_PORT.key << "=" << HttpServer::getInstance().getPort() << " ; " << IniOnly::Advanced::WEB_SERVER_PORT.description << "\n";
    out << IniOnly::Advanced::WEB_SERVER_THROTTLE_MS.key << "=" << HttpServer::getInstance().getThrottleMs() << " ; " << IniOnly::Advanced::WEB_SERVER_THROTTLE_MS.description << "\n";
    out << IniOnly::Advanced::WEB_SERVER_BIND_ADDRESS.key << "=" << HttpServer::getInstance().getBindAddress() << " ; " << IniOnly::Advanced::WEB_SERVER_BIND_ADDRESS.description << "\n";
#endif
    out << "\n";

    // Write Display section (units, clock format, and display toggles; shown first
    // on the Appearance tab)
    out << "[Display]\n";
    out << "speedUnit=" << speedUnitToString(hudManager.getSpeedWidget().m_speedUnit) << "\n";
    out << "fuelUnit=" << fuelUnitToString(hudManager.getFuelWidget().m_fuelUnit) << "\n";
    out << "tempUnit=" << tempUnitToString(UiConfig::getInstance().getTemperatureUnit()) << "\n";
    out << "format24h=" << (hudManager.getClockWidget().getFormat24h() ? 1 : 0) << "\n";
    out << "shortTimeFormat=" << (PluginData::getInstance().isShortTimeFormat() ? 1 : 0) << "\n";
    out << "dropShadow=" << (UiConfig::getInstance().getDropShadow() ? 1 : 0) << "\n";
    out << "titleIcons=" << (UiConfig::getInstance().getTitleIcons() ? 1 : 0) << "\n";
    out << "gridSnapping=" << (UiConfig::getInstance().getGridSnapping() ? 1 : 0) << "\n";
    out << "screenClamping=" << (UiConfig::getInstance().getScreenClamping() ? 1 : 0) << "\n\n";

    // Write Colors section
    const ColorConfig& colorConfig = ColorConfig::getInstance();
    out << "[Colors]\n";
    out << "primary=" << PluginUtils::formatColorHex(colorConfig.getPrimary()) << "\n";
    out << "secondary=" << PluginUtils::formatColorHex(colorConfig.getSecondary()) << "\n";
    out << "tertiary=" << PluginUtils::formatColorHex(colorConfig.getTertiary()) << "\n";
    out << "muted=" << PluginUtils::formatColorHex(colorConfig.getMuted()) << "\n";
    out << "background=" << PluginUtils::formatColorHex(colorConfig.getBackground()) << "\n";
    out << "positive=" << PluginUtils::formatColorHex(colorConfig.getPositive()) << "\n";
    out << "warning=" << PluginUtils::formatColorHex(colorConfig.getWarning()) << "\n";
    out << "neutral=" << PluginUtils::formatColorHex(colorConfig.getNeutral()) << "\n";
    out << "negative=" << PluginUtils::formatColorHex(colorConfig.getNegative()) << "\n";
    out << "accent=" << PluginUtils::formatColorHex(colorConfig.getAccent()) << "\n\n";

    // Write Fonts section
    const FontConfig& fontConfig = FontConfig::getInstance();
    out << "[Fonts]\n";
    out << "title=" << fontConfig.getFontName(FontCategory::TITLE) << "\n";
    out << "normal=" << fontConfig.getFontName(FontCategory::NORMAL) << "\n";
    out << "strong=" << fontConfig.getFontName(FontCategory::STRONG) << "\n";
    out << "digits=" << fontConfig.getFontName(FontCategory::DIGITS) << "\n";
    out << "marker=" << fontConfig.getFontName(FontCategory::MARKER) << "\n";
    out << "small=" << fontConfig.getFontName(FontCategory::SMALL) << "\n\n";

    // Write Rumble section (effect configuration)
    // Always save global config to INI (per-bike effects go to JSON)
    const RumbleConfig& rumbleConfig = XInputReader::getInstance().getGlobalRumbleConfig();
    out << "[Rumble]\n";
    out << "enabled=" << (rumbleConfig.enabled ? 1 : 0) << "\n";
    out << "additive_blend=" << (rumbleConfig.additiveBlend ? 1 : 0) << "\n";
    out << "rumble_when_crashed=" << (rumbleConfig.rumbleWhenCrashed ? 1 : 0) << "\n";
    out << "use_per_bike_effects=" << (rumbleConfig.usePerBikeEffects ? 1 : 0) << "\n";
    out << "send_interval_ms=" << XInputReader::getInstance().getRumbleSendIntervalMs()
        << " ; Min ms between rumble updates; raise to reduce Bluetooth traffic (4-200, default 10)\n";
    // Suspension effect (with optional front/rear split)
    out << "susp_min_input=" << rumbleConfig.suspensionEffect.minInput << "\n";
    out << "susp_max_input=" << rumbleConfig.suspensionEffect.maxInput << "\n";
    out << "susp_light_strength=" << rumbleConfig.suspensionEffect.lightStrength << "\n";
    out << "susp_heavy_strength=" << rumbleConfig.suspensionEffect.heavyStrength << "\n";
    out << "susp_split=" << (rumbleConfig.suspensionSplit ? 1 : 0) << "\n";
    out << "susp_split_init=" << (rumbleConfig.suspensionSplitInitialized ? 1 : 0) << "\n";
    out << "susp_front_min_input=" << rumbleConfig.suspensionEffectFront.minInput << "\n";
    out << "susp_front_max_input=" << rumbleConfig.suspensionEffectFront.maxInput << "\n";
    out << "susp_front_light_strength=" << rumbleConfig.suspensionEffectFront.lightStrength << "\n";
    out << "susp_front_heavy_strength=" << rumbleConfig.suspensionEffectFront.heavyStrength << "\n";
    out << "susp_rear_min_input=" << rumbleConfig.suspensionEffectRear.minInput << "\n";
    out << "susp_rear_max_input=" << rumbleConfig.suspensionEffectRear.maxInput << "\n";
    out << "susp_rear_light_strength=" << rumbleConfig.suspensionEffectRear.lightStrength << "\n";
    out << "susp_rear_heavy_strength=" << rumbleConfig.suspensionEffectRear.heavyStrength << "\n";
    // Wheelspin effect
    out << "wheel_min_input=" << rumbleConfig.wheelspinEffect.minInput << "\n";
    out << "wheel_max_input=" << rumbleConfig.wheelspinEffect.maxInput << "\n";
    out << "wheel_light_strength=" << rumbleConfig.wheelspinEffect.lightStrength << "\n";
    out << "wheel_heavy_strength=" << rumbleConfig.wheelspinEffect.heavyStrength << "\n";
    // Brake lockup effect (with optional front/rear split)
    out << "lockup_min_input=" << rumbleConfig.brakeLockupEffect.minInput << "\n";
    out << "lockup_max_input=" << rumbleConfig.brakeLockupEffect.maxInput << "\n";
    out << "lockup_light_strength=" << rumbleConfig.brakeLockupEffect.lightStrength << "\n";
    out << "lockup_heavy_strength=" << rumbleConfig.brakeLockupEffect.heavyStrength << "\n";
    out << "lockup_split=" << (rumbleConfig.brakeLockupSplit ? 1 : 0) << "\n";
    out << "lockup_split_init=" << (rumbleConfig.brakeLockupSplitInitialized ? 1 : 0) << "\n";
    out << "lockup_front_min_input=" << rumbleConfig.brakeLockupEffectFront.minInput << "\n";
    out << "lockup_front_max_input=" << rumbleConfig.brakeLockupEffectFront.maxInput << "\n";
    out << "lockup_front_light_strength=" << rumbleConfig.brakeLockupEffectFront.lightStrength << "\n";
    out << "lockup_front_heavy_strength=" << rumbleConfig.brakeLockupEffectFront.heavyStrength << "\n";
    out << "lockup_rear_min_input=" << rumbleConfig.brakeLockupEffectRear.minInput << "\n";
    out << "lockup_rear_max_input=" << rumbleConfig.brakeLockupEffectRear.maxInput << "\n";
    out << "lockup_rear_light_strength=" << rumbleConfig.brakeLockupEffectRear.lightStrength << "\n";
    out << "lockup_rear_heavy_strength=" << rumbleConfig.brakeLockupEffectRear.heavyStrength << "\n";
    // RPM effect
    out << "rpm_min_input=" << rumbleConfig.rpmEffect.minInput << "\n";
    out << "rpm_max_input=" << rumbleConfig.rpmEffect.maxInput << "\n";
    out << "rpm_light_strength=" << rumbleConfig.rpmEffect.lightStrength << "\n";
    out << "rpm_heavy_strength=" << rumbleConfig.rpmEffect.heavyStrength << "\n";
    // Slide effect
    out << "slide_min_input=" << rumbleConfig.slideEffect.minInput << "\n";
    out << "slide_max_input=" << rumbleConfig.slideEffect.maxInput << "\n";
    out << "slide_light_strength=" << rumbleConfig.slideEffect.lightStrength << "\n";
    out << "slide_heavy_strength=" << rumbleConfig.slideEffect.heavyStrength << "\n";
    // Surface effect
    out << "surface_min_input=" << rumbleConfig.surfaceEffect.minInput << "\n";
    out << "surface_max_input=" << rumbleConfig.surfaceEffect.maxInput << "\n";
    out << "surface_light_strength=" << rumbleConfig.surfaceEffect.lightStrength << "\n";
    out << "surface_heavy_strength=" << rumbleConfig.surfaceEffect.heavyStrength << "\n";
    // Steer effect
    out << "steer_min_input=" << rumbleConfig.steerEffect.minInput << "\n";
    out << "steer_max_input=" << rumbleConfig.steerEffect.maxInput << "\n";
    out << "steer_light_strength=" << rumbleConfig.steerEffect.lightStrength << "\n";
    out << "steer_heavy_strength=" << rumbleConfig.steerEffect.heavyStrength << "\n";
    // Wheelie effect
    out << "wheelie_min_input=" << rumbleConfig.wheelieEffect.minInput << "\n";
    out << "wheelie_max_input=" << rumbleConfig.wheelieEffect.maxInput << "\n";
    out << "wheelie_light_strength=" << rumbleConfig.wheelieEffect.lightStrength << "\n";
    out << "wheelie_heavy_strength=" << rumbleConfig.wheelieEffect.heavyStrength << "\n";
    // Rev limiter effect (Min/Max are percent of the bike's limiter RPM)
    out << "revlim_min_input=" << rumbleConfig.revLimiterEffect.minInput << "\n";
    out << "revlim_max_input=" << rumbleConfig.revLimiterEffect.maxInput << "\n";
    out << "revlim_light_strength=" << rumbleConfig.revLimiterEffect.lightStrength << "\n";
    out << "revlim_heavy_strength=" << rumbleConfig.revLimiterEffect.heavyStrength << "\n";
    // Pit limiter effect (binary input)
    out << "pitlim_min_input=" << rumbleConfig.pitLimiterEffect.minInput << "\n";
    out << "pitlim_max_input=" << rumbleConfig.pitLimiterEffect.maxInput << "\n";
    out << "pitlim_light_strength=" << rumbleConfig.pitLimiterEffect.lightStrength << "\n";
    out << "pitlim_heavy_strength=" << rumbleConfig.pitLimiterEffect.heavyStrength << "\n\n";

    // Write HelmetOverlay section (global, not per-profile)
    {
        const auto& hud = hudManager.getHelmetOverlayHud();
        out << "[HelmetOverlay]\n";
        out << "visible=" << (hud.isVisible() ? 1 : 0) << "\n";
        out << "helmetEnabled=" << (hud.m_helmetEnabled ? 1 : 0) << "\n";
        out << "visorMode=" << hud.m_visorMode << "\n";
        out << "helmetUpperVariant=" << hud.m_helmetUpperVariant << "\n";
        out << "helmetLowerVariant=" << hud.m_helmetLowerVariant << "\n";
        out << "helmetUpperOffsetY=" << hud.m_helmetUpperOffsetY << "\n";
        out << "helmetLowerOffsetY=" << hud.m_helmetLowerOffsetY << "\n";
        out << "helmetTiltStrength=" << hud.m_helmetTiltStrength << "\n";
        out << "helmetVibrationStrength=" << hud.m_helmetVibrationStrength << "\n";
        out << "helmetVibrationSensitivity=" << hud.m_helmetVibrationSensitivity << "\n";
        out << "helmetZoom=" << hud.m_helmetZoom << "\n";
        out << "visorTintColor=" << PluginUtils::formatColorHex(hud.m_visorTintColor) << "\n";
        out << "visorTintOpacity=" << hud.m_visorTintOpacity << "\n\n";
    }

    // Write Hotkeys section. Keys are named per action (e.g. standings_key) so
    // the file is self-documenting; values are numeric codes: _key = Windows
    // virtual-key code, _mod = modifier bitmask (1=Ctrl, 2=Shift, 4=Alt),
    // _btn = controller button. 0 means unbound. Actions with no row in the
    // settings UI (e.g. rumble/helmet/performance) are still written here and
    // can be bound by hand-editing.
    const HotkeyManager& hotkeyMgr = HotkeyManager::getInstance();
    out << "[Hotkeys]\n";
    for (int i = 0; i < static_cast<int>(HotkeyAction::COUNT); ++i) {
        HotkeyAction action = static_cast<HotkeyAction>(i);
        const HotkeyBinding& binding = hotkeyMgr.getBinding(action);
        const char* name = getActionConfigName(action);

        out << name << "_key=" << static_cast<int>(binding.keyboard.keyCode) << "\n";
        out << name << "_mod=" << static_cast<int>(binding.keyboard.modifiers) << "\n";
        out << name << "_btn=" << static_cast<int>(binding.controller) << "\n";
    }
    out << "\n";

}

bool SettingsManager::applyGlobalLine(const std::string& section, const std::string& key,
                                      const std::string& value, HudManager& hudManager) {
    if (section == "General") {
        try {
            if (key == "autoSave") {
                UiConfig::getInstance().setAutoSave(std::stoi(value) != 0);
            }
            // Legacy read-only fallbacks: update settings relocated to [Updates]. Old INIs
            // still carry them under [General], so read them here to preserve values on
            // upgrade. Saving writes them only under [Updates], so they migrate on the next
            // save and these branches stop matching. (checkForUpdates is an even older alias.)
            else if (key == "updateMode") {
                // Supported modes: off, notify (auto is treated as notify for backward compatibility)
                if (value == "off") {
                    UpdateChecker::getInstance().setMode(UpdateChecker::UpdateMode::OFF);
                } else if (value == "notify" || value == "auto") {
                    UpdateChecker::getInstance().setMode(UpdateChecker::UpdateMode::NOTIFY);
                }
            } else if (key == "checkForUpdates") {
                UpdateChecker::getInstance().setEnabled(std::stoi(value) != 0);
            } else if (key == "updateChannel") {
                // Load channel before dismissedVersion (setChannel clears dismissedVersion on change)
                if (value == "prerelease") {
                    UpdateChecker::getInstance().setChannel(UpdateChecker::UpdateChannel::PRERELEASE);
                } else {
                    UpdateChecker::getInstance().setChannel(UpdateChecker::UpdateChannel::STABLE);
                }
            } else if (key == "dismissedVersion") {
                UpdateChecker::getInstance().setDismissedVersion(value);
            } else if (key == "controller") {
                int idx = std::stoi(value);
                XInputReader::getInstance().getRumbleConfig().controllerIndex = idx;
                XInputReader::getInstance().setControllerIndex(idx);
            } else if (key == "pbScope") {
                UiConfig::getInstance().setPBScope(stringToPBScope(value));
            }
#if GAME_HAS_RECORDS_PROVIDER
            else if (key == "recordsAutoFetch") {
                hudManager.getRecordsHud().m_bAutoFetch = (std::stoi(value) != 0);
            } else if (key == "recordsProvider") {
                hudManager.getRecordsHud().m_provider = stringToDataProvider(value);
            }
#endif
#if GAME_HAS_DISCORD
            else if (key == "discordRichPresence") {
                DiscordManager::getInstance().setEnabled(std::stoi(value) != 0);
            }
#endif
#if GAME_HAS_STEAM_FRIENDS
            else if (key == "steamFriends") {
                SteamFriendsManager::getInstance().setEnabled(std::stoi(value) != 0);
            }
#endif
            else if (key == "filterDnsRiders") {
                PluginData::getInstance().setFilterDnsRiders(std::stoi(value) != 0);
            }
#if GAME_HAS_HTTP_SERVER
            else if (key == "webServer") {
                HttpServer::getInstance().setEnabled(std::stoi(value) != 0);
            }
#endif
            // Legacy read-only fallbacks: these eight relocated to [Display]. Old INIs
            // still carry them under [General], so read them here to preserve values
            // on upgrade. Saving writes them only under [Display], so they migrate on
            // the next save and these branches stop matching.
            else if (key == "speedUnit") {
                hudManager.getSpeedWidget().m_speedUnit = stringToSpeedUnit(value);
            } else if (key == "fuelUnit") {
                hudManager.getFuelWidget().m_fuelUnit = stringToFuelUnit(value);
            } else if (key == "tempUnit") {
                UiConfig::getInstance().setTemperatureUnit(stringToTempUnit(value));
            } else if (key == "format24h") {
                hudManager.getClockWidget().setFormat24h(std::stoi(value) != 0);
            } else if (key == "shortTimeFormat") {
                PluginData::getInstance().setShortTimeFormat(std::stoi(value) != 0);
            } else if (key == "dropShadow") {
                UiConfig::getInstance().setDropShadow(std::stoi(value) != 0);
            } else if (key == "gridSnapping") {
                UiConfig::getInstance().setGridSnapping(std::stoi(value) != 0);
            } else if (key == "screenClamping") {
                UiConfig::getInstance().setScreenClamping(std::stoi(value) != 0);
            }
        } catch (const std::exception& e) {
            DEBUG_WARN_F("General: Failed to parse settings: %s", e.what());
        }
        return true;
    }

    // Handle Display section (speed/fuel/temp units + clock format; moved here
    // from [General] — shown first on the Appearance tab)
    if (section == "Display") {
        try {
            if (key == "speedUnit") {
                hudManager.getSpeedWidget().m_speedUnit = stringToSpeedUnit(value);
            } else if (key == "fuelUnit") {
                hudManager.getFuelWidget().m_fuelUnit = stringToFuelUnit(value);
            } else if (key == "tempUnit") {
                UiConfig::getInstance().setTemperatureUnit(stringToTempUnit(value));
            } else if (key == "format24h") {
                hudManager.getClockWidget().setFormat24h(std::stoi(value) != 0);
            } else if (key == "shortTimeFormat") {
                PluginData::getInstance().setShortTimeFormat(std::stoi(value) != 0);
            } else if (key == "dropShadow") {
                UiConfig::getInstance().setDropShadow(std::stoi(value) != 0);
            } else if (key == "titleIcons") {
                UiConfig::getInstance().setTitleIcons(std::stoi(value) != 0);
            } else if (key == "gridSnapping") {
                UiConfig::getInstance().setGridSnapping(std::stoi(value) != 0);
            } else if (key == "screenClamping") {
                UiConfig::getInstance().setScreenClamping(std::stoi(value) != 0);
            }
        } catch (const std::exception& e) {
            DEBUG_WARN_F("Display: Failed to parse settings: %s", e.what());
        }
        return true;
    }

    // Handle Updates section (auto-update settings; consolidated from [General]/[Advanced])
    if (section == "Updates") {
        try {
            if (key == "updateChannel") {
                // Apply channel before dismissedVersion: setChannel() clears the dismissed
                // version when the channel changes, so a same-channel dismissal stays intact.
                if (value == "prerelease") {
                    UpdateChecker::getInstance().setChannel(UpdateChecker::UpdateChannel::PRERELEASE);
                } else {
                    UpdateChecker::getInstance().setChannel(UpdateChecker::UpdateChannel::STABLE);
                }
            } else if (key == "updateMode") {
                // Supported modes: off, notify (legacy "auto" maps to notify)
                if (value == "off") {
                    UpdateChecker::getInstance().setMode(UpdateChecker::UpdateMode::OFF);
                } else if (value == "notify" || value == "auto") {
                    UpdateChecker::getInstance().setMode(UpdateChecker::UpdateMode::NOTIFY);
                }
            } else if (key == "updateDebugMode") {
                bool debugMode = (std::stoi(value) != 0);
                UpdateChecker::getInstance().setDebugMode(debugMode);
                UpdateDownloader::getInstance().setDebugMode(debugMode);
            } else if (key == "dismissedVersion") {
                UpdateChecker::getInstance().setDismissedVersion(value);
            }
        } catch (const std::exception& e) {
            DEBUG_WARN_F("Updates: Failed to parse settings: %s", e.what());
        }
        return true;
    }

    // Handle Advanced section (power-user settings)
    if (section == "Advanced") {
        try {
            if (key == "developerMode") {
                m_developerMode = (std::stoi(value) != 0);
            }
            // Legacy read-only fallbacks: updateChannel/updateDebugMode relocated to [Updates].
            // Old INIs carry them under [Advanced]; read them so values survive the upgrade,
            // then they migrate to [Updates] on the next save.
            else if (key == "updateChannel") {
                if (value == "prerelease") {
                    UpdateChecker::getInstance().setChannel(UpdateChecker::UpdateChannel::PRERELEASE);
                } else {
                    UpdateChecker::getInstance().setChannel(UpdateChecker::UpdateChannel::STABLE);
                }
            } else if (key == "updateDebugMode") {
                bool debugMode = (std::stoi(value) != 0);
                UpdateChecker::getInstance().setDebugMode(debugMode);
                UpdateDownloader::getInstance().setDebugMode(debugMode);
            // Note: mapPixelSpacing moved to [MapHud]
            } else if (key == "speedoNeedleColor") {
                hudManager.getSpeedoWidget().setNeedleColor(PluginUtils::parseColorHex(value));
            } else if (key == "speedoShowOdometer") {
                hudManager.getSpeedoWidget().setShowOdometer(std::stoi(value) != 0);
            } else if (key == "speedoShowTripmeter") {
                hudManager.getSpeedoWidget().setShowTripmeter(std::stoi(value) != 0);
            } else if (key == "tachoNeedleColor") {
                hudManager.getTachoWidget().setNeedleColor(PluginUtils::parseColorHex(value));
            } else if (key == "leanArcFillColor") {
                hudManager.getLeanWidget().setArcFillColor(PluginUtils::parseColorHex(value));
            }
#if GAME_HAS_RECORDS_PROVIDER
            else if (key == "recordsShowFooter") {
                hudManager.getRecordsHud().m_bShowFooter = (std::stoi(value) != 0);
            }
#endif
            else if (key == "standingsTopPositions") {
                int topPos = std::stoi(value);
                // Clamp to valid range (0 to MAX_TOP_POSITIONS)
                topPos = std::max(0, std::min(topPos, static_cast<int>(StandingsHud::MAX_TOP_POSITIONS)));
                hudManager.getStandingsHud().m_topPositionsCount = topPos;
            } else if (key == "dropShadowOffsetX") {
                UiConfig::getInstance().setDropShadowOffsetX(std::stof(value));
            } else if (key == "dropShadowOffsetY") {
                UiConfig::getInstance().setDropShadowOffsetY(std::stof(value));
            } else if (key == "dropShadowColor") {
                UiConfig::getInstance().setDropShadowColor(PluginUtils::parseColorHex(value));
            } else if (key == "holdRepeatFastMs") {
                UiConfig::getInstance().setHoldRepeatFastMs(std::stoi(value));
            } else if (key == "cursorActivationThreshold") {
                UiConfig::getInstance().setCursorActivationThreshold(std::stof(value));
            } else if (key == "segmentSnapToSplits") {
                UiConfig::getInstance().setSnapSegmentsToSplits(std::stoi(value) != 0);
            } else if (key == "segmentSnapThreshold") {
                UiConfig::getInstance().setSegmentSnapThreshold(std::stof(value));
            } else if (key == "hazardStationaryTolerance") {
                PluginData::getInstance().setHazardStationaryTolerance(std::stof(value));
            } else if (key == "hazardStationaryDurationMs") {
                PluginData::getInstance().setHazardStationaryDurationMs(std::stoi(value));
            } else if (key == "hazardWrongWayDurationMs") {
                PluginData::getInstance().setHazardWrongWayDurationMs(std::stoi(value));
            } else if (key == "hazardAwarenessDistance") {
                PluginData::getInstance().setHazardAwarenessDistance(std::stof(value));
            } else if (key == "hazardCooldownMs") {
                PluginData::getInstance().setHazardCooldownMs(std::stoi(value));
            } else if (key == "hazardGracePeriodMs") {
                PluginData::getInstance().setHazardGracePeriodMs(std::stoi(value));
            } else if (key == "blueFlagAwarenessDistance") {
                PluginData::getInstance().setBlueFlagAwarenessDistance(std::stof(value));
            } else if (key == "gapNotifyIntervalMs") {
                PluginData::getInstance().setGapNotifyIntervalMs(std::stoi(value));
            }
#if GAME_HAS_HTTP_SERVER
            else if (key == "webServerPort") {
                HttpServer::getInstance().setPort(std::stoi(value));
            } else if (key == "webServerThrottleMs") {
                HttpServer::getInstance().setThrottleMs(std::stoi(value));
            } else if (key == "webServerBindAddress") {
                HttpServer::getInstance().setBindAddress(value);
            }
#endif
        } catch (const std::exception& e) {
            DEBUG_WARN_F("Advanced: Failed to parse settings: %s", e.what());
        }
        return true;
    }

    // Handle Colors section
    if (section == "Colors") {
        ColorConfig& colorConfig = ColorConfig::getInstance();
        try {
            if (key == "primary") {
                colorConfig.setColor(ColorSlot::PRIMARY, PluginUtils::parseColorHex(value));
            } else if (key == "secondary") {
                colorConfig.setColor(ColorSlot::SECONDARY, PluginUtils::parseColorHex(value));
            } else if (key == "tertiary") {
                colorConfig.setColor(ColorSlot::TERTIARY, PluginUtils::parseColorHex(value));
            } else if (key == "muted") {
                colorConfig.setColor(ColorSlot::MUTED, PluginUtils::parseColorHex(value));
            } else if (key == "background") {
                colorConfig.setColor(ColorSlot::BACKGROUND, PluginUtils::parseColorHex(value));
            } else if (key == "positive") {
                colorConfig.setColor(ColorSlot::POSITIVE, PluginUtils::parseColorHex(value));
            } else if (key == "warning") {
                colorConfig.setColor(ColorSlot::WARNING, PluginUtils::parseColorHex(value));
            } else if (key == "neutral") {
                colorConfig.setColor(ColorSlot::NEUTRAL, PluginUtils::parseColorHex(value));
            } else if (key == "negative") {
                colorConfig.setColor(ColorSlot::NEGATIVE, PluginUtils::parseColorHex(value));
            } else if (key == "accent") {
                colorConfig.setColor(ColorSlot::ACCENT, PluginUtils::parseColorHex(value));
            }
        } catch (const std::exception& e) {
            DEBUG_WARN_F("Colors: Failed to parse settings: %s", e.what());
        }
        return true;
    }

    // Handle Fonts section
    if (section == "Fonts") {
        FontConfig& fontConfig = FontConfig::getInstance();
        if (key == "title") {
            fontConfig.setFont(FontCategory::TITLE, value);
        } else if (key == "normal") {
            fontConfig.setFont(FontCategory::NORMAL, value);
        } else if (key == "strong") {
            fontConfig.setFont(FontCategory::STRONG, value);
        } else if (key == "digits") {
            fontConfig.setFont(FontCategory::DIGITS, value);
        } else if (key == "marker") {
            fontConfig.setFont(FontCategory::MARKER, value);
        } else if (key == "small") {
            fontConfig.setFont(FontCategory::SMALL, value);
        }
        return true;
    }

    // Handle Rumble section (effect configuration)
    // Always load into global config (per-bike profiles loaded from JSON)
    if (section == "Rumble") {
        RumbleConfig& config = XInputReader::getInstance().getGlobalRumbleConfig();
        try {
            if (key == "enabled") {
                config.enabled = std::stoi(value) != 0;
            } else if (key == "additive_blend") {
                config.additiveBlend = std::stoi(value) != 0;
            } else if (key == "rumble_when_crashed") {
                config.rumbleWhenCrashed = std::stoi(value) != 0;
            } else if (key == "use_per_bike_effects" || key == "use_per_bike_profiles") {
                // Note: use_per_bike_profiles is backward compatible alias
                config.usePerBikeEffects = std::stoi(value) != 0;
            } else if (key == "send_interval_ms") {
                // Global (never per-bike): lives on XInputReader, not RumbleConfig
                XInputReader::getInstance().setRumbleSendIntervalMs(std::stoi(value));
            } else if (key == "disable_on_crash") {
                // Backward compatibility: invert the old setting
                config.rumbleWhenCrashed = std::stoi(value) == 0;
            }
            // Suspension effect - new format
            else if (key == "susp_min_input") {
                config.suspensionEffect.minInput = std::stof(value);
            } else if (key == "susp_max_input") {
                config.suspensionEffect.maxInput = std::stof(value);
            } else if (key == "susp_light_strength") {
                config.suspensionEffect.lightStrength = std::stof(value);
            } else if (key == "susp_heavy_strength") {
                config.suspensionEffect.heavyStrength = std::stof(value);
            } else if (key == "susp_split") {
                config.suspensionSplit = std::stoi(value) != 0;
            } else if (key == "susp_split_init") {
                config.suspensionSplitInitialized = std::stoi(value) != 0;
            } else if (key == "susp_front_min_input") {
                config.suspensionEffectFront.minInput = std::stof(value);
            } else if (key == "susp_front_max_input") {
                config.suspensionEffectFront.maxInput = std::stof(value);
            } else if (key == "susp_front_light_strength") {
                config.suspensionEffectFront.lightStrength = std::stof(value);
            } else if (key == "susp_front_heavy_strength") {
                config.suspensionEffectFront.heavyStrength = std::stof(value);
            } else if (key == "susp_rear_min_input") {
                config.suspensionEffectRear.minInput = std::stof(value);
            } else if (key == "susp_rear_max_input") {
                config.suspensionEffectRear.maxInput = std::stof(value);
            } else if (key == "susp_rear_light_strength") {
                config.suspensionEffectRear.lightStrength = std::stof(value);
            } else if (key == "susp_rear_heavy_strength") {
                config.suspensionEffectRear.heavyStrength = std::stof(value);
            }
            // Wheelspin effect
            else if (key == "wheel_min_input") {
                config.wheelspinEffect.minInput = std::stof(value);
            } else if (key == "wheel_max_input") {
                config.wheelspinEffect.maxInput = std::stof(value);
            } else if (key == "wheel_light_strength") {
                config.wheelspinEffect.lightStrength = std::stof(value);
            } else if (key == "wheel_heavy_strength") {
                config.wheelspinEffect.heavyStrength = std::stof(value);
            }
            // Brake lockup effect
            else if (key == "lockup_min_input") {
                config.brakeLockupEffect.minInput = std::stof(value);
            } else if (key == "lockup_max_input") {
                config.brakeLockupEffect.maxInput = std::stof(value);
            } else if (key == "lockup_light_strength") {
                config.brakeLockupEffect.lightStrength = std::stof(value);
            } else if (key == "lockup_heavy_strength") {
                config.brakeLockupEffect.heavyStrength = std::stof(value);
            } else if (key == "lockup_split") {
                config.brakeLockupSplit = std::stoi(value) != 0;
            } else if (key == "lockup_split_init") {
                config.brakeLockupSplitInitialized = std::stoi(value) != 0;
            } else if (key == "lockup_front_min_input") {
                config.brakeLockupEffectFront.minInput = std::stof(value);
            } else if (key == "lockup_front_max_input") {
                config.brakeLockupEffectFront.maxInput = std::stof(value);
            } else if (key == "lockup_front_light_strength") {
                config.brakeLockupEffectFront.lightStrength = std::stof(value);
            } else if (key == "lockup_front_heavy_strength") {
                config.brakeLockupEffectFront.heavyStrength = std::stof(value);
            } else if (key == "lockup_rear_min_input") {
                config.brakeLockupEffectRear.minInput = std::stof(value);
            } else if (key == "lockup_rear_max_input") {
                config.brakeLockupEffectRear.maxInput = std::stof(value);
            } else if (key == "lockup_rear_light_strength") {
                config.brakeLockupEffectRear.lightStrength = std::stof(value);
            } else if (key == "lockup_rear_heavy_strength") {
                config.brakeLockupEffectRear.heavyStrength = std::stof(value);
            }
            // RPM effect
            else if (key == "rpm_min_input") {
                config.rpmEffect.minInput = std::stof(value);
            } else if (key == "rpm_max_input") {
                config.rpmEffect.maxInput = std::stof(value);
            } else if (key == "rpm_light_strength") {
                config.rpmEffect.lightStrength = std::stof(value);
            } else if (key == "rpm_heavy_strength") {
                config.rpmEffect.heavyStrength = std::stof(value);
            }
            // Slide effect
            else if (key == "slide_min_input") {
                config.slideEffect.minInput = std::stof(value);
            } else if (key == "slide_max_input") {
                config.slideEffect.maxInput = std::stof(value);
            } else if (key == "slide_light_strength") {
                config.slideEffect.lightStrength = std::stof(value);
            } else if (key == "slide_heavy_strength") {
                config.slideEffect.heavyStrength = std::stof(value);
            }
            // Surface effect
            else if (key == "surface_min_input") {
                config.surfaceEffect.minInput = std::stof(value);
            } else if (key == "surface_max_input") {
                config.surfaceEffect.maxInput = std::stof(value);
            } else if (key == "surface_light_strength") {
                config.surfaceEffect.lightStrength = std::stof(value);
            } else if (key == "surface_heavy_strength") {
                config.surfaceEffect.heavyStrength = std::stof(value);
            }
            // Steer effect
            else if (key == "steer_min_input") {
                config.steerEffect.minInput = std::stof(value);
            } else if (key == "steer_max_input") {
                config.steerEffect.maxInput = std::stof(value);
            } else if (key == "steer_light_strength") {
                config.steerEffect.lightStrength = std::stof(value);
            } else if (key == "steer_heavy_strength") {
                config.steerEffect.heavyStrength = std::stof(value);
            }
            // Wheelie effect
            else if (key == "wheelie_min_input") {
                config.wheelieEffect.minInput = std::stof(value);
            } else if (key == "wheelie_max_input") {
                config.wheelieEffect.maxInput = std::stof(value);
            } else if (key == "wheelie_light_strength") {
                config.wheelieEffect.lightStrength = std::stof(value);
            } else if (key == "wheelie_heavy_strength") {
                config.wheelieEffect.heavyStrength = std::stof(value);
            }
            // Rev limiter effect
            else if (key == "revlim_min_input") {
                config.revLimiterEffect.minInput = std::stof(value);
            } else if (key == "revlim_max_input") {
                config.revLimiterEffect.maxInput = std::stof(value);
            } else if (key == "revlim_light_strength") {
                config.revLimiterEffect.lightStrength = std::stof(value);
            } else if (key == "revlim_heavy_strength") {
                config.revLimiterEffect.heavyStrength = std::stof(value);
            }
            // Pit limiter effect
            else if (key == "pitlim_min_input") {
                config.pitLimiterEffect.minInput = std::stof(value);
            } else if (key == "pitlim_max_input") {
                config.pitLimiterEffect.maxInput = std::stof(value);
            } else if (key == "pitlim_light_strength") {
                config.pitLimiterEffect.lightStrength = std::stof(value);
            } else if (key == "pitlim_heavy_strength") {
                config.pitLimiterEffect.heavyStrength = std::stof(value);
            }
        } catch (const std::exception& e) {
            DEBUG_WARN_F("Rumble: Failed to parse settings: %s", e.what());
        }
        return true;
    }

    // Handle HelmetOverlay section (global, not per-profile)
    if (section == "HelmetOverlay") {
        auto& hud = hudManager.getHelmetOverlayHud();
        try {
            if (key == "visible") {
                hud.setVisible(std::stoi(value) != 0);
            } else if (key == "helmetEnabled") {
                hud.m_helmetEnabled = std::stoi(value) != 0;
            } else if (key == "visorMode") {
                hud.m_visorMode = std::clamp(std::stoi(value), 0, HelmetOverlayHud::VISOR_MODE_COUNT - 1);
            } else if (key == "helmetUpperVariant") {
                hud.m_helmetUpperVariant = std::stoi(value);
            } else if (key == "helmetLowerVariant") {
                hud.m_helmetLowerVariant = std::stoi(value);
            } else if (key == "helmetUpperOffsetY") {
                hud.m_helmetUpperOffsetY = std::stof(value);
            } else if (key == "helmetLowerOffsetY") {
                hud.m_helmetLowerOffsetY = std::stof(value);
            } else if (key == "helmetTiltStrength") {
                hud.m_helmetTiltStrength = std::stof(value);
            } else if (key == "helmetVibrationStrength") {
                hud.m_helmetVibrationStrength = std::stof(value);
            } else if (key == "helmetVibrationSensitivity") {
                hud.m_helmetVibrationSensitivity = std::stof(value);
            } else if (key == "helmetZoom") {
                hud.m_helmetZoom = std::stof(value);
            } else if (key == "visorTintColor") {
                hud.m_visorTintColor = PluginUtils::parseColorHex(value);
            } else if (key == "visorTintOpacity") {
                hud.m_visorTintOpacity = std::stof(value);
            }
        } catch (const std::exception& e) {
            DEBUG_WARN_F("HelmetOverlay: Failed to parse setting '%s': %s", key.c_str(), e.what());
        }
        return true;
    }

    // Handle Hotkeys section
    if (section == "Hotkeys") {
        HotkeyManager& hotkeyMgr = HotkeyManager::getInstance();
        try {
            // Split at the LAST underscore: <name>_<suffix> (suffix = key/mod/btn).
            // Names can contain underscores (e.g. "lap_log", "overlay_last_lap").
            size_t lastUnderscore = key.rfind('_');
            if (lastUnderscore == std::string::npos) return true;
            std::string name = key.substr(0, lastUnderscore);
            std::string suffix = key.substr(lastUnderscore + 1);

            // Resolve the action. New keys are name-based ("standings_key"); the
            // old index-based form ("action0_key") is still accepted so existing
            // configs migrate automatically - read here, written back in the new
            // form on the next save.
            HotkeyAction action = HotkeyAction::COUNT;
            if (name.length() > 6 && name.substr(0, 6) == "action") {
                int idx = std::stoi(name.substr(6));
                if (idx >= 0 && idx < static_cast<int>(HotkeyAction::COUNT)) {
                    action = static_cast<HotkeyAction>(idx);
                }
            } else {
                for (int i = 0; i < static_cast<int>(HotkeyAction::COUNT); ++i) {
                    if (name == getActionConfigName(static_cast<HotkeyAction>(i))) {
                        action = static_cast<HotkeyAction>(i);
                        break;
                    }
                }
            }

            if (action != HotkeyAction::COUNT) {
                HotkeyBinding binding = hotkeyMgr.getBinding(action);
                if (suffix == "key") {
                    binding.keyboard.keyCode = static_cast<uint8_t>(std::stoi(value));
                } else if (suffix == "mod") {
                    binding.keyboard.modifiers = static_cast<ModifierFlags>(std::stoi(value));
                } else if (suffix == "btn") {
                    binding.controller = static_cast<ControllerButton>(std::stoi(value));
                }
                hotkeyMgr.setBinding(action, binding);
            }
        } catch (const std::exception& e) {
            DEBUG_WARN_F("Hotkeys: Failed to parse settings: %s", e.what());
        }
        return true;
    }

    return false;
}

void SettingsManager::saveSettings(const HudManager& hudManager, const char* savePath) {
    std::string filePath = getSettingsFilePath(savePath);
    std::string tempFilePath = filePath + ".tmp";
    m_savePath = savePath ? savePath : "";

    // Capture current state to active profile before saving
    // Note: This modifies m_profileCache, which is why saveSettings is non-const
    captureCurrentState(hudManager);

    // Write to temp file first (atomic write pattern)
    // This prevents corrupted settings if write fails mid-way
    std::ofstream file(tempFilePath);
    if (!file.is_open()) {
        DEBUG_WARN_F("Failed to save settings to: %s", tempFilePath.c_str());
        return;
    }

    DEBUG_INFO_F("Saving settings to: %s (via temp file)", filePath.c_str());

    // Write header comment with usage notes
    file << "; MXBMRP3 Settings File\n";
    file << "; To edit manually, disable Auto-Save in Settings > General,\n";
    file << "; then reload in-game with the hotkey after saving changes.\n";
    file << "\n";

    // Write Settings section (format versioning)
    file << "[Settings]\n";
    file << "version=" << SETTINGS_VERSION << "\n\n";

    // Write Profiles section
    const ProfileManager& profileManager = ProfileManager::getInstance();
    file << "[Profiles]\n";
    file << "activeProfile=" << static_cast<int>(profileManager.getActiveProfile()) << "\n";
    file << "autoSwitch=" << (profileManager.isAutoSwitchEnabled() ? 1 : 0) << "\n\n";

    // Write all global (non-per-profile) sections via the shared serializer, keeping the
    // factory-defaults snapshot (see captureFactoryDefaults) in sync with the saved output.
    writeGlobalSettings(file, hudManager);

    // Save tracked riders to separate JSON file
    TrackedRidersManager::getInstance().save();

    // HUD order for consistent output
    // Note: HelmetOverlayHud is global (own [HelmetOverlay] section), not in this per-profile list.
    // Game-gated HUDs (FmxHud, RecordsHud, FriendsHud) are listed unconditionally;
    // they're skipped below when absent from m_hudDefaults on builds without them.
    static const std::array<const char*, 40> hudOrder = {
        "StandingsHud", "MapHud", "RadarHud", "PitboardHud", "RecordsHud",
        "LapLogHud", "LapConsistencyHud", "FmxHud", "StatsHud", "EventLogHud", "FriendsHud", "IdealLapHud", "TelemetryHud", "PerformanceHud",
        "LapWidget", "PositionWidget", "TimeWidget", "ClockWidget", "SessionHud", "SpeedWidget", "GearWidget",
        "SpeedoWidget", "TachoWidget", "TimingHud", "GapBarHud", "BarsWidget", "VersionWidget",
        "NoticesHud", "FuelWidget", "GamepadWidget", "LeanWidget", "GForceWidget", "CompassWidget", "TyreTempWidget", "EcuWidget", "SettingsButtonWidget", "PointerWidget", "RumbleHud",
        "BenchmarkWidget",
        "Global"
    };

    // Version 4 format: Write base section then sparse profile overrides per HUD
    // Each HUD's profile overrides appear directly after its base section
    for (const char* hudName : hudOrder) {
        auto defaultIt = m_hudDefaults.find(hudName);
        if (defaultIt == m_hudDefaults.end()) continue;

        // Write base section [HudName] with default values
        file << "[" << hudName << "]\n";

        // Write base properties first (for consistent ordering)
        writeBaseHudSettings(file, defaultIt->second);

        // Write HUD-specific properties (with inline comments for IniOnly settings)
        for (const auto& [key, value] : defaultIt->second) {
            if (isBaseKey(key)) continue;
            writeSettingWithComment(file, hudName, key, value);
        }
        file << "\n";

        // Write profile-specific overrides [HudName:ProfileName]
        // Only write values that differ from defaults
        for (int profileIdx = 0; profileIdx < static_cast<int>(ProfileType::COUNT); ++profileIdx) {
            ProfileType profile = static_cast<ProfileType>(profileIdx);
            const ProfileCache& cache = m_profileCache[static_cast<size_t>(profileIdx)];
            const char* profileName = ProfileManager::getProfileName(profile);

            auto cacheIt = cache.find(hudName);
            if (cacheIt == cache.end()) continue;

            // Collect keys that differ from defaults
            std::vector<std::pair<std::string, std::string>> diffKeys;
            for (const auto& [key, value] : cacheIt->second) {
                bool isDifferent = true;
                auto defKeyIt = defaultIt->second.find(key);
                if (defKeyIt != defaultIt->second.end() && defKeyIt->second == value) {
                    isDifferent = false;
                }
                if (isDifferent) {
                    diffKeys.emplace_back(key, value);
                }
            }

            // Only write section if there are differences
            if (!diffKeys.empty()) {
                file << "[" << hudName << ":" << profileName << "]\n";

                // Write differing keys (base properties first for consistency)
                for (const auto& [key, value] : diffKeys) {
                    if (isBaseKey(key)) {
                        file << key << "=" << value << "\n";
                    }
                }
                for (const auto& [key, value] : diffKeys) {
                    if (!isBaseKey(key)) {
                        file << key << "=" << value << "\n";
                    }
                }
                file << "\n";
            }
        }
    }

    // Write GamepadWidget per-variant layouts (not per-profile, global)
    // Only save layouts that actually exist (default: variants 1 and 2)
    {
        file << "# GamepadWidget Per-Variant Layouts\n";
        const auto& gamepadWidget = hudManager.getGamepadWidget();

        // Check which layouts exist and save them
        for (int variant = 1; variant <= 10; ++variant) {
            const auto* layout = gamepadWidget.getLayoutIfExists(variant);
            if (!layout) continue;

            file << "[GamepadWidget_Layout_" << variant << "]\n";
            file << "backgroundWidth=" << layout->backgroundWidth << "\n";
            file << "triggerWidth=" << layout->triggerWidth << "\n";
            file << "triggerHeight=" << layout->triggerHeight << "\n";
            file << "bumperWidth=" << layout->bumperWidth << "\n";
            file << "bumperHeight=" << layout->bumperHeight << "\n";
            file << "dpadWidth=" << layout->dpadWidth << "\n";
            file << "dpadHeight=" << layout->dpadHeight << "\n";
            file << "faceButtonSize=" << layout->faceButtonSize << "\n";
            file << "menuButtonWidth=" << layout->menuButtonWidth << "\n";
            file << "menuButtonHeight=" << layout->menuButtonHeight << "\n";
            file << "stickSize=" << layout->stickSize << "\n";
            file << "leftTriggerX=" << layout->leftTriggerX << "\n";
            file << "leftTriggerY=" << layout->leftTriggerY << "\n";
            file << "rightTriggerX=" << layout->rightTriggerX << "\n";
            file << "rightTriggerY=" << layout->rightTriggerY << "\n";
            file << "leftBumperX=" << layout->leftBumperX << "\n";
            file << "leftBumperY=" << layout->leftBumperY << "\n";
            file << "rightBumperX=" << layout->rightBumperX << "\n";
            file << "rightBumperY=" << layout->rightBumperY << "\n";
            file << "leftStickX=" << layout->leftStickX << "\n";
            file << "leftStickY=" << layout->leftStickY << "\n";
            file << "rightStickX=" << layout->rightStickX << "\n";
            file << "rightStickY=" << layout->rightStickY << "\n";
            file << "dpadX=" << layout->dpadX << "\n";
            file << "dpadY=" << layout->dpadY << "\n";
            file << "faceButtonsX=" << layout->faceButtonsX << "\n";
            file << "faceButtonsY=" << layout->faceButtonsY << "\n";
            file << "menuButtonsX=" << layout->menuButtonsX << "\n";
            file << "menuButtonsY=" << layout->menuButtonsY << "\n";
            file << "dpadSpacing=" << layout->dpadSpacing << "\n";
            file << "faceButtonSpacing=" << layout->faceButtonSpacing << "\n";
            file << "menuButtonSpacing=" << layout->menuButtonSpacing << "\n";
            writeSettingWithComment(file, "GamepadWidget", "triggerFillMode", std::to_string(layout->triggerFillMode));
            file << "\n";
        }
    }

    // Write PitboardHud per-texture layouts (not per-profile, global)
    {
        file << "# PitboardHud Per-Texture Layouts\n";
        const auto& pitboardHud = hudManager.getPitboardHud();

        // Check which layouts exist and save them
        for (int variant = 1; variant <= 10; ++variant) {
            const auto* layout = pitboardHud.getLayoutIfExists(variant);
            if (!layout) continue;

            file << "[PitboardHud_Layout_" << variant << "]\n";
            file << "riderIdX=" << layout->riderIdX << "\n";
            file << "riderIdY=" << layout->riderIdY << "\n";
            file << "sessionX=" << layout->sessionX << "\n";
            file << "sessionY=" << layout->sessionY << "\n";
            file << "positionX=" << layout->positionX << "\n";
            file << "positionY=" << layout->positionY << "\n";
            file << "timeX=" << layout->timeX << "\n";
            file << "timeY=" << layout->timeY << "\n";
            file << "lapX=" << layout->lapX << "\n";
            file << "lapY=" << layout->lapY << "\n";
            file << "lastLapX=" << layout->lastLapX << "\n";
            file << "lastLapY=" << layout->lastLapY << "\n";
            file << "gapX=" << layout->gapX << "\n";
            file << "gapY=" << layout->gapY << "\n";
            file << "\n";
        }
    }

    if (!file.good()) {
        DEBUG_WARN_F("Stream error occurred while writing settings to: %s", tempFilePath.c_str());
        file.close();
        std::remove(tempFilePath.c_str());
        return;
    }

    file.close();

    // Atomic rename: replace original with temp file
    // This ensures we never have a partially written settings file
    // Use MoveFileExA for atomic replace (consistent with stats_manager.cpp)
    if (!MoveFileExA(tempFilePath.c_str(), filePath.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
        DEBUG_WARN_F("Failed to save settings (error %lu): %s", GetLastError(), filePath.c_str());
        std::remove(tempFilePath.c_str());
        return;
    }

    DEBUG_INFO("Settings saved successfully");
}

void SettingsManager::loadSettings(HudManager& hudManager, const char* savePath) {
    std::string filePath = getSettingsFilePath(savePath);
    m_savePath = savePath ? savePath : "";

    // Capture factory defaults from HUDs BEFORE loading any settings
    // This gives us the constructor default values to use for sparse saving
    captureFactoryDefaults(hudManager);

    // Mark that settings loading has started (used by assertion in captureFactoryDefaults)
    m_settingsLoaded = true;

    std::ifstream file(filePath);
    if (!file.is_open()) {
        DEBUG_INFO_F("No settings file found at: %s (using defaults)", filePath.c_str());
        // Initialize cache with current (default) state for all profiles
        for (int i = 0; i < static_cast<int>(ProfileType::COUNT); ++i) {
            captureToProfile(hudManager, static_cast<ProfileType>(i));
        }
        return;
    }

    DEBUG_INFO_F("Loading settings from: %s", filePath.c_str());

    // Clear existing cache
    for (auto& cache : m_profileCache) {
        cache.clear();
    }

    std::string line;
    std::string currentSection;
    std::string currentHudName;
    int currentProfileIndex = -1;
    int loadedVersion = 0;  // Version 0 means old format (pre-versioning)

    while (std::getline(file, line)) {
        // Trim whitespace
        size_t start = line.find_first_not_of(" \t\r\n");
        size_t end = line.find_last_not_of(" \t\r\n");
        if (start == std::string::npos) continue;
        if (end == std::string::npos) continue;
        line = line.substr(start, end - start + 1);

        // Skip comments
        if (line[0] == '#') continue;

        // Check for section header
        if (line.length() >= 3 && line.front() == '[' && line.back() == ']') {
            currentSection = line.substr(1, line.length() - 2);
            parseSectionName(currentSection, currentHudName, currentProfileIndex);
            continue;
        }

        // Parse key=value
        size_t equals = line.find('=');
        if (equals == std::string::npos) continue;

        std::string key = line.substr(0, equals);
        std::string value = line.substr(equals + 1);

        // Strip inline comments (everything after ';')
        size_t commentPos = value.find(';');
        if (commentPos != std::string::npos) {
            value = value.substr(0, commentPos);
            // Trim trailing whitespace from value
            size_t valueEnd = value.find_last_not_of(" \t");
            if (valueEnd != std::string::npos) {
                value = value.substr(0, valueEnd + 1);
            } else {
                value.clear();  // Value was only whitespace before comment
            }
        }

        // Handle Settings section (format versioning)
        if (currentHudName == "Settings") {
            try {
                if (key == "version") {
                    loadedVersion = std::stoi(value);
                    DEBUG_INFO_F("Settings file version: %d (current: %d)", loadedVersion, SETTINGS_VERSION);
                }
            } catch (const std::exception& e) {
                DEBUG_WARN_F("Settings: Failed to parse version: %s", e.what());
            }
            continue;
        }

        // Handle Profiles section
        if (currentHudName == "Profiles") {
            try {
                if (key == "activeProfile") {
                    int profileIdx = std::stoi(value);
                    if (profileIdx >= 0 && profileIdx < static_cast<int>(ProfileType::COUNT)) {
                        ProfileManager::getInstance().setActiveProfile(static_cast<ProfileType>(profileIdx));
                    }
                } else if (key == "autoSwitch") {
                    ProfileManager::getInstance().setAutoSwitchEnabled(std::stoi(value) != 0);
                }
            } catch (const std::exception& e) {
                DEBUG_WARN_F("Profiles: Failed to parse settings: %s", e.what());
            }
            continue;
        }

        // Global (non-per-profile) sections share one applier so loadSettings() and
        // resetGlobalsToFactoryDefaults() cannot drift as new globals are added.
        if (applyGlobalLine(currentHudName, key, value, hudManager)) {
            continue;
        }

        // Handle GamepadWidget_Layout_N sections (per-variant layouts)
        if (currentHudName.length() > 20 && currentHudName.substr(0, 20) == "GamepadWidget_Layout") {
            try {
                int variant = std::stoi(currentHudName.substr(21));
                if (variant >= 1 && variant <= 10) {
                    auto& hud = hudManager.getGamepadWidget();
                    auto& layout = hud.getLayout(variant);

                    if (key == "backgroundWidth") layout.backgroundWidth = std::stof(value);
                    else if (key == "triggerWidth") layout.triggerWidth = std::stof(value);
                    else if (key == "triggerHeight") layout.triggerHeight = std::stof(value);
                    else if (key == "bumperWidth") layout.bumperWidth = std::stof(value);
                    else if (key == "bumperHeight") layout.bumperHeight = std::stof(value);
                    else if (key == "dpadWidth") layout.dpadWidth = std::stof(value);
                    else if (key == "dpadHeight") layout.dpadHeight = std::stof(value);
                    else if (key == "faceButtonSize") layout.faceButtonSize = std::stof(value);
                    else if (key == "menuButtonWidth") layout.menuButtonWidth = std::stof(value);
                    else if (key == "menuButtonHeight") layout.menuButtonHeight = std::stof(value);
                    else if (key == "stickSize") layout.stickSize = std::stof(value);
                    else if (key == "leftTriggerX") layout.leftTriggerX = std::stof(value);
                    else if (key == "leftTriggerY") layout.leftTriggerY = std::stof(value);
                    else if (key == "rightTriggerX") layout.rightTriggerX = std::stof(value);
                    else if (key == "rightTriggerY") layout.rightTriggerY = std::stof(value);
                    else if (key == "leftBumperX") layout.leftBumperX = std::stof(value);
                    else if (key == "leftBumperY") layout.leftBumperY = std::stof(value);
                    else if (key == "rightBumperX") layout.rightBumperX = std::stof(value);
                    else if (key == "rightBumperY") layout.rightBumperY = std::stof(value);
                    else if (key == "leftStickX") layout.leftStickX = std::stof(value);
                    else if (key == "leftStickY") layout.leftStickY = std::stof(value);
                    else if (key == "rightStickX") layout.rightStickX = std::stof(value);
                    else if (key == "rightStickY") layout.rightStickY = std::stof(value);
                    else if (key == "dpadX") layout.dpadX = std::stof(value);
                    else if (key == "dpadY") layout.dpadY = std::stof(value);
                    else if (key == "faceButtonsX") layout.faceButtonsX = std::stof(value);
                    else if (key == "faceButtonsY") layout.faceButtonsY = std::stof(value);
                    else if (key == "menuButtonsX") layout.menuButtonsX = std::stof(value);
                    else if (key == "menuButtonsY") layout.menuButtonsY = std::stof(value);
                    else if (key == "dpadSpacing") layout.dpadSpacing = std::stof(value);
                    else if (key == "faceButtonSpacing") layout.faceButtonSpacing = std::stof(value);
                    else if (key == "menuButtonSpacing") layout.menuButtonSpacing = std::stof(value);
                    else if (key == "triggerFillMode") layout.triggerFillMode = std::stoi(value);
                }
            } catch (const std::exception& e) {
                DEBUG_WARN_F("GamepadWidget Layout: Failed to parse settings: %s", e.what());
            }
            continue;
        }

        // Handle PitboardHud_Layout_N sections (per-texture layouts)
        if (currentHudName.length() > 18 && currentHudName.substr(0, 18) == "PitboardHud_Layout") {
            try {
                int variant = std::stoi(currentHudName.substr(19));
                if (variant >= 1 && variant <= 10) {
                    auto& hud = hudManager.getPitboardHud();
                    auto& layout = hud.getLayout(variant);

                    // Clamp layout offsets to reasonable range (-1.0 to 1.0)
                    auto clampOffset = [](float v) { return std::clamp(v, -1.0f, 1.0f); };

                    if (key == "riderIdX") layout.riderIdX = clampOffset(std::stof(value));
                    else if (key == "riderIdY") layout.riderIdY = clampOffset(std::stof(value));
                    else if (key == "sessionX") layout.sessionX = clampOffset(std::stof(value));
                    else if (key == "sessionY") layout.sessionY = clampOffset(std::stof(value));
                    else if (key == "positionX") layout.positionX = clampOffset(std::stof(value));
                    else if (key == "positionY") layout.positionY = clampOffset(std::stof(value));
                    else if (key == "timeX") layout.timeX = clampOffset(std::stof(value));
                    else if (key == "timeY") layout.timeY = clampOffset(std::stof(value));
                    else if (key == "lapX") layout.lapX = clampOffset(std::stof(value));
                    else if (key == "lapY") layout.lapY = clampOffset(std::stof(value));
                    else if (key == "lastLapX") layout.lastLapX = clampOffset(std::stof(value));
                    else if (key == "lastLapY") layout.lastLapY = clampOffset(std::stof(value));
                    else if (key == "gapX") layout.gapX = clampOffset(std::stof(value));
                    else if (key == "gapY") layout.gapY = clampOffset(std::stof(value));
                }
            } catch (const std::exception& e) {
                DEBUG_WARN_F("PitboardHud Layout: Failed to parse settings: %s", e.what());
            }
            continue;
        }

        // Handle HUD settings sections
        // Version 4+: [HudName] for base/defaults, [HudName:ProfileName] for overrides
        // Version 3: [HudName:0], [HudName:1], etc. (no base sections)
        if (loadedVersion >= SETTINGS_VERSION) {
            if (currentProfileIndex == -1) {
                // Base section [HudName] - apply to ALL profiles as baseline
                for (int i = 0; i < static_cast<int>(ProfileType::COUNT); ++i) {
                    m_profileCache[i][currentHudName][key] = value;
                }
                // Also update defaults so base keys round-trip correctly on save
                // (without this, user-added base keys like color_primary would migrate to profile sections)
                // Normalize color values to canonical format so string diffs work after captureToProfile
                if (key.rfind("color_", 0) == 0) {
                    try {
                        m_hudDefaults[currentHudName][key] = PluginUtils::formatColorHex(PluginUtils::parseColorHex(value));
                    } catch (const std::exception&) {
                        // Malformed hand-edited color value - keep the raw string
                        // rather than aborting the whole loadSettings() parse
                        DEBUG_WARN_F("Invalid color value '%s' for [%s] %s", value.c_str(), currentHudName.c_str(), key.c_str());
                        m_hudDefaults[currentHudName][key] = value;
                    }
                } else {
                    m_hudDefaults[currentHudName][key] = value;
                }
            } else if (currentProfileIndex >= 0 && currentProfileIndex < static_cast<int>(ProfileType::COUNT)) {
                // Profile-specific section [HudName:ProfileName] - overlay onto that profile
                m_profileCache[currentProfileIndex][currentHudName][key] = value;
            }
        } else if (loadedVersion == 3) {
            // v3 format: only profile-specific sections [HudName:0], [HudName:1], etc.
            if (currentProfileIndex >= 0 && currentProfileIndex < static_cast<int>(ProfileType::COUNT)) {
                m_profileCache[currentProfileIndex][currentHudName][key] = value;
            }
        }
        // Version < 3 settings are not supported (too old)
    }

    file.close();

    // Check if we need to reset to defaults due to old version
    // We support v3+ (v3 used numeric profile indices, v4+ uses named profiles)
    if (loadedVersion > 0 && loadedVersion < 3) {
        DEBUG_INFO_F("Settings version too old (file: %d, minimum: 3) - resetting HUD settings to defaults",
                    loadedVersion);
        DEBUG_INFO("Note: Global settings (colors, fonts, hotkeys) are preserved");
        for (int i = 0; i < static_cast<int>(ProfileType::COUNT); ++i) {
            captureToProfile(hudManager, static_cast<ProfileType>(i));
        }
    }

    // If cache is empty (corrupted file or first run), initialize all profiles with defaults
    bool anyProfileEmpty = false;
    for (const auto& cache : m_profileCache) {
        if (cache.empty()) {
            anyProfileEmpty = true;
            break;
        }
    }
    if (anyProfileEmpty && loadedVersion >= 3) {
        DEBUG_INFO("Initializing profiles with defaults (empty cache despite valid version)");
        for (int i = 0; i < static_cast<int>(ProfileType::COUNT); ++i) {
            captureToProfile(hudManager, static_cast<ProfileType>(i));
        }
    }

    m_cacheInitialized = true;

    // Drop retired keys so they don't linger in the rewritten file. The
    // forward-compatible loader preserves unknown keys by design; this prunes
    // only keys we have explicitly retired (renamed/removed), keeping upgraded
    // files tidy without touching genuinely-unknown keys.
    {
        static const std::pair<const char*, const char*> kRetiredKeys[] = {
            {"StandingsHud", "useAccentHighlight"},        // 1.22.0.0 -> playerRowHighlightAccent (#175)
            {"StandingsHud", "playerRowHighlightAccent"},  // -> playerRowHighlightBrand (this branch)
        };
        for (const auto& [hudName, key] : kRetiredKeys) {
            auto defIt = m_hudDefaults.find(hudName);
            if (defIt != m_hudDefaults.end()) defIt->second.erase(key);
            for (auto& cache : m_profileCache) {
                auto hudIt = cache.find(hudName);
                if (hudIt != cache.end()) hudIt->second.erase(key);
            }
        }
    }

    // Apply active profile to HUDs
    applyActiveProfile(hudManager);

    // Cleanup any leftover files from previous updates
    UpdateDownloader::getInstance().cleanupOldFiles();

    // Trigger update check on startup if enabled
    if (UpdateChecker::getInstance().isEnabled()) {
        DEBUG_INFO("Update check enabled, checking for updates on startup");
        // Set one-time startup callback to show version widget notification when update is found.
        // Note: Manual checks from settings UI will set their own callback, overwriting this one.
        UpdateChecker::getInstance().setCompletionCallback([]() {
            UpdateChecker& checker = UpdateChecker::getInstance();
            if (checker.getStatus() == UpdateChecker::Status::UPDATE_AVAILABLE) {
                if (checker.shouldShowUpdateNotification()) {
                    // Show the version widget with update notification
                    HudManager::getInstance().getVersionWidget().showUpdateNotification();
                } else {
                    DEBUG_INFO_F("Update %s available but dismissed by user",
                                checker.getLatestVersion().c_str());
                }
            }
        });
        UpdateChecker::getInstance().checkForUpdates();
    }

    DEBUG_INFO("Settings loaded successfully");
}
