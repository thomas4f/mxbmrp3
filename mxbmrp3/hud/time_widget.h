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

protected:
    void rebuildLayout() override;

private:
    void rebuildRenderData() override;

    // Cached rendered time to avoid unnecessary rebuilds
    int m_cachedRenderedTime;
};
