// ============================================================================
// hud/settings_hud.h
// Settings interface for configuring which columns/rows are visible in HUDs
// ============================================================================
#pragma once

#include "base_hud.h"
#include "ideal_lap_hud.h"
#include "lap_log_hud.h"
#include "lap_consistency_hud.h"
#include "standings_hud.h"
#include "performance_hud.h"
#include "pitboard_hud.h"
#include "time_widget.h"
#include "position_widget.h"
#include "lap_widget.h"
#include "session_hud.h"
#include "speed_widget.h"
#include "speedo_widget.h"
#include "tacho_widget.h"
#include "timing_hud.h"
#include "gap_bar_hud.h"
#include "bars_widget.h"
#include "version_widget.h"
#include "notices_widget.h"
#include "fuel_widget.h"
#include "pointer_widget.h"
#include "records_hud.h"
#include "gamepad_widget.h"
#include "lean_widget.h"
#if GAME_HAS_TYRE_TEMP
#include "tyre_temp_widget.h"
#endif
#include "fmx_hud.h"
#include <variant>
#include <string>
#include "map_hud.h"
#include "radar_hud.h"
#include "../core/plugin_constants.h"
#include "../core/color_config.h"
#include "../core/font_config.h"
#include "../core/xinput_reader.h"
#include "../core/hotkey_config.h"

// Forward declarations
class TelemetryHud;
class RumbleHud;
struct SettingsLayoutContext;

class SettingsHud : public BaseHud {
public:
    SettingsHud(IdealLapHud* idealLap, LapLogHud* lapLog, LapConsistencyHud* lapConsistency,
                StandingsHud* standings,
                PerformanceHud* performance,
                TelemetryHud* telemetry,
                TimeWidget* time, PositionWidget* position, LapWidget* lap, SessionHud* session, MapHud* mapHud, RadarHud* radarHud, SpeedWidget* speed, SpeedoWidget* speedo, TachoWidget* tacho, TimingHud* timing, GapBarHud* gapBar, BarsWidget* bars, VersionWidget* version, NoticesWidget* notices, PitboardHud* pitboard, RecordsHud* records, FuelWidget* fuel, PointerWidget* pointer, RumbleHud* rumble, GamepadWidget* gamepad, LeanWidget* lean,
                FmxHud* fmxHud
#if GAME_HAS_TYRE_TEMP
                , TyreTempWidget* tyreTemp
#endif
                );
    virtual ~SettingsHud() = default;

    void update() override;
    bool handlesDataType(DataChangeType dataType) const override { return false; }

    // Show/hide the settings panel
    void show();
    void hide();
    bool isVisible() const { return m_bVisible; }

    // Open settings panel directly to Updates tab
    void showUpdatesTab();

