// ============================================================================
// hud/time_widget.h
// Time widget - displays label + time in two rows (countdown or countup)
// ============================================================================
#pragma once

#include "base_hud.h"
#include "../core/plugin_data.h"
#include "../core/plugin_constants.h"
#include "../core/widget_constants.h"

class TimeWidget : public BaseHud {
public:
    TimeWidget();
    virtual ~TimeWidget() = default;

    void update() override;
    bool handlesDataType(DataChangeType dataType) const override;
    void resetToDefaults();

    // Session type display toggle
    void setShowSessionType(bool show) {
        if (m_bShowSessionType != show) {
            m_bShowSessionType = show;
            setDataDirty();
        }
    }
    bool getShowSessionType() const { return m_bShowSessionType; }

protected:
    void rebuildLayout() override;

private:
    void rebuildRenderData() override;

    // Cached rendered time to avoid unnecessary rebuilds
    int m_cachedRenderedTime;

    // Cached session info to detect changes
    int m_cachedEventType;
    int m_cachedSession;

    // Configuration
    bool m_bShowSessionType;  // Show session type (Practice, Warmup, etc.) below counter
};
