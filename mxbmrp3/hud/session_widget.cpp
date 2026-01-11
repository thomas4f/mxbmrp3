// ============================================================================
// hud/session_widget.cpp
// Session widget - displays session and state (e.g., "RACE 2 / In Progress")
// ============================================================================
#include "session_widget.h"

#include <cstdio>
#include <cstring>

#include "../diagnostics/logger.h"
#include "../diagnostics/timer.h"
#include "../core/plugin_utils.h"
#include "../core/color_config.h"

using namespace PluginConstants;

SessionWidget::SessionWidget()
    : m_cachedEventType(-1)
    , m_cachedSession(-1)
    , m_cachedSessionState(-1)
    , m_cachedSessionLength(-1)
    , m_cachedSessionNumLaps(-1)
{
    // One-time setup
    DEBUG_INFO("SessionWidget created");
    setDraggable(true);
    m_strings.reserve(4);

    // Set texture base name for dynamic texture discovery
    setTextureBaseName("session_widget");

    // Set all configurable defaults
    resetToDefaults();

    rebuildRenderData();
}

bool SessionWidget::handlesDataType(DataChangeType dataType) const {
    return dataType == DataChangeType::SessionData;
}

int SessionWidget::getEnabledRowCount() const {
    int count = 0;
    if (m_enabledRows & ROW_TYPE) count++;
    if (m_enabledRows & ROW_TRACK) count++;
    if (m_enabledRows & ROW_FORMAT) count++;
    return count;
}

float SessionWidget::calculateContentHeight(const ScaledDimensions& dim) const {
    float labelHeight = m_bShowTitle ? dim.lineHeightNormal : 0.0f;
    float typeHeight = (m_enabledRows & ROW_TYPE) ? dim.lineHeightLarge : 0.0f;
    float trackHeight = (m_enabledRows & ROW_TRACK) ? dim.lineHeightNormal : 0.0f;
    float formatHeight = (m_enabledRows & ROW_FORMAT) ? dim.lineHeightNormal : 0.0f;
    return labelHeight + typeHeight + trackHeight + formatHeight;
}

void SessionWidget::update() {
    // OPTIMIZATION: Skip processing when not visible
    if (!isVisible()) {
        clearDataDirty();
        clearLayoutDirty();
        return;
    }

    // Get session data
    const PluginData& pluginData = PluginData::getInstance();
    const SessionData& sessionData = pluginData.getSessionData();

    int eventType = sessionData.eventType;
    int session = sessionData.session;
    int sessionState = sessionData.sessionState;
    int sessionLength = sessionData.sessionLength;
    int sessionNumLaps = sessionData.sessionNumLaps;

    // Check if any session data changed
    if (eventType != m_cachedEventType || session != m_cachedSession || sessionState != m_cachedSessionState ||
        sessionLength != m_cachedSessionLength || sessionNumLaps != m_cachedSessionNumLaps) {
        setDataDirty();
    }

    // Check data dirty first (takes precedence)
    if (isDataDirty()) {
        rebuildRenderData();
        m_cachedEventType = eventType;
        m_cachedSession = session;
        m_cachedSessionState = sessionState;
        m_cachedSessionLength = sessionLength;
        m_cachedSessionNumLaps = sessionNumLaps;
        clearDataDirty();
        clearLayoutDirty();
    }
    else if (isLayoutDirty()) {
        rebuildLayout();
        clearLayoutDirty();
    }
}

void SessionWidget::rebuildLayout() {
    // Fast path - only update positions (not colors/opacity)
    auto dim = getScaledDimensions();

    float startX = 0.0f;
    float startY = 0.0f;

    // Calculate dimensions
    float backgroundWidth = calculateBackgroundWidth(WidgetDimensions::SESSION_WIDTH);
    float backgroundHeight = dim.paddingV + calculateContentHeight(dim) + dim.paddingV;

    // Individual row heights for positioning
    float labelHeight = m_bShowTitle ? dim.lineHeightNormal : 0.0f;
    float typeHeight = (m_enabledRows & ROW_TYPE) ? dim.lineHeightLarge : 0.0f;
    float trackHeight = (m_enabledRows & ROW_TRACK) ? dim.lineHeightNormal : 0.0f;

    // Set bounds for drag detection
    setBounds(startX, startY, startX + backgroundWidth, startY + backgroundHeight);

    // Update background quad position
    updateBackgroundQuadPosition(startX, startY, backgroundWidth, backgroundHeight);

    float contentStartX = startX + dim.paddingH;
    float contentStartY = startY + dim.paddingV;
    float currentY = contentStartY;

    // Position strings if they exist
    int stringIndex = 0;

    // "Session" label (optional, controlled by title toggle)
    if (m_bShowTitle && positionString(stringIndex, contentStartX, currentY)) {
        stringIndex++;
        currentY += labelHeight;
    }

    // Session type (extra large font - spans 2 lines)
    if (m_enabledRows & ROW_TYPE) {
        if (positionString(stringIndex, contentStartX, currentY)) {
            stringIndex++;
        }
        currentY += typeHeight;
    }

    // Track name (normal font - 1 line)
    if (m_enabledRows & ROW_TRACK) {
        if (positionString(stringIndex, contentStartX, currentY)) {
            stringIndex++;
        }
        currentY += trackHeight;
    }

    // Format + Session state (normal font - 1 line, combined)
    if (m_enabledRows & ROW_FORMAT) {
        positionString(stringIndex, contentStartX, currentY);
    }
}

