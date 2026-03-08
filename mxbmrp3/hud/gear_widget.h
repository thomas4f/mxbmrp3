// ============================================================================
// hud/gear_widget.h
// Gear widget - displays current gear with shift/limiter indicators
// ============================================================================
#pragma once

#include "base_hud.h"
#include "../core/plugin_data.h"
#include "../core/plugin_constants.h"
#include "../core/widget_constants.h"

class GearWidget : public BaseHud {
public:
    GearWidget();
    virtual ~GearWidget() = default;

    void update() override;
    bool handlesDataType(DataChangeType dataType) const override;
    void resetToDefaults();

    // INI-only settings
    bool m_bShowShiftColor = true;     // Red gear text at shift RPM
    bool m_bShowLimiterCircle = true;  // Circle indicator at limiter RPM

protected:
    void rebuildLayout() override;

private:
    void rebuildRenderData() override;
};
