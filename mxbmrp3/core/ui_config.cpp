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
}
