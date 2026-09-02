// ============================================================================
// hud/gear_widget.cpp
// Gear widget - displays current gear with shift/limiter indicators
// ============================================================================
#include "gear_widget.h"

#include "gear_geometry.h"

#include <cstdio>

#include "../diagnostics/logger.h"
#include "../diagnostics/timer.h"
#include "../core/plugin_utils.h"
#include "../core/color_config.h"
#include "../core/asset_manager.h"

using namespace PluginConstants;

GearWidget::GearWidget()
{
    m_panelKind = PanelKind::Widget;
    m_bContentCard = true;
    DEBUG_INFO("GearWidget created");
    setDraggable(true);
    m_quads.reserve(2);    // Background + gear circle
    m_strings.reserve(2);  // Title (optional) + gear value

    setTextureBaseName("gear_widget");

    resetToDefaults();
    rebuildRenderData();
}

bool GearWidget::handlesDataType(DataChangeType dataType) const {
    return dataType == DataChangeType::InputTelemetry ||
           dataType == DataChangeType::SpectateTarget;
}

void GearWidget::update() {
    if (!isVisibleAnySurface()) {
        clearDataDirty();
        clearLayoutDirty();
        return;
    }

    // Always rebuild - gear updates at telemetry rate, rebuild is cheap
    rebuildAndRecord();
    clearDataDirty();
    clearLayoutDirty();
}

void GearWidget::rebuildLayout() {
    // BOX-MODEL: one source of geometry — rebuild rather than reposition a
    // duplicated copy of the sizing arithmetic (see version_widget).
    rebuildRenderData();
}

