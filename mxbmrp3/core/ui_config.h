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

    // Hold-to-repeat max speed (ms between repeats at full acceleration)
    int getHoldRepeatFastMs() const { return m_holdRepeatFastMs; }
    void setHoldRepeatFastMs(int ms) { m_holdRepeatFastMs = (ms < 10) ? 10 : (ms > 500) ? 500 : ms; }

    // Drop shadow settings (for text rendering)
    bool getDropShadow() const { return m_bDropShadow; }
    void setDropShadow(bool enabled) { m_bDropShadow = enabled; }
    float getDropShadowOffsetX() const { return m_fDropShadowOffsetX; }
    float getDropShadowOffsetY() const { return m_fDropShadowOffsetY; }
    unsigned long getDropShadowColor() const { return m_ulDropShadowColor; }
    void setDropShadowOffsetX(float offset) { m_fDropShadowOffsetX = offset; }
    void setDropShadowOffsetY(float offset) { m_fDropShadowOffsetY = offset; }
    void setDropShadowColor(unsigned long color) { m_ulDropShadowColor = color; }

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
    int m_holdRepeatFastMs = 50;     // Max repeat speed: 50ms (~20/sec)

    // Drop shadow settings
    bool m_bDropShadow = true;                       // Drop shadow enabled by default
    float m_fDropShadowOffsetX = 0.03f;              // 3% of font size
    float m_fDropShadowOffsetY = 0.04f;              // 4% of font size
    unsigned long m_ulDropShadowColor = 0xAA000000;  // Semi-transparent black
};
