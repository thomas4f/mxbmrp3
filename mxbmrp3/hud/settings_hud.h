// ============================================================================
// hud/settings_hud.h
// Settings interface for configuring which columns/rows are visible in HUDs
// ============================================================================
// file-budget: 1500 one class definition; its implementation is already split across settings_hud_*.cpp
#pragma once

#include "base_hud.h"
#include "../core/layout_metrics.h"
#include "settings/settings_controls.h"
#include <variant>
#include <string>
#include <cmath>
#include <functional>
#include "../core/plugin_constants.h"
#include "../core/color_config.h"
#include "../core/font_config.h"
#include "../core/xinput_reader.h"
#include "../core/hotkey_config.h"

// THE HUD TYPES ARE FORWARD-DECLARED, NOT INCLUDED. This class only ever holds
// them by pointer (the m_* targets the tabs edit and their getters), so the
// declaration needs the names and nothing else. Including the HUD headers here
// would make every includer of this header -- the settings serializers,
// HudManager, the test hooks -- recompile whenever ANY HUD header changed. The
// .cpp files that call into a concrete HUD include that HUD's header
// themselves; the compiler names the one that is missing.
class IdealLapHud;
class LapLogHud;
class FriendsHud;
class SessionChartsHud;
class StandingsHud;
class PerformanceHud;
class PitboardHud;
class TimeWidget;
class PositionWidget;
class LapWidget;
class SessionHud;
class SpeedWidget;
class GearWidget;
class CrashWidget;
class SpeedoWidget;
class TachoWidget;
class TimingHud;
class GapBarHud;
class BarsWidget;
class VersionWidget;
class NoticesHud;
class FuelWidget;
class PointerWidget;
class SettingsButtonWidget;
class RecordsHud;
class GamepadWidget;
class LeanWidget;
class GForceWidget;
class CompassWidget;
class ClockWidget;
class TyreTempWidget;
class EcuWidget;
class FmxHud;
class EventLogHud;
class MapHud;
class RadarHud;
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
                );
    virtual ~SettingsHud() = default;

    void update() override;
    bool handlesDataType(DataChangeType /*dataType*/) const override { return false; }

    // The settings panel never drop-shadows, whatever the user set globally.
    //
    // A shadow buys legibility against the GAME, behind text that has no panel under
    // it. This one always draws its own background, so every shadow here is a second
    // string rendered underneath an opaque surface -- invisible work. And it is the
    // heaviest string emitter in the plugin by a wide margin (84 against the next
    // panel's 44), so it would pay that twice over more than anything else.
    //
    // It also reads better: a shadow behind dense UI text on a solid panel muddies
    // the glyph edges rather than separating them from anything.
    bool alwaysSkipDropShadow() const override { return true; }

    // The settings menu is a page, not a corner label: centred on screen, the only
    // thing you are looking at, and its caption is a heading. See centreTitle.
    bool centreTitle() const override { return true; }

    // Show/hide the settings panel
    void show();
    void hide();
    bool isVisible() const { return m_bVisible; }
    // The HUD the panel is currently talking about: the active tab's backing HUD,
    // or nullptr for a tab that has none (General, Appearance, About...). What
    // HudManager previews each frame, so a HUD that would otherwise be empty can
    // still be dragged into place. See BaseHud::isPreviewing.
    BaseHud* activeTabHud() const;

    // Open settings panel directly to Updates tab
    void showUpdatesTab();
    // The toast card's click: the menu, on the Achievements tab, on the page
    // holding that row (the tab finds it as it lays the page out); no row
    // (the summary card) opens the first page.
    void showAchievementsTab(int catalogueIndex = -1);
    // The menu, on `tabId` (a system toast's click). An unavailable tab opens
    // the menu where it was.
    void showTab(int tabId);
    // The Version popup's "What's New": the menu on the first tab with a live
    // "New" marker (WhatsNew::firstTabWithNews), else where it was.
    void showWhatsNew();
    // A tab's sidebar name, for a system toast about it ("Standings hidden").
    const char* tabTitle(int tabId) const { return getTabName(tabId); }

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
            // rebuild lifecycle as the click regions themselves). One descriptor,
            // not one enum pair per control, for plain "step + clamp/wrap + mark
            // dirty" numeric settings; see SettingsLayoutContext::addSteppedControl.
            STEPPED_UP,                // Step the descriptor's value up
            STEPPED_DOWN,              // Step the descriptor's value down
            // Shared data-driven mod-N cycle control: the region's cycleIndex
            // selects a CycleControl descriptor registered at layout time (same
            // rebuild lifecycle as the click regions). One descriptor, not one
            // enum pair per control, for plain "value = (value ± 1) mod N + mark
            // dirty" enum/mode cycles; see
            // SettingsLayoutContext::addCycleControl (descriptor overload).
            // Cycles never hold-accelerate (repeat steps are always ±1).
            CYCLE_UP,                  // Cycle the descriptor's value forward
            CYCLE_DOWN,                // Cycle the descriptor's value backward
            RESET_BUTTON,              // Unified reset button (General tab) - action depends on checkbox
            RESET_TAB_BUTTON,          // Reset current tab to defaults (footer)
            COPY_BUTTON,               // Execute copy to selected target profile(s)
            RESET_PROFILE_CHECKBOX,    // Radio-style checkbox for Reset Profile
            RESET_ALL_CHECKBOX,        // Radio-style checkbox for Reset All Profiles
            HUD_TOGGLE,                // Toggle entire HUD visibility
            TITLE_TOGGLE,              // Toggle HUD title
            // GamepadWidget only: its background is a PACK (art + geometry), not a
            // variant of a shared texture, so its Texture column cycles pack names.
            // PitboardHud only, same reasoning: its background is a board PACK.
            // TachoWidget and SpeedoWidget, same reasoning again: the dial FACE is a
            // pack (art plus the range and sweep that place the needle on it), and
            // each widget carries its own selection so a set can be mixed.
            LAP_LOG_GAP_ROW_TOGGLE,    // Toggle gap row display (LapLogHud)
            LAP_LOG_HEADERS_TOGGLE,    // Toggle column-header row (LapLogHud)
            FRIENDS_HEADERS_TOGGLE,    // Toggle column-header row (FriendsHud)
            FRIENDS_SELF_TOGGLE,       // Toggle show-myself row (FriendsHud)
            // (Session Charts: Rows to show / Top positions are data-driven
            // STEPPED controls; Colors is a data-driven CYCLE control.)
            MAP_ROTATION_TOGGLE,       // Toggle map rotation mode (MapHud)
            MAP_MARKERS_TOGGLE,        // Toggle S/F, sector markers and segment lines (MapHud)
            // (Track width / Detail / Marker scale are data-driven STEPPED controls.)
            MAP_DETAIL_ADAPTIVE_TOGGLE, // Toggle adaptive (screen-normalized) detail (MapHud)
            // (Radar range / Alert distance / Arrow scale / Marker scale are
            // data-driven STEPPED controls.)
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
            GAPBAR_GAP_TEXT_TOGGLE,    // Toggle gap text visibility (GapBarHud)
            // (Range / Width / Freeze / Marker scale are data-driven STEPPED controls.)
            GAPBAR_GAP_BAR_TOGGLE,     // Toggle gap bar visualization (green/red bars)
            SPEED_UNIT_TOGGLE,         // Toggle speed unit (mph/km/h)
            FUEL_UNIT_TOGGLE,          // Toggle fuel unit (L/gal)
            TEMP_UNIT_TOGGLE,          // Toggle temperature unit (C/F)
            PB_SCOPE_TOGGLE,           // Toggle personal best scope (Bike/Category)
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
            RUMBLE_BLEND_TOGGLE,       // Toggle blend mode (max vs additive)
            RUMBLE_CRASH_TOGGLE,       // Toggle disable on crash
            RUMBLE_EFFECT_PROFILE_TOGGLE, // Toggle effect profile (global vs per-bike)
            RUMBLE_SUSP_SPLIT_TOGGLE,  // Toggle front/rear split for Bumps
            RUMBLE_LOCKUP_SPLIT_TOGGLE,  // Toggle front/rear split for Lockup
            RUMBLE_HUD_TOGGLE,         // Toggle RumbleHud visibility
            // Helmet Overlay settings
            HELMET_OVERLAY_TOGGLE,     // Master toggle: enable helmet overlay
            HELMET_HELMET_TOGGLE,      // Toggle helmet section on/off
            // Hotkey settings
            HOTKEY_KEYBOARD_BIND,      // Click to capture keyboard binding
            HOTKEY_CONTROLLER_BIND,    // Click to capture controller binding
            // Tracked Riders settings
            RIDER_ADD,                 // Add rider to tracking list
            RIDER_REMOVE,              // Remove rider from tracking list
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
            // Director (auto-director, spectate broadcast tool)
            DIRECTOR_ENABLE_TOGGLE,    // Master enable for the auto-director
            SPOTTER_ENABLED_TOGGLE,    // Spotter master enable (audio callouts)
            SPOTTER_SUBTITLES_TOGGLE,  // Spotter subtitle widget content on/off
            DIRECTOR_GAMEPAD_TAKEOVER, // Toggle stick-push gamepad takeover
            // (Forks is an INI-only tunable - no click region.)
            DIRECTOR_FOLLOW_BATTLES,   // Toggle following on-track battles
            DIRECTOR_FOLLOW_INCIDENTS, // Toggle crash/incident interrupts
            DIRECTOR_FOLLOW_FASTEST,   // Toggle fastest-lap celebration cuts
            DIRECTOR_FOLLOW_PACE,      // Toggle non-race hot-lap (fastest-sector) cuts
            DIRECTOR_FINISH_LOCK,      // Toggle final-lap finish lock
            DIRECTOR_CATCH_OVERTAKES,  // Toggle on-track overtake rewards
            DIRECTOR_FOLLOW_LAPPERS,   // Toggle following a front-runner lapping backmarkers
            DIRECTOR_FOLLOW_DROPS,     // Toggle following a rider tumbling down the order
            // (Incident hold cap is an INI-only tunable - no click region.)
            DIRECTOR_HUD_VISIBLE,      // Toggle the on-screen director status button
            // FMX HUD
            // (Trick stack rows is a data-driven STEPPED control.)
            FMX_DEBUG_TOGGLE,          // Toggle FMX debug logging
            // Stats HUD
            STATS_SHOW_LAP_TOGGLE,     // Toggle lap column
            STATS_SHOW_SESSION_TOGGLE, // Toggle session column
            STATS_SHOW_ALLTIME_TOGGLE, // Toggle all-time column
            ACHIEVEMENTS_TOASTS_TOGGLE,// Achievement toasts on/off (global master; also the tab-list checkbox)
            ACHIEVEMENTS_PAGE_PREV,    // Previous page of the achievements list
            ACHIEVEMENTS_PAGE_NEXT,    // Next page of the achievements list
            ACHIEVEMENTS_PRESTIGE,     // Trade the ladder for a prestige level (arms, then performs)
            // Clock Widget
            CLOCK_FORMAT_TOGGLE,       // Toggle 12h/24h format (ClockWidget)
            // Event Log HUD
            EVENT_LOG_ICONS_TOGGLE,    // Toggle event type icons (EventLogHud)
            // Standings tab toggles
            LIVE_GAPS_TOGGLE,          // Toggle live gap display in races (StandingsHud per-profile member)
            FILTER_DNS_TOGGLE,         // Toggle DNS rider filtering (PluginData global)
            HEADERS_TOGGLE,            // Toggle column-header row in the standings (StandingsHud)
            SESSION_INFO_TOGGLE,       // Toggle session-info row (clock/laps/overtime) in the standings (StandingsHud)
            // Help & Community links (About page)
            OPEN_LINK_DOCS,            // Open documentation site
            OPEN_LINK_COMMUNITY,       // Open community forum
            OPEN_LINK_KOFI,            // Open Ko-fi donation page
            // APPENDED, not filed next to its relatives, for the reason the sentinel
            // below states: the golden encodes region types as raw ordinals, so a new
            // value in the middle shifts every later one and rewrites a golden for a
            // tab it never touched. At the end only the count moves.
            PROBE_SWEEP,               // Developer: run the render-probe sweep
            OPEN_LINK_OVERLAY,         // Open the live web overlay in a browser
            DIRECT_GL_TOGGLE,          // "Direct GL Rendering" toggle (General tab)
            OPEN_LINK_GITHUB,          // Open the GitHub repository (About page)
            TWITCH_CHANNEL_EDIT,       // Stream Chat tab: start/commit typing the Twitch channel name
            TWITCH_ENABLED_TOGGLE,     // Stream Chat tab: the Twitch connection on/off (its Status row)
            YOUTUBE_CHANNEL_EDIT,      // Stream Chat tab: start/commit typing the YouTube channel
            YOUTUBE_ENABLED_TOGGLE,    // Stream Chat tab: the YouTube connection on/off (its Status row)
            SLIDER,                    // A slider's track: click or drag to set (steppedIndex = m_sliders)
            DROPDOWN,                  // A dropdown's box: opens its list (cycleIndex)
            DROPDOWN_OPTION,           // One entry of the open list (cycleIndex, flagBit = entry)
            // Appended, not grouped with the Map's other toggles: see COUNT.
            MAP_RANGE_ADAPTIVE_TOGGLE, // Toggle Follow's range growing with speed (MapHud)

            // Sentinel, always last. settings_layout_test.cpp's golden encodes
            // region types as raw ORDINALS, and this enum is unnumbered and
            // grouped by topic — so inserting a control next to its relatives
            // shifts every later value and rewrites the whole golden, for a
            // change that never touched the General tab. The test asserts this
            // count FIRST, so that edit fails with one readable line instead of
            // a wall of shifted numbers.
            COUNT
        } type;

        // Type-safe variant instead of unsafe union (C++17)
        // Holds different pointer types based on ClickRegion::Type
        using TargetPointer = std::variant<
            std::monostate,                              // Empty state (for types that don't need a pointer)
            uint32_t*,                                   // For CHECKBOX (targetBitfield)
            bool*,                                       // For bool toggle regions
            HotkeyAction,                                // For HOTKEY_* controls
            std::string                                  // For RIDER_* controls (rider name)
        >;
        TargetPointer targetPointer;

        uint32_t flagBit;          // Which bit to toggle (for CHECKBOX)
        bool isRequired;           // Can't toggle if required (for CHECKBOX)
        BaseHud* targetHud;        // HUD to mark dirty after toggle
        int tabIndex;              // Which tab to switch to (for TAB type)
        std::string tooltipId;     // Tooltip ID for hover display (Phase 3)
        int steppedIndex = -1;     // Index into m_steppedControls (STEPPED_*), or m_sliders (SLIDER)
        int cycleIndex = -1;       // Index into m_cycleControls (CYCLE_*, DROPDOWN, DROPDOWN_OPTION)
        int cellIndex = 0, cellCount = 1;  // TOOLTIP_ROW grid cell (see rowBandSpan)

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

    // Descriptor for the shared STEPPED_UP/STEPPED_DOWN click regions (and the
    // slider a bounded one draws as): what to step, how, within which bounds, and
    // which HUD to mark dirty. Registered by addSteppedControl into
    // m_steppedControls, rebuilt with m_clickRegions; the value pointers point at
    // long-lived HUD members.
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
            FIXED_FLOAT,   // Fixed step, deliberately NO hold acceleration, clamped to
                           // [flo,fhi]; loLink (when set) overrides flo with a live value
            ACCESSOR       // get/set instead of a pointer (a setter that clamps, a manager):
                           // accelerated fstep, clamped to [flo,fhi], snapped to the fstep grid
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
        std::function<float()> get;        // ACCESSOR only
        std::function<void(float)> set;
        // ACCESSOR only, optional: a slider drag calls dragSet instead of set and
        // onRelease once the button comes up (or the panel closes mid-drag). For a
        // value that resizes the panel under the cursor (UI scale), which would
        // otherwise slide the track away from the pointer while it is held.
        std::function<void(float)> dragSet;
        std::function<void()> onRelease;

        static SteppedControl accessor(std::function<float()> g, std::function<void(float)> s,
                                       float step, float lo, float hi, BaseHud* dirtyHud) {
            SteppedControl c; c.kind = Kind::ACCESSOR; c.get = std::move(g); c.set = std::move(s);
            c.fstep = step; c.flo = lo; c.fhi = hi; c.dirtyHud = dirtyHud; return c;
        }
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

    // Descriptor for the shared CYCLE_UP/CYCLE_DOWN click regions (and the dropdown
    // a labelled one draws as): a mod-N state cycle, registered by addCycleControl
    // into m_cycleControls, rebuilt with m_clickRegions. get/set use a 0-based state
    // index; enums whose VISUAL order differs from their numeric order (StandingsHud's
    // PosGainMode) map inside get/set. NO hold acceleration: cycles step ±1.
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
        std::function<std::string(int)> nameOf;  // a state's name; with 3+ states it draws as a dropdown
        std::function<int(int)> spriteOf;        // a state's icon sprite (0 = none): the list opens as a grid of icons
        std::function<unsigned long(int)> swatchOf;  // a state's colour: the list shows a swatch before each name
        std::function<int(int)> fontOf;          // a state's font index (0 = the menu's): the list draws each name in it
        std::function<void(bool)> step;          // the arrows' own step, where the owner defines one (else get/set wrap)
        // Holding an arrow repeats the step. False for a step that does heavy
        // work (opens a window, loads a pack, previews a voice): it fires once,
        // on release, as a button does, so the press can still slide off.
        bool repeat = true;

        // The common case: cycle an enum (or integral) member of a HUD through
        // its full 0..count-1 numeric range. Works for any enum whose visual
        // cycle order equals its numeric order. Lambdas formed here have the
        // access rights of SettingsHud (nested class of a friend).
        template <typename OwnerT, typename EnumT>
        static CycleControl enumMember(OwnerT* owner, EnumT OwnerT::* member,
                                       int count, BaseHud* dirtyHud, const char* const* names = nullptr) {
            CycleControl c; if (names) c.nameOf = [names](int i) { return std::string(names[i]); };
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
    static BaseHud* renderTabDeltaTrace(SettingsLayoutContext& ctx);
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
    static BaseHud* renderTabAchievements(SettingsLayoutContext& ctx);
    static BaseHud* renderTabStats(SettingsLayoutContext& ctx);
    static BaseHud* renderTabEventLog(SettingsLayoutContext& ctx);
    static BaseHud* renderTabDirector(SettingsLayoutContext& ctx);
    static BaseHud* renderTabStreamChat(SettingsLayoutContext& ctx);
    static BaseHud* renderTabSpotter(SettingsLayoutContext& ctx);
    // The About page. Hidden from the tab list; opened by the footer's About button.
    static BaseHud* renderTabAbout(SettingsLayoutContext& ctx);
    static BaseHud* renderTabMore(SettingsLayoutContext& ctx);   // hidden; a More row opens it

    // Static click handler functions (implemented in tab files)
    // Return true if the click was handled, false otherwise
    bool handleClickTabMap(const ClickRegion& region);
    bool handleClickTabTiming(const ClickRegion& region);
    bool handleClickTabGapBar(const ClickRegion& region);
    bool handleClickTabStandings(const ClickRegion& region);
    bool handleClickTabRumble(const ClickRegion& region);
    bool handleClickTabHelmet(const ClickRegion& region);
    bool handleClickTabAppearance(const ClickRegion& region);
    bool handleClickTabGeneral(const ClickRegion& region);
    bool handleLinkClick(const ClickRegion& region);   // settings/settings_links.cpp; every tab's link rows
    bool handleClickTabFriends(const ClickRegion& region);
    bool handleClickTabHotkeys(const ClickRegion& region);
    bool handleClickTabRiders(const ClickRegion& region);
    bool handleClickTabRecords(const ClickRegion& region);
    bool handleClickTabSession(const ClickRegion& region);
    bool handleClickTabLapLog(const ClickRegion& region);
    bool handleClickTabUpdates(const ClickRegion& region);
    bool handleClickTabPerformance(const ClickRegion& region);

    // Drop any armed Reset. Called on every way OUT of the armed state that is not
    // the confirming click: closing the menu, and switching tabs by any route.
    //
    // An arm that outlives the screen it was made on is a trap -- you would come back
    // to the tab later, or reopen the menu, and find a button already saying
    // "Confirm?" with no memory of having pressed it, one click from throwing a
    // profile away. Arming is a statement about right now.
    void disarmResets() {
        m_resetProfileConfirmed = false;
        m_resetAllConfirmed = false;
        m_prestigeConfirmed = false;
        m_resetTabConfirmed = false;
        m_copyConfirmed = false;
    }
    bool handleClickTabSpotter(const ClickRegion& region);
    bool handleClickTabFmx(const ClickRegion& region);
    bool handleClickTabAchievements(const ClickRegion& region);
    bool handleClickTabStreamChat(const ClickRegion& region);
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
    CrashWidget* getCrashWidget() const { return m_crash; }
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

    // NO SURFACE OVERRIDES AND NO edgeInsetX. This panel's surfaces -- the caption
    // band, the sidebar's cards and the content column's -- are PanelBox's boxes,
    // placed from panelInnerLeft/panelInner like every other plan panel's, so
    // there is nothing here to keep in step with the base class.
    //
    // An override composing frame border + [panel] padding against a base class
    // that takes a MAX of the two is a second spelling, and a second spelling is
    // how the panel stops respecting the panel padding like HUDs do, or how holes
    // open at the sides of the settings title when the band uses one spelling and
    // the cards the other. One engine, one answer, no spellings.

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
    // The open tab, a NAME -> index lookup and whether a tab sits in a More group,
    // for the what's-new hooks: a test naming "Widgets" should not have to know it is tab 14.
    int testActiveTab() const { return m_activeTab; }
    const char* testTabNameForIndex(int t) const { return getTabName(t); }
    bool testTabInGroup(int t) const { return groupRowOf(t) >= 0; }
    // Does the LAST-BUILT tab carry a row registering this tooltip id? The
    // what's-new markers key on those ids, and one naming a row that does not
    // exist draws nothing and says nothing.
    std::vector<std::string>& testUntippedRows() { return m_testUntippedRows; }
    bool testHasRegionWithTooltip(const char* tooltipId) const {
        if (!tooltipId) return false;
        for (const ClickRegion& r : m_clickRegions) {
            if (r.tooltipId == tooltipId) return true;
        }
        return false;
    }
    // The centre of the LAST-BUILT region carrying this tooltip id, in the
    // panel's build space (the cursor space in-game), so a test can put the
    // injected mouse on a control. False when no region carries it.
    // The achievements list's current page and the group it shows.
    int testAchievementsPage(const char** group) const {
        if (group) *group = m_achievementsPageGroup;
        return m_achievementsPage;
    }
    bool testRegionCenter(const char* tooltipId, float* x, float* y,   // `only`: one type (a slider)
                          ClickRegion::Type only = ClickRegion::COUNT, float* width = nullptr) const {
        if (!tooltipId || !x || !y) return false;
        for (const ClickRegion& r : m_clickRegions) {
            if (r.tooltipId != tooltipId || (only != ClickRegion::COUNT && r.type != only)) continue;
            if (width) *width = r.width;
            *x = r.x + r.width * 0.5f;
            *y = r.y + r.height * 0.5f;
            return true;
        }
        return false;
    }
    // A tab CLICK, through handleTabClick -- the path the sidebar takes, which is
    // the one that dismisses a what's-new tag. setActiveTabByName deliberately does
    // NOT dismiss: it is also the persisted-tab restore, and clearing a tag before
    // the player has opened the menu would spend the marker on nobody.
    void testClickTab(int tabIndex) {
        ClickRegion r;
        r.type = ClickRegion::TAB;
        r.tabIndex = tabIndex;
        handleTabClick(r);
    }
    // The HOVER dismissal, through the same helper the real pointer path uses --
    // dismiss plus markSettingsDirty, never WhatsNew::dismissRow on its own. A hook
    // calling the free function directly would let the persistence case pass while
    // dismissals never reach disk.
    void testHoverDismissRow(const char* tooltipId) { dismissMarkedRow(tooltipId); }
    void testHoverDismissTab(int tabIndex) { dismissMarkedTab(tabIndex); }
    // The footer's About button, through dispatchRegion -- the same path a real
    // click takes, so the tab change AND the easter-egg counter both run. A test
    // that set m_activeTab directly would prove neither.
    // The About button's own click region (defined beside the footer that builds it).
    bool testAboutButtonRect(int* l, int* t, int* r, int* b) const;
    // Min/max right edge (x * 1e6) of the active tab's ROW closing arrows
    // (CYCLE_UP/STEPPED_UP); returns the count. Defined in settings_layout.cpp.
    int testClosingArrowRightX(int* minRight, int* maxRight) const;
    void testClickAbout() {
        ClickRegion r;
        r.type = ClickRegion::VERSION_CLICK;
        dispatchRegion(r, /*skipSave=*/true);
    }
    // Every SELECTABLE tab, hidden ones included -- a different question from
    // testTabNameAt, which enumerates the SIDEBAR LIST. About is hidden from the
    // list but its content still counts toward the panel's height, so a test
    // sweeping "every tab that can set the panel height" needs this one and a test
    // asserting "what the sidebar shows" needs the other. Conflating them leaves
    // title_band_test unable to find the tallest tab.
    const char* testAnyTabNameAt(int i) const;
    int testTabIndexForName(const char* name) const {
        if (!name) return -1;
        for (int t = 0; t < TAB_COUNT; ++t) {
            if (std::strcmp(getTabName(t), name) == 0) return t;
        }
        return -1;
    }
    bool testClickStepped(int index, bool up, int holdRepeats);
    // The i-th SELECTABLE tab's display name, in tab-list order; nullptr past the
    // end. Enumeration rather than a list in the test, because the panel is as tall
    // as the tab it is showing now — so "does the menu fit the screen" is a question
    // about EVERY tab, and a list in the test is a list that a new tab is not added
    // to. See theme_geometry_test's screen-fit case.
    const char* testTabNameAt(int i) const;
    // Same seam for the shared CYCLE_UP/CYCLE_DOWN regions (no hold tier — cycles
    // never accelerate).
    int testCycleRegionCount(bool up) const;
    // CYCLE_UP arrows on the active tab that fire once on release (a heavy
    // step: CycleControl::repeat false) rather than repeating while held.
    int testOnceCycleCount() const;
    // Lists on the active tab with 3+ states but no names, which therefore still
    // draw as bare arrows instead of a dropdown. Zero on every tab is the rule.
    int testUnnamedListCount() const;
    // Whether holding a profile arrow auto-repeats. It must not: see the comment
    // on isRepeatableRegionType. Asked by name rather than by passing a
    // ClickRegion::Type across the DLL boundary, where the enum's values are not
    // a contract.
    bool testProfileArrowRepeats() const {
        return isRepeatableRegionType(ClickRegion::PROFILE_CYCLE_UP) ||
               isRepeatableRegionType(ClickRegion::PROFILE_CYCLE_DOWN);
    }
    // Characterization seam: a stable text signature of the ACTIVE tab's emitted
    // click regions (type + tooltip id, in emission order) plus the string count.
    // Lets a headless test pin the settings panel's rendered output across a
    // refactor — region ORDER and TYPE are behaviour (they are what a click hits),
    // and nothing else below the Wine layer can see them.
    void testRegionSignature(char* out, int cap) const;
    bool testClickCycle(int index, bool up);

    // Click EVERY control on the active tab once, through the real click path,
    // skipping only the regions that navigate, open a link, capture input, start an
    // external action, or are the reset controls themselves (see isPerturbSafe in the
    // .cpp). Returns how many it clicked. This is the perturbation half of
    // reset_tab_test: a tab's Reset has to put back everything the tab can change,
    // and the only list of what a tab can change that cannot go stale is the one the
    // tab itself emits. A NEW control type is clicked by default.
    int testPerturbActiveTab();
    // Press the footer's "Reset <tab>" button through the real click path (the
    // perturbation sweep deliberately skips it).
    bool testClickResetTab(int clicks = 2);

    // Click the Director tab's "Visible" row through the REAL path. Named rather
    // than taking a ClickRegion::Type ordinal: that enum is unnumbered and grouped
    // by topic, so an ordinal crossing the DLL boundary silently means something
    // else the moment a control is inserted near its relatives.
    bool testClickDirectorHudVisible();

    // The panel's two COLUMN edges as the last rebuild placed them: the outer edge of
    // the tab column and the outer edge of the content column's full-width rows. Under
    // a theme those are the two columns' card edges; without one they are the tab
    // highlight's left and the row highlight's right. Same two numbers either way,
    // which is the point -- the symmetry rule they exist to pin is the same rule in
    // both modes, and each mode can break it on its own.
    //
    // Stashed rather than measured back out of m_quads: unthemed a full-width row is
    // only drawn UNDER THE CURSOR, so a headless scan of the emitted quads answers
    // with whatever else happens to be rightmost (the Close button) and reads as a
    // 27-cell margin.
    // The gutter's bounding card edges (test builds; see the fields). The
    // gutter — content card left minus sidebar card right — must equal the
    // vertical seam read (contentGapY) for the same terms; theme_geometry_test
    // pins that (a gap-only composition, or an unpaid row lead-in, reads short).
    void testCardEdgesX(float& sidebarRight, float& contentLeft) const {
#if defined(MXBMRP3_TEST_BUILD)
        sidebarRight = m_testSidebarCardRightX; contentLeft = m_testContentCardLeftX;
#else
        sidebarRight = 0.0f; contentLeft = 0.0f;
#endif
    }
    void testColumnEdgesX(float& left, float& right) const {
        left = m_testColumnLeftX; right = m_testColumnRightX;
    }

    // The CONTENT column's three x anchors as the last rebuild placed them: the label
    // column, the control column (where a row's `< >` steppers and checkboxes start)
    // and the right edge of a full-width row (what right-aligned glyphs are placed
    // against). All three are theme-INVARIANT by construction
    // -- the layout anchors the content box and hangs the panel off it -- which is the
    // rule these exist to pin, and which testColumnEdgesX cannot see: its two numbers
    // are relative to the panel, so they stay symmetric while the whole content walks
    // sideways together.
    void testContentColumnX(float& labelX, float& controlX, float& rowRight) const {
        labelX = m_testLabelX; controlX = m_testControlX; rowRight = m_testRowRightX;
    }
#endif

private:
    void rebuildRenderData() override;
    // Dismiss a hovered row's what's-new band + mark dirty. See the definition for
    // why the two must not be separable.
    void dismissMarkedRow(const char* tooltipId);
    void dismissMarkedTab(int tabIndex);

    // The tallest tab's content height in rows, measured by laying every tab out.
    // Cached; -1 means "not measured". See the definition for why it is measured
    // rather than declared, and why it runs per LAYOUT rather than per rebuild.
    // The tallest tab's BODY HEIGHT, laid out -- not its content flow.
    //
    // A row COUNT (a tab's cursor end plus one card pad) is a different quantity
    // from the height the engine produces: a column's body is sum(section heights)
    // PLUS, per section, the card's own margin/border/padding, PLUS a seam between
    // each pair. None of that scales with the cursor, all of it scales with the
    // SECTION COUNT -- so measured by rows, a tab with more sections outgrows the
    // floor and the panel changes height as you switch tabs, which is the one thing
    // the floor exists to prevent.
    //
    // So each tab is laid out for real and the tallest body wins. Deliberately NOT by
    // adding the per-card terms here: a hand-summed `nSections * (pad+border+margin)
    // + (n-1) * gap` is a second spelling of something PanelBox already computes,
    // and second spellings are where this panel's geometry faults come from. The
    // layout is cached behind TallestKey, so the cost is a dozen layouts when the
    // metrics move, never per frame -- and it already runs a full tab RENDER per tab
    // to get here.
    float measureTallestBodyH(const ScaledDimensions& dim,
                              float labelX, float controlX, float rightColumnX,
                              float contentAreaStartX, float contentAreaWidth,
                              float panelContentRightX,
                              float sidebarAsk, float contentAsk,
                              const std::vector<float>& tabGroups);
    float m_tallestContentRows = -1.0f;
    // True while measureTallestBodyH lays a tab out: a tab with an expandable
    // part (Rumble's Bumps/Lockup groups) renders its TALLEST state, so opening
    // it never outgrows the panel.
    bool m_measuringTallest = false;
    // What one dry layout pass over a tab learned: where its cursor ended, and the
    // content height of every section it opened, in order. See measureTab.
    struct TabMeasure {
        float endY = 0.0f;
        std::vector<float> sections;
    };
    // The sidebar's sections: one content height per tab-list group, in registry
    // order. The seam between groups is the engine's, not stated here.
    std::vector<float> measureTabGroups(const ScaledDimensions& dim) const;
    TabMeasure measureTab(int tabId, const ScaledDimensions& dim,
                          float labelX, float controlX, float rightColumnX,
                          float contentAreaStartX, float contentAreaWidth,
                          float panelContentRightX);
    // WHAT THE MEASUREMENT DEPENDS ON. Every tab's height is a row COUNT times
    // lineHeightNormal, so only the row metrics and the theme's box terms can move
    // it -- and a theme's generation counter covers discovery and a change of
    // selected theme alike (the same key ColorConfig's memo uses).
    //
    // A KEY RATHER THAN A SENTINEL dropped from rebuildLayout(): rebuildLayout()
    // runs on every frame of a DRAG, and a drag arrives as a layout dirty exactly
    // like a scale change does, so a sentinel re-lays every tab per frame to answer
    // a question whose inputs have not moved -- roughly 29x the cost of the one
    // rebuild the drag actually needs. The key names the real triggers and nothing
    // else.
    struct TallestKey {
        float lineHeight = 0.0f;
        float fontSize = 0.0f;
        float cellW = 0.0f;
        unsigned int themeGen = 0;   // 0 is never a live generation
        bool operator==(const TallestKey& o) const {
            return lineHeight == o.lineHeight && fontSize == o.fontSize
                && cellW == o.cellW && themeGen == o.themeGen;
        }
    };
    TallestKey m_tallestKey;
    // The tab the overflow warning last fired for, so a tab that overruns warns once
    // instead of on every rebuild (the panel rebuilds per frame while open). -1 = none.
    int m_overflowWarnedTab = -1;
public:
    // Drop the measurement. Called from show(), for the one input the key cannot
    // see: LIVE DATA moved while the menu was closed (the Riders tab lists session
    // entries, the Updates tab follows the downloader). The stale window this leaves
    // open while the menu IS open is the documented trade at measureTallestContentRows.
    void invalidateTallestTab() { m_tallestContentRows = -1.0f; }
private:

#if defined(MXBMRP3_TEST_BUILD)
public:
    // How far the last-built tab overran the space reserved for it, in rows;
    // negative is slack. Should be <= 0 always, since the height is MEASURED from
    // the tallest tab: a positive value means the measure pass and the real lay-out
    // disagree about some tab. See MXBMRP3_Test_SettingsOverflowRows and
    // settings_fit_test.
    float testOverflowRows() const { return m_testOverflowRows; }
    int testWhatsNewBands() const { return m_testWhatsNewBands; }   // last build's row bands
private:
    float m_testOverflowRows = 0.0f;
    int m_testWhatsNewBands = 0;
#endif

    // Tab-bar build, split out of rebuildRenderData(); see their definitions in
    // settings_hud_render.cpp.
    // Distance from the panel's TOP EDGE to where tab content begins -- the title
    // and the air around it. See the definition for why it is one function.
    // A settings card's interior pad per end, in CELLS: the panel's own base
    // (settingsSectionPadding) plus the resolved [content] padding side — the
    // same term a plan panel's card spends between its edge and its rows, so
    // [Advanced] contentPadding / a theme's [content] padding reach these
    // cards too. The composition applies only while a themed card is drawn
    // (the plan's own rule: a collapsed content box spends no padding).
    float cardPadTopY() const;
    float cardPadBotY() const;
    // The horizontal twin, in CHARACTERS (== x-cells at the shipped grid): the
    // row lead-in base plus the resolved [content] padding side — how far a
    // card reaches past its rows each side. Spent at the card edges AND paid
    // back to the trough/width chain (the pair that must move together).
    float cardPadLeftCells() const;
    // [content] MARGIN's horizontal sides, in CHARACTERS -- air OUTSIDE the cards,
    // between them and the panel's inner edge, exactly as a plan panel spends
    // c.m.l/c.m.r (PanelBox: cardLeft = panelInnerLeft + c.m.l).
    //
    // The vertical sides are read by contentGapCells() as the seam between two
    // section cards; these two must be read as well, or a theme's `[content]
    // margin = 2` spreads the settings sections apart while every card still runs
    // flush to the panel's surface line. Not gated on hasThemedCard(): margin is
    // AIR, and only the border reads as zero unthemed (the same split
    // cardBorderOverhangX makes).
    // The caption's box height, band or not -- see titleAdvance for why the two
    // differ by the band's border and by nothing else.
    // THE PANEL'S OWN PAD and the caption block's, resolved from the box terms
    // (theme key → [Advanced] built-in) and converted the BOX way — one stated
    // cell square on screen, as PanelBox::Spec::unit spends every vertical term.
    // Not basePaddingY()/dim.paddingH: those resolve the LEGACY
    // panelPadding{X,Y}Cells on a different lattice, a pair unreachable from any
    // ini, which would leave [Advanced] panelPadding, titleMargin and titlePadding
    // inert on this panel while they work everywhere else (pinned by
    // box_terms_test).
    // The sidebar↔content trough, in characters: the same composed seam read
    // the vertical card seams spend (contentGapCells = sectionGap + gap), so
    // the gutter measures what the air between two cards measures.
    // (Characters and cells are the same width at the shipped
    // one-cell-per-char grid.)
    bool addDisclosureCaret(float cx, float cy, float halfSize, bool open, unsigned long color);  // right = closed, down = open; false = UI icons off
    void buildMoreRow(const ScaledDimensions& dim, const PanelPlan& plan, const PanelBox::ColumnGeom& col,
                      int groupRow, float tabStartX, float& tabStartY, float tabWidth, float checkboxWidth);
    void buildTabBar(const ScaledDimensions& dim, const PanelPlan& plan,
                     const PanelBox::ColumnGeom& col, float tabStartX,
                     float tabWidth, float checkboxWidth);
    // rebuildRenderData() sections; the footer row is settings_hud_footer.cpp.
    PanelPlan& planSettingsPanel(const ScaledDimensions& dim, float& sidebarAsk, float& contentAsk, float& labelToControl, float& labelToRight);
    float renderActiveTab(SettingsLayoutContext& layoutCtx, const PanelPlan& plan, const PanelBox::ColumnGeom& mainCol, const ScaledDimensions& dim, float currentY);
    void addWhatsNewRowBands(const PanelPlan& plan, const PanelBox::ColumnGeom& mainCol);
    void checkTabOverflow(const PanelPlan& plan, const PanelBox::ColumnGeom& mainCol, const ScaledDimensions& dim, float currentY);
    void addHoveredRowHighlight(const PanelPlan& plan, const PanelBox::ColumnGeom& mainCol);
    void renderTooltipText(const SettingsLayoutContext& layoutCtx, const ScaledDimensions& dim);
    void buildFooterButtons(const ScaledDimensions& dim, const PanelPlan& plan, const PanelBox::ColumnGeom& sideCol, const PanelBox::ColumnGeom& mainCol, float startX, float panelWidth);
    void addResetTabButton(const ScaledDimensions& dim, const PanelPlan& plan, const PanelBox::ColumnGeom& sideCol, const PlanButtonTerms& bt, float buttonRowY, float buttonBoxH);
    void addAboutButton(const ScaledDimensions& dim, const PanelPlan& plan, const PanelBox::ColumnGeom& mainCol, const PlanButtonTerms& bt, float buttonRowY, float buttonBoxH);
    bool drawTabIcon(float x, float y, const char* iconName, unsigned long color,
                     const ScaledDimensions& dim, float checkboxWidth);
    // `onBand` = the icon is on the SELECTED tab's accent band (see the definition).
    void drawTabToggle(float x, float y, const char* iconName, bool enabled, bool onBand,
                       const ScaledDimensions& dim, float checkboxWidth);
    void handleClick(float mouseX, float mouseY);
    void dispatchRegion(const ClickRegion& region, bool skipSave = false);  // Dispatch a click region directly
    void handleRightClick(float mouseX, float mouseY);  // hotkey clearing
    bool handleRightClickTabHotkeys(const ClickRegion& region);  // settings_tab_hotkeys.cpp
    void resetToDefaults();        // Reset all profiles to defaults
    void resetCurrentTab();        // Reset current tab for current profile
    void resetCurrentProfile();    // Reset all HUDs for current profile

    // Click handlers - common handlers used by multiple tabs
    void applySteppedControl(const ClickRegion& region, bool increase);  // STEPPED_UP/STEPPED_DOWN
    void applyCycleControl(const ClickRegion& region, bool forward);     // CYCLE_UP/CYCLE_DOWN
    void handleCheckboxClick(const ClickRegion& region);
    // Every per-HUD on/off routes through this; see the definition for why a direct
    // setVisible() in a click handler is a bug on the companion surface.
    void toggleHudOnActiveSurface(class BaseHud* hud);
    void handleHudToggleClick(const ClickRegion& region);
public:
    // The lists a HUD's look row picks from (settings_controls.cpp): its texture
    // variants, its panel-theme override, and a pack HUD's installed packs.
    static CycleControl textureCycle(BaseHud* hud);
    static CycleControl themeOverrideCycle(BaseHud* hud);
    static CycleControl packCycle(BaseHud* hud);
    static void stepCycle(const CycleControl& c, bool forward);
    // One arrow step of the gamepad's / pit board's pack row. PUBLIC so
    // test_hooks.cpp can drive the row a player reaches (asset_pack_test.cpp):
    // widget state no control reaches is invisible through the widgets alone.
    void cycleGamepadPack(bool forward);
    void cyclePitboardPack(bool forward);
private:
    void handleTitleToggleClick(const ClickRegion& region);
    void handleTabClick(const ClickRegion& region);
    void handleCloseButtonClick();
    const char* getTabName(int tabIndex) const;  // Get display name for a tab
    // Whether a tab is selectable on this build: a real tab id (0..TAB_COUNT-1) whose
    // game-gated backing HUD is registered. Single source of truth for the tab-list skips
    // and the persisted-tab restore validation, so they can't drift.
    bool isTabAvailable(int tabId) const;

    // A tab the player can find and open from the sidebar: available on this
    // build, and not hidden from the list. Grand Tour's fraction is out of these,
    // and only these are recorded into it -- About is available but hidden, and is
    // reached by a footer button that sets the tab directly, so counting it in the
    // denominator made the row top out one tab short of 100% forever.
    bool isTabListed(int tabId) const {
        const TabDescriptor* d = findTabDescriptor(tabId);
        return d && !d->hidden && isTabAvailable(tabId);
    }
    // Grand Tour's fraction, recorded: the tab just opened, and the whole list it
    // is counted over. Walked on a tab change only, never per frame.
    void recordTabOpened(int tabId) const;
    // Note: Tab-specific handlers inlined into settings_tab_*.cpp files

    // ------------------------------------------------------------------
    // Per-tab descriptor registry (mirrors core/settings_hud_registry).
    // ONE static table drives every per-tab dispatch site: the tab-list
    // render loop (display order, name, tooltip id, backing-HUD checkbox,
    // section icon), game gating (isTabAvailable), the active-tab render
    // routing, the click routing, and the per-tab reset. Adding a tab =
    // one Tab enum value + ONE row in s_tabRegistry (settings_hud_render.cpp) —
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
        // A parenthesised suffix on the SIDEBAR LABEL only -- "Spotter (Beta)".
        // Deliberately not folded into `name`: that string is persisted as
        // [Profiles] activeTab and is what setActiveTabByName matches on, so
        // renaming a tab to badge it would strand everyone who had it open and
        // rename the "Reset <tab>" button with it. null = no badge.
        const char* badge;
        // NOT IN THE SIDEBAR LIST, but still a real selectable tab: the About page,
        // reached from the footer's About button instead of a row of its own.
        // LAST in the aggregate, with a DEFAULT MEMBER INITIALISER: the build runs
        // -Werror=missing-field-initializers, so the rows that omit it need one.
        // isTabAvailable() still returns true for a hidden tab -- it gates
        // SELECTABILITY (and the persisted-tab restore); only the list/measure
        // loops read this.
        bool hidden = false;
        // THE HUD THIS TAB CONFIGURES when it is not the checkbox's `hud`: the
        // Achievements toast and the Spotter subtitles. activeTabHud() prefers it,
        // so the positioning preview reaches them. Defaulted, like `hidden`.
        BaseHud* (*previewHud)(const SettingsHud&) = nullptr;
    };
    // Rows are in VISUAL ORDER - the tab-list render loop iterates this table
    // directly, so row position = position in the tab column. The negative
    // TAB_SECTION_* / TAB_GROUP rows render the section and group headers.
    static const TabDescriptor s_tabRegistry[];
    static const TabDescriptor* findTabDescriptor(int tabId);

    static constexpr int TAB_SECTION_GLOBAL = -1;    // s_tabRegistry section markers
    static constexpr int TAB_SECTION_PROFILE = -2;   // (negative = not a real tab)
    // A "More" group (tab rows up to the next marker): one sidebar row opening TAB_MORE.
    static constexpr int TAB_GROUP = -3;
    static constexpr uint32_t GROUP_HEADER = 1;   // a TAB region's flagBit on a More row
    static int groupRowOf(int tabId);   // the owning TAB_GROUP row's index, or -1
    void addBackButton(SettingsLayoutContext& ctx);   // under a More tab's content
    int moreGroupTabs(int* tabs, int cap, int* groupRow) const;   // what the More page lists
    // Whether the tab's row has an on/off toggle; its state goes to *enabled.
    bool tabToggleState(int tabId, BaseHud* tabHud, bool* enabled) const;
    ClickRegion::Type tabToggleType(int tabId, BaseHud* tabHud, BaseHud** target) const;  // its region
    const char* tabIconName(int tabId, BaseHud* tabHud) const;                             // identity icon

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
    void resetTabSpotter();
    void resetTabAchievements();
    void resetTabStreamChat();

    // Check if point is inside a clickable region
    bool isPointInRect(float x, float y, float rectX, float rectY, float width, float height) const;

    // Settings panel layout constants (character widths for monospace text).
    // The tunable ones live in core/layout_metrics.h, which derives the content and
    // tooltip widths from them; the ones below are fixed by the glyphs they measure
    // ("[X]" is three characters in every theme), so they stay compiled in.

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
    CrashWidget* m_crash;
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
    // The prestige button's arm. Same two-step and the same rule as the pair
    // above (disarmResets clears all three): trading the ladder in is the least
    // undoable act in the panel.
    bool m_prestigeConfirmed = false;
    // The footer's "Reset <tab>" and General's Copy, armed the same way: a tab's
    // whole tuning, or another profile overwritten, from one stray click.
    bool m_resetTabConfirmed = false;
    bool m_copyConfirmed = false;

    // Easter egg click detection (version string)
    static constexpr int EASTER_EGG_CLICKS = 5;
    static constexpr long long EASTER_EGG_TIMEOUT_US = 2000000;  // 2 seconds
    int m_versionClickCount = 0;
    long long m_lastVersionClickTimeUs = 0;

    // Window bounds cache for detecting resize
    // Cache actual pixel dimensions for resize detection
    int m_cachedWindowWidth;
    int m_cachedWindowHeight;
    int m_cachedTwitchStatus = -1;   // Stream Chat tab: last Twitch status drawn (live refresh)
    int m_cachedYouTubeStatus = -1;  // Stream Chat tab: last YouTube status drawn (live refresh)
    // Which text field the running TEXT capture belongs to: the commit in
    // update() sends the typed text to that field's channel.
    enum class TextField : uint8_t { NONE, TWITCH_CHANNEL, YOUTUBE_CHANNEL };
    TextField m_textField = TextField::NONE;
    std::string m_shownCaptureText;  // the text field at its last rebuild (rebuilt only on an edit)
    size_t m_shownCaptureCursor = 0;

