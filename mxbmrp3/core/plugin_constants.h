// ============================================================================
// core/plugin_constants.h
// Named constants for all MX Bikes API numeric values
// ============================================================================
#pragma once

#include "plugin_utils.h"
#include "color_config.h"  // ColorPalette is the source of truth for basic colors

namespace PluginConstants {
    // Plugin identification
    constexpr const char* PLUGIN_NAME = "mxbmrp3";
    constexpr const char* PLUGIN_DISPLAY_NAME = "MXBMRP3";
    constexpr const char* PLUGIN_VERSION = "1.5.5.3";
    constexpr const char* PLUGIN_AUTHOR = "thomas4f";

    // MXBikes API constants
    constexpr const char* MOD_ID = "mxbikes";
    constexpr int MOD_DATA_VERSION = 8;
    constexpr int INTERFACE_VERSION = 9;

    // Telemetry settings
    constexpr int TELEMETRY_RATE_100HZ = 0;
    constexpr int TELEMETRY_RATE_50HZ = 1;
    constexpr int TELEMETRY_RATE_20HZ = 2;
    constexpr int TELEMETRY_RATE_10HZ = 3;
    constexpr int TELEMETRY_DISABLE = -1;

    // All HUD elements are positioned in normalized 16:9 space
    constexpr float UI_ASPECT_RATIO = 16.0f / 9.0f;

    // Font metrics for RobotoMono (monospace font)
    namespace FontMetrics {
        constexpr float MONOSPACE_CHAR_WIDTH_RATIO = 0.275f;
    }

    // Font indices
    namespace Fonts {
        constexpr int ENTER_SANSMAN = 1;
        constexpr int ROBOTO_MONO = 2;
        constexpr int ROBOTO_MONO_BOLD = 3;
        constexpr int FUZZY_BUBBLES = 4;
        constexpr int TINY5 = 5;

        constexpr int NORMAL_FONT = ROBOTO_MONO;

        // CHAR_WIDTH = 0.0200f * 0.275f (FontSizes::NORMAL * FontMetrics::MONOSPACE_CHAR_WIDTH_RATIO)
        constexpr float CHAR_WIDTH = 0.0055f;
    }

    // Standard font sizes
    namespace FontSizes {
        constexpr float EXTRA_SMALL = 0.0125f;
        constexpr float SMALL = 0.0150f;
        constexpr float NORMAL = 0.0200f;
        constexpr float LARGE = 0.0300f;
        constexpr float EXTRA_LARGE = 0.0400f;
    }

    // Standard line heights
    namespace LineHeights {
        constexpr float EXTRA_SMALL = 0.0139f;          // 0.625x normal line height
        constexpr float SMALL = 0.0167f;                // 0.75x normal line height
        constexpr float NORMAL = 0.0222f;               // 1x line height
        constexpr float LARGE = 0.0444f;                // 2x normal line height
        constexpr float EXTRA_LARGE = 0.0444f;          // 2x normal line height (same as LARGE, for 0.04 font)
    }

    // HUD positioning grid for consistent alignment
    namespace HudGrid {
        constexpr float GRID_SIZE_HORIZONTAL = FontSizes::NORMAL * FontMetrics::MONOSPACE_CHAR_WIDTH_RATIO;
        constexpr float GRID_SIZE_VERTICAL = 0.5f * LineHeights::NORMAL;

        constexpr int roundToInt(float x) {
            return static_cast<int>(x + (x >= 0.0f ? 0.5f : -0.5f));
        }

        constexpr float SNAP_TO_GRID_X(float pos) {
            return static_cast<float>(roundToInt(pos / GRID_SIZE_HORIZONTAL)) * GRID_SIZE_HORIZONTAL;
        }

        constexpr float SNAP_TO_GRID_Y(float pos) {
            return static_cast<float>(roundToInt(pos / GRID_SIZE_VERTICAL)) * GRID_SIZE_VERTICAL;
        }
    }

    // Padding values
    namespace Padding {
        constexpr float NONE = 0.0000f;
        constexpr float HUD_VERTICAL = LineHeights::NORMAL;
        constexpr float HUD_HORIZONTAL = 2 * HudGrid::GRID_SIZE_HORIZONTAL;
    }

    // HUD element spacing patterns (in grid units)
    // Used with ScaledDimensions::gridH() and gridV() for consistent element spacing
    namespace HudSpacing {
        // Background padding scale factor for styled strings
        // Used as: dim.gridH(1) * BG_PADDING_H_SCALE
        constexpr float BG_PADDING_H_SCALE = 0.5f;  // 0.5 char widths left/right

