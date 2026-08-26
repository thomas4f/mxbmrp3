// ============================================================================
// hud/lap_widget.cpp
// Lap widget - displays current lap in minimal format (e.g., "L2/5" or "L2")
// Shows "Lx/y" for lap-only sessions, "Lx" for time-based or time+laps sessions
// ============================================================================
#include "lap_widget.h"

#include <cstdio>

#include "../diagnostics/logger.h"
#include "../diagnostics/timer.h"
#include "../core/plugin_utils.h"
#include "../core/color_config.h"

using namespace PluginConstants;

LapWidget::LapWidget()
    : m_cachedCurrentLap(-1)
    , m_cachedTotalLaps(-1)
    , m_cachedSessionLength(-1)
{
    m_panelKind = PanelKind::Widget;
    m_bContentCard = true;
    // One-time setup
    DEBUG_INFO("LapWidget created");
    setDraggable(true);
    m_strings.reserve(2);  // label (optional), lap value

    // Set texture base name for dynamic texture discovery
    setTextureBaseName("lap_widget");

    // Set all configurable defaults
    resetToDefaults();

    rebuildRenderData();
}

bool LapWidget::handlesDataType(DataChangeType dataType) const {
    return dataType == DataChangeType::SessionData ||
           dataType == DataChangeType::Standings ||
           dataType == DataChangeType::SpectateTarget;
}

void LapWidget::update() {
    // OPTIMIZATION: Skip processing when not visible
    if (!isVisibleAnySurface()) {
        clearDataDirty();
        clearLayoutDirty();
        return;
    }

    // Get current lap and total laps
    const PluginData& pluginData = PluginData::getInstance();
    const SessionData& sessionData = pluginData.getSessionData();
    int displayRaceNum = pluginData.getDisplayRaceNum();

    int currentLap = 0;
    int totalLaps = sessionData.sessionNumLaps;
    int sessionLength = sessionData.sessionLength;

    // Get rider's current lap from standings
    if (displayRaceNum > 0) {
        const StandingsData* standing = pluginData.getStanding(displayRaceNum);
        if (standing) {
            currentLap = standing->numLaps + 1;  // numLaps is completed laps, so add 1 for current lap
        }
    }

    // Check if lap data changed (including sessionLength since it affects display format)
    if (currentLap != m_cachedCurrentLap || totalLaps != m_cachedTotalLaps || sessionLength != m_cachedSessionLength) {
        setDataDirty();
    }

    // Check data dirty first (takes precedence)
    if (isDataDirty()) {
        rebuildAndRecord();
        m_cachedCurrentLap = currentLap;
        m_cachedTotalLaps = totalLaps;
        m_cachedSessionLength = sessionLength;
        clearDataDirty();
        clearLayoutDirty();
    }
    else if (isLayoutDirty()) {
        rebuildLayout();
        clearLayoutDirty();
    }
}

void LapWidget::rebuildLayout() {
    // BOX-MODEL: one source of geometry — the fast path duplicated the sizing
    // arithmetic, and a handful of strings is cheaper to rebuild than the drift.
    rebuildRenderData();
}

void LapWidget::rebuildRenderData() {
    // Clear render data
    clearStrings();
    m_quads.clear();

    auto dim = getScaledDimensions();

    // Get lap data
    const PluginData& pluginData = PluginData::getInstance();
    const SessionData& sessionData = pluginData.getSessionData();
    int displayRaceNum = pluginData.getDisplayRaceNum();

    int currentLap = 0;
    int totalLaps = sessionData.sessionNumLaps;

    // Get rider's current lap from standings
    if (displayRaceNum > 0) {
        const StandingsData* standing = pluginData.getStanding(displayRaceNum);
        if (standing) {
            currentLap = standing->numLaps + 1;  // numLaps is completed laps, so add 1 for current lap
        }
    }

    float startX = 0.0f;
    float startY = 0.0f;

    // BOX-MODEL: the plan owns padding, chrome, the title band and the content
    // origin. The fixed 12-char column (shared with Position/Time/Clock) is the
    // content width, so the four standard widgets keep tiling with each other.
    BaseHud::PanelWant want;
    want.contentW = PluginUtils::calculateMonospaceTextWidth(
        WidgetDimensions::STANDARD_WIDTH, dim.fontSize);
    want.sectionH = { bigValueRowHeight(dim) };  // Value (2 lines)
    want.captionW = planTitleWidth(dim, "Lap");
    PanelPlan& p = planPanel(dim, want);
    const float backgroundWidth = p.width();
    const float backgroundHeight = p.height();

    addPlanBackground(p, startX, startY);

    // Use full opacity for text
    unsigned long textColor = this->getColor(ColorSlot::PRIMARY);

    addPlanTitle(p, "Lap", this->getFont(FontCategory::TITLE), textColor);

    const float contentStartX = p.contentX();
    float currentY = p.contentY();

    // Determine if we should show total laps
    // Don't show total laps if:
    // - totalLaps is 0 (time-based session)
    // - sessionLength > 0 && totalLaps > 0 (time+laps session)
    int sessionLength = sessionData.sessionLength;
    bool showTotalLaps = (totalLaps > 0) && (sessionLength <= 0);

    // Build lap value string (e.g., "2/5" or "2" or "-")
    char lapValueBuffer[32];
    if (currentLap <= 0) {
        snprintf(lapValueBuffer, sizeof(lapValueBuffer), "%s", Placeholders::GENERIC);
    } else if (showTotalLaps) {
        snprintf(lapValueBuffer, sizeof(lapValueBuffer), "%d/%d", currentLap, totalLaps);
    } else {
        snprintf(lapValueBuffer, sizeof(lapValueBuffer), "%d", currentLap);
    }

    // Add lap value (extra large font - spans 2 lines)
    addString(lapValueBuffer, contentStartX, bigValueTextY(currentY, dim), Justify::LEFT,
        this->getFont(FontCategory::TITLE), textColor, dim.fontSizeExtraLarge);

    // Set bounds for drag detection
    setBounds(startX, startY, startX + backgroundWidth, startY + backgroundHeight);
}

void LapWidget::resetToDefaults() {
    m_bVisible = true;
    m_bShowTitle = true;
    setTextureVariant(0);  // No texture by default
    m_fBackgroundOpacity = 0.0f;
    m_fScale = 1.0f;
    setPosition(cellsX(18), cellsY(1));
    setDataDirty();
}
