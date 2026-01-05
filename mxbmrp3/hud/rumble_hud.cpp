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
    // One-time setup
    DEBUG_INFO("RumbleHud created");
    setDraggable(true);
    m_quads.reserve(500);   // Line segments for graphs
    m_strings.reserve(20);  // Title + labels

    // Set texture base name for dynamic texture discovery
    setTextureBaseName("rumble_hud");

    // Set all configurable defaults
    resetToDefaults();

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
    setTextureVariant(0);  // No texture by default
    m_fBackgroundOpacity = SettingsLimits::DEFAULT_OPACITY;
    setPosition(0.737f, 0.3663f);
    setScale(1.0f);
    m_bShowMaxMarkers = false;  // Max markers OFF by default
    m_maxMarkerLingerFrames = 60;  // ~1 second at 60fps

    // Reset max tracking state
    for (int i = 0; i < 2; ++i) {
        m_maxValues[i] = 0.0f;
        m_markerValues[i] = 0.0f;
        m_prevValues[i] = 0.0f;
        m_maxFramesRemaining[i] = 0;
    }

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

void RumbleHud::addVerticalBar(float x, float y, float barWidth, float barHeight,
                                float value, unsigned long color) {
    // Clamp value to 0-1 range
    value = std::max(0.0f, std::min(1.0f, value));

    // Calculate filled and empty heights
    float filledHeight = barHeight * value;
    float emptyHeight = barHeight - filledHeight;

    // Empty portion (top) - darker gray
    if (emptyHeight > 0.001f) {
        SPluginQuad_t emptyQuad;
        float emptyX = x, emptyY = y;
        applyOffset(emptyX, emptyY);
        setQuadPositions(emptyQuad, emptyX, emptyY, barWidth, emptyHeight);
        emptyQuad.m_iSprite = PluginConstants::SpriteIndex::SOLID_COLOR;

        // Apply background opacity to empty portion (half opacity)
        emptyQuad.m_ulColor = PluginUtils::applyOpacity(ColorConfig::getInstance().getMuted(), m_fBackgroundOpacity * 0.5f);

        m_quads.push_back(emptyQuad);
    }

    // Filled portion (bottom) - colored
    if (filledHeight > 0.001f) {
        SPluginQuad_t filledQuad;
        float filledX = x, filledY = y + emptyHeight;
        applyOffset(filledX, filledY);
        setQuadPositions(filledQuad, filledX, filledY, barWidth, filledHeight);
        filledQuad.m_iSprite = PluginConstants::SpriteIndex::SOLID_COLOR;

        // Apply full opacity to filled portion
        filledQuad.m_ulColor = PluginUtils::applyOpacity(color, 1.0f);

        m_quads.push_back(filledQuad);
    }
}

void RumbleHud::updateMaxTracking(int barIndex, float currentValue) {
    if (barIndex < 0 || barIndex >= 2) return;

    // Track overall max
    if (currentValue > m_maxValues[barIndex]) {
        m_maxValues[barIndex] = currentValue;
    }

    // Max marker: show at peak when value starts decreasing, hide when increasing
    // Use small threshold to avoid jitter from noise
    constexpr float THRESHOLD = 0.02f;

    if (currentValue > m_markerValues[barIndex] + THRESHOLD) {
        // Value exceeds marker - update marker position, hide it
        m_markerValues[barIndex] = currentValue;
        m_maxFramesRemaining[barIndex] = 0;
    } else if (currentValue < m_markerValues[barIndex] - THRESHOLD && m_maxFramesRemaining[barIndex] == 0) {
        // Value dropped below marker - start showing marker (linger at peak)
        m_maxFramesRemaining[barIndex] = m_maxMarkerLingerFrames;
    } else if (m_maxFramesRemaining[barIndex] > 0) {
        // Marker is showing - countdown
        m_maxFramesRemaining[barIndex]--;
        // When linger ends, reset marker to 0 so it disappears
        if (m_maxFramesRemaining[barIndex] == 0) {
            m_markerValues[barIndex] = 0.0f;
        }
    }

    m_prevValues[barIndex] = currentValue;
}

void RumbleHud::addMaxMarker(float x, float y, float barWidth, float barHeight, float maxValue) {
    // Draw a thin horizontal white line at the max value position
    maxValue = std::max(0.0f, std::min(1.0f, maxValue));
    if (maxValue < 0.01f) return;  // Don't draw if max is essentially zero

    float markerHeight = barHeight * 0.02f;  // Thin line (2% of bar height)
    float markerY = y + barHeight * (1.0f - maxValue) - markerHeight * 0.5f;

    SPluginQuad_t markerQuad;
    float markerX = x;
    applyOffset(markerX, markerY);
    setQuadPositions(markerQuad, markerX, markerY, barWidth, markerHeight);
    markerQuad.m_iSprite = PluginConstants::SpriteIndex::SOLID_COLOR;
    markerQuad.m_ulColor = ColorConfig::getInstance().getPrimary();  // White
    m_quads.push_back(markerQuad);
}

