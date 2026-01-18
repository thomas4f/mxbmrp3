// ============================================================================
// core/ui_config.h
// User-configurable UI behavior settings (grid snapping, screen clamping, etc.)
// ============================================================================
#pragma once

class UiConfig {
public:
    static UiConfig& getInstance();

    // Grid snapping setting (for HUD positioning)
    bool getGridSnapping() const { return m_bGridSnapping; }
    void setGridSnapping(bool enabled) { m_bGridSnapping = enabled; }

    // Screen clamping setting (keeps HUDs within screen bounds when dragging)
    bool getScreenClamping() const { return m_bScreenClamping; }
    void setScreenClamping(bool enabled) { m_bScreenClamping = enabled; }

    // Reset all settings to defaults
    void resetToDefaults();

private:
    UiConfig();
    ~UiConfig() = default;
    UiConfig(const UiConfig&) = delete;
    UiConfig& operator=(const UiConfig&) = delete;

    bool m_bGridSnapping = true;    // Grid snapping enabled by default
    bool m_bScreenClamping = true;  // Screen clamping enabled by default
};
