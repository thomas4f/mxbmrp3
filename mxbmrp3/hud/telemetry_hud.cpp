// ============================================================================
// hud/telemetry_hud.cpp
// Displays real-time bike telemetry inputs (throttle, brakes, clutch, steering)
// ============================================================================
#include "telemetry_hud.h"
#include "../core/plugin_data.h"
#include "../core/plugin_constants.h"
#include "../core/plugin_utils.h"
#include "../core/color_config.h"
#include "../diagnostics/logger.h"
#include <cmath>
#include <algorithm>
#include <vector>

using namespace PluginConstants;

TelemetryHud::TelemetryHud() {
    DEBUG_INFO("TelemetryHud initialized");
    setScale(1.0f);
    setDraggable(true);

    // Set defaults to match user configuration
    m_bShowTitle = true;
    m_fBackgroundOpacity = SettingsLimits::DEFAULT_OPACITY;
    setPosition(0.6875f, -0.0777f);

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

    // Full telemetry data (rear brake, clutch, steer, suspension) is ONLY available when ON_TRACK
    // because RunTelemetry() callback with SPluginsBikeData_t only fires when player is on track.
    // During SPECTATE/REPLAY, only limited SPluginsRaceVehicleData_t is available (throttle, front brake, RPM, gear).
    bool hasFullTelemetry = (pluginData.getDrawState() == PluginConstants::ViewState::ON_TRACK);

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
    // Count enabled metrics to determine legend height - show all enabled even if data unavailable
    int legendLines = 0;
    if ((m_enabledElements & ELEM_THROTTLE) && showValues) legendLines++;
    if ((m_enabledElements & ELEM_FRONT_BRAKE) && showValues) legendLines++;
    if ((m_enabledElements & ELEM_REAR_BRAKE) && showValues) legendLines++;
    if ((m_enabledElements & ELEM_CLUTCH) && showValues) legendLines++;
    if ((m_enabledElements & ELEM_RPM) && showValues) legendLines++;
    if ((m_enabledElements & ELEM_FRONT_SUSP) && showValues) legendLines++;
    if ((m_enabledElements & ELEM_REAR_SUSP) && showValues) legendLines++;
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
        PluginConstants::Fonts::ENTER_SANSMAN, ColorConfig::getInstance().getPrimary(), dims.fontSizeLarge);
    currentY += titleHeight;

    // Side-by-side layout: graph on left (36 chars), gap (1 char), legend on right (9 chars)
    float graphWidth = PluginUtils::calculateMonospaceTextWidth(GRAPH_WIDTH_CHARS, dims.fontSize);
    float legendWidth = PluginUtils::calculateMonospaceTextWidth(LEGEND_WIDTH_CHARS, dims.fontSize);
    float gapWidth = PluginUtils::calculateMonospaceTextWidth(1, dims.fontSize);
    // Position legend: if showing graphs, place after graph + gap; otherwise start at left edge
    float legendStartX = showGraphs ? (contentStartX + graphWidth + gapWidth) : contentStartX;

    // Input Graph - only render if graphs are shown
    if (showGraphs) {
        addCombinedInputGraph(history, bikeTelemetry, contentStartX, currentY, graphWidth, graphHeight, hasFullTelemetry);
    }

    // Legend (vertical format on right side)
    if (showValues) {
        // Get current values
        float throttlePercent = history.throttle.empty() ? 0.0f : history.throttle.back();
        float frontBrakePercent = history.frontBrake.empty() ? 0.0f : history.frontBrake.back();
        float rearBrakePercent = history.rearBrake.empty() ? 0.0f : history.rearBrake.back();

        // Clutch (only available when ON_TRACK - show 0 when spectating/replay)
        float clutchPercent = (hasFullTelemetry && !history.clutch.empty()) ? history.clutch.back() : 0.0f;

        float legendY = currentY;  // Start at same Y as graph
        float valueX = legendStartX + PluginUtils::calculateMonospaceTextWidth(4, dims.fontSize);  // After "XXX "

        // THR (if enabled) - color matches throttle graph
        if (m_enabledElements & ELEM_THROTTLE) {
            addString("THR", legendStartX, legendY, PluginConstants::Justify::LEFT,
                PluginConstants::Fonts::ROBOTO_MONO, PluginConstants::SemanticColors::THROTTLE, dims.fontSize);
            char buffer[16];
            snprintf(buffer, sizeof(buffer), "%4d%%", static_cast<int>(throttlePercent * 100));
            addString(buffer, valueX, legendY, PluginConstants::Justify::LEFT,
                PluginConstants::Fonts::ROBOTO_MONO, ColorConfig::getInstance().getSecondary(), dims.fontSize);
            legendY += dims.lineHeightNormal;
        }

        // FBR (front brake - if enabled, always available) - color matches front brake graph
        if (m_enabledElements & ELEM_FRONT_BRAKE) {
            addString("FBR", legendStartX, legendY, PluginConstants::Justify::LEFT,
                PluginConstants::Fonts::ROBOTO_MONO, PluginConstants::SemanticColors::FRONT_BRAKE, dims.fontSize);
            char buffer[16];
            snprintf(buffer, sizeof(buffer), "%4d%%", static_cast<int>(frontBrakePercent * 100));
            addString(buffer, valueX, legendY, PluginConstants::Justify::LEFT,
                PluginConstants::Fonts::ROBOTO_MONO, ColorConfig::getInstance().getSecondary(), dims.fontSize);
            legendY += dims.lineHeightNormal;
        }

        // RBR (rear brake - only available when ON_TRACK, not in spectate/replay) - color matches rear brake graph
        if (m_enabledElements & ELEM_REAR_BRAKE) {
            unsigned long labelColor = hasFullTelemetry ? PluginConstants::SemanticColors::REAR_BRAKE : ColorConfig::getInstance().getMuted();
            addString("RBR", legendStartX, legendY, PluginConstants::Justify::LEFT,
                PluginConstants::Fonts::ROBOTO_MONO, labelColor, dims.fontSize);
            char buffer[16];
            if (hasFullTelemetry) {
                snprintf(buffer, sizeof(buffer), "%4d%%", static_cast<int>(rearBrakePercent * 100));
                addString(buffer, valueX, legendY, PluginConstants::Justify::LEFT,
                    PluginConstants::Fonts::ROBOTO_MONO, ColorConfig::getInstance().getSecondary(), dims.fontSize);
            } else {
                addString("  N/A", valueX, legendY, PluginConstants::Justify::LEFT,
                    PluginConstants::Fonts::ROBOTO_MONO, ColorConfig::getInstance().getMuted(), dims.fontSize);
            }
            legendY += dims.lineHeightNormal;
        }

        // CLU (only available when ON_TRACK, not in spectate/replay) - color matches clutch graph
        if (m_enabledElements & ELEM_CLUTCH) {
            unsigned long labelColor = hasFullTelemetry ? PluginConstants::SemanticColors::CLUTCH : ColorConfig::getInstance().getMuted();
            addString("CLU", legendStartX, legendY, PluginConstants::Justify::LEFT,
                PluginConstants::Fonts::ROBOTO_MONO, labelColor, dims.fontSize);
            char buffer[16];
            if (hasFullTelemetry) {
                snprintf(buffer, sizeof(buffer), "%4d%%", static_cast<int>(clutchPercent * 100));
                addString(buffer, valueX, legendY, PluginConstants::Justify::LEFT,
                    PluginConstants::Fonts::ROBOTO_MONO, ColorConfig::getInstance().getSecondary(), dims.fontSize);
            } else {
                addString("  N/A", valueX, legendY, PluginConstants::Justify::LEFT,
                    PluginConstants::Fonts::ROBOTO_MONO, ColorConfig::getInstance().getMuted(), dims.fontSize);
            }
            legendY += dims.lineHeightNormal;
        }

        // RPM (if enabled) - uses fixed gray to match bars widget
        if (m_enabledElements & ELEM_RPM) {
            addString("RPM", legendStartX, legendY, PluginConstants::Justify::LEFT,
                PluginConstants::Fonts::ROBOTO_MONO, ColorPalette::GRAY, dims.fontSize);
            char buffer[16];
            int displayRpm = std::max(0, bikeTelemetry.rpm);
            snprintf(buffer, sizeof(buffer), "%5d", displayRpm);
            addString(buffer, valueX, legendY, PluginConstants::Justify::LEFT,
                PluginConstants::Fonts::ROBOTO_MONO, ColorPalette::GRAY, dims.fontSize);
            legendY += dims.lineHeightNormal;
        }

        // FSU (front suspension - only available when ON_TRACK, not in spectate/replay) - color matches front susp graph
        if (m_enabledElements & ELEM_FRONT_SUSP) {
            bool hasSuspData = hasFullTelemetry && bikeTelemetry.frontSuspMaxTravel > 0;
            unsigned long labelColor = hasSuspData ? PluginConstants::SemanticColors::FRONT_SUSP : ColorConfig::getInstance().getMuted();
            addString("FSU", legendStartX, legendY, PluginConstants::Justify::LEFT,
                PluginConstants::Fonts::ROBOTO_MONO, labelColor, dims.fontSize);
            if (hasSuspData) {
                float frontSuspPercent = (!history.frontSusp.empty()) ? history.frontSusp.back() : 0.0f;
                char buffer[16];
                snprintf(buffer, sizeof(buffer), "%4d%%", static_cast<int>(frontSuspPercent * 100));
                addString(buffer, valueX, legendY, PluginConstants::Justify::LEFT,
                    PluginConstants::Fonts::ROBOTO_MONO, ColorConfig::getInstance().getSecondary(), dims.fontSize);
            } else {
                addString("  N/A", valueX, legendY, PluginConstants::Justify::LEFT,
                    PluginConstants::Fonts::ROBOTO_MONO, ColorConfig::getInstance().getMuted(), dims.fontSize);
            }
            legendY += dims.lineHeightNormal;
        }

        // RSU (rear suspension - only available when ON_TRACK, not in spectate/replay) - color matches rear susp graph
        if (m_enabledElements & ELEM_REAR_SUSP) {
            bool hasSuspData = hasFullTelemetry && bikeTelemetry.rearSuspMaxTravel > 0;
            unsigned long labelColor = hasSuspData ? PluginConstants::SemanticColors::REAR_SUSP : ColorConfig::getInstance().getMuted();
            addString("RSU", legendStartX, legendY, PluginConstants::Justify::LEFT,
                PluginConstants::Fonts::ROBOTO_MONO, labelColor, dims.fontSize);
            if (hasSuspData) {
                float rearSuspPercent = (!history.rearSusp.empty()) ? history.rearSusp.back() : 0.0f;
                char buffer[16];
                snprintf(buffer, sizeof(buffer), "%4d%%", static_cast<int>(rearSuspPercent * 100));
                addString(buffer, valueX, legendY, PluginConstants::Justify::LEFT,
                    PluginConstants::Fonts::ROBOTO_MONO, ColorConfig::getInstance().getSecondary(), dims.fontSize);
            } else {
                addString("  N/A", valueX, legendY, PluginConstants::Justify::LEFT,
                    PluginConstants::Fonts::ROBOTO_MONO, ColorConfig::getInstance().getMuted(), dims.fontSize);
            }
            legendY += dims.lineHeightNormal;
        }

        // GEAR (if enabled - always available) - color matches gear graph
        if (m_enabledElements & ELEM_GEAR) {
            addString("GEA", legendStartX, legendY, PluginConstants::Justify::LEFT,
                PluginConstants::Fonts::ROBOTO_MONO, PluginConstants::SemanticColors::GEAR, dims.fontSize);
            char buffer[16];
            if (bikeTelemetry.gear == 0) {
                snprintf(buffer, sizeof(buffer), "    N");
            } else {
                snprintf(buffer, sizeof(buffer), "    %d", bikeTelemetry.gear);
            }
            addString(buffer, valueX, legendY, PluginConstants::Justify::LEFT,
                PluginConstants::Fonts::ROBOTO_MONO, ColorConfig::getInstance().getSecondary(), dims.fontSize);
        }
    }
}

