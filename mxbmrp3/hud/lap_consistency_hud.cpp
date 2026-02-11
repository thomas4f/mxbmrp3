// ============================================================================
// hud/lap_consistency_hud.cpp
// Lap Consistency HUD - visualizes lap time consistency with charts and statistics
// ============================================================================
#include "lap_consistency_hud.h"
#include "../core/plugin_utils.h"
#include "../core/color_config.h"
#include "../core/personal_best_manager.h"
#include "../core/hud_manager.h"
#include "../diagnostics/logger.h"
#if GAME_HAS_RECORDS_PROVIDER
#include "records_hud.h"
#endif
#include <algorithm>
#include <cstdio>
#include <cmath>
#include <numeric>

using namespace PluginConstants;

LapConsistencyHud::LapConsistencyHud() {
    DEBUG_INFO("LapConsistencyHud created");
    setDraggable(true);

    // Reserve space for render data
    // Bars mode: ~30 bars × 2 quads + background + reference line + labels
    m_quads.reserve(80);
    m_strings.reserve(20);
    m_lapBars.reserve(MAX_LAP_COUNT);

    // Set texture base name for dynamic texture discovery
    setTextureBaseName("lap_consistency_hud");

    resetToDefaults();
    rebuildRenderData();
}

void LapConsistencyHud::update() {
    // Skip processing when not visible
    if (!isVisible()) {
        clearDataDirty();
        clearLayoutDirty();
        return;
    }

    processDirtyFlags();
}

bool LapConsistencyHud::handlesDataType(DataChangeType dataType) const {
    switch (dataType) {
        case DataChangeType::LapLog:
        case DataChangeType::IdealLap:
        case DataChangeType::SpectateTarget:
        case DataChangeType::SessionData:  // For ALLTIME reference mode (track/bike changes)
            return true;
        default:
            return false;
    }
}

void LapConsistencyHud::rebuildLayout() {
    // Full rebuild since chart dimensions depend on data
    rebuildRenderData();
}

void LapConsistencyHud::rebuildRenderData() {
    m_quads.clear();
    clearStrings();
    m_lapBars.clear();

    const auto dims = getScaledDimensions();

    // Calculate statistics from available data
    calculateStatistics();

    // Determine what to show based on display mode
    bool showGraphs = (m_displayMode == DISPLAY_GRAPHS || m_displayMode == DISPLAY_BOTH);
    bool showValues = (m_displayMode == DISPLAY_VALUES || m_displayMode == DISPLAY_BOTH);

    // Calculate dimensions (horizontal layout: graph left, stats right)
    float titleHeight = m_bShowTitle ? dims.lineHeightLarge : 0.0f;

    // Fixed graph height (consistent with PerformanceHud/TelemetryHud)
    float graphHeight = GRAPH_HEIGHT_LINES * dims.lineHeightNormal;

    // Legend height based on enabled stats
    int enabledStatCount = getEnabledStatCount();
    float legendHeight = std::max(1, enabledStatCount) * dims.lineHeightNormal;

    // Content height is max of graph height and legend height (matches PerformanceHud pattern)
    float contentHeight = showGraphs
        ? (graphHeight > legendHeight ? graphHeight : legendHeight)
        : legendHeight;

    float gapWidth = dims.paddingH;

    // Width based on display mode
    float graphWidth = PluginUtils::calculateMonospaceTextWidth(GRAPH_WIDTH_CHARS, dims.fontSize);
    float legendWidth = PluginUtils::calculateMonospaceTextWidth(LEGEND_WIDTH_CHARS, dims.fontSize);

    float contentWidth = 0.0f;
    if (showGraphs && showValues) {
        contentWidth = graphWidth + gapWidth + legendWidth;
    } else if (showGraphs) {
        contentWidth = graphWidth;
    } else {
        contentWidth = legendWidth;
    }

    float backgroundWidth = dims.paddingH + contentWidth + dims.paddingH;
    float backgroundHeight = dims.paddingV + titleHeight + contentHeight + dims.paddingV;

    setBounds(START_X, START_Y, START_X + backgroundWidth, START_Y + backgroundHeight);

    // Add background quad
    addBackgroundQuad(START_X, START_Y, backgroundWidth, backgroundHeight);

    float contentStartX = START_X + dims.paddingH;
    float currentY = START_Y + dims.paddingV;

    // Title (conditional, matches PerformanceHud/TelemetryHud pattern)
    if (m_bShowTitle) {
        addTitleString("Consistency", contentStartX, currentY, Justify::LEFT,
            this->getFont(FontCategory::TITLE), this->getColor(ColorSlot::PRIMARY), dims.fontSizeLarge);
        currentY += titleHeight;
    }

    // Render based on display mode (horizontal layout)
    if (showGraphs && showValues) {
        // Both: graph on left, stats on right
        renderBars(contentStartX, currentY, graphWidth, graphHeight);
        renderTrendLine(contentStartX, currentY, graphWidth, graphHeight);
        float legendStartX = contentStartX + graphWidth + gapWidth;
        renderStatistics(legendStartX, currentY, legendWidth);
    } else if (showGraphs) {
        // Graphs only
        renderBars(contentStartX, currentY, graphWidth, graphHeight);
        renderTrendLine(contentStartX, currentY, graphWidth, graphHeight);
    } else {
        // Values only
        renderStatistics(contentStartX, currentY, legendWidth);
    }
}

