// ============================================================================
// hud/session_hud.cpp
// Session HUD - displays session info (type, track, format, server, weather)
// ============================================================================
#include "session_hud.h"

#include <cstdio>
#include <cstring>

#include "../diagnostics/logger.h"
#include "../diagnostics/timer.h"
#include "../core/plugin_utils.h"
#include "../core/color_config.h"
#include "../core/asset_manager.h"
#include "../core/ui_config.h"

using namespace PluginConstants;

namespace {
    // Icon size as a fraction of font size (slightly smaller than text for visual balance)
    constexpr float ICON_SIZE_FACTOR = 0.8f;
    // Max visible characters before truncating with "..."
    constexpr int MAX_DISPLAY_CHARS = 25;
}

SessionHud::SessionHud()
    : m_cachedEventType(-1)
    , m_cachedSession(-1)
    , m_cachedSessionState(-1)
    , m_cachedSessionLength(-1)
    , m_cachedSessionNumLaps(-1)
    , m_cachedServerType(CACHE_UNINITIALIZED)
    , m_cachedConditions(-1)
    , m_cachedAirTemperature(-1.0f)
    , m_cachedTrackTemperature(-1.0f)
{
    m_cachedServerName[0] = '\0';
    // One-time setup
    DEBUG_INFO("SessionHud created");
    setDraggable(true);
    m_strings.reserve(6);

    // Set texture base name for dynamic texture discovery
    setTextureBaseName("session_widget");  // Keep same texture for backwards compatibility

    // Set all configurable defaults
    resetToDefaults();

    rebuildRenderData();
}

bool SessionHud::handlesDataType(DataChangeType dataType) const {
    return dataType == DataChangeType::SessionData;
}

int SessionHud::getEnabledRowCount() const {
    int count = 0;
    if (m_enabledRows & ROW_TYPE) count++;
    if (m_enabledRows & ROW_TRACK) count++;
    if (m_enabledRows & ROW_FORMAT) count++;
    if (m_enabledRows & ROW_SERVER) count++;
    if (m_enabledRows & ROW_WEATHER) count++;
    return count;
}

void SessionHud::calculateIconQuadCorners(float x, float y, float fontSize, float corners[4][2]) const {
    float iconSize = fontSize * ICON_SIZE_FACTOR;
    float halfSize = iconSize / 2.0f;
    float halfWidth = halfSize / UI_ASPECT_RATIO;
    float iconCenterX = x + halfWidth;  // Left edge at x
    float iconCenterY = y + fontSize * 0.5f;  // Center vertically with text

    corners[0][0] = iconCenterX - halfWidth;  corners[0][1] = iconCenterY - halfSize;
    corners[1][0] = iconCenterX - halfWidth;  corners[1][1] = iconCenterY + halfSize;
    corners[2][0] = iconCenterX + halfWidth;  corners[2][1] = iconCenterY + halfSize;
    corners[3][0] = iconCenterX + halfWidth;  corners[3][1] = iconCenterY - halfSize;
}

float SessionHud::calculateContentHeight(const ScaledDimensions& dim) const {
    const PluginData& pluginData = PluginData::getInstance();
    const SessionData& sessionData = pluginData.getSessionData();

    float labelHeight = m_bShowTitle ? dim.lineHeightNormal : 0.0f;
    float typeHeight = (m_enabledRows & ROW_TYPE) ? dim.lineHeightLarge : 0.0f;
    float formatHeight = (m_enabledRows & ROW_FORMAT) ? dim.lineHeightNormal : 0.0f;
    float trackHeight = (m_enabledRows & ROW_TRACK) ? dim.lineHeightNormal : 0.0f;
    float weatherHeight = ((m_enabledRows & ROW_WEATHER) && sessionData.conditions >= 0) ? dim.lineHeightNormal : 0.0f;

    // Server row always reserves space when enabled — text varies (server name / Testing / Unknown).
    float serverHeight = (m_enabledRows & ROW_SERVER) ? dim.lineHeightNormal : 0.0f;
    return labelHeight + typeHeight + formatHeight + trackHeight + weatherHeight + serverHeight;
}