    // Clickable regions for checkboxes, buttons, and scale controls (public for SettingsLayoutContext)
    struct ClickRegion {
        float x, y, width, height;
        enum Type {
            CHECKBOX,                  // Toggle column/row visibility (bitfield)
            GAP_MODE_UP,               // Cycle gap mode forward (Off/Me/All)
            GAP_MODE_DOWN,             // Cycle gap mode backward
            GAP_INDICATOR_UP,          // Cycle gap indicator forward (Off/Official/Live/Both)
            GAP_INDICATOR_DOWN,        // Cycle gap indicator backward
            GAP_REFERENCE_UP,          // Cycle gap reference forward (Leader/Player)
            GAP_REFERENCE_DOWN,        // Cycle gap reference backward
            STANDINGS_DEBUG_TOGGLE,    // Unused - debug column is INI-only (col_debug=1)
            RESET_BUTTON,              // Unified reset button (General tab) - action depends on checkbox
            RESET_TAB_BUTTON,          // Reset current tab to defaults (footer)
            COPY_TARGET_UP,            // Cycle copy target forward
            COPY_TARGET_DOWN,          // Cycle copy target backward
            COPY_BUTTON,               // Execute copy to selected target profile(s)
            RESET_PROFILE_CHECKBOX,    // Radio-style checkbox for Reset Profile
            RESET_ALL_CHECKBOX,        // Radio-style checkbox for Reset All Profiles
            HUD_TOGGLE,                // Toggle entire HUD visibility
            TITLE_TOGGLE,              // Toggle HUD title
            TEXTURE_VARIANT_UP,        // Cycle texture variant forward (Off, 1, 2, ...)
            TEXTURE_VARIANT_DOWN,      // Cycle texture variant backward
            BACKGROUND_OPACITY_UP,     // Increase background opacity
            BACKGROUND_OPACITY_DOWN,   // Decrease background opacity
            SCALE_UP,                  // Increase scale
            SCALE_DOWN,                // Decrease scale
            ROW_COUNT_UP,              // Increase row count (StandingsHud)
            ROW_COUNT_DOWN,            // Decrease row count (StandingsHud)
            LAP_LOG_ROW_COUNT_UP,      // Increase lap log row count (LapLogHud)
            LAP_LOG_ROW_COUNT_DOWN,    // Decrease lap log row count (LapLogHud)
            LAP_LOG_ORDER_UP,          // Cycle display order forward (LapLogHud)
            LAP_LOG_ORDER_DOWN,        // Cycle display order backward (LapLogHud)
            LAP_LOG_GAP_ROW_TOGGLE,    // Toggle gap row display (LapLogHud)
            // Lap Consistency HUD
            LAP_CONSISTENCY_DISPLAY_MODE_UP,    // Cycle display mode forward (LapConsistencyHud)
            LAP_CONSISTENCY_DISPLAY_MODE_DOWN,  // Cycle display mode backward (LapConsistencyHud)
            LAP_CONSISTENCY_REFERENCE_UP,       // Cycle reference mode forward (LapConsistencyHud)
            LAP_CONSISTENCY_REFERENCE_DOWN,     // Cycle reference mode backward (LapConsistencyHud)
            LAP_CONSISTENCY_LAP_COUNT_UP,       // Increase lap count (LapConsistencyHud)
            LAP_CONSISTENCY_LAP_COUNT_DOWN,     // Decrease lap count (LapConsistencyHud)
            LAP_CONSISTENCY_TREND_MODE_UP,      // Cycle trend mode forward (LapConsistencyHud)
            LAP_CONSISTENCY_TREND_MODE_DOWN,    // Cycle trend mode backward (LapConsistencyHud)
            MAP_ROTATION_TOGGLE,       // Toggle map rotation mode (MapHud)
            MAP_OUTLINE_TOGGLE,        // Toggle track outline (MapHud)
            MAP_COLORIZE_UP,           // Cycle rider color mode forward (MapHud)
            MAP_COLORIZE_DOWN,         // Cycle rider color mode backward (MapHud)
            MAP_TRACK_WIDTH_UP,        // Increase track line width (MapHud)
            MAP_TRACK_WIDTH_DOWN,      // Decrease track line width (MapHud)
            MAP_LABEL_MODE_UP,         // Cycle label mode forward (MapHud)
            MAP_LABEL_MODE_DOWN,       // Cycle label mode backward (MapHud)
            MAP_RANGE_UP,              // Increase map range / decrease zoom (MapHud)
            MAP_RANGE_DOWN,            // Decrease map range / increase zoom (MapHud)
            MAP_RIDER_SHAPE_UP,        // Cycle rider shape forward (MapHud)
            MAP_RIDER_SHAPE_DOWN,      // Cycle rider shape backward (MapHud)
            MAP_MARKER_SCALE_UP,       // Increase marker scale (MapHud)
            MAP_MARKER_SCALE_DOWN,     // Decrease marker scale (MapHud)
            RADAR_RANGE_UP,            // Increase radar range (RadarHud)
            RADAR_RANGE_DOWN,          // Decrease radar range (RadarHud)
            RADAR_COLORIZE_UP,         // Cycle rider color mode forward (RadarHud)
            RADAR_COLORIZE_DOWN,       // Cycle rider color mode backward (RadarHud)
            RADAR_ALERT_DISTANCE_UP,   // Increase alert distance (RadarHud)
            RADAR_ALERT_DISTANCE_DOWN, // Decrease alert distance (RadarHud)
            RADAR_LABEL_MODE_UP,       // Cycle label mode forward (RadarHud)
            RADAR_LABEL_MODE_DOWN,     // Cycle label mode backward (RadarHud)
            RADAR_MODE_UP,             // Cycle radar mode forward (RadarHud)
            RADAR_MODE_DOWN,           // Cycle radar mode backward (RadarHud)
            RADAR_PROXIMITY_ARROWS_UP, // Cycle proximity arrow mode forward (RadarHud)
            RADAR_PROXIMITY_ARROWS_DOWN, // Cycle proximity arrow mode backward (RadarHud)
            RADAR_PROXIMITY_SHAPE_UP,  // Cycle proximity arrow shape forward (RadarHud)
            RADAR_PROXIMITY_SHAPE_DOWN, // Cycle proximity arrow shape backward (RadarHud)
            RADAR_PROXIMITY_SCALE_UP,  // Increase proximity arrow scale (RadarHud)
            RADAR_PROXIMITY_SCALE_DOWN, // Decrease proximity arrow scale (RadarHud)
            RADAR_PROXIMITY_COLOR_UP,  // Cycle proximity arrow color mode forward (RadarHud)
            RADAR_PROXIMITY_COLOR_DOWN, // Cycle proximity arrow color mode backward (RadarHud)
            RADAR_RIDER_SHAPE_UP,      // Cycle rider shape forward (RadarHud)
            RADAR_RIDER_SHAPE_DOWN,    // Cycle rider shape backward (RadarHud)
            RADAR_MARKER_SCALE_UP,     // Increase marker scale (RadarHud)
            RADAR_MARKER_SCALE_DOWN,   // Decrease marker scale (RadarHud)
            DISPLAY_MODE_UP,           // Cycle display mode forward (PerformanceHud)
            DISPLAY_MODE_DOWN,         // Cycle display mode backward (PerformanceHud)
            RECORDS_COUNT_UP,          // Increase records to show (RecordsHud)
            RECORDS_COUNT_DOWN,        // Decrease records to show (RecordsHud)
            RECORDS_PROVIDER_UP,       // Cycle provider forward (RecordsHud)
            RECORDS_PROVIDER_DOWN,     // Cycle provider backward (RecordsHud)
            RECORDS_AUTO_FETCH_TOGGLE, // Toggle auto-fetch on event start (RecordsHud)
            PITBOARD_SHOW_MODE_UP,     // Cycle pitboard show mode forward (PitboardHud)
            PITBOARD_SHOW_MODE_DOWN,   // Cycle pitboard show mode backward (PitboardHud)
            SESSION_PASSWORD_MODE_UP,  // Cycle password display mode forward (SessionHud)
            SESSION_PASSWORD_MODE_DOWN,// Cycle password display mode backward (SessionHud)
            SESSION_ICONS_TOGGLE,      // Toggle icons on/off (SessionHud)
            TIMING_LABEL_TOGGLE,       // Toggle label column on/off (TimingHud)
            TIMING_TIME_TOGGLE,        // Toggle time column on/off (TimingHud)
            TIMING_GAP_UP,             // Cycle gap (Off/Session PB/Alltime/Ideal/Overall/Record) forward (TimingHud)
            TIMING_GAP_DOWN,           // Cycle gap backward (TimingHud)
            TIMING_DISPLAY_MODE_UP,    // Cycle show mode (Splits/Always) forward (TimingHud)
            TIMING_DISPLAY_MODE_DOWN,  // Cycle show mode backward (TimingHud)
            TIMING_DURATION_UP,        // Increase freeze duration (TimingHud)
            TIMING_DURATION_DOWN,      // Decrease freeze duration (TimingHud)
            TIMING_REFERENCE_TOGGLE,   // Toggle reference time display on/off (TimingHud)
            TIMING_LAYOUT_TOGGLE,      // Toggle layout mode (Horizontal/Vertical) (TimingHud)
            TIMING_GAP_PB_TOGGLE,      // Toggle "Session PB" as secondary (TimingHud)
            TIMING_GAP_IDEAL_TOGGLE,   // Toggle "Ideal" as secondary chip (TimingHud)
            TIMING_GAP_OVERALL_TOGGLE, // Toggle "Server Best" as secondary chip (TimingHud)
            TIMING_GAP_ALLTIME_TOGGLE, // Toggle "All-Time PB" as secondary chip (TimingHud)
            TIMING_GAP_RECORD_TOGGLE,  // Toggle "Record" as secondary chip (TimingHud)
            GAPBAR_FREEZE_UP,          // Increase freeze duration (GapBarHud)
            GAPBAR_FREEZE_DOWN,        // Decrease freeze duration (GapBarHud)
            GAPBAR_MARKER_MODE_UP,     // Cycle marker mode forward (Ghost/Opponents/Both)
            GAPBAR_MARKER_MODE_DOWN,   // Cycle marker mode backward
            GAPBAR_ICON_UP,            // Cycle marker icon forward (GapBarHud)
            GAPBAR_ICON_DOWN,          // Cycle marker icon backward (GapBarHud)
            GAPBAR_GAP_TEXT_TOGGLE,    // Toggle gap text visibility (GapBarHud)
            GAPBAR_RANGE_UP,           // Increase gap range (GapBarHud)
            GAPBAR_RANGE_DOWN,         // Decrease gap range (GapBarHud)
            GAPBAR_WIDTH_UP,           // Increase bar width (GapBarHud)
            GAPBAR_WIDTH_DOWN,         // Decrease bar width (GapBarHud)
            GAPBAR_MARKER_SCALE_UP,    // Increase marker scale (GapBarHud)
            GAPBAR_MARKER_SCALE_DOWN,  // Decrease marker scale (GapBarHud)
            GAPBAR_LABEL_MODE_UP,      // Cycle label mode forward (Off/Position/RaceNum/Both)
            GAPBAR_LABEL_MODE_DOWN,    // Cycle label mode backward
            GAPBAR_GAP_BAR_TOGGLE,     // Toggle gap bar visualization (green/red bars)
            GAPBAR_COLOR_MODE_UP,      // Cycle color mode forward (Uniform/Brand/Position)
            GAPBAR_COLOR_MODE_DOWN,    // Cycle color mode backward
            COLOR_CYCLE_PREV,          // Cycle color backward (Appearance tab)
            COLOR_CYCLE_NEXT,          // Cycle color forward (Appearance tab)
            FONT_CATEGORY_PREV,        // Cycle font backward for category (Appearance tab)
            FONT_CATEGORY_NEXT,        // Cycle font forward for category (Appearance tab)
            SPEED_UNIT_TOGGLE,         // Toggle speed unit (mph/km/h)
            FUEL_UNIT_TOGGLE,          // Toggle fuel unit (L/gal)
            TEMP_UNIT_TOGGLE,          // Toggle temperature unit (C/F)
            GRID_SNAP_TOGGLE,          // Toggle grid snapping for HUD positioning
            SCREEN_CLAMP_TOGGLE,       // Toggle screen clamping for HUD positioning
            DROP_SHADOW_TOGGLE,        // Toggle drop shadow for text rendering
            UPDATE_CHECK_TOGGLE,       // Toggle automatic update checking
            AUTOSAVE_TOGGLE,           // Toggle auto-save for settings
            SAVE_BUTTON,               // Manual save button (when auto-save is off)
#if GAME_HAS_DISCORD
            DISCORD_TOGGLE,            // Toggle Discord Rich Presence
#endif
            PROFILE_CYCLE_DOWN,        // Cycle to previous profile (Practice/Qualify/Race/Spectate)
            PROFILE_CYCLE_UP,          // Cycle to next profile
            AUTO_SWITCH_TOGGLE,        // Toggle auto-switch for profiles
            WIDGETS_TOGGLE,            // Toggle all widgets visibility (master switch)
            TAB,                       // Select tab
            CLOSE_BUTTON,              // Close the settings menu
            // Controller/Rumble settings
            RUMBLE_TOGGLE,             // Toggle rumble master enable
            RUMBLE_CONTROLLER_UP,      // Cycle controller index up
            RUMBLE_CONTROLLER_DOWN,    // Cycle controller index down
            RUMBLE_BLEND_TOGGLE,       // Toggle blend mode (max vs additive)
            RUMBLE_CRASH_TOGGLE,       // Toggle disable on crash
            RUMBLE_EFFECT_PROFILE_TOGGLE, // Toggle effect profile (global vs per-bike)
            RUMBLE_SUSP_LIGHT_DOWN,    // Decrease suspension light motor strength
            RUMBLE_SUSP_LIGHT_UP,      // Increase suspension light motor strength
            RUMBLE_SUSP_HEAVY_DOWN,    // Decrease suspension heavy motor strength
            RUMBLE_SUSP_HEAVY_UP,      // Increase suspension heavy motor strength
            RUMBLE_SUSP_MIN_UP,        // Increase suspension min input
            RUMBLE_SUSP_MIN_DOWN,      // Decrease suspension min input
            RUMBLE_SUSP_MAX_UP,        // Increase suspension max input
            RUMBLE_SUSP_MAX_DOWN,      // Decrease suspension max input
            RUMBLE_WHEEL_LIGHT_DOWN,   // Decrease spin light motor strength
            RUMBLE_WHEEL_LIGHT_UP,     // Increase spin light motor strength
            RUMBLE_WHEEL_HEAVY_DOWN,   // Decrease spin heavy motor strength
            RUMBLE_WHEEL_HEAVY_UP,     // Increase spin heavy motor strength
            RUMBLE_WHEEL_MIN_UP,       // Increase spin min input
            RUMBLE_WHEEL_MIN_DOWN,     // Decrease spin min input
            RUMBLE_WHEEL_MAX_UP,       // Increase spin max input
            RUMBLE_WHEEL_MAX_DOWN,     // Decrease spin max input
            RUMBLE_LOCKUP_LIGHT_DOWN,  // Decrease brake lockup light motor strength
            RUMBLE_LOCKUP_LIGHT_UP,    // Increase brake lockup light motor strength
            RUMBLE_LOCKUP_HEAVY_DOWN,  // Decrease brake lockup heavy motor strength
            RUMBLE_LOCKUP_HEAVY_UP,    // Increase brake lockup heavy motor strength
            RUMBLE_LOCKUP_MIN_UP,      // Increase brake lockup min input
            RUMBLE_LOCKUP_MIN_DOWN,    // Decrease brake lockup min input
            RUMBLE_LOCKUP_MAX_UP,      // Increase brake lockup max input
            RUMBLE_LOCKUP_MAX_DOWN,    // Decrease brake lockup max input
            RUMBLE_WHEELIE_LIGHT_DOWN, // Decrease wheelie light motor strength
            RUMBLE_WHEELIE_LIGHT_UP,   // Increase wheelie light motor strength
            RUMBLE_WHEELIE_HEAVY_DOWN, // Decrease wheelie heavy motor strength
            RUMBLE_WHEELIE_HEAVY_UP,   // Increase wheelie heavy motor strength
            RUMBLE_WHEELIE_MIN_UP,     // Increase wheelie min input
            RUMBLE_WHEELIE_MIN_DOWN,   // Decrease wheelie min input
            RUMBLE_WHEELIE_MAX_UP,     // Increase wheelie max input
            RUMBLE_WHEELIE_MAX_DOWN,   // Decrease wheelie max input
            RUMBLE_RPM_LIGHT_DOWN,     // Decrease RPM light motor strength
            RUMBLE_RPM_LIGHT_UP,       // Increase RPM light motor strength
            RUMBLE_RPM_HEAVY_DOWN,     // Decrease RPM heavy motor strength
            RUMBLE_RPM_HEAVY_UP,       // Increase RPM heavy motor strength
            RUMBLE_RPM_MIN_UP,         // Increase RPM min input
            RUMBLE_RPM_MIN_DOWN,       // Decrease RPM min input
            RUMBLE_RPM_MAX_UP,         // Increase RPM max input
            RUMBLE_RPM_MAX_DOWN,       // Decrease RPM max input
            RUMBLE_SLIDE_LIGHT_DOWN,   // Decrease slide light motor strength
            RUMBLE_SLIDE_LIGHT_UP,     // Increase slide light motor strength
            RUMBLE_SLIDE_HEAVY_DOWN,   // Decrease slide heavy motor strength
            RUMBLE_SLIDE_HEAVY_UP,     // Increase slide heavy motor strength
            RUMBLE_SLIDE_MIN_UP,       // Increase slide min input
            RUMBLE_SLIDE_MIN_DOWN,     // Decrease slide min input
            RUMBLE_SLIDE_MAX_UP,       // Increase slide max input
            RUMBLE_SLIDE_MAX_DOWN,     // Decrease slide max input
            RUMBLE_SURFACE_LIGHT_DOWN, // Decrease surface light motor strength
            RUMBLE_SURFACE_LIGHT_UP,   // Increase surface light motor strength
            RUMBLE_SURFACE_HEAVY_DOWN, // Decrease surface heavy motor strength
            RUMBLE_SURFACE_HEAVY_UP,   // Increase surface heavy motor strength
            RUMBLE_SURFACE_MIN_UP,     // Increase surface min input
            RUMBLE_SURFACE_MIN_DOWN,   // Decrease surface min input
            RUMBLE_SURFACE_MAX_UP,     // Increase surface max input
            RUMBLE_SURFACE_MAX_DOWN,   // Decrease surface max input
            RUMBLE_STEER_LIGHT_DOWN,   // Decrease steer light motor strength
            RUMBLE_STEER_LIGHT_UP,     // Increase steer light motor strength
            RUMBLE_STEER_HEAVY_DOWN,   // Decrease steer heavy motor strength
            RUMBLE_STEER_HEAVY_UP,     // Increase steer heavy motor strength
            RUMBLE_STEER_MIN_UP,       // Increase steer min input
            RUMBLE_STEER_MIN_DOWN,     // Decrease steer min input
            RUMBLE_STEER_MAX_UP,       // Increase steer max input
            RUMBLE_STEER_MAX_DOWN,     // Decrease steer max input
            RUMBLE_HUD_TOGGLE,         // Toggle RumbleHud visibility
            // Hotkey settings
            HOTKEY_KEYBOARD_BIND,      // Click to capture keyboard binding
            HOTKEY_CONTROLLER_BIND,    // Click to capture controller binding
            HOTKEY_KEYBOARD_CLEAR,     // Clear keyboard binding
            HOTKEY_CONTROLLER_CLEAR,   // Clear controller binding
            // Tracked Riders settings
            RIDER_ADD,                 // Add rider to tracking list
            RIDER_REMOVE,              // Remove rider from tracking list
            RIDER_COLOR_PREV,          // Cycle rider color backward
            RIDER_COLOR_NEXT,          // Cycle rider color forward
            RIDER_SHAPE_PREV,          // Cycle rider shape backward
            RIDER_SHAPE_NEXT,          // Cycle rider shape forward
            // Pagination for Riders tab
            SERVER_PAGE_PREV,          // Previous page of server players
            SERVER_PAGE_NEXT,          // Next page of server players
            TRACKED_PAGE_PREV,         // Previous page of tracked riders
            TRACKED_PAGE_NEXT,         // Next page of tracked riders
            VERSION_CLICK,             // Easter egg trigger (version string click)
            TOOLTIP_ROW,               // Hover-only region for row tooltips (no click action)
            // Update settings
            UPDATE_CHECK_NOW,          // Manual check for updates button
            UPDATE_INSTALL,            // Install available update
            UPDATE_SKIP_VERSION,       // Skip this version (acts as Retry)
            UPDATE_DEBUG_MODE,         // Toggle debug mode for testing
            UPDATE_CHANNEL_UP,         // Cycle update channel forward (Stable -> Pre-release)
            UPDATE_CHANNEL_DOWN,       // Cycle update channel backward (Pre-release -> Stable)
            // FMX HUD
            FMX_DEBUG_TOGGLE,          // Toggle FMX debug logging
            FMX_CHAIN_ROWS_UP,         // Increase max chain display rows
            FMX_CHAIN_ROWS_DOWN        // Decrease max chain display rows
        } type;

