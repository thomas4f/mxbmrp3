// ============================================================================
// hud/session_hud.cpp
// Session HUD - displays session info (server, track, format, weather)
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
    : m_cachedSessionState(-1)
    , m_cachedSessionLength(-1)
    , m_cachedSessionNumLaps(-1)
    , m_cachedServerType(CACHE_UNINITIALIZED)
    , m_cachedMultiRider(false)
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
    // Server is the headline row (promoted from the bottom in place of the removed
    // session-type row): large line height, like the type row used to have.
    float serverHeight = (m_enabledRows & ROW_SERVER) ? dim.lineHeightLarge : 0.0f;
    float formatHeight = (m_enabledRows & ROW_FORMAT) ? dim.lineHeightNormal : 0.0f;
    float trackHeight = (m_enabledRows & ROW_TRACK) ? dim.lineHeightNormal : 0.0f;
    float weatherHeight = ((m_enabledRows & ROW_WEATHER) && sessionData.conditions >= 0) ? dim.lineHeightNormal : 0.0f;

    return labelHeight + serverHeight + formatHeight + trackHeight + weatherHeight;
}

void SessionHud::update() {
    // OPTIMIZATION: Skip processing when not visible
    if (!isVisibleAnySurface()) {
        clearDataDirty();
        clearLayoutDirty();
        return;
    }

    // Get session data
    const PluginData& pluginData = PluginData::getInstance();
    const SessionData& sessionData = pluginData.getSessionData();

    int sessionState = sessionData.sessionState;
    int sessionLength = sessionData.sessionLength;
    int sessionNumLaps = sessionData.sessionNumLaps;
    int serverType = sessionData.serverType;
    // The server label reads ">1 rider" (via serverLabel's riderCount) to show "Online"
    // when serverType is unknown (GP Bikes / KRP), so a rider joining/leaving must
    // invalidate this fingerprint - only the 1<->>1 boundary matters, so a bool suffices.
    bool multiRider = pluginData.getRaceEntries().size() > 1;

    // Check if any session data changed (session type/index no longer rendered here
    // — it moved to the StandingsHud title — so it's not part of the dirty check).
    if (sessionState != m_cachedSessionState ||
        sessionLength != m_cachedSessionLength || sessionNumLaps != m_cachedSessionNumLaps ||
        serverType != m_cachedServerType || multiRider != m_cachedMultiRider ||
        strcmp(sessionData.serverName, m_cachedServerName) != 0 ||
        sessionData.conditions != m_cachedConditions || sessionData.airTemperature != m_cachedAirTemperature ||
        sessionData.trackTemperature != m_cachedTrackTemperature) {
        setDataDirty();
    }

    // Check data dirty first (takes precedence)
    if (isDataDirty()) {
        rebuildRenderData();
        m_cachedSessionState = sessionState;
        m_cachedSessionLength = sessionLength;
        m_cachedSessionNumLaps = sessionNumLaps;
        m_cachedServerType = serverType;
        m_cachedMultiRider = multiRider;
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

    // Individual row heights for positioning (must match rebuildRenderData — the
    // server row is the extra-large headline at the top).
    float labelHeight = m_bShowTitle ? dim.lineHeightNormal : 0.0f;
    float serverHeight = (m_enabledRows & ROW_SERVER) ? dim.lineHeightLarge : 0.0f;
    float formatHeight = (m_enabledRows & ROW_FORMAT) ? dim.lineHeightNormal : 0.0f;
    float trackHeight = (m_enabledRows & ROW_TRACK) ? dim.lineHeightNormal : 0.0f;

    // Set bounds for drag detection
    setBounds(startX, startY, startX + backgroundWidth, startY + backgroundHeight);

    // Update background quad position
    updateBackgroundQuadPosition(startX, startY, backgroundWidth, backgroundHeight);

    float contentStartX = startX + dim.paddingH;
    float contentStartY = startY + dim.paddingV;
    float currentY = contentStartY;

    // Icon setup (must match rebuildRenderData). The server headline has no icon;
    // the remaining rows use a normal-size icon and indent.
    float iconTextGap = dim.paddingH * 0.3f;  // Small gap between icon and text
    float textOffset = m_bShowIcons
        ? (dim.fontSize * ICON_SIZE_FACTOR) / UI_ASPECT_RATIO + iconTextGap
        : 0.0f;

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

    // Server headline (extra-large font, no icon)
    if (m_enabledRows & ROW_SERVER) {
        if (positionString(stringIndex, contentStartX, currentY)) {
            stringIndex++;
        }
        currentY += serverHeight;
    }

    // Track name (normal font with icon) - directly under the server headline.
    // NOTE: order must match rebuildRenderData() (strings/icons positioned by index).
    if (m_enabledRows & ROW_TRACK) {
        repositionIconQuad(iconQuadIndex++, contentStartX, currentY);
        if (positionString(stringIndex, contentStartX + textOffset, currentY)) {
            stringIndex++;
        }
        currentY += trackHeight;
    }

    // Format + Session state (normal font with icon)
    if (m_enabledRows & ROW_FORMAT) {
        repositionIconQuad(iconQuadIndex++, contentStartX, currentY);
        if (positionString(stringIndex, contentStartX + textOffset, currentY)) {
            stringIndex++;
        }
        currentY += formatHeight;
    }

    // Weather row (conditions + temperature, with icon) - after track
    if ((m_enabledRows & ROW_WEATHER) && sessionData.conditions >= 0) {
        repositionIconQuad(iconQuadIndex++, contentStartX, currentY);
        if (positionString(stringIndex, contentStartX + textOffset, currentY)) {
            stringIndex++;
        }
        currentY += dim.lineHeightNormal;
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

    int sessionState = sessionData.sessionState;
    bool isOnline = sessionData.isOnline();

    // Session state string (the session type now lives in the StandingsHud title)
    const char* stateString = PluginUtils::getSessionStateString(sessionState);

    float startX = 0.0f;
    float startY = 0.0f;

    // Calculate dimensions
    float backgroundWidth = calculateBackgroundWidth(WidgetDimensions::SESSION_WIDTH);
    float backgroundHeight = dim.paddingV + calculateContentHeight(dim) + dim.paddingV;

    // Add background quad
    addBackgroundQuad(startX, startY, backgroundWidth, backgroundHeight);

    // Individual row heights for positioning. The server row is the headline now
    // (promoted to the top in place of the removed session-type row), so it uses
    // the large line height / extra-large font the type row used to.
    float labelHeight = m_bShowTitle ? dim.lineHeightNormal : 0.0f;
    float serverHeight = (m_enabledRows & ROW_SERVER) ? dim.lineHeightLarge : 0.0f;
    float formatHeight = (m_enabledRows & ROW_FORMAT) ? dim.lineHeightNormal : 0.0f;
    float trackHeight = (m_enabledRows & ROW_TRACK) ? dim.lineHeightNormal : 0.0f;

    float contentStartX = startX + dim.paddingH;
    float contentStartY = startY + dim.paddingV;
    float currentY = contentStartY;

    // Icon setup. All icon rows use the normal font size; the server headline has
    // no icon (flush-left). textOffset is the text indent past the icon (0 when
    // icons are off).
    float iconTextGap = dim.paddingH * 0.3f;  // Small gap between icon and text
    float textOffset = m_bShowIcons
        ? (dim.fontSize * ICON_SIZE_FACTOR) / UI_ASPECT_RATIO + iconTextGap
        : 0.0f;
    AssetManager& assetMgr = AssetManager::getInstance();

    // Get specific icons for each row type (only if icons enabled)
    int iconFormat = m_bShowIcons ? assetMgr.getIconSpriteIndex("clock") : 0;      // Time/format
    int iconTrack = m_bShowIcons ? assetMgr.getIconSpriteIndex("location-dot") : 0;    // Track location
    int iconWeather = m_bShowIcons ? assetMgr.getIconSpriteIndex("temperature-low") : 0;  // Weather/temperature

    // Use full opacity for text and icons
    unsigned long textColor = this->getColor(ColorSlot::PRIMARY);
    unsigned long iconColor = this->getColor(ColorSlot::PRIMARY);  // White icons to match text

    // Helper lambda to add an icon quad with a specific sprite (normal font size)
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

    // Server row: headline at the top (server name online / "Testing" offline /
    // "Unknown"). Extra-large font, no icon (flush-left) — replaces the old
    // session-type row.
    if (m_enabledRows & ROW_SERVER) {
        // Shared label: name / "Testing" (solo) / "Online" / "Unknown" - see
        // PluginUtils::serverLabel. Rider count lets it read GP Bikes / KRP (no
        // serverType in their API) as "Online" once a real opponent is present.
        const char* serverText = PluginUtils::serverLabel(sessionData.serverType, sessionData.serverName,
            static_cast<int>(pluginData.getRaceEntries().size()));
        // Extra-large chars are ~2x normal width, so the usual MAX_DISPLAY_CHARS
        // would overflow the fixed-width background. Truncate to what fits the
        // content area at this font size.
        float contentWidth = PluginUtils::calculateMonospaceTextWidth(WidgetDimensions::SESSION_WIDTH, dim.fontSize);
        float headlineCharW = PluginUtils::calculateMonospaceTextWidth(1, dim.fontSizeExtraLarge);
        int maxHeadlineChars = (headlineCharW > 0.0f)
            ? static_cast<int>(contentWidth / headlineCharW)
            : MAX_DISPLAY_CHARS;
        if (maxHeadlineChars < 4) maxHeadlineChars = 4;
        // Shared ellipsis truncation (ellipsis folded into the budget).
        std::string serverFit = PluginUtils::fitText(serverText, maxHeadlineChars);
        addString(serverFit.c_str(), contentStartX, currentY, Justify::LEFT,
            this->getFont(FontCategory::TITLE), textColor, dim.fontSizeExtraLarge);
        currentY += serverHeight;
    }

    // Track name (normal font with icon) - directly under the server headline,
    // mirroring Discord's "server then track-led detail" layout.
    if (m_enabledRows & ROW_TRACK) {
        addIconQuad(contentStartX, currentY, iconTrack);
        const char* trackName = sessionData.trackName[0] != '\0' ? sessionData.trackName : Placeholders::GENERIC;
        // Shared ellipsis truncation (ellipsis folded into the budget).
        std::string trackFit = PluginUtils::fitText(trackName, MAX_DISPLAY_CHARS);
        addString(trackFit.c_str(), contentStartX + textOffset, currentY, Justify::LEFT,
            this->getFont(FontCategory::TITLE), textColor, dim.fontSize);
        currentY += trackHeight;
    }

    // Session + Format + State on one line: "Session (Format), State" - matches the
    // Steam status / Friends Info / Discord detail line. The session name is added
    // only when online; offline it's already the server-row headline ("Testing").
    if (m_enabledRows & ROW_FORMAT) {
        addIconQuad(contentStartX, currentY, iconFormat);
        const char* sessionStateString = stateString ? stateString : Placeholders::GENERIC;

        const char* sessionStr = (isOnline && sessionData.session >= 0)
            ? PluginUtils::getSessionString(sessionData.eventType, sessionData.session) : nullptr;

        // Shared helper: "8:00 + 2L" / "8:00" / "2L" / "" (now "8:00" not "08:00").
        char formatBuffer[64];
        PluginUtils::formatSessionFormat(sessionData.sessionLength, sessionData.sessionNumLaps, formatBuffer, sizeof(formatBuffer));

        // Head = "Session (Format)" (any piece optional).
        char head[96];
        head[0] = '\0';
        if (sessionStr && formatBuffer[0] != '\0')  snprintf(head, sizeof(head), "%s (%s)", sessionStr, formatBuffer);
        else if (sessionStr)                        snprintf(head, sizeof(head), "%s", sessionStr);
        else if (formatBuffer[0] != '\0')           snprintf(head, sizeof(head), "(%s)", formatBuffer);

        // Append ", State" (skip if it duplicates the session name).
        const bool showState = (!sessionStr || strcmp(sessionStr, sessionStateString) != 0);
        char combinedBuffer[128];
        if (head[0] != '\0' && showState)  snprintf(combinedBuffer, sizeof(combinedBuffer), "%s, %s", head, sessionStateString);
        else if (head[0] != '\0')          snprintf(combinedBuffer, sizeof(combinedBuffer), "%s", head);
        else                               snprintf(combinedBuffer, sizeof(combinedBuffer), "%s", sessionStateString);

        addString(combinedBuffer, contentStartX + textOffset, currentY, Justify::LEFT,
            this->getFont(FontCategory::TITLE), textColor, dim.fontSize);
        currentY += formatHeight;
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
    setPosition(0.0055f, 0.12907f);

    // Reset cached values to force rebuild on next update
    m_cachedSessionState = -1;
    m_cachedSessionLength = -1;
    m_cachedSessionNumLaps = -1;
    m_cachedServerType = CACHE_UNINITIALIZED;
    m_cachedMultiRider = false;
    m_cachedServerName[0] = '\0';
    m_cachedConditions = -1;
    m_cachedAirTemperature = -1.0f;
    m_cachedTrackTemperature = -1.0f;

    setDataDirty();
}
