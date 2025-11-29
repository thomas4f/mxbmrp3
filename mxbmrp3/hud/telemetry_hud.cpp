// ============================================================================
// hud/telemetry_hud.cpp
// Displays real-time bike telemetry inputs (throttle, brakes, clutch, steering)
// ============================================================================
#include "telemetry_hud.h"
#include "../core/plugin_data.h"
#include "../core/plugin_utils.h"
#include "../diagnostics/logger.h"
#include <cmath>
#include <algorithm>
#include <vector>

#undef min
#undef max

using namespace PluginConstants;

TelemetryHud::TelemetryHud() {
    DEBUG_INFO("TelemetryHud initialized");
    setScale(1.0f);
    setDraggable(true);

    // Set defaults to match user configuration
    m_bShowTitle = true;
    m_fBackgroundOpacity = SettingsLimits::DEFAULT_OPACITY;
    setPosition(0.6875f, -0.0666f);

    // Pre-allocate render buffers to avoid reallocations
    m_quads.reserve(1000);   // 1 bg + 4 grid + 4 inputs × 199 line segments = ~1000 quads max
    m_strings.reserve(9);    // Title + up to 4 × (label + value) in duotone = 1 + 8 = 9

    rebuildRenderData();
}

void TelemetryHud::update() {
    // Always rebuild - scrolling graph needs continuous updates at physics rate (100Hz)
    // updateInputTelemetry() marks this dirty every physics callback
    rebuildRenderData();
    clearDataDirty();
    clearLayoutDirty();
}

bool TelemetryHud::handlesDataType(DataChangeType dataType) const {
    return dataType == DataChangeType::InputTelemetry;
}

