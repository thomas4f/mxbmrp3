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
#include "settings_hud.h"
#include "../core/plugin_constants.h"
#include "../core/plugin_utils.h"
#include "../diagnostics/logger.h"

#include <cstring>

using namespace PluginConstants;

namespace {
// The metal each tier is drawn in. Platinum is a cool near-white so it reads as
// a step above gold rather than as the plain primary text colour.
unsigned long tierColor(int tier, unsigned long fallback) {
    switch (tier) {
        case 1: return PodiumColors::BRONZE;
        case 2: return PodiumColors::SILVER;
        case 3: return PodiumColors::GOLD;
        case 4: return PluginUtils::makeColor(229, 228, 226);
        default: return fallback;
    }
}
}  // namespace

AchievementWidget::AchievementWidget() {
    m_panelKind = PanelKind::Widget;
    m_bContentCard = true;
    DEBUG_INFO("AchievementWidget created");
    setDraggable(true);
    m_strings.reserve(3);
    m_quads.reserve(8);
    setTextureBaseName("achievement_widget");
    resetToDefaults();
    rebuildRenderData();
}

bool AchievementWidget::handlesDataType(DataChangeType) const {
    // Content is polled from AchievementManager's queue in update(); no
    // PluginData change type carries an unlock.
    return false;
}

void AchievementWidget::update() {
    AchievementManager& ach = AchievementManager::getInstance();
    const auto now = std::chrono::steady_clock::now();

    // The master going off (a settings load or the toggle) ends a toast that
    // is already up: the queue is cleared behind it, but a taken toast lives
    // here, and its primitives would otherwise stay on screen until the clock
    // ran out -- or forever, if nothing else dirtied the widget.
    if (m_showing && (!ach.isToastsEnabled() ||
                      now - m_shownAt >= std::chrono::milliseconds(ach.getToastDurationMs()))) {
        m_showing = false;
        setDataDirty();
    }
    // Take the next toast only when it can actually be seen: this widget on some
    // surface, and the hide-all-HUDs hotkey not active. Taken while hidden, a
    // toast would run its clock unseen and be gone -- the same reason the clock
    // starts at take time, not queue time (see the manager's header). The
    // Widgets master toggle does not cover this widget: like the spotter's
    // subtitles it is a notification, not a gauge.
    const bool drawable = isVisibleAnySurface() && HudManager::getInstance().areHudsEnabled();
    if (!m_showing && drawable && ach.takeToast(m_toast)) {
        m_showing = true;
        m_shownAt = now;
        setDataDirty();
    }

    if (!isVisibleAnySurface()) {
        clearDataDirty();
        clearLayoutDirty();
        return;
    }

    // A click on the card opens the menu on the Achievements tab, and ends the
    // card: the player has gone where it pointed. Gated on being on screen
    // like every self-hit-testing HUD (isHeldBack), and on the cursor: with
    // the menu-only cursor there is no pointer on track, so no click either.
    if (m_showing && drawable && !isHeldBack()) {
        const InputManager& input = InputManager::getInstance();
        if (input.isCursorEnabled() && input.getLeftButton().isClicked()) {
            CursorPosition cursor = input.getCursorPosition();
            mapCursorToHudSpace(cursor.x, cursor.y);
            if (cursor.isValid && isPointInBounds(cursor.x, cursor.y)) {
                m_showing = false;
                setDataDirty();
                HudManager::getInstance().getSettingsHud().showAchievementsTab(m_toast.catalogueIndex);
            }
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

    // Nothing live (or toasts switched off, which empties the queue): render
    // nothing, and clear the bounds so an invisible widget never eats a click.
    if (!m_showing || !AchievementManager::getInstance().isToastsEnabled()) {
        setBounds(0.0f, 0.0f, 0.0f, 0.0f);
        return;
    }

    const auto dim = getScaledDimensions();
    const unsigned long primary = getColor(ColorSlot::PRIMARY);
    const unsigned long secondary = getColor(ColorSlot::SECONDARY);
    const unsigned long accent = tierColor(m_toast.tier, primary);

    // The marker sprite, if the icon set has it; a missing asset just drops the
    // icon column rather than drawing a coloured square in its place.
    const int sprite = m_toast.icon[0]
        ? AssetManager::getInstance().getIconSpriteIndex(m_toast.icon) : 0;
    const float rowH = dim.lineHeightNormal;
    const float iconSize = sprite > 0 ? rowH * 1.6f : 0.0f;
    // Aspect-corrected width of the icon column (addIcon draws a square in
    // screen pixels), plus a character of air before the text.
    const float iconColW = sprite > 0
        ? iconSize / UI_ASPECT_RATIO + PluginUtils::calculateMonospaceTextWidth(1, dim.fontSize)
        : 0.0f;

    // Monospace estimate over a proportional font: slightly generous card,
    // never a clipped one.
    const int titleChars = static_cast<int>(std::strlen(m_toast.title));
    const int detailChars = static_cast<int>(std::strlen(m_toast.detail));
    const int textChars = titleChars > detailChars ? titleChars : detailChars;

    BaseHud::PanelWant want;
    want.contentW = iconColW + PluginUtils::calculateMonospaceTextWidth(textChars, dim.fontSize);
    want.sectionH = { rowH * 2.0f };
    want.captionW = planTitleWidth(dim, "Achievement");
    PanelPlan& p = planPanel(dim, want);

    // RIGHT-ANCHORED: offsetX is this card's RIGHT edge, so a card as wide as
    // its text grows leftward and stays in the corner it defaults to.
    const float panelX = rightAnchoredPanelLeft(p.width());
    addPlanBackground(p, panelX, 0.0f);
    addPlanTitle(p, "Achievement", getFont(FontCategory::TITLE), primary);

    const float x = p.contentX();
    const float y = p.contentY();
    if (sprite > 0) {
        addIcon(x + (iconSize / UI_ASPECT_RATIO) * 0.5f, y + rowH, sprite, accent, iconSize);
    }
    const float textX = x + iconColW;
    addString(m_toast.title, textX, inkCenteredY(y, rowH, dim.fontSize),
              Justify::LEFT, getFont(FontCategory::STRONG), accent, dim.fontSize);
    addString(m_toast.detail, textX, inkCenteredY(y + rowH, rowH, dim.fontSize),
              Justify::LEFT, getFont(FontCategory::NORMAL), secondary, dim.fontSize);

    setBounds(panelX, 0.0f, panelX + p.width(), p.height());
}

void AchievementWidget::resetToDefaults() {
    m_bVisible = true;   // master gate is [Achievements] visible
    m_bShowTitle = false;
    setTextureVariant(0);
    m_fBackgroundOpacity = 0.55f;  // readable over track without a texture
    m_fScale = 1.0f;
    // The right corner, in the widget column above Speed and Gear: the right
    // edge on Speed's (168 + its 10 cells), the bottom a cell above their top
    // row (74) with the card's own two-row, untitled height of 6. Pinned
    // against those widgets' live edges by achievements_test.cpp.
    setPosition(cellsX(178), cellsY(67));
    setDataDirty();
}
