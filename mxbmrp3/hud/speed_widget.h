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
    // Speed unit options
    enum class SpeedUnit : uint8_t {
        MPH = 0,
        KMH = 1
    };

    SpeedWidget();
    virtual ~SpeedWidget() = default;

    void update() override;
    bool handlesDataType(DataChangeType dataType) const override;
    void resetToDefaults();

    // Speed unit setting
    SpeedUnit getSpeedUnit() const { return m_speedUnit; }
    void setSpeedUnit(SpeedUnit unit) { m_speedUnit = unit; setDataDirty(); }

    // Public for settings access
    SpeedUnit m_speedUnit = SpeedUnit::MPH;

protected:
    void rebuildLayout() override;

private:
    void rebuildRenderData() override;
};
