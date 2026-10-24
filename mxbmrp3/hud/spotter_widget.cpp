// ============================================================================
// hud/spotter_widget.cpp
// See the header. Render shape: one centered text row on the standard widget
// card, sized to the cue text. When no cue is live the widget renders nothing
// and has empty bounds (it only becomes draggable while showing a cue —
// enable the spotter and trigger one to place it).
// ============================================================================
#include "spotter_widget.h"
#include "default_slots.h"

#include "../core/color_config.h"
#include "../core/plugin_constants.h"
#include "../core/plugin_utils.h"
#include "../core/spotter_manager.h"
#include "../diagnostics/logger.h"

using namespace PluginConstants;

SpotterWidget::SpotterWidget() {
    m_panelKind = PanelKind::Widget;
    m_bContentCard = true;
    setDraggable(true);
    m_strings.reserve(1);
    setTextureBaseName("spotter_widget");
    resetToDefaults();
    rebuildRenderData();
}

bool SpotterWidget::handlesDataType(DataChangeType) const {
    // Content is polled from SpotterManager's revision counter in update();
    // no PluginData change type carries spotter cues.
    return false;
}

void SpotterWidget::update() {
    SpotterManager& spotter = SpotterManager::getInstance();

    // One atomic load per frame while nothing changes.
    const uint32_t rev = spotter.getCueLogRevision();
    if (rev != m_lastRevision) {
        m_lastRevision = rev;
        m_text = spotter.getLatestCueText();
        m_shownAt = std::chrono::steady_clock::now();
        setDataDirty();
    } else if (!m_text.empty() &&
               std::chrono::steady_clock::now() - m_shownAt >= kDisplayTime) {
        m_text.clear();
        setDataDirty();
    }

    if (!isVisibleAnySurface()) {
        clearDataDirty();
        clearLayoutDirty();
        return;
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

void SpotterWidget::rebuildLayout() {
    rebuildRenderData();
}

void SpotterWidget::rebuildRenderData() {
    clearStrings();
    m_quads.clear();

    // Subtitles off, or nothing recent to show: render nothing. Bounds are
    // cleared too, so an invisible widget never eats drag clicks.
    //
    // UNLESS THE SPOTTER TAB IS OPEN (isPreviewing), in which case a sample cue
    // stands in so the widget can be dragged into place -- most of a session it
    // has nothing to say, which is exactly when a player wants to position it.
    // Subtitles being OFF is still off: that switch says the player does not want
    // this widget's content at all, and a preview would argue with it -- except
    // for the Toggles strip's hover sample, which shows what the Spotter does
    // (BaseHud::isSampled).
    const bool subtitlesOn = SpotterManager::getInstance().isSubtitlesEnabled() || isSampled();
    if (!subtitlesOn || (m_text.empty() && !isPreviewing())) {
        setBounds(0.0f, 0.0f, 0.0f, 0.0f);
        return;
    }
    const char* text = m_text.empty() ? kSampleCue : m_text.c_str();
    const size_t textLen = m_text.empty() ? sizeof(kSampleCue) - 1 : m_text.size();

    const auto dim = getScaledDimensions();
    const unsigned long textColor = getColor(ColorSlot::PRIMARY);

    PanelWant want;
    // Monospace estimate over a proportional font: slightly generous card,
    // never a clipped one.
    want.contentW = PluginUtils::calculateMonospaceTextWidth(
        static_cast<int>(textLen), dim.fontSize);
    want.sectionH = { dim.lineHeightNormal };
    want.captionW = planTitleWidth(dim, "Spotter");
    PanelPlan& p = planPanel(dim, want);

    // CENTRE-ANCHORED: offsetX is this card's CENTRE, so a card as wide as its
    // text grows both ways and stays centred on the spot it defaults to. It
    // stored the LEFT edge until settings v12; the migration adds half the
    // preview card in (previewCardWidth).
    const float panelX = centerAnchoredPanelLeft(p.width());
    addPlanBackground(p, panelX, 0.0f);
    addPlanTitle(p, "Spotter", textColor);

    addString(text, p.contentX(),
              inkCenteredY(p.contentY(), dim.lineHeightNormal, dim.fontSize),
              Justify::LEFT, getFont(FontCategory::NORMAL), textColor,
              dim.fontSize);

    setBounds(panelX, 0.0f, panelX + p.width(), p.height());
}

// The preview card's width without a theme; see the declaration.
float SpotterWidget::previewCardWidth(float scale) {
    const LayoutMetrics& L = layoutDefaults();
    // The unthemed box's horizontal air, in x-cells: the panel's padding plus
    // the content card's margin and padding (resolvePanelSpec's fallbacks).
    const double airCells = L.boxPanelPadding.l + L.boxPanelPadding.r
                          + L.boxContentMargin.l + L.boxContentMargin.r
                          + L.boxContentPadding.l + L.boxContentPadding.r;
    return PluginUtils::calculateMonospaceTextWidth(
               static_cast<int>(sizeof(kSampleCue) - 1), L.fontSizeNormal * scale)
         + static_cast<float>(airCells) * L.cellW * scale;
}

void SpotterWidget::resetToDefaults() {
    m_bVisible = true;   // master gate is [Spotter] subtitles + enabled
    m_bShowTitle = false;
    setTextureVariant(0);
    m_fBackgroundOpacity = 0.66f;  // readable over track without a texture
    setScale(1.0f);
    // Low centre, where subtitles go: out of the racing line of sight, under
    // the widget strip (DefaultSlots::LOW_CENTRE_TOP_CELL).
    setPosition(CENTER_ANCHOR_X, DefaultSlots::lowCentreY());
    setDataDirty();
}
