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
#include "../core/asset_manager.h"

using namespace PluginConstants;

SpeedWidget::SpeedWidget()
{
    // One-time setup
    DEBUG_INFO("SpeedWidget created");
    setDraggable(true);
    m_strings.reserve(4);

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
    // Always rebuild - speed updates at high frequency (telemetry rate)
    // Rebuild is cheap (single snprintf), no need for caching
    rebuildRenderData();
    clearDataDirty();
    clearLayoutDirty();
}

float SpeedWidget::calculateContentHeight(const ScaledDimensions& dim) const {
    float height = 0.0f;
    if (m_enabledRows & ROW_SPEED) height += dim.lineHeightLarge;
    if (m_enabledRows & ROW_UNITS) height += dim.lineHeightNormal;
    if (m_enabledRows & ROW_GEAR)  height += dim.lineHeightNormal;
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

    // Position strings if they exist
    int stringIndex = 0;
    float centerX = contentStartX + (contentWidth / 2.0f);

    // Speed value (extra large font - spans 2 lines) - centered
    if (m_enabledRows & ROW_SPEED) {
        if (positionString(stringIndex, centerX, currentY)) {
            stringIndex++;
        }
        currentY += dim.lineHeightLarge;
    }

    // Units label (normal font - 1 line) - centered
    if (m_enabledRows & ROW_UNITS) {
        if (positionString(stringIndex, centerX, currentY)) {
            stringIndex++;
        }
        currentY += dim.lineHeightNormal;
    }

    // Gear (large font but normal line height) - centered
    if (m_enabledRows & ROW_GEAR) {
        positionString(stringIndex, centerX, currentY);

        // Update gear circle quad position if it exists (quads[0] = background, quads[1] = gear circle if present)
        if (m_quads.size() > 1) {
            float circleSize = dim.fontSizeLarge * 1.5f;
            float circleWidth = circleSize / PluginConstants::UI_ASPECT_RATIO;
            float circleHeight = circleSize;

            float circleX = centerX - (circleWidth / 2.0f);
            float circleTopY = currentY + (dim.lineHeightNormal - circleHeight) / 2.0f;

            applyOffset(circleX, circleTopY);
            setQuadPositions(m_quads[1], circleX, circleTopY, circleWidth, circleHeight);
        }
    }
}

