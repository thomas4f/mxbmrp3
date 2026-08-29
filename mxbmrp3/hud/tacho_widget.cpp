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
    // A PACK HUD: the face comes from gauges/<name>/tacho.tga, set through
    // setBackgroundTextureIndex() on every rebuild, so there is no texture stem to
    // declare and no variant cycle to offer. Declaring the stem as well would put
    // the widget in both worlds at once -- see BaseHud::setTextureVariant, where a
    // stale textureVariant key in an upgraded INI turned a pack HUD's art off.
    m_packKind = PackKind::Gauges;
    DEBUG_INFO("TachoWidget created");
    setDraggable(true);
    m_quads.reserve(2);  // dial background + needle

    // Set all configurable defaults
    resetToDefaults();

    rebuildRenderData();
}

void TachoWidget::setGaugesPack(const std::string& name) {
    if (m_gaugesPack == name) return;
    m_gaugesPack = name;
    setDataDirty();
}

const GaugesAsset* TachoWidget::activePack() const {
    const AssetManager& assets = AssetManager::getInstance();
    // Degrade, do not blank -- see PitboardHud::activePack for the full rule.
    if (const GaugesAsset* named = assets.getGaugesByName(m_gaugesPack)) return named;
    return assets.getDefaultGauges();
}

unsigned long TachoWidget::effectiveNeedleColor() const {
    if (m_needleColorSet) return m_needleColor;
    if (const GaugesAsset* pack = activePack())
        return static_cast<unsigned long>(pack->geometry.tacho.needleColor);
    return m_needleColor;
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

    // What THIS face reads and how its needle is drawn. The fallback is the
    // built-in geometry rather than a blank gauge: with no packs installed at all
    // the widget draws the scale it always drew.
    const GaugesAsset* pack = activePack();
    static const GaugeLayout::GaugeGeometry kNoPackGeometry;
    const GaugeLayout::Dial& dial =
        pack ? pack->geometry.tacho : kNoPackGeometry.tacho;

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

    // Keep BaseHud's background sprite pointing at the active pack's tacho face.
    // Assigned only on change: setBackgroundTextureIndex invalidates the theme memo.
    // (Same two lines PitboardHud runs, for the same reason.)
    const int packFace = pack ? pack->sprites[GaugeSprite::TACHO] : 0;
    if (getBackgroundTextureIndex() != packFace) setBackgroundTextureIndex(packFace);

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

    // Clamp to THIS face's range before smoothing, so the needle settles at the
    // end of the dial rather than easing towards a reading the face cannot show.
    if (targetRpm < dial.min) targetRpm = dial.min;
    if (targetRpm > dial.max) targetRpm = dial.max;

    // Apply exponential smoothing to simulate needle inertia
    // smoothed = smoothed + (target - smoothed) * factor
    m_smoothedRpm += (targetRpm - m_smoothedRpm) * NEEDLE_SMOOTH_FACTOR;

    // The pack's own sweep. Dial::angleFor is the expression that used to be
    // written out here against compiled constants; it clamps too, so a face whose
    // range changed under a parked needle cannot swing it off the dial.
    float angleRad = dial.angleFor(m_smoothedRpm) * DEG_TO_RAD;

    // Needle dimensions, as fractions of the dial this pack drew.
    float needleLength = dialHeight * dial.needleLength;
    float needleWidth = dialHeight * dial.needleWidth;

    // Add needle quad (centered on dial, rotated based on RPM).
    // Needle fades with the dial: the whole gauge responds to the background-opacity
    // slider as one, rather than leaving a solid needle floating over a faded dial.
    addNeedleQuad(centerX, centerY, angleRad, needleLength, needleWidth,
                  PluginUtils::applyOpacity(effectiveNeedleColor(), m_fBackgroundOpacity));
}

void TachoWidget::resetToDefaults() {
    m_bVisible = false;
    m_bShowTitle = false;
    // The dial face IS this widget, exactly as the pad's and the board's art is
    // theirs -- so this says so, the same way they do. It used to be implied by
    // setTextureVariant(1), which was the ONLY thing here setting the flag;
    // dropping that call when the rev-counter became a pack HUD left the flag at
    // BaseHud's `false`, so a fresh install drew a needle on an empty box and
    // then persisted showBackgroundTexture=0. Through the setter, not the
    // member: it owns the theme-memo invalidation, and check_hud_helpers.sh
    // fails a HUD that touches the member directly.
    setShowBackgroundTexture(true);
    m_fBackgroundOpacity = 1.0f;  // 100% opacity
    m_fScale = 1.0f;  // 100% default scale
    setPosition(cellsX(112), cellsY(71));
    m_smoothedRpm = 0.0f;
    // EMPTY, not "classic": empty means "whatever the default pack resolves to",
    // which is how an upgrading user whose own art was migrated into gauges\\legacy
    // gets it back without a settings key that could not have existed when their
    // file was written. See AssetManager::getDefaultGauges.
    m_gaugesPack.clear();
    // Assigned directly, NOT through setNeedleColor: going through the setter
    // would latch m_needleColorSet and make the factory default look like a user
    // choice, which is exactly what stops a pack shipping its own needle colour.
    m_needleColor = DEFAULT_NEEDLE_COLOR;
    m_needleColorSet = false;
    setDataDirty();
}
