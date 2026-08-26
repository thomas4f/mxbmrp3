// ============================================================================
// hud/position_widget.h
// Position widget - displays rider position in minimal format (e.g., "1/24")
// ============================================================================
#pragma once

#include "base_hud.h"
#include "../core/plugin_data.h"
#include "../core/plugin_constants.h"
#include "../core/widget_constants.h"

class PositionWidget : public BaseHud {
public:
    PositionWidget();
    virtual ~PositionWidget() = default;

    void update() override;
    bool handlesDataType(DataChangeType dataType) const override;
    void resetToDefaults();

protected:
    void rebuildLayout() override;

private:
    void rebuildRenderData() override;

    // Calculate player's current position (1-based)
    int calculatePlayerPosition() const;
};