void SessionWidget::rebuildRenderData() {
    // Clear render data
    clearStrings();
    m_quads.clear();

    auto dim = getScaledDimensions();

    // Get session data
    const PluginData& pluginData = PluginData::getInstance();
    const SessionData& sessionData = pluginData.getSessionData();

    int eventType = sessionData.eventType;
    int session = sessionData.session;
    int sessionState = sessionData.sessionState;

    // Get session and state strings
    const char* sessionString = PluginUtils::getSessionString(eventType, session);
    const char* stateString = PluginUtils::getSessionStateString(sessionState);

    float startX = 0.0f;
    float startY = 0.0f;

    // Calculate dimensions
    float backgroundWidth = calculateBackgroundWidth(WidgetDimensions::SESSION_WIDTH);
    float backgroundHeight = dim.paddingV + calculateContentHeight(dim) + dim.paddingV;

    // Add background quad
    addBackgroundQuad(startX, startY, backgroundWidth, backgroundHeight);

    // Individual row heights for positioning
    float labelHeight = m_bShowTitle ? dim.lineHeightNormal : 0.0f;
    float typeHeight = (m_enabledRows & ROW_TYPE) ? dim.lineHeightLarge : 0.0f;
    float trackHeight = (m_enabledRows & ROW_TRACK) ? dim.lineHeightNormal : 0.0f;

    float contentStartX = startX + dim.paddingH;
    float contentStartY = startY + dim.paddingV;
    float currentY = contentStartY;

    // Use full opacity for text
    unsigned long textColor = ColorConfig::getInstance().getPrimary();

    // "Session" label (optional, controlled by title toggle)
    if (m_bShowTitle) {
        addString("Session", contentStartX, currentY, Justify::LEFT,
            Fonts::getTitle(), textColor, dim.fontSize);
        currentY += labelHeight;
    }

    // Session type (extra large font - e.g., "PRACTICE", "RACE 2")
    if (m_enabledRows & ROW_TYPE) {
        const char* sessionTypeString = sessionString ? sessionString : Placeholders::GENERIC;
        addString(sessionTypeString, contentStartX, currentY, Justify::LEFT,
            Fonts::getTitle(), textColor, dim.fontSizeExtraLarge);
        currentY += typeHeight;
    }

    // Track name (normal font)
    if (m_enabledRows & ROW_TRACK) {
        const char* trackName = sessionData.trackName[0] != '\0' ? sessionData.trackName : Placeholders::GENERIC;
        addString(trackName, contentStartX, currentY, Justify::LEFT,
            Fonts::getTitle(), textColor, dim.fontSize);
        currentY += trackHeight;
    }

    // Format + Session state (combined on one line, e.g., "10:00 + 2 Laps, In Progress")
    if (m_enabledRows & ROW_FORMAT) {
        bool hasTime = (sessionData.sessionLength > 0);
        bool hasLaps = (sessionData.sessionNumLaps > 0);
        const char* sessionStateString = stateString ? stateString : Placeholders::GENERIC;

        char combinedBuffer[128];

        if (hasTime || hasLaps) {
            char formatBuffer[64];
            char timeBuffer[16];

            if (hasTime && hasLaps) {
                // Time + Laps format (e.g., "10:00 + 2 Laps")
                PluginUtils::formatTimeMinutesSeconds(sessionData.sessionLength, timeBuffer, sizeof(timeBuffer));
                snprintf(formatBuffer, sizeof(formatBuffer), "%s + %d Laps", timeBuffer, sessionData.sessionNumLaps);
            }
            else if (hasTime) {
                // Time only (e.g., "10:00")
                PluginUtils::formatTimeMinutesSeconds(sessionData.sessionLength, formatBuffer, sizeof(formatBuffer));
            }
            else {
                // Laps only (e.g., "2 Laps")
                snprintf(formatBuffer, sizeof(formatBuffer), "%d Laps", sessionData.sessionNumLaps);
            }

            // Combine format and state with comma separator
            snprintf(combinedBuffer, sizeof(combinedBuffer), "%s, %s", formatBuffer, sessionStateString);
        }
        else {
            // No format data, just show state
            snprintf(combinedBuffer, sizeof(combinedBuffer), "%s", sessionStateString);
        }

        addString(combinedBuffer, contentStartX, currentY, Justify::LEFT,
            Fonts::getTitle(), textColor, dim.fontSize);
    }

    // Set bounds for drag detection
    setBounds(startX, startY, startX + backgroundWidth, startY + backgroundHeight);
}

void SessionWidget::resetToDefaults() {
    m_bVisible = false;  // Disabled by default
    m_bShowTitle = false;  // No title by default
    setTextureVariant(0);  // No texture by default
    m_fBackgroundOpacity = 0.1f;
    m_fScale = 1.0f;
    m_enabledRows = ROW_DEFAULT;  // Reset row visibility
    setPosition(0.0055f, 0.1332f);
    setDataDirty();
}
