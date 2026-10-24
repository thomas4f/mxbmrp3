// ============================================================================
// hud/delta_trace_hud.cpp
// Delta Trace - see delta_trace_hud.h.
// ============================================================================
#include "delta_trace_hud.h"
#include "default_slots.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <vector>

#include "../core/plugin_constants.h"
#include "../core/plugin_data.h"
#include "../core/plugin_utils.h"
#include "../core/color_config.h"

using namespace PluginConstants;

DeltaTraceHud::DeltaTraceHud() {
    setDraggable(true);
    m_bContentCard = true;
    // 1 bg + 3 grid + split lines + the rider's marker + 2 peaks + up to 2 fill quads and 1 line
    // segment per profile point (the two laps share the points between them)
    m_quads.reserve(10 + Unified::MAX_SPLITS + LapDeltaProfile::POINTS * 3);
    m_strings.reserve(6);    // title + 3 axis labels + 2 peak labels
    setTextureBaseName("delta_trace_hud");
    resetToDefaults();
    rebuildRenderData();
}

bool DeltaTraceHud::handlesDataType(DataChangeType dataType) const {
    return dataType == DataChangeType::SpectateTarget ||
           dataType == DataChangeType::SessionData ||
           dataType == DataChangeType::LapLog;
}

void DeltaTraceHud::update() {
    if (!isVisibleAnySurface()) {
        clearDataDirty();
        clearLayoutDirty();
        return;
    }
    // A rebuild when the rider reaches the next profile point, the gap's
    // validity flips or a lap is committed (DeltaTracePlot::due) - a few times a
    // second, not every frame. Between them only the rider's marker moves, in
    // place, keeping the pace of the position updates like the other graphs
    // keep the pace of their data.
    if (m_plot.due(getReference(), isDataDirty() || isLayoutDirty())) {
        rebuildAndRecord();
    } else {
        m_plot.moveMarker(*this, getReference());
    }
    clearDataDirty();
    clearLayoutDirty();
}

void DeltaTraceHud::resetToDefaults() {
    m_bVisible = false;  // Off by default
    m_bShowTitle = true;
    setTextureVariant(0);
    m_fBackgroundOpacity = SettingsLimits::DEFAULT_OPACITY;
    setScale(1.0f);
    m_reference = Reference::SESSION_PB;
    m_referenceDefault = true;
    m_graphRows = DEFAULT_GRAPH_ROWS;
    m_showPeaks = true;
    setPosition(DefaultSlots::rightColumnX(DefaultSlots::WIDE_CELLS), DefaultSlots::rightColumnY());
    setDataDirty();
}

void DeltaTraceHud::rebuildRenderData() {
    m_quads.clear();
    clearStrings();

    const auto dims = getScaledDimensions();
    const float graphHeight = static_cast<float>(m_graphRows) * dims.lineHeightNormal;

    PanelWant want;
    want.contentW = PluginUtils::calculateMonospaceTextWidth(GRAPH_WIDTH_CHARS, dims.fontSize);
    want.sectionH = { graphHeight };
    // The reference lap, named on the title row.
    const char* refNote = PbGapTracker::refName(getReference());
    want.captionW = planTitleWidth(dims, "Delta Trace", TitleTier::Large) + planTitleNoteWidth(dims, refNote);
    want.tier = TitleTier::Large;
    PanelPlan& plan = planPanel(dims, want);
    setBounds(START_X, START_Y, START_X + plan.width(), START_Y + plan.height());
    addPlanBackground(plan, START_X, START_Y);
    addPlanTitle(plan, "Delta Trace", this->getColor(ColorSlot::PRIMARY));

    const float contentX = plan.contentX();
    m_plot.draw(*this, getReference(), contentX, plan.contentY(), plan.contentRight() - contentX, graphHeight,
                m_showPeaks, /*axisLabels=*/true, dims);
    addPlanTitleNote(plan, dims, refNote);
}
