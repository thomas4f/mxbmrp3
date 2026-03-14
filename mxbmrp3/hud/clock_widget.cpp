// ============================================================================
// hud/clock_widget.cpp
// Clock widget - displays local time with optional UTC secondary display
// ============================================================================
#include "clock_widget.h"
#include "../diagnostics/logger.h"
#include "../core/plugin_utils.h"
#include <cstdio>
#include <ctime>

using namespace PluginConstants;

ClockWidget::ClockWidget()
    : m_cachedLocalMinute(-1)
    , m_cachedUtcMinute(-1)
    , m_bShowUtc(false)
    , m_bUtcOnTop(false)
    , m_bFormat24h(true)
{
    DEBUG_INFO("ClockWidget created");
    setDraggable(true);
    m_strings.reserve(3);  // label (optional), primary time, secondary line (optional)

    resetToDefaults();

    rebuildRenderData();
}

bool ClockWidget::handlesDataType(DataChangeType /*dataType*/) const {
    // Clock doesn't depend on game data - it self-updates via system time
    return false;
}

void ClockWidget::update() {
    if (!isVisible()) {
        clearDataDirty();
        clearLayoutDirty();
        return;
    }

    // Check if the minute changed (no need to update more frequently for a clock)
    std::time_t now = std::time(nullptr);
    std::tm localTm = {};
    localtime_s(&localTm, &now);
    int currentLocalMinute = localTm.tm_hour * 60 + localTm.tm_min;

    if (currentLocalMinute != m_cachedLocalMinute) {
        setDataDirty();
    }

    int currentUtcMinute = m_cachedUtcMinute;
    if (m_bShowUtc) {
        std::tm utcTm = {};
        gmtime_s(&utcTm, &now);
        currentUtcMinute = utcTm.tm_hour * 60 + utcTm.tm_min;
        if (currentUtcMinute != m_cachedUtcMinute) {
            setDataDirty();
        }
    }

    if (isDataDirty()) {
        rebuildRenderData();
        // Update cached minutes
        m_cachedLocalMinute = currentLocalMinute;
        m_cachedUtcMinute = currentUtcMinute;
        clearDataDirty();
        clearLayoutDirty();
    }
    else if (isLayoutDirty()) {
        rebuildLayout();
        clearLayoutDirty();
    }
}

void ClockWidget::rebuildLayout() {
    auto dim = getScaledDimensions();

    float startX = 0.0f;
    float startY = 0.0f;

    float backgroundWidth = calculateBackgroundWidth(WidgetDimensions::STANDARD_WIDTH);

    // Height calculation - consistent with PositionWidget/LapWidget (no extra height for UTC line)
    float labelHeight = m_bShowTitle ? dim.lineHeightNormal : 0.0f;
    float contentHeight = labelHeight + dim.lineHeightLarge;  // Label (optional, 1 line) + Value (2 lines)
    float backgroundHeight = dim.paddingV + contentHeight + dim.paddingV;

    setBounds(startX, startY, startX + backgroundWidth, startY + backgroundHeight);
    updateBackgroundQuadPosition(startX, startY, backgroundWidth, backgroundHeight);

    float contentStartX = startX + dim.paddingH;
    float currentY = startY + dim.paddingV;

    int stringIndex = 0;

    // Title label
    if (m_bShowTitle && positionString(stringIndex, contentStartX, currentY)) {
        stringIndex++;
        currentY += labelHeight;
    }

    // Primary time (large)
    if (positionString(stringIndex, contentStartX, currentY)) {
        stringIndex++;
        currentY += dim.lineHeightLarge;
    }

    // Secondary time line (embedded in bottom padding, if UTC shown)
    if (m_bShowUtc) {
        positionString(stringIndex, contentStartX, currentY);
    }
}

