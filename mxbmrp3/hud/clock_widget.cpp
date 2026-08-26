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
    : m_bShowUtc(false)
    , m_bUtcOnTop(false)
    , m_bFormat24h(true)
{
    m_panelKind = PanelKind::Widget;
    m_bContentCard = true;
    DEBUG_INFO("ClockWidget created");
    setDraggable(true);
    m_strings.reserve(3);  // label (optional), primary time, secondary line (optional)
    setTextureBaseName("clock_widget");

    resetToDefaults();

    rebuildRenderData();
}

bool ClockWidget::handlesDataType(DataChangeType /*dataType*/) const {
    // Clock doesn't depend on game data - it self-updates via system time
    return false;
}

void ClockWidget::update() {
    if (!isVisibleAnySurface()) {
        clearDataDirty();
        clearLayoutDirty();
        return;
    }

    // MINUTE CHANGES ARE DETECTED FROM THE EPOCH, not from a broken-down time.
    // This called localtime_s() (and gmtime_s() with UTC on) EVERY FRAME purely to
    // compare a minute that changes once every 60 seconds -- and on the MSVC CRT
    // those consult the timezone database under a lock, which measured 1.19us/frame
    // in-game across a stint with ZERO rebuilds: the whole cost was the check.
    //
    // time_t is seconds since the epoch, so `now / 60` ticks at exactly the instant
    // any wall-clock minute rolls over, in every timezone -- including the :30 and
    // :45 offsets, whose minute boundary still lands on the same second. One integer
    // divide replaces both conversions, and the conversions now happen only inside
    // the rebuild that actually formats the time.
    const std::time_t now = std::time(nullptr);
    const long long epochMinute = static_cast<long long>(now / 60);
    if (epochMinute != m_cachedEpochMinute) {
        setDataDirty();
    }

    if (isDataDirty()) {
        rebuildAndRecord();
        m_cachedEpochMinute = epochMinute;
        clearDataDirty();
        clearLayoutDirty();
    }
    else if (isLayoutDirty()) {
        rebuildLayout();
        clearLayoutDirty();
    }
}

void ClockWidget::rebuildLayout() {
    // BOX-MODEL: one source of geometry — the fast path duplicated the sizing
    // arithmetic, and a handful of strings is cheaper to rebuild than the drift.
    rebuildRenderData();
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

    // BOX-MODEL: the plan owns padding, chrome, the title band and the content
    // origin. The fixed 12-char column (shared with Position/Lap/Time) is the
    // content width, so the four standard widgets keep tiling with each other.
    BaseHud::PanelWant want;
    want.contentW = PluginUtils::calculateMonospaceTextWidth(
        WidgetDimensions::STANDARD_WIDTH, dim.fontSize);
    want.sectionH = { bigValueRowHeight(dim) };  // the big value's row; the UTC line rides in the bottom padding
    want.captionW = planTitleWidth(dim, titleLabel);
    PanelPlan& p = planPanel(dim, want);
    const float backgroundWidth = p.width();
    const float backgroundHeight = p.height();

    addPlanBackground(p, startX, startY);
    addPlanTitle(p, titleLabel, this->getFont(FontCategory::TITLE), textColor);

    const float contentStartX = p.contentX();
    float currentY = p.contentY();

    // Primary time (extra large)
    addString(primaryBuf, contentStartX, bigValueTextY(currentY, dim), Justify::LEFT,
        this->getFont(FontCategory::TITLE), textColor, dim.fontSizeExtraLarge);
    currentY += bigValueRowHeight(dim);

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
    setPosition(cellsX(52), cellsY(1));
    setDataDirty();
}