void LapConsistencyHud::calculateStatistics() {
    const PluginData& pluginData = PluginData::getInstance();
    const std::deque<LapLogEntry>* lapLog = pluginData.getLapLog();

    // Reset statistics
    m_stats = LapStats();
    m_lapBars.clear();
    m_cachedMaxDeltaMs = 1000;  // Minimum 1 second range

    if (!lapLog || lapLog->empty()) {
        return;
    }

    // Collect valid completed laps (newest first in deque)
    std::vector<int> validTimes;
    validTimes.reserve(m_lapCount);

    for (size_t i = 0; i < lapLog->size() && static_cast<int>(m_lapBars.size()) < m_lapCount; ++i) {
        const LapLogEntry& entry = (*lapLog)[i];

        // Skip incomplete laps (still in progress)
        if (!entry.isComplete) continue;

        // Check if this is a valid lap with valid time
        bool isValidLap = entry.isValid && entry.lapTime > 0;

        // Always add to m_lapBars to preserve position (for gap display)
        LapBarData barData;
        barData.lapNum = entry.lapNum;
        barData.lapTimeMs = isValidLap ? entry.lapTime : 0;
        barData.deltaMs = 0;  // Calculated below for valid laps
        barData.isValid = isValidLap;
        barData.isBest = false;  // Set below
        m_lapBars.push_back(barData);

        // Only include valid laps in statistics
        if (!isValidLap) continue;

        validTimes.push_back(entry.lapTime);
    }

    if (validTimes.empty()) {
        return;
    }

    m_stats.validLapCount = static_cast<int>(validTimes.size());

    // Calculate average
    int64_t sum = 0;
    for (int t : validTimes) {
        sum += t;
    }
    m_stats.averageMs = static_cast<int>(sum / validTimes.size());

    // Find best, worst, and last (newest is at index 0)
    m_stats.bestMs = *std::min_element(validTimes.begin(), validTimes.end());
    m_stats.worstMs = *std::max_element(validTimes.begin(), validTimes.end());
    m_stats.lastMs = validTimes[0];  // Most recent valid lap

    // Find which lap is the best
    for (auto& bar : m_lapBars) {
        if (bar.lapTimeMs == m_stats.bestMs) {
            bar.isBest = true;
            m_stats.bestLapNum = bar.lapNum;
            break;  // Only mark first occurrence
        }
    }

    // Calculate standard deviation
    if (validTimes.size() > 1) {
        double variance = 0.0;
        for (int t : validTimes) {
            double diff = static_cast<double>(t - m_stats.averageMs);
            variance += diff * diff;
        }
        m_stats.stdDevMs = static_cast<float>(std::sqrt(variance / validTimes.size()));
    }

    // Calculate consistency score
    // Based on coefficient of variation (CV = stddev/mean)
    // Score = 100% when stddev = 0, reaches 0% at CV = 1/scaleFactor
    // m_consistencyScaleFactor controls sensitivity (default 20.0 = aggressive)
    if (m_stats.averageMs > 0) {
        float cv = m_stats.stdDevMs / static_cast<float>(m_stats.averageMs);
        m_stats.consistencyScore = std::max(0.0f, std::min(100.0f,
            100.0f * (1.0f - cv * m_consistencyScaleFactor)));
    }

    // Calculate trend (compare first half to second half)
    // Positive = improving (later laps faster), Negative = declining
    if (validTimes.size() >= 4) {
        size_t half = validTimes.size() / 2;
        int64_t firstHalfSum = 0, secondHalfSum = 0;

        // Note: validTimes[0] is newest, so "first half" is recent laps
        for (size_t i = 0; i < half; ++i) {
            firstHalfSum += validTimes[i];  // Recent laps
        }
        for (size_t i = half; i < validTimes.size(); ++i) {
            secondHalfSum += validTimes[i];  // Older laps
        }

        int firstHalfAvg = static_cast<int>(firstHalfSum / half);
        int secondHalfAvg = static_cast<int>(secondHalfSum / (validTimes.size() - half));

        // Threshold: percentage-based difference to register as trend
        // Adapts to track length (m_trendThresholdPercent default 0.5%)
        int thresholdMs = static_cast<int>(m_stats.averageMs * m_trendThresholdPercent / 100.0f);
        int diff = secondHalfAvg - firstHalfAvg;
        if (diff > thresholdMs) {
            m_stats.trendDirection = 1;  // Improving (recent laps faster)
        } else if (diff < -thresholdMs) {
            m_stats.trendDirection = -1;  // Declining (recent laps slower)
        } else {
            m_stats.trendDirection = 0;  // Stable
        }
    }

    // Calculate deltas from reference time (only for valid laps)
    int referenceTime = getReferenceTime();
    for (auto& bar : m_lapBars) {
        if (bar.isValid) {
            bar.deltaMs = bar.lapTimeMs - referenceTime;
            m_cachedMaxDeltaMs = std::max(m_cachedMaxDeltaMs, std::abs(bar.deltaMs));
        }
        // Invalid laps keep deltaMs = 0
    }
}

