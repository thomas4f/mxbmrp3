// ============================================================================
// hud/performance_hud.cpp
// Displays performance metrics including FPS and render timing diagnostics
// ============================================================================
#include "performance_hud.h"
#include "../diagnostics/logger.h"
#include "../core/plugin_utils.h"
#include "../core/plugin_constants.h"
#include "../core/plugin_data.h"
#include <cstring>
#include <cstdio>
#include <cmath>

using namespace PluginConstants;

PerformanceHud::PerformanceHud() : m_historyIndex(0), m_fpsMin(0.0f), m_fpsMax(0.0f), m_fpsAvg(0.0f),
    m_pluginTimeMsMin(0.0f), m_pluginTimeMsMax(0.0f), m_pluginTimeMsAvg(0.0f),
    m_fpsSum(0.0f), m_pluginTimeSum(0.0f), m_validFpsCount(0), m_validPluginTimeCount(0),
    m_fpsMinIndex(-1), m_fpsMaxIndex(-1), m_pluginMinIndex(-1), m_pluginMaxIndex(-1)
{
    DEBUG_INFO("PerformanceHud created");
    setDraggable(true);

    // Set defaults to match user configuration
    m_bVisible = false;  // Hidden by default
    m_fBackgroundOpacity = SettingsLimits::DEFAULT_OPACITY;
    setPosition(-0.0165f, 0.0444f);

    // Initialize history arrays
    m_fpsHistory.fill(0.0f);
    m_pluginTimeHistory.fill(0.0f);
    m_pluginTimePercentHistory.fill(0.0f);

    // Pre-allocate vectors (background + 2 line graphs with grid lines)
    // Background: 1, FPS: 4 grid lines + 119 line segments, Plugin: 4 grid lines + 119 line segments = 247 total
    m_quads.reserve(250);
    m_strings.reserve(15);  // Title + (3 labels + 3 values) per graph x 2 graphs = 13 total

    rebuildRenderData();
}

bool PerformanceHud::handlesDataType(DataChangeType dataType) const {
    // PerformanceHud handles debug metrics updates
    return (dataType == DataChangeType::DebugMetrics);
}

void PerformanceHud::update() {
    // Always rebuild - external notification system marks this dirty every frame
    // No need for conditional checks since updateDebugMetrics() is called every draw
    rebuildRenderData();
    clearDataDirty();
    clearLayoutDirty();
}

