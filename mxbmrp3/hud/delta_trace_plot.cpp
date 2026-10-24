// ============================================================================
// hud/delta_trace_plot.cpp
// The Delta Trace's chart - see delta_trace_plot.h.
// ============================================================================
#include "delta_trace_plot.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <vector>

#include "base_hud.h"
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
// A peak smaller than this is no peak: a lap a few ms ahead at its start and
// behind the rest of the way would otherwise mark a dot on the zero line.
constexpr float MIN_PEAK_MS = 20.0f;
}  // namespace

bool DeltaTracePlot::due(Reference reference, bool force) {
    const PluginData& pd = PluginData::getInstance();
    const int point = static_cast<int>(pd.getDisplayRiderTrackPos() * static_cast<float>(PbGapTracker::NUM_POINTS))
                      / LapDeltaProfile::STEP;
    const bool live = pd.hasValidLiveGap(reference);
    const unsigned lapStamp = pd.getPbGapTracker().lastLapStamp();
    if (!force && point == m_lastPoint && live == m_lastLive && lapStamp == m_lastLapStamp &&
        static_cast<int>(reference) == m_lastRef) return false;
    if ((live && !m_lastLive) || lapStamp != m_lastLapStamp ||
        static_cast<int>(reference) != m_lastRef) m_scalePeakMs = 0;
    m_lastRef = static_cast<int>(reference);
    m_lastPoint = point;
    m_lastLive = live;
    m_lastLapStamp = lapStamp;
    return true;
}

void DeltaTracePlot::formatScaleLabels() {
    // Ahead plots up, behind down: gaining reads as climbing. In seconds with
    // the unit, as the other strip charts label theirs, and trimmed the way the
    // Gap Bar's Range reads ("1s", "0.25s", "0.5s").
    const double secs = static_cast<double>(m_scaleMs) / 1000.0;
    const char* fmt = (m_scaleMs % 1000 == 0) ? "%c%.0fs" : (m_scaleMs % 100 == 0) ? "%c%.1fs" : "%c%.2fs";
    snprintf(m_scaleTop, sizeof(m_scaleTop), fmt, '-', secs);
    snprintf(m_scaleBottom, sizeof(m_scaleBottom), fmt, '+', secs);
}

void DeltaTracePlot::draw(BaseHud& host, Reference reference, float x, float y, float w, float h,
                          bool showPeaks, bool axisLabels, const ScaledDimensions& dims) {
    m_plotted = 0;
    m_plottedPrevious = 0;
    m_markerShown = false;
    m_peaksShown = 0;
    ++m_draws;

    const bool any = m_profile.sample(reference);
    m_scalePeakMs = std::max(m_scalePeakMs, m_profile.maxAbsMs);
    const int scale = PluginUtils::niceGapScaleMs(m_scalePeakMs);
    if (scale != m_scaleMs) {
        m_scaleMs = scale;
        formatScaleLabels();
    }

    // No middle label: the zero line is where the trace starts, so a label
    // there would sit under the line.
    host.addStripChartFrame(x, y, w, h, axisLabels ? m_scaleTop : "", "",
                            axisLabels ? m_scaleBottom : "", dims);
    addSplitLines(host, x, y, w, h);
    // Without a reference lap the chart simply stays empty.
    m_plotX = x;
    m_plotY = y;
    m_plotW = w;
    m_plotH = h;
    if (any) {
        addTrace(host, x, y, w, h);
        if (showPeaks) {
            findPeaks();
            addPeakMarkers(host, x, y, w, h, axisLabels, dims);
        }
        addRiderMarker(host, reference, x, y, w, h);
    }
}

bool DeltaTracePlot::moveMarker(BaseHud& host, Reference reference) {
    if (!m_markerShown || m_markerQuad >= host.m_quads.size()) return false;
    if (PluginData::getInstance().getDisplayRiderTrackPos() == m_markerPos) return false;
    const size_t at = m_markerQuad;
    const unsigned long color = host.m_quads[at].m_ulColor;
    addRiderMarker(host, reference, m_plotX, m_plotY, m_plotW, m_plotH);
    if (!m_markerShown) return false;   // addRiderMarker added nothing
    host.m_quads[at] = host.m_quads.back();
    host.m_quads[at].m_ulColor = color;
    host.m_quads.pop_back();
    m_markerQuad = at;
    return true;
}

