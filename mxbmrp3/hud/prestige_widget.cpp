// ============================================================================
// hud/prestige_widget.cpp
// The prestige badge. See prestige_widget.h for what it is and why it is locked.
// ============================================================================
#include "prestige_widget.h"

#include "../core/plugin_utils.h"
#include "../core/settings_manager.h"
#include "../core/stats_manager.h"
#include "../diagnostics/logger.h"

using namespace PluginConstants;

namespace {
// The badge's height as a multiple of the large font, so the Scale slider is
// the only size control -- the same relationship every other widget's content
// has to its text.
constexpr float BADGE_LINES = 3.0f;
}  // namespace

PrestigeWidget::PrestigeWidget() {
    m_panelKind = PanelKind::Widget;
    // No caption: a badge is not a readout, so there is nothing to label.
    disableTitle();
    // The artwork IS this widget -- see BaseHud::m_textureRequired. No content
    // card either: there is no block under a title for a theme to frame, only
    // the picture, and a card behind it would put a slab round the badge.
    m_textureRequired = true;
    DEBUG_INFO("PrestigeWidget created");
    setDraggable(true);
    m_quads.reserve(1);   // the badge, drawn as the panel background
    m_strings.reserve(0);

    setTextureBaseName("prestige_widget");

    resetToDefaults();
    rebuildRenderData();
}

bool PrestigeWidget::isUnlocked() {
    // Developer mode is a second key on the same lock, not a second lock: the
    // badge has to be layout-testable and its art checkable without a hundred
    // Platinums behind it. The same clause is in
    // AchievementManager::isPrestigeAvailable, which gates the button.
    return StatsManager::getInstance().getPrestige() > 0 ||
           SettingsManager::getInstance().isDeveloperMode();
}

bool PrestigeWidget::handlesDataType(DataChangeType /*dataType*/) const {
    // Nothing in the session changes a badge. It is rebuilt when a setting
    // moves (the dirty flag) and when the unlock state changes (see update()).
    return false;
}

void PrestigeWidget::update() {
    if (!isVisibleAnySurface()) {
        clearDataDirty();
        clearLayoutDirty();
        return;
    }
    if (isLayoutDirty()) {
        rebuildLayout();
        clearLayoutDirty();
    }
    // THE UNLOCK IS POLLED, not pushed: prestige() lives in StatsManager, which
    // has no business reaching into a HUD. A bool compare per frame is the whole
    // cost, and it must be compared against what was BUILT rather than against
    // m_quads being empty -- a locked badge builds nothing, so an "empty means
    // stale" test would rebuild it on every frame of every session for every
    // player who has not earned one.
    const bool unlocked = isUnlocked();
    if (isDataDirty() || unlocked != m_builtUnlocked) {
        rebuildAndRecord();
        clearDataDirty();
    }
}

void PrestigeWidget::rebuildLayout() {
    // BOX-MODEL: one source of geometry (see gear_widget).
    rebuildRenderData();
}

void PrestigeWidget::rebuildRenderData() {
    clearStrings();
    m_quads.clear();

    // Locked: draw nothing and clear the bounds, so there is no invisible box
    // to drag or click where a badge has not been earned.
    m_builtUnlocked = isUnlocked();
    if (!m_builtUnlocked) {
        setBounds(0.0f, 0.0f, 0.0f, 0.0f);
        return;
    }

    const ScaledDimensions dim = getScaledDimensions();
    const float badge = dim.fontSizeLarge * BADGE_LINES;

    BaseHud::PanelWant want;
    // Square on screen: the height is the badge, so the width is that height
    // divided by the aspect ratio (the same conversion the settings tab's tier
    // tile and the settings button's box make). The artwork is square, and
    // addBackgroundQuad stretches it to the box -- so the box has to be square
    // or the badge is drawn oval.
    want.contentW = badge / UI_ASPECT_RATIO;
    want.sectionH = { badge };
    PanelPlan& p = planPanel(dim, want);

    // The whole widget: the selected prestige_widget_N.tga, stretched to the
    // panel box. Nothing is drawn on top of it -- which numbered badge is worn
    // is the entire choice this widget offers.
    addPlanBackground(p, 0.0f, 0.0f);

    setBounds(0.0f, 0.0f, p.width(), p.height());
}

void PrestigeWidget::resetToDefaults() {
    // ON by default, because the default only ever applies to someone who has
    // just earned it: a locked badge renders nothing whatever this says, and
    // the reward for the trade appearing on screen is the reward.
    m_bVisible = true;
    m_bShowTitle = false;
    // The artwork is mandatory here, so 0 ("Off") is not a state this widget
    // has -- setTextureVariant snaps it up to the first available.
    setTextureVariant(1);
    m_fBackgroundOpacity = 1.0f;
    m_fScale = 1.0f;
    setPosition(0.5f, 0.05f);
    setDataDirty();
}
