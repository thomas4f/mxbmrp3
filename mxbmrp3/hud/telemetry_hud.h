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
    void resetToDefaults();

    // Element flags - each bit represents a metric that can be toggled
    enum ElementFlags : uint32_t {
        ELEM_THROTTLE    = 1 << 0,  // Throttle metric
        ELEM_FRONT_BRAKE = 1 << 1,  // Front brake metric (available for player and spectated riders)
        ELEM_REAR_BRAKE  = 1 << 2,  // Rear brake metric (only available for player)
        ELEM_CLUTCH      = 1 << 3,  // Clutch metric (only available for player)
        ELEM_RPM         = 1 << 4,  // RPM metric
        ELEM_FRONT_SUSP  = 1 << 6,  // Front suspension compression (only available for player)
        ELEM_REAR_SUSP   = 1 << 7,  // Rear suspension compression (only available for player)
        ELEM_GEAR        = 1 << 8,  // Gear indicator

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
    void addCombinedInputGraph(const HistoryBuffers& history, const BikeTelemetryData& bikeTelemetry,
                               float x, float y, float width, float height, bool isViewingPlayerBike);

    // Grid-aligned positions (0.0055 = 1 char width, 0.0222 = 1 line height)
    static constexpr float START_X = HudPositions::LEFT_SIDE_X;
    static constexpr float START_Y = HudPositions::MID_LOWER_Y;

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
