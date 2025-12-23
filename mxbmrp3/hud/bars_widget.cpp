// ============================================================================
// hud/bars_widget.cpp
// Bars Widget - displays 6 vertical bars (left to right):
//   - T: Throttle (green)
//   - B: Brakes (split: red front | dark red rear)
//   - C: Clutch (blue)
//   - R: RPM (gray)
//   - S: Suspension (split: purple front | dark purple rear)
//   - F: Fuel (yellow)
// ============================================================================
#include "bars_widget.h"
#include "../core/plugin_utils.h"
#include "../core/color_config.h"
#include "../diagnostics/logger.h"
#include <algorithm>
#include <cstdio>

using namespace PluginConstants;

BarsWidget::BarsWidget() {
    // One-time setup
    DEBUG_INFO("BarsWidget created");
    setDraggable(true);
    m_quads.reserve(17);   // 1 background + 8 bars (6 positions, 2 split) × 2 quads each (filled + empty)
    m_strings.reserve(6);  // 6 labels: T, B, C, R, F, S

    // Set texture base name for dynamic texture discovery
    setTextureBaseName("bars_widget");

    // Set all configurable defaults
    resetToDefaults();

    rebuildRenderData();
}

void BarsWidget::update() {
    // Always rebuild - telemetry updates at physics rate (100Hz)
    rebuildRenderData();
    clearDataDirty();
    clearLayoutDirty();
}

bool BarsWidget::handlesDataType(DataChangeType dataType) const {
    return dataType == DataChangeType::InputTelemetry;
}

void BarsWidget::rebuildLayout() {
    // Widget always rebuilds every frame (see update()), so rebuildLayout just delegates to full rebuild
    // This is acceptable because the widget is lightweight and the split bar logic is complex
    rebuildRenderData();
}

