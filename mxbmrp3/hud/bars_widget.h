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
    // Column visibility flags (bitfield) - configurable via INI
    static constexpr uint32_t COL_THROTTLE   = 1 << 0;  // T - Throttle
    static constexpr uint32_t COL_BRAKE      = 1 << 1;  // B - Brakes (front/rear)
    static constexpr uint32_t COL_CLUTCH     = 1 << 2;  // C - Clutch
    static constexpr uint32_t COL_RPM        = 1 << 3;  // R - RPM
    static constexpr uint32_t COL_SUSPENSION = 1 << 4;  // S - Suspension (front/rear)
    static constexpr uint32_t COL_FUEL       = 1 << 5;  // F - Fuel
    static constexpr uint32_t COL_DEFAULT    = COL_THROTTLE | COL_BRAKE | COL_CLUTCH | COL_RPM | COL_SUSPENSION | COL_FUEL;

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

    // Helper to add a horizontal max marker line
    void addMaxMarker(float x, float y, float barWidth, float barHeight, float maxValue);

    // Update max tracking for a bar (returns true if max was updated)
    void updateMaxTracking(int barIndex, float currentValue);

    // Base position (0,0) - actual position comes from m_fOffsetX/m_fOffsetY
    static constexpr float START_X = 0.0f;
    static constexpr float START_Y = 0.0f;

    // Bar dimensions (in characters/lines)
    static constexpr int BAR_WIDTH_CHARS = 1;        // Width of each bar (1 char)
    static constexpr float BAR_HEIGHT_LINES = 4.0f;  // Height of bars (4 lines)
    static constexpr float BAR_SPACING_CHARS = 0.4f; // Space between bars (0.4 char) - tuned so 6 bars = 8 chars total
    static constexpr int LABEL_HEIGHT_LINES = 1;     // Height for labels at bottom

    // Max marker constants
    static constexpr int NUM_BARS = 6;  // Number of trackable bars

    // Max marker tracking for each bar (index matches bar order: T, B, C, R, S, F)
    // Markers show when value starts decreasing, hide when increasing
    float m_maxValues[NUM_BARS] = {0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f};       // Overall max (for reference)
    float m_markerValues[NUM_BARS] = {0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f};    // Current marker position
    float m_prevValues[NUM_BARS] = {0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f};      // Previous frame values
    int m_maxFramesRemaining[NUM_BARS] = {0, 0, 0, 0, 0, 0};

    // Settings (configurable via INI)
    uint32_t m_enabledColumns = COL_DEFAULT;  // Bitfield of enabled bars
    bool m_bShowMaxMarkers = false;           // Show peak value markers (default OFF)
    int m_maxMarkerLingerFrames = 60;         // How long max markers linger (~frames, 60 = 1 second at 60fps)
};