int LapConsistencyHud::getReferenceTime() const {
    const PluginData& pluginData = PluginData::getInstance();

    // Helper lambda for fallback when primary reference is unavailable
    auto getFallbackTime = [&]() -> int {
        const LapLogEntry* bestEntry = pluginData.getBestLapEntry();
        if (bestEntry && bestEntry->lapTime > 0) {
            return bestEntry->lapTime;
        }
        return m_stats.bestMs > 0 ? m_stats.bestMs : m_stats.averageMs;
    };

    switch (m_referenceMode) {
        case ReferenceMode::SESSION_PB: {
            // Session personal best (best lap this session)
            // Always "available" (it's session data, just waiting for laps to be completed)
            m_referenceAvailable = true;
            const LapLogEntry* bestEntry = pluginData.getBestLapEntry();
            if (bestEntry && bestEntry->lapTime > 0) {
                return bestEntry->lapTime;
            }
            // Fall back to calculated best from sample
            return m_stats.bestMs > 0 ? m_stats.bestMs : 0;
        }

        case ReferenceMode::ALLTIME: {
            // All-time personal best (persisted across sessions)
            const SessionData& sessionData = pluginData.getSessionData();
            const PersonalBestEntry* allTimePB = PersonalBestManager::getInstance()
                .getPersonalBest(sessionData.trackId, sessionData.bikeName);
            if (allTimePB && allTimePB->isValid()) {
                m_referenceAvailable = true;
                return allTimePB->lapTime;
            }
            // Fall back to session PB - mark as unavailable since no alltime PB exists
            m_referenceAvailable = false;
            return getFallbackTime();
        }

        case ReferenceMode::IDEAL: {
            // Ideal lap (sum of best sectors)
            const IdealLapData* idealLapData = pluginData.getIdealLapData();
            if (idealLapData) {
                int idealTime = idealLapData->getIdealLapTime();
                if (idealTime > 0) {
                    m_referenceAvailable = true;
                    return idealTime;
                }
            }
            // Fall back to session PB - mark as unavailable since no ideal lap exists
            m_referenceAvailable = false;
            return getFallbackTime();
        }

        case ReferenceMode::OVERALL: {
            // Overall best lap by anyone in session (multiplayer)
            const LapLogEntry* overallBest = pluginData.getOverallBestLap();
            if (overallBest && overallBest->lapTime > 0) {
                m_referenceAvailable = true;
                return overallBest->lapTime;
            }
            // Fall back to session PB - mark as unavailable since no overall best exists
            m_referenceAvailable = false;
            return getFallbackTime();
        }

#if GAME_HAS_RECORDS_PROVIDER
        case ReferenceMode::RECORD: {
            // Record from RecordsHud provider
            const RecordsHud& recordsHud = HudManager::getInstance().getRecordsHud();
            int recordTime = recordsHud.getFastestRecordLapTime();
            if (recordTime > 0) {
                m_referenceAvailable = true;
                return recordTime;
            }
            // Fall back to session PB - mark as unavailable since no record exists
            m_referenceAvailable = false;
            return getFallbackTime();
        }
#endif

        case ReferenceMode::AVERAGE:
        default:
            // Average is always "available" (it's computed from session data, just waiting for laps)
            m_referenceAvailable = true;
            return m_stats.averageMs;
    }
}

