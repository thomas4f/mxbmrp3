// ============================================================================
// hud/delta_trace_hud.cpp
// Delta Trace - see delta_trace_hud.h.
// ============================================================================
#include "delta_trace_hud.h"

#include <algorithm>
#include <cstdio>
#include <vector>

#include "../core/plugin_constants.h"
#include "../core/plugin_data.h"
#include "../core/plugin_utils.h"
#include "../core/color_config.h"

using namespace PluginConstants;

namespace {
// How strongly the area between the line and zero is filled.
constexpr float FILL_OPACITY = 0.45f;
// The previous lap, ahead of the rider, while the new one draws over it.
constexpr float PREVIOUS_OPACITY = 0.4f;
// The rider's marker, in line thicknesses.
constexpr float MARKER_SIZE = 2.0f;
}  // namespace

DeltaTraceHud::DeltaTraceHud() {
    setDraggable(true);
    m_bContentCard = true;
    // 1 bg + 3 grid + split lines + the rider's marker + up to 2 fill quads and 1 line segment per
    // profile point (the two laps share the points between them)
    m_quads.reserve(8 + Unified::MAX_SPLITS + LapDeltaProfile::POINTS * 3);
    m_strings.reserve(4);    // title + 3 axis labels
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
    // Track position has no change notification of its own: rebuild when the
    // rider reaches the next profile point, the gap's validity flips or a lap
    // is committed - a few times a second, not every frame.
    const PluginData& pd = PluginData::getInstance();
    const int point = static_cast<int>(pd.getDisplayRiderTrackPos() * static_cast<float>(PbGapTracker::NUM_POINTS))
                      / LapDeltaProfile::STEP;
    const Reference reference = getReference();
    const bool live = pd.hasValidLiveGap(reference);
    const unsigned lapStamp = pd.getPbGapTracker().lastLapStamp();
    if (isDataDirty() || isLayoutDirty() || point != m_lastPoint || live != m_lastLive ||
        lapStamp != m_lastLapStamp || static_cast<int>(reference) != m_lastRef) {
        // A new lap (committed, or live again after the pits) or another
        // reference refits the scale.
        if ((live && !m_lastLive) || lapStamp != m_lastLapStamp ||
            static_cast<int>(reference) != m_lastRef) m_scalePeakMs = 0;
        m_lastRef = static_cast<int>(reference);
        m_lastPoint = point;
        m_lastLive = live;
        m_lastLapStamp = lapStamp;
        rebuildAndRecord();
    } else if (m_markerShown && pd.getDisplayRiderTrackPos() != m_markerPos) {
        // Between profile points only the rider's marker moves: it is the last
        // quad, so replace it in place and leave the trace alone. The marker
        // keeps the pace of the position updates, like the other graphs keep
        // the pace of their data.
        m_quads.pop_back();
        addRiderMarker(m_plotX, m_plotY, m_plotW, m_plotH);
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
    setPosition(cellsX(133), cellsY(35));  // right-column tower, between Rumble and Telemetry
    setDataDirty();
}

void DeltaTraceHud::formatScaleLabels() {
    // Ahead plots up, behind down: gaining reads as climbing. In seconds with
    // the unit, as the other strip charts label theirs, and trimmed the way the
    // Gap Bar's Range reads ("1s", "0.25s", "0.5s").
    const double secs = static_cast<double>(m_scaleMs) / 1000.0;
    const char* fmt = (m_scaleMs % 1000 == 0) ? "%c%.0fs" : (m_scaleMs % 100 == 0) ? "%c%.1fs" : "%c%.2fs";
    snprintf(m_scaleTop, sizeof(m_scaleTop), fmt, '-', secs);
    snprintf(m_scaleBottom, sizeof(m_scaleBottom), fmt, '+', secs);
}

void DeltaTraceHud::rebuildRenderData() {
    m_quads.clear();
    clearStrings();
    m_plotted = 0;
    m_plottedPrevious = 0;
    m_markerShown = false;
    ++m_rebuilds;

    const auto dims = getScaledDimensions();
    const bool any = m_profile.sample(getReference());
    m_scalePeakMs = std::max(m_scalePeakMs, m_profile.maxAbsMs);
    const int scale = PluginUtils::niceGapScaleMs(m_scalePeakMs);
    if (scale != m_scaleMs) {
        m_scaleMs = scale;
        formatScaleLabels();
    }

    const float graphHeight = static_cast<float>(m_graphRows) * dims.lineHeightNormal;

    PanelWant want;
    want.contentW = PluginUtils::calculateMonospaceTextWidth(GRAPH_WIDTH_CHARS, dims.fontSize);
    want.sectionH = { graphHeight };
    want.captionW = planTitleWidth(dims, "Delta Trace", TitleTier::Large);
    want.tier = TitleTier::Large;
    PanelPlan& plan = planPanel(dims, want);
    setBounds(START_X, START_Y, START_X + plan.width(), START_Y + plan.height());
    addPlanBackground(plan, START_X, START_Y);
    addPlanTitle(plan, "Delta Trace", this->getColor(ColorSlot::PRIMARY));

    const float contentX = plan.contentX();
    const float contentY = plan.contentY();
    const float graphWidth = plan.contentRight() - contentX;

    // No middle label: the zero line is where the trace starts, so a label
    // there would sit under the line.
    addStripChartFrame(contentX, contentY, graphWidth, graphHeight, m_scaleTop, "", m_scaleBottom, dims);
    addSplitLines(contentX, contentY, graphWidth, graphHeight);
    // Without a reference lap the chart simply stays empty.
    m_plotX = contentX;
    m_plotY = contentY;
    m_plotW = graphWidth;
    m_plotH = graphHeight;
    if (any) {
        addTrace(contentX, contentY, graphWidth, graphHeight);
        addRiderMarker(contentX, contentY, graphWidth, graphHeight);
    }
}

// Where the rider is now: a square on the live gap at their track position,
// so the point being drawn reads at a glance (and stands apart from the lap
// already drawn when they have ridden back). ACCENT, the rider's own colour on
// the Map and the Gap Bar.
void DeltaTraceHud::addRiderMarker(float x, float y, float width, float height) {
    const PluginData& pd = PluginData::getInstance();
    m_markerShown = false;
    if (!m_profile.live) return;
    m_markerPos = pd.getDisplayRiderTrackPos();
    const float pos = std::clamp(m_markerPos, 0.0f, 1.0f);
    const float gap = static_cast<float>(pd.getLiveGap(getReference()));
    const float half = height * 0.5f;
    const float cy = y + half + std::clamp(gap / static_cast<float>(m_scaleMs), -1.0f, 1.0f) * half;
    addDot(x + width * pos, cy, this->getColor(ColorSlot::ACCENT), stripChartLineThickness() * MARKER_SIZE);
    m_markerShown = true;
}

// A full-height line at each split, from the track's own marker data (the Gap
// Bar's ticks read the same), drawn as the chart frame draws its grid lines.
// None where the game sends no markers.
void DeltaTraceHud::addSplitLines(float x, float y, float width, float height) {
    const std::vector<float>& splits = PluginData::getInstance().getSplitPositions();
    const unsigned long color = this->getColor(ColorSlot::MUTED);
    const float thickness = stripChartGridThickness();
    // Entry 0 is the start/finish line: the chart's own left and right edges.
    for (size_t i = 1; i < splits.size(); ++i) {
        if (splits[i] <= 0.001f || splits[i] >= 0.999f) continue;
        const float sx = x + width * splits[i];
        addLineSegment(sx, y, sx, y + height, color, thickness);
    }
}

// Both laps: the previous one first (fainter while the new one is live, and
// only past the rider and the overwrite gap), then the lap in progress.
void DeltaTraceHud::addTrace(float x, float y, float width, float height) {
    m_plotted = 0;
    m_plottedPrevious = 0;
    if (m_profile.anyPrev) {
        addSeries(m_profile.prevGapMs, m_profile.prevHas, m_profile.prevFrom, LapDeltaProfile::POINTS - 1,
                  m_profile.live ? PREVIOUS_OPACITY : 1.0f, x, y, width, height);
    }
    const int previous = m_plotted;
    addSeries(m_profile.gapMs, m_profile.has, 0, m_profile.last, 1.0f, x, y, width, height);
    // The test hooks count the lap in progress while it is live, else what shows.
    if (m_profile.live) {
        m_plotted -= previous;
        m_plottedPrevious = previous;
    }
}

// A fill between the line and zero (green where ahead, red where behind; a
// segment that crosses zero is split at the crossing), then the line on top.
// Each piece is one of the game's own four-cornered quads.
void DeltaTraceHud::addSeries(const float* gapMs, const bool* has, int from, int to, float opacity,
                              float x, float y, float width, float height) {
    const float midY = y + height * 0.5f;
    const float half = height * 0.5f;
    const float scale = static_cast<float>(m_scaleMs);
    const unsigned long ahead = PluginUtils::applyOpacity(this->getColor(ColorSlot::POSITIVE), FILL_OPACITY * opacity);
    const unsigned long behind = PluginUtils::applyOpacity(this->getColor(ColorSlot::NEGATIVE), FILL_OPACITY * opacity);
    // addLineSegment draws opaque, so a faded lap's line steps down a colour
    // slot instead of taking the fills' opacity.
    const unsigned long lineColor = this->getColor(opacity < 1.0f ? ColorSlot::MUTED : ColorSlot::PRIMARY);
    const float lineThickness = stripChartLineThickness();

    auto px = [&](int p) { return x + width * LapDeltaProfile::pointPos(p); };
    auto py = [&](float gap) { return midY + std::clamp(gap / scale, -1.0f, 1.0f) * half; };
    auto fill = [&](float x0, float g0, float x1, float g1) {
        // Top-left, bottom-left, bottom-right, top-right, whichever side of zero
        // the piece is on: the game culls a quad wound the other way, so a fill
        // below the line (behind) built line-first vanished in game while the
        // companion window, which does not cull, still drew it.
        const float a0 = py(g0), a1 = py(g1);
        SPluginQuad_t q;
        float pts[4][2] = { { x0, std::min(a0, midY) }, { x0, std::max(a0, midY) },
                            { x1, std::max(a1, midY) }, { x1, std::min(a1, midY) } };
        for (int i = 0; i < 4; ++i) {
            applyOffset(pts[i][0], pts[i][1]);
            q.m_aafPos[i][0] = pts[i][0];
            q.m_aafPos[i][1] = pts[i][1];
        }
        q.m_iSprite = SpriteIndex::SOLID_COLOR;
        q.m_ulColor = (g0 + g1) > 0.0f ? behind : ahead;
        m_quads.push_back(q);
    };

    from = std::max(0, from);
    to = std::min(LapDeltaProfile::POINTS - 1, to);
    int prev = -1;
    // Fills first, so the line draws over them.
    for (int p = from; p <= to; ++p) {
        if (!has[p]) continue;
        ++m_plotted;
        // A gap of more than a few points (missing samples) is left open.
        if (prev >= 0 && p - prev <= 3) {
            const float x0 = px(prev), x1 = px(p);
            const float g0 = gapMs[prev], g1 = gapMs[p];
            if ((g0 > 0.0f) != (g1 > 0.0f) && g0 != g1) {
                const float xm = x0 + (x1 - x0) * (g0 / (g0 - g1));
                fill(x0, g0, xm, 0.0f);
                fill(xm, 0.0f, x1, g1);
            } else {
                fill(x0, g0, x1, g1);
            }
        }
        prev = p;
    }
    prev = -1;
    for (int p = from; p <= to; ++p) {
        if (!has[p]) continue;
        if (prev >= 0 && p - prev <= 3) {
            addLineSegment(px(prev), py(gapMs[prev]), px(p), py(gapMs[p]), lineColor, lineThickness);
        }
        prev = p;
    }
}