void SessionHud::update() {
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
    int serverType = sessionData.serverType;

    // Check if any session data changed
    if (eventType != m_cachedEventType || session != m_cachedSession || sessionState != m_cachedSessionState ||
        sessionLength != m_cachedSessionLength || sessionNumLaps != m_cachedSessionNumLaps ||
        serverType != m_cachedServerType ||
        strcmp(sessionData.serverName, m_cachedServerName) != 0 ||
        sessionData.conditions != m_cachedConditions || sessionData.airTemperature != m_cachedAirTemperature ||
        sessionData.trackTemperature != m_cachedTrackTemperature) {
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
        m_cachedServerType = serverType;
        strncpy_s(m_cachedServerName, sessionData.serverName, sizeof(m_cachedServerName) - 1);
        m_cachedConditions = sessionData.conditions;
        m_cachedAirTemperature = sessionData.airTemperature;
        m_cachedTrackTemperature = sessionData.trackTemperature;
        clearDataDirty();
        clearLayoutDirty();
    }
    else if (isLayoutDirty()) {
        rebuildLayout();
        clearLayoutDirty();
    }
}

void SessionHud::rebuildLayout() {
    // Fast path - only update positions (not colors/opacity)
    auto dim = getScaledDimensions();
    const PluginData& pluginData = PluginData::getInstance();
    const SessionData& sessionData = pluginData.getSessionData();

    float startX = 0.0f;
    float startY = 0.0f;

    // Calculate dimensions
    float backgroundWidth = calculateBackgroundWidth(WidgetDimensions::SESSION_WIDTH);
    float backgroundHeight = dim.paddingV + calculateContentHeight(dim) + dim.paddingV;

    // Individual row heights for positioning
    float labelHeight = m_bShowTitle ? dim.lineHeightNormal : 0.0f;
    float typeHeight = (m_enabledRows & ROW_TYPE) ? dim.lineHeightLarge : 0.0f;
    float formatHeight = (m_enabledRows & ROW_FORMAT) ? dim.lineHeightNormal : 0.0f;
    float trackHeight = (m_enabledRows & ROW_TRACK) ? dim.lineHeightNormal : 0.0f;
    float serverHeight = (m_enabledRows & ROW_SERVER) ? dim.lineHeightNormal : 0.0f;

    // Set bounds for drag detection
    setBounds(startX, startY, startX + backgroundWidth, startY + backgroundHeight);

    // Update background quad position
    updateBackgroundQuadPosition(startX, startY, backgroundWidth, backgroundHeight);

    float contentStartX = startX + dim.paddingH;
    float contentStartY = startY + dim.paddingV;
    float currentY = contentStartY;

    // Icon setup (must match rebuildRenderData)
    float iconSize = dim.fontSize * ICON_SIZE_FACTOR;
    float iconWidth = iconSize / UI_ASPECT_RATIO;  // Actual width after aspect ratio correction
    float iconTextGap = dim.paddingH * 0.3f;  // Small gap between icon and text
    float textOffset = m_bShowIcons ? (iconWidth + iconTextGap) : 0.0f;

    // Helper lambda to reposition an icon quad (indices 1+ are icons, 0 is background)
    auto repositionIconQuad = [&](size_t quadIndex, float x, float y) {
        if (!m_bShowIcons) return;  // Skip if icons disabled
        if (quadIndex >= m_quads.size()) return;

        float corners[4][2];
        calculateIconQuadCorners(x, y, dim.fontSize, corners);

        // Apply offset to each corner and set quad positions
        SPluginQuad_t& quad = m_quads[quadIndex];
        for (int i = 0; i < 4; i++) {
            float cornerX = corners[i][0];
            float cornerY = corners[i][1];
            applyOffset(cornerX, cornerY);
            quad.m_aafPos[i][0] = cornerX;
            quad.m_aafPos[i][1] = cornerY;
        }
    };

    // Position strings and icon quads
    int stringIndex = 0;
    size_t iconQuadIndex = 1;  // Icon quads start at index 1 (background is index 0)

    // "Session" label (optional, controlled by title toggle) - no icon
    if (m_bShowTitle && positionString(stringIndex, contentStartX, currentY)) {
        stringIndex++;
        currentY += labelHeight;
    }

    // Session type (extra large font) - no icon
    if (m_enabledRows & ROW_TYPE) {
        if (positionString(stringIndex, contentStartX, currentY)) {
            stringIndex++;
        }
        currentY += typeHeight;
    }

    // Format + Session state (normal font with icon)
    if (m_enabledRows & ROW_FORMAT) {
        repositionIconQuad(iconQuadIndex++, contentStartX, currentY);
        if (positionString(stringIndex, contentStartX + textOffset, currentY)) {
            stringIndex++;
        }
        currentY += formatHeight;
    }

    // Track name (normal font with icon)
    if (m_enabledRows & ROW_TRACK) {
        repositionIconQuad(iconQuadIndex++, contentStartX, currentY);
        if (positionString(stringIndex, contentStartX + textOffset, currentY)) {
            stringIndex++;
        }
        currentY += trackHeight;
    }

    // Weather row (conditions + temperature, with icon) - after track
    if ((m_enabledRows & ROW_WEATHER) && sessionData.conditions >= 0) {
        repositionIconQuad(iconQuadIndex++, contentStartX, currentY);
        if (positionString(stringIndex, contentStartX + textOffset, currentY)) {
            stringIndex++;
        }
        currentY += dim.lineHeightNormal;
    }

    // Server row (with icon) — text varies by state
    if (m_enabledRows & ROW_SERVER) {
        repositionIconQuad(iconQuadIndex++, contentStartX, currentY);
        if (positionString(stringIndex, contentStartX + textOffset, currentY)) {
            stringIndex++;
        }
        currentY += serverHeight;
    }
}

