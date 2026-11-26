// ============================================================================
// hud/achievement_widget.cpp
// See the header. Render shape: the standard widget card, an icon column on the
// left (the entry's marker sprite, tinted to its tier's metal), and two text
// rows -- title in the tier colour, detail in the secondary colour. When no
// toast is live the widget renders nothing and has empty bounds, so it only
// becomes draggable while showing one; [Achievements] devToast=1 plus the
// RELOAD_CONFIG hotkey puts a toast up on demand for placing it.
// ============================================================================
#include "achievement_widget.h"

#include "../core/asset_manager.h"
#include "../core/color_config.h"
#include "../core/hud_manager.h"
#include "../core/input_manager.h"
#include "../core/ui_config.h"
#include "../core/plugin_constants.h"
#include "../core/plugin_utils.h"
#include "../diagnostics/logger.h"

#include <cstdio>
#include <cstring>

using namespace PluginConstants;

AchievementWidget::AchievementWidget() {
    m_panelKind = PanelKind::Widget;
    m_bContentCard = true;
    setDraggable(true);
    m_strings.reserve(3);
    m_quads.reserve(8);
    setTextureBaseName("achievement_widget");
    // A new card starts with nothing up, as the card did when it held the queue
    ToastFeed::getInstance().reset();
    resetToDefaults();
    rebuildRenderData();
}

bool AchievementWidget::handlesDataType(DataChangeType) const {
    // Content is polled from AchievementManager's queue in update(); no
    // PluginData change type carries an unlock.
    return false;
}

void AchievementWidget::update() {
    const ToastFeed& feed = ToastFeed::getInstance();
    if (!isVisibleAnySurface()) {
        clearDataDirty();
        clearLayoutDirty();
        return;
    }
    // Which toast is up is ToastFeed's (stepped by HudManager before this);
    // a new one, or none, rebuilds the card.
    if (feed.revision() != m_renderedRevision) {
        m_renderedRevision = feed.revision();
        setDataDirty();
    }
    const bool drawable = HudManager::getInstance().areHudsEnabled() || feed.isThroughHideAll();

    // A click on the card opens the menu where it points -- an achievement's
    // page, or the tab a system toast names -- and ends the card: the player
    // has gone where it pointed. Gated like every self-hit-testing HUD on the
    // click being its own (ownsClick: on screen, the topmost under it), and on
    // the cursor: with the menu-only cursor there is no pointer on track.
    if (feed.showing() && drawable && ownsClick()) {
        const InputManager& input = InputManager::getInstance();
        if (input.isCursorEnabled() && input.getLeftButton().isClicked()) {
            CursorPosition cursor = input.getCursorPosition();
            mapCursorToHudSpace(cursor.x, cursor.y);
            if (cursor.isValid && isPointInBounds(cursor.x, cursor.y)) ToastFeed::getInstance().click();
        }
    }

    if (isDataDirty()) {
        rebuildAndRecord();
        clearDataDirty();
        clearLayoutDirty();
    } else if (isLayoutDirty()) {
        rebuildLayout();
        clearLayoutDirty();
    }
}

void AchievementWidget::rebuildLayout() {
    rebuildRenderData();
}