#if GAME_HAS_DISCORD
    // Discord state cache for live status updates
    int m_cachedDiscordState;
    bool m_cachedDiscordEnabled;
#endif

    // Tab system
    //
    // PUBLIC, alone among the members around it: the what's-new marker table
    // (hud/settings/whats_new.cpp) names tabs by these values, and the alternative
    // was a second copy of the enum as integer literals -- wrong the first time a
    // tab moves. The tab INDEX is already public surface anyway (setActiveTabByName,
    // ClickRegion::tabIndex); only the names were not.
public:
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
        TAB_SPOTTER = 27,      // Spotter (audio callouts + subtitles)
        TAB_ABOUT = 28,        // About (hidden from the tab list; opened from the footer)
        TAB_ACHIEVEMENTS = 29, // Achievements (global: lifetime numbers as tiered progress)
        TAB_STREAM_CHAT = 30,       // "Stream Chat": (global: Twitch + YouTube channels, the chat HUD)
        TAB_DELTA_TRACE = 31,  // Delta Trace HUD (gap to a reference lap across the lap)
        TAB_MORE = 32,         // A section's More page (hidden; its sidebar row opens it)
        TAB_COUNT = 33
    };
private:
    int m_activeTab;
    int m_moreGroupRow = -1;   // the More row last clicked; -1 = none (see moreGroupTabs)

    // Mark settings dirty after a settings-panel edit (always, so the Save button reflects
    // unsaved changes in manual mode too). The write waits for leave-track or Save.
    void markSettingsDirty();

    // Last-seen SettingsManager dirty state: update() rebuilds the Save button when it flips.
    bool m_lastSettingsDirty = false;
    // Stats tab periodic refresh timer (epoch default triggers immediate first refresh)
    std::chrono::steady_clock::time_point m_lastStatsRefresh{};

    // Hover tracking for button backgrounds
    int m_hoveredRegionIndex;  // -1 = none hovered
    int m_hoveredHotkeyRow;    // -1 = none, index into m_hotkeyCells of the hovered binding
    enum class HotkeyColumn { NONE, KEYBOARD, CONTROLLER };
    HotkeyColumn m_hoveredHotkeyColumn;  // Which field of that binding is hovered
    float m_hotkeyRowHeight;      // Row height for hotkey tab (set during rebuild)
    // Where each binding's two fields were drawn, in draw order (set during rebuild); the
    // bindings sit in two columns, so a row index alone no longer places them.
    struct HotkeyCell { float top, keyboardX, controllerX; };
    std::vector<HotkeyCell> m_hotkeyCells;
    float m_hotkeyFieldCharWidth; // Character width for field calculations (set during rebuild)
    static constexpr int HOTKEY_KEY_FIELD = 8, HOTKEY_PAD_FIELD = 8;  // characters in each input box

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
    int m_achievementsPage = 0;        // Current page of the achievements list (0-based)
    int m_achievementsJumpTo = -1;     // A row to open the page of at the next layout (-1: none)
    const char* m_achievementsPageGroup = "";   // The drawn page's group name (for the tests)

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
    // The region's type repeats, and a cycle's own descriptor allows it.
    bool isRepeatableRegion(const ClickRegion& region) const;

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