void TelemetryHud::rebuildRenderData() {
    m_quads.clear();
    m_strings.clear();

    // PERFORMANCE TEST: Skip all calculations when disabled
    if (!ENABLED) return;

    const auto dims = getScaledDimensions();
    const PluginData& pluginData = PluginData::getInstance();
    const HistoryBuffers& history = pluginData.getHistoryBuffers();
    const BikeTelemetryData& bikeTelemetry = pluginData.getBikeTelemetry();

    // Check if viewing player's bike (clutch/suspension data only available for player)
    bool isViewingPlayerBike = (pluginData.getDisplayRaceNum() == pluginData.getPlayerRaceNum());

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
    // Count enabled metrics to determine legend height
    int legendLines = 0;
    if ((m_enabledElements & ELEM_THROTTLE) && showValues) legendLines++;
    if ((m_enabledElements & ELEM_FRONT_BRAKE) && showValues) legendLines++;
    if ((m_enabledElements & ELEM_REAR_BRAKE) && showValues && bikeTelemetry.isValid) legendLines++;  // Rear brake only for player
    if ((m_enabledElements & ELEM_CLUTCH) && showValues && bikeTelemetry.isValid) legendLines++;
    if ((m_enabledElements & ELEM_RPM) && showValues) legendLines++;
    if ((m_enabledElements & ELEM_FRONT_SUSP) && showValues && bikeTelemetry.isValid && bikeTelemetry.frontSuspMaxTravel > 0) legendLines++;  // Front suspension only for player
    if ((m_enabledElements & ELEM_REAR_SUSP) && showValues && bikeTelemetry.isValid && bikeTelemetry.rearSuspMaxTravel > 0) legendLines++;  // Rear suspension only for player
    if ((m_enabledElements & ELEM_FUEL) && showValues && bikeTelemetry.isValid && bikeTelemetry.maxFuel > 0) legendLines++;
    if ((m_enabledElements & ELEM_GEAR) && showValues) legendLines++;
    float legendHeight = legendLines * dims.lineHeightNormal;

    // Content height is max of graph height and legend height
    float contentHeight = showGraphs ? (graphHeight > legendHeight ? graphHeight : legendHeight) : legendHeight;
    float backgroundHeight = dims.paddingV + titleHeight + contentHeight + dims.paddingV;

    setBounds(START_X, START_Y, START_X + backgroundWidth, START_Y + backgroundHeight);

    // Add background quad
    addBackgroundQuad(START_X, START_Y, backgroundWidth, backgroundHeight);

    float contentStartX = START_X + dims.paddingH;
    float contentStartY = START_Y + dims.paddingV;
    float currentY = contentStartY;

    // Title
    addTitleString("Telemetry", contentStartX, currentY, PluginConstants::Justify::LEFT,
        PluginConstants::Fonts::ENTER_SANSMAN, PluginConstants::TextColors::PRIMARY, dims.fontSizeLarge);
    currentY += titleHeight;

    // Side-by-side layout: graph on left (36 chars), gap (1 char), legend on right (9 chars)
    float graphWidth = PluginUtils::calculateMonospaceTextWidth(GRAPH_WIDTH_CHARS, dims.fontSize);
    float legendWidth = PluginUtils::calculateMonospaceTextWidth(LEGEND_WIDTH_CHARS, dims.fontSize);
    float gapWidth = PluginUtils::calculateMonospaceTextWidth(1, dims.fontSize);
    // Position legend: if showing graphs, place after graph + gap; otherwise start at left edge
    float legendStartX = showGraphs ? (contentStartX + graphWidth + gapWidth) : contentStartX;

    // Input Graph - only render if graphs are shown
    if (showGraphs) {
        addCombinedInputGraph(history, bikeTelemetry, contentStartX, currentY, graphWidth, graphHeight, isViewingPlayerBike);
    }

    // Legend (vertical format on right side)
    if (showValues) {
        // Get current values
        float throttlePercent = history.throttle.empty() ? 0.0f : history.throttle.back();
        float frontBrakePercent = history.frontBrake.empty() ? 0.0f : history.frontBrake.back();
        float rearBrakePercent = history.rearBrake.empty() ? 0.0f : history.rearBrake.back();

        // Clutch (only available for player - show 0 when spectating)
        float clutchPercent = (isViewingPlayerBike && !history.clutch.empty()) ? history.clutch.back() : 0.0f;

        float legendY = currentY;  // Start at same Y as graph

        // THR (if enabled)
        if (m_enabledElements & ELEM_THROTTLE) {
            char buffer[16];
            snprintf(buffer, sizeof(buffer), "THR  %3d%%", static_cast<int>(throttlePercent * 100));
            addString(buffer, legendStartX, legendY, PluginConstants::Justify::LEFT,
                PluginConstants::Fonts::ROBOTO_MONO, PluginConstants::TextColors::SECONDARY, dims.fontSize);
            legendY += dims.lineHeightNormal;
        }

        // FBR (front brake - if enabled, always available)
        if (m_enabledElements & ELEM_FRONT_BRAKE) {
            char buffer[16];
            snprintf(buffer, sizeof(buffer), "FBR  %3d%%", static_cast<int>(frontBrakePercent * 100));
            addString(buffer, legendStartX, legendY, PluginConstants::Justify::LEFT,
                PluginConstants::Fonts::ROBOTO_MONO, PluginConstants::TextColors::SECONDARY, dims.fontSize);
            legendY += dims.lineHeightNormal;
        }

        // RBR (rear brake - if enabled and telemetry is valid, rear brake data not available when spectating)
        if ((m_enabledElements & ELEM_REAR_BRAKE) && bikeTelemetry.isValid) {
            char buffer[16];
            snprintf(buffer, sizeof(buffer), "RBR  %3d%%", static_cast<int>(rearBrakePercent * 100));
            addString(buffer, legendStartX, legendY, PluginConstants::Justify::LEFT,
                PluginConstants::Fonts::ROBOTO_MONO, PluginConstants::TextColors::SECONDARY, dims.fontSize);
            legendY += dims.lineHeightNormal;
        }

        // CLU (if enabled and telemetry is valid - clutch data not available when spectating)
        if ((m_enabledElements & ELEM_CLUTCH) && bikeTelemetry.isValid) {
            char buffer[16];
            snprintf(buffer, sizeof(buffer), "CLU  %3d%%", static_cast<int>(clutchPercent * 100));
            addString(buffer, legendStartX, legendY, PluginConstants::Justify::LEFT,
                PluginConstants::Fonts::ROBOTO_MONO, PluginConstants::TextColors::SECONDARY, dims.fontSize);
            legendY += dims.lineHeightNormal;
        }

        // RPM (if enabled)
        if (m_enabledElements & ELEM_RPM) {
            char buffer[16];
            // Clamp RPM to non-negative values
            int displayRpm = std::max(0, bikeTelemetry.rpm);
            snprintf(buffer, sizeof(buffer), "RPM %5d", displayRpm);
            addString(buffer, legendStartX, legendY, PluginConstants::Justify::LEFT,
                PluginConstants::Fonts::ROBOTO_MONO, PluginConstants::TextColors::SECONDARY, dims.fontSize);
            legendY += dims.lineHeightNormal;
        }

        // FSU (front suspension - if enabled and telemetry is valid, suspension data not available when spectating)
        if ((m_enabledElements & ELEM_FRONT_SUSP) && bikeTelemetry.isValid && bikeTelemetry.frontSuspMaxTravel > 0) {
            // Only show suspension data when viewing player's bike
            float frontSuspPercent = (isViewingPlayerBike && !history.frontSusp.empty()) ? history.frontSusp.back() : 0.0f;
            char buffer[16];
            snprintf(buffer, sizeof(buffer), "FSU  %3d%%", static_cast<int>(frontSuspPercent * 100));
            addString(buffer, legendStartX, legendY, PluginConstants::Justify::LEFT,
                PluginConstants::Fonts::ROBOTO_MONO, PluginConstants::TextColors::SECONDARY, dims.fontSize);
            legendY += dims.lineHeightNormal;
        }

        // RSU (rear suspension - if enabled and telemetry is valid, suspension data not available when spectating)
        if ((m_enabledElements & ELEM_REAR_SUSP) && bikeTelemetry.isValid && bikeTelemetry.rearSuspMaxTravel > 0) {
            // Only show suspension data when viewing player's bike
            float rearSuspPercent = (isViewingPlayerBike && !history.rearSusp.empty()) ? history.rearSusp.back() : 0.0f;
            char buffer[16];
            snprintf(buffer, sizeof(buffer), "RSU  %3d%%", static_cast<int>(rearSuspPercent * 100));
            addString(buffer, legendStartX, legendY, PluginConstants::Justify::LEFT,
                PluginConstants::Fonts::ROBOTO_MONO, PluginConstants::TextColors::SECONDARY, dims.fontSize);
            legendY += dims.lineHeightNormal;
        }

        // FUEL (if enabled and telemetry is valid - fuel data not available when spectating)
        if ((m_enabledElements & ELEM_FUEL) && bikeTelemetry.isValid && bikeTelemetry.maxFuel > 0) {
            char buffer[16];
            snprintf(buffer, sizeof(buffer), "FUE %.2fL", bikeTelemetry.fuel);
            addString(buffer, legendStartX, legendY, PluginConstants::Justify::LEFT,
                PluginConstants::Fonts::ROBOTO_MONO, PluginConstants::TextColors::SECONDARY, dims.fontSize);
            legendY += dims.lineHeightNormal;
        }

        // GEAR (if enabled - always available)
        if (m_enabledElements & ELEM_GEAR) {
            char buffer[16];
            // Display gear as N for neutral, number for other gears
            if (bikeTelemetry.gear == 0) {
                snprintf(buffer, sizeof(buffer), "GEA     N");
            } else {
                snprintf(buffer, sizeof(buffer), "GEA     %d", bikeTelemetry.gear);
            }
            addString(buffer, legendStartX, legendY, PluginConstants::Justify::LEFT,
                PluginConstants::Fonts::ROBOTO_MONO, PluginConstants::TextColors::SECONDARY, dims.fontSize);
        }
    }
}