void GearWidget::rebuildRenderData() {
    clearStrings();
    m_quads.clear();

    auto dim = getScaledDimensions();
    float gearRowHeight = dim.lineHeightLarge + dim.lineHeightNormal;  // Match SpeedWidget content height (value + units)
    // From the FONT, capped by the BOX -- see gear_geometry.h for why the row
    // height alone is wrong.
    float gearFontSize = GearGeometry::fontSize(dim.fontSize, gearRowHeight);

    const PluginData& pluginData = PluginData::getInstance();
    const BikeTelemetryData& bikeData = pluginData.getBikeTelemetry();
    const SessionData& sessionData = pluginData.getSessionData();

    float startX = 0.0f;
    float startY = 0.0f;

    // BOX-MODEL: the plan owns padding, chrome, the title band and the card;
    // the widget states only its content — the single gear row.
    BaseHud::PanelWant want;
    want.contentW = PluginUtils::calculateMonospaceTextWidth(WidgetDimensions::GEAR_WIDTH, dim.fontSize);
    want.sectionH = { gearRowHeight };
    want.captionW = planTitleWidth(dim, "Gear");
    PanelPlan& p = planPanel(dim, want);

    const float backgroundWidth = p.width();
    const float backgroundHeight = p.height();

    unsigned long textColor = this->getColor(ColorSlot::PRIMARY);

    addPlanBackground(p, startX, startY);
    addPlanTitle(p, "Gear", this->getFont(FontCategory::TITLE), textColor);
    float currentY = p.contentY();
    // THE ROW THE PLAN LAID OUT, not the one asked for: the last section's box absorbs
    // the panel's ceil remainder (panel_box.h), so at some uiLineHeight values it is
    // taller than gearRowHeight by up to a cell. Centring in the ask instead puts the
    // digit high by half that slack -- ~5px at 1080p with uiLineHeight 1.8, and nothing
    // at the shipped default. Same read-back the gap bar makes for its bar.
    const float gearRowDrawnH = p.H(p.g.sections[0].h);
    // The gear digit is centered on the CARD (PanelPlan::sectionBoxCenterX --
    // the panel's centre is the same place only while the [content] terms are
    // left/right symmetric); the caption sits at the plan's own column.
    float centerX = p.sectionBoxCenterX();

    // Format gear string
    char gearValueBuffer[8];
    if (!bikeData.isValid) {
        snprintf(gearValueBuffer, sizeof(gearValueBuffer), "%s", Placeholders::GENERIC);
    } else if (bikeData.numberOfGears <= 1) {
        // Gearless vehicle (e-bike, direct-drive kart) - "N" would be misleading
        snprintf(gearValueBuffer, sizeof(gearValueBuffer), "D");
    } else {
        if (bikeData.gear == GearValue::NEUTRAL) {
            snprintf(gearValueBuffer, sizeof(gearValueBuffer), "N");
        } else {
            snprintf(gearValueBuffer, sizeof(gearValueBuffer), "%d", bikeData.gear);
        }
    }

    // RPM-based indicators only apply to the player's own bike
    bool isViewingPlayer = (pluginData.getDisplayRaceNum() == pluginData.getPlayerRaceNum());

    // Add gear circle indicator if limiter RPM is reached (behind gear text)
    bool isLimiterHit = (m_bShowLimiterCircle && bikeData.isValid && isViewingPlayer && sessionData.limiterRPM > 0 && bikeData.rpm >= sessionData.limiterRPM);

    if (isLimiterHit) {
        float circleSize = gearFontSize * 1.5f;
        float circleWidth = circleSize / PluginConstants::UI_ASPECT_RATIO;
        float circleHeight = circleSize;

        float circleX = centerX - (circleWidth / 2.0f);
        // Centred in the gear ROW, with none of the text's y solve. inkCenteredY is
        // the TEXT's: it undoes addString's row centring and shifts the glyph cell so
        // the digit's INK lands mid-row. A quad has neither problem, so borrowing that
        // y would move the circle off the digit it is drawn around, by a third of the
        // gear font. The ink IS centred in the row, so centring the circle in the row is
        // what makes the two concentric.
        float circleTopY = currentY + (gearRowDrawnH - circleHeight) / 2.0f;

        SPluginQuad_t circleQuad{};
        applyOffset(circleX, circleTopY);
        setQuadPositions(circleQuad, circleX, circleTopY, circleWidth, circleHeight);
        circleQuad.m_iSprite = AssetManager::getInstance().getSpriteIndex("gear_circle", 1);
        circleQuad.m_ulColor = ColorPalette::WHITE;
        m_quads.push_back(circleQuad);
    }

    // Gear color: red if recommended shift point reached, otherwise primary.
    // Skip shift coloring on gearless vehicles (nothing to shift to).
    unsigned long gearColor = (m_bShowShiftColor && bikeData.isValid && isViewingPlayer
                               && bikeData.numberOfGears > 1
                               && sessionData.shiftRPM > 0 && bikeData.rpm >= sessionData.shiftRPM)
        ? this->getColor(ColorSlot::NEGATIVE)
        : textColor;
    // INK-CENTRED IN THE ROW -- the same solve TimingHud's big time, the gap bar's
    // gap text and every bigValueTextY caller use.
    //
    // A hand-tuned nudge, a fixed fraction OF THE GEAR FONT, centres the digit only
    // while the font is itself a multiple of the row (both scaling with uiLineHeight
    // together, so the ratio holds by coincidence). The font is sized from the base
    // font instead (gear_geometry.h), so such a nudge leaves the digit sitting high
    // in a raised row. inkCenteredY has no such dependency -- it solves
    // for the ink's position in an arbitrary box at an arbitrary font size, which is
    // exactly this case, and it also undoes addString's own row centring, which grows
    // with uiLineHeight and would otherwise drift on its own.
    addString(gearValueBuffer, centerX, inkCenteredY(currentY, gearRowDrawnH, gearFontSize),
        Justify::CENTER, this->getFont(FontCategory::TITLE), gearColor, gearFontSize);

    setBounds(startX, startY, startX + backgroundWidth, startY + backgroundHeight);
}

void GearWidget::resetToDefaults() {
    m_bVisible = true;
    m_bShowTitle = false;
    setTextureVariant(0);
    m_fBackgroundOpacity = 0.0f;  // Transparent by default
    m_fScale = 1.0f;
    m_bShowShiftColor = true;
    m_bShowLimiterCircle = true;
    setPosition(cellsX(161), cellsY(74));
    setDataDirty();
}
