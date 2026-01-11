// ============================================================================
// hud/telemetry_hud.h
// Displays real-time bike telemetry inputs (throttle, brakes, clutch, steering)
// ============================================================================
#pragma once

#include "base_hud.h"
#include "../core/plugin_constants.h"
#include "../core/widget_constants.h"

class TelemetryHud : public BaseHud {
public:
    // PERFORMANCE TEST: Set to false to disable HUD and all calculations
    static constexpr bool ENABLED = true;

    TelemetryHud();
    virtual ~TelemetryHud() = default;

    void update() override;
    bool handlesDataType(DataChangeType dataType) const override;
    void setVisible(bool visible) override;
    void resetToDefaults();

    // Element flags - each bit represents a metric that can be toggled
    // Note: Some elements are only available when ON_TRACK (via RunTelemetry callback).
    //       During SPECTATE/REPLAY, only limited data from RaceVehicleData is available.
    enum ElementFlags : uint32_t {
        ELEM_THROTTLE    = 1 << 0,  // Throttle metric (always available)
        ELEM_FRONT_BRAKE = 1 << 1,  // Front brake metric (always available)
        ELEM_REAR_BRAKE  = 1 << 2,  // Rear brake metric (ON_TRACK only - not in SPluginsRaceVehicleData_t)
        ELEM_CLUTCH      = 1 << 3,  // Clutch metric (ON_TRACK only - not in SPluginsRaceVehicleData_t)
        ELEM_RPM         = 1 << 4,  // RPM metric (always available)
        ELEM_FRONT_SUSP  = 1 << 6,  // Front suspension compression (ON_TRACK only)
        ELEM_REAR_SUSP   = 1 << 7,  // Rear suspension compression (ON_TRACK only)
        ELEM_GEAR        = 1 << 8,  // Gear indicator (always available)

        ELEM_DEFAULT     = 0x1B     // Throttle, front brake, rear brake, clutch, RPM enabled; suspension, gear disabled (binary: 00011011)
    };

    // Display mode - controls whether to show graphs, numbers, or both
    enum DisplayMode : uint8_t {
        DISPLAY_GRAPHS = 0,      // Show only graphs
        DISPLAY_VALUES = 1,      // Show only numeric values
        DISPLAY_BOTH = 2,        // Show both graphs and values

        DISPLAY_DEFAULT = DISPLAY_BOTH
    };

    // Allow SettingsHud and SettingsManager to access private members
    friend class SettingsHud;
    friend class SettingsManager;

private:
    void rebuildRenderData() override;

    // Calculate dynamic width based on display mode
    int getBackgroundWidthChars() const {
        switch (m_displayMode) {
            case DISPLAY_GRAPHS:
                return GRAPH_WIDTH_CHARS;  // Graph only: 33 chars
            case DISPLAY_VALUES:
                return LEGEND_WIDTH_CHARS;  // Values only: 9 chars
            case DISPLAY_BOTH:
            default:
                return GRAPH_WIDTH_CHARS + 1 + LEGEND_WIDTH_CHARS;  // Both: 33 + 1 gap + 9 = 43 chars
        }
    }

    // Helper to add combined input graph (all inputs as lines in one graph)
    // hasFullTelemetry: true when ON_TRACK (full data via RunTelemetry), false during SPECTATE/REPLAY
    void addCombinedInputGraph(const HistoryBuffers& history, const BikeTelemetryData& bikeTelemetry,
                               float x, float y, float width, float height, bool hasFullTelemetry);

    // Base position (0,0) - actual position comes from m_fOffsetX/m_fOffsetY
    static constexpr float START_X = 0.0f;
    static constexpr float START_Y = 0.0f;

    static constexpr int GRAPH_WIDTH_CHARS = 33;    // Width for graph display (left side)
    static constexpr int LEGEND_WIDTH_CHARS = 9;    // Width for legend/values (right side) - fits "RPM 12345"
    static constexpr int BACKGROUND_WIDTH_CHARS = GRAPH_WIDTH_CHARS + 1 + LEGEND_WIDTH_CHARS;  // 33 + 1 gap + 9 = 43
    static constexpr float GRAPH_HEIGHT_LINES = 6;  // Height in line units

    // Graph grid line percentages (0-100% input range)
    static constexpr float GRID_LINE_80_PERCENT = 0.8f;
    static constexpr float GRID_LINE_60_PERCENT = 0.6f;
    static constexpr float GRID_LINE_40_PERCENT = 0.4f;
    static constexpr float GRID_LINE_20_PERCENT = 0.2f;

    uint32_t m_enabledElements = ELEM_DEFAULT;  // Bitfield of enabled metrics
    uint8_t m_displayMode = DISPLAY_DEFAULT;    // Display mode (graphs/values/both)
};
