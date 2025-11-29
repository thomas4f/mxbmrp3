// ============================================================================
// hud/speed_widget.h
// Speed widget - displays speedometer (ground speed)
// ============================================================================
#pragma once

#include "base_hud.h"
#include "../core/plugin_data.h"
#include "../core/plugin_constants.h"
#include "../core/widget_constants.h"

class SpeedWidget : public BaseHud {
public:
    SpeedWidget();
    virtual ~SpeedWidget() = default;

    void update() override;
    bool handlesDataType(DataChangeType dataType) const override;
    void resetToDefaults();

protected:
    void rebuildLayout() override;

private:
    void rebuildRenderData() override;
};
