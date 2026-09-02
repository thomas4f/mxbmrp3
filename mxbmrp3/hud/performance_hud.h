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
    const char* getIconName() const override { return "hud-performance"; }
    void resetToDefaults();

    // Element flags - each bit represents a metric that can be toggled
    enum ElementFlags : uint32_t {
        ELEM_FPS = 1 << 0,  // FPS metric
        // The plugin's own per-frame work, in milliseconds. NOT process or
        // system CPU, and not a utilisation percentage - the enumerator keeps
        // the name because the INI key elem_cpu is on disk in every user's
        // settings, but nothing user-facing says "CPU", because both halves of
        // that are wrong: it is elapsed time, ours alone, and under
        // [Advanced] pluginThread it is the WORKER's build cost rather than
        // anything the game thread paid. See DebugMetrics in plugin_data_types.h.
        ELEM_CPU = 1 << 1,

        ELEM_DEFAULT = ELEM_CPU  // plugin time enabled by default
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
    // Test-only: the shipped default is CPU alone, which is ONE section -- so against
    // it the themed geometry gates assert whole-cell heights and no-overlap on a panel
    // that has no section boundary at all, and pass for the wrong reason. Forcing both
    // elements on is the only way to give this HUD the shape those gates exist to check.
    // See core/test_hooks.cpp, which is excluded from every shipping target.
    friend void MXBMRP3_Test_SetPerformanceElementsImpl(unsigned int);

private:
    // Clears the graph history on the ANY-SURFACE show edge; see the definition.
    void syncVisibilityEdge();
    void onVisibilityChanged() override { syncVisibilityEdge(); }
    // Edge state for the above: visible on SOME surface last frame. Not m_bVisible,
    // which is the game surface alone.
    bool m_bWasVisibleAnySurface = false;

    void rebuildRenderData() override;

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
    // One section fills 10 rows so a single-section HUD is title(2) + subheading(1)
    // + 10 + padding(2) = 15 rows, matching the StandingsHud default height. (Two
    // sections stack taller, like the Session Charts HUD.)
    // Graph HEIGHT, in text rows -- a user setting, not a constant. This is the knob
    // that makes the panel shrink and grow the way Standings' "Rows to show" does;
    // with a fixed line count the only way to change this HUD's height is the scale
    // slider, which changes everything else with it.
    //
    // Rows rather than pixels so it lands on the same lattice every other panel uses:
    // one row is lineHeightNormal, so any value here keeps the panel a whole number of
    // grid cells tall and it still tiles with the HUDs beside it.
    static constexpr int MIN_GRAPH_ROWS = 3;
    static constexpr int MAX_GRAPH_ROWS = 30;
    static constexpr int DEFAULT_GRAPH_ROWS = 10;
    int m_graphRows = DEFAULT_GRAPH_ROWS;

    // Graph scaling constants
    static constexpr float MAX_FPS_DISPLAY = 250.0f;  // FPS graph ceiling
    // Plugin-time graph ceiling: THE FRAME BUDGET, so the top of the graph means
    // "this frame is gone" rather than an arbitrary number, and it follows the target
    // -- a ceiling tuned for a slower frame rate keeps the whole graph below the
    // quarter line and shows the ENTIRE 480fps budget as safe.
    //
    // Static rather than auto-scaling on purpose: a dynamic ceiling rescales the moment
    // anything spikes, so the trace looks the same at 0.2ms and at 2ms and a glance
    // tells you nothing. A fixed budget line means the height IS the reading.
    static constexpr float MAX_PLUGIN_TIME_MS = PluginConstants::FRAME_BUDGET_MS;
    // Bands as FRACTIONS of that budget, so they follow it: comfortable below half,
    // worth watching to three quarters, over budget above.
    static constexpr float PLUGIN_TIME_WARN_FRAC = 0.5f;
    static constexpr float PLUGIN_TIME_BAD_FRAC  = 0.75f;

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
