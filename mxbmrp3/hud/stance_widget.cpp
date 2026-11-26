// ============================================================================
// hud/stance_widget.cpp
// Stance widget - whether the rider is standing on the pegs or seated
// ============================================================================
#include "stance_widget.h"
#include "../core/i18n.h"

#include "gear_geometry.h"

#include "../core/asset_manager.h"
#include "../core/plugin_data.h"
#include "../core/plugin_utils.h"
#include "../core/rider_stance.h"
#include "../core/settings_manager.h"
#include "../core/widget_constants.h"

using namespace PluginConstants;

namespace {
    // The gap between the pair's two icons, as a share of the pair's height.
    // Matched by eye against the gear digit beside it.
    constexpr float PAIR_GAP_SHARE = 0.14f;
}

StanceWidget::StanceWidget()
{
    m_panelKind = PanelKind::Widget;
    m_bContentCard = true;
    setDraggable(true);
    m_quads.reserve(3);    // Background + two icons
    m_strings.reserve(1);  // Title (optional)

    setTextureBaseName("stance_widget");

    resetToDefaults();
    rebuildRenderData();
}

bool StanceWidget::isAvailable() {
    // Developer mode is a second key on the same lock, as on the Prestige badge:
    // the widget has to be layout-testable on a game build that sends nothing.
    return RiderStance::gameSendsIt() || SettingsManager::getInstance().isDeveloperMode();
}

bool StanceWidget::handlesDataType(DataChangeType dataType) const {
    // Not InputTelemetry: that fires every frame, and update() compares the
    // stance itself instead (rebuilding only when it changes).
    return dataType == DataChangeType::SpectateTarget;
}

void StanceWidget::update() {
    if (!isVisibleAnySurface()) {
        clearDataDirty();
        clearLayoutDirty();
        return;
    }

    // Rebuilt when what it draws changes, not on every telemetry frame: the
    // stance flips a few times a lap, and the compare is the whole cost between.
    const int stance = PluginData::getInstance().getBikeTelemetry().riderStance;
    if (isDataDirty() || isLayoutDirty() ||
        stance != m_builtStance || isAvailable() != m_builtAvailable) {
        rebuildAndRecord();
    }
    clearDataDirty();
    clearLayoutDirty();
}

void StanceWidget::rebuildLayout() {
    // BOX-MODEL: one source of geometry (see gear_widget).
    rebuildRenderData();
}

void StanceWidget::rebuildRenderData() {
    clearStrings();
    m_quads.clear();

    m_builtStance = PluginData::getInstance().getBikeTelemetry().riderStance;

    // Not on this game build: draw nothing and clear the bounds, so there is no
    // invisible box to drag or click.
    m_builtAvailable = isAvailable();
    if (!m_builtAvailable) {
        setBounds(0.0f, 0.0f, 0.0f, 0.0f);
        return;
    }

    // The Gear widget's box: its width, its two-line row, its digit size.
    const auto dim = getScaledDimensions();
    const float rowHeight = dim.lineHeightLarge + dim.lineHeightNormal;
    const float gearFontSize = GearGeometry::fontSize(dim.fontSize, rowHeight);

    PanelWant want;
    want.contentW = PluginUtils::calculateMonospaceTextWidth(WidgetDimensions::GEAR_WIDTH, dim.fontSize);
    want.sectionH = { rowHeight };
    want.captionW = planTitleWidth(dim, TR("Stance"));
    PanelPlan& p = planPanel(dim, want);

    addPlanBackground(p, 0.0f, 0.0f);
    addPlanTitle(p, TR("Stance"), this->getColor(ColorSlot::PRIMARY));

    // The icons span the gear digit's ink, centred in the row the plan laid out
    // (the gear widget's read-back: the last section absorbs the panel's ceil
    // remainder) and on the card.
    const float rowY = p.contentY();
    const float rowDrawnH = p.H(p.g.sections[0].h);
    const float inkH = gearFontSize * GearGeometry::DIGIT_INK_SHARE;
    const float centerX = p.sectionBoxCenterX();
    const float centerY = rowY + rowDrawnH * 0.5f;

    const unsigned long lit = this->getColor(ColorSlot::ACCENT);
    const unsigned long unlit = this->getColor(ColorSlot::MUTED);
    const AssetManager& assets = AssetManager::getInstance();
    const int sprite = (m_iconShape > 0) ? assets.iconSpriteForShape(m_iconShape)
                                         : assets.getIconSpriteIndex(DEFAULT_ICON);

    // Pointing up, or turned 180 degrees in place: a turn, not a mirror, so the
    // winding the engine needs is kept (addRotatedSpriteQuad).
    auto addStanceIcon = [&](float cy, float size, bool up, unsigned long color) {
        addRotatedSpriteQuad(centerX, cy, size * 0.5f, up ? 1.0f : -1.0f, 0.0f, sprite, color);
    };

    if (m_mode == Mode::Pair) {
        const float gap = inkH * PAIR_GAP_SHARE;
        const float size = (inkH - gap) * 0.5f;
        const float step = (size + gap) * 0.5f;
        addStanceIcon(centerY - step, size, true, m_builtStance == 1 ? lit : unlit);
        addStanceIcon(centerY + step, size, false, m_builtStance == 0 ? lit : unlit);
    } else if (m_mode == Mode::Flip) {
        // Up while the stance is unknown, muted like the pair.
        addStanceIcon(centerY, inkH, m_builtStance != 0, m_builtStance >= 0 ? lit : unlit);
    } else if (m_builtStance == 1) {
        addStanceIcon(centerY, inkH, true, lit);
    }

    setBounds(0.0f, 0.0f, p.width(), p.height());
}

void StanceWidget::resetToDefaults() {
    m_bVisible = false;   // Off by default
    m_bShowTitle = false;
    setTextureVariant(0);
    m_fBackgroundOpacity = 0.0f;  // Transparent, like Speed and Gear beside it
    m_mode = Mode::Pair;
    m_iconShape = 0;      // DEFAULT_ICON
    setScale(1.0f);
    // Left of the Gear widget, the step Gear keeps from Speed.
    setPosition(cellsX(154), cellsY(74));
    setDataDirty();
}
