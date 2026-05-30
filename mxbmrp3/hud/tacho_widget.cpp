// ============================================================================
// hud/tacho_widget.cpp
// Tacho widget - displays rotating needle (0-15000 RPM) with dial background
// ============================================================================
#include "tacho_widget.h"

#include <cstdio>
#include <cmath>

#include "../diagnostics/logger.h"
#include "../core/plugin_utils.h"

using namespace PluginConstants;
using namespace PluginConstants::Math;

TachoWidget::TachoWidget()
{
    // One-time setup
    DEBUG_INFO("TachoWidget created");
    setDraggable(true);
    m_quads.reserve(2);  // dial background + needle

    // Set texture base name for dynamic texture discovery
    setTextureBaseName("tacho_widget");

    // Set all configurable defaults
    resetToDefaults();

    rebuildRenderData();
}

bool TachoWidget::handlesDataType(DataChangeType dataType) const {
    // Update on telemetry changes (bike data)
    return dataType == DataChangeType::InputTelemetry ||
           dataType == DataChangeType::SpectateTarget;
}

void TachoWidget::update() {
    // OPTIMIZATION: Skip processing when not visible
    if (!isVisible()) {
        clearDataDirty();
        clearLayoutDirty();
        return;
    }

    // Always rebuild - RPM updates at high frequency (telemetry rate)
    // Rebuild is cheap (single quad calculation), no need for caching
    rebuildRenderData();
    clearDataDirty();
    clearLayoutDirty();
}

void TachoWidget::rebuildLayout() {
    // Fast path - only update positions (not colors/opacity)
    // For this widget, full rebuild is still cheap, just call rebuildRenderData
    rebuildRenderData();
}

void TachoWidget::rebuildRenderData() {
    // Clear render data
    clearStrings();
    m_quads.clear();

    // Get bike telemetry data
    const PluginData& pluginData = PluginData::getInstance();
    const BikeTelemetryData& bikeData = pluginData.getBikeTelemetry();

    // Calculate dial dimensions based on scale
    float dialSize = DIAL_SIZE * m_fScale;
    float dialWidth = dialSize / UI_ASPECT_RATIO;
    float dialHeight = dialSize;

    // Start pivot at (0,0) relative coordinates - the m_fOffsetX/Y values position the widget on screen
    float startX = 0.0f;
    float startY = 0.0f;

    // Calculate center of dial
    float centerX = startX + dialWidth / 2.0f;
    float centerY = startY + dialHeight / 2.0f;

    // Set bounds for drag detection (relative coordinates, offset applied by base class)
    setBounds(startX, startY, startX + dialWidth, startY + dialHeight);

    // Add dial as background quad (uses base class helper for consistency with PitboardHud)
    // BG Tex ON: shows dial sprite with opacity
    // BG Tex OFF: shows solid black with opacity
    addBackgroundQuad(startX, startY, dialWidth, dialHeight);

    // Get target RPM from telemetry
    float targetRpm = 0.0f;
    if (bikeData.isValid) {
        targetRpm = static_cast<float>(bikeData.rpm);
    }

    // Clamp target RPM to dial range
    if (targetRpm < MIN_RPM) targetRpm = MIN_RPM;
    if (targetRpm > MAX_RPM) targetRpm = MAX_RPM;

    // Apply exponential smoothing to simulate needle inertia
    // smoothed = smoothed + (target - smoothed) * factor
    m_smoothedRpm += (targetRpm - m_smoothedRpm) * NEEDLE_SMOOTH_FACTOR;

    // Calculate needle angle based on smoothed RPM
    // Linear interpolation from MIN_ANGLE_DEG at 0 RPM to MAX_ANGLE_DEG at 15000 RPM
    float rpmRatio = m_smoothedRpm / MAX_RPM;
    float angleDeg = MIN_ANGLE_DEG + rpmRatio * (MAX_ANGLE_DEG - MIN_ANGLE_DEG);
    float angleRad = angleDeg * DEG_TO_RAD;

    // Calculate needle dimensions (relative to dial size)
    float needleLength = dialHeight * 0.42f;  // Needle extends 42% of dial height from center
    float needleWidth = dialHeight * 0.025f;  // Needle width is 2.5% of dial height

    // Add needle quad (centered on dial, rotated based on RPM)
    addNeedleQuad(centerX, centerY, angleRad, needleLength, needleWidth, m_needleColor);
}

void TachoWidget::resetToDefaults() {
    m_bVisible = false;
    m_bShowTitle = false;
    setTextureVariant(1);  // Show dial texture by default
    m_fBackgroundOpacity = 1.0f;  // 100% opacity
    m_fScale = 1.0f;  // 100% default scale
    setPosition(0.616f, 0.8436f);
    m_smoothedRpm = 0.0f;
    m_needleColor = DEFAULT_NEEDLE_COLOR;
    setDataDirty();
}