        // Horizontal element spacing (in grid units)
        // Used as: dim.gridH(ELEMENT_TOUCHING_H) or dim.gridH(ELEMENT_SEPARATED_H)
        constexpr float ELEMENT_TOUCHING_H = 3.0f;    // Elements touch horizontally (2 chars text + 1 char padding)
        constexpr float ELEMENT_SEPARATED_H = 3.5f;   // Elements have small gap horizontally (+ 0.5 char gap)

        // Vertical spacing (in grid units)
        // Used as: dim.gridV(ROW_GAP) or dim.gridV(SECTION_GAP)
        constexpr float ROW_GAP = 0.5f;               // Small gap between rows (half-line-height)
        constexpr float SECTION_GAP = 2.0f;           // Larger gap between sections (full-line-height)
    }

    // Brand Colors
    namespace BrandColors {
        constexpr unsigned long KTM = PluginUtils::makeColor(255, 102, 0);        // #ff6600 - KTM Orange
        constexpr unsigned long HONDA = PluginUtils::makeColor(222, 28, 33);      // #de1c21 - Honda Red
        constexpr unsigned long HUSQVARNA = PluginUtils::makeColor(39, 58, 96);   // #273a60 - Husqvarna Blue
        constexpr unsigned long KAWASAKI = PluginUtils::makeColor(102, 204, 51);  // #66cc33 - Kawasaki Green
        constexpr unsigned long GASGAS = PluginUtils::makeColor(203, 13, 37);     // #cb0d25 - GasGas Red
        constexpr unsigned long YAMAHA = PluginUtils::makeColor(27, 62, 144);     // #1b3e90 - Yamaha Blue
        constexpr unsigned long FANTIC = PluginUtils::makeColor(228, 3, 44);      // #e4032c - Fantic Red
        constexpr unsigned long TRIUMPH = PluginUtils::makeColor(42, 42, 42);     // #2a2a2a - Triumph Black
        constexpr unsigned long TM = PluginUtils::makeColor(0, 175, 241);         // #00aff1 - TM Blue
        constexpr unsigned long SUZUKI = PluginUtils::makeColor(254, 242, 0);     // #fef200 - Suzuki Yellow
        constexpr unsigned long BETA = PluginUtils::makeColor(210, 20, 20);       // #d21414 - Beta Red
        constexpr unsigned long STARK = PluginUtils::makeColor(100, 100, 100);    // #646464 - Stark Gray
        constexpr unsigned long ALTA = PluginUtils::makeColor(200, 200, 200);     // #c8c8c8 - Alta Light Gray
        constexpr unsigned long DEFAULT = PluginUtils::makeColor(128, 128, 128);  // #808080 - Default Gray
    }

    // Podium Colors
    namespace PodiumColors {
        constexpr unsigned long GOLD = PluginUtils::makeColor(255, 215, 0);      // #ffd700 - Gold
        constexpr unsigned long SILVER = PluginUtils::makeColor(192, 192, 192);  // #c0c0c0 - Silver
        constexpr unsigned long BRONZE = PluginUtils::makeColor(205, 127, 50);   // #cd7f32 - Bronze
    }

    // Semantic color aliases for input controls (used in input visualizer)
    // Basic colors are defined in ColorPalette (color_config.h)
    namespace SemanticColors {
        constexpr unsigned long THROTTLE = ColorPalette::GREEN;
        constexpr unsigned long FRONT_BRAKE = ColorPalette::RED;                  // Bright red (255, 0, 0)
        constexpr unsigned long REAR_BRAKE = PluginUtils::makeColor(180, 0, 0);   // Dark red
        constexpr unsigned long CLUTCH = ColorPalette::BLUE;
        constexpr unsigned long FRONT_SUSP = ColorPalette::PURPLE;                // Bright purple (200, 0, 255)
        constexpr unsigned long REAR_SUSP = PluginUtils::makeColor(120, 0, 200);  // Dark purple
        constexpr unsigned long GEAR = ColorPalette::ORANGE;                      // Orange for gear indicator
        constexpr unsigned long STICK_L = ColorPalette::BLUE;   // Left stick (bike control)
        constexpr unsigned long STICK_R = ColorPalette::GREEN;  // Right stick (rider lean)
    }

    // Mathematical constants
    namespace Math {
        constexpr float PI = 3.14159265f;
        constexpr float DEG_TO_RAD = PI / 180.0f;
        constexpr float RAD_TO_DEG = 180.0f / PI;
    }