void LapConsistencyHud::renderBars(float x, float y, float width, float height) {
    const auto dims = getScaledDimensions();

    // Grid line styling (consistent with PerformanceHud/TelemetryHud)
    float gridLineThickness = 0.001f * dims.scale;  // ~1px at 1080p for subtle grid lines
    unsigned long gridColor = this->getColor(ColorSlot::MUTED);    // Muted gray for subtle grid lines

    // Calculate FIXED bar width based on configured lap count, not actual bars
    // This ensures consistent bar width regardless of how many laps completed
    float barSpacing = 0.002f * dims.scale;
    float totalBarWidth = width - (barSpacing * (m_lapCount - 1));
    float barWidth = totalBarWidth / m_lapCount;

    // Reference line at center (y + height/2)
    float midY = y + height / 2.0f;

    // Draw reference line at center (more prominent - uses secondary color)
    addHorizontalGridLine(x, midY, width, this->getColor(ColorSlot::SECONDARY), gridLineThickness);

    // Draw grid lines at 0% and 100% (50% covered by reference line above)
    const float gridPositions[] = { 0.0f, 1.0f };
    for (float pos : gridPositions) {
        float gridY = y + (pos * height);
        addHorizontalGridLine(x, gridY, width, gridColor, gridLineThickness);
    }

    if (m_lapBars.empty()) {
        return;
    }

    int numBars = static_cast<int>(m_lapBars.size());

    // Position bars from the RIGHT side (newest lap on right)
    // If we have fewer bars than m_lapCount, empty space is on the left
    // m_lapBars[0] is newest, m_lapBars[numBars-1] is oldest
    // We want: oldest on left, newest on right
    int emptySlots = m_lapCount - numBars;
    float startX = x + (emptySlots * (barWidth + barSpacing));

    // Draw bars (oldest to newest, left to right)
    float currentX = startX;
    for (int i = numBars - 1; i >= 0; --i) {
        const LapBarData& bar = m_lapBars[i];

        // For invalid laps, leave empty gap
        if (!bar.isValid) {
            currentX += barWidth + barSpacing;
            continue;
        }

        addConsistencyBar(currentX, y, barWidth, height / 2.0f,
                          bar.deltaMs, m_cachedMaxDeltaMs, bar.isBest);
        currentX += barWidth + barSpacing;
    }

    // Draw Y-axis labels (delta times on left side, inside chart area)
    {
        char labelBuf[16];
        float labelX = x + dims.paddingH * 0.2f;  // Inside chart area, near left edge

        // Top label: max positive delta (slower)
        float maxDeltaSec = static_cast<float>(m_cachedMaxDeltaMs) / 1000.0f;
        snprintf(labelBuf, sizeof(labelBuf), "+%.1fs", maxDeltaSec);
        addString(labelBuf, labelX, y,
                  Justify::LEFT, this->getFont(FontCategory::SMALL), this->getColor(ColorSlot::TERTIARY), dims.fontSizeSmall);

        // Middle label: reference time (below center line for clarity)
        int refTime = getReferenceTime();
        if (refTime > 0) {
            formatLapTime(labelBuf, sizeof(labelBuf), refTime);
            addString(labelBuf, labelX, midY + dims.lineHeightSmall * 0.15f,
                      Justify::LEFT, this->getFont(FontCategory::SMALL), this->getColor(ColorSlot::SECONDARY), dims.fontSizeSmall);
        }

        // Bottom label: max negative delta (faster)
        snprintf(labelBuf, sizeof(labelBuf), "-%.1fs", maxDeltaSec);
        addString(labelBuf, labelX, y + height - dims.lineHeightSmall,
                  Justify::LEFT, this->getFont(FontCategory::SMALL), this->getColor(ColorSlot::TERTIARY), dims.fontSizeSmall);
    }

    // Draw X-axis lap number labels
    // Note: API uses 0-based lap numbers, but UI displays 1-based (like LapLogHud)
    if (numBars > 0) {
        char labelBuf[8];

        // Oldest visible lap (at startX position)
        snprintf(labelBuf, sizeof(labelBuf), "L%d", m_lapBars[numBars - 1].lapNum + 1);
        addString(labelBuf, startX, y + height + dims.lineHeightSmall * 0.2f,
                  Justify::LEFT, this->getFont(FontCategory::SMALL), this->getColor(ColorSlot::TERTIARY), dims.fontSizeSmall);

        // Newest lap (right edge)
        if (numBars > 1) {
            snprintf(labelBuf, sizeof(labelBuf), "L%d", m_lapBars[0].lapNum + 1);
            addString(labelBuf, x + width, y + height + dims.lineHeightSmall * 0.2f,
                      Justify::RIGHT, this->getFont(FontCategory::SMALL), this->getColor(ColorSlot::TERTIARY), dims.fontSizeSmall);
        }
    }
}

