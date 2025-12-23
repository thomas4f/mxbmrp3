// ============================================================================
// hud/performance_hud.h
// Displays performance metrics including FPS and render timing diagnostics
// ============================================================================
#pragma once

#include "base_hud.h"
#include "../core/plugin_constants.h"
#include "../core/widget_constants.h"
#include <array>

class PerformanceHud : public BaseHud {
public:
    PerformanceHud();
    virtual ~PerformanceHud() = default;

    void update() override;
    bool handlesDataType(DataChangeType dataType) const override;
    void resetToDefaults();

    // Element flags - each bit represents a metric that can be toggled
    enum ElementFlags : uint32_t {
        ELEM_FPS = 1 << 0,  // FPS metric
        ELEM_CPU = 1 << 1,  // CPU time metric (plugin execution time)

        ELEM_DEFAULT = 0x3  // All metrics enabled (binary: 11)
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

    // Helper to render stats row (Current/Max/Avg/Min pattern used by FPS and CPU)
    void addStatsRow(float startX, float y, const ScaledDimensions& dims,
                     const char* currentLabel, float currentValue, int currentPrecision,
                     float maxValue, float avgValue, float minValue);

    // Calculate dynamic width based on enabled elements
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

    // Base position (0,0) - actual position comes from m_fOffsetX/m_fOffsetY
    static constexpr float START_X = 0.0f;
    static constexpr float START_Y = 0.0f;

    // Graph configuration
    static constexpr int GRAPH_HISTORY_SIZE = 120;  // Number of data points in performance graphs
    static constexpr int GRAPH_WIDTH_CHARS = 33;    // Width for graph display (left side)
    static constexpr int LEGEND_WIDTH_CHARS = 9;    // Width for legend/stats (right side) - fits "Max 12.34"
    static constexpr int BACKGROUND_WIDTH_CHARS = GRAPH_WIDTH_CHARS + 1 + LEGEND_WIDTH_CHARS;  // 33 + 1 gap + 9 = 43
    static constexpr float GRAPH_HEIGHT_LINES = 4;  // Height in line units (matches legend section height)

    // Graph scaling constants
    static constexpr float MAX_FPS_DISPLAY = 250.0f;  // FPS graph ceiling
    static constexpr float MAX_PLUGIN_TIME_MS = 4.0f; // Plugin time graph ceiling (4ms @ 144fps)

    // Layout constants for stats row rendering
    static constexpr int LABEL_WIDTH = 3;          // Width of stat labels ("FPS", "CPU", "Max", "Avg", "Min")
    static constexpr int SMALL_GAP = 1;            // Gap between label and value (1 space)
    static constexpr int LARGE_GAP = 2;            // Gap between stat pairs (2 spaces)
    static constexpr int INTEGER_VALUE_WIDTH = 3;  // Width for integer values (e.g., "144")
    static constexpr int DECIMAL_VALUE_WIDTH = 4;  // Width for decimal values (e.g., "1.23")

    // Value history for graphing
    std::array<float, GRAPH_HISTORY_SIZE> m_fpsHistory;
    std::array<float, GRAPH_HISTORY_SIZE> m_pluginTimeHistory;
    std::array<float, GRAPH_HISTORY_SIZE> m_pluginTimePercentHistory;  // Pre-calculated percentages
    int m_historyIndex;

    // Cached statistics
    float m_fpsMin;
    float m_fpsMax;
    float m_fpsAvg;
    float m_pluginTimeMsMin;   // Min plugin time in milliseconds (best case)
    float m_pluginTimeMsMax;   // Max plugin time in milliseconds (worst case)
    float m_pluginTimeMsAvg;   // Average plugin time in milliseconds

    // Incremental statistics tracking (performance optimization)
    float m_fpsSum;            // Running sum for average calculation
    float m_pluginTimeSum;     // Running sum for average calculation
    int m_validFpsCount;       // Count of valid FPS samples
    int m_validPluginTimeCount; // Count of valid plugin time samples
    int m_fpsMinIndex;         // Index of current min FPS value
    int m_fpsMaxIndex;         // Index of current max FPS value
    int m_pluginMinIndex;      // Index of current min plugin time value
    int m_pluginMaxIndex;      // Index of current max plugin time value

    // Helper methods for incremental statistics
    void recalculateFpsMinMax();
    void recalculatePluginTimeMinMax();

    uint32_t m_enabledElements = ELEM_DEFAULT;  // Bitfield of enabled metrics
    uint8_t m_displayMode = DISPLAY_DEFAULT;    // Display mode (graphs/values/both)
};