void PerformanceHud::addStatsRow(float startX, float y, const ScaledDimensions& dims,
                                 const char* currentLabel, float currentValue, int currentPrecision,
                                 float maxValue, float avgValue, float minValue) {
    // Renders a stats row in the format: "LABEL value  Max value  Avg value  Min value"
    // Used for both FPS (integer) and CPU (decimal) stats
    float currentX = startX;

    // Current value (with label)
    addString(currentLabel, currentX, y, Justify::LEFT,
        Fonts::ROBOTO_MONO, TextColors::TERTIARY, dims.fontSize);
    currentX += PluginUtils::calculateMonospaceTextWidth(LABEL_WIDTH, dims.fontSize);
    currentX += PluginUtils::calculateMonospaceTextWidth(SMALL_GAP, dims.fontSize);

    char currentBuffer[16];
    if (currentPrecision == 0) {
        snprintf(currentBuffer, sizeof(currentBuffer), "%*d", INTEGER_VALUE_WIDTH, (int)currentValue);
    } else {
        snprintf(currentBuffer, sizeof(currentBuffer), "%*.2f", DECIMAL_VALUE_WIDTH, currentValue);
    }
    addString(currentBuffer, currentX, y, Justify::LEFT,
        Fonts::ROBOTO_MONO, TextColors::SECONDARY, dims.fontSize);
    currentX += PluginUtils::calculateMonospaceTextWidth(currentPrecision == 0 ? INTEGER_VALUE_WIDTH : DECIMAL_VALUE_WIDTH, dims.fontSize);
    currentX += PluginUtils::calculateMonospaceTextWidth(LARGE_GAP, dims.fontSize);

    // Max
    addString("Max", currentX, y, Justify::LEFT,
        Fonts::ROBOTO_MONO, TextColors::TERTIARY, dims.fontSize);
    currentX += PluginUtils::calculateMonospaceTextWidth(LABEL_WIDTH, dims.fontSize);
    currentX += PluginUtils::calculateMonospaceTextWidth(SMALL_GAP, dims.fontSize);

    char maxBuffer[16];
    if (currentPrecision == 0) {
        snprintf(maxBuffer, sizeof(maxBuffer), "%*d", INTEGER_VALUE_WIDTH, (int)maxValue);
    } else {
        snprintf(maxBuffer, sizeof(maxBuffer), "%*.2f", DECIMAL_VALUE_WIDTH, maxValue);
    }
    addString(maxBuffer, currentX, y, Justify::LEFT,
        Fonts::ROBOTO_MONO, TextColors::SECONDARY, dims.fontSize);
    currentX += PluginUtils::calculateMonospaceTextWidth(currentPrecision == 0 ? INTEGER_VALUE_WIDTH : DECIMAL_VALUE_WIDTH, dims.fontSize);
    currentX += PluginUtils::calculateMonospaceTextWidth(LARGE_GAP, dims.fontSize);

    // Avg
    addString("Avg", currentX, y, Justify::LEFT,
        Fonts::ROBOTO_MONO, TextColors::TERTIARY, dims.fontSize);
    currentX += PluginUtils::calculateMonospaceTextWidth(LABEL_WIDTH, dims.fontSize);
    currentX += PluginUtils::calculateMonospaceTextWidth(SMALL_GAP, dims.fontSize);

    char avgBuffer[16];
    if (currentPrecision == 0) {
        snprintf(avgBuffer, sizeof(avgBuffer), "%*d", INTEGER_VALUE_WIDTH, (int)avgValue);
    } else {
        snprintf(avgBuffer, sizeof(avgBuffer), "%*.2f", DECIMAL_VALUE_WIDTH, avgValue);
    }
    addString(avgBuffer, currentX, y, Justify::LEFT,
        Fonts::ROBOTO_MONO, TextColors::SECONDARY, dims.fontSize);
    currentX += PluginUtils::calculateMonospaceTextWidth(currentPrecision == 0 ? INTEGER_VALUE_WIDTH : DECIMAL_VALUE_WIDTH, dims.fontSize);
    currentX += PluginUtils::calculateMonospaceTextWidth(LARGE_GAP, dims.fontSize);

    // Min
    addString("Min", currentX, y, Justify::LEFT,
        Fonts::ROBOTO_MONO, TextColors::TERTIARY, dims.fontSize);
    currentX += PluginUtils::calculateMonospaceTextWidth(LABEL_WIDTH, dims.fontSize);
    currentX += PluginUtils::calculateMonospaceTextWidth(SMALL_GAP, dims.fontSize);

    char minBuffer[16];
    if (currentPrecision == 0) {
        snprintf(minBuffer, sizeof(minBuffer), "%*d", INTEGER_VALUE_WIDTH, (int)minValue);
    } else {
        snprintf(minBuffer, sizeof(minBuffer), "%*.2f", DECIMAL_VALUE_WIDTH, minValue);
    }
    addString(minBuffer, currentX, y, Justify::LEFT,
        Fonts::ROBOTO_MONO, TextColors::SECONDARY, dims.fontSize);
}

