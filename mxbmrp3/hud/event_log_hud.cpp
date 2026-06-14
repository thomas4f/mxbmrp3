// ============================================================================
// hud/event_log_hud.cpp
// Event Log HUD - displays a scrolling log of notable race events
// ============================================================================

#include "event_log_hud.h"
#include "../core/plugin_data.h"
#include "../core/plugin_constants.h"
#include "../core/plugin_utils.h"
#include "../core/color_config.h"
#include "../core/font_config.h"
#include "../core/asset_manager.h"
#include <ctime>

EventLogHud::EventLogHud() {
    setTextureBaseName("event_log_hud");
    setDraggable(true);
    resetToDefaults();
}

void EventLogHud::resetToDefaults() {
    m_bVisible = false;
    m_bShowTitle = true;
    setTextureVariant(0);
    m_fBackgroundOpacity = 0.80f;
    m_fScale = 1.0f;
    setPosition(0.7315f, 0.5106f);  // Right column, before Friends in the stack
    m_displayMode = DisplayMode::ON;
    m_displayOrder = DisplayOrder::OLDEST_FIRST;
    m_enabledEvents = EVENT_DEFAULT;
    m_maxDisplayEvents = 6;
    m_autoHideDurationMs = DEFAULT_AUTO_HIDE_MS;
    m_timestampMode = TimestampMode::CLOCK;
    m_showIcons = true;
    m_hasEvents = false;
    setDataDirty();
}

bool EventLogHud::handlesDataType(DataChangeType dataType) const {
    return dataType == DataChangeType::EventLog
        || dataType == DataChangeType::SessionData;
}

void EventLogHud::update() {
    if (!isVisible() || m_displayMode == DisplayMode::OFF) {
        if (!m_quads.empty() || !m_strings.empty()) {
            m_quads.clear();
            clearStrings();
        }
        clearDataDirty();
        clearLayoutDirty();
        return;
    }

    // Track new events for auto-hide timing
    const auto& eventLog = PluginData::getInstance().getEventLog();
    if (!eventLog.empty()) {
        const auto& latest = eventLog.back();
        if (latest.steadyTime != m_lastEventTime) {
            m_lastEventTime = latest.steadyTime;
            m_hasEvents = true;
            setDataDirty();
        }
    } else if (m_hasEvents) {
        // Event log was cleared (session change) — reset auto-hide state
        m_hasEvents = false;
        setDataDirty();
    }

    // Auto-hide: check if we should be visible
    if (m_displayMode == DisplayMode::AUTO_HIDE) {
        if (!m_hasEvents) {
            if (!m_quads.empty() || !m_strings.empty()) {
                m_quads.clear();
                clearStrings();
            }
            clearDataDirty();
            clearLayoutDirty();
            return;
        }

        auto now = std::chrono::steady_clock::now();
        auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(now - m_lastEventTime).count();
        if (elapsed >= m_autoHideDurationMs) {
            if (!m_quads.empty() || !m_strings.empty()) {
                m_quads.clear();
                clearStrings();
            }
            m_hasEvents = false;  // Stop checking timer until next event arrives
            clearDataDirty();
            clearLayoutDirty();
            return;
        }
    }

    processDirtyFlags();
}

