// ============================================================================
// hud/fuel_widget.h
// Fuel calculator widget - displays fuel level, avg consumption, and estimated laps
// ============================================================================
#pragma once

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

        ROW_DEFAULT = 0x0F  // All 4 rows enabled (binary: 1111)
    };

    FuelWidget();
    virtual ~FuelWidget() = default;

    void update() override;
    bool handlesDataType(DataChangeType dataType) const override;
    void resetToDefaults();

    // Called when a new session starts to reset fuel tracking
    void resetFuelTracking();

    // Fuel unit setting
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
    void updateFuelTracking();

    // Fuel tracking state
    float m_fuelAtRunStart;           // Fuel level when run started (for total used calculation)
    float m_fuelAtLapStart;           // Fuel level when current lap started
    int m_lastTrackedLapNum;          // Last lap number we tracked fuel for
    bool m_bTrackingActive;           // True if we're actively tracking fuel consumption

    // Fuel consumption history (stores fuel used per lap)
    static constexpr size_t MAX_FUEL_HISTORY = 10;  // Keep last 10 laps for averaging
    std::vector<float> m_fuelPerLap;  // Fuel consumed per lap (most recent at back)
    size_t m_totalLapsRecorded;       // Total laps ever recorded (to know if first lap is still in buffer)
};
