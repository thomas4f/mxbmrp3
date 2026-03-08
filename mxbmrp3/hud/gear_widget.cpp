// ============================================================================
// hud/gear_widget.cpp
// Gear widget - displays current gear with shift/limiter indicators
// ============================================================================
#include "gear_widget.h"

#include <cstdio>

#include "../diagnostics/logger.h"
#include "../diagnostics/timer.h"
#include "../core/plugin_utils.h"
#include "../core/color_config.h"
#include "../core/asset_manager.h"

using namespace PluginConstants;

namespace {
    constexpr float GEAR_FONT_SIZE_EXTRA = 0.01944f;  // Extra size added to gear font beyond row height
    constexpr float GEAR_TEXT_OFFSET_Y   = -0.00741f;  // Vertical nudge for gear text alignment
}

GearWidget::GearWidget()
{
    DEBUG_INFO("GearWidget created");
    setDraggable(true);
    m_quads.reserve(2);    // Background + gear circle
    m_strings.reserve(2);  // Title (optional) + gear value

    setTextureBaseName("gear_widget");

    resetToDefaults();
    rebuildRenderData();
}

bool GearWidget::handlesDataType(DataChangeType dataType) const {
    return dataType == DataChangeType::InputTelemetry ||
           dataType == DataChangeType::SpectateTarget;
}

void GearWidget::update() {
    if (!isVisible()) {
        clearDataDirty();
        clearLayoutDirty();
        return;
    }

    // Always rebuild - gear updates at telemetry rate, rebuild is cheap
    rebuildRenderData();
    clearDataDirty();
    clearLayoutDirty();
}

void GearWidget::rebuildLayout() {
    auto dim = getScaledDimensions();
    float gearRowHeight = dim.lineHeightLarge + dim.lineHeightNormal;  // Match SpeedWidget content height (value + units)
    float gearFontSize = gearRowHeight + GEAR_FONT_SIZE_EXTRA;
    float gearTextOffsetY = GEAR_TEXT_OFFSET_Y;

    float startX = 0.0f;
    float startY = 0.0f;

    float backgroundWidth = calculateBackgroundWidth(WidgetDimensions::GEAR_WIDTH);
    float contentWidth = PluginUtils::calculateMonospaceTextWidth(WidgetDimensions::GEAR_WIDTH, dim.fontSize);

    float labelHeight = m_bShowTitle ? dim.lineHeightNormal : 0.0f;
    float contentHeight = labelHeight + gearRowHeight;
    float backgroundHeight = dim.paddingV + contentHeight + dim.paddingV;

    setBounds(startX, startY, startX + backgroundWidth, startY + backgroundHeight);
    updateBackgroundQuadPosition(startX, startY, backgroundWidth, backgroundHeight);

    float contentStartX = startX + dim.paddingH;
    float contentStartY = startY + dim.paddingV;
    float currentY = contentStartY;
    float centerX = contentStartX + (contentWidth / 2.0f);

    int stringIndex = 0;

    // Title label (optional)
    if (m_bShowTitle && positionString(stringIndex, centerX, currentY)) {
        stringIndex++;
        currentY += labelHeight;
    }

    // Gear text (nudged up within the row)
    positionString(stringIndex, centerX, currentY + gearTextOffsetY);

    // Update gear circle quad position if it exists (quads[0] = background, quads[1] = gear circle)
    if (m_quads.size() > 1) {
        float circleSize = gearFontSize * 1.5f;
        float circleWidth = circleSize / PluginConstants::UI_ASPECT_RATIO;
        float circleHeight = circleSize;

        float circleX = centerX - (circleWidth / 2.0f);
        float circleTopY = currentY + gearTextOffsetY + (gearRowHeight - circleHeight) / 2.0f;

        applyOffset(circleX, circleTopY);
        setQuadPositions(m_quads[1], circleX, circleTopY, circleWidth, circleHeight);
    }
}

