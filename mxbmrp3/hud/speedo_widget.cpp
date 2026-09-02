// ============================================================================
// hud/speedo_widget.cpp
// Speedo widget - displays rotating needle (0-230 km/h) with dial background
// ============================================================================
#include "speedo_widget.h"
#include "../core/layout_config.h"
#include "speed_widget.h"  // For SpeedUnit enum

#include <cstdio>
#include <cmath>

#include "../diagnostics/logger.h"
#include "../core/plugin_utils.h"
#include "../core/stats_manager.h"
#include "../core/color_config.h"
#include "../core/hud_manager.h"

using namespace PluginConstants;
using namespace PluginConstants::Math;

SpeedoWidget::SpeedoWidget()
{
    // No caption on this panel -- see BaseHud::m_titleSupported.
    disableTitle();
    m_panelKind = PanelKind::Widget;
    m_bContentCard = true;
    // One-time setup
        // The dial face IS this gauge -- see BaseHud::m_textureRequired. Without it
    // the widget is a red needle on an empty box: no face, no ticks, no numbers.
    m_textureRequired = true;
    // A PACK HUD -- see TachoWidget's constructor for why no texture stem is
    // declared alongside.
    m_packKind = PackKind::Gauges;
    DEBUG_INFO("SpeedoWidget created");
    setDraggable(true);
    m_quads.reserve(6);   // dial background + needle + 4 odometer background quads
    m_strings.reserve(5); // odometer: main + last + unit, trip: main + last

    // Set all configurable defaults
    resetToDefaults();

    rebuildRenderData();
}

void SpeedoWidget::setGaugesPack(const std::string& name) {
    if (m_gaugesPack == name) return;
    m_gaugesPack = name;
    setDataDirty();
}

const GaugesAsset* SpeedoWidget::activePack() const {
    const AssetManager& assets = AssetManager::getInstance();
    // Degrade, do not blank -- see PitboardHud::activePack for the full rule.
    if (const GaugesAsset* named = assets.getGaugesByName(m_gaugesPack)) return named;
    return assets.getDefaultGauges();
}

unsigned long SpeedoWidget::effectiveNeedleColor() const {
    if (m_needleColorSet) return m_needleColor;
    if (const GaugesAsset* pack = activePack())
        return static_cast<unsigned long>(pack->geometry.speedo.needleColor);
    return m_needleColor;
}

bool SpeedoWidget::handlesDataType(DataChangeType dataType) const {
    // Update on telemetry changes (bike data)
    return dataType == DataChangeType::InputTelemetry ||
           dataType == DataChangeType::SpectateTarget;
}

void SpeedoWidget::update() {
    // OPTIMIZATION: Skip processing when not visible
    if (!isVisibleAnySurface()) {
        clearDataDirty();
        clearLayoutDirty();
        return;
    }

    // Always rebuild - speed updates at high frequency (telemetry rate)
    // Rebuild is cheap (single quad calculation), no need for caching
    rebuildAndRecord();
    clearDataDirty();
    clearLayoutDirty();
}

void SpeedoWidget::rebuildLayout() {
    // Fast path - only update positions (not colors/opacity)
    // For this widget, full rebuild is still cheap, just call rebuildRenderData
    rebuildRenderData();
}