void AchievementWidget::rebuildRenderData() {
    clearStrings();
    m_quads.clear();

    // Nothing live: render nothing, and clear the bounds so an invisible widget
    // never eats a click.
    //
    // UNLESS THE ACHIEVEMENTS TAB IS OPEN (isPreviewing): a card is up for a few
    // seconds a session, which is no time at all to drag it somewhere, so a sample
    // one stands in. Toasts switched OFF stays off -- that switch is the player
    // saying they do not want these, and a preview would argue with it -- except
    // for the Toggles strip's hover sample, which shows what switching them on
    // would bring (BaseHud::isSampled).
    const ToastFeed& feed = ToastFeed::getInstance();
    const bool showing = feed.showing();
    const bool toastsOn = AchievementManager::getInstance().isToastsEnabled();
    if (!showing && !(isPreviewing() && (toastsOn || isSampled()))) {
        setBounds(0.0f, 0.0f, 0.0f, 0.0f);
        return;
    }
    // A real card's shape at its widest tier. Built here, never queued: nothing is
    // earned, nothing is logged, and a click on it opens no page (catalogueIndex
    // stays -1). Built per rebuild, not once: a static would keep the language
    // it was first drawn in (core/i18n.h, "never cache TR() in a static").
    AchievementManager::Toast sampleToast;
    if (!showing) {
        std::snprintf(sampleToast.title, sizeof(sampleToast.title), "%s", TR("Preview"));
        std::snprintf(sampleToast.detail, sizeof(sampleToast.detail), "%s", TR("Achievement toasts appear here"));
        std::snprintf(sampleToast.icon, sizeof(sampleToast.icon), "%s", "trophy");
        sampleToast.tier = Achievements::TIER_COUNT;
    }
    const bool system = feed.source() == ToastFeed::Source::System;
    const SystemMessages::Toast& message = feed.system();
    const AchievementManager::Toast& toast = showing ? feed.achievement() : sampleToast;
    const char* title = system ? message.title : toast.title;
    const char* detail = system ? message.detail : toast.detail;
    const char* icon = system ? message.icon : toast.icon;
    const char* caption = system ? TR("Message") : TR("Achievement");

    const auto dim = getScaledDimensions();
    const unsigned long primary = getColor(ColorSlot::PRIMARY);
    const unsigned long secondary = getColor(ColorSlot::SECONDARY);
    // An achievement wears its tier's metal; a system toast the primary text
    // colour with an accent icon, or the negative colour when it is a warning.
    const bool warning = system && message.severity == SystemMessages::Severity::Warning;
    const unsigned long titleColor = system ? (warning ? getColor(ColorSlot::NEGATIVE) : primary)
                                            : PodiumColors::tierColor(toast.tier, primary);
    const unsigned long iconColor = system ? (warning ? getColor(ColorSlot::NEGATIVE)
                                                     : getColor(ColorSlot::ACCENT))
                                           : titleColor;

    // The marker sprite, if the icon set has it and UI icons are on (as on the
    // Achievements tab); otherwise the icon column is dropped rather than drawing
    // a coloured square in its place.
    const int sprite = (icon[0] && UiConfig::getInstance().getTitleIcons())
        ? AssetManager::getInstance().getIconSpriteIndex(icon) : 0;
    const float rowH = dim.lineHeightNormal;
    const float iconSize = sprite > 0 ? rowH * 1.6f : 0.0f;
    // Aspect-corrected width of the icon column (addIcon draws a square in
    // screen pixels), plus a character of air before the text.
    const float iconColW = sprite > 0
        ? iconSize / UI_ASPECT_RATIO + PluginUtils::calculateMonospaceTextWidth(1, dim.fontSize)
        : 0.0f;

    // Monospace estimate over a proportional font: slightly generous card,
    // never a clipped one.
    const int titleChars = static_cast<int>(std::strlen(title));
    const int detailChars = static_cast<int>(std::strlen(detail));
    const int textChars = titleChars > detailChars ? titleChars : detailChars;

    PanelWant want;
    want.contentW = iconColW + PluginUtils::calculateMonospaceTextWidth(textChars, dim.fontSize);
    want.sectionH = { rowH * 2.0f };
    want.captionW = planTitleWidth(dim, caption);
    PanelPlan& p = planPanel(dim, want);

    // RIGHT-ANCHORED: offsetX is this card's RIGHT edge, so a card as wide as
    // its text grows leftward and stays in the corner it defaults to.
    const float panelX = rightAnchoredPanelLeft(p.width());
    addPlanBackground(p, panelX, 0.0f);
    addPlanTitle(p, caption, primary);

    const float x = p.contentX();
    const float y = p.contentY();
    if (sprite > 0) {
        addIcon(x + (iconSize / UI_ASPECT_RATIO) * 0.5f, y + rowH, sprite, iconColor, iconSize);
        shadowLastIcon();
    }
    const float textX = x + iconColW;
    addString(title, textX, inkCenteredY(y, rowH, dim.fontSize),
              Justify::LEFT, getFont(FontCategory::STRONG), titleColor, dim.fontSize);
    addString(detail, textX, inkCenteredY(y + rowH, rowH, dim.fontSize),
              Justify::LEFT, getFont(FontCategory::NORMAL), secondary, dim.fontSize);

    setBounds(panelX, 0.0f, panelX + p.width(), p.height());
}

void AchievementWidget::resetToDefaults() {
    m_bVisible = true;   // master gate is [Achievements] visible
    m_bShowTitle = false;
    setTextureVariant(0);
    m_fBackgroundOpacity = 0.66f;  // readable over track without a texture
    setScale(1.0f);
    // The right corner, in the widget column above Speed and Gear: the right
    // edge on Speed's (168 + its 10 cells), the bottom a cell above their top
    // row (74) with the card's own two-row, untitled height of 6. Pinned
    // against those widgets' live edges by achievements_test.cpp.
    setPosition(cellsX(178), cellsY(67));
    setDataDirty();
}