#if defined(MXBMRP3_TEST_BUILD)
    void recordTestAnchors(const PanelPlan& plan, const PanelBox::ColumnGeom& sideCol, const PanelBox::ColumnGeom& mainCol, float leftColumnX, float controlX, const SettingsLayoutContext& layoutCtx);
    // See testColumnEdgesX().
    float m_testColumnLeftX = 0.0f;
    float m_testColumnRightX = 0.0f;
    // See testCardEdgesX(): the DRAWN card edges bounding the gutter, recorded
    // at the two rewriteThemedCard sites — measured where the art lands, not
    // re-derived from the chain that places it (a re-derivation would agree
    // with the chain's bugs).
    float m_testSidebarCardRightX = 0.0f;
    float m_testContentCardLeftX = 0.0f;
    // Index of the first click region belonging to the ACTIVE TAB's content (the
    // sidebar's regions precede it). See testPerturbActiveTab.
    int m_testContentRegionBegin = 0;
    // See testContentColumnX().
    float m_testLabelX = 0.0f;
    float m_testControlX = 0.0f;
    float m_testRowRightX = 0.0f;
    // Rows built without a tooltip id, as "tab: label" (addRowTooltip).
    std::vector<std::string> m_testUntippedRows;
#endif

    std::vector<ClickRegion> m_clickRegions;

    // Descriptors referenced by ClickRegion::steppedIndex / cycleIndex, rebuilt in
    // lockstep with m_clickRegions (rebuildRenderData / hide), never touched per
    // frame - see SteppedControl, CycleControl and settings_controls.h.
    std::vector<SteppedControl> m_steppedControls;
    std::vector<CycleControl> m_cycleControls;
    // Counts the menu's openings: a tab that reads something too heavy to read
    // per rebuild (the Spotter's packs and SAPI voices) reads it once per open.
    unsigned m_openSerial = 0;   // mt-plain: game thread only
    std::vector<SliderControl> m_sliders;
    SliderDrag m_sliderDrag;
    DropdownState m_dropdown;
    bool handleControlPress(int regionIndex, float cursorX);  // settings_controls.cpp, all four
    void updateSliderDrag(float cursorX, bool pressed); void endSliderDrag(); void handleControlRegion(const ClickRegion& region); void buildDropdownPopup(float l, float t, float r, float b);

    // Tooltip ID for a click region type on the active tab ("" when none)
    static const char* getTooltipIdForRegion(ClickRegion::Type type, int activeTab);
};
