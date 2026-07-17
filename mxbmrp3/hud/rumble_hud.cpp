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
    // OPTIMIZATION: Skip processing when not visible
    // Note: Rumble feedback itself runs via XInputReader regardless of HUD visibility
    if (!isVisibleAnySurface()) {
        clearDataDirty();
        clearLayoutDirty();
        return;
    }

    // Rebuild only when new telemetry arrived (data dirty fires per
    // InputTelemetry change, ~100Hz while riding) or layout changed. The rumble
    // effect values and their history deques are all telemetry-driven, so the
    // graph output is identical between telemetry ticks — rebuilding every render
    // frame (~2,600 line-segment quads with all channels on) wasted most of the
    // work at 240fps. Same fix TelemetryHud received for the identical pattern.
    if (isDataDirty() || isLayoutDirty()) {
        rebuildRenderData();
    }
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
    setPosition(0.7315f, 0.37547f);
    setScale(1.0f);
    m_bShowMaxMarkers = false;  // Max markers OFF by default
    // Linger counts REBUILDS, which since the dirty-gate fix tick at telemetry
    // rate (~100Hz while riding), not render fps — so 60 ≈ 0.6s on track (and
    // markers freeze while paused/in menus, when no telemetry arrives).
    m_maxMarkerLingerFrames = 60;

    // Reset max tracking state
    for (int i = 0; i < 2; ++i) {
        m_markerValues[i] = 0.0f;
        m_maxFramesRemaining[i] = 0;
    }

    setDataDirty();
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

        // Fixed 50% — the empty bar is part of the gauge readout, not the panel backdrop,
        // so it doesn't follow the background-opacity slider (matches BarsWidget).
        emptyQuad.m_ulColor = PluginUtils::applyOpacity(this->getColor(ColorSlot::MUTED), 0.5f);

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
    markerQuad.m_ulColor = this->getColor(ColorSlot::PRIMARY);  // White
    m_quads.push_back(markerQuad);
}

