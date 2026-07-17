// ============================================================================
// hud/settings_hud.h
// Settings interface for configuring which columns/rows are visible in HUDs
// ============================================================================
#pragma once

#include "base_hud.h"
#include "ideal_lap_hud.h"
#include "lap_log_hud.h"
#include "friends_hud.h"
#include "session_charts_hud.h"
#include "standings_hud.h"
#include "performance_hud.h"
#include "pitboard_hud.h"
#include "time_widget.h"
#include "position_widget.h"
#include "lap_widget.h"
#include "session_hud.h"
#include "speed_widget.h"
#include "gear_widget.h"
#include "speedo_widget.h"
#include "tacho_widget.h"
#include "timing_hud.h"
#include "gap_bar_hud.h"
#include "bars_widget.h"
#include "version_widget.h"
#include "notices_hud.h"
#include "fuel_widget.h"
#include "pointer_widget.h"
#include "settings_button_widget.h"
#include "records_hud.h"
#include "gamepad_widget.h"
#include "lean_widget.h"
#include "gforce_widget.h"
#include "compass_widget.h"
#include "clock_widget.h"
#if GAME_HAS_TYRE_TEMP
#include "tyre_temp_widget.h"
#endif
#if GAME_HAS_ECU
#include "ecu_widget.h"
#endif
#include "fmx_hud.h"
#include "event_log_hud.h"
#include <variant>
#include <string>
#include <cmath>
#include <functional>
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
class StatsHud;
class HelmetOverlayHud;
struct SettingsLayoutContext;