        // Type-safe variant instead of unsafe union (C++17)
        // Holds different pointer types based on ClickRegion::Type
        using TargetPointer = std::variant<
            std::monostate,                              // Empty state (for types that don't need a pointer)
            uint32_t*,                                   // For CHECKBOX (targetBitfield)
            StandingsHud::GapMode*,                      // For GAP_MODE_CYCLE
            StandingsHud::GapIndicatorMode*,             // For GAP_INDICATOR_CYCLE
            StandingsHud::GapReferenceMode*,             // For GAP_REFERENCE_UP/DOWN
            uint8_t*,                                    // For DISPLAY_MODE_UP/DOWN
            ColorSlot,                                   // For COLOR_CYCLE_PREV/NEXT
            FontCategory,                                // For FONT_CATEGORY_PREV/NEXT
            HotkeyAction,                                // For HOTKEY_* controls
            std::string                                  // For RIDER_* controls (rider name)
        >;
        TargetPointer targetPointer;

        uint32_t flagBit;          // Which bit to toggle (for CHECKBOX)
        bool isRequired;           // Can't toggle if required (for CHECKBOX)
        BaseHud* targetHud;        // HUD to mark dirty after toggle
        int tabIndex;              // Which tab to switch to (for TAB type)
        std::string tooltipId;     // Tooltip ID for hover display (Phase 3)