int EventLogHud::getIconForEvent(EventLogType type) const {
    if (!m_showIcons) return 0;
    const AssetManager& assets = AssetManager::getInstance();
    switch (type) {
    case EventLogType::SessionStarted:    return assets.getIconSpriteIndex("flag");
    case EventLogType::SessionStateChange:return assets.getIconSpriteIndex("flag");
    case EventLogType::SessionComplete:   return assets.getIconSpriteIndex("flag-checkered");
    case EventLogType::SessionPreStart:   return assets.getIconSpriteIndex("hourglass-half");
    case EventLogType::FastestLap:        return assets.getIconSpriteIndex("stopwatch");
    case EventLogType::Penalty:           return assets.getIconSpriteIndex("exclamation");
    case EventLogType::PenaltyClear:      return assets.getIconSpriteIndex("circle-xmark");
    case EventLogType::PenaltyChange:     return assets.getIconSpriteIndex("exclamation");
    case EventLogType::RiderRetired:      return assets.getIconSpriteIndex("xmark");
    case EventLogType::RiderDSQ:          return assets.getIconSpriteIndex("ban");
    case EventLogType::RiderDNS:          return assets.getIconSpriteIndex("xmark");
    case EventLogType::OvertimeStarted:   return assets.getIconSpriteIndex("stopwatch");
    case EventLogType::SessionTimeExpired:return assets.getIconSpriteIndex("stopwatch");
    case EventLogType::FinalLap:          return assets.getIconSpriteIndex("flag");
    case EventLogType::RiderFinished:     return assets.getIconSpriteIndex("flag-checkered");
    case EventLogType::LeaderChange:      return assets.getIconSpriteIndex("crown");
    case EventLogType::PitEntry:          return assets.getIconSpriteIndex("wrench");
    case EventLogType::PitExit:           return assets.getIconSpriteIndex("wrench");
    default: return 0;
    }
}

unsigned long EventLogHud::getIconColorForEvent(EventLogType type) const {
    using namespace PluginConstants;
    switch (type) {
    case EventLogType::SessionStarted:    return getColor(ColorSlot::POSITIVE);
    case EventLogType::SessionPreStart:   return ColorPalette::BROWN;
    case EventLogType::FastestLap:        return ColorPalette::PINK;
    case EventLogType::Penalty:
    case EventLogType::PenaltyChange:     return getColor(ColorSlot::NEGATIVE);
    case EventLogType::FinalLap:          return ColorPalette::WHITE;
    case EventLogType::RiderFinished:     return ColorPalette::WHITE;
    case EventLogType::LeaderChange:      return PodiumColors::GOLD;
    case EventLogType::SessionComplete:   return ColorPalette::WHITE;
    case EventLogType::RiderRetired:
    case EventLogType::RiderDNS:          return getColor(ColorSlot::MUTED);
    case EventLogType::RiderDSQ:          return getColor(ColorSlot::NEGATIVE);
    case EventLogType::PitEntry:
    case EventLogType::PitExit:           return ColorPalette::GRAY;
    default:                              return getColor(ColorSlot::PRIMARY);
    }
}

int EventLogHud::getBackgroundWidthChars() const {
    int width = MESSAGE_WIDTH;
    if (m_showIcons) width += ICON_COL_WIDTH;
    if (m_timestampMode != TimestampMode::OFF) width += TIMESTAMP_WIDTH;
    return width;
}

