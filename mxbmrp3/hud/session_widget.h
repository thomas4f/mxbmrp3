// ============================================================================
// hud/session_widget.h
// Session widget - displays session and state (e.g., "RACE 2 / In Progress")
// ============================================================================
#pragma once

#include "base_hud.h"
#include "../core/plugin_data.h"
#include "../core/plugin_constants.h"
#include "../core/widget_constants.h"

class SessionWidget : public BaseHud {
public:
    // Row visibility flags (configurable via INI file)
    enum RowFlags : uint32_t {
        ROW_TYPE   = 1 << 0,  // Session type (e.g., "PRACTICE", "RACE 2")
        ROW_TRACK  = 1 << 1,  // Track name
        ROW_FORMAT = 1 << 2,  // Format + Session state (e.g., "10:00 + 2 Laps, In Progress")

        ROW_DEFAULT = 0x07    // All 3 rows enabled (binary: 111)
    };

    SessionWidget();
    virtual ~SessionWidget() = default;

    void update() override;
    bool handlesDataType(DataChangeType dataType) const override;
    void resetToDefaults();

    // Public for settings access
    uint32_t m_enabledRows = ROW_DEFAULT;

    // Helper to count enabled rows
    int getEnabledRowCount() const;

protected:
    void rebuildLayout() override;

private:
    void rebuildRenderData() override;

    // Helper to calculate content height based on enabled rows
    float calculateContentHeight(const ScaledDimensions& dim) const;

    // Cached data to avoid unnecessary rebuilds
    int m_cachedEventType;
    int m_cachedSession;
    int m_cachedSessionState;
    int m_cachedSessionLength;
    int m_cachedSessionNumLaps;
};