        // Constructor for simple regions (no pointer needed)
        ClickRegion(float _x, float _y, float _width, float _height, Type _type,
                   BaseHud* _targetHud = nullptr, uint32_t _flagBit = 0,
                   bool _isRequired = false, int _tabIndex = 0)
            : x(_x), y(_y), width(_width), height(_height), type(_type),
              targetPointer(std::monostate{}), flagBit(_flagBit), isRequired(_isRequired),
              targetHud(_targetHud), tabIndex(_tabIndex), tooltipId() {}

        // Constructor for TOOLTIP_ROW regions (hover-only, no click)
        ClickRegion(float _x, float _y, float _width, float _height, const char* _tooltipId)
            : x(_x), y(_y), width(_width), height(_height), type(TOOLTIP_ROW),
              targetPointer(std::monostate{}), flagBit(0), isRequired(false),
              targetHud(nullptr), tabIndex(0), tooltipId(_tooltipId ? _tooltipId : "") {}

        // Constructor for CHECKBOX regions (uses uint32_t* bitfield)
        ClickRegion(float _x, float _y, float _width, float _height, Type _type,
                   uint32_t* bitfield, uint32_t _flagBit, bool _isRequired, BaseHud* _targetHud)
            : x(_x), y(_y), width(_width), height(_height), type(_type),
              targetPointer(bitfield), flagBit(_flagBit), isRequired(_isRequired),
              targetHud(_targetHud), tabIndex(0), tooltipId() {}

