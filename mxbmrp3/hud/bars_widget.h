// ============================================================================
// hud/bars_widget.h
// Bars Widget - displays 6 vertical bars (left to right):
//   - T: Throttle (green)
//   - B: Brakes (split: red front | dark red rear)
//   - C: Clutch (blue)
//   - R: RPM (gray)
//   - S: Suspension (split: purple front | dark purple rear)
//   - F: Fuel (yellow)
// ============================================================================
#pragma once

#include "base_hud.h"
#include "../core/plugin_data.h"
#include "../core/plugin_constants.h"
#include "../core/widget_constants.h"

class BarsWidget : public BaseHud {
public:
    BarsWidget();
    virtual ~BarsWidget() = default;

    void update() override;
    bool handlesDataType(DataChangeType dataType) const override;
    void resetToDefaults();

    // Allow SettingsHud and SettingsManager to access private members
    friend class SettingsHud;
    friend class SettingsManager;

protected:
    void rebuildLayout() override;

private:
    void rebuildRenderData() override;

    // Helper to add a single vertical bar
    void addVerticalBar(float x, float y, float barWidth, float barHeight,
                        float value, unsigned long color);

    // Widget positioning (grid-aligned)
    static constexpr float START_X = HudPositions::LEFT_SIDE_X;
    static constexpr float START_Y = 0.3552f;  // Between MID_UPPER and MID_LOWER

    // Bar dimensions (in characters/lines)
    static constexpr int BAR_WIDTH_CHARS = 1;        // Width of each bar (1 char)
    static constexpr float BAR_HEIGHT_LINES = 4.0f;  // Height of bars (4 lines)
    static constexpr float BAR_SPACING_CHARS = 0.5f; // Space between bars (0.5 char)
    static constexpr int LABEL_HEIGHT_LINES = 1;     // Height for labels at bottom
};