    // Unit conversion constants
    namespace UnitConversion {
        // Speed conversions
        constexpr float MS_TO_KMH = 3.6f;       // meters/second to kilometers/hour
        constexpr float MS_TO_MPH = 2.23694f;   // meters/second to miles/hour

        // Volume conversions
        constexpr float LITERS_TO_GALLONS = 0.264172f;  // liters to US gallons

        // Temperature conversions
        constexpr float CELSIUS_TO_FAHRENHEIT_MULT = 9.0f / 5.0f;
        constexpr float CELSIUS_TO_FAHRENHEIT_OFFSET = 32.0f;
    }

    // Time conversion constants
    namespace TimeConversion {
        constexpr int MS_PER_SECOND = 1000;
        constexpr int MS_PER_MINUTE = 60000;
        constexpr int US_PER_SECOND = 1000000;
    }

    // XInput hardware limits for normalization
    namespace XInputLimits {
        constexpr float STICK_NEGATIVE_MAX = 32768.0f;  // Left stick max negative value
        constexpr float STICK_POSITIVE_MAX = 32767.0f;  // Left stick max positive value
        constexpr float TRIGGER_MAX = 255.0f;           // Trigger max value
    }

    // Display formatting placeholders
    namespace Placeholders {
        constexpr const char* LAP_TIME = "-:--.---";   // For lap times in M:SS.mmm format
        constexpr const char* GENERIC = "-";           // For gaps, diffs, or missing data
    }

    // Text justification
    namespace Justify {
        constexpr int LEFT = 0;
        constexpr int CENTER = 1;
        constexpr int RIGHT = 2;
    }

    // Game limits from MX Bikes
    namespace GameLimits {
        constexpr int MAX_CONNECTIONS = 50;  // Maximum server connections (riders in race)
        constexpr size_t RACE_ENTRY_NAME_MAX = 31;  // RaceAddEntry truncates names to 31 chars (EventInit allows 100)
    }

    // Track segment types (from MX Bikes API SPluginsTrackSegment_t)
    namespace TrackSegmentType {
        constexpr int STRAIGHT = 0;
        constexpr int CURVED = 1;  // Non-zero = curved segment
    }

    // Communication types (from MX Bikes API SPluginsRaceCommunication_t)
    namespace CommunicationType {
        constexpr int STATE_CHANGE = 1;  // Rider state changes (DNF, pit, etc.)
        constexpr int PENALTY = 2;       // Penalty notifications
    }

    // Gear values (from bike telemetry)
    namespace GearValue {
        constexpr int NEUTRAL = 0;  // Neutral gear
        // 1-6 are normal gear numbers
    }

    // Podium positions
    namespace Position {
        constexpr int FIRST = 1;
        constexpr int SECOND = 2;
        constexpr int THIRD = 3;
    }

    // View state values (from MX Bikes API Draw callback)
    // Renamed from DrawState to ViewState to avoid Windows API conflict with DrawState/DrawStateW
    namespace ViewState {
        constexpr int ON_TRACK = 0;   // Player is on track
        constexpr int SPECTATE = 1;   // Player is spectating
        constexpr int REPLAY = 2;     // Viewing replay
    }

    // Session state flags (bitwise flags from MXBikes API)
    namespace SessionState {
        constexpr int IN_PROGRESS = 16;
        constexpr int FINISHED = 32;
        constexpr int SIGHTING_LAP = 64;
        constexpr int PRE_START = 256;
        constexpr int RACE_OVER = 512;
        constexpr int CANCELLED = 2048;
    }

    // Rider state values (from MXBikes API)
    namespace RiderState {
        constexpr int NORMAL = 0;
        constexpr int DNS = 1;
        constexpr int UNKNOWN = 2;
        constexpr int RETIRED = 3;
        constexpr int DSQ = 4;
    }

    // Event type values (from MXBikes API)
    namespace EventType {
        constexpr int TESTING = 1;  // Shows as "Open Practice" when online
        constexpr int RACE = 2;
        constexpr int STRAIGHT_RHYTHM = 4;
    }

    // Session values (from MXBikes API)
    namespace Session {
        constexpr int WAITING = 0;
        constexpr int PRACTICE = 1;
        constexpr int PRE_QUALIFY = 2;
        constexpr int QUALIFY_PRACTICE = 3;
        constexpr int QUALIFY = 4;
        constexpr int WARMUP = 5;
        constexpr int RACE_1 = 6;
        constexpr int RACE_2 = 7;