        // Constructor for GAP_MODE_UP/DOWN regions
        ClickRegion(float _x, float _y, float _width, float _height, Type _type,
                   StandingsHud::GapMode* gapMode, BaseHud* _targetHud)
            : x(_x), y(_y), width(_width), height(_height), type(_type),
              targetPointer(gapMode), flagBit(0), isRequired(false),
              targetHud(_targetHud), tabIndex(0), tooltipId() {}

        // Constructor for GAP_INDICATOR_UP/DOWN regions
        ClickRegion(float _x, float _y, float _width, float _height, Type _type,
                   StandingsHud::GapIndicatorMode* gapIndicatorMode, BaseHud* _targetHud)
            : x(_x), y(_y), width(_width), height(_height), type(_type),
              targetPointer(gapIndicatorMode), flagBit(0), isRequired(false),
              targetHud(_targetHud), tabIndex(0), tooltipId() {}

        // Constructor for GAP_REFERENCE_UP/DOWN regions
        ClickRegion(float _x, float _y, float _width, float _height, Type _type,
                   StandingsHud::GapReferenceMode* gapReferenceMode, BaseHud* _targetHud)
            : x(_x), y(_y), width(_width), height(_height), type(_type),
              targetPointer(gapReferenceMode), flagBit(0), isRequired(false),
              targetHud(_targetHud), tabIndex(0), tooltipId() {}