void LapConsistencyHud::addConsistencyBar(float x, float y, float barWidth, float maxBarHeight,
                                          int deltaMs, int maxDeltaMs, bool isBest) {
    // Normalize delta to -1.0 to +1.0 range
    float normalizedDelta = 0.0f;
    if (maxDeltaMs > 0) {
        normalizedDelta = static_cast<float>(deltaMs) / static_cast<float>(maxDeltaMs);
        normalizedDelta = std::max(-1.0f, std::min(1.0f, normalizedDelta));
    }

    float barHeight = std::abs(normalizedDelta) * maxBarHeight;
    if (barHeight < 0.001f) {
        barHeight = 0.001f;  // Minimum visible height
    }

    // Determine color based on delta from reference
    unsigned long color = (deltaMs < 0) ? this->getColor(ColorSlot::POSITIVE) : this->getColor(ColorSlot::NEGATIVE);

    // Calculate bar position (grows from center line)
    float barY = y + maxBarHeight;  // Center line position
    if (deltaMs < 0) {
        // Faster: bar grows upward from center
        barY = y + maxBarHeight - barHeight;
    }
    // Slower: bar grows downward from center (barY stays at maxBarHeight)

    // Create bar quad - best lap gets full opacity for emphasis
    SPluginQuad_t barQuad;
    float barX = x;
    float adjustedY = barY;
    applyOffset(barX, adjustedY);
    setQuadPositions(barQuad, barX, adjustedY, barWidth, barHeight);
    barQuad.m_iSprite = SpriteIndex::SOLID_COLOR;
    barQuad.m_ulColor = PluginUtils::applyOpacity(color, isBest ? 1.0f : 0.85f);
    m_quads.push_back(barQuad);
}