void TelemetryHud::addCombinedInputGraph(const HistoryBuffers& history, const BikeTelemetryData& bikeTelemetry,
                                                float x, float y, float width, float height, bool hasFullTelemetry) {
    const auto dims = getScaledDimensions();

    // Grid lines (0-100% range, drawn first so dots appear on top)
    float gridLineThickness = 0.001f * getScale();  // ~1px at 1080p for subtle grid lines
    unsigned long gridColor = ColorConfig::getInstance().getMuted();  // Muted gray for subtle grid lines

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

        // Rear brake graph (only available when ON_TRACK, not in spectate/replay)
        if ((m_enabledElements & ELEM_REAR_BRAKE) && hasFullTelemetry) {
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

        // Clutch graph (only available when ON_TRACK, not in spectate/replay)
        if ((m_enabledElements & ELEM_CLUTCH) && hasFullTelemetry) {
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
                    addLineSegment(x1, y1, x2, y2, ColorPalette::GRAY, lineThickness);
                }
            }
        }

        // Front suspension graph (only available when ON_TRACK, not in spectate/replay)
        if ((m_enabledElements & ELEM_FRONT_SUSP) && hasFullTelemetry && bikeTelemetry.frontSuspMaxTravel > 0) {
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

        // Rear suspension graph (only available when ON_TRACK, not in spectate/replay)
        if ((m_enabledElements & ELEM_REAR_SUSP) && hasFullTelemetry && bikeTelemetry.rearSuspMaxTravel > 0) {
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
    setPosition(0.6875f, -0.0777f);
    m_enabledElements = ELEM_DEFAULT;
    m_displayMode = DISPLAY_DEFAULT;
    setDataDirty();
}
