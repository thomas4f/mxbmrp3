// ============================================================================
// hud/rumble_hud.cpp
// Displays real-time controller rumble motor outputs and effect values
// ============================================================================
#include "rumble_hud.h"
#include "../core/plugin_data.h"
#include "../core/plugin_utils.h"
#include "../core/color_config.h"
#include "../core/xinput_reader.h"
#include "../diagnostics/logger.h"
#include <cmath>
#include <algorithm>

using namespace PluginConstants;

RumbleHud::RumbleHud() {
    DEBUG_INFO("RumbleHud initialized");
    setScale(1.0f);
    setDraggable(true);

    // Set defaults
    m_bVisible = false;  // Disabled by default (debug/tuning HUD)
    m_bShowTitle = true;
    m_bShowBackgroundTexture = false;  // No texture by default
    m_fBackgroundOpacity = SettingsLimits::DEFAULT_OPACITY;
    setPosition(START_X, START_Y);

    // Pre-allocate render buffers
    m_quads.reserve(500);   // Line segments for graphs
    m_strings.reserve(20);  // Title + labels

    rebuildRenderData();
}

void RumbleHud::update() {
    // Always rebuild - rumble values update every frame
    rebuildRenderData();
    clearDataDirty();
    clearLayoutDirty();
}

bool RumbleHud::handlesDataType(DataChangeType dataType) const {
    // Update on telemetry changes (same rate as rumble updates)
    return dataType == DataChangeType::InputTelemetry;
}

void RumbleHud::resetToDefaults() {
    m_bVisible = false;
    m_bShowTitle = true;
    m_bShowBackgroundTexture = false;  // No texture by default
    m_fBackgroundOpacity = SettingsLimits::DEFAULT_OPACITY;
    setPosition(START_X, START_Y);
    setScale(1.0f);
    setDataDirty();
}

void RumbleHud::addHistoryGraph(const std::deque<float>& history, unsigned long color,
                                 float x, float y, float width, float height,
                                 float lineThickness, size_t maxHistory) {
    if (history.size() < 2) return;

    // Calculate point spacing based on max history size for consistent graph width
    float pointSpacing = width / (maxHistory - 1);

    // Draw line segments connecting consecutive points
    for (size_t i = 0; i < history.size() - 1; ++i) {
        float value1 = std::max(0.0f, std::min(1.0f, history[i]));
        float value2 = std::max(0.0f, std::min(1.0f, history[i + 1]));

        // Skip segments where both values are near zero
        if (value1 < 0.01f && value2 < 0.01f) continue;

        float x1 = x + i * pointSpacing;
        float x2 = x + (i + 1) * pointSpacing;
        float y1 = y + height - (value1 * height);
        float y2 = y + height - (value2 * height);

        addLineSegment(x1, y1, x2, y2, color, lineThickness);
    }
}

