// ============================================================================
// hud/lap_widget.h
// Lap widget - displays current lap in minimal format (e.g., "L2/5" or "L2")
// Shows "Lx/y" for lap-only sessions, "Lx" for time-based or time+laps sessions
// ============================================================================
#pragma once

#include "base_hud.h"
#include "../core/plugin_data.h"
#include "../core/plugin_constants.h"
#include "../core/widget_constants.h"

class LapWidget : public BaseHud {
public:
    LapWidget();
    virtual ~LapWidget() = default;

    void update() override;
    bool handlesDataType(DataChangeType dataType) const override;
    void resetToDefaults();

protected:
    void rebuildLayout() override;

private:
    void rebuildRenderData() override;

    // Cached lap data to avoid unnecessary rebuilds
    int m_cachedCurrentLap;
    int m_cachedTotalLaps;
    int m_cachedSessionLength;
};
