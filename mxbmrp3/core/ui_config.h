// ============================================================================
// core/ui_config.h
// User-configurable UI behavior settings (grid snapping, screen clamping, etc.)
// ============================================================================
#pragma once

#include <cstdint>

// Temperature unit options (used by SessionHud weather display)
enum class TemperatureUnit : uint8_t {
    CELSIUS = 0,
    FAHRENHEIT = 1
};

class UiConfig {
public:
    static UiConfig& getInstance();

    // Grid snapping setting (for HUD positioning)
    bool getGridSnapping() const { return m_bGridSnapping; }
    void setGridSnapping(bool enabled) { m_bGridSnapping = enabled; }

    // Screen clamping setting (keeps HUDs within screen bounds when dragging)
    bool getScreenClamping() const { return m_bScreenClamping; }
    void setScreenClamping(bool enabled) { m_bScreenClamping = enabled; }

    // Auto-save setting (automatically save settings on every change)
    bool getAutoSave() const { return m_bAutoSave; }
    void setAutoSave(bool enabled) { m_bAutoSave = enabled; }

    // Temperature unit setting (used by SessionHud weather display)
    TemperatureUnit getTemperatureUnit() const { return m_temperatureUnit; }
    void setTemperatureUnit(TemperatureUnit unit) { m_temperatureUnit = unit; }

    // Reset all settings to defaults
    void resetToDefaults();

private:
    UiConfig();
    ~UiConfig() = default;
    UiConfig(const UiConfig&) = delete;
    UiConfig& operator=(const UiConfig&) = delete;

    bool m_bGridSnapping = true;    // Grid snapping enabled by default
    bool m_bScreenClamping = false;  // Screen clamping disabled by default
    bool m_bAutoSave = true;         // Auto-save enabled by default
    TemperatureUnit m_temperatureUnit = TemperatureUnit::CELSIUS;  // Celsius by default
};