void TelemetryHud::addCombinedInputGraph(const HistoryBuffers& history, const BikeTelemetryData& bikeTelemetry,
                                                float x, float y, float width, float height, bool isViewingPlayerBike) {
    const auto dims = getScaledDimensions();

    // Grid lines (0-100% range, drawn first so dots appear on top)
    float gridLineThickness = 0.001f * getScale();  // ~1px at 1080p for subtle grid lines
    constexpr unsigned long gridColor = PluginConstants::TextColors::MUTED;  // Muted gray for subtle grid lines

    // Grid line percentages (defined as constants in header)
    const float gridValues[] = {
        GRID_LINE_80_PERCENT,
        GRID_LINE_60_PERCENT,
        GRID_LINE_40_PERCENT,
        GRID_LINE_20_PERCENT
    };
    for (float gridValue : gridValues) {
        float gridY = y + height - (gridValue * height);
        addHorizontalGridLine(x, gridY, width, gridColor, gridLineThickness);
    }

    // Draw input histories as line graphs (only enabled inputs)
    struct InputLine {
        const std::deque<float>& data;
        unsigned long color;
        const char* label;
    };

    // Match performance graph logic: spacing based on max history, not current size
    float pointSpacing = width / (HistoryBuffers::MAX_TELEMETRY_HISTORY - 1);
    float lineThickness = 0.002f * getScale();  // Line thickness for graph rendering

    // PERFORMANCE OPTIMIZATION: Merge 4 separate loops into single pass
    // Previous: 4 × O(199) = 796 iterations per frame
    // New: 1 × O(199) = 199 iterations per frame (75% reduction)
    // At 144 fps: reduces 114,624 iterations/sec to 28,656 iterations/sec
    //
    // Render inputs in order within each iteration: brake, clutch, RPM, then throttle
    // This maintains the visual appearance (throttle appears on top) while reducing iterations
    for (size_t i = 0; i < HistoryBuffers::MAX_TELEMETRY_HISTORY - 1; ++i) {
        // Pre-calculate shared position values (used by all graphs)
        float x1 = x + i * pointSpacing;
        float x2 = x + (i + 1) * pointSpacing;

        // Front brake graph (always available - available for player and spectated riders)
        if (m_enabledElements & ELEM_FRONT_BRAKE) {
            const std::deque<float>& data = history.frontBrake;
            if (i < data.size() && (i + 1) < data.size()) {
                float value1 = std::max(0.0f, std::min(1.0f, data[i]));
                float value2 = std::max(0.0f, std::min(1.0f, data[i + 1]));

                if (value1 >= 0.01f || value2 >= 0.01f) {
                    float y1 = y + height - (value1 * height);
                    float y2 = y + height - (value2 * height);
                    addLineSegment(x1, y1, x2, y2, PluginConstants::SemanticColors::FRONT_BRAKE, lineThickness);
                }
            }
        }

        // Rear brake graph (only render if telemetry is valid - rear brake data not available when spectating)
        if ((m_enabledElements & ELEM_REAR_BRAKE) && bikeTelemetry.isValid) {
            const std::deque<float>& data = history.rearBrake;
            if (i < data.size() && (i + 1) < data.size()) {
                float value1 = std::max(0.0f, std::min(1.0f, data[i]));
                float value2 = std::max(0.0f, std::min(1.0f, data[i + 1]));

                if (value1 >= 0.01f || value2 >= 0.01f) {
                    float y1 = y + height - (value1 * height);
                    float y2 = y + height - (value2 * height);
                    addLineSegment(x1, y1, x2, y2, PluginConstants::SemanticColors::REAR_BRAKE, lineThickness);
                }
            }
        }

        // Clutch graph (only render if viewing player's bike - clutch data not available when spectating)
        if ((m_enabledElements & ELEM_CLUTCH) && isViewingPlayerBike) {
            const std::deque<float>& data = history.clutch;
            if (i < data.size() && (i + 1) < data.size()) {
                float value1 = std::max(0.0f, std::min(1.0f, data[i]));
                float value2 = std::max(0.0f, std::min(1.0f, data[i + 1]));

                if (value1 >= 0.01f || value2 >= 0.01f) {
                    float y1 = y + height - (value1 * height);
                    float y2 = y + height - (value2 * height);
                    addLineSegment(x1, y1, x2, y2, PluginConstants::SemanticColors::CLUTCH, lineThickness);
                }
            }
        }

        // RPM graph
        if (m_enabledElements & ELEM_RPM) {
            const std::deque<float>& data = history.rpm;
            if (i < data.size() && (i + 1) < data.size()) {
                float value1 = std::max(0.0f, std::min(1.0f, data[i]));
                float value2 = std::max(0.0f, std::min(1.0f, data[i + 1]));

                if (value1 >= 0.01f || value2 >= 0.01f) {
                    float y1 = y + height - (value1 * height);
                    float y2 = y + height - (value2 * height);
                    addLineSegment(x1, y1, x2, y2, PluginConstants::TextColors::SECONDARY, lineThickness);
                }
            }
        }

        // Fuel graph (only render if telemetry is valid and maxFuel > 0 - fuel data not available when spectating)
        if ((m_enabledElements & ELEM_FUEL) && bikeTelemetry.isValid && bikeTelemetry.maxFuel > 0) {
            const std::deque<float>& data = history.fuel;
            if (i < data.size() && (i + 1) < data.size()) {
                float value1 = std::max(0.0f, std::min(1.0f, data[i]));
                float value2 = std::max(0.0f, std::min(1.0f, data[i + 1]));

                if (value1 >= 0.01f || value2 >= 0.01f) {
                    float y1 = y + height - (value1 * height);
                    float y2 = y + height - (value2 * height);
                    addLineSegment(x1, y1, x2, y2, PluginConstants::Colors::YELLOW, lineThickness);
                }
            }
        }

        // Front suspension graph (only render if viewing player's bike - suspension data not available when spectating)
        if ((m_enabledElements & ELEM_FRONT_SUSP) && isViewingPlayerBike && bikeTelemetry.frontSuspMaxTravel > 0) {
            const std::deque<float>& data = history.frontSusp;
            if (i < data.size() && (i + 1) < data.size()) {
                float value1 = std::max(0.0f, std::min(1.0f, data[i]));
                float value2 = std::max(0.0f, std::min(1.0f, data[i + 1]));

                if (value1 >= 0.01f || value2 >= 0.01f) {
                    float y1 = y + height - (value1 * height);
                    float y2 = y + height - (value2 * height);
                    addLineSegment(x1, y1, x2, y2, PluginConstants::SemanticColors::FRONT_SUSP, lineThickness);
                }
            }
        }

        // Rear suspension graph (only render if viewing player's bike - suspension data not available when spectating)
        if ((m_enabledElements & ELEM_REAR_SUSP) && isViewingPlayerBike && bikeTelemetry.rearSuspMaxTravel > 0) {
            const std::deque<float>& data = history.rearSusp;
            if (i < data.size() && (i + 1) < data.size()) {
                float value1 = std::max(0.0f, std::min(1.0f, data[i]));
                float value2 = std::max(0.0f, std::min(1.0f, data[i + 1]));

                if (value1 >= 0.01f || value2 >= 0.01f) {
                    float y1 = y + height - (value1 * height);
                    float y2 = y + height - (value2 * height);
                    addLineSegment(x1, y1, x2, y2, PluginConstants::SemanticColors::REAR_SUSP, lineThickness);
                }
            }
        }

        // Gear graph (always available)
        if (m_enabledElements & ELEM_GEAR) {
            const std::deque<float>& data = history.gear;
            if (i < data.size() && (i + 1) < data.size()) {
                float value1 = std::max(0.0f, std::min(1.0f, data[i]));
                float value2 = std::max(0.0f, std::min(1.0f, data[i + 1]));

                // Render even at 0 (neutral) for gear
                float y1 = y + height - (value1 * height);
                float y2 = y + height - (value2 * height);
                addLineSegment(x1, y1, x2, y2, PluginConstants::SemanticColors::GEAR, lineThickness);
            }
        }

        // Throttle graph (rendered last within each iteration so it appears on top)
        if (m_enabledElements & ELEM_THROTTLE) {
            const std::deque<float>& data = history.throttle;
            if (i < data.size() && (i + 1) < data.size()) {
                float value1 = std::max(0.0f, std::min(1.0f, data[i]));
                float value2 = std::max(0.0f, std::min(1.0f, data[i + 1]));

                if (value1 >= 0.01f || value2 >= 0.01f) {
                    float y1 = y + height - (value1 * height);
                    float y2 = y + height - (value2 * height);
                    addLineSegment(x1, y1, x2, y2, PluginConstants::SemanticColors::THROTTLE, lineThickness);
                }
            }
        }
    }
}

void TelemetryHud::resetToDefaults() {
    m_bVisible = true;
    m_bShowTitle = true;
    m_bShowBackgroundTexture = false;  // No texture by default
    m_fBackgroundOpacity = SettingsLimits::DEFAULT_OPACITY;
    m_fScale = 1.0f;
    setPosition(0.6875f, -0.0666f);
    m_enabledElements = ELEM_DEFAULT;
    m_displayMode = DISPLAY_DEFAULT;
    setDataDirty();
}