void ClockWidget::rebuildRenderData() {
    clearStrings();
    m_quads.clear();

    auto dim = getScaledDimensions();

    // Get current time
    std::time_t now = std::time(nullptr);
    std::tm localTm = {};
    localtime_s(&localTm, &now);
    std::tm utcTm = {};
    gmtime_s(&utcTm, &now);

    // Format time strings
    char primaryBuf[16];
    char secondaryBuf[32] = {};

    // Large display: no AM/PM (would overflow), just the time digits
    auto formatTimeLarge = [this](char* buf, size_t bufSize, const std::tm& tm) {
        if (m_bFormat24h) {
            snprintf(buf, bufSize, "%02d:%02d", tm.tm_hour, tm.tm_min);
        } else {
            int hour12 = tm.tm_hour % 12;
            if (hour12 == 0) hour12 = 12;
            snprintf(buf, bufSize, "%d:%02d", hour12, tm.tm_min);
        }
    };

    // Small display: includes AM/PM suffix for 12h format
    auto formatTimeSmall = [this](char* buf, size_t bufSize, const std::tm& tm) {
        if (m_bFormat24h) {
            snprintf(buf, bufSize, "%02d:%02d", tm.tm_hour, tm.tm_min);
        } else {
            int hour12 = tm.tm_hour % 12;
            if (hour12 == 0) hour12 = 12;
            const char* ampm = (tm.tm_hour >= 12) ? "PM" : "AM";
            snprintf(buf, bufSize, "%d:%02d %s", hour12, tm.tm_min, ampm);
        }
    };

    // Determine which time is primary (large) and which is secondary (small)
    const char* titleLabel = "Clock";
    if (m_bShowUtc && m_bUtcOnTop) {
        // UTC is primary (large), local is secondary (small)
        formatTimeLarge(primaryBuf, sizeof(primaryBuf), utcTm);
        titleLabel = "UTC";
        // Secondary: "7:38 PM Local"
        char timePart[16];
        formatTimeSmall(timePart, sizeof(timePart), localTm);
        snprintf(secondaryBuf, sizeof(secondaryBuf), "%s Local", timePart);
    } else {
        // Local is primary (large)
        formatTimeLarge(primaryBuf, sizeof(primaryBuf), localTm);
        if (m_bShowUtc) {
            titleLabel = "Local";
            // Secondary: "06:38 UTC"
            char timePart[16];
            formatTimeSmall(timePart, sizeof(timePart), utcTm);
            snprintf(secondaryBuf, sizeof(secondaryBuf), "%s UTC", timePart);
        }
    }

    unsigned long textColor = this->getColor(ColorSlot::PRIMARY);

    float startX = 0.0f;
    float startY = 0.0f;

    float backgroundWidth = calculateBackgroundWidth(WidgetDimensions::STANDARD_WIDTH);

    // Height calculation - consistent with PositionWidget/LapWidget (no extra height for UTC line)
    float labelHeight = m_bShowTitle ? dim.lineHeightNormal : 0.0f;
    float contentHeight = labelHeight + dim.lineHeightLarge;  // Label (optional, 1 line) + Value (2 lines)
    float backgroundHeight = dim.paddingV + contentHeight + dim.paddingV;

    addBackgroundQuad(startX, startY, backgroundWidth, backgroundHeight);

    float contentStartX = startX + dim.paddingH;
    float currentY = startY + dim.paddingV;

    // Title label (optional)
    if (m_bShowTitle) {
        addString(titleLabel, contentStartX, currentY, Justify::LEFT,
            this->getFont(FontCategory::TITLE), textColor, dim.fontSize);
        currentY += labelHeight;
    }

    // Primary time (extra large)
    addString(primaryBuf, contentStartX, currentY, Justify::LEFT,
        this->getFont(FontCategory::TITLE), textColor, dim.fontSizeExtraLarge);
    currentY += dim.lineHeightLarge;

    // Secondary time (embedded in bottom padding - like TimeWidget's session type)
    if (m_bShowUtc) {
        addString(secondaryBuf, contentStartX, currentY, Justify::LEFT,
            this->getFont(FontCategory::TITLE), textColor, dim.fontSize);
    }

    setBounds(startX, startY, startX + backgroundWidth, startY + backgroundHeight);
}

void ClockWidget::resetToDefaults() {
    m_bVisible = false;  // Hidden by default - opt-in widget
    m_bShowTitle = true;
    m_bShowUtc = false;
    m_bUtcOnTop = false;
    m_bFormat24h = true;
    setTextureVariant(0);
    m_fBackgroundOpacity = 0.0f;
    m_fScale = 1.0f;
    setPosition(0.2860f, 0.0111f);
    setDataDirty();
}
