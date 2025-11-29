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

using namespace PluginConstants;

SpeedWidget::SpeedWidget()
{
    initializeWidget("SpeedWidget", 4, 1.0f);  // Four strings, full opacity background
    m_bShowTitle = false;  // No title rendered (widget design doesn't support titles)
    setPosition(0.4125f, 0.6882f);
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

void SpeedWidget::rebuildLayout() {
    // Fast path - only update positions (not colors/opacity)
    auto dim = getScaledDimensions();

    float startX = WidgetPositions::WIDGET_STACK_X;
    float startY = WidgetPositions::SPEED_Y;

    // Calculate dimensions using base helper
    float backgroundWidth = calculateBackgroundWidth(WidgetDimensions::SPEED_WIDTH);
    float contentWidth = PluginUtils::calculateMonospaceTextWidth(WidgetDimensions::SPEED_WIDTH, dim.fontSize);  // Needed for centering

    // Height calculation is widget-specific due to lineHeightLarge value display
    float labelHeight = 0.0f;  // No title (widget design doesn't support titles)
    float contentHeight = labelHeight + dim.lineHeightLarge + dim.lineHeightNormal + dim.lineHeightNormal;  // Speed (2 lines) + Units (1 line) + Gear (1 line)
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
    if (positionString(stringIndex, centerX, currentY)) {
        stringIndex++;
    }

    // Units label (normal font - 1 line) - centered
    if (positionString(stringIndex, centerX, currentY + dim.lineHeightLarge)) {
        stringIndex++;
    }

    // Gear (large font but normal line height) - centered
    positionString(stringIndex, centerX, currentY + dim.lineHeightLarge + dim.lineHeightNormal);

    // Update gear circle quad position if it exists (quads[0] = background, quads[1] = gear circle if present)
    if (m_quads.size() > 1) {
        float gearY = currentY + dim.lineHeightLarge + dim.lineHeightNormal;
        float circleSize = dim.fontSizeLarge * 1.5f;
        float circleWidth = circleSize / PluginConstants::UI_ASPECT_RATIO;
        float circleHeight = circleSize;

        float circleX = centerX - (circleWidth / 2.0f);
        float circleTopY = gearY + (dim.lineHeightNormal - circleHeight) / 2.0f;

        applyOffset(circleX, circleTopY);
        setQuadPositions(m_quads[1], circleX, circleTopY, circleWidth, circleHeight);
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

    float startX = WidgetPositions::WIDGET_STACK_X;
    float startY = WidgetPositions::SPEED_Y;

    // Calculate dimensions using base helper
    float backgroundWidth = calculateBackgroundWidth(WidgetDimensions::SPEED_WIDTH);
    float contentWidth = PluginUtils::calculateMonospaceTextWidth(WidgetDimensions::SPEED_WIDTH, dim.fontSize);  // Needed for centering

    // Height calculation is widget-specific due to lineHeightLarge value display
    float labelHeight = 0.0f;  // No title (widget design doesn't support titles)
    float contentHeight = labelHeight + dim.lineHeightLarge + dim.lineHeightNormal + dim.lineHeightNormal;  // Speed (2 lines) + Units (1 line) + Gear (1 line)
    float backgroundHeight = dim.paddingV + contentHeight + dim.paddingV;

    // Add background quad
    addBackgroundQuad(startX, startY, backgroundWidth, backgroundHeight);

    float contentStartX = startX + dim.paddingH;
    float contentStartY = startY + dim.paddingV;
    float currentY = contentStartY;

    // Use full opacity for text
    unsigned long textColor = TextColors::PRIMARY;

    // Build speed value string and gear string separately
    char speedValueBuffer[64];
    char gearValueBuffer[64];

    if (!bikeData.isValid) {
        // Show placeholder when telemetry data is not available
        snprintf(speedValueBuffer, sizeof(speedValueBuffer), "%s", Placeholders::GENERIC);
        snprintf(gearValueBuffer, sizeof(gearValueBuffer), "");
    } else {
        // Convert speedometer to mph (meters/sec to mph) and round to integer
        int speedMph = static_cast<int>(bikeData.speedometer * UnitConversion::MS_TO_MPH + 0.5f);
        snprintf(speedValueBuffer, sizeof(speedValueBuffer), "%d", speedMph);

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
    addString(speedValueBuffer, centerX, currentY, Justify::CENTER,
        Fonts::ENTER_SANSMAN, textColor, dim.fontSizeExtraLarge);

    // Add units label (normal font) - centered
    addString("mph", centerX, currentY + dim.lineHeightLarge, Justify::CENTER,
        Fonts::ENTER_SANSMAN, textColor, dim.fontSize);

    // Add gear circle indicator if limiter RPM is reached (behind gear text)
    // Only show when viewing player's bike (limiter RPM data only available for player's bike)
    // Skip if limiterRPM is 0 (some bikes don't report this value)
    bool isViewingPlayerBike = (pluginData.getDisplayRaceNum() == pluginData.getPlayerRaceNum());
    bool isLimiterHit = (bikeData.isValid && isViewingPlayerBike && sessionData.limiterRPM > 0 && bikeData.rpm >= sessionData.limiterRPM);

    if (isLimiterHit) {
        // Calculate gear circle quad position (centered behind gear text)
        float gearY = currentY + dim.lineHeightLarge + dim.lineHeightNormal;
        float circleSize = dim.fontSizeLarge * 1.5f;  // Circle slightly larger than gear text
        float circleWidth = circleSize / PluginConstants::UI_ASPECT_RATIO;
        float circleHeight = circleSize;

        // Center the circle at the gear text position
        // Text line center is at: gearY + (lineHeight / 2)
        // Circle top should be: lineCenter - (circleHeight / 2)
        float circleX = centerX - (circleWidth / 2.0f);
        float circleTopY = gearY + (dim.lineHeightNormal - circleHeight) / 2.0f;

        // Add gear circle quad (behind the text, so added first)
        SPluginQuad_t circleQuad;
        applyOffset(circleX, circleTopY);
        setQuadPositions(circleQuad, circleX, circleTopY, circleWidth, circleHeight);
        circleQuad.m_iSprite = PluginConstants::SpriteIndex::GEAR_CIRCLE;
        circleQuad.m_ulColor = 0xFFFFFFFF;  // White color, full opacity
        m_quads.push_back(circleQuad);
    }

    // Add gear value (large font but normal line height) - centered
    // Color: red if recommended shift point is reached, otherwise primary
    // Gear circle sprite appears at limiter (higher RPM threshold)
    // Skip color change if shiftRPM is 0 (some bikes don't report this value)
    unsigned long gearColor = (bikeData.isValid && isViewingPlayerBike && sessionData.shiftRPM > 0 && bikeData.rpm >= sessionData.shiftRPM)
        ? PluginConstants::Colors::RED
        : textColor;
    addString(gearValueBuffer, centerX, currentY + dim.lineHeightLarge + dim.lineHeightNormal, Justify::CENTER,
        Fonts::ENTER_SANSMAN, gearColor, dim.fontSizeLarge);

    // Set bounds for drag detection
    setBounds(startX, startY, startX + backgroundWidth, startY + backgroundHeight);
}

void SpeedWidget::resetToDefaults() {
    m_bVisible = true;
    m_bShowTitle = false;  // No title rendered (widget design doesn't support titles)
    m_bShowBackgroundTexture = false;  // No texture by default
    m_fBackgroundOpacity = 1.0f;  // Full opacity
    m_fScale = 1.0f;
    setPosition(0.4125f, 0.6882f);
    setDataDirty();
}
