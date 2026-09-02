// ============================================================================
// hud/performance_hud.cpp
// Displays performance metrics including FPS and render timing diagnostics
// ============================================================================
#include "performance_hud.h"
#include "../diagnostics/logger.h"
#include "../core/plugin_utils.h"
#include "../core/plugin_constants.h"
#include "../core/plugin_data.h"
#include "../core/plugin_thread.h"
#include "../core/color_config.h"
#include <cstring>
#include <cstdio>
#include <cmath>

using namespace PluginConstants;

PerformanceHud::PerformanceHud() : m_historyIndex(0), m_fpsMin(0.0f), m_fpsMax(0.0f), m_fpsAvg(0.0f),
    m_pluginTimeMsMin(0.0f), m_pluginTimeMsMax(0.0f), m_pluginTimeMsAvg(0.0f),
    m_fpsSum(0.0f), m_pluginTimeSum(0.0f), m_validFpsCount(0), m_validPluginTimeCount(0),
    m_fpsMinIndex(-1), m_fpsMaxIndex(-1), m_pluginMinIndex(-1), m_pluginMaxIndex(-1)
{
    // One-time setup
    DEBUG_INFO("PerformanceHud created");
    setDraggable(true);
    // Body cards, one PER SECTION: Frame Rate and Plugin Time are two separate graphs,
    // and one card round both of them says they are one thing. See
    // BaseHud::m_bContentSections.
    m_bContentCard = true;
    m_bContentSections = true;

    // Initialize history arrays
    m_fpsHistory.fill(0.0f);
    m_pluginTimeHistory.fill(0.0f);
    m_pluginTimePercentHistory.fill(0.0f);

    // Pre-allocate vectors (background + 2 line graphs with grid lines). Sized from
    // the footprint the benchmark report actually measures for this panel with both
    // sections on -- 286 quads / 25 strings -- not from a guess: an undersized
    // reserve is one growth reallocation on the first rebuild, and this panel is the
    // one that draws the graph telling you about it.
    m_quads.reserve(300);
    m_strings.reserve(32);

    // Set texture base name for dynamic texture discovery
    setTextureBaseName("performance_hud");

    // Set all configurable defaults
    resetToDefaults();

    rebuildRenderData();
}

bool PerformanceHud::handlesDataType(DataChangeType dataType) const {
    // PerformanceHud handles debug metrics updates
    return (dataType == DataChangeType::DebugMetrics);
}

// Clear the graph history when this HUD first becomes visible on ANY surface.
//
// Was keyed off setVisible() -- the GAME toggle -- so enabling the HUD only on the
// companion window showed whatever stale samples the buffers still held instead of
// a fresh graph. setCompanionVisible() is not virtual and never reached the
// override. Same defect as the benchmark profiler's collection switch
// (benchmark_companion_test.cpp), milder because it costs correctness of the first
// screenful rather than all the data.
//
// Per frame rather than in the setters: any-surface visibility also flips when the
// companion window opens or closes, and no setter runs then. update() is called
// unconditionally, so every transition is seen.
void PerformanceHud::syncVisibilityEdge() {
    const bool visibleNow = isVisibleAnySurface();
    if (visibleNow == m_bWasVisibleAnySurface) return;
    m_bWasVisibleAnySurface = visibleNow;

    if (visibleNow) {
        m_fpsHistory.fill(0.0f);
        m_pluginTimeHistory.fill(0.0f);
        m_pluginTimePercentHistory.fill(0.0f);
        m_historyIndex = 0;
        m_fpsMin = 0.0f;
        m_fpsMax = 0.0f;
        m_fpsAvg = 0.0f;
        m_pluginTimeMsMin = 0.0f;
        m_pluginTimeMsMax = 0.0f;
        m_pluginTimeMsAvg = 0.0f;
        m_fpsSum = 0.0f;
        m_pluginTimeSum = 0.0f;
        m_validFpsCount = 0;
        m_validPluginTimeCount = 0;
        m_fpsMinIndex = -1;
        m_fpsMaxIndex = -1;
        m_pluginMinIndex = -1;
        m_pluginMaxIndex = -1;
    }
}

void PerformanceHud::update() {
    // Before the early-out: the show edge has to be seen to clear the history.
    syncVisibilityEdge();

    // OPTIMIZATION: Skip expensive graph rebuild when not visible
    // History is cleared by syncVisibilityEdge() on the show edge, so the graph
    // starts fresh.
    if (!isVisibleAnySurface()) {
        clearDataDirty();
        clearLayoutDirty();
        return;
    }

    // Always rebuild - external notification system marks this dirty every frame
    // No need for conditional checks since updateDebugMetrics() is called every draw
    rebuildAndRecord();
    clearDataDirty();
    clearLayoutDirty();
}

