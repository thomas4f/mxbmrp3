// ============================================================================
// hud/time_widget.cpp
// Time widget - displays label + time in two rows (countdown or countup)
// ============================================================================
#include "time_widget.h"
#include "../diagnostics/logger.h"
#include "../diagnostics/timer.h"
#include "../core/plugin_utils.h"
#include "../core/color_config.h"
#include <cstdio>

using namespace PluginConstants;

TimeWidget::TimeWidget()
    : m_cachedRenderedTime(-1)
    , m_cachedEventType(-1)
    , m_cachedSession(-1)
    , m_bShowSessionType(false)
{
    // One-time setup
    DEBUG_INFO("TimeWidget created");
    setDraggable(true);
    m_strings.reserve(3);  // label (optional), time, session type (optional)

    // Set texture base name for dynamic texture discovery
    setTextureBaseName("time_widget");

    // Set all configurable defaults
    resetToDefaults();

    rebuildRenderData();
}

bool TimeWidget::handlesDataType(DataChangeType dataType) const {
    return dataType == DataChangeType::SessionData ||
           dataType == DataChangeType::Standings;
}

void TimeWidget::update() {
    // OPTIMIZATION: Skip processing when not visible
    if (!isVisible()) {
        clearDataDirty();
        clearLayoutDirty();
        return;
    }

    // Check if time changed enough to update display
    // Only rebuild when seconds change, not every millisecond
    const PluginData& pluginData = PluginData::getInstance();
    const SessionData& sessionData = pluginData.getSessionData();
    int currentTime = pluginData.getSessionTime();
    int currentSeconds = currentTime / TimeConversion::MS_PER_SECOND;
    int lastSeconds = m_cachedRenderedTime / TimeConversion::MS_PER_SECOND;

    if (currentSeconds != lastSeconds) {
        setDataDirty();
    }

    // Check if session type changed (for session type display)
    if (m_bShowSessionType) {
        if (sessionData.eventType != m_cachedEventType || sessionData.session != m_cachedSession) {
            setDataDirty();
        }
    }

    // Check data dirty first (takes precedence)
    if (isDataDirty()) {
        rebuildRenderData();
        m_cachedRenderedTime = currentTime;
        m_cachedEventType = sessionData.eventType;
        m_cachedSession = sessionData.session;
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

    float startX = 0.0f;
    float startY = 0.0f;

    // Calculate dimensions using base helper
    float backgroundWidth = calculateBackgroundWidth(WidgetDimensions::STANDARD_WIDTH);

    // Height calculation is widget-specific due to lineHeightLarge value display
    float labelHeight = m_bShowTitle ? dim.lineHeightNormal : 0.0f;
    float sessionTypeHeight = m_bShowSessionType ? dim.lineHeightNormal : 0.0f;
    float contentHeight = labelHeight + dim.lineHeightLarge + sessionTypeHeight;  // Label (optional) + Time (2 lines) + Session type (optional)
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
    if (positionString(stringIndex, contentStartX, currentY)) {
        stringIndex++;
        currentY += dim.lineHeightLarge;
    }

    // Session type (optional, same size as label)
    if (m_bShowSessionType) {
        positionString(stringIndex, contentStartX, currentY);
    }
}

void TimeWidget::rebuildRenderData() {
    // Clear render data
    clearStrings();
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
    unsigned long textColor = this->getColor(ColorSlot::PRIMARY);

    float startX = 0.0f;
    float startY = 0.0f;

    // Calculate dimensions using base helper
    float backgroundWidth = calculateBackgroundWidth(WidgetDimensions::STANDARD_WIDTH);

    // Height calculation is widget-specific due to lineHeightLarge value display
    float labelHeight = m_bShowTitle ? dim.lineHeightNormal : 0.0f;
    float sessionTypeHeight = m_bShowSessionType ? dim.lineHeightNormal : 0.0f;
    float contentHeight = labelHeight + dim.lineHeightLarge + sessionTypeHeight;  // Label (optional) + Time (2 lines) + Session type (optional)
    float backgroundHeight = dim.paddingV + contentHeight + dim.paddingV;

    // Add background quad
    addBackgroundQuad(startX, startY, backgroundWidth, backgroundHeight);

    float contentStartX = startX + dim.paddingH;
    float contentStartY = startY + dim.paddingV;
    float currentY = contentStartY;

    // Label (optional, controlled by title toggle)
    if (m_bShowTitle) {
        addString("Time", contentStartX, currentY, Justify::LEFT, this->getFont(FontCategory::TITLE), textColor, dim.fontSize);
        currentY += labelHeight;
    }

    // Time value (extra large font - spans 2 lines)
    addString(timeBuffer, contentStartX, currentY, Justify::LEFT,
        this->getFont(FontCategory::TITLE), textColor, dim.fontSizeExtraLarge);
    currentY += dim.lineHeightLarge;

    // Session type (optional, same size as label)
    if (m_bShowSessionType) {
        const char* sessionString = PluginUtils::getSessionString(sessionData.eventType, sessionData.session);
        const char* displayString = sessionString ? sessionString : Placeholders::GENERIC;
        addString(displayString, contentStartX, currentY, Justify::LEFT,
            this->getFont(FontCategory::TITLE), textColor, dim.fontSize);
    }

    // Set bounds for drag detection
    setBounds(startX, startY, startX + backgroundWidth, startY + backgroundHeight);
}

void TimeWidget::resetToDefaults() {
    m_bVisible = true;
    m_bShowTitle = true;
    m_bShowSessionType = false;  // Hide session type by default
    setTextureVariant(0);  // No texture by default
    m_fBackgroundOpacity = 0.1f;
    m_fScale = 1.0f;
    setPosition(0.1925f, 0.0111f);
    setDataDirty();
}