void LapConsistencyHud::renderTrendLine(float x, float y, float width, float height) {
    // Check if trend line is disabled
    if (m_trendMode == TrendMode::OFF || m_lapBars.size() < 2) {
        return;
    }

    const auto dims = getScaledDimensions();

    int numPoints = static_cast<int>(m_lapBars.size());

    // Use FIXED point spacing based on configured lap count
    float pointSpacing = width / (m_lapCount - 1);
    float midY = y + height / 2.0f;
    float maxBarHeight = height / 2.0f;

    // Calculate starting X offset (same as bars - empty slots on left)
    int emptySlots = m_lapCount - numPoints;
    float startX = x + (emptySlots * pointSpacing);

    unsigned long lineColor = this->getColor(ColorSlot::PRIMARY);
    float lineThickness = 0.002f * dims.scale;

    // Collect valid lap data (in display order: oldest to newest)
    std::vector<std::pair<int, int>> validLaps;  // (display index, deltaMs)
    for (int i = numPoints - 1; i >= 0; --i) {
        if (m_lapBars[i].isValid) {
            int displayIdx = numPoints - 1 - i;  // 0 = oldest, numPoints-1 = newest
            validLaps.push_back({displayIdx, m_lapBars[i].deltaMs});
        }
    }

    if (validLaps.size() < 2) {
        return;
    }

    switch (m_trendMode) {
        case TrendMode::LINE: {
            // Connected dots - original behavior
            for (size_t v = 0; v + 1 < validLaps.size(); ++v) {
                float x1 = startX + validLaps[v].first * pointSpacing;
                float x2 = startX + validLaps[v + 1].first * pointSpacing;

                float norm1 = (m_cachedMaxDeltaMs > 0)
                    ? static_cast<float>(validLaps[v].second) / m_cachedMaxDeltaMs : 0.0f;
                float norm2 = (m_cachedMaxDeltaMs > 0)
                    ? static_cast<float>(validLaps[v + 1].second) / m_cachedMaxDeltaMs : 0.0f;

                float y1 = midY + (norm1 * maxBarHeight);
                float y2 = midY + (norm2 * maxBarHeight);

                addLineSegment(x1, y1, x2, y2, lineColor, lineThickness);
            }

            // Draw dots at each data point
            float dotSize = 0.004f * dims.scale;
            for (size_t v = 0; v < validLaps.size(); ++v) {
                float dotX = startX + validLaps[v].first * pointSpacing;
                float norm = (m_cachedMaxDeltaMs > 0)
                    ? static_cast<float>(validLaps[v].second) / m_cachedMaxDeltaMs : 0.0f;
                float dotY = midY + (norm * maxBarHeight);

                // Color dot by delta (consistent with bar coloring)
                int deltaMs = validLaps[v].second;
                unsigned long dotColor = (deltaMs < 0) ? this->getColor(ColorSlot::POSITIVE) : this->getColor(ColorSlot::NEGATIVE);
                addDot(dotX, dotY, dotColor, dotSize);
            }
            break;
        }

        case TrendMode::AVERAGE: {
            // Moving average (3-lap window)
            const int windowSize = 3;
            std::vector<std::pair<float, float>> avgPoints;  // (x, y) coordinates

            for (size_t v = 0; v < validLaps.size(); ++v) {
                // Calculate average of surrounding laps (centered window)
                int start = static_cast<int>(v) - windowSize / 2;
                int end = start + windowSize;

                // Clamp to valid range
                if (start < 0) start = 0;
                if (end > static_cast<int>(validLaps.size())) end = static_cast<int>(validLaps.size());

                int sum = 0;
                int count = 0;
                for (int i = start; i < end; ++i) {
                    sum += validLaps[i].second;
                    ++count;
                }

                if (count > 0) {
                    float avgDelta = static_cast<float>(sum) / count;
                    float px = startX + validLaps[v].first * pointSpacing;
                    float norm = (m_cachedMaxDeltaMs > 0) ? avgDelta / m_cachedMaxDeltaMs : 0.0f;
                    float py = midY + (norm * maxBarHeight);
                    avgPoints.push_back({px, py});
                }
            }

            // Draw smoothed line
            for (size_t i = 0; i + 1 < avgPoints.size(); ++i) {
                addLineSegment(avgPoints[i].first, avgPoints[i].second,
                              avgPoints[i + 1].first, avgPoints[i + 1].second,
                              lineColor, lineThickness);
            }
            break;
        }

        case TrendMode::LINEAR: {
            // Linear regression best-fit line
            // y = mx + b where x is lap index, y is delta
            float sumX = 0, sumY = 0, sumXY = 0, sumX2 = 0;
            int n = static_cast<int>(validLaps.size());

            for (const auto& lap : validLaps) {
                float xi = static_cast<float>(lap.first);
                float yi = static_cast<float>(lap.second);
                sumX += xi;
                sumY += yi;
                sumXY += xi * yi;
                sumX2 += xi * xi;
            }

            float denom = n * sumX2 - sumX * sumX;
            if (std::abs(denom) < 0.0001f) {
                // Degenerate case - just draw horizontal line at average
                float avgY = midY + ((sumY / n) / m_cachedMaxDeltaMs) * maxBarHeight;
                addLineSegment(startX, avgY, startX + width, avgY, lineColor, lineThickness);
            } else {
                float slope = (n * sumXY - sumX * sumY) / denom;
                float intercept = (sumY - slope * sumX) / n;

                // Calculate line endpoints at first and last valid lap positions
                float x1 = startX + validLaps.front().first * pointSpacing;
                float x2 = startX + validLaps.back().first * pointSpacing;

                float y1_delta = slope * validLaps.front().first + intercept;
                float y2_delta = slope * validLaps.back().first + intercept;

                float norm1 = (m_cachedMaxDeltaMs > 0) ? y1_delta / m_cachedMaxDeltaMs : 0.0f;
                float norm2 = (m_cachedMaxDeltaMs > 0) ? y2_delta / m_cachedMaxDeltaMs : 0.0f;

                float y1 = midY + (norm1 * maxBarHeight);
                float y2 = midY + (norm2 * maxBarHeight);

                addLineSegment(x1, y1, x2, y2, lineColor, lineThickness);
            }
            break;
        }

        default:
            break;
    }
}