void BarsWidget::rebuildRenderData() {
    m_quads.clear();
    m_strings.clear();

    const auto dims = getScaledDimensions();
    const PluginData& pluginData = PluginData::getInstance();
    const HistoryBuffers& history = pluginData.getHistoryBuffers();
    const BikeTelemetryData& bikeTelemetry = pluginData.getBikeTelemetry();
    const SessionData& sessionData = pluginData.getSessionData();

    // Full telemetry data (rear brake, clutch, suspension, fuel) is ONLY available when ON_TRACK
    // because RunTelemetry() callback only fires when player is on track.
    // During SPECTATE/REPLAY, only limited RaceVehicleData is available (throttle, front brake, RPM, gear).
    bool hasFullTelemetry = (pluginData.getDrawState() == PluginConstants::ViewState::ON_TRACK);

    // Calculate bar dimensions
    float barWidth = PluginUtils::calculateMonospaceTextWidth(BAR_WIDTH_CHARS, dims.fontSize);
    float halfBarWidth = barWidth * 0.5f;  // For split bars (FBR/RBR, FSU/RSU)
    float barSpacing = PluginUtils::calculateMonospaceTextWidth(1, dims.fontSize) * BAR_SPACING_CHARS;  // 0.5 char spacing
    float barHeight = BAR_HEIGHT_LINES * dims.lineHeightNormal;
    float labelHeight = LABEL_HEIGHT_LINES * dims.lineHeightNormal;

    // Use standard widget width (consistent with other widgets)
    float backgroundWidth = PluginUtils::calculateMonospaceTextWidth(WidgetDimensions::BARS_WIDTH, dims.fontSize);
    float backgroundHeight = dims.paddingV + barHeight + labelHeight;  // No bottom padding below labels

    setBounds(START_X, START_Y, START_X + backgroundWidth, START_Y + backgroundHeight);

    // Add background quad
    addBackgroundQuad(START_X, START_Y, backgroundWidth, backgroundHeight);

    float contentStartX = START_X + dims.paddingH;
    float contentStartY = START_Y + dims.paddingV;

    // Get current values - throttle and front brake always available
    float throttleValue = history.throttle.empty() ? 0.0f : history.throttle.back();
    float frontBrakeValue = history.frontBrake.empty() ? 0.0f : history.frontBrake.back();

    // Rear brake (only available when ON_TRACK - show 0 when spectating/replay)
    float rearBrakeValue = (hasFullTelemetry && !history.rearBrake.empty()) ? history.rearBrake.back() : 0.0f;

    // Clutch (only available when ON_TRACK - show 0 when spectating/replay)
    float clutchValue = (hasFullTelemetry && !history.clutch.empty()) ? history.clutch.back() : 0.0f;

    // RPM normalized to 0-1 range (always available)
    float rpmValue = 0.0f;
    int rpm = std::max(0, bikeTelemetry.rpm);
    int limiterRPM = sessionData.limiterRPM;
    rpmValue = (limiterRPM > 0) ? static_cast<float>(rpm) / limiterRPM : 0.0f;

    // Fuel normalized to 0-1 range (only available when ON_TRACK)
    float fuelValue = 0.0f;
    if (hasFullTelemetry && bikeTelemetry.maxFuel > 0) {
        fuelValue = bikeTelemetry.fuel / bikeTelemetry.maxFuel;
    }

    // Suspension compression normalized to 0-1 range (only available when ON_TRACK)
    float frontSuspValue = (hasFullTelemetry && !history.frontSusp.empty()) ? history.frontSusp.back() : 0.0f;
    float rearSuspValue = (hasFullTelemetry && !history.rearSusp.empty()) ? history.rearSusp.back() : 0.0f;

    // Bar colors - use muted gray when data unavailable
    unsigned long mutedColor = ColorConfig::getInstance().getMuted();
    unsigned long throttleColor = SemanticColors::THROTTLE;       // Green (always available)
    unsigned long frontBrakeColor = SemanticColors::FRONT_BRAKE;  // Red (always available)
    unsigned long rearBrakeColor = hasFullTelemetry ? SemanticColors::REAR_BRAKE : mutedColor;
    unsigned long clutchColor = hasFullTelemetry ? SemanticColors::CLUTCH : mutedColor;
    unsigned long rpmColor = ColorPalette::GRAY;                  // Gray (always available)
    unsigned long fuelColor = hasFullTelemetry ? ColorPalette::YELLOW : mutedColor;
    unsigned long frontSuspColor = hasFullTelemetry ? SemanticColors::FRONT_SUSP : mutedColor;
    unsigned long rearSuspColor = hasFullTelemetry ? SemanticColors::REAR_SUSP : mutedColor;

    // Render bars (6 positions, with positions 1 and 5 split into two half-width bars)
    float currentX = contentStartX;

    // Bar 0: Throttle (T) - single bar
    addVerticalBar(currentX, contentStartY, barWidth, barHeight, throttleValue, throttleColor);
    addString("T", currentX + barWidth / 2.0f, contentStartY + barHeight, Justify::CENTER,
              Fonts::getNormal(), ColorConfig::getInstance().getTertiary(), dims.fontSize);
    currentX += barWidth + barSpacing;

    // Bar 1: Brake (B) - split into FBR | RBR when both available, full width FBR when rear unavailable
    if (hasFullTelemetry) {
        // Split bar: front brake (left) | rear brake (right)
        addVerticalBar(currentX, contentStartY, halfBarWidth, barHeight, frontBrakeValue, frontBrakeColor);
        addVerticalBar(currentX + halfBarWidth, contentStartY, halfBarWidth, barHeight, rearBrakeValue, rearBrakeColor);
    } else {
        // Full width: only front brake available
        addVerticalBar(currentX, contentStartY, barWidth, barHeight, frontBrakeValue, frontBrakeColor);
    }
    addString("B", currentX + barWidth / 2.0f, contentStartY + barHeight, Justify::CENTER,
              Fonts::getNormal(), ColorConfig::getInstance().getTertiary(), dims.fontSize);
    currentX += barWidth + barSpacing;

    // Bar 2: Clutch (C) - single bar (muted when unavailable)
    addVerticalBar(currentX, contentStartY, barWidth, barHeight, clutchValue, clutchColor);
    addString("C", currentX + barWidth / 2.0f, contentStartY + barHeight, Justify::CENTER,
              Fonts::getNormal(), hasFullTelemetry ? ColorConfig::getInstance().getTertiary() : mutedColor, dims.fontSize);
    currentX += barWidth + barSpacing;

    // Bar 3: RPM (R) - single bar
    addVerticalBar(currentX, contentStartY, barWidth, barHeight, rpmValue, rpmColor);
    addString("R", currentX + barWidth / 2.0f, contentStartY + barHeight, Justify::CENTER,
              Fonts::getNormal(), ColorConfig::getInstance().getTertiary(), dims.fontSize);
    currentX += barWidth + barSpacing;

    // Bar 4: Suspension (S) - split into FSU | RSU (muted when unavailable)
    addVerticalBar(currentX, contentStartY, halfBarWidth, barHeight, frontSuspValue, frontSuspColor);
    addVerticalBar(currentX + halfBarWidth, contentStartY, halfBarWidth, barHeight, rearSuspValue, rearSuspColor);
    addString("S", currentX + barWidth / 2.0f, contentStartY + barHeight, Justify::CENTER,
              Fonts::getNormal(), hasFullTelemetry ? ColorConfig::getInstance().getTertiary() : mutedColor, dims.fontSize);
    currentX += barWidth + barSpacing;

    // Bar 5: Fuel (F) - single bar (muted when unavailable)
    addVerticalBar(currentX, contentStartY, barWidth, barHeight, fuelValue, fuelColor);
    addString("F", currentX + barWidth / 2.0f, contentStartY + barHeight, Justify::CENTER,
              Fonts::getNormal(), hasFullTelemetry ? ColorConfig::getInstance().getTertiary() : mutedColor, dims.fontSize);
}

void BarsWidget::addVerticalBar(float x, float y, float barWidth, float barHeight,
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

void BarsWidget::resetToDefaults() {
    m_bVisible = true;
    m_bShowTitle = false;  // No title by default
    setTextureVariant(0);  // No texture by default
    m_fBackgroundOpacity = 1.0f;  // Full opacity
    m_fScale = 1.0f;
    setPosition(0.858f, 0.8547f);
    setDataDirty();
}
