// ============================================================================
// hud/spotter_widget.cpp
// See the header. Render shape: one centered text row on the standard widget
// card, sized to the cue text. When no cue is live the widget renders nothing
// and has empty bounds (it only becomes draggable while showing a cue —
// enable the spotter and trigger one to place it).
// ============================================================================
#include "spotter_widget.h"

#include "../core/color_config.h"
#include "../core/plugin_constants.h"
#include "../core/plugin_utils.h"
#include "../core/spotter_manager.h"
#include "../diagnostics/logger.h"

using namespace PluginConstants;

SpotterWidget::SpotterWidget() {
    m_panelKind = PanelKind::Widget;
    m_bContentCard = true;
    DEBUG_INFO("SpotterWidget created");
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
    if (m_text.empty() ||
        !SpotterManager::getInstance().isSubtitlesEnabled()) {
        setBounds(0.0f, 0.0f, 0.0f, 0.0f);
        return;
    }

    const auto dim = getScaledDimensions();
    const unsigned long textColor = getColor(ColorSlot::PRIMARY);

    BaseHud::PanelWant want;
    // Monospace estimate over a proportional font: slightly generous card,
    // never a clipped one.
    want.contentW = PluginUtils::calculateMonospaceTextWidth(
        static_cast<int>(m_text.size()), dim.fontSize);
    want.sectionH = { dim.lineHeightNormal };
    want.captionW = planTitleWidth(dim, "Spotter");
    PanelPlan& p = planPanel(dim, want);

    addPlanBackground(p, 0.0f, 0.0f);
    addPlanTitle(p, "Spotter", getFont(FontCategory::TITLE), textColor);

    addString(m_text.c_str(), p.contentX(),
              inkCenteredY(p.contentY(), dim.lineHeightNormal, dim.fontSize),
              Justify::LEFT, getFont(FontCategory::NORMAL), textColor,
              dim.fontSize);

    setBounds(0.0f, 0.0f, p.width(), p.height());
}

void SpotterWidget::resetToDefaults() {
    m_bVisible = true;   // master gate is [Spotter] subtitles + enabled
    m_bShowTitle = false;
    setTextureVariant(0);
    m_fBackgroundOpacity = 0.55f;  // readable over track without a texture
    m_fScale = 1.0f;
    // Lower-center: out of the racing line of sight, near where game chat
    // and subtitle conventions put text.
    setPosition(cellsX(20), cellsY(24));
    setDataDirty();
}
