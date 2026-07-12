// ============================================================================
// core/plugin_constants.h
// Named constants for all MX Bikes API numeric values
// ============================================================================
#pragma once

#include "plugin_utils.h"
#include "color_config.h"  // ColorPalette is the source of truth for basic colors
#include "font_config.h"   // For dynamic font category selection
#include "asset_manager.h" // For dynamic font index lookup by name

namespace PluginConstants {
    // Plugin identification
    constexpr const char* PLUGIN_NAME = "mxbmrp3";
    constexpr const char* PLUGIN_DISPLAY_NAME = "MXBMRP3";
    // Defined in plugin_version.cpp from resource.h's VER_STRING (VER_MAJOR.MINOR.PATCH
    // + the auto-stamped build number), so the runtime version and the DLL FILEVERSION
    // can never drift. Declared here as an extern — NOT a constexpr including resource.h —
    // so the per-build version bump recompiles only plugin_version.cpp, not every TU that
    // includes this header (resource.h is deliberately kept out of the wide include graph).
    extern const char* const PLUGIN_VERSION;
    constexpr const char* PLUGIN_AUTHOR = "thomas4f";

    // GitHub repository for updates (centralized to support repo moves/renames)
    constexpr const char* GITHUB_REPO_OWNER = "thomas4f";
    constexpr const char* GITHUB_REPO_NAME = "mxbmrp3";

    // --- Analytics: two secret keys injected at BUILD time + one PUBLIC site code ---
    // APTABASE_KEY and GOATCOUNTER_TOKEN come from environment variables read by
    // mxbmrp3.vcxproj at build time (-> the MXBMRP3_* preprocessor defines used here).
    // With an env var unset the matching define is absent and the constant falls back
    // to its placeholder, so a plain developer build sends nothing. Set those two (as
    // repo Actions Secrets) only for an official release. The GoatCounter *code* is
    // public (it's the site subdomain, in every hit's URL), so it's hardcoded below
    // rather than injected. Rotate the GoatCounter token if it was ever committed.
    #define MXBMRP3_STRINGIFY2(x) #x
    #define MXBMRP3_STRINGIFY(x) MXBMRP3_STRINGIFY2(x)

    // Anonymous usage analytics (Aptabase). Privacy-friendly, opt-out (toggle in
    // Settings > General > Integrations). A single "app_started" beacon is sent
    // per launch carrying only a random install UUID + plugin/game version and
    // enabled-feature flags — no personal data. While the key equals the
    // placeholder, no network request is ever made.
    constexpr const char* ANALYTICS_APP_KEY_PLACEHOLDER = "A-US-0000000000";
#ifdef MXBMRP3_APTABASE_KEY
    constexpr const char* ANALYTICS_APP_KEY = MXBMRP3_STRINGIFY(MXBMRP3_APTABASE_KEY);
#else
    constexpr const char* ANALYTICS_APP_KEY = ANALYTICS_APP_KEY_PLACEHOLDER;
#endif

    // GoatCounter (privacy-first, page-path analytics) runs alongside Aptabase as
    // an uncapped headcount + version backstop: one hit per launch, ~100k/month
    // soft quota, so the active-user count survives even if Aptabase's 20k/month
    // cap is reached. The code is the goatcounter.com site subdomain
    // (mxbmrp3 -> mxbmrp3.goatcounter.com). Unlike the token this is PUBLIC — it
    // rides in every hit's URL — so it's hardcoded, not injected. GOATCOUNTER_TOKEN
    // alone gates whether a hit is actually sent (a placeholder token = no hit), so a
    // dev build still sends nothing. (The #ifdef stays so a build can still override
    // the site code via env var if ever needed.)
    constexpr const char* GOATCOUNTER_CODE_PLACEHOLDER = "your-code";  // kept for the enabled-check
#ifdef MXBMRP3_GOATCOUNTER_CODE
    constexpr const char* GOATCOUNTER_CODE = MXBMRP3_STRINGIFY(MXBMRP3_GOATCOUNTER_CODE);
#else
    constexpr const char* GOATCOUNTER_CODE = "mxbmrp3";
#endif