void RumbleHud::rebuildRenderData() {
    m_quads.clear();
    m_strings.clear();

    const auto dims = getScaledDimensions();
    const XInputReader& xinput = PluginData::getInstance().getXInputReader();
    const RumbleConfig& config = xinput.getRumbleConfig();

    // Calculate dimensions
    float graphWidth = PluginUtils::calculateMonospaceTextWidth(GRAPH_WIDTH_CHARS, dims.fontSize);
    float barWidth = PluginUtils::calculateMonospaceTextWidth(BAR_WIDTH_CHARS, dims.fontSize);
    float gapWidth = PluginUtils::calculateMonospaceTextWidth(GAP_WIDTH_CHARS, dims.fontSize);
    float backgroundWidth = PluginUtils::calculateMonospaceTextWidth(BACKGROUND_WIDTH_CHARS, dims.fontSize)
        + dims.paddingH + dims.paddingH;
    float graphHeight = GRAPH_HEIGHT_LINES * dims.lineHeightNormal;
    float labelHeight = dims.lineHeightNormal;  // Height for "H" and "L" labels

    // Calculate legend height (count enabled effects)
    int legendLines = 0;
    if (config.suspensionEffect.isEnabled()) legendLines++;
    if (config.wheelspinEffect.isEnabled()) legendLines++;
    if (config.brakeLockupEffect.isEnabled()) legendLines++;
    if (config.wheelieEffect.isEnabled()) legendLines++;
    if (config.rpmEffect.isEnabled()) legendLines++;
    if (config.slideEffect.isEnabled()) legendLines++;
    if (config.surfaceEffect.isEnabled()) legendLines++;
    if (config.steerEffect.isEnabled()) legendLines++;
    float legendHeight = legendLines * dims.lineHeightNormal;

    // Height: title + max(graph height, legend height, bar height + label height)
    float titleHeight = m_bShowTitle ? dims.lineHeightLarge : 0.0f;
    float barTotalHeight = graphHeight + labelHeight;  // Bars + labels below
    float contentHeight = graphHeight > legendHeight ? graphHeight : legendHeight;
    if (barTotalHeight > contentHeight) contentHeight = barTotalHeight;
    float backgroundHeight = dims.paddingV + titleHeight + contentHeight + dims.paddingV;

    setBounds(START_X, START_Y, START_X + backgroundWidth, START_Y + backgroundHeight);

    // Add background quad
    addBackgroundQuad(START_X, START_Y, backgroundWidth, backgroundHeight);

    float contentStartX = START_X + dims.paddingH;
    float contentStartY = START_Y + dims.paddingV;
    float currentY = contentStartY;

    // Title
    if (m_bShowTitle) {
        addTitleString("Rumble", contentStartX, currentY, Justify::LEFT,
            Fonts::getTitle(), ColorConfig::getInstance().getPrimary(), dims.fontSizeLarge);
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

    // === MIDDLE: Force Bars (LGT and HVY) ===
    float barsStartX = contentStartX + graphWidth + gapWidth;
    float barsStartY = currentY;

    // Get accumulated motor values from history (history contains correct values even when rumble disabled)
    const auto& heavyHistory = xinput.getHeavyMotorHistory();
    const auto& lightHistory = xinput.getLightMotorHistory();
    float heavyValue = heavyHistory.empty() ? 0.0f : heavyHistory.back();
    float lightValue = lightHistory.empty() ? 0.0f : lightHistory.back();

    // Light motor bar (first) - index 0
    updateMaxTracking(0, lightValue);
    addVerticalBar(barsStartX, barsStartY, barWidth, graphHeight, lightValue, lightColor);
    if (m_bShowMaxMarkers && m_maxFramesRemaining[0] > 0) {
        addMaxMarker(barsStartX, barsStartY, barWidth, graphHeight, m_markerValues[0]);
    }
    addString("L", barsStartX + barWidth / 2.0f, barsStartY + graphHeight, Justify::CENTER,
              Fonts::getNormal(), ColorConfig::getInstance().getTertiary(), dims.fontSize);

    // Heavy motor bar (second) - index 1
    float heavyBarX = barsStartX + barWidth + gapWidth;
    updateMaxTracking(1, heavyValue);
    addVerticalBar(heavyBarX, barsStartY, barWidth, graphHeight, heavyValue, heavyColor);
    if (m_bShowMaxMarkers && m_maxFramesRemaining[1] > 0) {
        addMaxMarker(heavyBarX, barsStartY, barWidth, graphHeight, m_markerValues[1]);
    }
    addString("H", heavyBarX + barWidth / 2.0f, barsStartY + graphHeight, Justify::CENTER,
              Fonts::getNormal(), ColorConfig::getInstance().getTertiary(), dims.fontSize);

    // === RIGHT SIDE: Legend (effects only, motor totals shown in bars) ===
    float legendStartX = heavyBarX + barWidth + gapWidth;
    float legendY = currentY;
    float valueX = legendStartX + PluginUtils::calculateMonospaceTextWidth(4, dims.fontSize);  // After "XXX "
    char buffer[16];

    // Bumps/Suspension effect
    if (config.suspensionEffect.isEnabled()) {
        addString("BMP", legendStartX, legendY, Justify::LEFT,
            Fonts::getNormal(), bumpsColor, dims.fontSize);
        snprintf(buffer, sizeof(buffer), "%4d%%", static_cast<int>(xinput.getLastSuspensionRumble() * 100));
        addString(buffer, valueX, legendY, Justify::LEFT,
            Fonts::getNormal(), ColorConfig::getInstance().getSecondary(), dims.fontSize);
        legendY += dims.lineHeightNormal;
    }

    // Spin effect
    if (config.wheelspinEffect.isEnabled()) {
        addString("SPN", legendStartX, legendY, Justify::LEFT,
            Fonts::getNormal(), wheelColor, dims.fontSize);
        snprintf(buffer, sizeof(buffer), "%4d%%", static_cast<int>(xinput.getLastWheelspinRumble() * 100));
        addString(buffer, valueX, legendY, Justify::LEFT,
            Fonts::getNormal(), ColorConfig::getInstance().getSecondary(), dims.fontSize);
        legendY += dims.lineHeightNormal;
    }

    // Brake lockup effect
    if (config.brakeLockupEffect.isEnabled()) {
        addString("LCK", legendStartX, legendY, Justify::LEFT,
            Fonts::getNormal(), lockupColor, dims.fontSize);
        snprintf(buffer, sizeof(buffer), "%4d%%", static_cast<int>(xinput.getLastLockupRumble() * 100));
        addString(buffer, valueX, legendY, Justify::LEFT,
            Fonts::getNormal(), ColorConfig::getInstance().getSecondary(), dims.fontSize);
        legendY += dims.lineHeightNormal;
    }

    // Wheelie effect
    if (config.wheelieEffect.isEnabled()) {
        addString("WHL", legendStartX, legendY, Justify::LEFT,
            Fonts::getNormal(), wheelieColor, dims.fontSize);
        snprintf(buffer, sizeof(buffer), "%4d%%", static_cast<int>(xinput.getLastWheelieRumble() * 100));
        addString(buffer, valueX, legendY, Justify::LEFT,
            Fonts::getNormal(), ColorConfig::getInstance().getSecondary(), dims.fontSize);
        legendY += dims.lineHeightNormal;
    }

    // RPM effect
    if (config.rpmEffect.isEnabled()) {
        addString("RPM", legendStartX, legendY, Justify::LEFT,
            Fonts::getNormal(), rpmColor, dims.fontSize);
        snprintf(buffer, sizeof(buffer), "%4d%%", static_cast<int>(xinput.getLastRpmRumble() * 100));
        addString(buffer, valueX, legendY, Justify::LEFT,
            Fonts::getNormal(), ColorConfig::getInstance().getSecondary(), dims.fontSize);
        legendY += dims.lineHeightNormal;
    }

    // Slide effect
    if (config.slideEffect.isEnabled()) {
        addString("SLD", legendStartX, legendY, Justify::LEFT,
            Fonts::getNormal(), slideColor, dims.fontSize);
        snprintf(buffer, sizeof(buffer), "%4d%%", static_cast<int>(xinput.getLastSlideRumble() * 100));
        addString(buffer, valueX, legendY, Justify::LEFT,
            Fonts::getNormal(), ColorConfig::getInstance().getSecondary(), dims.fontSize);
        legendY += dims.lineHeightNormal;
    }

    // Surface effect
    if (config.surfaceEffect.isEnabled()) {
        addString("SRF", legendStartX, legendY, Justify::LEFT,
            Fonts::getNormal(), terrainColor, dims.fontSize);
        snprintf(buffer, sizeof(buffer), "%4d%%", static_cast<int>(xinput.getLastSurfaceRumble() * 100));
        addString(buffer, valueX, legendY, Justify::LEFT,
            Fonts::getNormal(), ColorConfig::getInstance().getSecondary(), dims.fontSize);
        legendY += dims.lineHeightNormal;
    }

    // Steer torque effect
    if (config.steerEffect.isEnabled()) {
        addString("STR", legendStartX, legendY, Justify::LEFT,
            Fonts::getNormal(), steerColor, dims.fontSize);
        snprintf(buffer, sizeof(buffer), "%4d%%", static_cast<int>(xinput.getLastSteerRumble() * 100));
        addString(buffer, valueX, legendY, Justify::LEFT,
            Fonts::getNormal(), ColorConfig::getInstance().getSecondary(), dims.fontSize);
    }
}