        // Constructor for DISPLAY_MODE regions
        ClickRegion(float _x, float _y, float _width, float _height, Type _type,
                   uint8_t* displayMode, BaseHud* _targetHud)
            : x(_x), y(_y), width(_width), height(_height), type(_type),
              targetPointer(displayMode), flagBit(0), isRequired(false),
              targetHud(_targetHud), tabIndex(0), tooltipId() {}

        // Constructor for COLOR_CYCLE regions
        ClickRegion(float _x, float _y, float _width, float _height, Type _type,
                   ColorSlot colorSlot)
            : x(_x), y(_y), width(_width), height(_height), type(_type),
              targetPointer(colorSlot), flagBit(0), isRequired(false),
              targetHud(nullptr), tabIndex(0), tooltipId() {}

        // Constructor for FONT_CATEGORY regions
        ClickRegion(float _x, float _y, float _width, float _height, Type _type,
                   FontCategory fontCategory)
            : x(_x), y(_y), width(_width), height(_height), type(_type),
              targetPointer(fontCategory), flagBit(0), isRequired(false),
              targetHud(nullptr), tabIndex(0), tooltipId() {}

        // Constructor for HOTKEY_* regions
        ClickRegion(float _x, float _y, float _width, float _height, Type _type,
                   HotkeyAction hotkeyAction)
            : x(_x), y(_y), width(_width), height(_height), type(_type),
              targetPointer(hotkeyAction), flagBit(0), isRequired(false),
              targetHud(nullptr), tabIndex(0), tooltipId() {}

        // Constructor for RIDER_* regions
        ClickRegion(float _x, float _y, float _width, float _height, Type _type,
                   const std::string& riderName)
            : x(_x), y(_y), width(_width), height(_height), type(_type),
              targetPointer(riderName), flagBit(0), isRequired(false),
              targetHud(nullptr), tabIndex(0), tooltipId() {}

        // Default constructor
        ClickRegion() : x(0), y(0), width(0), height(0), type(CLOSE_BUTTON),
                       targetPointer(std::monostate{}), flagBit(0), isRequired(false),
                       targetHud(nullptr), tabIndex(0), tooltipId() {}
    };

    // Friend declarations for settings layout system
    friend struct SettingsLayoutContext;

    // Static tab rendering functions (inherit friend access to HUD classes)
    // Implemented in separate files under hud/settings/
    static BaseHud* renderTabIdealLap(SettingsLayoutContext& ctx);
    static BaseHud* renderTabLapLog(SettingsLayoutContext& ctx);
    static BaseHud* renderTabLapConsistency(SettingsLayoutContext& ctx);
    static BaseHud* renderTabTelemetry(SettingsLayoutContext& ctx);
    static BaseHud* renderTabPerformance(SettingsLayoutContext& ctx);
    static BaseHud* renderTabRecords(SettingsLayoutContext& ctx);
    static BaseHud* renderTabPitboard(SettingsLayoutContext& ctx);
    static BaseHud* renderTabSession(SettingsLayoutContext& ctx);
    static BaseHud* renderTabTiming(SettingsLayoutContext& ctx);
    static BaseHud* renderTabGapBar(SettingsLayoutContext& ctx);
    static BaseHud* renderTabStandings(SettingsLayoutContext& ctx);
    static BaseHud* renderTabMap(SettingsLayoutContext& ctx);
    static BaseHud* renderTabRadar(SettingsLayoutContext& ctx);
    static BaseHud* renderTabWidgets(SettingsLayoutContext& ctx);
    static BaseHud* renderTabRumble(SettingsLayoutContext& ctx);
    static BaseHud* renderTabGeneral(SettingsLayoutContext& ctx);
    static BaseHud* renderTabAppearance(SettingsLayoutContext& ctx);
    static BaseHud* renderTabHotkeys(SettingsLayoutContext& ctx);
    static BaseHud* renderTabRiders(SettingsLayoutContext& ctx);
    static BaseHud* renderTabUpdates(SettingsLayoutContext& ctx);
    static BaseHud* renderTabFmx(SettingsLayoutContext& ctx);

    // Static click handler functions (implemented in tab files)
    // Return true if the click was handled, false otherwise
    bool handleClickTabMap(const ClickRegion& region);
    bool handleClickTabRadar(const ClickRegion& region);
    bool handleClickTabTiming(const ClickRegion& region);
    bool handleClickTabGapBar(const ClickRegion& region);
    bool handleClickTabStandings(const ClickRegion& region);
    bool handleClickTabRumble(const ClickRegion& region);
    bool handleClickTabAppearance(const ClickRegion& region);
    bool handleClickTabGeneral(const ClickRegion& region);
    bool handleClickTabHotkeys(const ClickRegion& region);
    bool handleClickTabRiders(const ClickRegion& region);
    bool handleClickTabRecords(const ClickRegion& region);
    bool handleClickTabPitboard(const ClickRegion& region);
    bool handleClickTabSession(const ClickRegion& region);
    bool handleClickTabLapLog(const ClickRegion& region);
    bool handleClickTabLapConsistency(const ClickRegion& region);
    bool handleClickTabUpdates(const ClickRegion& region);
    bool handleClickTabFmx(const ClickRegion& region);

