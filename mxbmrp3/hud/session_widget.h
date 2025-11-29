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
    SessionWidget();
    virtual ~SessionWidget() = default;

    void update() override;
    bool handlesDataType(DataChangeType dataType) const override;
    void resetToDefaults();

protected:
    void rebuildLayout() override;

private:
    void rebuildRenderData() override;

    // Cached data to avoid unnecessary rebuilds
    int m_cachedEventType;
    int m_cachedSession;
    int m_cachedSessionState;
    int m_cachedSessionLength;
    int m_cachedSessionNumLaps;
};
