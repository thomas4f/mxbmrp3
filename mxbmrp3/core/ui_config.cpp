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
    setThemeName(std::string());   // the SETTER, so the theme generation bumps with it
    m_bScreenClamping = false;
    m_bMenuOnlyCursor = false;
    m_bAutoSave = true;
    m_temperatureUnit = TemperatureUnit::CELSIUS;
    m_pbScope = PBScope::CATEGORY;
    m_bSnapSegmentsToSplits = true;
    m_fSegmentSnapThreshold = 0.02f;
    m_holdRepeatFastMs = 50;
    m_fCursorActivationThreshold = 0.015f;
    m_bTitleIcons = true;
    m_bGridOverlay = false;
    m_gridOverlayMajorEvery = 10;
    m_ulGridOverlayColor = 0x22FFFFFF;
    m_ulGridOverlayMajorColor = 0x9933CCFF;
    m_bDropShadow = false;
    m_fDropShadowOffsetX = 0.03f;
    m_fDropShadowOffsetY = 0.04f;
    m_ulDropShadowColor = 0xAA000000;
}

void UiConfig::setThemeName(const std::string& name) {
    if (m_themeName == name) return;
    m_themeName = name;
    // Every HUD memoises the ThemeAsset* it resolved; the bump is what makes them
    // all re-resolve. Without it a theme change would leave the old sprites, layout
    // and palette in place until something else happened to invalidate the cache.
    mxbBumpThemeGeneration();
}
