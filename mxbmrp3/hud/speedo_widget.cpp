// ============================================================================
// hud/speedo_widget.cpp
// Speedo widget - displays rotating needle (0-230 km/h) with dial background
// ============================================================================
#include "speedo_widget.h"

#include <cstdio>
#include <cmath>

#include "../diagnostics/logger.h"
#include "../core/plugin_utils.h"

using namespace PluginConstants;
using namespace PluginConstants::Math;

SpeedoWidget::SpeedoWidget()
{
    // One-time setup
    DEBUG_INFO("SpeedoWidget created");
    setDraggable(true);
    m_quads.reserve(2);  // dial background + needle

    // Set texture base name for dynamic texture discovery
    setTextureBaseName("speedo_widget");

    // Set all configurable defaults
    resetToDefaults();

    rebuildRenderData();
}

bool SpeedoWidget::handlesDataType(DataChangeType dataType) const {
    // Update on telemetry changes (bike data)
    return dataType == DataChangeType::InputTelemetry ||
           dataType == DataChangeType::SpectateTarget;
}

void SpeedoWidget::update() {
    // Always rebuild - speed updates at high frequency (telemetry rate)
    // Rebuild is cheap (single quad calculation), no need for caching
    rebuildRenderData();
    clearDataDirty();
    clearLayoutDirty();
}

void SpeedoWidget::rebuildLayout() {
    // Fast path - only update positions (not colors/opacity)
    // For this widget, full rebuild is still cheap, just call rebuildRenderData
    rebuildRenderData();
}

void SpeedoWidget::addNeedleQuad(float centerX, float centerY, float angleRad, float needleLength, float needleWidth) {
    // Create needle as a trapezoid shape (flat tip, wider base)
    // The needle points from center outward in the direction of angleRad
    // Uses clockwise vertex order and applyOffset() on each point individually

    // Calculate tip center (pointing outward)
    float tipCenterX = centerX + std::sin(angleRad) * needleLength / UI_ASPECT_RATIO;
    float tipCenterY = centerY - std::cos(angleRad) * needleLength;

    // Calculate base center (opposite of tip, small distance from center)
    float baseLength = needleLength * 0.15f;  // Base extends 15% of needle length behind center
    float baseCenterX = centerX - std::sin(angleRad) * baseLength / UI_ASPECT_RATIO;
    float baseCenterY = centerY + std::cos(angleRad) * baseLength;

    // Calculate perpendicular direction for width
    float perpAngle = angleRad + PI * 0.5f;  // 90 degrees to the right

    // Tip is narrower (30% of base width) - creates flat but tapered look
    float tipHalfWidth = needleWidth * 0.15f;
    float baseHalfWidth = needleWidth * 0.5f;

    // Calculate tip left and right points
    float tipLeftX = tipCenterX + std::sin(perpAngle) * tipHalfWidth / UI_ASPECT_RATIO;
    float tipLeftY = tipCenterY - std::cos(perpAngle) * tipHalfWidth;
    float tipRightX = tipCenterX - std::sin(perpAngle) * tipHalfWidth / UI_ASPECT_RATIO;
    float tipRightY = tipCenterY + std::cos(perpAngle) * tipHalfWidth;

    // Calculate base left and right points
    float baseLeftX = baseCenterX + std::sin(perpAngle) * baseHalfWidth / UI_ASPECT_RATIO;
    float baseLeftY = baseCenterY - std::cos(perpAngle) * baseHalfWidth;
    float baseRightX = baseCenterX - std::sin(perpAngle) * baseHalfWidth / UI_ASPECT_RATIO;
    float baseRightY = baseCenterY + std::cos(perpAngle) * baseHalfWidth;

    // Apply HUD offset to each point individually (MapHud pattern)
    applyOffset(tipLeftX, tipLeftY);
    applyOffset(tipRightX, tipRightY);
    applyOffset(baseRightX, baseRightY);
    applyOffset(baseLeftX, baseLeftY);

    // Create quad with clockwise vertex order: tipLeft -> tipRight -> baseRight -> baseLeft
    // NOTE: Must use clockwise for proper rendering (counter-clockwise gets face-culled)
    SPluginQuad_t needle;
    needle.m_aafPos[0][0] = tipLeftX;      // Front left
    needle.m_aafPos[0][1] = tipLeftY;
    needle.m_aafPos[1][0] = tipRightX;     // Front right (clockwise)
    needle.m_aafPos[1][1] = tipRightY;
    needle.m_aafPos[2][0] = baseRightX;    // Back right
    needle.m_aafPos[2][1] = baseRightY;
    needle.m_aafPos[3][0] = baseLeftX;     // Back left (completes trapezoid)
    needle.m_aafPos[3][1] = baseLeftY;

    needle.m_iSprite = SpriteIndex::SOLID_COLOR;
    needle.m_ulColor = ColorPalette::RED;  // Red needle
    m_quads.push_back(needle);
}

void SpeedoWidget::rebuildRenderData() {
    // Clear render data
    m_strings.clear();
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

    // Get target speed in km/h from telemetry
    float targetSpeed = 0.0f;
    if (bikeData.isValid) {
        targetSpeed = bikeData.speedometer * UnitConversion::MS_TO_KMH;
    }

    // Clamp target speed to dial range
    if (targetSpeed < MIN_SPEED_KMH) targetSpeed = MIN_SPEED_KMH;
    if (targetSpeed > MAX_SPEED_KMH) targetSpeed = MAX_SPEED_KMH;

    // Apply exponential smoothing to simulate needle inertia
    // smoothed = smoothed + (target - smoothed) * factor
    m_smoothedSpeed += (targetSpeed - m_smoothedSpeed) * NEEDLE_SMOOTH_FACTOR;

    // Calculate needle angle based on smoothed speed
    // Linear interpolation from MIN_ANGLE_DEG at 0 km/h to MAX_ANGLE_DEG at 230 km/h
    float speedRatio = m_smoothedSpeed / MAX_SPEED_KMH;
    float angleDeg = MIN_ANGLE_DEG + speedRatio * (MAX_ANGLE_DEG - MIN_ANGLE_DEG);
    float angleRad = angleDeg * DEG_TO_RAD;

    // Calculate needle dimensions (relative to dial size)
    float needleLength = dialHeight * 0.42f;  // Needle extends 42% of dial height from center
    float needleWidth = dialHeight * 0.025f;  // Needle width is 2.5% of dial height

    // Add needle quad (centered on dial, rotated based on speed)
    addNeedleQuad(centerX, centerY, angleRad, needleLength, needleWidth);
}

void SpeedoWidget::resetToDefaults() {
    m_bVisible = false;
    m_bShowTitle = false;
    setTextureVariant(1);  // Show dial texture by default
    m_fBackgroundOpacity = 1.0f;  // 100% opacity
    m_fScale = 1.5f;  // 150% default scale
    setPosition(0.6875f, 0.7659f);
    m_smoothedSpeed = 0.0f;
    setDataDirty();
}