    // GoatCounter API token ("Count" permission) for the backend /api/v0/count
    // endpoint. The public /count pixel bot-filters non-browser senders, so the
    // authenticated API is the supported way to send from an app. A leaked
    // count-only token can only inject pageviews into the GoatCounter site.
    constexpr const char* GOATCOUNTER_TOKEN_PLACEHOLDER = "your-token";
#ifdef MXBMRP3_GOATCOUNTER_TOKEN
    constexpr const char* GOATCOUNTER_TOKEN = MXBMRP3_STRINGIFY(MXBMRP3_GOATCOUNTER_TOKEN);
#else
    constexpr const char* GOATCOUNTER_TOKEN = GOATCOUNTER_TOKEN_PLACEHOLDER;
#endif

    #undef MXBMRP3_STRINGIFY
    #undef MXBMRP3_STRINGIFY2

    // Remote analytics sampling config (public, unauthenticated). Aptabase bills per
    // event, so this is the developer's cost lever: the client fetches this small JSON
    // file (ONLY if analytics is enabled) and reads `aptabase_full_sample` ∈ [0,1] — the
    // fraction of launches that send the FULL set (session_end + custom) on top of the
    // always-sent app_started (+ crash). It lets the volume be dialed down WITHOUT a
    // release. Reduce-only + fail-open to full (see analytics_remote_config.h). Hosted at
    // the default branch root of the PUBLIC release repo, so only repo write-access can
    // flip it and it's fetchable without auth. Path built from the repo constants above.
    // NOTE: confirm the branch matches the public repo's default (main vs master).
    constexpr const char* ANALYTICS_CONFIG_HOST = "raw.githubusercontent.com";
    constexpr const char* ANALYTICS_CONFIG_BRANCH = "main";
    constexpr const char* ANALYTICS_CONFIG_FILE = "analytics_config.json";

    // Build guard: a RELEASE build (NDEBUG) must have the two secret analytics keys
    // injected, so an official .dlo can never be shipped with analytics silently
    // disabled because someone forgot to set the env vars. (GOATCOUNTER_CODE is not
    // checked — it's hardcoded/public above.) Debug/dev builds are exempt (they build
    // fine with placeholders and send nothing). To intentionally build a keyless
    // Release, define MXBMRP3_ALLOW_NO_ANALYTICS.
#if defined(NDEBUG) && !defined(MXBMRP3_ALLOW_NO_ANALYTICS)
  #if !defined(MXBMRP3_APTABASE_KEY) || !defined(MXBMRP3_GOATCOUNTER_TOKEN)
    #error "Release build is missing analytics keys. Set the APTABASE_KEY and GOATCOUNTER_TOKEN environment variables (see mxbmrp3.vcxproj), or define MXBMRP3_ALLOW_NO_ANALYTICS to build a keyless release on purpose. (GOATCOUNTER_CODE is hardcoded — it's public.)"
  #endif
#endif

    // MXBikes API constants
    constexpr const char* MOD_ID = "mxbikes";
    constexpr int MOD_DATA_VERSION = 8;
    constexpr int INTERFACE_VERSION = 9;

    // How long the corner status buttons (settings / director) flash into view on an
    // event (entering the track, toggling a mode) before returning to their normal
    // mouse-idle auto-hide. Milliseconds.
    constexpr int WIDGET_REVEAL_MS = 4000;
    // A gap between Draw calls longer than this means the game stopped drawing us
    // (menu / loading / alt-tab); when drawing resumes we treat it as "entered the
    // track" and reveal the corner buttons. Milliseconds.
    constexpr int ENTER_TRACK_GAP_MS = 1000;

    // Telemetry settings
    constexpr int TELEMETRY_RATE_100HZ = 0;
    constexpr int TELEMETRY_RATE_50HZ = 1;
    constexpr int TELEMETRY_RATE_20HZ = 2;
    constexpr int TELEMETRY_RATE_10HZ = 3;
    constexpr int TELEMETRY_DISABLE = -1;

    // All HUD elements are positioned in normalized 16:9 space
    constexpr float UI_ASPECT_RATIO = 16.0f / 9.0f;

    // Icon glyphs fill their box more than text fills the em, so HUD title/identity
    // icons are drawn a bit smaller than the title font (~0.63x). Single source of truth
    // shared by BaseHud's title icons and the settings/camera button glyphs so they match.
    constexpr float TITLE_ICON_SCALE = 0.63f;

    // Font metrics for RobotoMono (monospace font)
    namespace FontMetrics {
        constexpr float MONOSPACE_CHAR_WIDTH_RATIO = 0.275f;
    }