// The lap the peaks belong to is the one the chart is about: the lap in
// progress while the gap is live (the previous lap ahead of the rider is only
// there to compare with), else the whole previous lap. Ahead is a negative
// gap, behind a positive one; a side the lap never reached has no peak, and
// neither has a lap without data (nor a side under MIN_PEAK_MS). Runs at a
// draw only, over the profile's 201 points; a label is formatted only when
// its value (or the time format) changed since it was last formatted.
void DeltaTracePlot::findPeaks() {
    const bool current = m_profile.live;
    const float* gapMs = current ? m_profile.gapMs : m_profile.prevGapMs;
    const bool* has = current ? m_profile.has : m_profile.prevHas;
    const int from = current ? 0 : std::max(0, m_profile.prevFrom);
    const int to = current ? m_profile.last : LapDeltaProfile::POINTS - 1;

    m_peakAhead.point = -1;
    m_peakBehind.point = -1;
    float lo = -MIN_PEAK_MS, hi = MIN_PEAK_MS;
    for (int p = from; p <= to; ++p) {
        if (!has[p]) continue;
        if (gapMs[p] < lo) { lo = gapMs[p]; m_peakAhead.point = p; }
        if (gapMs[p] > hi) { hi = gapMs[p]; m_peakBehind.point = p; }
    }
    m_peakAhead.gapMs = static_cast<int>(std::lround(lo));
    m_peakBehind.gapMs = static_cast<int>(std::lround(hi));

    const bool shortFormat = PluginData::getInstance().isShortTimeFormat();
    for (Peak* peak : { &m_peakAhead, &m_peakBehind }) {
        if (peak->point < 0) continue;
        if (peak->formatted && peak->formattedMs == peak->gapMs && peak->formattedShort == shortFormat) continue;
        PluginUtils::formatTimeDiff(peak->text, sizeof(peak->text), peak->gapMs);
        peak->textLen = static_cast<int>(strlen(peak->text));
        peak->formattedMs = peak->gapMs;
        peak->formattedShort = shortFormat;
        peak->formatted = true;
    }
}

// A dot on the line at each peak with the gap beside it, as the Charts HUD
// marks its points and tags its lines: the same square and small-font label,
// in the colour of its side of the trace (POSITIVE ahead, NEGATIVE behind -
// the fills', drawn opaque; the rider's own square is ACCENT). The label sits
// outside the line (above ahead, below behind) unless that leaves the chart,
// and is kept inside its edges - and, in the top or bottom row, clear of the
// axis label there.
void DeltaTracePlot::addPeakMarkers(BaseHud& host, float x, float y, float width, float height,
                                    bool axisLabels, const ScaledDimensions& dims) {
    const float half = height * 0.5f;
    const float scale = static_cast<float>(m_scaleMs);
    const float dotSize = host.stripChartLineThickness() * MARKER_SIZE;
    const float halfDot = dotSize * 0.5f;
    const int font = host.getFont(FontCategory::SMALL);
    // Where an axis label's row ends (addStripChartFrame draws it at the left
    // edge), plus a character of space; the chart's own edge without labels.
    const float labelInset = dims.paddingH * BaseHud::STRIP_CHART_LABEL_INSET;
    const float topLabelEnd = !axisLabels ? x : x + labelInset +
        PluginUtils::calculateMonospaceTextWidth(static_cast<int>(strlen(m_scaleTop)) + 1, dims.fontSizeSmall);
    const float bottomLabelEnd = !axisLabels ? x : x + labelInset +
        PluginUtils::calculateMonospaceTextWidth(static_cast<int>(strlen(m_scaleBottom)) + 1, dims.fontSizeSmall);
    struct Side { const Peak* peak; ColorSlot slot; bool above; };
    const Side sides[] = { { &m_peakAhead, ColorSlot::POSITIVE, true },
                           { &m_peakBehind, ColorSlot::NEGATIVE, false } };
    for (const Side& side : sides) {
        const Peak& peak = *side.peak;
        if (peak.point < 0) continue;
        const unsigned long color = host.getColor(side.slot);
        const float cx = x + width * LapDeltaProfile::pointPos(peak.point);
        const float cy = y + half + std::clamp(static_cast<float>(peak.gapMs) / scale, -1.0f, 1.0f) * half;

        host.addDot(cx, cy, color, dotSize);
        ++m_peaksShown;

        const float textW = PluginUtils::calculateMonospaceTextWidth(peak.textLen, dims.fontSizeSmall);
        const float aboveY = cy - halfDot - dims.lineHeightSmall;
        const float belowY = cy + halfDot;
        float textY = side.above ? aboveY : belowY;
        if (side.above && textY < y) textY = belowY;
        if (!side.above && textY + dims.lineHeightSmall > y + height) textY = aboveY;
        float left = x;
        if (textY < y + dims.lineHeightSmall) left = topLabelEnd;
        else if (textY + dims.lineHeightSmall > y + height - dims.lineHeightSmall) left = bottomLabelEnd;
        const float textX = std::clamp(cx, left + textW * 0.5f, std::max(left + textW * 0.5f, x + width - textW * 0.5f));
        host.addString(peak.text, textX, textY, Justify::CENTER, font, color, dims.fontSizeSmall);
    }
}

