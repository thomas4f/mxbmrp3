// ============================================================================
// hud/speedo_widget.cpp
// Speedo widget - displays rotating needle (0-230 km/h) with dial background
// ============================================================================
#include "speedo_widget.h"
#include "speed_widget.h"  // For SpeedUnit enum

#include <cstdio>
#include <cmath>

#include "../diagnostics/logger.h"
#include "../core/plugin_utils.h"
#include "../core/odometer_manager.h"
#include "../core/color_config.h"
#include "../core/hud_manager.h"

using namespace PluginConstants;
using namespace PluginConstants::Math;

SpeedoWidget::SpeedoWidget()
{
    // One-time setup
    DEBUG_INFO("SpeedoWidget created");
    setDraggable(true);
    m_quads.reserve(6);   // dial background + needle + 4 odometer background quads
    m_strings.reserve(5); // odometer: main + last + unit, trip: main + last

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
    // OPTIMIZATION: Skip processing when not visible
    if (!isVisible()) {
        clearDataDirty();
        clearLayoutDirty();
        return;
    }

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
    needle.m_ulColor = m_needleColor;
    m_quads.push_back(needle);
}

void SpeedoWidget::rebuildRenderData() {
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

    // ========================================================================
    // Odometer display - traditional analog style with black background
    // Format: 6 digits (5 whole + 1 tenth), last digit inverted (white bg, black text)
    // ========================================================================

    // Get speed unit preference from SpeedWidget (shared setting)
    SpeedWidget::SpeedUnit speedUnit = HudManager::getInstance().getSpeedWidget().getSpeedUnit();
    bool useMiles = (speedUnit == SpeedWidget::SpeedUnit::MPH);

    // Get distances and convert based on unit preference
    // Meters to km: /1000, Meters to miles: /1609.344
    double odometerMeters = OdometerManager::getInstance().getOdometerForCurrentBike();
    double tripMeters = OdometerManager::getInstance().getSessionTripDistance();
    double odometerDist = useMiles ? (odometerMeters / 1609.344) : (odometerMeters / 1000.0);
    double tripDist = useMiles ? (tripMeters / 1609.344) : (tripMeters / 1000.0);

    // Clamp to max displayable values
    // Odometer: 6 digits of whole km/mi (max 999999)
    // Trip: 4 digits with last as tenths (max 999.9)
    if (odometerDist > 999999.0) odometerDist = 999999.0;
    if (tripDist > 999.9) tripDist = 999.9;

    // Odometer: whole km/mi (last digit = 1 km)
    int odometerWhole = static_cast<int>(odometerDist + 0.5) % 1000000;

    // Sizing - use standard font size and line height, scaled with widget
    float fontSize = FontSizes::SMALL * m_fScale;
    float charWidth = fontSize * FontMetrics::MONOSPACE_CHAR_WIDTH_RATIO;
    float charHeight = LineHeights::SMALL * m_fScale;

    // Padding - make uniform on all sides (vertical padding comes from lineHeight > fontSize)
    float paddingV = (charHeight - fontSize) / 2.0f;
    float paddingH = paddingV;  // Same padding horizontally

    // Colors - semantic colors from user's color scheme
    unsigned long bgNormal = this->getColor(ColorSlot::BACKGROUND);
    unsigned long textNormal = this->getColor(ColorSlot::PRIMARY);

    // Helper lambda to add an odometer row - black background, white text, inverted last digit
    auto addOdometerRow = [&](const char* displayText, float rowY) {
        int numChars = static_cast<int>(strlen(displayText));
        float textWidth = charWidth * numChars;
        float bgWidth = textWidth + (paddingH * 2.0f);
        float rowStartX = centerX - (bgWidth / 2.0f);

        // Background quad (black) with uniform padding
        SPluginQuad_t bg;
        float bgX = rowStartX;
        float bgY = rowY;
        applyOffset(bgX, bgY);
        setQuadPositions(bg, bgX, bgY, bgWidth, charHeight);
        bg.m_iSprite = SpriteIndex::SOLID_COLOR;
        bg.m_ulColor = bgNormal;
        m_quads.push_back(bg);

        // White quad behind last digit
        float lastCharX = centerX + (textWidth / 2.0f) - charWidth;
        float lastCharY = rowY + paddingV;
        SPluginQuad_t lastBg;
        float lastBgX = lastCharX;
        float lastBgY = lastCharY;
        applyOffset(lastBgX, lastBgY);
        setQuadPositions(lastBg, lastBgX, lastBgY, charWidth, fontSize);
        lastBg.m_iSprite = SpriteIndex::SOLID_COLOR;
        lastBg.m_ulColor = textNormal;  // White (same as text color)
        m_quads.push_back(lastBg);

        // Split text: main digits (white) and last digit (black)
        char mainDigits[16];
        char lastDigit[2];
        strncpy_s(mainDigits, sizeof(mainDigits), displayText, numChars - 1);
        mainDigits[numChars - 1] = '\0';
        lastDigit[0] = displayText[numChars - 1];
        lastDigit[1] = '\0';

        // Main digits (white, left-justified from text start)
        // skipShadow=true: odometer uses inverted last digit styling, shadows look wrong
        float textStartX = centerX - (textWidth / 2.0f);
        addString(mainDigits, textStartX, rowY, Justify::LEFT,
                  this->getFont(FontCategory::DIGITS), textNormal, fontSize, true);

        // Last digit (black, right-aligned in white quad)
        // skipShadow=true: inverted digit (black on white) should not have shadow
        float lastCharRightX = lastCharX + charWidth;
        addString(lastDigit, lastCharRightX, rowY, Justify::RIGHT,
                  this->getFont(FontCategory::DIGITS), bgNormal, fontSize, true);
    };

    // Odometer: 6 digits, last digit = 1 km (e.g., "000078" = 78 km)
    if (m_showOdometer) {
        char odometerDisplay[16];
        snprintf(odometerDisplay, sizeof(odometerDisplay), "%06d", odometerWhole);
        float odometerY = startY + dialHeight * 0.33f;
        addOdometerRow(odometerDisplay, odometerY);
    }

    // Trip meter: 4 digits, last digit = 0.1 km (e.g., "0078" = 7.8 km)
    if (m_showTripmeter) {
        int tripTenths = static_cast<int>(tripDist * 10.0 + 0.5) % 10000;  // Wrap at 999.9
        char tripDisplay[16];
        snprintf(tripDisplay, sizeof(tripDisplay), "%04d", tripTenths);
        float tripY = startY + dialHeight * 0.66f;
        addOdometerRow(tripDisplay, tripY);
    }

    // Add needle quad LAST so it renders on top of everything
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
    m_needleColor = DEFAULT_NEEDLE_COLOR;
    m_showOdometer = true;   // Odometer ON by default
    m_showTripmeter = false; // Trip meter OFF by default
    setDataDirty();
}
