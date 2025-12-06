// ============================================================================
// hud/settings_hud.h
// Settings interface for configuring which columns/rows are visible in HUDs
// ============================================================================
#pragma once

#include "base_hud.h"
#include "session_best_hud.h"
#include "lap_log_hud.h"
#include "standings_hud.h"
#include "performance_hud.h"
#include "input_hud.h"
#include "pitboard_hud.h"
#include "time_widget.h"
#include "position_widget.h"
#include "lap_widget.h"
#include "session_widget.h"
#include "speed_widget.h"
#include "speedo_widget.h"
#include "tacho_widget.h"
#include "timing_widget.h"
#include "bars_widget.h"
#include "version_widget.h"
#include "notices_widget.h"
#include <variant>
#include "map_hud.h"
#include "radar_hud.h"
#include "../core/plugin_constants.h"

// Forward declarations
class TelemetryHud;

class SettingsHud : public BaseHud {
public:
    SettingsHud(SessionBestHud* sessionBest, LapLogHud* lapLog,
                StandingsHud* standings,
                PerformanceHud* performance,
                TelemetryHud* telemetry, InputHud* input,
                TimeWidget* time, PositionWidget* position, LapWidget* lap, SessionWidget* session, MapHud* mapHud, RadarHud* radarHud, SpeedWidget* speed, SpeedoWidget* speedo, TachoWidget* tacho, TimingWidget* timing, BarsWidget* bars, VersionWidget* version, NoticesWidget* notices, PitboardHud* pitboard);
    virtual ~SettingsHud() = default;

    void update() override;
    bool handlesDataType(DataChangeType dataType) const override { return false; }

    // Show/hide the settings panel
    void show();
    void hide();
    bool isVisible() const { return m_bVisible; }

protected:
    void rebuildLayout() override;

private:
    // Clickable regions for checkboxes, buttons, and scale controls
    struct ClickRegion {
        float x, y, width, height;
        enum Type {
            CHECKBOX,                  // Toggle column/row visibility (bitfield)
            GAP_MODE_CYCLE,            // Cycle through gap modes (Off/Me/All)
            GAP_INDICATOR_CYCLE,       // Cycle through gap indicator modes (Off/Official/Live/Both)
            RESET_BUTTON,              // Reset to defaults button
            HUD_TOGGLE,                // Toggle entire HUD visibility
            TITLE_TOGGLE,              // Toggle HUD title
            BACKGROUND_TEXTURE_TOGGLE, // Toggle background texture
            BACKGROUND_OPACITY_UP,     // Increase background opacity
            BACKGROUND_OPACITY_DOWN,   // Decrease background opacity
            SCALE_UP,                  // Increase scale
            SCALE_DOWN,                // Decrease scale
            ROW_COUNT_UP,              // Increase row count (StandingsHud)
            ROW_COUNT_DOWN,            // Decrease row count (StandingsHud)
            LAP_LOG_ROW_COUNT_UP,      // Increase lap log row count (LapLogHud)
            LAP_LOG_ROW_COUNT_DOWN,    // Decrease lap log row count (LapLogHud)
            MAP_ROTATION_TOGGLE,       // Toggle map rotation mode (MapHud)
            MAP_OUTLINE_TOGGLE,        // Toggle track outline (MapHud)
            MAP_COLORIZE_TOGGLE,       // Toggle rider colorization (MapHud)
            MAP_TRACK_WIDTH_UP,        // Increase track line width (MapHud)
            MAP_TRACK_WIDTH_DOWN,      // Decrease track line width (MapHud)
            MAP_LABEL_MODE_CYCLE,      // Cycle label display mode (MapHud)
            RADAR_RANGE_UP,            // Increase radar range (RadarHud)
            RADAR_RANGE_DOWN,          // Decrease radar range (RadarHud)
            RADAR_COLORIZE_TOGGLE,     // Toggle rider colorization (RadarHud)
            RADAR_ALERT_DISTANCE_UP,   // Increase alert distance (RadarHud)
            RADAR_ALERT_DISTANCE_DOWN, // Decrease alert distance (RadarHud)
            RADAR_LABEL_MODE_CYCLE,    // Cycle label display mode (RadarHud)
            DISPLAY_MODE_UP,           // Cycle display mode forward (PerformanceHud)
            DISPLAY_MODE_DOWN,         // Cycle display mode backward (PerformanceHud)
            TAB,                       // Select tab
            CLOSE_BUTTON               // Close the settings menu
        } type;

        // Type-safe variant instead of unsafe union (C++17)
        // Holds different pointer types based on ClickRegion::Type
        using TargetPointer = std::variant<
            std::monostate,                              // Empty state (for types that don't need a pointer)
            uint32_t*,                                   // For CHECKBOX (targetBitfield)
            StandingsHud::GapMode*,                      // For GAP_MODE_CYCLE
            StandingsHud::GapIndicatorMode*,             // For GAP_INDICATOR_CYCLE
            uint8_t*                                     // For DISPLAY_MODE_UP/DOWN
        >;
        TargetPointer targetPointer;

        uint32_t flagBit;          // Which bit to toggle (for CHECKBOX)
        bool isRequired;           // Can't toggle if required (for CHECKBOX)
        BaseHud* targetHud;        // HUD to mark dirty after toggle
        int tabIndex;              // Which tab to switch to (for TAB type)

        // Constructor for simple regions (no pointer needed)
        ClickRegion(float _x, float _y, float _width, float _height, Type _type,
                   BaseHud* _targetHud = nullptr, uint32_t _flagBit = 0,
                   bool _isRequired = false, int _tabIndex = 0)
            : x(_x), y(_y), width(_width), height(_height), type(_type),
              targetPointer(std::monostate{}), flagBit(_flagBit), isRequired(_isRequired),
              targetHud(_targetHud), tabIndex(_tabIndex) {}