void RumbleHud::rebuildRenderData() {
    m_quads.clear();
    m_strings.clear();

    const auto dims = getScaledDimensions();
    const XInputReader& xinput = XInputReader::getInstance();
    const RumbleConfig& config = xinput.getRumbleConfig();

    // Calculate dimensions
    float graphWidth = PluginUtils::calculateMonospaceTextWidth(GRAPH_WIDTH_CHARS, dims.fontSize);
    float legendWidth = PluginUtils::calculateMonospaceTextWidth(LEGEND_WIDTH_CHARS, dims.fontSize);
    float gapWidth = PluginUtils::calculateMonospaceTextWidth(1, dims.fontSize);
    float backgroundWidth = PluginUtils::calculateMonospaceTextWidth(BACKGROUND_WIDTH_CHARS, dims.fontSize)
        + dims.paddingH + dims.paddingH;
    float graphHeight = GRAPH_HEIGHT_LINES * dims.lineHeightNormal;

    // Calculate legend height (count enabled items)
    int legendLines = 2;  // Always show Heavy and Light motors
    if (config.suspensionEffect.isEnabled()) legendLines++;
    if (config.wheelspinEffect.isEnabled()) legendLines++;
    if (config.brakeLockupEffect.isEnabled()) legendLines++;
    if (config.wheelieEffect.isEnabled()) legendLines++;
    if (config.rpmEffect.isEnabled()) legendLines++;
    if (config.slideEffect.isEnabled()) legendLines++;
    if (config.surfaceEffect.isEnabled()) legendLines++;
    if (config.steerEffect.isEnabled()) legendLines++;
    float legendHeight = legendLines * dims.lineHeightNormal;

    // Height: title + max(graph height, legend height)
    float titleHeight = m_bShowTitle ? dims.lineHeightLarge : 0.0f;
    float contentHeight = graphHeight > legendHeight ? graphHeight : legendHeight;
    float backgroundHeight = dims.paddingV + titleHeight + contentHeight + dims.paddingV;

    setBounds(START_X, START_Y, START_X + backgroundWidth, START_Y + backgroundHeight);

    // Add background quad
    addBackgroundQuad(START_X, START_Y, backgroundWidth, backgroundHeight);

    float contentStartX = START_X + dims.paddingH;
    float contentStartY = START_Y + dims.paddingV;
    float currentY = contentStartY;

    // Title
    if (m_bShowTitle) {
        addTitleString("Forces", contentStartX, currentY, Justify::LEFT,
            Fonts::ENTER_SANSMAN, ColorConfig::getInstance().getPrimary(), dims.fontSizeLarge);
        currentY += titleHeight;
    }

    // Colors for motors and effects
    unsigned long heavyColor = PluginUtils::makeColor(255, 100, 100, 230);  // Red-ish for heavy motor
    unsigned long lightColor = PluginUtils::makeColor(100, 200, 255, 230);  // Blue-ish for light motor
    unsigned long bumpsColor = SemanticColors::FRONT_SUSP;    // Purple for bumps/suspension
    unsigned long wheelColor = SemanticColors::THROTTLE;      // Green
    unsigned long lockupColor = SemanticColors::FRONT_BRAKE;  // Red
    unsigned long wheelieColor = PluginUtils::makeColor(50, 220, 220, 230); // Cyan for wheelie
    unsigned long rpmColor = ColorPalette::GRAY;              // Gray
    unsigned long slideColor = PluginUtils::makeColor(255, 200, 50, 230);   // Orange/yellow for lateral slide
    unsigned long terrainColor = PluginUtils::makeColor(139, 90, 43, 230); // Brown for terrain/surface
    unsigned long steerColor = PluginUtils::makeColor(180, 100, 220, 230);  // Purple-ish for steer torque

    // === LEFT SIDE: Graph ===
    float graphStartX = contentStartX;
    float graphStartY = currentY;

    // Grid lines (20%, 40%, 60%, 80%)
    unsigned long gridColor = ColorConfig::getInstance().getMuted();
    float gridLineThickness = 0.001f * getScale();

    const float gridValues[] = {
        GRID_LINE_80_PERCENT,
        GRID_LINE_60_PERCENT,
        GRID_LINE_40_PERCENT,
        GRID_LINE_20_PERCENT
    };
    for (float gridValue : gridValues) {
        float gridY = graphStartY + graphHeight - (gridValue * graphHeight);
        addHorizontalGridLine(graphStartX, gridY, graphWidth, gridColor, gridLineThickness);
    }

    // Draw all graphs overlaid
    float lineThickness = 0.002f * getScale();
    size_t maxHistory = XInputReader::MAX_RUMBLE_HISTORY;

    // Effects first (underneath motors)
    if (config.suspensionEffect.isEnabled()) {
        addHistoryGraph(xinput.getSuspensionHistory(), bumpsColor,
                        graphStartX, graphStartY, graphWidth, graphHeight, lineThickness, maxHistory);
    }
    if (config.wheelspinEffect.isEnabled()) {
        addHistoryGraph(xinput.getWheelspinHistory(), wheelColor,
                        graphStartX, graphStartY, graphWidth, graphHeight, lineThickness, maxHistory);
    }
    if (config.brakeLockupEffect.isEnabled()) {
        addHistoryGraph(xinput.getLockupHistory(), lockupColor,
                        graphStartX, graphStartY, graphWidth, graphHeight, lineThickness, maxHistory);
    }
    if (config.wheelieEffect.isEnabled()) {
        addHistoryGraph(xinput.getWheelieHistory(), wheelieColor,
                        graphStartX, graphStartY, graphWidth, graphHeight, lineThickness, maxHistory);
    }
    if (config.rpmEffect.isEnabled()) {
        addHistoryGraph(xinput.getRpmHistory(), rpmColor,
                        graphStartX, graphStartY, graphWidth, graphHeight, lineThickness, maxHistory);
    }
    if (config.slideEffect.isEnabled()) {
        addHistoryGraph(xinput.getSlideHistory(), slideColor,
                        graphStartX, graphStartY, graphWidth, graphHeight, lineThickness, maxHistory);
    }
    if (config.surfaceEffect.isEnabled()) {
        addHistoryGraph(xinput.getSurfaceHistory(), terrainColor,
                        graphStartX, graphStartY, graphWidth, graphHeight, lineThickness, maxHistory);
    }
    if (config.steerEffect.isEnabled()) {
        addHistoryGraph(xinput.getSteerHistory(), steerColor,
                        graphStartX, graphStartY, graphWidth, graphHeight, lineThickness, maxHistory);
    }

    // Motor outputs on top
    addHistoryGraph(xinput.getHeavyMotorHistory(), heavyColor,
                    graphStartX, graphStartY, graphWidth, graphHeight, lineThickness, maxHistory);
    addHistoryGraph(xinput.getLightMotorHistory(), lightColor,
                    graphStartX, graphStartY, graphWidth, graphHeight, lineThickness, maxHistory);

    // === RIGHT SIDE: Legend ===
    float legendStartX = contentStartX + graphWidth + gapWidth;
    float legendY = currentY;
    float valueX = legendStartX + PluginUtils::calculateMonospaceTextWidth(4, dims.fontSize);  // After "XXX "

    // Heavy motor
    addString("HVY", legendStartX, legendY, Justify::LEFT,
        Fonts::ROBOTO_MONO, heavyColor, dims.fontSize);
    char buffer[16];
    snprintf(buffer, sizeof(buffer), "%4d%%", static_cast<int>(xinput.getLastHeavyMotor() * 100));
    addString(buffer, valueX, legendY, Justify::LEFT,
        Fonts::ROBOTO_MONO, ColorConfig::getInstance().getSecondary(), dims.fontSize);
    legendY += dims.lineHeightNormal;

    // Light motor
    addString("LGT", legendStartX, legendY, Justify::LEFT,
        Fonts::ROBOTO_MONO, lightColor, dims.fontSize);
    snprintf(buffer, sizeof(buffer), "%4d%%", static_cast<int>(xinput.getLastLightMotor() * 100));
    addString(buffer, valueX, legendY, Justify::LEFT,
        Fonts::ROBOTO_MONO, ColorConfig::getInstance().getSecondary(), dims.fontSize);
    legendY += dims.lineHeightNormal;

    // Bumps/Suspension effect
    if (config.suspensionEffect.isEnabled()) {
        addString("BMP", legendStartX, legendY, Justify::LEFT,
            Fonts::ROBOTO_MONO, bumpsColor, dims.fontSize);
        snprintf(buffer, sizeof(buffer), "%4d%%", static_cast<int>(xinput.getLastSuspensionRumble() * 100));
        addString(buffer, valueX, legendY, Justify::LEFT,
            Fonts::ROBOTO_MONO, ColorConfig::getInstance().getSecondary(), dims.fontSize);
        legendY += dims.lineHeightNormal;
    }

    // Spin effect
    if (config.wheelspinEffect.isEnabled()) {
        addString("SPN", legendStartX, legendY, Justify::LEFT,
            Fonts::ROBOTO_MONO, wheelColor, dims.fontSize);
        snprintf(buffer, sizeof(buffer), "%4d%%", static_cast<int>(xinput.getLastWheelspinRumble() * 100));
        addString(buffer, valueX, legendY, Justify::LEFT,
            Fonts::ROBOTO_MONO, ColorConfig::getInstance().getSecondary(), dims.fontSize);
        legendY += dims.lineHeightNormal;
    }

    // Brake lockup effect
    if (config.brakeLockupEffect.isEnabled()) {
        addString("LCK", legendStartX, legendY, Justify::LEFT,
            Fonts::ROBOTO_MONO, lockupColor, dims.fontSize);
        snprintf(buffer, sizeof(buffer), "%4d%%", static_cast<int>(xinput.getLastLockupRumble() * 100));
        addString(buffer, valueX, legendY, Justify::LEFT,
            Fonts::ROBOTO_MONO, ColorConfig::getInstance().getSecondary(), dims.fontSize);
        legendY += dims.lineHeightNormal;
    }

    // Wheelie effect
    if (config.wheelieEffect.isEnabled()) {
        addString("WHL", legendStartX, legendY, Justify::LEFT,
            Fonts::ROBOTO_MONO, wheelieColor, dims.fontSize);
        snprintf(buffer, sizeof(buffer), "%4d%%", static_cast<int>(xinput.getLastWheelieRumble() * 100));
        addString(buffer, valueX, legendY, Justify::LEFT,
            Fonts::ROBOTO_MONO, ColorConfig::getInstance().getSecondary(), dims.fontSize);
        legendY += dims.lineHeightNormal;
    }

    // RPM effect
    if (config.rpmEffect.isEnabled()) {
        addString("RPM", legendStartX, legendY, Justify::LEFT,
            Fonts::ROBOTO_MONO, rpmColor, dims.fontSize);
        snprintf(buffer, sizeof(buffer), "%4d%%", static_cast<int>(xinput.getLastRpmRumble() * 100));
        addString(buffer, valueX, legendY, Justify::LEFT,
            Fonts::ROBOTO_MONO, ColorConfig::getInstance().getSecondary(), dims.fontSize);
        legendY += dims.lineHeightNormal;
    }

    // Slide effect
    if (config.slideEffect.isEnabled()) {
        addString("SLD", legendStartX, legendY, Justify::LEFT,
            Fonts::ROBOTO_MONO, slideColor, dims.fontSize);
        snprintf(buffer, sizeof(buffer), "%4d%%", static_cast<int>(xinput.getLastSlideRumble() * 100));
        addString(buffer, valueX, legendY, Justify::LEFT,
            Fonts::ROBOTO_MONO, ColorConfig::getInstance().getSecondary(), dims.fontSize);
        legendY += dims.lineHeightNormal;
    }

    // Surface effect
    if (config.surfaceEffect.isEnabled()) {
        addString("SRF", legendStartX, legendY, Justify::LEFT,
            Fonts::ROBOTO_MONO, terrainColor, dims.fontSize);
        snprintf(buffer, sizeof(buffer), "%4d%%", static_cast<int>(xinput.getLastSurfaceRumble() * 100));
        addString(buffer, valueX, legendY, Justify::LEFT,
            Fonts::ROBOTO_MONO, ColorConfig::getInstance().getSecondary(), dims.fontSize);
        legendY += dims.lineHeightNormal;
    }

    // Steer torque effect
    if (config.steerEffect.isEnabled()) {
        addString("STR", legendStartX, legendY, Justify::LEFT,
            Fonts::ROBOTO_MONO, steerColor, dims.fontSize);
        snprintf(buffer, sizeof(buffer), "%4d%%", static_cast<int>(xinput.getLastSteerRumble() * 100));
        addString(buffer, valueX, legendY, Justify::LEFT,
            Fonts::ROBOTO_MONO, ColorConfig::getInstance().getSecondary(), dims.fontSize);
    }
}
