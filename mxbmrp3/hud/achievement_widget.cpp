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
#include "settings_hud.h"
#include "../core/plugin_constants.h"
#include "../core/plugin_utils.h"
#include "../diagnostics/logger.h"

#include <cstdio>
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

void AchievementWidget::showSystem(const std::chrono::steady_clock::time_point& now) {
    m_showing = true;
    m_source = Source::System;
    m_shownAt = now;
    setDataDirty();
}

void AchievementWidget::update() {
    AchievementManager& ach = AchievementManager::getInstance();
    SystemMessages& sys = SystemMessages::getInstance();
    const auto now = std::chrono::steady_clock::now();
    const bool hudsOn = HudManager::getInstance().areHudsEnabled();

    // END THE CARD SHOWING when its time is up, its feed was switched off (a
    // settings load or the toggle -- the queue is cleared behind it, but a taken
    // toast lives here, and its primitives would otherwise stay on screen until
    // the clock ran out, or forever if nothing else dirtied the widget), or, for
    // a system toast, when what it announced was undone (SystemMessages::cancel).
    if (m_showing) {
        bool end;
        if (m_source == Source::Achievement) {
            end = !ach.isToastsEnabled() ||
                  now - m_shownAt >= std::chrono::milliseconds(ach.getToastDurationMs());
        } else {
            end = !sys.isEnabled() || sys.consumeCancel(m_system.key) ||
                  now - m_shownAt >= std::chrono::milliseconds(m_system.durationMs);
        }
        if (end) {
            m_showing = false;
            setDataDirty();
        }
    }
    // The same key again replaces the card in place and restarts its clock: a
    // key mashed five times is one card with the latest text.
    if (m_showing && m_source == Source::System && sys.takeReplacement(m_system.key, m_system)) {
        showSystem(now);
    }

    // Take the next toast only when it can actually be seen: this widget on some
    // surface, and the hide-all-HUDs hotkey not active (unless the toast is that
    // hotkey's own message). Taken while hidden, a toast would run its clock
    // unseen and be gone -- the same reason the clock starts at take time, not
    // queue time (see the manager's header). The Widgets master toggle does not
    // cover this widget: like the spotter's subtitles it is a notification, not
    // a gauge.
    const bool onSurface = isVisibleAnySurface();
    const bool systemDrawable = onSurface && (hudsOn || sys.nextIsThroughHideAll());
    // A system toast takes the card from an achievement; the achievement goes
    // back to the front of its queue and is shown whole afterwards.
    if (m_showing && m_source == Source::Achievement && systemDrawable && sys.hasPending()) {
        ach.requeueFront(m_toast);
        m_showing = false;
    }
    if (!m_showing && systemDrawable && sys.take(m_system)) {
        showSystem(now);
    } else if (!m_showing && onSurface && hudsOn && ach.takeToast(m_toast)) {
        m_showing = true;
        m_source = Source::Achievement;
        m_shownAt = now;
        setDataDirty();
    }
    const bool drawable = onSurface && (hudsOn || isShowingThroughHideAll());

    if (!onSurface) {
        clearDataDirty();
        clearLayoutDirty();
        return;
    }

    // A click on the card opens the menu where it points -- an achievement's
    // page, or the tab a system toast names -- and ends the card: the player
    // has gone where it pointed. Gated on being on screen like every
    // self-hit-testing HUD (isHeldBack), and on the cursor: with the menu-only
    // cursor there is no pointer on track, so no click either.
    if (m_showing && drawable && !isHeldBack()) {
        const InputManager& input = InputManager::getInstance();
        if (input.isCursorEnabled() && input.getLeftButton().isClicked()) {
            CursorPosition cursor = input.getCursorPosition();
            mapCursorToHudSpace(cursor.x, cursor.y);
            if (cursor.isValid && isPointInBounds(cursor.x, cursor.y)) {
                m_showing = false;
                setDataDirty();
                SettingsHud& settings = HudManager::getInstance().getSettingsHud();
                if (m_source == Source::Achievement) {
                    settings.showAchievementsTab(m_toast.catalogueIndex);
                } else if (m_system.tab >= 0) {
                    settings.showTab(m_system.tab);
                }
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

    // Nothing live: render nothing, and clear the bounds so an invisible widget
    // never eats a click.
    //
    // UNLESS THE ACHIEVEMENTS TAB IS OPEN (isPreviewing): a card is up for a few
    // seconds a session, which is no time at all to drag it somewhere, so a sample
    // one stands in. Toasts switched OFF stays off -- that switch is the player
    // saying they do not want these, and a preview would argue with it.
    const bool toastsOn = AchievementManager::getInstance().isToastsEnabled();
    if (!m_showing && !(isPreviewing() && toastsOn)) {
        setBounds(0.0f, 0.0f, 0.0f, 0.0f);
        return;
    }
    // A real card's shape at its widest tier. Built here, never queued: nothing is
    // earned, nothing is logged, and a click on it opens no page (catalogueIndex
    // stays -1).
    static const AchievementManager::Toast kSampleToast = [] {
        AchievementManager::Toast t;
        std::snprintf(t.title, sizeof(t.title), "%s", "Preview");
        std::snprintf(t.detail, sizeof(t.detail), "%s", "Achievement toasts appear here");
        std::snprintf(t.icon, sizeof(t.icon), "%s", "trophy");
        t.tier = Achievements::TIER_COUNT;
        return t;
    }();
    const bool system = m_showing && m_source == Source::System;
    const AchievementManager::Toast& toast = m_showing ? m_toast : kSampleToast;
    const char* title = system ? m_system.title : toast.title;
    const char* detail = system ? m_system.detail : toast.detail;
    const char* icon = system ? m_system.icon : toast.icon;
    const char* caption = system ? "Message" : "Achievement";

    const auto dim = getScaledDimensions();
    const unsigned long primary = getColor(ColorSlot::PRIMARY);
    const unsigned long secondary = getColor(ColorSlot::SECONDARY);
    // An achievement wears its tier's metal; a system toast the primary text
    // colour with an accent icon, or the negative colour when it is a warning.
    const bool warning = system && m_system.severity == SystemMessages::Severity::Warning;
    const unsigned long titleColor = system ? (warning ? getColor(ColorSlot::NEGATIVE) : primary)
                                            : tierColor(toast.tier, primary);
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
    m_fBackgroundOpacity = 0.55f;  // readable over track without a texture
    setScale(1.0f);
    // The right corner, in the widget column above Speed and Gear: the right
    // edge on Speed's (168 + its 10 cells), the bottom a cell above their top
    // row (74) with the card's own two-row, untitled height of 6. Pinned
    // against those widgets' live edges by achievements_test.cpp.
    setPosition(cellsX(178), cellsY(67));
    setDataDirty();
}