void PerformanceHud::rebuildRenderData() {
    clearStrings();
    m_quads.clear();

    // Get debug metrics from PluginData
    const auto& metrics = PluginData::getInstance().getDebugMetrics();

    // PERFORMANCE OPTIMIZATION: incremental statistics instead of rescanning 120 samples every frame
    // O(1) best case, O(120) only when the min/max value is replaced

    // Get old values at current index (about to be overwritten)
    float oldFps = m_fpsHistory[m_historyIndex];
    float oldPluginTime = m_pluginTimeHistory[m_historyIndex];

    // Update running sums (for average calculation)
    if (oldFps > 0) {
        m_fpsSum -= oldFps;
        m_validFpsCount--;
    }
    if (oldPluginTime > 0) {
        m_pluginTimeSum -= oldPluginTime;
        m_validPluginTimeCount--;
    }

    // Store new values in history
    m_fpsHistory[m_historyIndex] = metrics.currentFps;
    m_pluginTimeHistory[m_historyIndex] = metrics.pluginTimeMs;

    // Update running sums with new values
    if (metrics.currentFps > 0) {
        m_fpsSum += metrics.currentFps;
        m_validFpsCount++;
    }
    if (metrics.pluginTimeMs > 0) {
        m_pluginTimeSum += metrics.pluginTimeMs;
        m_validPluginTimeCount++;
    }

    // Update average (O(1))
    m_fpsAvg = (m_validFpsCount > 0) ? (m_fpsSum / m_validFpsCount) : 0.0f;
    m_pluginTimeMsAvg = (m_validPluginTimeCount > 0) ? (m_pluginTimeSum / m_validPluginTimeCount) : 0.0f;

    // Update min/max incrementally
    // Check if we're replacing the current min or max value - if so, need to rescan
    bool needFpsMinMaxRecalc = (m_historyIndex == m_fpsMinIndex || m_historyIndex == m_fpsMaxIndex);
    bool needPluginMinMaxRecalc = (m_historyIndex == m_pluginMinIndex || m_historyIndex == m_pluginMaxIndex);

    // Check if new value is new min or max
    if (metrics.currentFps > 0) {
        if (m_fpsMinIndex == -1 || metrics.currentFps < m_fpsHistory[m_fpsMinIndex]) {
            m_fpsMin = metrics.currentFps;
            m_fpsMinIndex = m_historyIndex;
        }
        if (m_fpsMaxIndex == -1 || metrics.currentFps > m_fpsHistory[m_fpsMaxIndex]) {
            m_fpsMax = metrics.currentFps;
            m_fpsMaxIndex = m_historyIndex;
        }
    }

    if (metrics.pluginTimeMs > 0) {
        if (m_pluginMinIndex == -1 || metrics.pluginTimeMs < m_pluginTimeHistory[m_pluginMinIndex]) {
            m_pluginTimeMsMin = metrics.pluginTimeMs;
            m_pluginMinIndex = m_historyIndex;
        }
        if (m_pluginMaxIndex == -1 || metrics.pluginTimeMs > m_pluginTimeHistory[m_pluginMaxIndex]) {
            m_pluginTimeMsMax = metrics.pluginTimeMs;
            m_pluginMaxIndex = m_historyIndex;
        }
    }

    // Rescan only if we replaced the current min/max value
    if (needFpsMinMaxRecalc) {
        recalculateFpsMinMax();
    }
    if (needPluginMinMaxRecalc) {
        recalculatePluginTimeMinMax();
    }

    // Advance to next index
    m_historyIndex = (m_historyIndex + 1) % GRAPH_HISTORY_SIZE;

    // Apply scale to all dimensions
    auto dims = getScaledDimensions();

    // Calculate dimensions (the plan owns the panel box below)
    int widthChars = getBackgroundWidthChars();
    float graphHeight = static_cast<float>(m_graphRows) * dims.lineHeightNormal;

    // Determine if we show graphs and/or values based on display mode
    bool showGraphs = (m_displayMode == DISPLAY_GRAPHS || m_displayMode == DISPLAY_BOTH);
    bool showValues = (m_displayMode == DISPLAY_VALUES || m_displayMode == DISPLAY_BOTH);

    // Calculate per-section content heights
    // Each section's height = max(graph height, legend height for that section)
    // When both FPS and plugin time are enabled, sections stack vertically with a gap
    bool hasFps = (m_enabledElements & ELEM_FPS) != 0;
    bool hasCpu = (m_enabledElements & ELEM_CPU) != 0;

    float fpsSectionH = 0.0f;
    if (hasFps) {
        float fpsLegendH = showValues ? (4 * dims.lineHeightNormal) : 0.0f;
        fpsSectionH = showGraphs ? (std::max)(graphHeight, fpsLegendH) : fpsLegendH;
    }

    float cpuSectionH = 0.0f;
    if (hasCpu) {
        float cpuLegendH = showValues ? (4 * dims.lineHeightNormal) : 0.0f;
        cpuSectionH = showGraphs ? (std::max)(graphHeight, cpuLegendH) : cpuLegendH;
    }

    // BOX-MODEL: one sibling section card per enabled block (heading + chart),
    // the seam between them the sum of the facing [content] margins — the
    // engine's rule, replacing the beginContentSection/sectionGapY dance.
    float subHeadH = dims.lineHeightNormal;
    const char* title = PluginThread::getInstance().enabled()
        ? "Performance (threaded)" : "Performance";
    BaseHud::PanelWant want;
    want.contentW = PluginUtils::calculateMonospaceTextWidth(widthChars, dims.fontSize);
    if (hasFps) want.sectionH.push_back(subHeadH + fpsSectionH);
    if (hasCpu) want.sectionH.push_back(subHeadH + cpuSectionH);
    want.captionW = planTitleWidth(dims, title, TitleTier::Large);
    want.tier = TitleTier::Large;
    PanelPlan& plan = planPanel(dims, want);
    float backgroundWidth = plan.width();

    setBounds(START_X, START_Y, START_X + backgroundWidth, START_Y + plan.height());
    addPlanBackground(plan, START_X, START_Y);

    float contentStartX = plan.contentX();
    float currentY = plan.contentY(0);

    // Title. In plugin-thread mode the plugin-time figure is the WORKER's build time, not the
    // game-thread cost (which is ~0) — flag it so a >100% frame-budget reading reads as
    // "off-thread build exceeds a frame", not "the game is stalled".
    addPlanTitle(plan, title, this->getFont(FontCategory::TITLE),
                 this->getColor(ColorSlot::PRIMARY));

    // Side-by-side layout: graph on left (36 chars), gap (1 char), legend on right (9 chars)
    float graphWidth = PluginUtils::calculateMonospaceTextWidth(GRAPH_WIDTH_CHARS, dims.fontSize);
    float gapWidth = PluginUtils::calculateMonospaceTextWidth(1, dims.fontSize);
    // Position legend: if showing graphs, place after graph + gap; otherwise start at left edge
    float legendStartX = showGraphs ? (contentStartX + graphWidth + gapWidth) : contentStartX;

    float pointSpacing = graphWidth / (GRAPH_HISTORY_SIZE - 1);
    float lineThickness = stripChartLineThickness();  // Line thickness for graph rendering

    // Resolve the palette ONCE per rebuild, not once per segment. Both strip charts
    // pick one of these three per point, and this panel draws 238 points a frame --
    // the lookup is not free: an unpinned slot walks BaseHud's override array, then
    // ColorConfig, then UiConfig's theme name, then the theme memo's string compare.
    // Every one of those answers the same thing for the whole rebuild. The legend
    // colours are hoisted for the same reason (eight uses each per section).
    // This is what TelemetryHud has always done -- it passes one resolved colour into
    // addStripChartHistoryLine -- and is most of why it drew MORE quads for less time.
    const unsigned long colGood = this->getColor(ColorSlot::POSITIVE);
    const unsigned long colWarn = this->getColor(ColorSlot::WARNING);
    const unsigned long colBad = this->getColor(ColorSlot::NEGATIVE);
    const unsigned long colLabel = this->getColor(ColorSlot::TERTIARY);
    const unsigned long colValue = this->getColor(ColorSlot::SECONDARY);
    const int fontLabel = this->getFont(FontCategory::STRONG);
    const int fontValue = this->getFont(FontCategory::DIGITS);

    // FPS Section: its OWN card, opened at the heading so the card contains the
    // heading it belongs to, then graph on left + legend on right.
    if (hasFps) {
        addSectionHeading("Frame Rate", contentStartX, currentY, dims);
        currentY += subHeadH;
    }
    float legendY = currentY;  // Track legend Y position separately
    if (m_enabledElements & ELEM_FPS) {
        // FPS Graph - only render if graphs are shown
        if (showGraphs) {
            // FPS grid lines (0-MAX_FPS_DISPLAY range, at 0%/50%/100%) + axis
            // labels (top / middle / bottom) — the shared strip-chart frame.
            char fpsTopBuf[12], fpsMidBuf[12];
            snprintf(fpsTopBuf, sizeof(fpsTopBuf), "%.0f FPS", MAX_FPS_DISPLAY);
            snprintf(fpsMidBuf, sizeof(fpsMidBuf), "%.0f FPS", MAX_FPS_DISPLAY * 0.5f);
            addStripChartFrame(contentStartX, currentY, graphWidth, graphHeight,
                               fpsTopBuf, fpsMidBuf, "0 FPS", dims);

            // Render FPS graph (continuous line segments)
            for (int i = 0; i < GRAPH_HISTORY_SIZE - 1; ++i) {
                int histIdx1 = (m_historyIndex + i) % GRAPH_HISTORY_SIZE;
                int histIdx2 = (m_historyIndex + i + 1) % GRAPH_HISTORY_SIZE;
                float fps1 = m_fpsHistory[histIdx1];
                float fps2 = m_fpsHistory[histIdx2];

                if (fps1 > 0 && fps2 > 0) {
                    float normalizedValue1 = fps1 / MAX_FPS_DISPLAY;
                    float normalizedValue2 = fps2 / MAX_FPS_DISPLAY;
                    if (normalizedValue1 > 1.0f) normalizedValue1 = 1.0f;
                    if (normalizedValue2 > 1.0f) normalizedValue2 = 1.0f;

                    float x1 = contentStartX + i * pointSpacing;
                    float y1 = currentY + graphHeight - (normalizedValue1 * graphHeight);
                    float x2 = contentStartX + (i + 1) * pointSpacing;
                    float y2 = currentY + graphHeight - (normalizedValue2 * graphHeight);

                    // Use color from the first point for consistency
                    unsigned long color;
                    if (fps1 >= 60.0f) {
                        color = colGood;      // Green: good performance
                    } else if (fps1 >= 30.0f) {
                        color = colWarn;      // Yellow: caution
                    } else {
                        color = colBad;       // Red: bad performance
                    }

                    addLineSegment(x1, y1, x2, y2, color, lineThickness);
                }
            }
        }

        // FPS Legend (vertical format on right side)
        if (showValues) {
            // Format (12 chars wide):
            // FPS xxx
            // Max xxx
            // Avg xxx
            // Min xxx
            char buffer[16];
            float valueX = legendStartX + PluginUtils::calculateMonospaceTextWidth(4, dims.fontSize);  // After "XXX "

            // FPS current value
            addLabel("FPS", legendStartX, legendY, Justify::LEFT,
                fontLabel, colLabel, dims);
            snprintf(buffer, sizeof(buffer), "%4d", (int)metrics.currentFps);
            addString(buffer, valueX, legendY, Justify::LEFT,
                fontValue, colValue, dims.fontSize);
            legendY += dims.lineHeightNormal;

            // Max
            addLabel("Max", legendStartX, legendY, Justify::LEFT,
                fontLabel, colLabel, dims);
            snprintf(buffer, sizeof(buffer), "%4d", (int)m_fpsMax);
            addString(buffer, valueX, legendY, Justify::LEFT,
                fontValue, colValue, dims.fontSize);
            legendY += dims.lineHeightNormal;

            // Avg
            addLabel("Avg", legendStartX, legendY, Justify::LEFT,
                fontLabel, colLabel, dims);
            snprintf(buffer, sizeof(buffer), "%4d", (int)m_fpsAvg);
            addString(buffer, valueX, legendY, Justify::LEFT,
                fontValue, colValue, dims.fontSize);
            legendY += dims.lineHeightNormal;

            // Min
            addLabel("Min", legendStartX, legendY, Justify::LEFT,
                fontLabel, colLabel, dims);
            snprintf(buffer, sizeof(buffer), "%4d", (int)m_fpsMin);
            addString(buffer, valueX, legendY, Justify::LEFT,
                fontValue, colValue, dims.fontSize);
            legendY += dims.lineHeightNormal;
        }
    }

    // Advance past FPS section: graph may extend below legend text
    if (hasFps && showGraphs) {
        float fpsGraphBottom = currentY + graphHeight;
        if (fpsGraphBottom > legendY) legendY = fpsGraphBottom;
    }
    // Plugin Time section: its own sibling card — its content starts at the
    // plan's second section (or the first when FPS is off); the seam is the plan's.
    if (hasCpu) {
        currentY = plan.contentY(hasFps ? 1 : 0);
        addSectionHeading("Plugin Time", contentStartX, currentY, dims);
        currentY += subHeadH;
        legendY = currentY;
    } else {
        currentY = legendY;
    }
    if (m_enabledElements & ELEM_CPU) {
        // Plugin Time graph - only render if graphs are shown
        // Ceiling is the 480fps frame budget -- see MAX_PLUGIN_TIME_MS.
        if (showGraphs) {
            // Plugin Time grid lines (0-MAX_PLUGIN_TIME_MS range, at 0%/50%/100%)
            // + ms axis labels (top / middle / bottom) — the shared strip-chart frame.
            char msTopBuf[12], msMidBuf[12];
            snprintf(msTopBuf, sizeof(msTopBuf), "%.1f ms", MAX_PLUGIN_TIME_MS);
            snprintf(msMidBuf, sizeof(msMidBuf), "%.1f ms", MAX_PLUGIN_TIME_MS * 0.5f);
            addStripChartFrame(contentStartX, currentY, graphWidth, graphHeight,
                               msTopBuf, msMidBuf, "0.0 ms", dims);

            // Render Plugin Time graph (continuous line segments, 0-4ms range)
            for (int i = 0; i < GRAPH_HISTORY_SIZE - 1; ++i) {
                int histIdx1 = (m_historyIndex + i) % GRAPH_HISTORY_SIZE;
                int histIdx2 = (m_historyIndex + i + 1) % GRAPH_HISTORY_SIZE;
                float pluginTimeMs1 = m_pluginTimeHistory[histIdx1];
                float pluginTimeMs2 = m_pluginTimeHistory[histIdx2];

                if (pluginTimeMs1 > 0 && pluginTimeMs2 > 0) {
                    // Normalize to 0-4ms range
                    float normalizedValue1 = pluginTimeMs1 / MAX_PLUGIN_TIME_MS;
                    float normalizedValue2 = pluginTimeMs2 / MAX_PLUGIN_TIME_MS;
                    if (normalizedValue1 > 1.0f) normalizedValue1 = 1.0f;
                    if (normalizedValue2 > 1.0f) normalizedValue2 = 1.0f;

                    float x1 = contentStartX + i * pointSpacing;
                    float y1 = currentY + graphHeight - (normalizedValue1 * graphHeight);
                    float x2 = contentStartX + (i + 1) * pointSpacing;
                    float y2 = currentY + graphHeight - (normalizedValue2 * graphHeight);

                    // Use color from the first point for consistency
                    unsigned long color;
                    if (pluginTimeMs1 < MAX_PLUGIN_TIME_MS * PLUGIN_TIME_WARN_FRAC) {
                        color = colGood;      // under half the frame budget
                    } else if (pluginTimeMs1 < MAX_PLUGIN_TIME_MS * PLUGIN_TIME_BAD_FRAC) {
                        color = colWarn;      // half to three quarters
                    } else {
                        color = colBad;       // over three quarters of budget
                    }

                    addLineSegment(x1, y1, x2, y2, color, lineThickness);
                }
            }
        }

        // Plugin Time legend (vertical format on right side)
        if (showValues) {
            // Format (12 chars wide):
            // Now y.yy
            // Max y.yy
            // Avg y.yy
            // Min y.yy
            char buffer[16];
            float valueX = legendStartX + PluginUtils::calculateMonospaceTextWidth(4, dims.fontSize);  // After "XXX "

            // Current value. "Now", not a repeat of the metric name: the
            // heading above already says WHAT is measured, so the four rows can
            // all be statistics (Now/Max/Avg/Min) instead of one metric name
            // followed by three statistics.
            addLabel("Now", legendStartX, legendY, Justify::LEFT,
                fontLabel, colLabel, dims);
            snprintf(buffer, sizeof(buffer), "%5.2f", metrics.pluginTimeMs);
            addString(buffer, valueX, legendY, Justify::LEFT,
                fontValue, colValue, dims.fontSize);
            legendY += dims.lineHeightNormal;

            // Max
            addLabel("Max", legendStartX, legendY, Justify::LEFT,
                fontLabel, colLabel, dims);
            snprintf(buffer, sizeof(buffer), "%5.2f", m_pluginTimeMsMax);
            addString(buffer, valueX, legendY, Justify::LEFT,
                fontValue, colValue, dims.fontSize);
            legendY += dims.lineHeightNormal;

            // Avg
            addLabel("Avg", legendStartX, legendY, Justify::LEFT,
                fontLabel, colLabel, dims);
            snprintf(buffer, sizeof(buffer), "%5.2f", m_pluginTimeMsAvg);
            addString(buffer, valueX, legendY, Justify::LEFT,
                fontValue, colValue, dims.fontSize);
            legendY += dims.lineHeightNormal;

            // Min
            addLabel("Min", legendStartX, legendY, Justify::LEFT,
                fontLabel, colLabel, dims);
            snprintf(buffer, sizeof(buffer), "%5.2f", m_pluginTimeMsMin);
            addString(buffer, valueX, legendY, Justify::LEFT,
                fontValue, colValue, dims.fontSize);
        }
    }

    // Nothing to close: the plan sized and drew every section card up front.
}

