// ============================================================================
// core/ui_config.cpp
// User-configurable UI behavior settings (grid snapping, screen clamping, etc.)
// ============================================================================
#include "ui_config.h"

UiConfig& UiConfig::getInstance() {
    static UiConfig instance;
    return instance;
}

UiConfig::UiConfig() {
    resetToDefaults();
}

void UiConfig::resetToDefaults() {
    m_bGridSnapping = true;
    m_bScreenClamping = false;
    m_bAutoSave = true;
    m_temperatureUnit = TemperatureUnit::CELSIUS;
    m_holdRepeatFastMs = 50;
    m_bDropShadow = true;
    m_fDropShadowOffsetX = 0.03f;
    m_fDropShadowOffsetY = 0.04f;
    m_ulDropShadowColor = 0xAA000000;
}