    // Font accessors - all use dynamic lookup by name (safe regardless of discovery order)
    namespace Fonts {
        // Direct font lookups by name (use these for specific fonts)
        inline int ENTER_SANSMAN() { return AssetManager::getInstance().getFontIndexByName("EnterSansman-Italic"); }
        inline int FUZZY_BUBBLES() { return AssetManager::getInstance().getFontIndexByName("FuzzyBubbles-Regular"); }
        inline int ROBOTO_MONO_BOLD() { return AssetManager::getInstance().getFontIndexByName("RobotoMono-Bold"); }
        inline int ROBOTO_MONO() { return AssetManager::getInstance().getFontIndexByName("RobotoMono-Regular"); }
        inline int TINY5() { return AssetManager::getInstance().getFontIndexByName("Tiny5-Regular"); }

        // Category-based font accessors (use FontConfig for user-selected fonts)
        // These should be used for configurable UI elements
        inline int getTitle() { return FontConfig::getInstance().getFont(FontCategory::TITLE); }
        inline int getNormal() { return FontConfig::getInstance().getFont(FontCategory::NORMAL); }
        inline int getStrong() { return FontConfig::getInstance().getFont(FontCategory::STRONG); }
        inline int getDigits() { return FontConfig::getInstance().getFont(FontCategory::DIGITS); }
        inline int getMarker() { return FontConfig::getInstance().getFont(FontCategory::MARKER); }
        inline int getSmall() { return FontConfig::getInstance().getFont(FontCategory::SMALL); }

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

    // Standard line heights.
    // NORMAL is the base; the rest are fixed MULTIPLES of it (not literals) so that a
    // change to NORMAL scales them together and preserves every widget's relationship to
    // the snap grid (GRID_SIZE_VERTICAL = 0.5 * NORMAL, so NORMAL = 2 cells and LARGE =
    // 4 cells). If these were literals, a NORMAL bump would knock LARGE off the grid and
    // break vertical snap alignment for the big-value widgets (Time/Position/Lap/Speed/
    // Gear) - which is exactly what happened when NORMAL was first bumped.
    namespace LineHeights {
        // 1x line height. Bumped from 0.0222 so the 8-char x 3-row gauge widgets
        // (G-force / Lean / Fuel) render PIXEL-SQUARE at 16:9: their height is
        // 5 x NORMAL and their width is 12 x CHAR_WIDTH, so NORMAL = 12 * CHAR_WIDTH *
        // (16/9) / 5 makes height == width in pixels (~127x127 at 1080p). Cascades to
        // HUD_VERTICAL (padding), GRID_SIZE_VERTICAL (snap grid) and the multiples below,
        // so normal-height rows across the UI get ~5.7% taller. Widths are unchanged, so
        // non-gauge widgets just get taller (still rectangular).
        constexpr float NORMAL = 0.023467f;             // 1x line height (12 * CHAR_WIDTH * 16/9 / 5)
        constexpr float EXTRA_SMALL = 0.625f * NORMAL;  // 0.625x normal line height
        constexpr float SMALL = 0.75f * NORMAL;         // 0.75x normal line height
        constexpr float LARGE = 2.0f * NORMAL;          // 2x normal line height
        constexpr float EXTRA_LARGE = 2.0f * NORMAL;    // 2x normal line height (same as LARGE, for 0.04 font)
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
    // Rear variants are darkened versions of front colors for visual distinction
    namespace SemanticColors {
        constexpr unsigned long THROTTLE = ColorPalette::GREEN;
        constexpr unsigned long FRONT_BRAKE = ColorPalette::RED;
        constexpr unsigned long REAR_BRAKE = PluginUtils::darkenColor(FRONT_BRAKE, 0.7f);
        constexpr unsigned long CLUTCH = ColorPalette::BLUE;
        constexpr unsigned long FRONT_SUSP = ColorPalette::PURPLE;
        constexpr unsigned long REAR_SUSP = PluginUtils::darkenColor(FRONT_SUSP, 0.6f);
        constexpr unsigned long GEAR = ColorPalette::ORANGE;
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
        constexpr const char* LAP_TIME = "-:--.---";    // For lap times in M:SS.mmm format
        constexpr const char* GENERIC = "-";            // For simple missing data
        constexpr const char* NOT_AVAILABLE = "N/A";    // For structurally unavailable data (e.g., live gap in practice)
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
    // Note: per-game session ID constants (PRE_QUALIFY=2 etc.) used to live here,
    // but they leaked MX Bikes' raw numbering into core code and produced wrong
    // labels / wrong isRaceSession() results on other games. The canonical
    // session set now lives in Unified::Session (game/unified_types.h);
    // each adapter exposes Adapter::toCanonicalSession() to convert raw -> canonical.

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
        constexpr int MAX_LAP_LOG_CAPACITY = 30;   // Max laps the Lap Log HUD displays per rider
        // Storage cap for the per-rider lap history in PluginData. Larger than the
        // Lap Log HUD's display cap so the Session Charts HUD can plot a full race,
        // not just the most recent laps. ~100 laps x 50 riders x ~28 B is trivial.
        constexpr int MAX_LAP_LOG_STORAGE = 100;
        constexpr int MAX_EVENT_LOG_CAPACITY = 100; // Maximum events stored in event log ring buffer
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
            // Testing event's practice session: "Testing" offline, "Open Practice" online
            constexpr const char* TESTING = "Testing";
            constexpr const char* OPEN_PRACTICE = "Open Practice";
            constexpr const char* PRE_QUALIFY = "Pre-Qualify";
            constexpr const char* QUALIFY_PRACTICE = "Qualify Practice";
            constexpr const char* QUALIFY = "Qualify";
            constexpr const char* WARMUP = "Warmup";
            constexpr const char* RACE_1 = "Race 1";
            constexpr const char* RACE_2 = "Race 2";
            constexpr const char* RACE   = "Race";
            constexpr const char* SR_ROUND = "Round";
            constexpr const char* SR_QUARTER_FINALS = "Quarter-Finals";
            constexpr const char* SR_SEMI_FINALS = "Semi-Finals";
            constexpr const char* SR_FINAL = "Final";
            // KRP multi-heat race format. The group letter (A/B/C) for heats
            // lives in RaceSessionData, not in this string — callers that want
            // "Heat A" should format it themselves.
            constexpr const char* KRP_QUALIFY_HEAT = "Qualify Heat";
            constexpr const char* KRP_SECOND_CHANCE_HEAT = "Second Chance";
            constexpr const char* KRP_PRE_FINAL = "PreFinal";
            constexpr const char* KRP_FINAL = "Final";
            constexpr const char* UNKNOWN = "Unknown";
        }