void PerformanceHud::recalculateFpsMinMax() {
    // Rescan history to find new min/max (called only when current min/max is removed from buffer)
    m_fpsMin = 999.0f;
    m_fpsMax = 0.0f;
    m_fpsMinIndex = -1;
    m_fpsMaxIndex = -1;

    for (int i = 0; i < GRAPH_HISTORY_SIZE; ++i) {
        if (m_fpsHistory[i] > 0) {
            if (m_fpsMinIndex == -1 || m_fpsHistory[i] < m_fpsMin) {
                m_fpsMin = m_fpsHistory[i];
                m_fpsMinIndex = i;
            }
            if (m_fpsMaxIndex == -1 || m_fpsHistory[i] > m_fpsMax) {
                m_fpsMax = m_fpsHistory[i];
                m_fpsMaxIndex = i;
            }
        }
    }

    // Handle empty history
    if (m_fpsMinIndex == -1) {
        m_fpsMin = 0.0f;
        m_fpsMax = 0.0f;
    }
}

void PerformanceHud::recalculatePluginTimeMinMax() {
    // Rescan history to find new min/max (called only when current min/max is removed from buffer)
    m_pluginTimeMsMin = 999.0f;
    m_pluginTimeMsMax = 0.0f;
    m_pluginMinIndex = -1;
    m_pluginMaxIndex = -1;

    for (int i = 0; i < GRAPH_HISTORY_SIZE; ++i) {
        if (m_pluginTimeHistory[i] > 0) {
            if (m_pluginMinIndex == -1 || m_pluginTimeHistory[i] < m_pluginTimeMsMin) {
                m_pluginTimeMsMin = m_pluginTimeHistory[i];
                m_pluginMinIndex = i;
            }
            if (m_pluginMaxIndex == -1 || m_pluginTimeHistory[i] > m_pluginTimeMsMax) {
                m_pluginTimeMsMax = m_pluginTimeHistory[i];
                m_pluginMaxIndex = i;
            }
        }
    }

    // Handle empty history
    if (m_pluginMinIndex == -1) {
        m_pluginTimeMsMin = 0.0f;
        m_pluginTimeMsMax = 0.0f;
    }
}