// Where the rider is now: a square on the live gap at their track position,
// so the point being drawn reads at a glance (and stands apart from the lap
// already drawn when they have ridden back). ACCENT, the rider's own colour on
// the Map and the Gap Bar.
void DeltaTracePlot::addRiderMarker(BaseHud& host, Reference reference, float x, float y, float width, float height) {
    const PluginData& pd = PluginData::getInstance();
    m_markerShown = false;
    if (!m_profile.live) return;
    m_markerPos = pd.getDisplayRiderTrackPos();
    const float pos = std::clamp(m_markerPos, 0.0f, 1.0f);
    const float gap = static_cast<float>(pd.getLiveGap(reference));
    const float half = height * 0.5f;
    const float cy = y + half + std::clamp(gap / static_cast<float>(m_scaleMs), -1.0f, 1.0f) * half;
    m_markerQuad = host.m_quads.size();
    host.addDot(x + width * pos, cy, host.getColor(ColorSlot::ACCENT), host.stripChartLineThickness() * MARKER_SIZE);
    m_markerShown = true;
}

// A full-height line at each split, from the track's own marker data (the Gap
// Bar's ticks read the same), drawn as the chart frame draws its grid lines.
// None where the game sends no markers.
void DeltaTracePlot::addSplitLines(BaseHud& host, float x, float y, float width, float height) {
    const std::vector<float>& splits = PluginData::getInstance().getSplitPositions();
    const unsigned long color = host.getColor(ColorSlot::MUTED);
    const float thickness = host.stripChartGridThickness();
    // Entry 0 is the start/finish line: the chart's own left and right edges.
    for (size_t i = 1; i < splits.size(); ++i) {
        if (splits[i] <= 0.001f || splits[i] >= 0.999f) continue;
        const float sx = x + width * splits[i];
        host.addLineSegment(sx, y, sx, y + height, color, thickness);
    }
}

// Both laps: the previous one first (fainter while the new one is live, and
// only past the rider and the overwrite gap), then the lap in progress.
void DeltaTracePlot::addTrace(BaseHud& host, float x, float y, float width, float height) {
    m_plotted = 0;
    m_plottedPrevious = 0;
    if (m_profile.anyPrev) {
        addSeries(host, m_profile.prevGapMs, m_profile.prevHas, m_profile.prevFrom, LapDeltaProfile::POINTS - 1,
                  m_profile.live ? PREVIOUS_OPACITY : 1.0f, x, y, width, height);
    }
    const int previous = m_plotted;
    addSeries(host, m_profile.gapMs, m_profile.has, 0, m_profile.last, 1.0f, x, y, width, height);
    // The test hooks count the lap in progress while it is live, else what shows.
    if (m_profile.live) {
        m_plotted -= previous;
        m_plottedPrevious = previous;
    }
}

// A fill between the line and zero (green where ahead, red where behind; a
// segment that crosses zero is split at the crossing), then the line on top.
// Each piece is one of the game's own four-cornered quads.
void DeltaTracePlot::addSeries(BaseHud& host, const float* gapMs, const bool* has, int from, int to, float opacity,
                               float x, float y, float width, float height) {
    const float midY = y + height * 0.5f;
    const float half = height * 0.5f;
    const float scale = static_cast<float>(m_scaleMs);
    const unsigned long ahead = PluginUtils::applyOpacity(host.getColor(ColorSlot::POSITIVE), FILL_OPACITY * opacity);
    const unsigned long behind = PluginUtils::applyOpacity(host.getColor(ColorSlot::NEGATIVE), FILL_OPACITY * opacity);
    // addLineSegment draws opaque, so a faded lap's line steps down a colour
    // slot instead of taking the fills' opacity.
    const unsigned long lineColor = host.getColor(opacity < 1.0f ? ColorSlot::MUTED : ColorSlot::PRIMARY);
    const float lineThickness = host.stripChartLineThickness();

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
            host.applyOffset(pts[i][0], pts[i][1]);
            q.m_aafPos[i][0] = pts[i][0];
            q.m_aafPos[i][1] = pts[i][1];
        }
        q.m_iSprite = SpriteIndex::SOLID_COLOR;
        q.m_ulColor = (g0 + g1) > 0.0f ? behind : ahead;
        host.m_quads.push_back(q);
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
            host.addLineSegment(px(prev), py(gapMs[prev]), px(p), py(gapMs[p]), lineColor, lineThickness);
        }
        prev = p;
    }
}
