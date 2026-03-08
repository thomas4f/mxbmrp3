// ============================================================================
// hud/speed_widget.cpp
// Speed widget - displays speedometer (ground speed)
// ============================================================================
#include "speed_widget.h"

#include <cstdio>
#include <cmath>

#include "../diagnostics/logger.h"
#include "../diagnostics/timer.h"
#include "../core/plugin_utils.h"
#include "../core/color_config.h"

using namespace PluginConstants;

SpeedWidget::SpeedWidget()
{
    // One-time setup
    DEBUG_INFO("SpeedWidget created");
    setDraggable(true);
    m_strings.reserve(3);  // Title (optional) + speed value + units

    // Set texture base name for dynamic texture discovery
    setTextureBaseName("speed_widget");

    // Set all configurable defaults
    resetToDefaults();

    rebuildRenderData();
}

bool SpeedWidget::handlesDataType(DataChangeType dataType) const {
    // Update on telemetry changes (bike data)
    return dataType == DataChangeType::InputTelemetry ||
           dataType == DataChangeType::SpectateTarget;
}

void SpeedWidget::update() {
    // OPTIMIZATION: Skip processing when not visible
    if (!isVisible()) {
        clearDataDirty();
        clearLayoutDirty();
        return;
    }

    // Always rebuild - speed updates at high frequency (telemetry rate)
    // Rebuild is cheap (single snprintf), no need for caching
    rebuildRenderData();
    clearDataDirty();
    clearLayoutDirty();
}

float SpeedWidget::calculateContentHeight(const ScaledDimensions& dim) const {
    float height = 0.0f;
    if (m_bShowTitle) height += dim.lineHeightNormal;  // Title label
    height += dim.lineHeightLarge;  // Speed value always shown
    if (m_enabledRows & ROW_UNITS) height += dim.lineHeightNormal;
    return height;
}

void SpeedWidget::rebuildLayout() {
    // Fast path - only update positions (not colors/opacity)
    auto dim = getScaledDimensions();

    float startX = 0.0f;
    float startY = 0.0f;

    // Calculate dimensions using base helper
    float backgroundWidth = calculateBackgroundWidth(WidgetDimensions::SPEED_WIDTH);
    float contentWidth = PluginUtils::calculateMonospaceTextWidth(WidgetDimensions::SPEED_WIDTH, dim.fontSize);  // Needed for centering

    // Height calculation based on enabled rows
    float contentHeight = calculateContentHeight(dim);
    float backgroundHeight = dim.paddingV + contentHeight + dim.paddingV;

    // Set bounds for drag detection
    setBounds(startX, startY, startX + backgroundWidth, startY + backgroundHeight);

    // Update background quad position
    updateBackgroundQuadPosition(startX, startY, backgroundWidth, backgroundHeight);

    float contentStartX = startX + dim.paddingH;
    float contentStartY = startY + dim.paddingV;
    float currentY = contentStartY;

    float centerX = contentStartX + (contentWidth / 2.0f);

    int stringIndex = 0;

    // Title label (optional)
    if (m_bShowTitle && positionString(stringIndex, centerX, currentY)) {
        stringIndex++;
        currentY += dim.lineHeightNormal;
    }

    // Speed value (extra large font) - always shown, centered
    if (positionString(stringIndex, centerX, currentY)) {
        stringIndex++;
    }
    currentY += dim.lineHeightLarge;

    // Units label (normal font - 1 line) - centered
    if (m_enabledRows & ROW_UNITS) {
        positionString(stringIndex, centerX, currentY);
    }
}

void SpeedWidget::rebuildRenderData() {
    // Clear render data
    clearStrings();
    m_quads.clear();

    auto dim = getScaledDimensions();

    // Get bike telemetry data
    const PluginData& pluginData = PluginData::getInstance();
    const BikeTelemetryData& bikeData = pluginData.getBikeTelemetry();

    float startX = 0.0f;
    float startY = 0.0f;

    // Calculate dimensions using base helper
    float backgroundWidth = calculateBackgroundWidth(WidgetDimensions::SPEED_WIDTH);
    float contentWidth = PluginUtils::calculateMonospaceTextWidth(WidgetDimensions::SPEED_WIDTH, dim.fontSize);  // Needed for centering

    // Height calculation based on enabled rows
    float contentHeight = calculateContentHeight(dim);
    float backgroundHeight = dim.paddingV + contentHeight + dim.paddingV;

    // Add background quad
    addBackgroundQuad(startX, startY, backgroundWidth, backgroundHeight);

    float contentStartX = startX + dim.paddingH;
    float contentStartY = startY + dim.paddingV;
    float currentY = contentStartY;

    // Use full opacity for text
    unsigned long textColor = this->getColor(ColorSlot::PRIMARY);

    // Build speed value string
    char speedValueBuffer[8];

    if (!bikeData.isValid) {
        snprintf(speedValueBuffer, sizeof(speedValueBuffer), "%s", Placeholders::GENERIC);
    } else {
        int speed;
        if (m_speedUnit == SpeedUnit::KMH) {
            speed = static_cast<int>(bikeData.speedometer * UnitConversion::MS_TO_KMH + 0.5f);
        } else {
            speed = static_cast<int>(bikeData.speedometer * UnitConversion::MS_TO_MPH + 0.5f);
        }
        snprintf(speedValueBuffer, sizeof(speedValueBuffer), "%d", speed);
    }

    float centerX = contentStartX + (contentWidth / 2.0f);

    // Title label (optional)
    if (m_bShowTitle) {
        addString("Speed", centerX, currentY, Justify::CENTER,
            this->getFont(FontCategory::TITLE), textColor, dim.fontSize);
        currentY += dim.lineHeightNormal;
    }

    // Speed value (extra large font) - always shown, centered
    addString(speedValueBuffer, centerX, currentY, Justify::CENTER,
        this->getFont(FontCategory::TITLE), textColor, dim.fontSizeExtraLarge);
    currentY += dim.lineHeightLarge;

    // Add units label (normal font) - centered
    if (m_enabledRows & ROW_UNITS) {
        const char* unitsLabel = (m_speedUnit == SpeedUnit::KMH) ? "km/h" : "mph";
        addString(unitsLabel, centerX, currentY, Justify::CENTER,
            this->getFont(FontCategory::TITLE), textColor, dim.fontSize);
    }

    // Set bounds for drag detection
    setBounds(startX, startY, startX + backgroundWidth, startY + backgroundHeight);
}

void SpeedWidget::resetToDefaults() {
    m_bVisible = true;
    m_bShowTitle = false;  // Title disabled by default
    setTextureVariant(0);  // No texture by default
    m_fBackgroundOpacity = 0.0f;  // Transparent by default
    m_fScale = 1.0f;
    m_enabledRows = ROW_DEFAULT;  // Reset row visibility
    // Note: speedUnit is NOT reset here - it's a global preference, not per-profile
    setPosition(0.9405f, 0.8769f);
    setDataDirty();
}
