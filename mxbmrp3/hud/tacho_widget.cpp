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
    // No caption on this panel -- see BaseHud::m_titleSupported.
    disableTitle();
    m_panelKind = PanelKind::Widget;
    m_bContentCard = true;
    // One-time setup
        // The dial face IS this gauge -- see BaseHud::m_textureRequired. Without it
    // the widget is a red needle on an empty box: no face, no ticks, no numbers.
    m_textureRequired = true;
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
    if (!isVisibleAnySurface()) {
        clearDataDirty();
        clearLayoutDirty();
        return;
    }

    // Always rebuild - RPM updates at high frequency (telemetry rate)
    // Rebuild is cheap (single quad calculation), no need for caching
    rebuildAndRecord();
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

    // The BOX lands on the lattice, the DIAL keeps its size -- see SpeedoWidget and
    // fitPanelToGrid. Stretching a circle to whole cells on both axes is an ellipse.
    const GridFit fit = fitPanelToGrid(dialWidth, dialHeight);

    // Set bounds for drag detection (relative coordinates, offset applied by base class)
    setBounds(startX, startY, startX + fit.w, startY + fit.h);
    startX += fit.padX;
    startY += fit.padY;

    // Calculate center of dial
    float centerX = startX + dialWidth / 2.0f;
    float centerY = startY + dialHeight / 2.0f;

    // Add dial as background quad (uses base class helper for consistency with PitboardHud)
    // BG Tex ON: shows dial sprite with opacity
    // BG Tex OFF: shows solid black with opacity
    addBackgroundQuad(startX, startY, dialWidth, dialHeight);
    // No caption at all here -- not a title toggle switched off, NO title -- so
    // nothing reaches the caption path, and that is what emits the body card
    // the constructor asked for with m_bContentCard. Without this the flag drew
    // nothing while still reserving the card's clearance (contentPaddingX reads the
    // same flag), so a themed panel was a frame around bare content with an
    // unexplained cell of padding inside it. Same call the captioned widgets make
    // from their `else` branch.
    //
    // A sprite background opts a panel out of theming entirely, upstream of this
    // (resolveActiveTheme), so on the widgets that ship with one this is inert until
    // the sprite is switched off -- at which point they get the same treatment as
    // every other widget instead of the one they had.
    // check_hud_helpers.sh rule 10 fails the build if a new one forgets the call.
    emitContentCard(0.0f);

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

    // Add needle quad (centered on dial, rotated based on RPM).
    // Needle fades with the dial: the whole gauge responds to the background-opacity
    // slider as one, rather than leaving a solid needle floating over a faded dial.
    addNeedleQuad(centerX, centerY, angleRad, needleLength, needleWidth,
                  PluginUtils::applyOpacity(m_needleColor, m_fBackgroundOpacity));
}

void TachoWidget::resetToDefaults() {
    m_bVisible = false;
    m_bShowTitle = false;
    setTextureVariant(1);  // Show dial texture by default
    m_fBackgroundOpacity = 1.0f;  // 100% opacity
    m_fScale = 1.0f;  // 100% default scale
    setPosition(cellsX(112), cellsY(71));
    m_smoothedRpm = 0.0f;
    m_needleColor = DEFAULT_NEEDLE_COLOR;
    setDataDirty();
}