void EventLogHud::rebuildRenderData() {
    m_quads.clear();
    clearStrings();
    m_iconQuads.clear();

    const auto& pluginData = PluginData::getInstance();
    const auto& eventLog = pluginData.getEventLog();

    auto dim = getScaledDimensions();
    float startX = START_X;
    float startY = START_Y;

    // Calculate background dimensions
    int bgWidthChars = getBackgroundWidthChars();
    float backgroundWidth = calculateBackgroundWidth(bgWidthChars);
    float contentStartX = startX + dim.paddingH;

    // Filter events by enabled flags and collect visible entries
    std::vector<const EventLogEntry*> visibleEntries;
    if (!eventLog.empty()) {
        visibleEntries.reserve(eventLog.size());
        for (const auto& entry : eventLog) {
            uint32_t flag = eventLogTypeToFlag(entry.type);
            if (m_enabledEvents & flag) {
                visibleEntries.push_back(&entry);
            }
        }
    }

    // Determine which entries to show (last N based on m_maxDisplayEvents)
    int totalVisible = static_cast<int>(visibleEntries.size());
    int startIdx = (totalVisible > m_maxDisplayEvents) ? totalVisible - m_maxDisplayEvents : 0;
    int numRows = totalVisible - startIdx;

    // Always size the background for m_maxDisplayEvents so the HUD shows its
    // full configured size even when empty or partially filled
    m_cachedNumDataRows = m_maxDisplayEvents;
    float backgroundHeight = calculateBackgroundHeight(m_maxDisplayEvents, m_bShowTitle);

    // Add background
    addBackgroundQuad(startX, startY, backgroundWidth, backgroundHeight);

    float currentY = startY + dim.paddingV;

    // Title
    if (m_bShowTitle) {
        addTitleString("Event Log", contentStartX, currentY,
                       PluginConstants::Justify::LEFT, this->getFont(FontCategory::TITLE),
                       getColor(ColorSlot::PRIMARY), dim.fontSizeLarge);
        currentY += dim.lineHeightLarge;
    }

    // Layout: [icon] [timestamp] message — columns are optional
    float iconHalfSize = ICON_BASE_SIZE * m_fScale;
    float iconHalfWidth = iconHalfSize / PluginConstants::UI_ASPECT_RATIO;
    float iconColWidth = PluginUtils::calculateMonospaceTextWidth(ICON_COL_WIDTH, dim.fontSize);
    float iconOffset = m_showIcons ? iconColWidth : 0.0f;
    float timestampWidth = (m_timestampMode != TimestampMode::OFF)
        ? PluginUtils::calculateMonospaceTextWidth(TIMESTAMP_WIDTH, dim.fontSize) : 0.0f;

    float iconX = contentStartX;
    float timestampX = contentStartX + iconOffset;
    float messageX = timestampX + timestampWidth;

    // Fill empty rows with muted placeholder dashes so the player can see
    // the full extent of the HUD before any events arrive
    int emptyRows = m_maxDisplayEvents - numRows;
    unsigned long placeholderColor = getColor(ColorSlot::MUTED);

    if (m_displayOrder == DisplayOrder::NEWEST_FIRST) {
        // Newest first: real events at top, placeholders fill the bottom
    } else {
        // Oldest first: placeholders fill the top, real events at bottom
        for (int i = 0; i < emptyRows; ++i) {
            addString("", timestampX, currentY,
                      PluginConstants::Justify::LEFT, this->getFont(FontCategory::DIGITS),
                      placeholderColor, dim.fontSize);
            addString("", messageX, currentY,
                      PluginConstants::Justify::LEFT, this->getFont(FontCategory::NORMAL),
                      placeholderColor, dim.fontSize);
            addString("", messageX, currentY,
                      PluginConstants::Justify::LEFT, this->getFont(FontCategory::NORMAL),
                      placeholderColor, dim.fontSize);
            currentY += dim.lineHeightNormal;
        }
    }

    // Build entries in correct order
    // currentRow tracks absolute row index from first data row (for icon repositioning)
    int currentRow = (m_displayOrder == DisplayOrder::OLDEST_FIRST) ? emptyRows : 0;
    auto renderEntry = [&](const EventLogEntry* entry) {
        // Icon (leftmost, visual anchor for scanning)
        int spriteIndex = getIconForEvent(entry->type);
        if (spriteIndex > 0) {
            float cx = iconX + iconColWidth * 0.25f;
            float cy = currentY + dim.lineHeightNormal * 0.5f;
            float ox = cx, oy = cy;
            applyOffset(ox, oy);

            SPluginQuad_t quad;
            quad.m_aafPos[0][0] = ox - iconHalfWidth; quad.m_aafPos[0][1] = oy - iconHalfSize;
            quad.m_aafPos[1][0] = ox - iconHalfWidth; quad.m_aafPos[1][1] = oy + iconHalfSize;
            quad.m_aafPos[2][0] = ox + iconHalfWidth; quad.m_aafPos[2][1] = oy + iconHalfSize;
            quad.m_aafPos[3][0] = ox + iconHalfWidth; quad.m_aafPos[3][1] = oy - iconHalfSize;
            quad.m_iSprite = spriteIndex;
            quad.m_ulColor = getIconColorForEvent(entry->type);
            m_iconQuads.push_back({m_quads.size(), currentRow});
            m_quads.push_back(quad);
        }

        // Timestamp (conditional — may be off)
        if (m_timestampMode != TimestampMode::OFF) {
            char timestamp[12];
            if (m_timestampMode == TimestampMode::CLOCK) {
                auto time_t = std::chrono::system_clock::to_time_t(entry->systemTime);
                struct tm localTime;
                localtime_s(&localTime, &time_t);
                snprintf(timestamp, sizeof(timestamp), "%02d:%02d:%02d",
                         localTime.tm_hour, localTime.tm_min, localTime.tm_sec);
            } else {
                int timeMs = entry->sessionTimeMs;
                bool negative = (timeMs < 0);
                int absTime = negative ? -timeMs : timeMs;
                int minutes = (absTime / 1000) / 60;
                int seconds = (absTime / 1000) % 60;
                if (minutes > 99) minutes = 99;
                if (negative) {
                    snprintf(timestamp, sizeof(timestamp), "-%02d:%02d", minutes, seconds);
                } else {
                    snprintf(timestamp, sizeof(timestamp), "%02d:%02d", minutes, seconds);
                }
            }
            addString(timestamp, timestampX, currentY,
                      PluginConstants::Justify::LEFT, this->getFont(FontCategory::DIGITS),
                      getColor(ColorSlot::TERTIARY), dim.fontSize);
        } else {
            // Empty string to keep consistent string count (3 per row)
            addString("", contentStartX, currentY,
                      PluginConstants::Justify::LEFT, this->getFont(FontCategory::DIGITS),
                      getColor(ColorSlot::TERTIARY), dim.fontSize);
        }

        // Message + detail (detail uses digits font in PRIMARY color)
        if (entry->detail[0] != '\0') {
            char msgWithColon[68];
            snprintf(msgWithColon, sizeof(msgWithColon), "%s:", entry->message);
            addString(msgWithColon, messageX, currentY,
                      PluginConstants::Justify::LEFT, this->getFont(FontCategory::NORMAL),
                      getColor(ColorSlot::SECONDARY), dim.fontSize);
            float detailX = messageX + PluginUtils::calculateMonospaceTextWidth(
                static_cast<int>(strlen(msgWithColon)) + 1, dim.fontSize);
            addString(entry->detail, detailX, currentY,
                      PluginConstants::Justify::LEFT, this->getFont(FontCategory::DIGITS),
                      getColor(ColorSlot::PRIMARY), dim.fontSize);
        } else {
            addString(entry->message, messageX, currentY,
                      PluginConstants::Justify::LEFT, this->getFont(FontCategory::NORMAL),
                      getColor(ColorSlot::SECONDARY), dim.fontSize);
            // Empty detail string to keep consistent string count (3 per row)
            addString("", messageX, currentY,
                      PluginConstants::Justify::LEFT, this->getFont(FontCategory::NORMAL),
                      getColor(ColorSlot::PRIMARY), dim.fontSize);
        }

        currentY += dim.lineHeightNormal;
        ++currentRow;
    };

    if (m_displayOrder == DisplayOrder::NEWEST_FIRST) {
        // Newest first: real events at top, placeholders fill the bottom
        for (int i = totalVisible - 1; i >= startIdx; --i) {
            renderEntry(visibleEntries[i]);
        }
        for (int i = 0; i < emptyRows; ++i) {
            addString("", timestampX, currentY,
                      PluginConstants::Justify::LEFT, this->getFont(FontCategory::DIGITS),
                      placeholderColor, dim.fontSize);
            addString("", messageX, currentY,
                      PluginConstants::Justify::LEFT, this->getFont(FontCategory::NORMAL),
                      placeholderColor, dim.fontSize);
            addString("", messageX, currentY,
                      PluginConstants::Justify::LEFT, this->getFont(FontCategory::NORMAL),
                      placeholderColor, dim.fontSize);
            currentY += dim.lineHeightNormal;
        }
    } else {
        // Oldest first: placeholders already rendered above, then real events
        for (int i = startIdx; i < totalVisible; ++i) {
            renderEntry(visibleEntries[i]);
        }
    }

    // Update bounds (unoffset — isPointInBounds applies offset at test time)
    setBounds(startX, startY, startX + backgroundWidth, startY + backgroundHeight);
}