        // "Where you are" labels shown in place of a server name across the
        // SessionHud server row, Discord Rich Presence and Steam presence. Same
        // text on every game (no per-game variant). Domain-scoped literals like the
        // rest of this file - "Testing"/"Unknown" recurring per namespace is the
        // intended convention (each owns its own display string; none cross-ref).
        // Solo/offline reads "Testing"; an online session with no name (older MX
        // Bikes build, or GP Bikes / KRP which don't report the server) reads
        // "Unknown".
        namespace ServerLabel {
            constexpr const char* TESTING = "Testing";  // solo / offline
            constexpr const char* UNKNOWN = "Unknown";  // name unavailable / not reported
            constexpr const char* ONLINE  = "Online";   // known online but unnamed (GP Bikes / KRP: no serverType in API, inferred from >1 rider)
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

    // ========================================================================
    // Sprite Index Constants
    // ========================================================================
    // IMPORTANT: Sprite indices are now dynamically assigned by AssetManager.
    // Textures and icons are discovered from mxbmrp3_data/ subdirectories:
    //   - fonts/    -> Font files (.fnt)
    //   - textures/ -> HUD/widget textures with variants (e.g., standings_hud_1.tga)
    //   - icons/    -> Rider icon sprites (e.g., trophy-solid-full.tga)
    //
    // To get texture indices, use:
    //   AssetManager::getInstance().getSpriteIndex("texture_name", variant)
    //
    // For rider icons, use:
    //   AssetManager::getInstance().getFirstIconSpriteIndex() + shapeIndex - 1
    // where shapeIndex is 1-based (1 = first icon)
    // ========================================================================
    namespace SpriteIndex {
        // SOLID_COLOR is always 0 - means "no texture, use quad color directly"
        constexpr int SOLID_COLOR = 0;

        // Rider icon count - icons are discovered dynamically from icons/ directory
        // This is used for UI cycling through available shapes (updated to match actual count)
        constexpr int RIDER_ICON_COUNT = 51;

        // Helper to get first icon sprite index at runtime
        // Use: AssetManager::getInstance().getFirstIconSpriteIndex()
        // Individual icon: getFirstIconSpriteIndex() + shapeIndex - 1  (shapeIndex 1-based)
    }
}
