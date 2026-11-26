// ============================================================================
// hud/fuel_widget.h
// Fuel calculator widget - displays fuel level, avg consumption, and estimated laps
// ============================================================================
#pragma once

#include "../core/fuel_estimate.h"

#include "base_hud.h"
#include "../core/plugin_data.h"
#include "../core/plugin_constants.h"
#include "../core/widget_constants.h"
#include <vector>

class FuelWidget : public BaseHud {
public:
    // Fuel unit options
    enum class FuelUnit : uint8_t {
        LITERS = 0,
        GALLONS = 1
    };

    // Row visibility flags (configurable via INI file)
    enum RowFlags : uint32_t {
        ROW_FUEL = 1 << 0,  // Current fuel level
        ROW_USED = 1 << 1,  // Total fuel used this run
        ROW_AVG  = 1 << 2,  // Average fuel per lap
        ROW_EST  = 1 << 3,  // Estimated laps remaining

        ROW_DEFAULT = 0x0D  // Fuel, Avg, Est enabled (binary: 1101) - Use row INI-only
    };

    FuelWidget();
    virtual ~FuelWidget() = default;

    void update() override;
    bool handlesDataType(DataChangeType dataType) const override;
    void resetToDefaults();

    FuelUnit getFuelUnit() const { return m_fuelUnit; }
    void setFuelUnit(FuelUnit unit) { m_fuelUnit = unit; setDataDirty(); }

    // Public for settings access
    FuelUnit m_fuelUnit = FuelUnit::LITERS;
    uint32_t m_enabledRows = ROW_DEFAULT;  // Bitfield of enabled rows (INI-configurable)

    // Helper to count enabled rows
    int getEnabledRowCount() const;

protected:
    void rebuildLayout() override;

private:
    void rebuildRenderData() override;
};
