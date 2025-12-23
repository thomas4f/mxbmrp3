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

    // Row visibility flags (configurable via INI file)
    enum RowFlags : uint32_t {
        ROW_SPEED = 1 << 0,  // Speed value (large, 2 lines)
        ROW_UNITS = 1 << 1,  // Units label (km/h or mph)
        ROW_GEAR  = 1 << 2,  // Gear indicator

        ROW_DEFAULT = 0x07   // All 3 rows enabled (binary: 111)
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
    uint32_t m_enabledRows = ROW_DEFAULT;  // Bitfield of enabled rows (INI-configurable)

    // Helper to calculate content height based on enabled rows
    float calculateContentHeight(const ScaledDimensions& dim) const;

protected:
    void rebuildLayout() override;

private:
    void rebuildRenderData() override;
};