void RumbleHud::rebuildRenderData() {
    m_quads.clear();
    clearStrings();

    const auto dims = getScaledDimensions();
    const PluginData& pluginData = PluginData::getInstance();
    const XInputReader& xinput = pluginData.getXInputReader();
    const RumbleConfig& config = xinput.getRumbleConfig();

    // Split-aware enablement for Bumps/Lockup: the primary (front/combined) trace uses the
    // front effect when split, the combined effect otherwise; the rear trace only exists
    // when split. These drive the legend count, graph traces and legend entries.
    const bool bumpsPrimaryOn = config.suspensionSplit ? config.suspensionEffectFront.isEnabled()
                                                       : config.suspensionEffect.isEnabled();
    const bool bumpsRearOn = config.suspensionSplit && config.suspensionEffectRear.isEnabled();
    const bool lockupPrimaryOn = config.brakeLockupSplit ? config.brakeLockupEffectFront.isEnabled()
                                                         : config.brakeLockupEffect.isEnabled();
    const bool lockupRearOn = config.brakeLockupSplit && config.brakeLockupEffectRear.isEnabled();

    // Rumble data is only valid when player is on track
    // During spectate/replay, telemetry isn't received so history would be stale
    bool isOnTrack = (pluginData.getDrawState() == ViewState::ON_TRACK);

    // Calculate dimensions
    float graphWidth = PluginUtils::calculateMonospaceTextWidth(GRAPH_WIDTH_CHARS, dims.fontSize);
    float barWidth = PluginUtils::calculateMonospaceTextWidth(BAR_WIDTH_CHARS, dims.fontSize);
    float gapWidth = PluginUtils::calculateMonospaceTextWidth(GAP_WIDTH_CHARS, dims.fontSize);
    float backgroundWidth = PluginUtils::calculateMonospaceTextWidth(BACKGROUND_WIDTH_CHARS, dims.fontSize)
        + dims.paddingH + dims.paddingH;
    float graphHeight = GRAPH_HEIGHT_LINES * dims.lineHeightNormal;
    float barHeight = (GRAPH_HEIGHT_LINES - 1) * dims.lineHeightNormal;  // Bars are 1 line shorter to fit labels

    // Calculate legend height (count enabled effects)
    int legendLines = 0;
    if (bumpsPrimaryOn || bumpsRearOn) legendLines++;
    if (config.wheelspinEffect.isEnabled()) legendLines++;
    if (lockupPrimaryOn || lockupRearOn) legendLines++;
    if (config.wheelieEffect.isEnabled()) legendLines++;
    if (config.rpmEffect.isEnabled()) legendLines++;
    if (config.slideEffect.isEnabled()) legendLines++;
    if (config.surfaceEffect.isEnabled()) legendLines++;
    if (config.steerEffect.isEnabled()) legendLines++;
    if (config.revLimiterEffect.isEnabled()) legendLines++;
    if (config.pitLimiterEffect.isEnabled()) legendLines++;
    float legendHeight = legendLines * dims.lineHeightNormal;

    // Height: title + max(graph height, legend height) - matching TelemetryHud/PerformanceHud
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
        addTitleString("Rumble", contentStartX, currentY, Justify::LEFT,
            this->getFont(FontCategory::TITLE), this->getColor(ColorSlot::PRIMARY), dims.fontSizeLarge);
        currentY += titleHeight;
    }

    // Colors for motors and effects
    unsigned long heavyColor = PluginUtils::makeColor(255, 100, 100, 230);  // Red-ish for heavy motor
    unsigned long lightColor = PluginUtils::makeColor(100, 200, 255, 230);  // Blue-ish for light motor
    unsigned long bumpsColor = SemanticColors::FRONT_SUSP;    // Purple for bumps/suspension (front)
    unsigned long bumpsRearColor = SemanticColors::REAR_SUSP; // Rear suspension when split
    unsigned long wheelColor = SemanticColors::THROTTLE;      // Green
    unsigned long lockupColor = SemanticColors::FRONT_BRAKE;  // Red for lockup (front)
    unsigned long lockupRearColor = SemanticColors::REAR_BRAKE; // Rear lockup when split
    unsigned long wheelieColor = PluginUtils::makeColor(50, 220, 220, 230); // Cyan for wheelie
    unsigned long rpmColor = ColorPalette::GRAY;              // Gray
    unsigned long slideColor = PluginUtils::makeColor(255, 200, 50, 230);   // Orange/yellow for lateral slide
    unsigned long terrainColor = PluginUtils::makeColor(139, 90, 43, 230); // Brown for terrain/surface
    unsigned long steerColor = PluginUtils::makeColor(180, 100, 220, 230);  // Purple-ish for steer torque
    unsigned long revLimColor = PluginUtils::makeColor(255, 230, 60, 230);   // Bright yellow for rev limiter
    unsigned long pitLimColor = PluginUtils::makeColor(120, 160, 255, 230);  // Blue for pit limiter

    // === LEFT SIDE: Graph ===
    float graphStartX = contentStartX;
    float graphStartY = currentY;

    // Grid lines (0%, 50%, 100%) + Y-axis effect% labels — the shared strip-chart
    // frame, like the other graph HUDs.
    addStripChartFrame(graphStartX, graphStartY, graphWidth, graphHeight, "100%", "50%", "0%", dims);

    // Draw all graphs overlaid (only when on track - no telemetry data during spectate/replay)
    float lineThickness = stripChartLineThickness();
    size_t maxHistory = XInputReader::MAX_RUMBLE_HISTORY;

    if (isOnTrack) {
        // Effects first (underneath motors)
        if (bumpsPrimaryOn) {
            addStripChartHistoryLine(xinput.getSuspensionHistory(), bumpsColor,
                            graphStartX, graphStartY, graphWidth, graphHeight, lineThickness, maxHistory);
        }
        // Rear suspension trace (only when split) in the rear-wheel color
        if (bumpsRearOn) {
            addStripChartHistoryLine(xinput.getSuspensionRearHistory(), bumpsRearColor,
                            graphStartX, graphStartY, graphWidth, graphHeight, lineThickness, maxHistory);
        }
        if (config.wheelspinEffect.isEnabled()) {
            addStripChartHistoryLine(xinput.getWheelspinHistory(), wheelColor,
                            graphStartX, graphStartY, graphWidth, graphHeight, lineThickness, maxHistory);
        }
        if (lockupPrimaryOn) {
            addStripChartHistoryLine(xinput.getLockupHistory(), lockupColor,
                            graphStartX, graphStartY, graphWidth, graphHeight, lineThickness, maxHistory);
        }
        // Rear lockup trace (only when split) in the rear-wheel color
        if (lockupRearOn) {
            addStripChartHistoryLine(xinput.getLockupRearHistory(), lockupRearColor,
                            graphStartX, graphStartY, graphWidth, graphHeight, lineThickness, maxHistory);
        }
        if (config.wheelieEffect.isEnabled()) {
            addStripChartHistoryLine(xinput.getWheelieHistory(), wheelieColor,
                            graphStartX, graphStartY, graphWidth, graphHeight, lineThickness, maxHistory);
        }
        if (config.rpmEffect.isEnabled()) {
            addStripChartHistoryLine(xinput.getRpmHistory(), rpmColor,
                            graphStartX, graphStartY, graphWidth, graphHeight, lineThickness, maxHistory);
        }
        if (config.slideEffect.isEnabled()) {
            addStripChartHistoryLine(xinput.getSlideHistory(), slideColor,
                            graphStartX, graphStartY, graphWidth, graphHeight, lineThickness, maxHistory);
        }
        if (config.surfaceEffect.isEnabled()) {
            addStripChartHistoryLine(xinput.getSurfaceHistory(), terrainColor,
                            graphStartX, graphStartY, graphWidth, graphHeight, lineThickness, maxHistory);
        }
        if (config.steerEffect.isEnabled()) {
            addStripChartHistoryLine(xinput.getSteerHistory(), steerColor,
                            graphStartX, graphStartY, graphWidth, graphHeight, lineThickness, maxHistory);
        }
        if (config.revLimiterEffect.isEnabled()) {
            addStripChartHistoryLine(xinput.getRevLimiterHistory(), revLimColor,
                            graphStartX, graphStartY, graphWidth, graphHeight, lineThickness, maxHistory);
        }
        if (config.pitLimiterEffect.isEnabled()) {
            addStripChartHistoryLine(xinput.getPitLimiterHistory(), pitLimColor,
                            graphStartX, graphStartY, graphWidth, graphHeight, lineThickness, maxHistory);
        }
    }

    // === MIDDLE: Force Bars (LGT and HVY) ===
    float barsStartX = contentStartX + graphWidth + gapWidth;
    float barsStartY = currentY;

    // Get accumulated motor values from history (only when on track)
    float heavyValue = 0.0f;
    float lightValue = 0.0f;
    if (isOnTrack) {
        const auto& heavyHistory = xinput.getHeavyMotorHistory();
        const auto& lightHistory = xinput.getLightMotorHistory();
        heavyValue = heavyHistory.empty() ? 0.0f : heavyHistory.back();
        lightValue = lightHistory.empty() ? 0.0f : lightHistory.back();
    }

    // Light motor bar (first) - index 0
    updateMaxTracking(0, lightValue);
    addVerticalBar(barsStartX, barsStartY, barWidth, barHeight, lightValue, lightColor);
    if (m_bShowMaxMarkers && m_maxFramesRemaining[0] > 0) {
        addMaxMarker(barsStartX, barsStartY, barWidth, barHeight, m_markerValues[0]);
    }
    addString("L", barsStartX + barWidth / 2.0f, barsStartY + barHeight, Justify::CENTER,
              this->getFont(FontCategory::STRONG), this->getColor(ColorSlot::TERTIARY), dims.fontSizeSmall);

    // Heavy motor bar (second) - index 1
    float heavyBarX = barsStartX + barWidth + gapWidth;
    updateMaxTracking(1, heavyValue);
    addVerticalBar(heavyBarX, barsStartY, barWidth, barHeight, heavyValue, heavyColor);
    if (m_bShowMaxMarkers && m_maxFramesRemaining[1] > 0) {
        addMaxMarker(heavyBarX, barsStartY, barWidth, barHeight, m_markerValues[1]);
    }
    addString("H", heavyBarX + barWidth / 2.0f, barsStartY + barHeight, Justify::CENTER,
              this->getFont(FontCategory::STRONG), this->getColor(ColorSlot::TERTIARY), dims.fontSizeSmall);

    // === RIGHT SIDE: Legend (effects only, motor totals shown in bars) ===
    float legendStartX = heavyBarX + barWidth + gapWidth;
    float legendY = currentY;
    float valueX = legendStartX + PluginUtils::calculateMonospaceTextWidth(4, dims.fontSize);  // After "XXX "
    char buffer[16];

    // Bumps/Suspension effect (show 0% when not on track)
    if (bumpsPrimaryOn || bumpsRearOn) {
        addLabel("Bmp", legendStartX, legendY, Justify::LEFT,
            this->getFont(FontCategory::STRONG), bumpsColor, dims);
        // Overall intensity = stronger of front/rear (rear is 0 when not split)
        float suspVal = std::max(xinput.getLastSuspensionRumble(), xinput.getLastSuspensionRumbleRear());
        snprintf(buffer, sizeof(buffer), "%4d%%", isOnTrack ? static_cast<int>(suspVal * 100) : 0);
        addString(buffer, valueX, legendY, Justify::LEFT,
            this->getFont(FontCategory::NORMAL), this->getColor(ColorSlot::SECONDARY), dims.fontSize);
        legendY += dims.lineHeightNormal;
    }

    // Spin effect
    if (config.wheelspinEffect.isEnabled()) {
        addLabel("Spn", legendStartX, legendY, Justify::LEFT,
            this->getFont(FontCategory::STRONG), wheelColor, dims);
        snprintf(buffer, sizeof(buffer), "%4d%%", isOnTrack ? static_cast<int>(xinput.getLastWheelspinRumble() * 100) : 0);
        addString(buffer, valueX, legendY, Justify::LEFT,
            this->getFont(FontCategory::NORMAL), this->getColor(ColorSlot::SECONDARY), dims.fontSize);
        legendY += dims.lineHeightNormal;
    }

    // Brake lockup effect
    if (lockupPrimaryOn || lockupRearOn) {
        addLabel("Lck", legendStartX, legendY, Justify::LEFT,
            this->getFont(FontCategory::STRONG), lockupColor, dims);
        // Overall intensity = stronger of front/rear (rear is 0 when not split)
        float lockVal = std::max(xinput.getLastLockupRumble(), xinput.getLastLockupRumbleRear());
        snprintf(buffer, sizeof(buffer), "%4d%%", isOnTrack ? static_cast<int>(lockVal * 100) : 0);
        addString(buffer, valueX, legendY, Justify::LEFT,
            this->getFont(FontCategory::NORMAL), this->getColor(ColorSlot::SECONDARY), dims.fontSize);
        legendY += dims.lineHeightNormal;
    }

    // Wheelie effect
    if (config.wheelieEffect.isEnabled()) {
        addLabel("Whl", legendStartX, legendY, Justify::LEFT,
            this->getFont(FontCategory::STRONG), wheelieColor, dims);
        snprintf(buffer, sizeof(buffer), "%4d%%", isOnTrack ? static_cast<int>(xinput.getLastWheelieRumble() * 100) : 0);
        addString(buffer, valueX, legendY, Justify::LEFT,
            this->getFont(FontCategory::NORMAL), this->getColor(ColorSlot::SECONDARY), dims.fontSize);
        legendY += dims.lineHeightNormal;
    }

    // RPM effect
    if (config.rpmEffect.isEnabled()) {
        addLabel("RPM", legendStartX, legendY, Justify::LEFT,
            this->getFont(FontCategory::STRONG), rpmColor, dims);
        snprintf(buffer, sizeof(buffer), "%4d%%", isOnTrack ? static_cast<int>(xinput.getLastRpmRumble() * 100) : 0);
        addString(buffer, valueX, legendY, Justify::LEFT,
            this->getFont(FontCategory::NORMAL), this->getColor(ColorSlot::SECONDARY), dims.fontSize);
        legendY += dims.lineHeightNormal;
    }

    // Slide effect
    if (config.slideEffect.isEnabled()) {
        addLabel("Sld", legendStartX, legendY, Justify::LEFT,
            this->getFont(FontCategory::STRONG), slideColor, dims);
        snprintf(buffer, sizeof(buffer), "%4d%%", isOnTrack ? static_cast<int>(xinput.getLastSlideRumble() * 100) : 0);
        addString(buffer, valueX, legendY, Justify::LEFT,
            this->getFont(FontCategory::NORMAL), this->getColor(ColorSlot::SECONDARY), dims.fontSize);
        legendY += dims.lineHeightNormal;
    }

    // Surface effect
    if (config.surfaceEffect.isEnabled()) {
        addLabel("Srf", legendStartX, legendY, Justify::LEFT,
            this->getFont(FontCategory::STRONG), terrainColor, dims);
        snprintf(buffer, sizeof(buffer), "%4d%%", isOnTrack ? static_cast<int>(xinput.getLastSurfaceRumble() * 100) : 0);
        addString(buffer, valueX, legendY, Justify::LEFT,
            this->getFont(FontCategory::NORMAL), this->getColor(ColorSlot::SECONDARY), dims.fontSize);
        legendY += dims.lineHeightNormal;
    }

    // Steer torque effect
    if (config.steerEffect.isEnabled()) {
        addLabel("Str", legendStartX, legendY, Justify::LEFT,
            this->getFont(FontCategory::STRONG), steerColor, dims);
        snprintf(buffer, sizeof(buffer), "%4d%%", isOnTrack ? static_cast<int>(xinput.getLastSteerRumble() * 100) : 0);
        addString(buffer, valueX, legendY, Justify::LEFT,
            this->getFont(FontCategory::NORMAL), this->getColor(ColorSlot::SECONDARY), dims.fontSize);
        legendY += dims.lineHeightNormal;
    }

    // Rev limiter effect
    if (config.revLimiterEffect.isEnabled()) {
        addLabel("Rev", legendStartX, legendY, Justify::LEFT,
            this->getFont(FontCategory::STRONG), revLimColor, dims);
        snprintf(buffer, sizeof(buffer), "%4d%%", isOnTrack ? static_cast<int>(xinput.getLastRevLimiterRumble() * 100) : 0);
        addString(buffer, valueX, legendY, Justify::LEFT,
            this->getFont(FontCategory::NORMAL), this->getColor(ColorSlot::SECONDARY), dims.fontSize);
        legendY += dims.lineHeightNormal;
    }

    // Pit limiter effect
    if (config.pitLimiterEffect.isEnabled()) {
        addLabel("Pit", legendStartX, legendY, Justify::LEFT,
            this->getFont(FontCategory::STRONG), pitLimColor, dims);
        snprintf(buffer, sizeof(buffer), "%4d%%", isOnTrack ? static_cast<int>(xinput.getLastPitLimiterRumble() * 100) : 0);
        addString(buffer, valueX, legendY, Justify::LEFT,
            this->getFont(FontCategory::NORMAL), this->getColor(ColorSlot::SECONDARY), dims.fontSize);
    }
}