void SpeedWidget::rebuildRenderData() {
    // Clear render data
    m_strings.clear();
    m_quads.clear();

    auto dim = getScaledDimensions();

    // Get bike telemetry data and session data (for shift RPM threshold)
    const PluginData& pluginData = PluginData::getInstance();
    const BikeTelemetryData& bikeData = pluginData.getBikeTelemetry();
    const SessionData& sessionData = pluginData.getSessionData();

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
    unsigned long textColor = ColorConfig::getInstance().getPrimary();

    // Build speed value string and gear string separately
    char speedValueBuffer[64];
    char gearValueBuffer[64];

    if (!bikeData.isValid) {
        // Show placeholder when telemetry data is not available
        snprintf(speedValueBuffer, sizeof(speedValueBuffer), "%s", Placeholders::GENERIC);
        snprintf(gearValueBuffer, sizeof(gearValueBuffer), "");
    } else {
        // Convert speedometer based on unit setting
        int speed;
        if (m_speedUnit == SpeedUnit::KMH) {
            speed = static_cast<int>(bikeData.speedometer * UnitConversion::MS_TO_KMH + 0.5f);
        } else {
            speed = static_cast<int>(bikeData.speedometer * UnitConversion::MS_TO_MPH + 0.5f);
        }
        snprintf(speedValueBuffer, sizeof(speedValueBuffer), "%d", speed);

        // Display gear: NEUTRAL = N, 1-6 = gear number
        if (bikeData.gear == GearValue::NEUTRAL) {
            snprintf(gearValueBuffer, sizeof(gearValueBuffer), "N");
        } else {
            snprintf(gearValueBuffer, sizeof(gearValueBuffer), "%d", bikeData.gear);
        }
    }

    // Calculate center X for centering elements
    float centerX = contentStartX + (contentWidth / 2.0f);

    // Add speed value (extra large font - spans 2 lines) - centered
    if (m_enabledRows & ROW_SPEED) {
        addString(speedValueBuffer, centerX, currentY, Justify::CENTER,
            Fonts::getTitle(), textColor, dim.fontSizeExtraLarge);
        currentY += dim.lineHeightLarge;
    }

    // Add units label (normal font) - centered
    if (m_enabledRows & ROW_UNITS) {
        const char* unitsLabel = (m_speedUnit == SpeedUnit::KMH) ? "km/h" : "mph";
        addString(unitsLabel, centerX, currentY, Justify::CENTER,
            Fonts::getTitle(), textColor, dim.fontSize);
        currentY += dim.lineHeightNormal;
    }

    // Add gear indicator
    if (m_enabledRows & ROW_GEAR) {
        // Add gear circle indicator if limiter RPM is reached (behind gear text)
        // Only show when viewing player's bike (limiter RPM data only available for player's bike)
        // Skip if limiterRPM is 0 (some bikes don't report this value)
        bool isViewingPlayerBike = (pluginData.getDisplayRaceNum() == pluginData.getPlayerRaceNum());
        bool isLimiterHit = (bikeData.isValid && isViewingPlayerBike && sessionData.limiterRPM > 0 && bikeData.rpm >= sessionData.limiterRPM);

        if (isLimiterHit) {
            // Calculate gear circle quad position (centered behind gear text)
            float circleSize = dim.fontSizeLarge * 1.5f;  // Circle slightly larger than gear text
            float circleWidth = circleSize / PluginConstants::UI_ASPECT_RATIO;
            float circleHeight = circleSize;

            // Center the circle at the gear text position
            float circleX = centerX - (circleWidth / 2.0f);
            float circleTopY = currentY + (dim.lineHeightNormal - circleHeight) / 2.0f;

            // Add gear circle quad (behind the text, so added first)
            SPluginQuad_t circleQuad;
            applyOffset(circleX, circleTopY);
            setQuadPositions(circleQuad, circleX, circleTopY, circleWidth, circleHeight);
            circleQuad.m_iSprite = AssetManager::getInstance().getSpriteIndex("gear_circle", 1);
            circleQuad.m_ulColor = ColorPalette::WHITE;  // Includes full alpha by default
            m_quads.push_back(circleQuad);
        }

        // Add gear value (large font but normal line height) - centered
        // Color: negative (red) if recommended shift point is reached, otherwise primary
        // Gear circle sprite appears at limiter (higher RPM threshold)
        // Skip color change if shiftRPM is 0 (some bikes don't report this value)
        bool isViewingPlayer = (pluginData.getDisplayRaceNum() == pluginData.getPlayerRaceNum());
        unsigned long gearColor = (bikeData.isValid && isViewingPlayer && sessionData.shiftRPM > 0 && bikeData.rpm >= sessionData.shiftRPM)
            ? ColorConfig::getInstance().getNegative()
            : textColor;
        addString(gearValueBuffer, centerX, currentY, Justify::CENTER,
            Fonts::getTitle(), gearColor, dim.fontSizeLarge);
    }

    // Set bounds for drag detection
    setBounds(startX, startY, startX + backgroundWidth, startY + backgroundHeight);
}

void SpeedWidget::resetToDefaults() {
    m_bVisible = true;
    m_bShowTitle = false;  // No title rendered (widget design doesn't support titles)
    setTextureVariant(0);  // No texture by default
    m_fBackgroundOpacity = 1.0f;  // Full opacity
    m_fScale = 1.0f;
    m_enabledRows = ROW_DEFAULT;  // Reset row visibility
    // Note: speedUnit is NOT reset here - it's a global preference, not per-profile
    setPosition(0.7865f, 0.8547f);
    setDataDirty();
}
