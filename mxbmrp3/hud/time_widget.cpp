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
{
    m_panelKind = PanelKind::Widget;
    m_bContentCard = true;
    // One-time setup
    DEBUG_INFO("TimeWidget created");
    setDraggable(true);
    m_strings.reserve(2);  // label (optional), time

    // Set texture base name for dynamic texture discovery
    setTextureBaseName("time_widget");

    // Set all configurable defaults
    resetToDefaults();

    rebuildRenderData();
}

bool TimeWidget::handlesDataType(DataChangeType dataType) const {
    // Only SessionData (session length/type/format). The per-second clock tick is
    // driven by update() polling getSessionTime(), and the widget no longer reads
    // leader laps-to-go, so it doesn't need the high-frequency Standings notifies.
    return dataType == DataChangeType::SessionData;
}

void TimeWidget::update() {
    // OPTIMIZATION: Skip processing when not visible
    if (!isVisibleAnySurface()) {
        clearDataDirty();
        clearLayoutDirty();
        return;
    }

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
        rebuildAndRecord();
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
    // BOX-MODEL: one source of geometry — the fast path duplicated the sizing
    // arithmetic, and a handful of strings is cheaper to rebuild than the drift.
    rebuildRenderData();
}

void TimeWidget::rebuildRenderData() {
    // Clear render data
    clearStrings();
    m_quads.clear();

    auto dim = getScaledDimensions();

    // Get session data
    const PluginData& pluginData = PluginData::getInstance();
    int sessionTime = pluginData.getSessionTime();

    // Plain MM:SS countdown only (freezes at "00:00" when a timed clock expires).
    // Deliberately NOT the shared formatSessionClock() overtime labels ("N TO GO" /
    // "FINAL LAP" / "CHECKERED") - this widget shows just the time; the StandingsHud
    // title and web overlay still carry the leader-relative overtime label.
    char timeBuffer[16];
    PluginUtils::formatTimeMinutesSeconds(sessionTime, timeBuffer, sizeof(timeBuffer));

    // Use full opacity for text
    unsigned long textColor = this->getColor(ColorSlot::PRIMARY);

    float startX = 0.0f;
    float startY = 0.0f;

    // BOX-MODEL: the plan owns padding, chrome, the title band and the content
    // origin. The fixed 12-char column (shared with Position/Lap/Clock) is the
    // content width, so the four standard widgets keep tiling with each other.
    BaseHud::PanelWant want;
    want.contentW = PluginUtils::calculateMonospaceTextWidth(
        WidgetDimensions::STANDARD_WIDTH, dim.fontSize);
    want.sectionH = { bigValueRowHeight(dim) };  // Value (2 lines)
    want.captionW = planTitleWidth(dim, "Time");
    PanelPlan& p = planPanel(dim, want);
    const float backgroundWidth = p.width();
    const float backgroundHeight = p.height();

    addPlanBackground(p, startX, startY);
    addPlanTitle(p, "Time", this->getFont(FontCategory::TITLE), textColor);

    const float contentStartX = p.contentX();
    const float currentY = p.contentY();

    // Time value (extra large font - spans 2 lines)
    addString(timeBuffer, contentStartX, bigValueTextY(currentY, dim), Justify::LEFT,
        this->getFont(FontCategory::TITLE), textColor, dim.fontSizeExtraLarge);

    // Set bounds for drag detection
    setBounds(startX, startY, startX + backgroundWidth, startY + backgroundHeight);
}

void TimeWidget::resetToDefaults() {
    m_bVisible = false;  // Standings title now shows session/time info
    m_bShowTitle = true;
    setTextureVariant(0);  // No texture by default
    m_fBackgroundOpacity = 0.0f;
    m_fScale = 1.0f;
    setPosition(cellsX(35), cellsY(1));
    setDataDirty();
}