void LapConsistencyHud::renderStatistics(float x, float y, float width) {
    const auto dims = getScaledDimensions();

    // Follow PerformanceHud pattern: labels use tertiary, values use secondary
    // No semantic coloring in legend - that's reserved for the graph bars
    unsigned long labelColor = this->getColor(ColorSlot::TERTIARY);
    unsigned long valueColor = this->getColor(ColorSlot::SECONDARY);

    char buffer[32];
    float lineY = y;
    float labelX = x;
    float valueX = x + width;

    // REF: Reference time (what bars are compared against)
    // Shows N/A when reference source is unavailable (e.g., no Record set for this track)
    // Shows placeholder when waiting for data (e.g., AVERAGE mode but no laps completed yet)
    if (m_enabledStats & STAT_REF) {
        int refTime = getReferenceTime();  // This also updates m_referenceAvailable
        addString("REF", labelX, lineY, Justify::LEFT, this->getFont(FontCategory::NORMAL),
                  m_referenceAvailable ? labelColor : this->getColor(ColorSlot::MUTED), dims.fontSize);
        if (refTime > 0) {
            // Have a valid reference time
            formatLapTime(buffer, sizeof(buffer), refTime);
            addString(buffer, valueX, lineY, Justify::RIGHT, this->getFont(FontCategory::DIGITS),
                      valueColor, dims.fontSize);
        } else if (!m_referenceAvailable) {
            // Reference source doesn't exist (e.g., no Record, no Alltime PB)
            addString(Placeholders::NOT_AVAILABLE, valueX, lineY, Justify::RIGHT, this->getFont(FontCategory::DIGITS),
                      this->getColor(ColorSlot::MUTED), dims.fontSize);
        } else {
            // Reference source exists but no data yet (waiting for laps)
            addString(Placeholders::LAP_TIME, valueX, lineY, Justify::RIGHT, this->getFont(FontCategory::DIGITS),
                      this->getColor(ColorSlot::MUTED), dims.fontSize);
        }
        lineY += dims.lineHeightNormal;
    }

    // BEST: Best lap in sample
    if (m_enabledStats & STAT_BEST) {
        addString("BEST", labelX, lineY, Justify::LEFT, this->getFont(FontCategory::NORMAL),
                  labelColor, dims.fontSize);
        if (m_stats.bestMs > 0) {
            formatLapTime(buffer, sizeof(buffer), m_stats.bestMs);
            addString(buffer, valueX, lineY, Justify::RIGHT, this->getFont(FontCategory::DIGITS),
                      valueColor, dims.fontSize);
        } else {
            addString(Placeholders::LAP_TIME, valueX, lineY, Justify::RIGHT, this->getFont(FontCategory::DIGITS),
                      this->getColor(ColorSlot::MUTED), dims.fontSize);
        }
        lineY += dims.lineHeightNormal;
    }

    // AVG: Average lap time
    if (m_enabledStats & STAT_AVG) {
        addString("AVG", labelX, lineY, Justify::LEFT, this->getFont(FontCategory::NORMAL),
                  labelColor, dims.fontSize);
        if (m_stats.averageMs > 0) {
            formatLapTime(buffer, sizeof(buffer), m_stats.averageMs);
            addString(buffer, valueX, lineY, Justify::RIGHT, this->getFont(FontCategory::DIGITS),
                      valueColor, dims.fontSize);
        } else {
            addString(Placeholders::LAP_TIME, valueX, lineY, Justify::RIGHT, this->getFont(FontCategory::DIGITS),
                      this->getColor(ColorSlot::MUTED), dims.fontSize);
        }
        lineY += dims.lineHeightNormal;
    }

    // WORST: Worst lap in sample
    if (m_enabledStats & STAT_WORST) {
        addString("WORST", labelX, lineY, Justify::LEFT, this->getFont(FontCategory::NORMAL),
                  labelColor, dims.fontSize);
        if (m_stats.worstMs > 0) {
            formatLapTime(buffer, sizeof(buffer), m_stats.worstMs);
            addString(buffer, valueX, lineY, Justify::RIGHT, this->getFont(FontCategory::DIGITS),
                      valueColor, dims.fontSize);
        } else {
            addString(Placeholders::LAP_TIME, valueX, lineY, Justify::RIGHT, this->getFont(FontCategory::DIGITS),
                      this->getColor(ColorSlot::MUTED), dims.fontSize);
        }
        lineY += dims.lineHeightNormal;
    }

    // LAST: Most recent lap
    if (m_enabledStats & STAT_LAST) {
        addString("LAST", labelX, lineY, Justify::LEFT, this->getFont(FontCategory::NORMAL),
                  labelColor, dims.fontSize);
        if (m_stats.lastMs > 0) {
            formatLapTime(buffer, sizeof(buffer), m_stats.lastMs);
            addString(buffer, valueX, lineY, Justify::RIGHT, this->getFont(FontCategory::DIGITS),
                      valueColor, dims.fontSize);
        } else {
            addString(Placeholders::LAP_TIME, valueX, lineY, Justify::RIGHT, this->getFont(FontCategory::DIGITS),
                      this->getColor(ColorSlot::MUTED), dims.fontSize);
        }
        lineY += dims.lineHeightNormal;
    }

    // +/-: Standard deviation (consistency metric)
    if (m_enabledStats & STAT_STDDEV) {
        addString("+/-", labelX, lineY, Justify::LEFT, this->getFont(FontCategory::NORMAL),
                  labelColor, dims.fontSize);
        if (m_stats.validLapCount > 1) {
            snprintf(buffer, sizeof(buffer), "%.3fs", m_stats.stdDevMs / 1000.0f);
            addString(buffer, valueX, lineY, Justify::RIGHT, this->getFont(FontCategory::DIGITS),
                      valueColor, dims.fontSize);
        } else {
            addString(Placeholders::GENERIC, valueX, lineY, Justify::RIGHT, this->getFont(FontCategory::DIGITS),
                      this->getColor(ColorSlot::MUTED), dims.fontSize);
        }
        lineY += dims.lineHeightNormal;
    }

    // TREND: Trend indicator (semantic coloring based on direction)
    if (m_enabledStats & STAT_TREND) {
        addString("TREND", labelX, lineY, Justify::LEFT, this->getFont(FontCategory::NORMAL),
                  labelColor, dims.fontSize);
        if (m_stats.validLapCount >= 4) {
            const char* trendText;
            unsigned long trendColor = valueColor;
            if (m_stats.trendDirection > 0) {
                trendText = "Faster";
                trendColor = this->getColor(ColorSlot::POSITIVE);  // Green - improving
            } else if (m_stats.trendDirection < 0) {
                trendText = "Slower";
                trendColor = this->getColor(ColorSlot::NEGATIVE);  // Red - declining
            } else {
                trendText = "Stable";
                // Keep neutral valueColor
            }
            addString(trendText, valueX, lineY, Justify::RIGHT, this->getFont(FontCategory::NORMAL),
                      trendColor, dims.fontSize);
        } else {
            addString(Placeholders::GENERIC, valueX, lineY, Justify::RIGHT, this->getFont(FontCategory::NORMAL),
                      this->getColor(ColorSlot::MUTED), dims.fontSize);
        }
        lineY += dims.lineHeightNormal;
    }

    // CONS: Consistency score (semantic coloring based on score)
    if (m_enabledStats & STAT_CONS) {
        addString("CONS", labelX, lineY, Justify::LEFT, this->getFont(FontCategory::NORMAL),
                  labelColor, dims.fontSize);
        if (m_stats.validLapCount > 1) {
            snprintf(buffer, sizeof(buffer), "%.0f%%", m_stats.consistencyScore);
            // Semantic coloring: positive for high (80%+), neutral for medium, negative for low (<50%)
            unsigned long consColor = valueColor;
            if (m_stats.consistencyScore >= 80.0f) {
                consColor = this->getColor(ColorSlot::POSITIVE);
            } else if (m_stats.consistencyScore < 50.0f) {
                consColor = this->getColor(ColorSlot::NEGATIVE);
            }
            addString(buffer, valueX, lineY, Justify::RIGHT, this->getFont(FontCategory::DIGITS),
                      consColor, dims.fontSize);
        } else {
            addString(Placeholders::GENERIC, valueX, lineY, Justify::RIGHT, this->getFont(FontCategory::DIGITS),
                      this->getColor(ColorSlot::MUTED), dims.fontSize);
        }
    }
}