void EventLogHud::rebuildLayout() {
    if (m_quads.empty()) {
        setBounds(0.0f, 0.0f, 0.0f, 0.0f);
        return;
    }

    auto dim = getScaledDimensions();

    float startX = START_X;
    float startY = START_Y;
    float backgroundWidth = calculateBackgroundWidth(getBackgroundWidthChars());
    float backgroundHeight = calculateBackgroundHeight(m_cachedNumDataRows, m_bShowTitle);

    // Update background quad position
    updateBackgroundQuadPosition(startX, startY, backgroundWidth, backgroundHeight);

    // Reposition all strings
    float contentStartX = startX + dim.paddingH;
    float currentY = startY + dim.paddingV;
    size_t stringIndex = 0;

    if (m_bShowTitle && stringIndex < m_strings.size()) {
        positionString(stringIndex++, contentStartX, currentY);
        currentY += dim.lineHeightLarge;
    }

    // Reposition event entries (3 strings per row: timestamp, message, detail)
    // Layout order: icon, timestamp, message [: detail]
    float iconHalfSize = ICON_BASE_SIZE * m_fScale;
    float iconHalfWidth = iconHalfSize / PluginConstants::UI_ASPECT_RATIO;
    float iconColWidth = PluginUtils::calculateMonospaceTextWidth(ICON_COL_WIDTH, dim.fontSize);
    float iconOffset = m_showIcons ? iconColWidth : 0.0f;
    float timestampWidth = (m_timestampMode != TimestampMode::OFF)
        ? PluginUtils::calculateMonospaceTextWidth(TIMESTAMP_WIDTH, dim.fontSize) : 0.0f;
    float iconX = contentStartX;
    float timestampX = contentStartX + iconOffset;
    float messageX = timestampX + timestampWidth;
    float dataStartY = currentY;

    for (int row = 0; row < m_cachedNumDataRows && stringIndex + 2 < m_strings.size(); ++row) {
        positionString(stringIndex++, timestampX, currentY);  // timestamp
        size_t msgIdx = stringIndex;
        positionString(stringIndex++, messageX, currentY);     // message
        // Detail: position after message text (message width + 1 char gap)
        int msgLen = static_cast<int>(strlen(m_strings[msgIdx].m_szString));
        float detailX = messageX + PluginUtils::calculateMonospaceTextWidth(msgLen + 1, dim.fontSize);
        positionString(stringIndex++, detailX, currentY);      // detail
        currentY += dim.lineHeightNormal;
    }

    // Reposition icon quads (same calculation as rebuildRenderData, prevents lag during drag)
    for (const auto& iconInfo : m_iconQuads) {
        if (iconInfo.quadIndex >= m_quads.size()) continue;
        float rowY = dataStartY + (iconInfo.rowIndex * dim.lineHeightNormal);
        float cx = iconX + iconColWidth * 0.25f;
        float cy = rowY + dim.lineHeightNormal * 0.5f;
        applyOffset(cx, cy);

        SPluginQuad_t& quad = m_quads[iconInfo.quadIndex];
        quad.m_aafPos[0][0] = cx - iconHalfWidth; quad.m_aafPos[0][1] = cy - iconHalfSize;
        quad.m_aafPos[1][0] = cx - iconHalfWidth; quad.m_aafPos[1][1] = cy + iconHalfSize;
        quad.m_aafPos[2][0] = cx + iconHalfWidth; quad.m_aafPos[2][1] = cy + iconHalfSize;
        quad.m_aafPos[3][0] = cx + iconHalfWidth; quad.m_aafPos[3][1] = cy - iconHalfSize;
    }

    // Update bounds (unoffset — isPointInBounds applies offset at test time)
    setBounds(startX, startY, startX + backgroundWidth, startY + backgroundHeight);
}