    // HUD getter methods (for tab rendering functions)
    IdealLapHud* getIdealLapHud() const { return m_idealLap; }
    LapLogHud* getLapLogHud() const { return m_lapLog; }
    LapConsistencyHud* getLapConsistencyHud() const { return m_lapConsistency; }
    StandingsHud* getStandingsHud() const { return m_standings; }
    PerformanceHud* getPerformanceHud() const { return m_performance; }
    TelemetryHud* getTelemetryHud() const { return m_telemetry; }
    TimeWidget* getTimeWidget() const { return m_time; }
    PositionWidget* getPositionWidget() const { return m_position; }
    LapWidget* getLapWidget() const { return m_lap; }
    SessionHud* getSessionHud() const { return m_session; }
    MapHud* getMapHud() const { return m_mapHud; }
    RadarHud* getRadarHud() const { return m_radarHud; }
    SpeedWidget* getSpeedWidget() const { return m_speed; }
    SpeedoWidget* getSpeedoWidget() const { return m_speedo; }
    TachoWidget* getTachoWidget() const { return m_tacho; }
    TimingHud* getTimingHud() const { return m_timing; }
    GapBarHud* getGapBarHud() const { return m_gapBar; }
    BarsWidget* getBarsWidget() const { return m_bars; }
    VersionWidget* getVersionWidget() const { return m_version; }
    NoticesWidget* getNoticesWidget() const { return m_notices; }
    PitboardHud* getPitboardHud() const { return m_pitboard; }
    RecordsHud* getRecordsHud() const { return m_records; }
    FuelWidget* getFuelWidget() const { return m_fuel; }
    PointerWidget* getPointerWidget() const { return m_pointer; }
    RumbleHud* getRumbleHud() const { return m_rumble; }
    GamepadWidget* getGamepadWidget() const { return m_gamepad; }
    LeanWidget* getLeanWidget() const { return m_lean; }
#if GAME_HAS_TYRE_TEMP
    TyreTempWidget* getTyreTempWidget() const { return m_tyreTemp; }
#endif
    FmxHud* getFmxHud() const { return m_fmxHud; }

protected:
    void rebuildLayout() override;

private:
    void rebuildRenderData() override;
    void handleClick(float mouseX, float mouseY);
    void handleRightClick(float mouseX, float mouseY);  // Right-click for shape cycling
    void resetToDefaults();        // Reset all profiles to defaults
    void resetCurrentTab();        // Reset current tab for current profile
    void resetCurrentProfile();    // Reset all HUDs for current profile

    // Click handlers - common handlers used by multiple tabs
    void handleCheckboxClick(const ClickRegion& region);
    void handleHudToggleClick(const ClickRegion& region);
    void handleTitleToggleClick(const ClickRegion& region);
    void handleOpacityClick(const ClickRegion& region, bool increase);
    void handleScaleClick(const ClickRegion& region, bool increase);
    void handleDisplayModeClick(const ClickRegion& region, bool increase);
    void handleTabClick(const ClickRegion& region);
    void handleCloseButtonClick();
    const char* getTabName(int tabIndex) const;  // Get display name for a tab
    // Note: Tab-specific handlers inlined into settings_tab_*.cpp files

    // Helper methods to reduce code duplication
    float addDisplayModeControl(float x, float& currentY, const ScaledDimensions& dims,
                                uint8_t* displayMode, BaseHud* targetHud);
    void addClickRegion(ClickRegion::Type type, float x, float y, float width, float height,
                        BaseHud* targetHud, uint32_t* bitfield = nullptr,
                        uint8_t* displayMode = nullptr, uint32_t flagBit = 0,
                        bool isRequired = false, int tabIndex = 0);

    // Check if point is inside a clickable region
    bool isPointInRect(float x, float y, float rectX, float rectY, float width, float height) const;

    // Settings panel layout constants (character widths for monospace text)
    static constexpr int SETTINGS_PANEL_WIDTH = 71;     // Settings panel total width (fits Rumble effects table)
    static constexpr int SETTINGS_TAB_WIDTH = 16;       // Width of vertical tab column (fits "[X] Ideal Lap")
    static constexpr int SETTINGS_LEFT_COLUMN = 2;      // Left column offset within content area
    static constexpr int SETTINGS_RIGHT_COLUMN = 28;    // Right column offset within content area

    // Settings UI element dimensions (character widths)
    static constexpr int CHECKBOX_WIDTH = 4;            // "[ ]" or "[X]"
    static constexpr int BUTTON_WIDTH = 3;              // "[-]" or "[+]"
    static constexpr int CHECKBOX_LABEL_SMALL = 12;     // "Visible" width
    static constexpr int CHECKBOX_LABEL_MEDIUM = 15;    // "Show Title" width
    static constexpr int CHECKBOX_LABEL_LARGE = 20;     // "Show Background" width
    static constexpr int CHECKBOX_CLICKABLE = 40;       // Clickable area for data checkboxes
    static constexpr int SCALE_LABEL_WIDTH = 14;        // "Scale: 0.00" width
    static constexpr int SCALE_BUTTON_GAP = 4;          // Gap between scale label and buttons

