// ============================================================================
// hud/position_widget.cpp
// Position widget - displays rider position in minimal format (e.g., "1/24")
// ============================================================================
#include "position_widget.h"
#include "../diagnostics/logger.h"
#include "../diagnostics/timer.h"
#include "../core/plugin_utils.h"
#include "../core/color_config.h"
#include <cstdio>
#include <algorithm>

using namespace PluginConstants;

PositionWidget::PositionWidget()
    : m_cachedPosition(-1)
    , m_cachedTotalEntries(-1)
{
    // One-time setup
    DEBUG_INFO("PositionWidget created");
    setDraggable(true);
    m_strings.reserve(2);  // label (optional), position value

    // Set texture base name for dynamic texture discovery
    setTextureBaseName("position_widget");

    // Set all configurable defaults
    resetToDefaults();

    rebuildRenderData();
}

bool PositionWidget::handlesDataType(DataChangeType dataType) const {
    return dataType == DataChangeType::Standings ||
           dataType == DataChangeType::SpectateTarget;
}

void PositionWidget::update() {
    // OPTIMIZATION: Skip processing when not visible
    if (!isVisible()) {
        clearDataDirty();
        clearLayoutDirty();
        return;
    }

    // Check if position or total entries changed
    int currentPosition = calculatePlayerPosition();
    const PluginData& pluginData = PluginData::getInstance();
    int totalEntries = static_cast<int>(pluginData.getClassificationOrder().size());

    if (currentPosition != m_cachedPosition || totalEntries != m_cachedTotalEntries) {
        setDataDirty();
    }

    // Check data dirty first (takes precedence)
    if (isDataDirty()) {
        rebuildRenderData();
        m_cachedPosition = currentPosition;
        m_cachedTotalEntries = totalEntries;
        clearDataDirty();
        clearLayoutDirty();
    }
    else if (isLayoutDirty()) {
        rebuildLayout();
        clearLayoutDirty();
    }
}

int PositionWidget::calculatePlayerPosition() const {
    const PluginData& pluginData = PluginData::getInstance();
    int displayRaceNum = pluginData.getDisplayRaceNum();

    if (displayRaceNum <= 0) {
        return -1;  // No rider to display
    }

    // Use centralized position cache (O(1) lookup instead of O(n) linear search)
    return pluginData.getPositionForRaceNum(displayRaceNum);
}

void PositionWidget::rebuildLayout() {
    // Fast path - only update positions (not colors/opacity)
    auto dim = getScaledDimensions();

    float startX = 0.0f;
    float startY = 0.0f;

    // Calculate dimensions using base helper
    float backgroundWidth = calculateBackgroundWidth(WidgetDimensions::STANDARD_WIDTH);

    // Height calculation is widget-specific due to lineHeightLarge value display
    float labelHeight = m_bShowTitle ? dim.lineHeightNormal : 0.0f;
    float contentHeight = labelHeight + dim.lineHeightLarge;  // Label (optional, 1 line) + Value (2 lines)
    float backgroundHeight = dim.paddingV + contentHeight + dim.paddingV;

    // Set bounds for drag detection
    setBounds(startX, startY, startX + backgroundWidth, startY + backgroundHeight);

    // Update background quad position
    updateBackgroundQuadPosition(startX, startY, backgroundWidth, backgroundHeight);

    float contentStartX = startX + dim.paddingH;
    float contentStartY = startY + dim.paddingV;
    float currentY = contentStartY;

    // Position strings if they exist
    int stringIndex = 0;

    // Label (optional, controlled by title toggle)
    if (m_bShowTitle && positionString(stringIndex, contentStartX, currentY)) {
        stringIndex++;
        currentY += labelHeight;
    }

    // Position value (extra large font - spans 2 lines)
    positionString(stringIndex, contentStartX, currentY);
}

void PositionWidget::rebuildRenderData() {
    // Clear render data
    clearStrings();
    m_quads.clear();

    auto dim = getScaledDimensions();

    // Get position data
    int position = calculatePlayerPosition();
    const PluginData& pluginData = PluginData::getInstance();
    int totalEntries = static_cast<int>(pluginData.getClassificationOrder().size());

    float startX = 0.0f;
    float startY = 0.0f;

    // Calculate dimensions using base helper
    float backgroundWidth = calculateBackgroundWidth(WidgetDimensions::STANDARD_WIDTH);

    // Height calculation is widget-specific due to lineHeightLarge value display
    float labelHeight = m_bShowTitle ? dim.lineHeightNormal : 0.0f;
    float contentHeight = labelHeight + dim.lineHeightLarge;  // Label (optional, 1 line) + Value (2 lines)
    float backgroundHeight = dim.paddingV + contentHeight + dim.paddingV;

    // Add background quad
    addBackgroundQuad(startX, startY, backgroundWidth, backgroundHeight);

    float contentStartX = startX + dim.paddingH;
    float contentStartY = startY + dim.paddingV;
    float currentY = contentStartY;

    // Use full opacity for text
    unsigned long textColor = ColorConfig::getInstance().getPrimary();

    // Label (optional, controlled by title toggle)
    if (m_bShowTitle) {
        addString("Position", contentStartX, currentY, Justify::LEFT, Fonts::getTitle(), textColor, dim.fontSize);
        currentY += labelHeight;
    }

    // Build position value string (e.g., "1/24" or "-")
    char positionValueBuffer[32];
    if (position <= 0 || totalEntries <= 0) {
        snprintf(positionValueBuffer, sizeof(positionValueBuffer), "%s", Placeholders::GENERIC);
    } else {
        snprintf(positionValueBuffer, sizeof(positionValueBuffer), "%d/%d", position, totalEntries);
    }

    // Add position value (extra large font - spans 2 lines)
    addString(positionValueBuffer, contentStartX, currentY, Justify::LEFT,
        Fonts::getTitle(), textColor, dim.fontSizeExtraLarge);

    // Set bounds for drag detection
    setBounds(startX, startY, startX + backgroundWidth, startY + backgroundHeight);
}

void PositionWidget::resetToDefaults() {
    m_bVisible = true;
    m_bShowTitle = true;
    setTextureVariant(0);  // No texture by default
    m_fBackgroundOpacity = 0.1f;
    m_fScale = 1.0f;
    setPosition(0.0055f, 0.0111f);
    setDataDirty();
}