void GearWidget::rebuildRenderData() {
    clearStrings();
    m_quads.clear();

    auto dim = getScaledDimensions();
    float gearRowHeight = dim.lineHeightLarge + dim.lineHeightNormal;  // Match SpeedWidget content height (value + units)
    float gearFontSize = gearRowHeight + GEAR_FONT_SIZE_EXTRA;
    float gearTextOffsetY = GEAR_TEXT_OFFSET_Y;

    const PluginData& pluginData = PluginData::getInstance();
    const BikeTelemetryData& bikeData = pluginData.getBikeTelemetry();
    const SessionData& sessionData = pluginData.getSessionData();

    float startX = 0.0f;
    float startY = 0.0f;

    float backgroundWidth = calculateBackgroundWidth(WidgetDimensions::GEAR_WIDTH);
    float contentWidth = PluginUtils::calculateMonospaceTextWidth(WidgetDimensions::GEAR_WIDTH, dim.fontSize);

    float labelHeight = m_bShowTitle ? dim.lineHeightNormal : 0.0f;
    float contentHeight = labelHeight + gearRowHeight;
    float backgroundHeight = dim.paddingV + contentHeight + dim.paddingV;

    addBackgroundQuad(startX, startY, backgroundWidth, backgroundHeight);

    float contentStartX = startX + dim.paddingH;
    float contentStartY = startY + dim.paddingV;
    float currentY = contentStartY;
    float centerX = contentStartX + (contentWidth / 2.0f);

    unsigned long textColor = this->getColor(ColorSlot::PRIMARY);

    // Title label (optional)
    if (m_bShowTitle) {
        addString("Gear", centerX, currentY, Justify::CENTER,
            this->getFont(FontCategory::TITLE), textColor, dim.fontSize);
        currentY += labelHeight;
    }

    // Format gear string
    char gearValueBuffer[8];
    if (!bikeData.isValid) {
        snprintf(gearValueBuffer, sizeof(gearValueBuffer), "%s", Placeholders::GENERIC);
    } else {
        if (bikeData.gear == GearValue::NEUTRAL) {
            snprintf(gearValueBuffer, sizeof(gearValueBuffer), "N");
        } else {
            snprintf(gearValueBuffer, sizeof(gearValueBuffer), "%d", bikeData.gear);
        }
    }

    // RPM-based indicators only apply to the player's own bike
    bool isViewingPlayer = (pluginData.getDisplayRaceNum() == pluginData.getPlayerRaceNum());

    // Add gear circle indicator if limiter RPM is reached (behind gear text)
    bool isLimiterHit = (m_bShowLimiterCircle && bikeData.isValid && isViewingPlayer && sessionData.limiterRPM > 0 && bikeData.rpm >= sessionData.limiterRPM);

    if (isLimiterHit) {
        float circleSize = gearFontSize * 1.5f;
        float circleWidth = circleSize / PluginConstants::UI_ASPECT_RATIO;
        float circleHeight = circleSize;

        float circleX = centerX - (circleWidth / 2.0f);
        float circleTopY = currentY + gearTextOffsetY + (gearRowHeight - circleHeight) / 2.0f;

        SPluginQuad_t circleQuad{};
        applyOffset(circleX, circleTopY);
        setQuadPositions(circleQuad, circleX, circleTopY, circleWidth, circleHeight);
        circleQuad.m_iSprite = AssetManager::getInstance().getSpriteIndex("gear_circle", 1);
        circleQuad.m_ulColor = ColorPalette::WHITE;
        m_quads.push_back(circleQuad);
    }

    // Gear color: red if recommended shift point reached, otherwise primary
    unsigned long gearColor = (m_bShowShiftColor && bikeData.isValid && isViewingPlayer && sessionData.shiftRPM > 0 && bikeData.rpm >= sessionData.shiftRPM)
        ? this->getColor(ColorSlot::NEGATIVE)
        : textColor;
    addString(gearValueBuffer, centerX, currentY + gearTextOffsetY, Justify::CENTER,
        this->getFont(FontCategory::TITLE), gearColor, gearFontSize);

    setBounds(startX, startY, startX + backgroundWidth, startY + backgroundHeight);
}

void GearWidget::resetToDefaults() {
    m_bVisible = true;
    m_bShowTitle = false;
    setTextureVariant(0);
    m_fBackgroundOpacity = 0.0f;  // Transparent by default
    m_fScale = 1.0f;
    m_bShowShiftColor = true;
    m_bShowLimiterCircle = true;
    setPosition(0.9020f, 0.8769f);
    setDataDirty();
}
