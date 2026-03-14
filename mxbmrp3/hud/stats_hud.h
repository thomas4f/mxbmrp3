// ============================================================================
// hud/stats_hud.h
// On-screen stats widget — shows session stats for current bike/track combo
// ============================================================================
#pragma once

#include "base_hud.h"

class StatsHud : public BaseHud {
public:
    // Visibility modes
    enum class VisibilityMode : uint8_t {
        ALWAYS = 0,          // Always visible when on track
        SESSION_END = 1,     // Show when player finishes
        COUNT = 2
    };

    StatsHud();
    virtual ~StatsHud() = default;

    void update() override;
    bool handlesDataType(DataChangeType dataType) const override;
    bool needsFrequentUpdates() const override;
    int getTickIntervalMs() const override;
    void resetToDefaults();

    static const char* getVisibilityModeName(VisibilityMode mode);

    // Number of enabled columns (0-3)
    int getColumnCount() const;

    // Allow SettingsHud and SettingsManager to access private members
    friend class SettingsHud;
    friend class SettingsManager;

    // Public for settings access
    VisibilityMode m_visibilityMode = VisibilityMode::ALWAYS;
    bool m_showLap = true;
    bool m_showSession = true;
    bool m_showAllTime = false;

protected:
    void rebuildLayout() override;

private:
    void rebuildRenderData() override;
    int getRowCount() const;

    // Shared geometry computed from current column/row/scale state
    struct Layout {
        int cols;
        float backgroundWidth, backgroundHeight;
        float titleHeight;
        float contentStartX, contentStartY;
        float col[3];  // right-edge X positions for up to 3 columns
        ScaledDimensions dim;
    };
    bool computeLayout(Layout& out) const;

    bool m_finishAutoShown = false;  // Tracks whether "On Finish" mode auto-showed the HUD
    bool m_wasInPits = false;        // Tracks pit entry after finish (hide on next track re-entry)

    static constexpr int LABEL_WIDTH_CHARS = 8;     // Label reserve (longer labels extend into column space)
    static constexpr int COLUMN_WIDTH_CHARS = 9;    // Each data column (right-aligned, fits "102 km/h" + gap)
    static constexpr int DATA_ROWS = 10;      // Best lap, Laps, Riding time, Distance, Crashes, Gear shifts, Penalties, Pen. time, Top speed, Avg speed
    static constexpr int STATS_TICK_INTERVAL_MS = 1000;  // ~1Hz rebuild for live distance/time
};