        // Straight Rhythm specific
        constexpr int SR_ROUND = 2;
        constexpr int SR_QUARTER_FINALS = 3;
        constexpr int SR_SEMI_FINALS = 4;
        constexpr int SR_FINAL = 5;
    }

    // Weather conditions (from MXBikes API)
    namespace Conditions {
        constexpr int CLEAR = 0;
        constexpr int CLOUDY = 1;
        constexpr int RAINY = 2;
    }

    // Penalty reason values (from MXBikes API)
    namespace Reason {
        constexpr int JUMP_START = 0;
        constexpr int TOO_MANY_OFFENCES = 1;
        constexpr int DIRECTOR = 2;
    }

    // Offence type values (from MXBikes API)
    namespace Offence {
        constexpr int JUMP_START = 1;
        constexpr int CUTTING = 2;
    }

    // HUD display limits
    namespace HudLimits {
        constexpr int MAX_LAP_LOG_CAPACITY = 30;  // Maximum laps stored per rider in lap log
        // Note: HUD-specific limits (MAX_STANDINGS_ENTRIES, FRAME_HISTORY_SIZE, GRAPH_HISTORY_SIZE)
        // have been relocated to their respective HUD/handler files for better code locality
    }

    // Standard HUD dimensions (in character counts)
    namespace HudDimensions {
        // Standard HUD widths
        constexpr int STANDARD_HUD_WIDTH = 40;      // Full width for HUDs with stats/values
        // Note: GRAPH_ONLY_WIDTH relocated to performance_hud.h (HUD-specific)
        // Note: All SETTINGS_* constants relocated to settings_hud.h (settings-specific)
    }

    // Settings Validation Ranges
    namespace SettingsLimits {
        // HUD scale limits
        constexpr float MIN_SCALE = 0.1f;
        constexpr float MAX_SCALE = 10.0f;
        constexpr float DEFAULT_SCALE = 1.0f;

        // Background opacity limits (0.0 = fully transparent, 1.0 = fully opaque)
        constexpr float MIN_OPACITY = 0.0f;
        constexpr float MAX_OPACITY = 1.0f;
        constexpr float DEFAULT_OPACITY = 0.8f;  // Used by most HUDs (was 0.85f, corrected to match actual usage)

        // Position offset limits (normalized coordinates)
        constexpr float MIN_OFFSET = -2.0f;  // Allow off-screen for ultrawide/multi-monitor
        constexpr float MAX_OFFSET = 2.0f;
        constexpr float DEFAULT_OFFSET = 0.0f;

        // Display row/lap count limits
        constexpr int MIN_DISPLAY_ROWS = 1;
        constexpr int MAX_DISPLAY_ROWS = 100;
        constexpr int MIN_DISPLAY_LAPS = 1;
        constexpr int MAX_DISPLAY_LAPS = 30;

        // Note: Map-specific limits (MIN/MAX/DEFAULT_TRACK_LINE_WIDTH) relocated to map_hud.h
        // Migration: PluginConstants::SettingsLimits::*_TRACK_LINE_WIDTH → MapHud::*_TRACK_LINE_WIDTH
    }

    // Note: GAP_UPDATE_THRESHOLD_MS relocated to PluginData class (private static constexpr)
    // Migration: PluginConstants::PerformanceThresholds::GAP_UPDATE_THRESHOLD_MS → PluginData::GAP_UPDATE_THRESHOLD_MS

    // Display strings for UI - all user-visible text strings centralized here
    namespace DisplayStrings {
        // Event type display strings (corresponds to EventType enum)
        namespace EventType {
            constexpr const char* TESTING = "Testing";
            constexpr const char* RACE = "Race";
            constexpr const char* STRAIGHT_RHYTHM = "Straight Rhythm";
            constexpr const char* UNKNOWN = "Unknown";
        }

        // Session display strings (corresponds to Session enum)
        namespace Session {
            constexpr const char* WAITING = "Waiting";
            constexpr const char* PRACTICE = "Practice";
            constexpr const char* PRE_QUALIFY = "Pre-Qualify";
            constexpr const char* QUALIFY_PRACTICE = "Qualify Practice";
            constexpr const char* QUALIFY = "Qualify";
            constexpr const char* WARMUP = "Warmup";
            constexpr const char* RACE_1 = "Race 1";
            constexpr const char* RACE_2 = "Race 2";
            constexpr const char* SR_ROUND = "Round";
            constexpr const char* SR_QUARTER_FINALS = "Quarter-Finals";
            constexpr const char* SR_SEMI_FINALS = "Semi-Finals";
            constexpr const char* SR_FINAL = "Final";
            constexpr const char* UNKNOWN = "Unknown";
        }