void SessionHud::rebuildRenderData() {
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
    bool isOnline = sessionData.isOnline();
    bool isOffline = sessionData.isOffline();
    bool hasServerName = sessionData.serverName[0] != '\0';

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
    float formatHeight = (m_enabledRows & ROW_FORMAT) ? dim.lineHeightNormal : 0.0f;
    float trackHeight = (m_enabledRows & ROW_TRACK) ? dim.lineHeightNormal : 0.0f;
    float serverHeight = (m_enabledRows & ROW_SERVER) ? dim.lineHeightNormal : 0.0f;

    float contentStartX = startX + dim.paddingH;
    float contentStartY = startY + dim.paddingV;
    float currentY = contentStartY;

    // Icon setup
    float iconSize = dim.fontSize * ICON_SIZE_FACTOR;
    float iconWidth = iconSize / UI_ASPECT_RATIO;  // Actual width after aspect ratio correction
    float iconTextGap = dim.paddingH * 0.3f;  // Small gap between icon and text
    float textOffset = m_bShowIcons ? (iconWidth + iconTextGap) : 0.0f;
    AssetManager& assetMgr = AssetManager::getInstance();

    // Get specific icons for each row type (only if icons enabled)
    int iconFormat = m_bShowIcons ? assetMgr.getIconSpriteIndex("clock") : 0;      // Time/format
    int iconTrack = m_bShowIcons ? assetMgr.getIconSpriteIndex("location-dot") : 0;    // Track location
    int iconServer = m_bShowIcons ? assetMgr.getIconSpriteIndex("server") : 0;         // Server
    int iconWeather = m_bShowIcons ? assetMgr.getIconSpriteIndex("temperature-low") : 0;  // Weather/temperature

    // Use full opacity for text and icons
    unsigned long textColor = this->getColor(ColorSlot::PRIMARY);
    unsigned long iconColor = this->getColor(ColorSlot::PRIMARY);  // White icons to match text

    // Helper lambda to add an icon quad with specific sprite
    auto addIconQuad = [&](float x, float y, int spriteIndex) {
        if (!m_bShowIcons) return;  // Skip if icons disabled
        if (spriteIndex <= 0) return;

        float corners[4][2];
        calculateIconQuadCorners(x, y, dim.fontSize, corners);

        SPluginQuad_t quad;
        for (int i = 0; i < 4; i++) {
            quad.m_aafPos[i][0] = corners[i][0];
            quad.m_aafPos[i][1] = corners[i][1];
            applyOffset(quad.m_aafPos[i][0], quad.m_aafPos[i][1]);
        }
        quad.m_iSprite = spriteIndex;
        quad.m_ulColor = iconColor;
        m_quads.push_back(quad);
    };

    // "Session" label (optional, controlled by title toggle) - no icon
    if (m_bShowTitle) {
        addString("Session", contentStartX, currentY, Justify::LEFT,
            this->getFont(FontCategory::TITLE), textColor, dim.fontSize);
        currentY += labelHeight;
    }

    // Session type (extra large font - e.g., "PRACTICE", "RACE 2") - no icon
    if (m_enabledRows & ROW_TYPE) {
        const char* sessionTypeString = sessionString ? sessionString : Placeholders::GENERIC;
        addString(sessionTypeString, contentStartX, currentY, Justify::LEFT,
            this->getFont(FontCategory::TITLE), textColor, dim.fontSizeExtraLarge);
        currentY += typeHeight;
    }

    // Format + Session state (combined on one line, with icon)
    if (m_enabledRows & ROW_FORMAT) {
        addIconQuad(contentStartX, currentY, iconFormat);
        bool hasTime = (sessionData.sessionLength > 0);
        bool hasLaps = (sessionData.sessionNumLaps > 0);
        const char* sessionStateString = stateString ? stateString : Placeholders::GENERIC;

        char combinedBuffer[128];

        if (hasTime || hasLaps) {
            char formatBuffer[64];
            char timeBuffer[16];

            if (hasTime && hasLaps) {
                // Time + Laps format (e.g., "10:00 + 2L")
                PluginUtils::formatTimeMinutesSeconds(sessionData.sessionLength, timeBuffer, sizeof(timeBuffer));
                snprintf(formatBuffer, sizeof(formatBuffer), "%s + %dL", timeBuffer, sessionData.sessionNumLaps);
            }
            else if (hasTime) {
                // Time only (e.g., "10:00")
                PluginUtils::formatTimeMinutesSeconds(sessionData.sessionLength, formatBuffer, sizeof(formatBuffer));
            }
            else {
                // Laps only (e.g., "2L")
                snprintf(formatBuffer, sizeof(formatBuffer), "%dL", sessionData.sessionNumLaps);
            }

            // Combine format and state with comma separator
            snprintf(combinedBuffer, sizeof(combinedBuffer), "%s, %s", formatBuffer, sessionStateString);
        }
        else {
            // No format data, just show state
            snprintf(combinedBuffer, sizeof(combinedBuffer), "%s", sessionStateString);
        }

        addString(combinedBuffer, contentStartX + textOffset, currentY, Justify::LEFT,
            this->getFont(FontCategory::TITLE), textColor, dim.fontSize);
        currentY += formatHeight;
    }

    // Track name (normal font with icon)
    if (m_enabledRows & ROW_TRACK) {
        addIconQuad(contentStartX, currentY, iconTrack);
        const char* trackName = sessionData.trackName[0] != '\0' ? sessionData.trackName : Placeholders::GENERIC;
        char trackBuf[32];
        if (strlen(trackName) > MAX_DISPLAY_CHARS) {
            snprintf(trackBuf, sizeof(trackBuf), "%.*s...", MAX_DISPLAY_CHARS, trackName);
            trackName = trackBuf;
        }
        addString(trackName, contentStartX + textOffset, currentY, Justify::LEFT,
            this->getFont(FontCategory::TITLE), textColor, dim.fontSize);
        currentY += trackHeight;
    }

    // Weather row (conditions + temperatures, with icon) - after track
    if ((m_enabledRows & ROW_WEATHER) && sessionData.conditions >= 0) {
        addIconQuad(contentStartX, currentY, iconWeather);

        // Get conditions string
        const char* conditionsStr = PluginUtils::getConditionsString(sessionData.conditions);

        // Format temperature based on unit setting
        // Note: -1.0f is the sentinel for "no data" in plugin_data.h
        char weatherBuffer[64];
        bool useFahrenheit = (UiConfig::getInstance().getTemperatureUnit() == TemperatureUnit::FAHRENHEIT);
        bool hasAirTemp = (sessionData.airTemperature != -1.0f);
        bool hasTrackTemp = (sessionData.trackTemperature != -1.0f);

        if (hasAirTemp && hasTrackTemp) {
            // Show both air and track temperature (GP Bikes, WRS, KRP)
            if (useFahrenheit) {
                int airF = static_cast<int>(sessionData.airTemperature * 1.8f + 32.0f);
                int trackF = static_cast<int>(sessionData.trackTemperature * 1.8f + 32.0f);
                snprintf(weatherBuffer, sizeof(weatherBuffer), "%s, %d / %d F", conditionsStr, airF, trackF);
            } else {
                int airC = static_cast<int>(sessionData.airTemperature);
                int trackC = static_cast<int>(sessionData.trackTemperature);
                snprintf(weatherBuffer, sizeof(weatherBuffer), "%s, %d / %d C", conditionsStr, airC, trackC);
            }
        } else if (hasAirTemp) {
            // Only air temperature (MX Bikes)
            if (useFahrenheit) {
                int tempF = static_cast<int>(sessionData.airTemperature * 1.8f + 32.0f);
                snprintf(weatherBuffer, sizeof(weatherBuffer), "%s, %d F", conditionsStr, tempF);
            } else {
                int tempC = static_cast<int>(sessionData.airTemperature);
                snprintf(weatherBuffer, sizeof(weatherBuffer), "%s, %d C", conditionsStr, tempC);
            }
        } else {
            // No temperature data, just show conditions
            snprintf(weatherBuffer, sizeof(weatherBuffer), "%s", conditionsStr);
        }

        addString(weatherBuffer, contentStartX + textOffset, currentY, Justify::LEFT,
            this->getFont(FontCategory::TITLE), textColor, dim.fontSize);
        currentY += dim.lineHeightNormal;
    }

    // Server row: server name (online + name) / "Testing" (offline) / "Unknown" (no info)
    if (m_enabledRows & ROW_SERVER) {
        addIconQuad(contentStartX, currentY, iconServer);
        const char* serverText;
        if (isOnline && hasServerName) {
            serverText = sessionData.serverName;
        } else if (isOffline) {
            serverText = "Testing";
        } else {
            serverText = "Unknown";
        }
        char serverBuf[32];
        if (strlen(serverText) > MAX_DISPLAY_CHARS) {
            snprintf(serverBuf, sizeof(serverBuf), "%.*s...", MAX_DISPLAY_CHARS, serverText);
            serverText = serverBuf;
        }
        addString(serverText, contentStartX + textOffset, currentY, Justify::LEFT,
            this->getFont(FontCategory::TITLE), textColor, dim.fontSize);
        currentY += serverHeight;
    }

    // Set bounds for drag detection
    setBounds(startX, startY, startX + backgroundWidth, startY + backgroundHeight);
}

void SessionHud::resetToDefaults() {
    m_bVisible = false;  // Disabled by default
    m_bShowTitle = false;  // No title by default
    setTextureVariant(0);  // No texture by default
    m_fBackgroundOpacity = 0.8f;
    m_fScale = 1.0f;
    m_enabledRows = ROW_DEFAULT;  // Reset row visibility
    m_bShowIcons = true;  // Icons enabled by default
    setPosition(0.0055f, 0.1332f);

    // Reset cached values to force rebuild on next update
    m_cachedEventType = -1;
    m_cachedSession = -1;
    m_cachedSessionState = -1;
    m_cachedSessionLength = -1;
    m_cachedSessionNumLaps = -1;
    m_cachedServerType = CACHE_UNINITIALIZED;
    m_cachedServerName[0] = '\0';
    m_cachedConditions = -1;
    m_cachedAirTemperature = -1.0f;
    m_cachedTrackTemperature = -1.0f;

    setDataDirty();
}