void PerformanceHud::rebuildRenderData() {
    m_strings.clear();
    m_quads.clear();

    // Get debug metrics from PluginData
    const auto& metrics = PluginData::getInstance().getDebugMetrics();

    // PERFORMANCE OPTIMIZATION: Use incremental statistics instead of rescanning 120 samples every frame
    // Previous: O(120) scan every frame
    // New: O(1) best case, O(120) only when min/max value is replaced

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

    // Calculate dimensions
    int widthChars = getBackgroundWidthChars();
    float backgroundWidth = PluginUtils::calculateMonospaceTextWidth(widthChars, dims.fontSize)
        + dims.paddingH + dims.paddingH;
    float graphHeight = GRAPH_HEIGHT_LINES * dims.lineHeightNormal;
    // Height: top pad + title (if shown) + max(graph height, legend height) + bottom pad
    float titleHeight = m_bShowTitle ? dims.lineHeightLarge : 0.0f;

    // Determine if we show graphs and/or values based on display mode
    bool showGraphs = (m_displayMode == DISPLAY_GRAPHS || m_displayMode == DISPLAY_BOTH);
    bool showValues = (m_displayMode == DISPLAY_VALUES || m_displayMode == DISPLAY_BOTH);

    // Calculate legend height (values are shown vertically on the right)
    // Each metric: label + Max + Avg + Min = 4 lines
    // Gap between metrics: 1 line
    int legendLines = 0;
    bool hasFps = (m_enabledElements & ELEM_FPS) && showValues;
    bool hasCpu = (m_enabledElements & ELEM_CPU) && showValues;
    if (hasFps) legendLines += 4;
    if (hasFps && hasCpu) legendLines += 1;  // Gap between FPS and CPU sections
    if (hasCpu) legendLines += 4;
    float legendHeight = legendLines * dims.lineHeightNormal;

    // Content height is max of graph height and legend height
    float contentHeight = showGraphs ? (graphHeight > legendHeight ? graphHeight : legendHeight) : legendHeight;
    float backgroundHeight = dims.paddingV + titleHeight + contentHeight + dims.paddingV;

    setBounds(START_X, START_Y, START_X + backgroundWidth, START_Y + backgroundHeight);
    addBackgroundQuad(START_X, START_Y, backgroundWidth, backgroundHeight);

    float contentStartX = START_X + dims.paddingH;
    float contentStartY = START_Y + dims.paddingV;
    float currentY = contentStartY;

    // Title
    addTitleString("Performance", contentStartX, currentY, Justify::LEFT,
        Fonts::ENTER_SANSMAN, TextColors::PRIMARY, dims.fontSizeLarge);
    currentY += titleHeight;

    // Side-by-side layout: graph on left (36 chars), gap (1 char), legend on right (9 chars)
    float graphWidth = PluginUtils::calculateMonospaceTextWidth(GRAPH_WIDTH_CHARS, dims.fontSize);
    float legendWidth = PluginUtils::calculateMonospaceTextWidth(LEGEND_WIDTH_CHARS, dims.fontSize);
    float gapWidth = PluginUtils::calculateMonospaceTextWidth(1, dims.fontSize);
    // Position legend: if showing graphs, place after graph + gap; otherwise start at left edge
    float legendStartX = showGraphs ? (contentStartX + graphWidth + gapWidth) : contentStartX;

    float pointSpacing = graphWidth / (GRAPH_HISTORY_SIZE - 1);
    float lineThickness = 0.002f * getScale();  // Line thickness for graph rendering
    float gridLineThickness = 0.001f * getScale();  // ~1px at 1080p for subtle grid lines
    constexpr unsigned long gridColor = TextColors::MUTED;  // Muted gray for subtle grid lines

    // FPS Section (graph on left, legend on right)
    float legendY = currentY;  // Track legend Y position separately
    if (m_enabledElements & ELEM_FPS) {
        // FPS Graph - only render if graphs are shown
        if (showGraphs) {
            // Draw FPS grid lines (0-MAX_FPS_DISPLAY range, evenly spaced at 80%/60%/40%/20%)
            const float fpsGridValues[] = {
                MAX_FPS_DISPLAY * 0.8f,  // 200 FPS (80%)
                MAX_FPS_DISPLAY * 0.6f,  // 150 FPS (60%)
                MAX_FPS_DISPLAY * 0.4f,  // 100 FPS (40%)
                MAX_FPS_DISPLAY * 0.2f   //  50 FPS (20%)
            };
            for (float fpsValue : fpsGridValues) {
                float normalizedValue = fpsValue / MAX_FPS_DISPLAY;
                float gridY = currentY + graphHeight - (normalizedValue * graphHeight);
                addHorizontalGridLine(contentStartX, gridY, graphWidth, gridColor, gridLineThickness);
            }

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
                        color = SemanticColors::POSITIVE;  // Green: good performance
                    } else if (fps1 >= 30.0f) {
                        color = SemanticColors::WARNING;   // Yellow: caution
                    } else {
                        color = SemanticColors::NEGATIVE;  // Red: bad performance
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

            // FPS current value
            snprintf(buffer, sizeof(buffer), "FPS  %3d", (int)metrics.currentFps);
            addString(buffer, legendStartX, legendY, Justify::LEFT,
                Fonts::ROBOTO_MONO, TextColors::SECONDARY, dims.fontSize);
            legendY += dims.lineHeightNormal;

            // Max
            snprintf(buffer, sizeof(buffer), "Max  %3d", (int)m_fpsMax);
            addString(buffer, legendStartX, legendY, Justify::LEFT,
                Fonts::ROBOTO_MONO, TextColors::SECONDARY, dims.fontSize);
            legendY += dims.lineHeightNormal;

            // Avg
            snprintf(buffer, sizeof(buffer), "Avg  %3d", (int)m_fpsAvg);
            addString(buffer, legendStartX, legendY, Justify::LEFT,
                Fonts::ROBOTO_MONO, TextColors::SECONDARY, dims.fontSize);
            legendY += dims.lineHeightNormal;

            // Min
            snprintf(buffer, sizeof(buffer), "Min  %3d", (int)m_fpsMin);
            addString(buffer, legendStartX, legendY, Justify::LEFT,
                Fonts::ROBOTO_MONO, TextColors::SECONDARY, dims.fontSize);
            legendY += dims.lineHeightNormal;

            // Add gap before CPU section if both are enabled
            if (m_enabledElements & ELEM_CPU) {
                legendY += dims.lineHeightNormal;
            }
        }
    }

    // Update currentY to align with legend position for CPU graph
    currentY = legendY;

    // CPU Section (graph on left, legend on right)
    if (m_enabledElements & ELEM_CPU) {
        // CPU Time Graph - only render if graphs are shown
        // Conservative ceiling: MAX_PLUGIN_TIME_MS gives safe buffer (leaves ~3ms for game at 144fps)
        if (showGraphs) {
            // Draw Plugin Time grid lines (0-MAX_PLUGIN_TIME_MS range, evenly spaced at 80%/60%/40%/20%)
            const float msGridValues[] = {
                MAX_PLUGIN_TIME_MS * 0.8f,  // 3.2ms (80%)
                MAX_PLUGIN_TIME_MS * 0.6f,  // 2.4ms (60%)
                MAX_PLUGIN_TIME_MS * 0.4f,  // 1.6ms (40%)
                MAX_PLUGIN_TIME_MS * 0.2f   // 0.8ms (20%)
            };
            for (float msValue : msGridValues) {
                float normalizedValue = msValue / MAX_PLUGIN_TIME_MS;
                float gridY = currentY + graphHeight - (normalizedValue * graphHeight);
                addHorizontalGridLine(contentStartX, gridY, graphWidth, gridColor, gridLineThickness);
            }

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
                    if (pluginTimeMs1 < 2.0f) {
                        color = SemanticColors::POSITIVE;  // Green: <2ms (safe)
                    } else if (pluginTimeMs1 < 3.0f) {
                        color = SemanticColors::WARNING;   // Yellow: 2-3ms (caution)
                    } else {
                        color = SemanticColors::NEGATIVE;  // Red: >3ms (heavy)
                    }

                    addLineSegment(x1, y1, x2, y2, color, lineThickness);
                }
            }
        }

        // CPU Legend (vertical format on right side)
        if (showValues) {
            // Format (12 chars wide):
            // CPU y.yy
            // Max y.yy
            // Avg y.yy
            // Min y.yy
            char buffer[16];

            // CPU current value
            snprintf(buffer, sizeof(buffer), "CPU  %4.2f", metrics.pluginTimeMs);
            addString(buffer, legendStartX, legendY, Justify::LEFT,
                Fonts::ROBOTO_MONO, TextColors::SECONDARY, dims.fontSize);
            legendY += dims.lineHeightNormal;

            // Max
            snprintf(buffer, sizeof(buffer), "Max  %4.2f", m_pluginTimeMsMax);
            addString(buffer, legendStartX, legendY, Justify::LEFT,
                Fonts::ROBOTO_MONO, TextColors::SECONDARY, dims.fontSize);
            legendY += dims.lineHeightNormal;

            // Avg
            snprintf(buffer, sizeof(buffer), "Avg  %4.2f", m_pluginTimeMsAvg);
            addString(buffer, legendStartX, legendY, Justify::LEFT,
                Fonts::ROBOTO_MONO, TextColors::SECONDARY, dims.fontSize);
            legendY += dims.lineHeightNormal;

            // Min
            snprintf(buffer, sizeof(buffer), "Min  %4.2f", m_pluginTimeMsMin);
            addString(buffer, legendStartX, legendY, Justify::LEFT,
                Fonts::ROBOTO_MONO, TextColors::SECONDARY, dims.fontSize);
        }
    }
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
    m_bShowBackgroundTexture = false;  // No texture by default
    m_fBackgroundOpacity = SettingsLimits::DEFAULT_OPACITY;
    m_fScale = 1.0f;
    setPosition(-0.0165f, 0.0333f);
    m_enabledElements = ELEM_DEFAULT;
    m_displayMode = DISPLAY_BOTH;  // Show both graphs and values by default
    setDataDirty();
}