void LapConsistencyHud::formatLapTime(char* buffer, size_t bufferSize, int timeMs) {
    if (timeMs <= 0) {
        snprintf(buffer, bufferSize, "--:--.---");
        return;
    }

    int totalSeconds = timeMs / 1000;
    int milliseconds = timeMs % 1000;
    int minutes = totalSeconds / 60;
    int seconds = totalSeconds % 60;

    snprintf(buffer, bufferSize, "%d:%02d.%03d", minutes, seconds, milliseconds);
}

void LapConsistencyHud::resetToDefaults() {
    m_bVisible = false;  // Disabled by default - enable via settings
    m_bShowTitle = true;
    setTextureVariant(0);
    m_fBackgroundOpacity = SettingsLimits::DEFAULT_OPACITY;
    m_fScale = 1.0f;
    setPosition(0.0055f, 0.5106f);  // Left side, below ideal lap area

    m_displayMode = DISPLAY_DEFAULT;
    m_referenceMode = ReferenceMode::AVERAGE;
    m_trendMode = TrendMode::LINE;
    m_enabledStats = STAT_DEFAULT;
    m_lapCount = 15;

    // Advanced tuning (INI-only)
    m_consistencyScaleFactor = 20.0f;   // CV of 5% = 0% consistency
    m_trendThresholdPercent = 0.5f;     // 0.5% of average lap time

    setDataDirty();
}