class SettingsHud : public BaseHud {
public:
    SettingsHud(IdealLapHud* idealLap, LapLogHud* lapLog, FriendsHud* friends, SessionChartsHud* sessionCharts,
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

    // Persisted active-tab restore. The last-focused tab is saved to the INI
    // ([Profiles] activeTab) and restored on load, so reopening the settings menu lands on
    // the tab the player left it on. Stored by NAME (not index) so it survives tab
    // reordering and is ignored cleanly when the stored tab doesn't exist on this game
    // build (e.g. FMX on karts) - see setActiveTabByName / isTabAvailable.
    const char* getActiveTabName() const;       // display name of the current tab (for save)
    void setActiveTabByName(const char* name);  // restore by name (no-op if unknown/unavailable)

    // Clickable regions for checkboxes, buttons, and scale controls (public for SettingsLayoutContext)
    struct ClickRegion {
        float x, y, width, height;
        enum Type {
            CHECKBOX,                  // Toggle column/row visibility (bitfield)
            // Shared data-driven stepped-value control: the region's steppedIndex
            // selects a SteppedControl descriptor registered at layout time (same
            // rebuild lifecycle as the click regions themselves). Replaces the old
            // one-enum-pair-per-control pattern for plain "step + clamp/wrap +
            // mark dirty" numeric settings; see SettingsLayoutContext::addSteppedControl.
            STEPPED_UP,                // Step the descriptor's value up
            STEPPED_DOWN,              // Step the descriptor's value down
            // Shared data-driven mod-N cycle control: the region's cycleIndex
            // selects a CycleControl descriptor registered at layout time (same
            // rebuild lifecycle as the click regions). Replaces the old
            // one-enum-pair-per-control pattern for plain "value = (value ± 1)
            // mod N + mark dirty" enum/mode cycles; see
            // SettingsLayoutContext::addCycleControl (descriptor overload).
            // Cycles never hold-accelerate (repeat steps are always ±1).
            CYCLE_UP,                  // Cycle the descriptor's value forward
            CYCLE_DOWN,                // Cycle the descriptor's value backward
            GAP_REFERENCE_TOGGLE,      // Cycle gap reference forward (Leader→Player→Auto)
            GAP_REFERENCE_BACK,        // Cycle gap reference backward (Auto→Player→Leader)
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
            STANDINGS_TOP_COUNT_UP,    // Increase pinned top-N positions (StandingsHud)
            STANDINGS_TOP_COUNT_DOWN,  // Decrease pinned top-N positions (StandingsHud)
            LAP_LOG_GAP_ROW_TOGGLE,    // Toggle gap row display (LapLogHud)
            LAP_LOG_HEADERS_TOGGLE,    // Toggle column-header row (LapLogHud)
            FRIENDS_HEADERS_TOGGLE,    // Toggle column-header row (FriendsHud)
            FRIENDS_SELF_TOGGLE,       // Toggle show-myself row (FriendsHud)
            // (Session Charts: Rows to show / Top positions are data-driven
            // STEPPED controls; Colors is a data-driven CYCLE control.)
            MAP_ROTATION_TOGGLE,       // Toggle map rotation mode (MapHud)
            MAP_OUTLINE_UP,            // Increase outline width; from Off enables at min (MapHud)
            MAP_OUTLINE_DOWN,          // Decrease outline width; below min disables (MapHud)
            MAP_MARKERS_TOGGLE,        // Toggle S/F, sector markers and segment lines (MapHud)
            // (Track width / Detail / Marker scale are data-driven STEPPED controls.)
            MAP_RANGE_UP,              // Increase map range / decrease zoom (MapHud)
            MAP_RANGE_DOWN,            // Decrease map range / increase zoom (MapHud)
            MAP_RIDER_SHAPE_UP,        // Cycle rider shape forward (MapHud)
            MAP_RIDER_SHAPE_DOWN,      // Cycle rider shape backward (MapHud)
            MAP_DETAIL_ADAPTIVE_TOGGLE, // Toggle adaptive (screen-normalized) detail (MapHud)
            // (Radar range / Alert distance / Arrow scale / Marker scale are
            // data-driven STEPPED controls.)
            RADAR_PROXIMITY_SHAPE_UP,  // Cycle proximity arrow shape forward (RadarHud)
            RADAR_PROXIMITY_SHAPE_DOWN, // Cycle proximity arrow shape backward (RadarHud)
            RADAR_RIDER_SHAPE_UP,      // Cycle rider shape forward (RadarHud)
            RADAR_RIDER_SHAPE_DOWN,    // Cycle rider shape backward (RadarHud)
            // (Records to show is a data-driven STEPPED control.)
            RECORDS_AUTO_FETCH_TOGGLE, // Toggle auto-fetch on event start (RecordsHud)
            RECORDS_HEADERS_TOGGLE,    // Toggle column-header row (RecordsHud)
            SESSION_ICONS_TOGGLE,       // Toggle icons on/off (SessionHud)
            TIMING_TIME_TOGGLE,        // Toggle the big time row on/off (TimingHud)
            TIMING_GAP_PB_TOGGLE,      // Toggle "Session PB" comparison row (TimingHud)
            TIMING_GAP_IDEAL_TOGGLE,   // Toggle "Ideal" comparison row (TimingHud)
            TIMING_GAP_OVERALL_TOGGLE, // Toggle "Overall" comparison row (TimingHud)
            TIMING_GAP_ALLTIME_TOGGLE, // Toggle "All-Time PB" comparison row (TimingHud)
            TIMING_GAP_RECORD_TOGGLE,  // Toggle "Record" comparison row (TimingHud)
            TIMING_GAP_LASTLAP_TOGGLE, // Toggle "Last Lap" comparison row (TimingHud)
            GAPBAR_ICON_UP,            // Cycle marker icon forward (GapBarHud)
            GAPBAR_ICON_DOWN,          // Cycle marker icon backward (GapBarHud)
            GAPBAR_GAP_TEXT_TOGGLE,    // Toggle gap text visibility (GapBarHud)
            // (Range / Width / Freeze / Marker scale are data-driven STEPPED controls.)
            GAPBAR_GAP_BAR_TOGGLE,     // Toggle gap bar visualization (green/red bars)
            COLOR_CYCLE_PREV,          // Cycle color backward (Appearance tab)
            COLOR_CYCLE_NEXT,          // Cycle color forward (Appearance tab)
            FONT_CATEGORY_PREV,        // Cycle font backward for category (Appearance tab)
            FONT_CATEGORY_NEXT,        // Cycle font forward for category (Appearance tab)
            SPEED_UNIT_TOGGLE,         // Toggle speed unit (mph/km/h)
            FUEL_UNIT_TOGGLE,          // Toggle fuel unit (L/gal)
            TEMP_UNIT_TOGGLE,          // Toggle temperature unit (C/F)
            PB_SCOPE_TOGGLE,           // Toggle personal best scope (Bike/Category)
            DISPLAY_TARGET_TOGGLE,     // Cycle HUD display: In-game / Companion / Both
            GRID_SNAP_TOGGLE,          // Toggle grid snapping for HUD positioning
            SCREEN_CLAMP_TOGGLE,       // Toggle screen clamping for HUD positioning
            MENU_ONLY_CURSOR_TOGGLE,   // Toggle menu-only cursor (controller-as-mouse fix)
            DROP_SHADOW_TOGGLE,        // Toggle drop shadow for text rendering
            TITLE_ICONS_TOGGLE,        // Toggle HUD title identity icons
            UPDATE_CHECK_TOGGLE,       // Toggle automatic update checking
            AUTOSAVE_TOGGLE,           // Toggle auto-save for settings
            SAVE_BUTTON,               // Manual save button (when auto-save is off)
#if GAME_HAS_STEAM_FRIENDS
            STEAM_FRIENDS_TOGGLE,      // Toggle Steam friends integration
#endif
#if GAME_HAS_DISCORD
            DISCORD_TOGGLE,            // Toggle Discord Rich Presence
#endif
#if GAME_HAS_ANALYTICS
            ANALYTICS_TOGGLE,          // Toggle anonymous usage analytics
#endif
#if GAME_HAS_HTTP_SERVER
            WEB_SERVER_TOGGLE,         // Toggle embedded web server
            WEB_SERVER_PORT_DOWN,      // Decrease web server port
            WEB_SERVER_PORT_UP,        // Increase web server port
#endif
            PROFILE_CYCLE_DOWN,        // Cycle to previous profile (Practice/Qualify/Race/Spectate)
            PROFILE_CYCLE_UP,          // Cycle to next profile
            AUTO_SWITCH_TOGGLE,        // Toggle auto-switch for profiles
            SHORT_TIME_FORMAT_TOGGLE,  // Toggle compact time format
            WIDGETS_TOGGLE,            // Toggle all widgets visibility (master switch)
            TAB,                       // Select tab
            CLOSE_BUTTON,              // Close the settings menu
            // Controller/Rumble settings
            // (The per-effect Light/Heavy/Min/Max stepper arrows are data-driven
            // STEPPED_UP/STEPPED_DOWN controls - see SteppedControl and
            // settings_tab_rumble.cpp. Only the toggles keep dedicated types.)
            RUMBLE_TOGGLE,             // Toggle rumble master enable
            RUMBLE_CONTROLLER_UP,      // Cycle controller index up
            RUMBLE_CONTROLLER_DOWN,    // Cycle controller index down
            RUMBLE_BLEND_TOGGLE,       // Toggle blend mode (max vs additive)
            RUMBLE_CRASH_TOGGLE,       // Toggle disable on crash
            RUMBLE_EFFECT_PROFILE_TOGGLE, // Toggle effect profile (global vs per-bike)
            RUMBLE_SUSP_SPLIT_TOGGLE,  // Toggle front/rear split for Bumps
            RUMBLE_LOCKUP_SPLIT_TOGGLE,  // Toggle front/rear split for Lockup
            RUMBLE_HUD_TOGGLE,         // Toggle RumbleHud visibility
            // Helmet Overlay settings
            HELMET_OVERLAY_TOGGLE,     // Master toggle: enable helmet overlay
            HELMET_HELMET_TOGGLE,      // Toggle helmet section on/off
            HELMET_VISOR_MODE_DOWN,    // Cycle visor mode backward (Off/Visor/Goggles)
            HELMET_VISOR_MODE_UP,      // Cycle visor mode forward
            HELMET_UPPER_TEX_DOWN,     // Cycle helmet upper texture variant backward
            HELMET_UPPER_TEX_UP,       // Cycle helmet upper texture variant forward
            HELMET_LOWER_TEX_DOWN,     // Cycle helmet lower texture variant backward
            HELMET_LOWER_TEX_UP,       // Cycle helmet lower texture variant forward
            HELMET_UPPER_OFFSET_DOWN,  // Decrease upper helmet Y offset
            HELMET_UPPER_OFFSET_UP,    // Increase upper helmet Y offset
            HELMET_LOWER_OFFSET_DOWN,  // Decrease lower helmet Y offset
            HELMET_LOWER_OFFSET_UP,    // Increase lower helmet Y offset
            HELMET_TILT_DOWN,          // Decrease helmet tilt strength
            HELMET_TILT_UP,            // Increase helmet tilt strength
            HELMET_VIBRATION_DOWN,     // Decrease helmet vibration strength
            HELMET_VIBRATION_UP,       // Increase helmet vibration strength
            HELMET_VIB_SENS_DOWN,      // Decrease helmet vibration sensitivity
            HELMET_VIB_SENS_UP,        // Increase helmet vibration sensitivity
            HELMET_ZOOM_DOWN,          // Decrease helmet zoom
            HELMET_ZOOM_UP,            // Increase helmet zoom
            HELMET_VISOR_TINT_COLOR_DOWN,   // Cycle visor tint color backward
            HELMET_VISOR_TINT_COLOR_UP,     // Cycle visor tint color forward
            HELMET_VISOR_TINT_OPACITY_DOWN, // Decrease visor tint opacity
            HELMET_VISOR_TINT_OPACITY_UP,   // Increase visor tint opacity
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
            // Director (auto-director, spectate broadcast tool)
            DIRECTOR_ENABLE_TOGGLE,    // Master enable for the auto-director
            DIRECTOR_MINSHOT_UP,       // Increase minimum shot length
            DIRECTOR_MINSHOT_DOWN,     // Decrease minimum shot length
            DIRECTOR_MAXSHOT_UP,       // Increase maximum shot length
            DIRECTOR_MAXSHOT_DOWN,     // Decrease maximum shot length
            DIRECTOR_BATTLEGAP_UP,     // Increase battle gap threshold
            DIRECTOR_BATTLEGAP_DOWN,   // Decrease battle gap threshold
            DIRECTOR_BATTLEMAXPOS_UP,  // Increase battle max-position cutoff
            DIRECTOR_BATTLEMAXPOS_DOWN,// Decrease battle max-position cutoff
            DIRECTOR_RESUME_UP,        // Increase manual-resume timeout
            DIRECTOR_RESUME_DOWN,      // Decrease manual-resume timeout
            DIRECTOR_GAMEPAD_TAKEOVER, // Toggle stick-push gamepad takeover
            DIRECTOR_CAM_FENDER_UP,    // Cycle Fender cam: Off > Front > Rear > Both
            DIRECTOR_CAM_FENDER_DOWN,  // Cycle Fender cam (reverse)
            DIRECTOR_CAM_HELMET_UP,    // Cycle Helmet cam: Off > Helmet 1 > Helmet 2 > Both
            DIRECTOR_CAM_HELMET_DOWN,  // Cycle Helmet cam (reverse)
            // (Forks is an INI-only tunable - no click region.)
            DIRECTOR_FOLLOW_BATTLES,   // Toggle following on-track battles
            DIRECTOR_FOLLOW_INCIDENTS, // Toggle crash/incident interrupts
            DIRECTOR_FOLLOW_FASTEST,   // Toggle fastest-lap celebration cuts
            DIRECTOR_FOLLOW_PACE,      // Toggle non-race hot-lap (fastest-sector) cuts
            DIRECTOR_FINISH_LOCK,      // Toggle final-lap finish lock
            DIRECTOR_CATCH_OVERTAKES,  // Toggle on-track overtake rewards
            DIRECTOR_FOLLOW_LAPPERS,   // Toggle following a front-runner lapping backmarkers
            DIRECTOR_FOLLOW_DROPS,     // Toggle following a rider tumbling down the order
            DIRECTOR_VARIETY_UP,       // Increase variety cadence (more solo before an onboard)
            DIRECTOR_VARIETY_DOWN,     // Decrease variety cadence
            DIRECTOR_HOLD_UP,          // Increase the shared story hold
            DIRECTOR_HOLD_DOWN,        // Decrease the shared story hold
            // (Incident hold cap is an INI-only tunable now - no click region.)
            DIRECTOR_HUD_VISIBLE,      // Toggle the on-screen director status button
            // FMX HUD
            // (Trick stack rows is a data-driven STEPPED control.)
            FMX_DEBUG_TOGGLE,          // Toggle FMX debug logging
            // Stats HUD
            STATS_SHOW_LAP_TOGGLE,     // Toggle lap column
            STATS_SHOW_SESSION_TOGGLE, // Toggle session column
            STATS_SHOW_ALLTIME_TOGGLE, // Toggle all-time column
            // Clock Widget
            CLOCK_FORMAT_TOGGLE,       // Toggle 12h/24h format (ClockWidget)
            // Event Log HUD
            EVENT_LOG_ICONS_TOGGLE,    // Toggle event type icons (EventLogHud)
            // Standings tab toggles
            LIVE_GAPS_TOGGLE,          // Toggle live gap display in races (StandingsHud per-profile member)
            FILTER_DNS_TOGGLE,         // Toggle DNS rider filtering (PluginData global)
            HEADERS_TOGGLE,            // Toggle column-header row in the standings (StandingsHud)
            SESSION_INFO_TOGGLE,       // Toggle session-info row (clock/laps/overtime) in the standings (StandingsHud)
            // Help & Community links (General tab footer)
            OPEN_LINK_DOCS,            // Open documentation site
            OPEN_LINK_COMMUNITY,       // Open community forum
            OPEN_LINK_KOFI             // Open Ko-fi donation page
        } type;

        // Type-safe variant instead of unsafe union (C++17)
        // Holds different pointer types based on ClickRegion::Type
        using TargetPointer = std::variant<
            std::monostate,                              // Empty state (for types that don't need a pointer)
            uint32_t*,                                   // For CHECKBOX (targetBitfield)
            bool*,                                       // For bool toggle regions
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
        int steppedIndex = -1;     // Index into m_steppedControls (for STEPPED_UP/STEPPED_DOWN)
        int cycleIndex = -1;       // Index into m_cycleControls (for CYCLE_UP/CYCLE_DOWN)

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

        // Constructor for bool* toggle regions
        ClickRegion(float _x, float _y, float _width, float _height, Type _type,
                   bool* boolPtr, BaseHud* _targetHud)
            : x(_x), y(_y), width(_width), height(_height), type(_type),
              targetPointer(boolPtr), flagBit(0), isRequired(false),
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

    // Descriptor for the shared STEPPED_UP/STEPPED_DOWN click regions: what to
    // step, how, within which bounds, and which HUD to mark dirty. Registered by
    // SettingsLayoutContext::addSteppedControl during rebuildRenderData (layout
    // time) into m_steppedControls, which is cleared and rebuilt together with
    // m_clickRegions - so the index stored in ClickRegion::steppedIndex is stable
    // for exactly as long as the region itself is. The value pointers follow the
    // same lifetime rules as ClickRegion's bitfield/bool pointers (they point at
    // long-lived HUD members).
    struct SteppedControl {
        enum class Kind {
            WRAP_INT,      // applyAcceleratedWrap  (wraps at the bounds)
            CLAMP_INT,     // applyAcceleratedClamp (clamps at the bounds)
            FIXED_INT,     // Fixed integer step, deliberately NO hold acceleration,
                           // clamped to [lo,hi] (the legacy plain ++/-- count steppers:
                           // records to show, chart row counts)
            STEP_FLOAT,    // applyAcceleratedStep  (clamped toward the pressed direction)
            PERCENT_FLOAT, // Rumble-strength stepper: accelerated 1% step, clamp
                           // [flo,fhi], then round to hundredths (NOT the STEP_FLOAT
                           // snap-to-accelerated-grid - preserves the legacy sequences)
            FIXED_FLOAT    // Fixed step, deliberately NO hold acceleration, clamped to
                           // [flo,fhi]; loLink (when set) overrides flo with a live value
        };
        Kind kind = Kind::WRAP_INT;
        int* intValue = nullptr;      // WRAP_INT / CLAMP_INT target
        float* floatValue = nullptr;  // float-kind target
        int step = 1, lo = 0, hi = 0;             // int kinds
        float fstep = 0.0f, flo = 0.0f, fhi = 0.0f; // float kinds
        const float* loLink = nullptr; // FIXED_FLOAT: dynamic lower bound (e.g. a rumble
                                       // effect's max input clamps at its live minInput)
        BaseHud* dirtyHud = nullptr;  // HUD to mark dirty after the change
        // Optional extra work, run after the step and before the dirty calls (e.g.
        // the Rumble tab marking the per-bike profile dirty / latching a split
        // flag). Rebuilt in lockstep with the descriptor vector, so captures follow
        // the same lifetime rules as the value pointers.
        std::function<void()> postStep;
        // Optional validity predicate, checked BEFORE applying the step: when it
        // returns false the click is swallowed and the settings layout is marked
        // dirty so the next frame rebuilds against the right target. Guards
        // descriptors whose value pointers bind to state that can be swapped out
        // from under an open menu — e.g. the Rumble tab's per-bike profile, which
        // changes when the player swaps bikes (the old pointers would silently
        // edit the PREVIOUS bike's profile). Swallowing is correct: the control
        // the user clicked no longer shows the truth.
        std::function<bool()> valid;

        static SteppedControl wrapInt(int* value, int step, int lo, int hi, BaseHud* dirtyHud) {
            SteppedControl c; c.kind = Kind::WRAP_INT; c.intValue = value;
            c.step = step; c.lo = lo; c.hi = hi; c.dirtyHud = dirtyHud; return c;
        }
        static SteppedControl clampInt(int* value, int step, int lo, int hi, BaseHud* dirtyHud) {
            SteppedControl c; c.kind = Kind::CLAMP_INT; c.intValue = value;
            c.step = step; c.lo = lo; c.hi = hi; c.dirtyHud = dirtyHud; return c;
        }
        static SteppedControl fixedInt(int* value, int step, int lo, int hi, BaseHud* dirtyHud) {
            SteppedControl c; c.kind = Kind::FIXED_INT; c.intValue = value;
            c.step = step; c.lo = lo; c.hi = hi; c.dirtyHud = dirtyHud; return c;
        }
        static SteppedControl stepFloat(float* value, float step, float lo, float hi, BaseHud* dirtyHud) {
            SteppedControl c; c.kind = Kind::STEP_FLOAT; c.floatValue = value;
            c.fstep = step; c.flo = lo; c.fhi = hi; c.dirtyHud = dirtyHud; return c;
        }
        static SteppedControl percentFloat(float* value, BaseHud* dirtyHud) {
            SteppedControl c; c.kind = Kind::PERCENT_FLOAT; c.floatValue = value;
            c.fstep = 0.01f; c.flo = 0.0f; c.fhi = 1.0f; c.dirtyHud = dirtyHud; return c;
        }
        static SteppedControl fixedFloat(float* value, float step, float lo, float hi, BaseHud* dirtyHud) {
            SteppedControl c; c.kind = Kind::FIXED_FLOAT; c.floatValue = value;
            c.fstep = step; c.flo = lo; c.fhi = hi; c.dirtyHud = dirtyHud; return c;
        }
        static SteppedControl fixedFloatDynamicLo(float* value, float step, const float* loLink, float hi, BaseHud* dirtyHud) {
            SteppedControl c; c.kind = Kind::FIXED_FLOAT; c.floatValue = value;
            c.fstep = step; c.loLink = loLink; c.fhi = hi; c.dirtyHud = dirtyHud; return c;
        }
    };

    // Descriptor for the shared CYCLE_UP/CYCLE_DOWN click regions: a plain mod-N
    // state cycle ("value = (value ± 1) mod N; mark dirty"). Registered by the
    // SettingsLayoutContext::addCycleControl descriptor overload during
    // rebuildRenderData into m_cycleControls, which is cleared and rebuilt in
    // lockstep with m_clickRegions — ClickRegion::cycleIndex is stable exactly as
    // long as the region itself is. get/set use a 0-based state index; enums whose
    // VISUAL cycle order differs from their numeric order (e.g. StandingsHud's
    // PosGainMode) map inside their get/set lambdas. Deliberately NO hold
    // acceleration: cycles step ±1 per click/repeat, whatever the hold tier.
    struct CycleControl {
        std::function<int()> get;        // current 0-based state index
        std::function<void(int)> set;    // store the new state index
        int count = 1;                   // number of states (the modulus N)
        BaseHud* dirtyHud = nullptr;     // HUD to mark dirty after the change
        // Optional extra work, run after set() and before the dirty calls (e.g.
        // Stats resetting its auto-show latches, Friends clearing its transient
        // ON_JOIN state, Standings stopping in-flight animations on OFF). Same
        // lifetime rules as the get/set captures.
        std::function<void()> postStep;

        // The common case: cycle an enum (or integral) member of a HUD through
        // its full 0..count-1 numeric range. Works for any enum whose visual
        // cycle order equals its numeric order. Lambdas formed here have the
        // access rights of SettingsHud (nested class of a friend).
        template <typename OwnerT, typename EnumT>
        static CycleControl enumMember(OwnerT* owner, EnumT OwnerT::* member,
                                       int count, BaseHud* dirtyHud) {
            CycleControl c;
            c.get = [owner, member]() { return static_cast<int>(owner->*member); };
            c.set = [owner, member](int v) { owner->*member = static_cast<EnumT>(v); };
            c.count = count;
            c.dirtyHud = dirtyHud;
            return c;
        }
    };

    // Friend declarations for settings layout system
    friend struct SettingsLayoutContext;

    // Static tab rendering functions (inherit friend access to HUD classes)
    // Implemented in separate files under hud/settings/
    static BaseHud* renderTabIdealLap(SettingsLayoutContext& ctx);
    static BaseHud* renderTabLapLog(SettingsLayoutContext& ctx);
    static BaseHud* renderTabSessionCharts(SettingsLayoutContext& ctx);
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
    static BaseHud* renderTabNotices(SettingsLayoutContext& ctx);
    static BaseHud* renderTabRumble(SettingsLayoutContext& ctx);
    static BaseHud* renderTabHelmet(SettingsLayoutContext& ctx);
    static BaseHud* renderTabGeneral(SettingsLayoutContext& ctx);
    static BaseHud* renderTabFriends(SettingsLayoutContext& ctx);
    static BaseHud* renderTabAppearance(SettingsLayoutContext& ctx);
    static BaseHud* renderTabHotkeys(SettingsLayoutContext& ctx);
    static BaseHud* renderTabRiders(SettingsLayoutContext& ctx);
    static BaseHud* renderTabUpdates(SettingsLayoutContext& ctx);
    static BaseHud* renderTabFmx(SettingsLayoutContext& ctx);
    static BaseHud* renderTabStats(SettingsLayoutContext& ctx);
    static BaseHud* renderTabEventLog(SettingsLayoutContext& ctx);
    static BaseHud* renderTabDirector(SettingsLayoutContext& ctx);

    // Static click handler functions (implemented in tab files)
    // Return true if the click was handled, false otherwise
    bool handleClickTabMap(const ClickRegion& region);
    bool handleClickTabRadar(const ClickRegion& region);
    bool handleClickTabTiming(const ClickRegion& region);
    bool handleClickTabGapBar(const ClickRegion& region);
    bool handleClickTabStandings(const ClickRegion& region);
    bool handleClickTabRumble(const ClickRegion& region);
    bool handleClickTabHelmet(const ClickRegion& region);
    bool handleClickTabAppearance(const ClickRegion& region);
    bool handleClickTabGeneral(const ClickRegion& region);
    bool handleClickTabFriends(const ClickRegion& region);
    bool handleClickTabHotkeys(const ClickRegion& region);
    bool handleClickTabRiders(const ClickRegion& region);
    bool handleClickTabRecords(const ClickRegion& region);
    bool handleClickTabSession(const ClickRegion& region);
    bool handleClickTabLapLog(const ClickRegion& region);
    bool handleClickTabUpdates(const ClickRegion& region);
    bool handleClickTabFmx(const ClickRegion& region);
    bool handleClickTabStats(const ClickRegion& region);
    bool handleClickTabEventLog(const ClickRegion& region);
    // (Notices has no tab-specific click handler: its Duration control is a
    // shared STEPPED control and the rest uses the common handlers.)

    // HUD getter methods (for tab rendering functions)
    IdealLapHud* getIdealLapHud() const { return m_idealLap; }
    LapLogHud* getLapLogHud() const { return m_lapLog; }
    FriendsHud* getFriendsHud() const { return m_friends; }
    SessionChartsHud* getSessionChartsHud() const { return m_sessionCharts; }
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
    GearWidget* getGearWidget() const { return m_gear; }
    SpeedoWidget* getSpeedoWidget() const { return m_speedo; }
    TachoWidget* getTachoWidget() const { return m_tacho; }
    TimingHud* getTimingHud() const { return m_timing; }
    GapBarHud* getGapBarHud() const { return m_gapBar; }
    BarsWidget* getBarsWidget() const { return m_bars; }
    VersionWidget* getVersionWidget() const { return m_version; }
    NoticesHud* getNoticesHud() const { return m_notices; }
    PitboardHud* getPitboardHud() const { return m_pitboard; }
    RecordsHud* getRecordsHud() const { return m_records; }
    FuelWidget* getFuelWidget() const { return m_fuel; }
    PointerWidget* getPointerWidget() const { return m_pointer; }
    SettingsButtonWidget* getSettingsButtonWidget() const { return m_settingsButton; }
    RumbleHud* getRumbleHud() const { return m_rumble; }
    HelmetOverlayHud* getHelmetOverlayHud() const { return m_helmetOverlay; }
    GamepadWidget* getGamepadWidget() const { return m_gamepad; }
    LeanWidget* getLeanWidget() const { return m_lean; }
    GForceWidget* getGForceWidget() const { return m_gforce; }
    CompassWidget* getCompassWidget() const { return m_compass; }
    ClockWidget* getClockWidget() const { return m_clock; }
#if GAME_HAS_TYRE_TEMP
    TyreTempWidget* getTyreTempWidget() const { return m_tyreTemp; }
#endif
#if GAME_HAS_ECU
    EcuWidget* getEcuWidget() const { return m_ecu; }
#endif
    FmxHud* getFmxHud() const { return m_fmxHud; }
    class StatsHud* getStatsHud() const { return m_statsHud; }
    EventLogHud* getEventLogHud() const { return m_eventLog; }

protected:
    void rebuildLayout() override;

#if defined(MXBMRP3_TEST_BUILD)
public:
    // Headless click seam (never in a shipping build): the settings-click path is
    // otherwise reachable only via real OS mouse input, which the Wine harness
    // can't synthesize. testClickStepped routes a click through the REAL path
    // (handleClick: hit-test -> dispatchRegion -> applySteppedControl) at the
    // center of the index-th built STEPPED_UP/STEPPED_DOWN region, with the
    // hold-repeat counter forced so the acceleration tiers (1/5/10) are drivable.
    // Regions are counted in layout order on the ACTIVE tab. Returns false when
    // no such region exists (e.g. wrong tab, index out of range).
    int testSteppedRegionCount(bool up) const;
    bool testClickStepped(int index, bool up, int holdRepeats);
    // Same seam for the shared CYCLE_UP/CYCLE_DOWN regions (no hold tier — cycles
    // never accelerate).
    int testCycleRegionCount(bool up) const;
    bool testClickCycle(int index, bool up);
#endif

private:
    void rebuildRenderData() override;
    void handleClick(float mouseX, float mouseY);
    void dispatchRegion(const ClickRegion& region, bool skipSave = false);  // Dispatch a click region directly
    void handleRightClick(float mouseX, float mouseY);  // Right-click for shape cycling
    void resetToDefaults();        // Reset all profiles to defaults
    void resetCurrentTab();        // Reset current tab for current profile
    void resetCurrentProfile();    // Reset all HUDs for current profile

    // Click handlers - common handlers used by multiple tabs
    void applySteppedControl(const ClickRegion& region, bool increase);  // STEPPED_UP/STEPPED_DOWN
    void applyCycleControl(const ClickRegion& region, bool forward);     // CYCLE_UP/CYCLE_DOWN
    void handleCheckboxClick(const ClickRegion& region);
    void handleHudToggleClick(const ClickRegion& region);
    void handleTitleToggleClick(const ClickRegion& region);
    void handleOpacityClick(const ClickRegion& region, bool increase);
    void handleScaleClick(const ClickRegion& region, bool increase);
    void handleTabClick(const ClickRegion& region);
    void handleCloseButtonClick();
    const char* getTabName(int tabIndex) const;  // Get display name for a tab
    // Whether a tab is selectable on this build: a real tab id (0..TAB_COUNT-1) whose
    // game-gated backing HUD is registered. Single source of truth for the tab-list skips
    // and the persisted-tab restore validation, so they can't drift.
    bool isTabAvailable(int tabId) const;
    // Note: Tab-specific handlers inlined into settings_tab_*.cpp files

    // ------------------------------------------------------------------
    // Per-tab descriptor registry (mirrors core/settings_hud_registry).
    // ONE static table drives every per-tab dispatch site: the tab-list
    // render loop (display order, name, tooltip id, backing-HUD checkbox,
    // section icon), game gating (isTabAvailable), the active-tab render
    // routing, the click routing, and the per-tab reset. Adding a tab =
    // one Tab enum value + ONE row in s_tabRegistry (settings_hud.cpp) —
    // there is no separate switch to keep in step.
    // ------------------------------------------------------------------
    struct TabDescriptor {
        int tabId;                                      // Tab enum value, or TAB_SECTION_* marker
        const char* name;                               // Display name (persisted via [Profiles] activeTab - keep stable)
        const char* tooltipId;                          // Lowercase id for tab description lookup (TooltipManager)
        BaseHud* (*hud)(const SettingsHud&);            // Backing HUD for the tab-list checkbox; null = master-toggle/section tab
        bool gameGated;                                 // Tab unavailable when hud() returns null (Records/FMX/Friends)
        BaseHud* (*render)(SettingsLayoutContext&);     // Tab renderer (static member fn in settings_tab_*.cpp)
        bool (SettingsHud::*click)(const ClickRegion&); // Tab click handler; null = common handlers only
        const char* resetHud;                           // HUD name for the standard per-tab reset (resetHudsToFactoryDefaults); null = none
        void (SettingsHud::*resetExtra)();              // Custom reset steps (run after resetHud); null = none
        const char* sectionIcon;                        // Identity icon for non-toggleable section tabs; null = none
    };
    // Rows are in VISUAL ORDER - the tab-list render loop iterates this table
    // directly, so row position = position in the tab column. The negative
    // TAB_SECTION_* rows render the section headers/controls between groups.
    static const TabDescriptor s_tabRegistry[];
    static const TabDescriptor* findTabDescriptor(int tabId);

    // Section markers used in s_tabRegistry (negative = not a real tab)
    static constexpr int TAB_SECTION_GLOBAL = -1;
    static constexpr int TAB_SECTION_PROFILE = -2;
    static constexpr int TAB_SECTION_ELEMENTS = -3;

    // Per-tab custom reset bodies (referenced by s_tabRegistry rows; the simple
    // "reset this HUD's section" tabs use TabDescriptor::resetHud instead).
    // Implemented in settings_hud_input.cpp next to resetCurrentTab().
    void resetTabGeneral();
    void resetTabAppearance();
    void resetTabStandingsExtra();   // DNS filter (global, outside the HUD snapshot)
    void resetTabRecordsExtra();     // provider + auto-fetch (global [General] keys)
    void resetTabWidgets();
    void resetTabRumble();
    void resetTabHelmet();
    void resetTabHotkeys();
    void resetTabUpdates();
    void resetTabRiders();
    void resetTabDirector();

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
    FriendsHud* m_friends;
    SessionChartsHud* m_sessionCharts;
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
    GearWidget* m_gear;
    SpeedoWidget* m_speedo;
    TachoWidget* m_tacho;
    TimingHud* m_timing;
    GapBarHud* m_gapBar;
    BarsWidget* m_bars;
    VersionWidget* m_version;
    NoticesHud* m_notices;
    PitboardHud* m_pitboard;
    RecordsHud* m_records;
    FuelWidget* m_fuel;
    PointerWidget* m_pointer;
    SettingsButtonWidget* m_settingsButton;
    RumbleHud* m_rumble;
    HelmetOverlayHud* m_helmetOverlay;
    GamepadWidget* m_gamepad;
    LeanWidget* m_lean;
    GForceWidget* m_gforce;
    CompassWidget* m_compass;
    ClockWidget* m_clock;
#if GAME_HAS_TYRE_TEMP
    TyreTempWidget* m_tyreTemp;
#endif
#if GAME_HAS_ECU
    EcuWidget* m_ecu;
#endif
    FmxHud* m_fmxHud;
    StatsHud* m_statsHud;
    EventLogHud* m_eventLog;

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
        TAB_SESSION_CHARTS = 5,   // F5 - Session Charts (position/trace/gap/pace)
        TAB_IDEAL_LAP = 6,     // F6
        TAB_TELEMETRY = 7,     // F7
        TAB_RECORDS = 8,       // F8 - Lap Records (online)
        TAB_PITBOARD = 9,
        TAB_SESSION = 10,      // Session HUD (server info, password)
        TAB_TIMING = 11,       // Timing HUD (center display)
        TAB_GAP_BAR = 12,      // Gap Bar HUD (lap timing comparison)
        TAB_PERFORMANCE = 13,
        TAB_WIDGETS = 14,
        TAB_NOTICES = 15,      // Notices HUD (warnings, PB notifications)
        TAB_RIDERS = 16,       // Tracked riders configuration
        TAB_RUMBLE = 17,
        TAB_APPEARANCE = 18,   // Appearance configuration (fonts, colors)
        TAB_HOTKEYS = 19,      // Keyboard/controller hotkey bindings
        TAB_UPDATES = 20,      // Auto-update settings
        TAB_FMX = 21,          // FMX (Freestyle) trick scoring
        TAB_STATS = 22,        // Stats tracking (laps, crashes, PBs)
        TAB_EVENT_LOG = 23,    // Event Log (race event feed)
        TAB_HELMET = 24,       // Helmet overlay (immersion)
        TAB_FRIENDS = 25,      // Friends (Steam friends in-game)
        TAB_DIRECTOR = 26,     // Auto-director (spectate broadcast tool)
        TAB_COUNT = 27
    };
    int m_activeTab;

    // Mark settings dirty after a settings-panel edit (always — independent of auto-save, so
    // the Save button reflects unsaved changes in manual mode too). The write is deferred to a
    // leave-track transition (auto-save) or the Save button, so it never spikes a gameplay frame.
    void markSettingsDirty();

    // Last-seen SettingsManager dirty state, so update() can rebuild the Save button the frame
    // the unsaved-changes state flips (e.g. a HUD dragged while the panel is open).
    bool m_lastSettingsDirty = false;

    // Hover tracking for button backgrounds
    int m_hoveredRegionIndex;  // -1 = none hovered
    int m_hoveredHotkeyRow;    // -1 = none, tracks which hotkey row is hovered
    enum class HotkeyColumn { NONE, KEYBOARD, CONTROLLER };
    HotkeyColumn m_hoveredHotkeyColumn;  // Which column is hovered
    float m_hotkeyContentStartY;  // Y position where hotkey rows start (set during rebuild)
    float m_hotkeyRowHeight;      // Row height for hotkey tab (set during rebuild)
    std::vector<float> m_hotkeyRowTops;  // Top Y of each rendered hotkey row (set during rebuild);
                                         // indexed to match the hover row index, accounts for spacer gaps
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

    // Hold-to-repeat acceleration for click regions
    int m_holdRegionIndex;         // Index of region being held (-1 = none)
    int m_holdRepeatCount;         // Number of repeats fired so far
    bool m_holdSavePending;        // True if auto-save needed when hold ends
    std::chrono::steady_clock::time_point m_holdStartTime{};   // When the button was first pressed
    std::chrono::steady_clock::time_point m_holdLastRepeat{};  // When the last repeat fired

    // Click-on-release for non-repeatable buttons/toggles: a press arms the region, the
    // action fires on release only if the cursor is still over it (so a press can be
    // aborted by sliding off). Repeatable steppers still fire on press + hold-repeat.
    bool m_leftPressArmed = false;
    float m_pressX = 0.0f;
    float m_pressY = 0.0f;
    // Index of the clickable region at (x,y), skipping TOOLTIP_ROW; -1 if none.
    int findClickRegionAt(float x, float y) const;

    // Returns true if a click region type supports hold-to-repeat
    static bool isRepeatableRegionType(ClickRegion::Type type);

    // Returns step multiplier for hold-to-repeat acceleration:
    // repeats 0-5: 1x (1%), repeats 6-15: 5x (5%), repeats 16+: 10x (10%)
    int getHoldStepMultiplier() const {
        if (m_holdRepeatCount < 6) return 1;
        if (m_holdRepeatCount < 16) return 5;
        return 10;
    }

    // Apply accelerated step to a float value and snap to the nearest step multiple.
    // Produces clean sequences like: 1,2,3,4,5,10,15,20,25,30,40,50,60...
    float applyAcceleratedStep(float current, float baseStep, bool increase) const {
        int mult = getHoldStepMultiplier();
        float step = baseStep * mult;
        float raw = current + (increase ? step : -step);
        // Snap to nearest multiple of step for clean round numbers
        return std::round(raw / step) * step;
    }

    // Step a wrapping integer cycle (durations, etc.) with hold-acceleration.
    // The hold multiplier accelerates the step, but the result is clamped to
    // [lo, hi] so a fast hold can't overshoot a bound; pressing again while
    // already sitting on a bound wraps to the far end (preserving the cycle feel).
    int applyAcceleratedWrap(int current, int baseStep, int lo, int hi, bool increase) const {
        int step = baseStep * getHoldStepMultiplier();
        if (increase) {
            if (current >= hi) return lo;                 // at top -> wrap to bottom
            int next = current + step;
            return next > hi ? hi : next;                 // accelerate, clamp at top
        } else {
            if (current <= lo) return hi;                 // at bottom -> wrap to top
            int next = current - step;
            return next < lo ? lo : next;                 // accelerate, clamp at bottom
        }
    }

    // Step a clamped integer count control (rows, events, ...) with hold-acceleration.
    // The integer analog of applyAcceleratedStep, but it simply clamps to [lo, hi]
    // with no wrap and no grid snapping (every integer in range is a valid count).
    int applyAcceleratedClamp(int current, int baseStep, int lo, int hi, bool increase) const {
        int step = baseStep * getHoldStepMultiplier();
        int next = current + (increase ? step : -step);
        if (next < lo) next = lo;
        if (next > hi) next = hi;
        return next;
    }

    // Stats tab periodic refresh timer (epoch default triggers immediate first refresh)
    std::chrono::steady_clock::time_point m_lastStatsRefresh{};

    std::vector<ClickRegion> m_clickRegions;

    // Stepped-control descriptors referenced by ClickRegion::steppedIndex.
    // Rebuilt in lockstep with m_clickRegions (rebuildRenderData / hide), never
    // touched per frame - see SteppedControl.
    std::vector<SteppedControl> m_steppedControls;

    // Cycle-control descriptors referenced by ClickRegion::cycleIndex. Same
    // lifecycle as m_steppedControls - see CycleControl.
    std::vector<CycleControl> m_cycleControls;

    // Get tooltip ID for a click region type and current active tab
    // Returns empty string if no tooltip is available
    static const char* getTooltipIdForRegion(ClickRegion::Type type, int activeTab);
};
