// ============================================================================
// hud/position_widget.cpp
// Position widget - displays rider position in minimal format (e.g., "1/24")
// ============================================================================
#include "position_widget.h"
#include "../diagnostics/logger.h"
#include "../diagnostics/timer.h"
#include "../core/plugin_utils.h"
#include "../core/color_config.h"
#include <cstdio>
#include <algorithm>

using namespace PluginConstants;

PositionWidget::PositionWidget() {
    m_panelKind = PanelKind::Widget;
    m_bContentCard = true;
    // One-time setup
    DEBUG_INFO("PositionWidget created");
    setDraggable(true);
    m_strings.reserve(2);  // label (optional), position value

    // Set texture base name for dynamic texture discovery
    setTextureBaseName("position_widget");

    // Set all configurable defaults
    resetToDefaults();

    rebuildRenderData();
}

bool PositionWidget::handlesDataType(DataChangeType dataType) const {
    // RaceEntries as well as Standings: this widget shows "P3 / 22", and the
    // denominator changes when a rider joins or leaves, which fires RaceEntries and
    // not Standings. Subscribing to both is what let the per-frame poll below go --
    // see update().
    return dataType == DataChangeType::Standings ||
           dataType == DataChangeType::RaceEntries ||
           dataType == DataChangeType::SpectateTarget;
}

void PositionWidget::update() {
    // OPTIMIZATION: Skip processing when not visible
    if (!isVisibleAnySurface()) {
        clearDataDirty();
        clearLayoutDirty();
        return;
    }

    // NO PER-FRAME POLL. This recomputed the position and the entry count EVERY
    // FRAME just to notice a change the dirty flag already reports -- and it was the
    // most expensive widget in the plugin for it, at 1.41us/frame across a stint
    // with ZERO rebuilds, ten times its peers.
    //
    // The cost was not the comparison, it was what the lookup drags behind it:
    // getDisplayPositionForRaceNum() serves a LAZILY REBUILT cache, and this was the
    // only per-frame caller, so it was the one paying to rebuild it -- clear plus a
    // fresh unordered_map node, i.e. a heap round-trip (1.5-3.4us in the game
    // process, see small_vec.h) on each of the ~1200 frames a Classification landed.
    // Every other reader takes that cache during its own gated rebuild and finds it
    // warm.
    //
    // The poll was redundant regardless: position changes with the classification
    // (Standings) and the entry count with a join or leave (RaceEntries), and this
    // widget now subscribes to both, plus SpectateTarget for whose position is
    // shown. There is no path that moves this readout without one of the three.
    if (isDataDirty()) {
        rebuildAndRecord();
        clearDataDirty();
        clearLayoutDirty();
    }
    else if (isLayoutDirty()) {
        rebuildLayout();
        clearLayoutDirty();
    }
}

int PositionWidget::calculatePlayerPosition() const {
    const PluginData& pluginData = PluginData::getInstance();
    int displayRaceNum = pluginData.getDisplayRaceNum();

    if (displayRaceNum <= 0) {
        return -1;  // No rider to display
    }

    // Use centralized position cache (O(1) lookup instead of O(n) linear search)
    return pluginData.getDisplayPositionForRaceNum(displayRaceNum);
}

void PositionWidget::rebuildLayout() {
    // BOX-MODEL: one source of geometry — the fast path duplicated the sizing
    // arithmetic, and a handful of strings is cheaper to rebuild than the drift.
    rebuildRenderData();
}

void PositionWidget::rebuildRenderData() {
    // Clear render data
    clearStrings();
    m_quads.clear();

    auto dim = getScaledDimensions();

    // Get position data
    int position = calculatePlayerPosition();
    const PluginData& pluginData = PluginData::getInstance();
    int totalEntries = static_cast<int>(pluginData.getDisplayClassificationOrder().size());

    float startX = 0.0f;
    float startY = 0.0f;

    // BOX-MODEL: the plan owns padding, chrome, the title band and the content
    // origin. The fixed 12-char column (shared with Lap/Time/Clock) is the
    // content width, so the four standard widgets keep tiling with each other.
    BaseHud::PanelWant want;
    want.contentW = PluginUtils::calculateMonospaceTextWidth(
        WidgetDimensions::STANDARD_WIDTH, dim.fontSize);
    want.sectionH = { bigValueRowHeight(dim) };  // Value (2 lines)
    want.captionW = planTitleWidth(dim, "Position");
    PanelPlan& p = planPanel(dim, want);
    const float backgroundWidth = p.width();
    const float backgroundHeight = p.height();

    addPlanBackground(p, startX, startY);

    // Use full opacity for text
    unsigned long textColor = this->getColor(ColorSlot::PRIMARY);

    addPlanTitle(p, "Position", this->getFont(FontCategory::TITLE), textColor);

    const float contentStartX = p.contentX();
    float currentY = p.contentY();

    // Build position value string (e.g., "1/24" or "-")
    char positionValueBuffer[32];
    if (position <= 0 || totalEntries <= 0) {
        snprintf(positionValueBuffer, sizeof(positionValueBuffer), "%s", Placeholders::GENERIC);
    } else {
        snprintf(positionValueBuffer, sizeof(positionValueBuffer), "%d/%d", position, totalEntries);
    }

    // Add position value (extra large font - spans 2 lines)
    addString(positionValueBuffer, contentStartX, bigValueTextY(currentY, dim), Justify::LEFT,
        this->getFont(FontCategory::TITLE), textColor, dim.fontSizeExtraLarge);

    // Set bounds for drag detection
    setBounds(startX, startY, startX + backgroundWidth, startY + backgroundHeight);
}

void PositionWidget::resetToDefaults() {
    m_bVisible = true;
    m_bShowTitle = true;
    setTextureVariant(0);  // No texture by default
    m_fBackgroundOpacity = 0.0f;
    m_fScale = 1.0f;
    setPosition(cellsX(1), cellsY(1));
    setDataDirty();
}
