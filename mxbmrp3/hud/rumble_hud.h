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

    // Helper to draw a vertical bar (similar to BarsWidget)
    void addVerticalBar(float x, float y, float barWidth, float barHeight,
                        float value, unsigned long color);

    // Base position (0,0) - actual position comes from m_fOffsetX/m_fOffsetY
    static constexpr float START_X = 0.0f;
    static constexpr float START_Y = 0.0f;

    // Layout constants (matching TelemetryHud style)
    static constexpr int GRAPH_WIDTH_CHARS = 29;     // Width for graph display (left side, narrower to fit bars)
    static constexpr int BAR_WIDTH_CHARS = 1;        // Width for each force bar
    static constexpr int GAP_WIDTH_CHARS = 1;        // Gap between elements
    static constexpr int LEGEND_WIDTH_CHARS = 9;     // Width for legend/values (right side)
    // Total: graph + gap + bar + gap + bar + gap + legend = 29 + 1 + 1 + 1 + 1 + 1 + 9 = 43
    static constexpr int BACKGROUND_WIDTH_CHARS = GRAPH_WIDTH_CHARS + GAP_WIDTH_CHARS + BAR_WIDTH_CHARS + GAP_WIDTH_CHARS + BAR_WIDTH_CHARS + GAP_WIDTH_CHARS + LEGEND_WIDTH_CHARS;
    static constexpr float GRAPH_HEIGHT_LINES = 6;   // Height in line units

    // Graph grid line percentages
    static constexpr float GRID_LINE_80_PERCENT = 0.8f;
    static constexpr float GRID_LINE_60_PERCENT = 0.6f;
    static constexpr float GRID_LINE_40_PERCENT = 0.4f;
    static constexpr float GRID_LINE_20_PERCENT = 0.2f;
};
