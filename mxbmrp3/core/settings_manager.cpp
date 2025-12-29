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
#include "../hud/standings_hud.h"
#include "../hud/performance_hud.h"
#include "../hud/telemetry_hud.h"
#include "../hud/time_widget.h"
#include "../hud/position_widget.h"
#include "../hud/lap_widget.h"
#include "../hud/session_widget.h"
#include "../hud/speed_widget.h"
#include "../hud/speedo_widget.h"
#include "../hud/tacho_widget.h"
#include "../hud/timing_hud.h"
#include "../hud/gap_bar_hud.h"
#include "../hud/bars_widget.h"
#include "../hud/version_widget.h"
#include "../hud/notices_widget.h"
#include "../hud/fuel_widget.h"
#include "../hud/settings_button_widget.h"
#include "../hud/pointer_widget.h"
#include "../hud/map_hud.h"
#include "../hud/radar_hud.h"
#include "../hud/pitboard_hud.h"
#include "../hud/records_hud.h"
#include "../hud/rumble_hud.h"
#include "../hud/gamepad_widget.h"
#include "color_config.h"
#include "font_config.h"
#include "update_checker.h"
#include "xinput_reader.h"
#include "hotkey_manager.h"
#include "tracked_riders_manager.h"
#include "asset_manager.h"
#include <fstream>
#include <sstream>
#include <array>
#include <windows.h>

namespace {
    constexpr const char* SETTINGS_SUBDIRECTORY = "mxbmrp3";
    constexpr const char* SETTINGS_FILENAME = "mxbmrp3_settings.ini";