void SpeedoWidget::rebuildRenderData() {
    // Clear render data
    clearStrings();
    m_quads.clear();

    // Get bike telemetry data
    const PluginData& pluginData = PluginData::getInstance();
    const BikeTelemetryData& bikeData = pluginData.getBikeTelemetry();

    // What THIS face reads, how its needle is drawn, and where its readout windows
    // are. Falls back to the built-in geometry rather than a blank gauge when no
    // packs are installed at all.
    const GaugesAsset* pack = activePack();
    static const GaugeLayout::GaugeGeometry kNoPackGeometry;
    const GaugeLayout::GaugeGeometry& gauges =
        pack ? pack->geometry : kNoPackGeometry;
    const GaugeLayout::Dial& dial = gauges.speedo;

    // Calculate dial dimensions based on scale
    float dialSize = DIAL_SIZE * m_fScale;
    float dialWidth = dialSize / UI_ASPECT_RATIO;
    float dialHeight = dialSize;

    // Start pivot at (0,0) relative coordinates - the m_fOffsetX/Y values position the widget on screen
    float startX = 0.0f;
    float startY = 0.0f;

    // The BOX lands on the lattice, the DIAL keeps its size -- it is drawn circular by
    // dividing by UI_ASPECT_RATIO, and stretching it to whole cells on both axes would
    // make it an ellipse. See fitPanelToGrid.
    const GridFit fit = fitPanelToGrid(dialWidth, dialHeight);

    // Set bounds for drag detection (relative coordinates, offset applied by base class)
    setBounds(startX, startY, startX + fit.w, startY + fit.h);
    startX += fit.padX;
    startY += fit.padY;

    // Calculate center of dial
    float centerX = startX + dialWidth / 2.0f;
    float centerY = startY + dialHeight / 2.0f;

    // Keep BaseHud's background sprite pointing at the active pack's speedo face.
    // Assigned only on change: setBackgroundTextureIndex invalidates the theme memo.
    const int packFace = pack ? pack->sprites[GaugeSprite::SPEEDO] : 0;
    if (getBackgroundTextureIndex() != packFace) setBackgroundTextureIndex(packFace);

    // Add dial as background quad (uses base class helper for consistency with PitboardHud)
    // BG Tex ON: shows dial sprite with opacity
    // BG Tex OFF: shows solid black with opacity
    addBackgroundQuad(startX, startY, dialWidth, dialHeight);
    // No caption at all here -- not a title toggle switched off, NO title -- so
    // nothing reaches the caption path, and that is what emits the body card
    // the constructor asked for with m_bContentCard. Without this the flag draws
    // nothing while still reserving the card's clearance (contentPaddingX reads the
    // same flag), so a themed panel is a frame around bare content with an
    // unexplained cell of padding inside it. Same call the captioned widgets make
    // from their `else` branch.
    //
    // A sprite background opts a panel out of theming entirely, upstream of this
    // (resolveActiveTheme), so on the widgets that ship with one this is inert until
    // the sprite is switched off -- at which point they get the same treatment as
    // every other widget.
    // check_hud_helpers.sh rule 10 fails the build if a new one forgets the call.
    emitContentCard(0.0f);

    // Get target speed in km/h from telemetry
    float targetSpeed = 0.0f;
    if (bikeData.isValid) {
        targetSpeed = bikeData.speedometer * UnitConversion::MS_TO_KMH;
    }

    // Clamp to THIS face's range before smoothing, so the needle settles at the end
    // of the dial rather than easing towards a reading the face cannot show.
    // The range is in km/h whatever unit the face is PRINTED in -- see
    // gauge_geometry.h, and the pack's max-mph key.
    if (targetSpeed < dial.min) targetSpeed = dial.min;
    if (targetSpeed > dial.max) targetSpeed = dial.max;

    // Apply exponential smoothing to simulate needle inertia
    // smoothed = smoothed + (target - smoothed) * factor
    m_smoothedSpeed += (targetSpeed - m_smoothedSpeed) * NEEDLE_SMOOTH_FACTOR;

    // The pack's own sweep -- see Dial::angleFor.
    float angleRad = dial.angleFor(m_smoothedSpeed) * DEG_TO_RAD;

    // Needle dimensions, as fractions of the dial this pack drew.
    float needleLength = dialHeight * dial.needleLength;
    float needleWidth = dialHeight * dial.needleWidth;

    // ========================================================================
    // Odometer display - traditional analog style with black background
    // Format: 6 digits (5 whole + 1 tenth), last digit inverted (white bg, black text)
    // ========================================================================

    // Get speed unit preference from SpeedWidget (shared setting)
    SpeedWidget::SpeedUnit speedUnit = HudManager::getInstance().getSpeedWidget().getSpeedUnit();
    bool useMiles = (speedUnit == SpeedWidget::SpeedUnit::MPH);

    // Get distances and convert based on unit preference
    // Meters to km: /1000, Meters to miles: /1609.344
    const StatsManager& stats = StatsManager::getInstance();
    double odometerMeters = stats.getOdometerForCurrentBike();
    double tripMeters = stats.getSessionTripDistance();
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
    float fontSize = layoutDefaults().fontSizeSmall * m_fScale;
    float charWidth = fontSize * layout().charWidthRatio;
    float charHeight = layoutDefaults().lineHeightSmall * m_fScale;

    // Padding - make uniform on all sides (vertical padding comes from lineHeight > fontSize)
    float paddingV = (charHeight - fontSize) / 2.0f;
    float paddingH = paddingV;  // Same padding horizontally

    // Colors - semantic colors from user's color scheme.
    // Faded by the background-opacity slider so the odometer/tripmeter readout fades
    // together with the dial and needle (the whole gauge responds as one).
    unsigned long bgNormal = PluginUtils::applyOpacity(this->getColor(ColorSlot::BACKGROUND), m_fBackgroundOpacity);
    unsigned long textNormal = PluginUtils::applyOpacity(this->getColor(ColorSlot::PRIMARY), m_fBackgroundOpacity);

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
        float odometerY = startY + dialHeight * gauges.odometerY;
        addOdometerRow(odometerDisplay, odometerY);
    }

    // Trip meter: 4 digits, last digit = 0.1 km (e.g., "0078" = 7.8 km)
    if (m_showTripmeter) {
        int tripTenths = static_cast<int>(tripDist * 10.0 + 0.5) % 10000;  // Wrap at 999.9
        char tripDisplay[16];
        snprintf(tripDisplay, sizeof(tripDisplay), "%04d", tripTenths);
        float tripY = startY + dialHeight * gauges.tripmeterY;
        addOdometerRow(tripDisplay, tripY);
    }

    // Add needle quad LAST so it renders on top of everything.
    // Needle fades with the dial so the whole gauge responds to the background-opacity
    // slider as one (matches the faded odometer above).
    addNeedleQuad(centerX, centerY, angleRad, needleLength, needleWidth,
                  PluginUtils::applyOpacity(effectiveNeedleColor(), m_fBackgroundOpacity));
}