        // Session state display strings (corresponds to SessionState bitflags)
        namespace SessionState {
            constexpr const char* CANCELLED = "Cancelled";
            constexpr const char* RACE_OVER = "Race Over";
            constexpr const char* PRE_START = "Pre-Start";
            constexpr const char* SIGHTING_LAP = "Sighting Lap";
            constexpr const char* START_SEQUENCE = "Start Sequence";
            constexpr const char* COMPLETE = "Complete";
            constexpr const char* IN_PROGRESS = "In Progress";
            constexpr const char* WAITING = "Waiting";
        }

        // Rider state abbreviations (corresponds to RiderState enum)
        namespace RiderState {
            constexpr const char* DNS = "DNS";  // Did not start
            constexpr const char* UNKNOWN = "UNK";  // Unknown state
            constexpr const char* RETIRED = "RET";  // Retired from race
            constexpr const char* DISQUALIFIED = "DSQ";  // Disqualified
        }

        // Race status abbreviations (not from enum - calculated from session data)
        namespace RaceStatus {
            constexpr const char* FINISHED = "FIN";  // Rider finished race
            constexpr const char* IN_PIT = "PIT";  // Rider in pit
            constexpr const char* LAST_LAP = "LL";  // Rider on last lap
        }

        // Weather conditions (corresponds to Conditions enum)
        namespace Conditions {
            constexpr const char* CLEAR = "Clear";
            constexpr const char* CLOUDY = "Cloudy";
            constexpr const char* RAINY = "Rainy";
            constexpr const char* UNKNOWN = "Unknown";
        }

        // Communication type strings (corresponds to CommunicationType enum)
        namespace CommunicationType {
            constexpr const char* STATE_CHANGE = "State Change";
            constexpr const char* PENALTY = "Penalty";
            constexpr const char* UNKNOWN = "Unknown";
        }

        // Penalty/DSQ reason strings (corresponds to Reason enum)
        namespace Reason {
            constexpr const char* JUMP_START = "Jump start";
            constexpr const char* TOO_MANY_OFFENCES = "Too many offences";
            constexpr const char* DIRECTOR = "Director";
            constexpr const char* NONE = "None";
        }

        // Offence type strings (corresponds to Offence enum)
        namespace Offence {
            constexpr const char* JUMP_START = "Jump start";
            constexpr const char* CUTTING = "Cutting";
            constexpr const char* NONE = "None";
        }
    }

    // Sprite indices for rendering
    // Note: Sprite indices are 1-based (0 = solid color fill)
    // Indices 1-2 are existing sprites, 3+ are background textures
    namespace SpriteIndex {
        constexpr int SOLID_COLOR = 0;
        constexpr int POINTER = 1;
        constexpr int GEAR_CIRCLE = 2;

        // Background texture sprites (optional, loaded from mxbmrp3_data/*.tga)
        // Each HUD/widget can have an optional background texture
        // Files must be named: mxbmrp3_data/{name}.tga (e.g., standings_hud.tga)
        constexpr int BG_STANDINGS_HUD = 3;
        constexpr int BG_MAP_HUD = 4;
        constexpr int BG_LAP_LOG_HUD = 5;
        constexpr int BG_SESSION_BEST_HUD = 6;
        constexpr int BG_TELEMETRY_HUD = 7;
        constexpr int BG_INPUT_HUD = 8;
        constexpr int BG_PERFORMANCE_HUD = 9;
        constexpr int BG_LAP_WIDGET = 10;
        constexpr int BG_POSITION_WIDGET = 11;
        constexpr int BG_TIME_WIDGET = 12;
        constexpr int BG_SESSION_WIDGET = 13;
        constexpr int BG_SPEED_WIDGET = 14;
        constexpr int BG_TIMING_WIDGET = 15;
        constexpr int BG_BARS_WIDGET = 16;
        constexpr int BG_PITBOARD_HUD = 17;
        constexpr int SPEEDO_DIAL = 18;
        constexpr int TACHO_DIAL = 19;
        constexpr int BG_RADAR_HUD = 20;
        constexpr int RADAR_SECTOR = 21;  // Proximity sector highlight sprite
        constexpr int BG_FUEL_WIDGET = 22;
        constexpr int BG_RECORDS_HUD = 23;
    }
}
