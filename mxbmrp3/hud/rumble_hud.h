// ============================================================================
// hud/rumble_hud.h
// Displays real-time controller rumble motor outputs and effect values
// ============================================================================
#pragma once

#include "base_hud.h"
#include "../core/plugin_constants.h"
#include "../core/widget_constants.h"
#include <deque>

class RumbleHud : public BaseHud {
public:
    RumbleHud();
    virtual ~RumbleHud() = default;

    void update() override;
    bool handlesDataType(DataChangeType dataType) const override;
    void resetToDefaults();

    // Allow SettingsHud and SettingsManager to access private members
    friend class SettingsHud;
    friend class SettingsManager;

private:
    void rebuildRenderData() override;

    // Helper to draw a line graph from history buffer
    void addHistoryGraph(const std::deque<float>& history, unsigned long color,
                         float x, float y, float width, float height,
                         float lineThickness, size_t maxHistory);

    // Grid-aligned positions
    static constexpr float START_X = 0.6875f;
    static constexpr float START_Y = -0.0444f;

    // Layout constants (matching TelemetryHud style)
    static constexpr int GRAPH_WIDTH_CHARS = 33;     // Width for graph display (left side)
    static constexpr int LEGEND_WIDTH_CHARS = 9;     // Width for legend/values (right side)
    static constexpr int BACKGROUND_WIDTH_CHARS = GRAPH_WIDTH_CHARS + 1 + LEGEND_WIDTH_CHARS;  // +1 for gap
    static constexpr float GRAPH_HEIGHT_LINES = 6;   // Height in line units

    // Graph grid line percentages
    static constexpr float GRID_LINE_80_PERCENT = 0.8f;
    static constexpr float GRID_LINE_60_PERCENT = 0.6f;
    static constexpr float GRID_LINE_40_PERCENT = 0.4f;
    static constexpr float GRID_LINE_20_PERCENT = 0.2f;
};