    // Settings format version - bump this when making incompatible changes
    // Version 1: Original format with bitmasks (implicit, no version field)
    // Version 2: Named keys instead of bitmasks for columns/rows/elements
    // Version 3: String enums instead of integers for all enum settings
    constexpr int SETTINGS_VERSION = 3;

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
            constexpr const char* OFFICIAL_GAP_MODE = "officialGapMode";
            constexpr const char* LIVE_GAP_MODE = "liveGapMode";
            constexpr const char* GAP_INDICATOR_MODE = "gapIndicatorMode";
            constexpr const char* GAP_REFERENCE_MODE = "gapReferenceMode";
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
            constexpr const char* SHOW_PLAYER_ARROW = "showPlayerArrow";
            constexpr const char* RADAR_MODE = "radarMode";
            constexpr const char* ALERT_DISTANCE = "alertDistance";
        }

        // RecordsHud-specific keys
        namespace Records {
            constexpr const char* PROVIDER = "provider";
            constexpr const char* RECORDS_TO_SHOW = "recordsToShow";
        }

        // LapLogHud-specific keys
        namespace LapLog {
            constexpr const char* MAX_DISPLAY_LAPS = "maxDisplayLaps";
        }

        // TimingHud-specific keys
        namespace Timing {
            constexpr const char* TIME_MODE = "timeMode";
            constexpr const char* GAP_MODE = "gapMode";
            constexpr const char* DISPLAY_DURATION = "displayDuration";
            constexpr const char* GAP_TYPES = "gapTypes";
        }

        // SpeedWidget-specific keys
        namespace Speed {
            constexpr const char* SPEED_UNIT = "speedUnit";
        }

        // FuelWidget-specific keys
        namespace Fuel {
            constexpr const char* FUEL_UNIT = "fuelUnit";
        }

        // ====================================================================
        // Named keys for bitmask fields (replaces positional bit storage)
        // These are stable identifiers that won't break when options are added
        // ====================================================================

        // StandingsHud columns
        namespace StandingsCols {
            constexpr const char* TRACKED = "col_tracked";
            constexpr const char* POS = "col_pos";
            constexpr const char* RACENUM = "col_racenum";
            constexpr const char* NAME = "col_name";
            constexpr const char* BIKE = "col_bike";
            constexpr const char* STATUS = "col_status";
            constexpr const char* PENALTY = "col_penalty";
            constexpr const char* BEST_LAP = "col_best_lap";
            constexpr const char* OFFICIAL_GAP = "col_official_gap";
            constexpr const char* LIVE_GAP = "col_live_gap";
            constexpr const char* DEBUG = "col_debug";
        }

        // RecordsHud columns
        namespace RecordsCols {
            constexpr const char* POS = "col_pos";
            constexpr const char* RIDER = "col_rider";
            constexpr const char* BIKE = "col_bike";
            constexpr const char* LAPTIME = "col_laptime";
            constexpr const char* DATE = "col_date";
        }

        // LapLogHud columns
        namespace LapLogCols {
            constexpr const char* LAP = "col_lap";
            constexpr const char* S1 = "col_s1";
            constexpr const char* S2 = "col_s2";
            constexpr const char* S3 = "col_s3";
            constexpr const char* TIME = "col_time";
        }

        // IdealLapHud rows
        namespace IdealLapRows {
            constexpr const char* S1 = "row_s1";
            constexpr const char* S2 = "row_s2";
            constexpr const char* S3 = "row_s3";
            constexpr const char* LAST = "row_last";
            constexpr const char* BEST = "row_best";
            constexpr const char* IDEAL = "row_ideal";
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
            constexpr const char* SPEED = "row_speed";
            constexpr const char* UNITS = "row_units";
            constexpr const char* GEAR = "row_gear";
        }

        // FuelWidget rows
        namespace FuelRows {
            constexpr const char* FUEL = "row_fuel";
            constexpr const char* USED = "row_used";
            constexpr const char* AVG = "row_avg";
            constexpr const char* EST = "row_est";
        }

        // BarsWidget columns
        namespace BarsCols {
            constexpr const char* THROTTLE = "col_throttle";
            constexpr const char* BRAKE = "col_brake";
            constexpr const char* CLUTCH = "col_clutch";
            constexpr const char* RPM = "col_rpm";
            constexpr const char* SUSPENSION = "col_suspension";
            constexpr const char* FUEL = "col_fuel";
        }

        // NoticesWidget notices
        namespace Notices {
            constexpr const char* WRONG_WAY = "notice_wrong_way";
            constexpr const char* BLUE_FLAG = "notice_blue_flag";
            constexpr const char* LAST_LAP = "notice_last_lap";
            constexpr const char* FINISHED = "notice_finished";
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
            case StandingsHud::GapMode::ALL: return "ALL";
            default: return "OFF";
        }
    }

    StandingsHud::GapMode stringToGapMode(const std::string& str, StandingsHud::GapMode defaultVal = StandingsHud::GapMode::OFF) {
        if (str == "OFF") return StandingsHud::GapMode::OFF;
        if (str == "PLAYER") return StandingsHud::GapMode::PLAYER;
        if (str == "ALL") return StandingsHud::GapMode::ALL;
        DEBUG_WARN_F("Unknown GapMode '%s', using default", str.c_str());
        return defaultVal;
    }

    // StandingsHud::GapIndicatorMode
    const char* gapIndicatorModeToString(StandingsHud::GapIndicatorMode mode) {
        switch (mode) {
            case StandingsHud::GapIndicatorMode::OFF: return "OFF";
            case StandingsHud::GapIndicatorMode::OFFICIAL: return "OFFICIAL";
            case StandingsHud::GapIndicatorMode::LIVE: return "LIVE";
            case StandingsHud::GapIndicatorMode::BOTH: return "BOTH";
            default: return "OFF";
        }
    }

    StandingsHud::GapIndicatorMode stringToGapIndicatorMode(const std::string& str, StandingsHud::GapIndicatorMode defaultVal = StandingsHud::GapIndicatorMode::OFF) {
        if (str == "OFF") return StandingsHud::GapIndicatorMode::OFF;
        if (str == "OFFICIAL") return StandingsHud::GapIndicatorMode::OFFICIAL;
        if (str == "LIVE") return StandingsHud::GapIndicatorMode::LIVE;
        if (str == "BOTH") return StandingsHud::GapIndicatorMode::BOTH;
        DEBUG_WARN_F("Unknown GapIndicatorMode '%s', using default", str.c_str());
        return defaultVal;
    }

    // StandingsHud::GapReferenceMode
    const char* gapReferenceModeToString(StandingsHud::GapReferenceMode mode) {
        switch (mode) {
            case StandingsHud::GapReferenceMode::LEADER: return "LEADER";
            case StandingsHud::GapReferenceMode::PLAYER: return "PLAYER";
            default: return "LEADER";
        }
    }

    StandingsHud::GapReferenceMode stringToGapReferenceMode(const std::string& str, StandingsHud::GapReferenceMode defaultVal = StandingsHud::GapReferenceMode::LEADER) {
        if (str == "LEADER") return StandingsHud::GapReferenceMode::LEADER;
        if (str == "PLAYER") return StandingsHud::GapReferenceMode::PLAYER;
        DEBUG_WARN_F("Unknown GapReferenceMode '%s', using default", str.c_str());
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

    // RecordsHud::DataProvider
    const char* dataProviderToString(RecordsHud::DataProvider provider) {
        switch (provider) {
            case RecordsHud::DataProvider::CBR: return "CBR";
            default: return "CBR";
        }
    }

    RecordsHud::DataProvider stringToDataProvider(const std::string& str, RecordsHud::DataProvider defaultVal = RecordsHud::DataProvider::CBR) {
        if (str == "CBR") return RecordsHud::DataProvider::CBR;
        DEBUG_WARN_F("Unknown DataProvider '%s', using default", str.c_str());
        return defaultVal;
    }

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
    bool parseSectionName(const std::string& section, std::string& hudName, int& profileIndex) {
        size_t colonPos = section.find(':');
        if (colonPos == std::string::npos) {
            hudName = section;
            profileIndex = -1;  // Global section
            return false;
        }
        hudName = section.substr(0, colonPos);
        try {
            profileIndex = std::stoi(section.substr(colonPos + 1));
            return true;
        } catch (...) {
            hudName = section;
            profileIndex = -1;
            return false;
        }
    }

    // Helper to capture base HUD properties to a settings map
    void captureBaseHudSettings(SettingsManager::HudSettings& settings, const BaseHud& hud) {
        using namespace Keys::Base;
        settings[VISIBLE] = std::to_string(hud.isVisible() ? 1 : 0);
        settings[SHOW_TITLE] = std::to_string(hud.getShowTitle() ? 1 : 0);
        settings[SHOW_BG_TEXTURE] = std::to_string(hud.getShowBackgroundTexture() ? 1 : 0);
        settings[TEXTURE_VARIANT] = std::to_string(hud.getTextureVariant());
        settings[BG_OPACITY] = std::to_string(hud.getBackgroundOpacity());
        settings[SCALE] = std::to_string(hud.getScale());
        settings[OFFSET_X] = std::to_string(hud.getOffsetX());
        settings[OFFSET_Y] = std::to_string(hud.getOffsetY());
    }

    // Helper to write base HUD properties to file
    void writeBaseHudSettings(std::ofstream& file, const SettingsManager::HudSettings& settings) {
        using namespace Keys::Base;
        static const std::array<const char*, 7> baseKeys = {
            VISIBLE, SHOW_TITLE, SHOW_BG_TEXTURE, BG_OPACITY,
            SCALE, OFFSET_X, OFFSET_Y
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
               key == BG_OPACITY || key == SCALE || key == OFFSET_X || key == OFFSET_Y;
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
        saveBitAsKey(settings, RACENUM, cols, StandingsHud::COL_RACENUM);
        saveBitAsKey(settings, NAME, cols, StandingsHud::COL_NAME);
        saveBitAsKey(settings, BIKE, cols, StandingsHud::COL_BIKE);
        saveBitAsKey(settings, STATUS, cols, StandingsHud::COL_STATUS);
        saveBitAsKey(settings, PENALTY, cols, StandingsHud::COL_PENALTY);
        saveBitAsKey(settings, BEST_LAP, cols, StandingsHud::COL_BEST_LAP);
        saveBitAsKey(settings, OFFICIAL_GAP, cols, StandingsHud::COL_OFFICIAL_GAP);
        saveBitAsKey(settings, LIVE_GAP, cols, StandingsHud::COL_LIVE_GAP);
        saveBitAsKey(settings, DEBUG, cols, StandingsHud::COL_DEBUG);
    }

    // StandingsHud: load columns from named keys
    void loadStandingsColumns(const SettingsManager::HudSettings& settings, uint32_t& cols) {
        using namespace Keys::StandingsCols;
        loadBitFromKey(settings, TRACKED, cols, StandingsHud::COL_TRACKED);
        loadBitFromKey(settings, POS, cols, StandingsHud::COL_POS);
        loadBitFromKey(settings, RACENUM, cols, StandingsHud::COL_RACENUM);
        loadBitFromKey(settings, NAME, cols, StandingsHud::COL_NAME);
        loadBitFromKey(settings, BIKE, cols, StandingsHud::COL_BIKE);
        loadBitFromKey(settings, STATUS, cols, StandingsHud::COL_STATUS);
        loadBitFromKey(settings, PENALTY, cols, StandingsHud::COL_PENALTY);
        loadBitFromKey(settings, BEST_LAP, cols, StandingsHud::COL_BEST_LAP);
        loadBitFromKey(settings, OFFICIAL_GAP, cols, StandingsHud::COL_OFFICIAL_GAP);
        loadBitFromKey(settings, LIVE_GAP, cols, StandingsHud::COL_LIVE_GAP);
        loadBitFromKey(settings, DEBUG, cols, StandingsHud::COL_DEBUG);
    }

    // RecordsHud: save columns as named keys
    void saveRecordsColumns(SettingsManager::HudSettings& settings, uint32_t cols) {
        using namespace Keys::RecordsCols;
        saveBitAsKey(settings, POS, cols, RecordsHud::COL_POS);
        saveBitAsKey(settings, RIDER, cols, RecordsHud::COL_RIDER);
        saveBitAsKey(settings, BIKE, cols, RecordsHud::COL_BIKE);
        saveBitAsKey(settings, LAPTIME, cols, RecordsHud::COL_LAPTIME);
        saveBitAsKey(settings, DATE, cols, RecordsHud::COL_DATE);
    }

    // RecordsHud: load columns from named keys
    void loadRecordsColumns(const SettingsManager::HudSettings& settings, uint32_t& cols) {
        using namespace Keys::RecordsCols;
        loadBitFromKey(settings, POS, cols, RecordsHud::COL_POS);
        loadBitFromKey(settings, RIDER, cols, RecordsHud::COL_RIDER);
        loadBitFromKey(settings, BIKE, cols, RecordsHud::COL_BIKE);
        loadBitFromKey(settings, LAPTIME, cols, RecordsHud::COL_LAPTIME);
        loadBitFromKey(settings, DATE, cols, RecordsHud::COL_DATE);
    }

    // LapLogHud: save columns as named keys
    void saveLapLogColumns(SettingsManager::HudSettings& settings, uint32_t cols) {
        using namespace Keys::LapLogCols;
        saveBitAsKey(settings, LAP, cols, LapLogHud::COL_LAP);
        saveBitAsKey(settings, S1, cols, LapLogHud::COL_S1);
        saveBitAsKey(settings, S2, cols, LapLogHud::COL_S2);
        saveBitAsKey(settings, S3, cols, LapLogHud::COL_S3);
        saveBitAsKey(settings, TIME, cols, LapLogHud::COL_TIME);
    }

    // LapLogHud: load columns from named keys
    void loadLapLogColumns(const SettingsManager::HudSettings& settings, uint32_t& cols) {
        using namespace Keys::LapLogCols;
        loadBitFromKey(settings, LAP, cols, LapLogHud::COL_LAP);
        loadBitFromKey(settings, S1, cols, LapLogHud::COL_S1);
        loadBitFromKey(settings, S2, cols, LapLogHud::COL_S2);
        loadBitFromKey(settings, S3, cols, LapLogHud::COL_S3);
        loadBitFromKey(settings, TIME, cols, LapLogHud::COL_TIME);
    }

    // IdealLapHud: save rows as named keys
    void saveIdealLapRows(SettingsManager::HudSettings& settings, uint32_t rows) {
        using namespace Keys::IdealLapRows;
        saveBitAsKey(settings, S1, rows, IdealLapHud::ROW_S1);
        saveBitAsKey(settings, S2, rows, IdealLapHud::ROW_S2);
        saveBitAsKey(settings, S3, rows, IdealLapHud::ROW_S3);
        saveBitAsKey(settings, LAST, rows, IdealLapHud::ROW_LAST);
        saveBitAsKey(settings, BEST, rows, IdealLapHud::ROW_BEST);
        saveBitAsKey(settings, IDEAL, rows, IdealLapHud::ROW_IDEAL);
    }

    // IdealLapHud: load rows from named keys
    void loadIdealLapRows(const SettingsManager::HudSettings& settings, uint32_t& rows) {
        using namespace Keys::IdealLapRows;
        loadBitFromKey(settings, S1, rows, IdealLapHud::ROW_S1);
        loadBitFromKey(settings, S2, rows, IdealLapHud::ROW_S2);
        loadBitFromKey(settings, S3, rows, IdealLapHud::ROW_S3);
        loadBitFromKey(settings, LAST, rows, IdealLapHud::ROW_LAST);
        loadBitFromKey(settings, BEST, rows, IdealLapHud::ROW_BEST);
        loadBitFromKey(settings, IDEAL, rows, IdealLapHud::ROW_IDEAL);
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
        saveBitAsKey(settings, SPEED, rows, SpeedWidget::ROW_SPEED);
        saveBitAsKey(settings, UNITS, rows, SpeedWidget::ROW_UNITS);
        saveBitAsKey(settings, GEAR, rows, SpeedWidget::ROW_GEAR);
    }

    // SpeedWidget: load rows from named keys
    void loadSpeedRows(const SettingsManager::HudSettings& settings, uint32_t& rows) {
        using namespace Keys::SpeedRows;
        loadBitFromKey(settings, SPEED, rows, SpeedWidget::ROW_SPEED);
        loadBitFromKey(settings, UNITS, rows, SpeedWidget::ROW_UNITS);
        loadBitFromKey(settings, GEAR, rows, SpeedWidget::ROW_GEAR);
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

    // BarsWidget: save columns as named keys
    void saveBarsColumns(SettingsManager::HudSettings& settings, uint32_t cols) {
        using namespace Keys::BarsCols;
        saveBitAsKey(settings, THROTTLE, cols, BarsWidget::COL_THROTTLE);
        saveBitAsKey(settings, BRAKE, cols, BarsWidget::COL_BRAKE);
        saveBitAsKey(settings, CLUTCH, cols, BarsWidget::COL_CLUTCH);
        saveBitAsKey(settings, RPM, cols, BarsWidget::COL_RPM);
        saveBitAsKey(settings, SUSPENSION, cols, BarsWidget::COL_SUSPENSION);
        saveBitAsKey(settings, FUEL, cols, BarsWidget::COL_FUEL);
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
    }

    // NoticesWidget: save notices as named keys
    void saveNotices(SettingsManager::HudSettings& settings, uint32_t notices) {
        using namespace Keys::Notices;
        saveBitAsKey(settings, WRONG_WAY, notices, NoticesWidget::NOTICE_WRONG_WAY);
        saveBitAsKey(settings, BLUE_FLAG, notices, NoticesWidget::NOTICE_BLUE_FLAG);
        saveBitAsKey(settings, LAST_LAP, notices, NoticesWidget::NOTICE_LAST_LAP);
        saveBitAsKey(settings, FINISHED, notices, NoticesWidget::NOTICE_FINISHED);
    }

    // NoticesWidget: load notices from named keys
    void loadNotices(const SettingsManager::HudSettings& settings, uint32_t& notices) {
        using namespace Keys::Notices;
        loadBitFromKey(settings, WRONG_WAY, notices, NoticesWidget::NOTICE_WRONG_WAY);
        loadBitFromKey(settings, BLUE_FLAG, notices, NoticesWidget::NOTICE_BLUE_FLAG);
        loadBitFromKey(settings, LAST_LAP, notices, NoticesWidget::NOTICE_LAST_LAP);
        loadBitFromKey(settings, FINISHED, notices, NoticesWidget::NOTICE_FINISHED);
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

    // TimingHud: save gap types as named keys
    void saveTimingGapTypes(SettingsManager::HudSettings& settings, uint8_t gaps) {
        using namespace Keys::TimingGaps;
        saveBitAsKey(settings, TO_PB, gaps, GAP_TO_PB);
        saveBitAsKey(settings, TO_IDEAL, gaps, GAP_TO_IDEAL);
        saveBitAsKey(settings, TO_OVERALL, gaps, GAP_TO_OVERALL);
        saveBitAsKey(settings, TO_ALLTIME, gaps, GAP_TO_ALLTIME);
    }

    // TimingHud: load gap types from named keys
    void loadTimingGapTypes(const SettingsManager::HudSettings& settings, uint8_t& gaps) {
        using namespace Keys::TimingGaps;
        uint32_t gaps32 = gaps;
        loadBitFromKey(settings, TO_PB, gaps32, GAP_TO_PB);
        loadBitFromKey(settings, TO_IDEAL, gaps32, GAP_TO_IDEAL);
        loadBitFromKey(settings, TO_OVERALL, gaps32, GAP_TO_OVERALL);
        loadBitFromKey(settings, TO_ALLTIME, gaps32, GAP_TO_ALLTIME);
        gaps = static_cast<uint8_t>(gaps32);
    }

    // Helper to apply base HUD settings from a map
    void applyBaseHudSettings(BaseHud& hud, const SettingsManager::HudSettings& settings) {
        using namespace Keys::Base;
        float pendingOffsetX = 0, pendingOffsetY = 0;
        bool hasOffsetX = false, hasOffsetY = false;

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
                }
            } catch (...) {
                DEBUG_WARN_F("Failed to parse base setting '%s=%s'", key.c_str(), value.c_str());
            }
        }
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

void SettingsManager::captureToProfile(const HudManager& hudManager, ProfileType profile) {
    if (profile >= ProfileType::COUNT) {
        DEBUG_WARN_F("captureToProfile called with invalid profile index: %d", static_cast<int>(profile));
        return;
    }

    ProfileCache& cache = m_profileCache[static_cast<size_t>(profile)];
    cache.clear();

    // Capture StandingsHud
    {
        using namespace Keys::Standings;
        HudSettings settings;
        const auto& hud = hudManager.getStandingsHud();
        captureBaseHudSettings(settings, hud);
        settings[DISPLAY_ROW_COUNT] = std::to_string(hud.m_displayRowCount);
        saveStandingsColumns(settings, hud.m_enabledColumns);  // Named keys instead of bitmask
        settings[OFFICIAL_GAP_MODE] = gapModeToString(hud.m_officialGapMode);
        settings[LIVE_GAP_MODE] = gapModeToString(hud.m_liveGapMode);
        settings[GAP_INDICATOR_MODE] = gapIndicatorModeToString(hud.m_gapIndicatorMode);
        settings[GAP_REFERENCE_MODE] = gapReferenceModeToString(hud.m_gapReferenceMode);
        cache["StandingsHud"] = std::move(settings);
    }

    // Capture MapHud
    {
        HudSettings settings;
        const auto& hud = hudManager.getMapHud();
        captureBaseHudSettings(settings, hud);
        settings["rotateToPlayer"] = std::to_string(hud.getRotateToPlayer() ? 1 : 0);
        settings["showOutline"] = std::to_string(hud.getShowOutline() ? 1 : 0);
        settings["riderColorMode"] = riderColorModeToString(hud.getRiderColorMode());
        settings["trackWidthScale"] = std::to_string(hud.getTrackWidthScale());
        settings["labelMode"] = labelModeToString(hud.getLabelMode());
        settings["riderShape"] = shapeIndexToFilename(hud.getRiderShape());
        settings["anchorPoint"] = anchorPointToString(hud.getAnchorPoint());
        settings["anchorX"] = std::to_string(hud.m_fAnchorX);
        settings["anchorY"] = std::to_string(hud.m_fAnchorY);
        settings["zoomEnabled"] = std::to_string(hud.getZoomEnabled() ? 1 : 0);
        settings["zoomDistance"] = std::to_string(hud.getZoomDistance());
        settings["markerScale"] = std::to_string(hud.getMarkerScale());
        cache["MapHud"] = std::move(settings);
    }

    // Capture RadarHud
    {
        HudSettings settings;
        const auto& hud = hudManager.getRadarHud();
        captureBaseHudSettings(settings, hud);
        settings["radarRange"] = std::to_string(hud.getRadarRange());
        settings["riderColorMode"] = radarRiderColorModeToString(hud.getRiderColorMode());
        settings["showPlayerArrow"] = std::to_string(hud.getShowPlayerArrow() ? 1 : 0);
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
        cache["PitboardHud"] = std::move(settings);
    }

    // Capture RecordsHud
    {
        HudSettings settings;
        const auto& hud = hudManager.getRecordsHud();
        captureBaseHudSettings(settings, hud);
        settings["provider"] = dataProviderToString(hud.m_provider);
        saveRecordsColumns(settings, hud.m_enabledColumns);  // Named keys instead of bitmask
        settings["recordsToShow"] = std::to_string(hud.m_recordsToShow);
        cache["RecordsHud"] = std::move(settings);
    }

    // Capture LapLogHud
    {
        HudSettings settings;
        const auto& hud = hudManager.getLapLogHud();
        captureBaseHudSettings(settings, hud);
        saveLapLogColumns(settings, hud.m_enabledColumns);  // Named keys instead of bitmask
        settings["maxDisplayLaps"] = std::to_string(hud.m_maxDisplayLaps);
        cache["LapLogHud"] = std::move(settings);
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
    // TimeWidget has showSessionType setting
    {
        HudSettings settings;
        const auto& hud = hudManager.getTimeWidget();
        captureBaseHudSettings(settings, hud);
        settings["showSessionType"] = std::to_string(hud.getShowSessionType() ? 1 : 0);
        cache["TimeWidget"] = std::move(settings);
    }
    captureWidget("SessionWidget", hudManager.getSessionWidget());
    captureWidget("SpeedoWidget", hudManager.getSpeedoWidget());
    captureWidget("TachoWidget", hudManager.getTachoWidget());
    // BarsWidget has enabledColumns
    {
        HudSettings settings;
        const auto& hud = hudManager.getBarsWidget();
        captureBaseHudSettings(settings, hud);
        saveBarsColumns(settings, hud.m_enabledColumns);
        cache["BarsWidget"] = std::move(settings);
    }
    captureWidget("VersionWidget", hudManager.getVersionWidget());
    // NoticesWidget has enabledNotices
    {
        HudSettings settings;
        const auto& hud = hudManager.getNoticesWidget();
        captureBaseHudSettings(settings, hud);
        saveNotices(settings, hud.m_enabledNotices);
        cache["NoticesWidget"] = std::move(settings);
    }
    captureWidget("SettingsButtonWidget", hudManager.getSettingsButtonWidget());
    captureWidget("PointerWidget", hudManager.getPointerWidget());
    captureWidget("RumbleHud", hudManager.getRumbleHud());

    // SpeedWidget has enabledRows - speedUnit is now global (in Preferences section)
    {
        HudSettings settings;
        const auto& hud = hudManager.getSpeedWidget();
        captureBaseHudSettings(settings, hud);
        saveSpeedRows(settings, hud.m_enabledRows);  // Named keys instead of bitmask
        cache["SpeedWidget"] = std::move(settings);
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

    // TimingHud has per-column modes and displayDuration
    {
        HudSettings settings;
        const auto& hud = hudManager.getTimingHud();
        captureBaseHudSettings(settings, hud);
        settings["labelMode"] = columnModeToString(hud.m_columnModes[TimingHud::COL_LABEL]);
        settings["timeMode"] = columnModeToString(hud.m_columnModes[TimingHud::COL_TIME]);
        settings["gapMode"] = columnModeToString(hud.m_columnModes[TimingHud::COL_GAP]);
        settings["displayDuration"] = std::to_string(hud.m_displayDurationMs);
        saveTimingGapTypes(settings, hud.getGapTypes());  // Named keys instead of bitmask
        cache["TimingHud"] = std::move(settings);
    }

    // GapBarHud with freeze, marker, and range settings
    {
        HudSettings settings;
        const auto& hud = hudManager.getGapBarHud();
        captureBaseHudSettings(settings, hud);
        settings["freezeDuration"] = std::to_string(hud.m_freezeDurationMs);
        settings["showMarkers"] = hud.m_showMarkers ? "1" : "0";
        settings["gapRange"] = std::to_string(hud.m_gapRangeMs);
        settings["barWidth"] = std::to_string(hud.m_barWidthPercent);
        cache["GapBarHud"] = std::move(settings);
    }

    // Note: ColorConfig is now global (not per-profile) - saved/loaded in saveSettings/loadSettings directly

    m_cacheInitialized = true;
}

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

    // Apply StandingsHud
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
                if (settings.count(OFFICIAL_GAP_MODE)) hud.m_officialGapMode = stringToGapMode(settings.at(OFFICIAL_GAP_MODE));
                if (settings.count(LIVE_GAP_MODE)) hud.m_liveGapMode = stringToGapMode(settings.at(LIVE_GAP_MODE));
                if (settings.count(GAP_INDICATOR_MODE)) hud.m_gapIndicatorMode = stringToGapIndicatorMode(settings.at(GAP_INDICATOR_MODE));
                if (settings.count(GAP_REFERENCE_MODE)) hud.m_gapReferenceMode = stringToGapReferenceMode(settings.at(GAP_REFERENCE_MODE));
            } catch (const std::exception& e) {
                DEBUG_WARN_F("StandingsHud: Failed to parse settings: %s", e.what());
            }
            hud.setDataDirty();
        }
    }

    // Apply MapHud
    {
        auto it = cache.find("MapHud");
        if (it != cache.end()) {
            auto& hud = hudManager.getMapHud();
            applyBaseHudSettings(hud, it->second);

            const auto& settings = it->second;
            try {
                if (settings.count("rotateToPlayer")) hud.setRotateToPlayer(std::stoi(settings.at("rotateToPlayer")) != 0);
                if (settings.count("showOutline")) hud.setShowOutline(std::stoi(settings.at("showOutline")) != 0);
                // riderColorMode - string enum
                if (settings.count("riderColorMode")) {
                    hud.setRiderColorMode(stringToRiderColorMode(settings.at("riderColorMode")));
                }
                // trackWidthScale
                if (settings.count("trackWidthScale")) {
                    hud.setTrackWidthScale(validateTrackWidthScale(std::stof(settings.at("trackWidthScale"))));
                }
                if (settings.count("labelMode")) hud.setLabelMode(stringToLabelMode(settings.at("labelMode")));
                if (settings.count("riderShape")) {
                    hud.setRiderShape(filenameToShapeIndex(settings.at("riderShape"), 1));
                }
                if (settings.count("anchorPoint")) hud.setAnchorPoint(stringToAnchorPoint(settings.at("anchorPoint")));
                if (settings.count("anchorX")) hud.m_fAnchorX = std::stof(settings.at("anchorX"));
                if (settings.count("anchorY")) hud.m_fAnchorY = std::stof(settings.at("anchorY"));
                if (settings.count("zoomEnabled")) hud.setZoomEnabled(std::stoi(settings.at("zoomEnabled")) != 0);
                if (settings.count("zoomDistance")) hud.setZoomDistance(validateZoomDistance(std::stof(settings.at("zoomDistance"))));
                if (settings.count("markerScale")) hud.setMarkerScale(std::stof(settings.at("markerScale")));
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
                if (settings.count("showPlayerArrow")) hud.setShowPlayerArrow(std::stoi(settings.at("showPlayerArrow")) != 0);
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
            } catch (const std::exception& e) {
                DEBUG_WARN_F("PitboardHud: Failed to parse settings: %s", e.what());
            }
            hud.setDataDirty();
        }
    }

    // Apply RecordsHud
    {
        auto it = cache.find("RecordsHud");
        if (it != cache.end()) {
            auto& hud = hudManager.getRecordsHud();
            applyBaseHudSettings(hud, it->second);

            const auto& settings = it->second;
            try {
                if (settings.count("provider")) {
                    hud.m_provider = stringToDataProvider(settings.at("provider"));
                }
                loadRecordsColumns(settings, hud.m_enabledColumns);  // Named keys instead of bitmask
                if (settings.count("recordsToShow")) {
                    int count = std::stoi(settings.at("recordsToShow"));
                    if (count >= 4 && count <= 30) hud.m_recordsToShow = count;
                }
            } catch (const std::exception& e) {
                DEBUG_WARN_F("RecordsHud: Failed to parse settings: %s", e.what());
            }
            hud.setDataDirty();
        }
    }

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
            } catch (const std::exception& e) {
                DEBUG_WARN_F("LapLogHud: Failed to parse settings: %s", e.what());
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
    // Apply TimeWidget with showSessionType setting
    {
        auto it = cache.find("TimeWidget");
        if (it != cache.end()) {
            auto& hud = hudManager.getTimeWidget();
            applyBaseHudSettings(hud, it->second);

            const auto& settings = it->second;
            try {
                if (settings.count("showSessionType")) {
                    hud.setShowSessionType(std::stoi(settings.at("showSessionType")) != 0);
                }
            } catch (const std::exception& e) {
                DEBUG_WARN_F("TimeWidget: Failed to parse settings: %s", e.what());
            }
            hud.setDataDirty();
        }
    }
    applyToHud("SessionWidget", hudManager.getSessionWidget());
    applyToHud("SpeedoWidget", hudManager.getSpeedoWidget());
    applyToHud("TachoWidget", hudManager.getTachoWidget());
    // Apply BarsWidget with enabledColumns
    {
        auto it = cache.find("BarsWidget");
        if (it != cache.end()) {
            auto& hud = hudManager.getBarsWidget();
            applyBaseHudSettings(hud, it->second);

            const auto& settings = it->second;
            try {
                loadBarsColumns(settings, hud.m_enabledColumns);
            } catch (const std::exception& e) {
                DEBUG_WARN_F("BarsWidget: Failed to parse settings: %s", e.what());
            }
            hud.setDataDirty();
        }
    }
    applyToHud("VersionWidget", hudManager.getVersionWidget());
    // Apply NoticesWidget with enabledNotices
    {
        auto it = cache.find("NoticesWidget");
        if (it != cache.end()) {
            auto& hud = hudManager.getNoticesWidget();
            applyBaseHudSettings(hud, it->second);

            const auto& settings = it->second;
            try {
                loadNotices(settings, hud.m_enabledNotices);
            } catch (const std::exception& e) {
                DEBUG_WARN_F("NoticesWidget: Failed to parse settings: %s", e.what());
            }
            hud.setDataDirty();
        }
    }
    applyToHud("SettingsButtonWidget", hudManager.getSettingsButtonWidget());
    applyToHud("PointerWidget", hudManager.getPointerWidget());
    applyToHud("RumbleHud", hudManager.getRumbleHud());

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

    // Apply TimingHud with per-column modes and displayDuration
    {
        auto it = cache.find("TimingHud");
        if (it != cache.end()) {
            auto& hud = hudManager.getTimingHud();
            applyBaseHudSettings(hud, it->second);

            const auto& settings = it->second;
            try {
                if (settings.count("labelMode")) {
                    hud.m_columnModes[TimingHud::COL_LABEL] = stringToColumnMode(settings.at("labelMode"));
                }
                if (settings.count("timeMode")) {
                    hud.m_columnModes[TimingHud::COL_TIME] = stringToColumnMode(settings.at("timeMode"));
                }
                if (settings.count("gapMode")) {
                    hud.m_columnModes[TimingHud::COL_GAP] = stringToColumnMode(settings.at("gapMode"));
                }
                if (settings.count("displayDuration")) {
                    int duration = std::stoi(settings.at("displayDuration"));
                    if (duration >= TimingHud::MIN_DURATION_MS && duration <= TimingHud::MAX_DURATION_MS) {
                        hud.m_displayDurationMs = duration;
                    }
                }
                loadTimingGapTypes(settings, hud.m_gapTypes);  // Named keys instead of bitmask
            } catch (const std::exception& e) {
                DEBUG_WARN_F("TimingHud: Failed to parse settings: %s", e.what());
            }
            hud.setDataDirty();
        }
    }

    // Apply GapBarHud with freeze, marker, and range settings
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
                // Try new key first, fall back to old key for compatibility
                if (settings.count("showMarkers")) {
                    hud.m_showMarkers = (settings.at("showMarkers") == "1");
                } else if (settings.count("showMarker")) {
                    hud.m_showMarkers = (settings.at("showMarker") == "1");
                }
                // Try new key first, fall back to old key for compatibility
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
            } catch (const std::exception& e) {
                DEBUG_WARN_F("GapBarHud: Failed to parse settings: %s", e.what());
            }
            hud.setDataDirty();
        }
    }

    // Note: ColorConfig is now global (not per-profile) - loaded once in loadSettings

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
            m_profileCache[i] = sourceCache;
        }
    }

    // Save to disk
    if (!m_savePath.empty()) {
        saveSettings(hudManager, m_savePath.c_str());
    }

    DEBUG_INFO_F("Applied %s profile settings to all profiles", ProfileManager::getProfileName(activeProfile));
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

    // Write Settings section (format versioning)
    file << "[Settings]\n";
    file << "version=" << SETTINGS_VERSION << "\n\n";

    // Write Profiles section
    const ProfileManager& profileManager = ProfileManager::getInstance();
    file << "[Profiles]\n";
    file << "activeProfile=" << static_cast<int>(profileManager.getActiveProfile()) << "\n";
    file << "autoSwitch=" << (profileManager.isAutoSwitchEnabled() ? 1 : 0) << "\n\n";

    // Write General section (global preferences)
    file << "[General]\n";
    file << "widgetsEnabled=" << (HudManager::getInstance().areWidgetsEnabled() ? 1 : 0) << "\n";
    file << "gridSnapping=" << (ColorConfig::getInstance().getGridSnapping() ? 1 : 0) << "\n";
    file << "checkForUpdates=" << (UpdateChecker::getInstance().isEnabled() ? 1 : 0) << "\n";
    file << "controller=" << XInputReader::getInstance().getRumbleConfig().controllerIndex << "\n";
    file << "speedUnit=" << speedUnitToString(hudManager.getSpeedWidget().m_speedUnit) << "\n";
    file << "fuelUnit=" << fuelUnitToString(hudManager.getFuelWidget().m_fuelUnit) << "\n\n";

    // Write Advanced section (power-user settings)
    file << "[Advanced]\n";
    file << "mapPixelSpacing=" << hudManager.getMapHud().getPixelSpacing() << "\n";
    file << "speedoNeedleColor=" << PluginUtils::formatColorHex(hudManager.getSpeedoWidget().getNeedleColor()) << "\n";
    file << "tachoNeedleColor=" << PluginUtils::formatColorHex(hudManager.getTachoWidget().getNeedleColor()) << "\n";
    file << "recordsShowFooter=" << (hudManager.getRecordsHud().m_bShowFooter ? 1 : 0) << "\n\n";

    // Write Colors section
    const ColorConfig& colorConfig = ColorConfig::getInstance();
    file << "[Colors]\n";
    file << "primary=" << PluginUtils::formatColorHex(colorConfig.getPrimary()) << "\n";
    file << "secondary=" << PluginUtils::formatColorHex(colorConfig.getSecondary()) << "\n";
    file << "tertiary=" << PluginUtils::formatColorHex(colorConfig.getTertiary()) << "\n";
    file << "muted=" << PluginUtils::formatColorHex(colorConfig.getMuted()) << "\n";
    file << "background=" << PluginUtils::formatColorHex(colorConfig.getBackground()) << "\n";
    file << "positive=" << PluginUtils::formatColorHex(colorConfig.getPositive()) << "\n";
    file << "warning=" << PluginUtils::formatColorHex(colorConfig.getWarning()) << "\n";
    file << "neutral=" << PluginUtils::formatColorHex(colorConfig.getNeutral()) << "\n";
    file << "negative=" << PluginUtils::formatColorHex(colorConfig.getNegative()) << "\n";
    file << "accent=" << PluginUtils::formatColorHex(colorConfig.getAccent()) << "\n\n";

    // Write Fonts section
    const FontConfig& fontConfig = FontConfig::getInstance();
    file << "[Fonts]\n";
    file << "title=" << fontConfig.getFontName(FontCategory::TITLE) << "\n";
    file << "normal=" << fontConfig.getFontName(FontCategory::NORMAL) << "\n";
    file << "strong=" << fontConfig.getFontName(FontCategory::STRONG) << "\n";
    file << "marker=" << fontConfig.getFontName(FontCategory::MARKER) << "\n";
    file << "small=" << fontConfig.getFontName(FontCategory::SMALL) << "\n\n";

    // Write Rumble section (effect configuration)
    const RumbleConfig& rumbleConfig = XInputReader::getInstance().getRumbleConfig();
    file << "[Rumble]\n";
    file << "enabled=" << (rumbleConfig.enabled ? 1 : 0) << "\n";
    file << "additive_blend=" << (rumbleConfig.additiveBlend ? 1 : 0) << "\n";
    file << "rumble_when_crashed=" << (rumbleConfig.rumbleWhenCrashed ? 1 : 0) << "\n";
    // Suspension effect
    file << "susp_min_input=" << rumbleConfig.suspensionEffect.minInput << "\n";
    file << "susp_max_input=" << rumbleConfig.suspensionEffect.maxInput << "\n";
    file << "susp_light_strength=" << rumbleConfig.suspensionEffect.lightStrength << "\n";
    file << "susp_heavy_strength=" << rumbleConfig.suspensionEffect.heavyStrength << "\n";
    // Wheelspin effect
    file << "wheel_min_input=" << rumbleConfig.wheelspinEffect.minInput << "\n";
    file << "wheel_max_input=" << rumbleConfig.wheelspinEffect.maxInput << "\n";
    file << "wheel_light_strength=" << rumbleConfig.wheelspinEffect.lightStrength << "\n";
    file << "wheel_heavy_strength=" << rumbleConfig.wheelspinEffect.heavyStrength << "\n";
    // Brake lockup effect
    file << "lockup_min_input=" << rumbleConfig.brakeLockupEffect.minInput << "\n";
    file << "lockup_max_input=" << rumbleConfig.brakeLockupEffect.maxInput << "\n";
    file << "lockup_light_strength=" << rumbleConfig.brakeLockupEffect.lightStrength << "\n";
    file << "lockup_heavy_strength=" << rumbleConfig.brakeLockupEffect.heavyStrength << "\n";
    // RPM effect
    file << "rpm_min_input=" << rumbleConfig.rpmEffect.minInput << "\n";
    file << "rpm_max_input=" << rumbleConfig.rpmEffect.maxInput << "\n";
    file << "rpm_light_strength=" << rumbleConfig.rpmEffect.lightStrength << "\n";
    file << "rpm_heavy_strength=" << rumbleConfig.rpmEffect.heavyStrength << "\n";
    // Slide effect
    file << "slide_min_input=" << rumbleConfig.slideEffect.minInput << "\n";
    file << "slide_max_input=" << rumbleConfig.slideEffect.maxInput << "\n";
    file << "slide_light_strength=" << rumbleConfig.slideEffect.lightStrength << "\n";
    file << "slide_heavy_strength=" << rumbleConfig.slideEffect.heavyStrength << "\n";
    // Surface effect
    file << "surface_min_input=" << rumbleConfig.surfaceEffect.minInput << "\n";
    file << "surface_max_input=" << rumbleConfig.surfaceEffect.maxInput << "\n";
    file << "surface_light_strength=" << rumbleConfig.surfaceEffect.lightStrength << "\n";
    file << "surface_heavy_strength=" << rumbleConfig.surfaceEffect.heavyStrength << "\n";
    // Steer effect
    file << "steer_min_input=" << rumbleConfig.steerEffect.minInput << "\n";
    file << "steer_max_input=" << rumbleConfig.steerEffect.maxInput << "\n";
    file << "steer_light_strength=" << rumbleConfig.steerEffect.lightStrength << "\n";
    file << "steer_heavy_strength=" << rumbleConfig.steerEffect.heavyStrength << "\n";
    // Wheelie effect
    file << "wheelie_min_input=" << rumbleConfig.wheelieEffect.minInput << "\n";
    file << "wheelie_max_input=" << rumbleConfig.wheelieEffect.maxInput << "\n";
    file << "wheelie_light_strength=" << rumbleConfig.wheelieEffect.lightStrength << "\n";
    file << "wheelie_heavy_strength=" << rumbleConfig.wheelieEffect.heavyStrength << "\n\n";

    // Write Hotkeys section
    const HotkeyManager& hotkeyMgr = HotkeyManager::getInstance();
    file << "[Hotkeys]\n";
    for (int i = 0; i < static_cast<int>(HotkeyAction::COUNT); ++i) {
        HotkeyAction action = static_cast<HotkeyAction>(i);
        const HotkeyBinding& binding = hotkeyMgr.getBinding(action);

        // Save keyboard binding
        file << "action" << i << "_key=" << static_cast<int>(binding.keyboard.keyCode) << "\n";
        file << "action" << i << "_mod=" << static_cast<int>(binding.keyboard.modifiers) << "\n";
        // Save controller binding
        file << "action" << i << "_btn=" << static_cast<int>(binding.controller) << "\n";
    }
    file << "\n";

    // Write TrackedRiders section
    file << "[TrackedRiders]\n";
    file << "data=" << TrackedRidersManager::getInstance().serializeToString() << "\n";
    file << "\n";

    // Write all profiles (HUDs and Widgets)
    static const std::array<const char*, 26> hudOrder = {
        "StandingsHud", "MapHud", "RadarHud", "PitboardHud", "RecordsHud",
        "LapLogHud", "IdealLapHud", "TelemetryHud", "PerformanceHud",
        "LapWidget", "PositionWidget", "TimeWidget", "SessionWidget", "SpeedWidget",
        "SpeedoWidget", "TachoWidget", "TimingHud", "GapBarHud", "BarsWidget", "VersionWidget",
        "NoticesWidget", "FuelWidget", "GamepadWidget", "SettingsButtonWidget", "PointerWidget", "RumbleHud"
    };

    for (int profileIdx = 0; profileIdx < static_cast<int>(ProfileType::COUNT); ++profileIdx) {
        ProfileType profile = static_cast<ProfileType>(profileIdx);
        const ProfileCache& cache = m_profileCache[profileIdx];

        file << "# Profile: " << ProfileManager::getProfileName(profile) << "\n";

        for (const char* hudName : hudOrder) {
            auto it = cache.find(hudName);
            if (it == cache.end()) continue;

            file << "[" << hudName << ":" << profileIdx << "]\n";

            // Write base properties first
            writeBaseHudSettings(file, it->second);

            // Write HUD-specific properties
            for (const auto& [key, value] : it->second) {
                // Skip base properties (already written)
                if (isBaseKey(key)) {
                    continue;
                }
                file << key << "=" << value << "\n";
            }
            file << "\n";
        }
    }

    // Write GamepadWidget per-variant layouts (not per-profile, global)
    // Only save layouts that actually exist (default: variants 1 and 2)
    {
        file << "# GamepadWidget Per-Variant Layouts\n";
        const auto& gamepadWidget = hudManager.getGamepadWidget();

        // Check which layouts exist and save them
        for (int variant = 1; variant <= 10; ++variant) {
            if (!gamepadWidget.hasLayout(variant)) continue;

            // Use non-const reference to get layout
            auto& mutableWidget = const_cast<GamepadWidget&>(gamepadWidget);
            const auto& layout = mutableWidget.getLayout(variant);

            file << "[GamepadWidget_Layout_" << variant << "]\n";
            file << "backgroundWidth=" << layout.backgroundWidth << "\n";
            file << "triggerWidth=" << layout.triggerWidth << "\n";
            file << "triggerHeight=" << layout.triggerHeight << "\n";
            file << "bumperWidth=" << layout.bumperWidth << "\n";
            file << "bumperHeight=" << layout.bumperHeight << "\n";
            file << "dpadWidth=" << layout.dpadWidth << "\n";
            file << "dpadHeight=" << layout.dpadHeight << "\n";
            file << "faceButtonSize=" << layout.faceButtonSize << "\n";
            file << "menuButtonWidth=" << layout.menuButtonWidth << "\n";
            file << "menuButtonHeight=" << layout.menuButtonHeight << "\n";
            file << "stickSize=" << layout.stickSize << "\n";
            file << "leftTriggerX=" << layout.leftTriggerX << "\n";
            file << "leftTriggerY=" << layout.leftTriggerY << "\n";
            file << "rightTriggerX=" << layout.rightTriggerX << "\n";
            file << "rightTriggerY=" << layout.rightTriggerY << "\n";
            file << "leftBumperX=" << layout.leftBumperX << "\n";
            file << "leftBumperY=" << layout.leftBumperY << "\n";
            file << "rightBumperX=" << layout.rightBumperX << "\n";
            file << "rightBumperY=" << layout.rightBumperY << "\n";
            file << "leftStickX=" << layout.leftStickX << "\n";
            file << "leftStickY=" << layout.leftStickY << "\n";
            file << "rightStickX=" << layout.rightStickX << "\n";
            file << "rightStickY=" << layout.rightStickY << "\n";
            file << "dpadX=" << layout.dpadX << "\n";
            file << "dpadY=" << layout.dpadY << "\n";
            file << "faceButtonsX=" << layout.faceButtonsX << "\n";
            file << "faceButtonsY=" << layout.faceButtonsY << "\n";
            file << "menuButtonsX=" << layout.menuButtonsX << "\n";
            file << "menuButtonsY=" << layout.menuButtonsY << "\n";
            file << "dpadSpacing=" << layout.dpadSpacing << "\n";
            file << "faceButtonSpacing=" << layout.faceButtonSpacing << "\n";
            file << "menuButtonSpacing=" << layout.menuButtonSpacing << "\n";
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
    // Use MoveFileExA for atomic replace (consistent with personal_best_manager.cpp)
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

        // Handle General section (global preferences)
        if (currentHudName == "General") {
            try {
                if (key == "widgetsEnabled") {
                    HudManager::getInstance().setWidgetsEnabled(std::stoi(value) != 0);
                } else if (key == "gridSnapping") {
                    ColorConfig::getInstance().setGridSnapping(std::stoi(value) != 0);
                } else if (key == "checkForUpdates") {
                    UpdateChecker::getInstance().setEnabled(std::stoi(value) != 0);
                } else if (key == "controller") {
                    int idx = std::stoi(value);
                    XInputReader::getInstance().getRumbleConfig().controllerIndex = idx;
                    XInputReader::getInstance().setControllerIndex(idx);
                } else if (key == "speedUnit") {
                    hudManager.getSpeedWidget().m_speedUnit = stringToSpeedUnit(value);
                } else if (key == "fuelUnit") {
                    hudManager.getFuelWidget().m_fuelUnit = stringToFuelUnit(value);
                }
            } catch (const std::exception& e) {
                DEBUG_WARN_F("General: Failed to parse settings: %s", e.what());
            }
            continue;
        }

        // Handle Advanced section (power-user settings)
        if (currentHudName == "Advanced") {
            try {
                if (key == "mapPixelSpacing") {
                    hudManager.getMapHud().setPixelSpacing(std::stof(value));
                } else if (key == "speedoNeedleColor") {
                    hudManager.getSpeedoWidget().setNeedleColor(PluginUtils::parseColorHex(value));
                } else if (key == "tachoNeedleColor") {
                    hudManager.getTachoWidget().setNeedleColor(PluginUtils::parseColorHex(value));
                } else if (key == "recordsShowFooter") {
                    hudManager.getRecordsHud().m_bShowFooter = (std::stoi(value) != 0);
                }
            } catch (const std::exception& e) {
                DEBUG_WARN_F("Advanced: Failed to parse settings: %s", e.what());
            }
            continue;
        }

        // Handle Colors section
        if (currentHudName == "Colors") {
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
            continue;
        }

        // Handle Fonts section
        if (currentHudName == "Fonts") {
            FontConfig& fontConfig = FontConfig::getInstance();
            if (key == "title") {
                fontConfig.setFont(FontCategory::TITLE, value);
            } else if (key == "normal") {
                fontConfig.setFont(FontCategory::NORMAL, value);
            } else if (key == "strong") {
                fontConfig.setFont(FontCategory::STRONG, value);
            } else if (key == "marker") {
                fontConfig.setFont(FontCategory::MARKER, value);
            } else if (key == "small") {
                fontConfig.setFont(FontCategory::SMALL, value);
            }
            continue;
        }

        // Handle Rumble section (effect configuration)
        if (currentHudName == "Rumble") {
            RumbleConfig& config = XInputReader::getInstance().getRumbleConfig();
            try {
                if (key == "enabled") {
                    config.enabled = std::stoi(value) != 0;
                } else if (key == "additive_blend") {
                    config.additiveBlend = std::stoi(value) != 0;
                } else if (key == "rumble_when_crashed") {
                    config.rumbleWhenCrashed = std::stoi(value) != 0;
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
            } catch (const std::exception& e) {
                DEBUG_WARN_F("Rumble: Failed to parse settings: %s", e.what());
            }
            continue;
        }

        // Handle Hotkeys section
        if (currentHudName == "Hotkeys") {
            HotkeyManager& hotkeyMgr = HotkeyManager::getInstance();
            try {
                // Parse action index from key name (e.g., "action0_key" -> 0)
                if (key.length() > 7 && key.substr(0, 6) == "action") {
                    size_t underscorePos = key.find('_', 6);
                    if (underscorePos != std::string::npos) {
                        int actionIdx = std::stoi(key.substr(6, underscorePos - 6));
                        if (actionIdx >= 0 && actionIdx < static_cast<int>(HotkeyAction::COUNT)) {
                            HotkeyAction action = static_cast<HotkeyAction>(actionIdx);
                            std::string suffix = key.substr(underscorePos + 1);

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
                    }
                }
            } catch (const std::exception& e) {
                DEBUG_WARN_F("Hotkeys: Failed to parse settings: %s", e.what());
            }
            continue;
        }

        // Handle TrackedRiders section
        if (currentHudName == "TrackedRiders") {
            try {
                if (key == "data") {
                    TrackedRidersManager::getInstance().deserializeFromString(value);
                }
            } catch (const std::exception& e) {
                DEBUG_WARN_F("TrackedRiders: Failed to parse settings: %s", e.what());
            }
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
                }
            } catch (const std::exception& e) {
                DEBUG_WARN_F("GamepadWidget Layout: Failed to parse settings: %s", e.what());
            }
            continue;
        }

        // Handle profile-specific HUD settings (only if version matches)
        // Skip loading HUD settings from old format - they use incompatible bitmask storage
        if (currentProfileIndex >= 0 && currentProfileIndex < static_cast<int>(ProfileType::COUNT)) {
            if (loadedVersion == SETTINGS_VERSION) {
                m_profileCache[currentProfileIndex][currentHudName][key] = value;
            }
            // Old format settings are silently ignored - users get fresh defaults
        }
    }

    file.close();

    // Check if we need to reset to defaults due to version mismatch
    if (loadedVersion != SETTINGS_VERSION) {
        DEBUG_INFO_F("Settings version mismatch (file: %d, current: %d) - resetting HUD settings to defaults",
                    loadedVersion, SETTINGS_VERSION);
        DEBUG_INFO("Note: Global settings (colors, fonts, hotkeys) are preserved");
        for (int i = 0; i < static_cast<int>(ProfileType::COUNT); ++i) {
            captureToProfile(hudManager, static_cast<ProfileType>(i));
        }
    }

    // If cache is empty (corrupted file), initialize all profiles with current defaults
    bool anyProfileEmpty = false;
    for (const auto& cache : m_profileCache) {
        if (cache.empty()) {
            anyProfileEmpty = true;
            break;
        }
    }
    if (anyProfileEmpty && loadedVersion == SETTINGS_VERSION) {
        DEBUG_INFO("Initializing profiles with defaults (empty cache despite valid version)");
        for (int i = 0; i < static_cast<int>(ProfileType::COUNT); ++i) {
            captureToProfile(hudManager, static_cast<ProfileType>(i));
        }
    }

    m_cacheInitialized = true;

    // Apply active profile to HUDs
    applyActiveProfile(hudManager);

    // Trigger update check on startup if enabled
    if (UpdateChecker::getInstance().isEnabled()) {
        DEBUG_INFO("Update check enabled, checking for updates on startup");
        UpdateChecker::getInstance().checkForUpdates();
    }

    DEBUG_INFO("Settings loaded successfully");
}