void SpeedoWidget::resetToDefaults() {
    m_bVisible = false;
    m_bShowTitle = false;
    // The dial face IS this widget, exactly as the pad's and the board's art is
    // theirs -- so this says so, the same way they do. Left at BaseHud's `false`,
    // a fresh install draws a needle on an empty box and then persists
    // showBackgroundTexture=0. Through the setter, not the member: it owns the
    // theme-memo invalidation, and check_hud_helpers.sh fails a HUD that touches
    // the member directly.
    setShowBackgroundTexture(true);
    m_fBackgroundOpacity = 1.0f;  // 100% opacity
    m_fScale = 1.5f;  // 150% default scale
    setPosition(cellsX(125), cellsY(64));
    m_smoothedSpeed = 0.0f;
    // EMPTY, not "classic": empty means "whatever the default pack resolves to",
    // which is how an upgrading user whose own art was migrated into gauges\\legacy
    // gets it back without a settings key that could not have existed when their
    // file was written. See AssetManager::getDefaultGauges.
    m_gaugesPack.clear();
    // Assigned directly, NOT through setNeedleColor -- see TachoWidget.
    m_needleColor = DEFAULT_NEEDLE_COLOR;
    m_needleColorSet = false;
    m_showOdometer = true;   // Odometer ON by default
    m_showTripmeter = false; // Trip meter OFF by default
    setDataDirty();
}
