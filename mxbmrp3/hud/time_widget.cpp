// ============================================================================
// hud/time_widget.cpp
// Time widget - displays label + time in two rows (countdown or countup)
// ============================================================================
#include "time_widget.h"
#include "../diagnostics/logger.h"
#include "../diagnostics/timer.h"
#include "../core/plugin_utils.h"
#include <cstdio>

using namespace PluginConstants;

TimeWidget::TimeWidget()
    : m_cachedRenderedTime(-1)
{
    initializeWidget("TimeWidget", 2);  // Two strings: label (optional), time
    setPosition(-0.275f, -0.0999f);
}

bool TimeWidget::handlesDataType(DataChangeType dataType) const {
    return dataType == DataChangeType::SessionData ||
           dataType == DataChangeType::Standings;
}

void TimeWidget::update() {
    // Check if time changed enough to update display
    // Only rebuild when seconds change, not every millisecond
    const PluginData& pluginData = PluginData::getInstance();
    int currentTime = pluginData.getSessionTime();
    int currentSeconds = currentTime / TimeConversion::MS_PER_SECOND;
    int lastSeconds = m_cachedRenderedTime / TimeConversion::MS_PER_SECOND;

    if (currentSeconds != lastSeconds) {
        setDataDirty();
    }

    // Check data dirty first (takes precedence)
    if (isDataDirty()) {
        rebuildRenderData();
        m_cachedRenderedTime = currentTime;
        clearDataDirty();
        clearLayoutDirty();
    }
    else if (isLayoutDirty()) {
        rebuildLayout();
        clearLayoutDirty();
    }
}


void TimeWidget::rebuildLayout() {
    // Fast path - only update positions (not colors/opacity)
    auto dim = getScaledDimensions();

    float startX = WidgetPositions::WIDGET_STACK_X;
    float startY = WidgetPositions::TIME_Y;

    // Calculate dimensions using base helper
    float backgroundWidth = calculateBackgroundWidth(WidgetDimensions::STANDARD_WIDTH);

    // Height calculation is widget-specific due to lineHeightLarge value display
    float labelHeight = m_bShowTitle ? dim.lineHeightNormal : 0.0f;
    float contentHeight = labelHeight + dim.lineHeightLarge;  // Label (optional, 1 line) + Time (2 lines)
    float backgroundHeight = dim.paddingV + contentHeight + dim.paddingV;

    setBounds(startX, startY, startX + backgroundWidth, startY + backgroundHeight);

    // Update background quad position (applies offset internally)
    updateBackgroundQuadPosition(startX, startY, backgroundWidth, backgroundHeight);

    float contentStartX = startX + dim.paddingH;
    float contentStartY = startY + dim.paddingV;
    float currentY = contentStartY;

    // Position strings if they exist
    int stringIndex = 0;

    // Label (optional, controlled by title toggle)
    if (m_bShowTitle && positionString(stringIndex, contentStartX, currentY)) {
        stringIndex++;
        currentY += labelHeight;
    }

    // Time value (extra large font - spans 2 lines)
    positionString(stringIndex, contentStartX, currentY);
}

void TimeWidget::rebuildRenderData() {
    // Clear render data
    m_strings.clear();
    m_quads.clear();

    auto dim = getScaledDimensions();

    // Get session data
    const PluginData& pluginData = PluginData::getInstance();
    const SessionData& sessionData = pluginData.getSessionData();
    int sessionTime = pluginData.getSessionTime();

    // Format the time
    char timeBuffer[16];
    PluginUtils::formatTimeMinutesSeconds(sessionTime, timeBuffer, sizeof(timeBuffer));

    // Use full opacity for text
    unsigned long textColor = TextColors::PRIMARY;

    float startX = WidgetPositions::WIDGET_STACK_X;
    float startY = WidgetPositions::TIME_Y;

    // Calculate dimensions using base helper
    float backgroundWidth = calculateBackgroundWidth(WidgetDimensions::STANDARD_WIDTH);

    // Height calculation is widget-specific due to lineHeightLarge value display
    float labelHeight = m_bShowTitle ? dim.lineHeightNormal : 0.0f;
    float contentHeight = labelHeight + dim.lineHeightLarge;  // Label (optional, 1 line) + Time (2 lines)
    float backgroundHeight = dim.paddingV + contentHeight + dim.paddingV;

    // Add background quad
    addBackgroundQuad(startX, startY, backgroundWidth, backgroundHeight);

    float contentStartX = startX + dim.paddingH;
    float contentStartY = startY + dim.paddingV;
    float currentY = contentStartY;

    // Label (optional, controlled by title toggle)
    if (m_bShowTitle) {
        addString("Time", contentStartX, currentY, Justify::LEFT, Fonts::ENTER_SANSMAN, textColor, dim.fontSize);
        currentY += labelHeight;
    }

    // Time value (ENTER_SANSMAN, extra large font - spans 2 lines)
    addString(timeBuffer, contentStartX, currentY, Justify::LEFT,
        Fonts::ENTER_SANSMAN, textColor, dim.fontSizeExtraLarge);

    // Set bounds for drag detection
    setBounds(startX, startY, startX + backgroundWidth, startY + backgroundHeight);
}

void TimeWidget::resetToDefaults() {
    m_bVisible = true;
    m_bShowTitle = true;
    m_bShowBackgroundTexture = false;  // No texture by default
    m_fBackgroundOpacity = 0.1f;
    m_fScale = 1.0f;
    setPosition(-0.275f, -0.0999f);
    setDataDirty();
}