void PerformanceHud::resetToDefaults() {
    m_bVisible = false;  // Hidden by default
    m_bShowTitle = true;
    setTextureVariant(0);  // No texture by default
    m_fBackgroundOpacity = SettingsLimits::DEFAULT_OPACITY;
    m_fScale = 1.0f;
    m_graphRows = DEFAULT_GRAPH_ROWS;
    setPosition(cellsX(133), cellsY(56));
    m_enabledElements = ELEM_DEFAULT;
    m_displayMode = DISPLAY_BOTH;  // Show both graphs and values by default

    // Reset history and statistics
    m_fpsHistory.fill(0.0f);
    m_pluginTimeHistory.fill(0.0f);
    m_pluginTimePercentHistory.fill(0.0f);
    m_historyIndex = 0;
    m_fpsMin = 0.0f;
    m_fpsMax = 0.0f;
    m_fpsAvg = 0.0f;
    m_pluginTimeMsMin = 0.0f;
    m_pluginTimeMsMax = 0.0f;
    m_pluginTimeMsAvg = 0.0f;
    m_fpsSum = 0.0f;
    m_pluginTimeSum = 0.0f;
    m_validFpsCount = 0;
    m_validPluginTimeCount = 0;
    m_fpsMinIndex = -1;
    m_fpsMaxIndex = -1;
    m_pluginMinIndex = -1;
    m_pluginMaxIndex = -1;

    setDataDirty();
}
