// ============================================================================
// hud/now_playing_widget.cpp
// Now Playing widget - see now_playing_widget.h.
// ============================================================================
#include "now_playing_widget.h"

#include "default_slots.h"
#include "settings_hud.h"
#include "../core/hud_manager.h"
#include "../core/input_manager.h"
#include "../core/plugin_utils.h"

using namespace PluginConstants;

NowPlayingWidget::NowPlayingWidget() {
    m_panelKind = PanelKind::Widget;
    m_bContentCard = true;
    setDraggable(true);
    m_quads.reserve(12);
    m_strings.reserve(6);   // title (optional), track title, artist, marks for missing icons
    setTextureBaseName("now_playing_widget");
    resetToDefaults();
    rebuildRenderData();
}

bool NowPlayingWidget::handlesDataType(DataChangeType /*dataType*/) const {
    // MediaManager is not PluginData: update() follows its revision.
    return false;
}

void NowPlayingWidget::update() {
    followInput();
    if (!isVisibleAnySurface()) {
        clearDataDirty();
        clearLayoutDirty();
        return;
    }
    // The worker bumps the revision when the track, its time or the volume moves
    // (once a second while playing), so this rebuilds at most about that often.
    if (m_card.changed()) setDataDirty();
    if (isDataDirty()) {
        rebuildAndRecord();
        clearDataDirty();
        clearLayoutDirty();
    } else if (isLayoutDirty()) {
        rebuildLayout();
        clearLayoutDirty();
    }
}

void NowPlayingWidget::followInput() {
    // Gated like the Dash page: on screen, the pointer showing, the menu shut
    // (a click on the menu must not reach a widget under it).
    const InputManager& input = InputManager::getInstance();
    const bool live = isVisibleAnySurface() && !isHeldBack() && input.shouldShowCursor() &&
                      !HudManager::getInstance().getSettingsHud().isVisible();   // vis-gate: the menu's own state
    CursorPosition cursor = input.getCursorPosition();
    mapCursorToHudSpace(cursor.x, cursor.y);
    const MouseButton& left = input.getLeftButton();
    if (m_card.follow(live && cursor.isValid, cursor.x - getOffsetX(), cursor.y - getOffsetY(),
                      left.isClicked(), left.isPressed)) {
        setDataDirty();
    }
}

void NowPlayingWidget::rebuildLayout() {
    // BOX-MODEL: one source of geometry.
    rebuildRenderData();
}

void NowPlayingWidget::rebuildRenderData() {
    clearStrings();
    m_quads.clear();

    const auto dim = getScaledDimensions();
    PanelWant want;
    want.contentW = PluginUtils::calculateMonospaceTextWidth(WIDTH_CHARS, dim.fontSize);
    want.sectionH = { dim.lineHeightNormal * static_cast<float>(NowPlayingCard::ROWS) };
    want.captionW = planTitleWidth(dim, "Now Playing");
    PanelPlan& p = planPanel(dim, want);

    addPlanBackground(p, 0.0f, 0.0f);
    addPlanTitle(p, "Now Playing", getColor(ColorSlot::PRIMARY));

    NowPlayingCard::Style style;
    style.primary = getColor(ColorSlot::PRIMARY);
    style.secondary = getColor(ColorSlot::SECONDARY);
    style.tertiary = getColor(ColorSlot::TERTIARY);
    style.muted = getColor(ColorSlot::MUTED);
    style.accent = getColor(ColorSlot::ACCENT);
    const int font = getFont(FontCategory::NORMAL);
    m_card.layout(p.contentX(), p.contentRight(), p.contentY(), dim.lineHeightNormal, dim.fontSize, font, style);
    for (const NowPlayingCard::Prim& prim : m_card) {
        switch (prim.kind) {
            case NowPlayingCard::Prim::Kind::BAR:
                addHorizontalGridLine(prim.x, prim.y + prim.h * 0.5f, prim.w, prim.color, prim.h);
                break;
            case NowPlayingCard::Prim::Kind::TEXT:
                addString(prim.text, prim.x, prim.y, prim.center ? Justify::CENTER : Justify::LEFT, font,
                          prim.color, dim.fontSize);
                break;
            case NowPlayingCard::Prim::Kind::ICON:
                addIcon(prim.x, prim.y, prim.sprite, prim.color, prim.h);
                break;
        }
    }

    setBounds(0.0f, 0.0f, p.width(), p.height());
}

void NowPlayingWidget::resetToDefaults() {
    m_bVisible = false;
    m_bShowTitle = false;
    setTextureVariant(0);
    m_fBackgroundOpacity = SettingsLimits::DEFAULT_OPACITY;
    setScale(1.0f);
    // The right column's spot, as the Ideal Lap HUD (default_slots.h).
    setPosition(DefaultSlots::rightColumnX(DefaultSlots::NARROW_CELLS), DefaultSlots::rightColumnY());
    setDataDirty();
}