    // HUD references (non-owning pointers)
    IdealLapHud* m_idealLap;
    LapLogHud* m_lapLog;
    LapConsistencyHud* m_lapConsistency;
    StandingsHud* m_standings;
    PerformanceHud* m_performance;
    TelemetryHud* m_telemetry;
    TimeWidget* m_time;
    PositionWidget* m_position;
    LapWidget* m_lap;
    SessionHud* m_session;
    MapHud* m_mapHud;
    RadarHud* m_radarHud;
    SpeedWidget* m_speed;
    SpeedoWidget* m_speedo;
    TachoWidget* m_tacho;
    TimingHud* m_timing;
    GapBarHud* m_gapBar;
    BarsWidget* m_bars;
    VersionWidget* m_version;
    NoticesWidget* m_notices;
    PitboardHud* m_pitboard;
    RecordsHud* m_records;
    FuelWidget* m_fuel;
    PointerWidget* m_pointer;
    RumbleHud* m_rumble;
    GamepadWidget* m_gamepad;
    LeanWidget* m_lean;
#if GAME_HAS_TYRE_TEMP
    TyreTempWidget* m_tyreTemp;
#endif
    FmxHud* m_fmxHud;

    // Visibility flag
    bool m_bVisible;

    // Profile copy target: -1 = none, 0-3 = specific ProfileType, 4 = all profiles
    int8_t m_copyTargetProfile;
    // Reset radio button states (mutually exclusive)
    bool m_resetProfileConfirmed;
    bool m_resetAllConfirmed;

    // Easter egg click detection (version string)
    static constexpr int EASTER_EGG_CLICKS = 5;
    static constexpr long long EASTER_EGG_TIMEOUT_US = 2000000;  // 2 seconds
    int m_versionClickCount = 0;
    long long m_lastVersionClickTimeUs = 0;

    // Window bounds cache for detecting resize
    // Cache actual pixel dimensions for resize detection
    int m_cachedWindowWidth;
    int m_cachedWindowHeight;

#if GAME_HAS_DISCORD
    // Discord state cache for live status updates
    int m_cachedDiscordState;
    bool m_cachedDiscordEnabled;
#endif

    // Tab system
    enum Tab {
        TAB_GENERAL = 0,       // General settings (preferences, profiles)
        TAB_STANDINGS = 1,     // F1
        TAB_MAP = 2,           // F2
        TAB_RADAR = 3,         // F3
        TAB_LAP_LOG = 4,       // F4
        TAB_LAP_CONSISTENCY = 5, // F5 - Lap Consistency (chart/stats)
        TAB_IDEAL_LAP = 6,     // F6
        TAB_TELEMETRY = 7,     // F7
        TAB_RECORDS = 8,       // F8 - Lap Records (online)
        TAB_PITBOARD = 9,
        TAB_SESSION = 10,      // Session HUD (server info, password)
        TAB_TIMING = 11,       // Timing HUD (center display)
        TAB_GAP_BAR = 12,      // Gap Bar HUD (lap timing comparison)
        TAB_PERFORMANCE = 13,
        TAB_WIDGETS = 14,
        TAB_RIDERS = 15,       // Tracked riders configuration
        TAB_RUMBLE = 16,
        TAB_APPEARANCE = 17,   // Appearance configuration (fonts, colors)
        TAB_HOTKEYS = 18,      // Keyboard/controller hotkey bindings
        TAB_UPDATES = 19,      // Auto-update settings
        TAB_FMX = 20,          // FMX (Freestyle) trick scoring
        TAB_COUNT = 21
    };
    int m_activeTab;

    // Hover tracking for button backgrounds
    int m_hoveredRegionIndex;  // -1 = none hovered
    int m_hoveredHotkeyRow;    // -1 = none, tracks which hotkey row is hovered
    enum class HotkeyColumn { NONE, KEYBOARD, CONTROLLER };
    HotkeyColumn m_hoveredHotkeyColumn;  // Which column is hovered
    float m_hotkeyContentStartY;  // Y position where hotkey rows start (set during rebuild)
    float m_hotkeyRowHeight;      // Row height for hotkey tab (set during rebuild)
    float m_hotkeyKeyboardX;      // X position of keyboard column (set during rebuild)
    float m_hotkeyControllerX;    // X position of controller column (set during rebuild)
    float m_hotkeyFieldCharWidth; // Character width for field calculations (set during rebuild)

    // Tracked Riders tab hover tracking
    int m_hoveredTrackedRiderIndex;    // -1 = none, tracks which tracked rider cell is hovered
    float m_trackedRidersStartY;       // Y position where tracked riders section starts
    float m_trackedRidersCellHeight;   // Height of each tracked rider cell
    float m_trackedRidersCellWidth;    // Width of each tracked rider cell
    float m_trackedRidersStartX;       // X position where tracked riders section starts
    int m_trackedRidersPerRow;         // Number of tracked riders per row

    // Pagination for Riders tab
    int m_serverPlayersPage;           // Current page of server players (0-based)
    int m_trackedRidersPage;           // Current page of tracked riders (0-based)

    // Tooltip support (Phase 2 description system)
    std::string m_hoveredTooltipId;    // Current tooltip ID from hovered region (empty = none)

    // Update checker state tracking (to refresh UI when status changes)
    bool m_wasUpdateCheckerOnCooldown;
    int m_cachedUpdateCheckerStatus;
    int m_cachedUpdateDownloaderState;

    std::vector<ClickRegion> m_clickRegions;

    // Get tooltip ID for a click region type and current active tab
    // Returns empty string if no tooltip is available
    static const char* getTooltipIdForRegion(ClickRegion::Type type, int activeTab);
};