        // Constructor for CHECKBOX regions (uses uint32_t* bitfield)
        ClickRegion(float _x, float _y, float _width, float _height, Type _type,
                   uint32_t* bitfield, uint32_t _flagBit, bool _isRequired, BaseHud* _targetHud)
            : x(_x), y(_y), width(_width), height(_height), type(_type),
              targetPointer(bitfield), flagBit(_flagBit), isRequired(_isRequired),
              targetHud(_targetHud), tabIndex(0) {}

        // Constructor for GAP_MODE_CYCLE regions
        ClickRegion(float _x, float _y, float _width, float _height, Type _type,
                   StandingsHud::GapMode* gapMode, BaseHud* _targetHud)
            : x(_x), y(_y), width(_width), height(_height), type(_type),
              targetPointer(gapMode), flagBit(0), isRequired(false),
              targetHud(_targetHud), tabIndex(0) {}

        // Constructor for GAP_INDICATOR_CYCLE regions
        ClickRegion(float _x, float _y, float _width, float _height, Type _type,
                   StandingsHud::GapIndicatorMode* gapIndicatorMode, BaseHud* _targetHud)
            : x(_x), y(_y), width(_width), height(_height), type(_type),
              targetPointer(gapIndicatorMode), flagBit(0), isRequired(false),
              targetHud(_targetHud), tabIndex(0) {}

        // Constructor for DISPLAY_MODE regions
        ClickRegion(float _x, float _y, float _width, float _height, Type _type,
                   uint8_t* displayMode, BaseHud* _targetHud)
            : x(_x), y(_y), width(_width), height(_height), type(_type),
              targetPointer(displayMode), flagBit(0), isRequired(false),
              targetHud(_targetHud), tabIndex(0) {}

        // Default constructor
        ClickRegion() : x(0), y(0), width(0), height(0), type(CLOSE_BUTTON),
                       targetPointer(std::monostate{}), flagBit(0), isRequired(false),
                       targetHud(nullptr), tabIndex(0) {}
    };

    void rebuildRenderData() override;
    void handleClick(float mouseX, float mouseY);
    void resetToDefaults();

    // Click handlers per type
    void handleCheckboxClick(const ClickRegion& region);
    void handleGapModeClick(const ClickRegion& region);
    void handleGapIndicatorClick(const ClickRegion& region);
    void handleHudToggleClick(const ClickRegion& region);
    void handleTitleToggleClick(const ClickRegion& region);
    void handleBackgroundTextureToggleClick(const ClickRegion& region);
    void handleOpacityClick(const ClickRegion& region, bool increase);
    void handleScaleClick(const ClickRegion& region, bool increase);
    void handleRowCountClick(const ClickRegion& region, bool increase);
    void handleLapLogRowCountClick(const ClickRegion& region, bool increase);
    void handleMapRotationClick(const ClickRegion& region);
    void handleMapOutlineClick(const ClickRegion& region);
    void handleMapColorizeClick(const ClickRegion& region);
    void handleMapTrackWidthClick(const ClickRegion& region, bool increase);
    void handleMapLabelModeClick(const ClickRegion& region);
    void handleRadarRangeClick(const ClickRegion& region, bool increase);
    void handleRadarColorizeClick(const ClickRegion& region);
    void handleRadarAlertDistanceClick(const ClickRegion& region, bool increase);
    void handleRadarLabelModeClick(const ClickRegion& region);
    void handleDisplayModeClick(const ClickRegion& region, bool increase);
    void handleTabClick(const ClickRegion& region);
    void handleCloseButtonClick();

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
    static constexpr int SETTINGS_PANEL_WIDTH = 75;     // Settings panel total width (increased for vertical tabs)
    static constexpr int SETTINGS_TAB_WIDTH = 15;       // Width of vertical tab column (fits "[Session Best]")
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
    static constexpr int RESET_BUTTON_WIDTH = 18;       // "[Restore Defaults]" width
    static constexpr int RESET_BUTTON_HALF = 9;         // Half of reset button for centering

    // HUD references (non-owning pointers)
    SessionBestHud* m_sessionBest;
    LapLogHud* m_lapLog;
    StandingsHud* m_standings;
    PerformanceHud* m_performance;
    TelemetryHud* m_telemetry;
    InputHud* m_input;
    TimeWidget* m_time;
    PositionWidget* m_position;
    LapWidget* m_lap;
    SessionWidget* m_session;
    MapHud* m_mapHud;
    RadarHud* m_radarHud;
    SpeedWidget* m_speed;
    SpeedoWidget* m_speedo;
    TachoWidget* m_tacho;
    TimingWidget* m_timing;
    BarsWidget* m_bars;
    VersionWidget* m_version;
    NoticesWidget* m_notices;
    PitboardHud* m_pitboard;

    // Visibility flag
    bool m_bVisible;

    // Window bounds cache for detecting resize
    // Cache actual pixel dimensions for resize detection
    int m_cachedWindowWidth;
    int m_cachedWindowHeight;

    // Tab system (order matches F-key assignments: F1-F10)
    enum Tab {
        TAB_STANDINGS = 0,     // F1
        TAB_MAP = 1,           // F2
        TAB_RADAR = 2,         // F3
        TAB_PITBOARD = 3,      // F4
        TAB_LAP_LOG = 4,       // F5
        TAB_SESSION_BEST = 5,  // F6
        TAB_TELEMETRY = 6,     // F7
        TAB_INPUT = 7,         // F8
        TAB_PERFORMANCE = 8,   // F9
        TAB_WIDGETS = 9,       // F10 (toggles all)
        TAB_COUNT = 10
    };
    int m_activeTab;

    std::vector<ClickRegion> m_clickRegions;
};
