// ============================================================================
// hud/lap_widget.cpp
// Lap widget - displays current lap in minimal format (e.g., "L2/5" or "L2")
// Shows "Lx/y" for lap-only sessions, "Lx" for time-based or time+laps sessions
// ============================================================================
#include "lap_widget.h"

#include <cstdio>

#include "../diagnostics/logger.h"
#include "../diagnostics/timer.h"
#include "../core/plugin_utils.h"
#include "../core/color_config.h"

using namespace PluginConstants;

LapWidget::LapWidget()
    : m_cachedCurrentLap(-1)
    , m_cachedTotalLaps(-1)
    , m_cachedSessionLength(-1)
{
    // One-time setup
    DEBUG_INFO("LapWidget created");
    setDraggable(true);
    m_strings.reserve(2);  // label (optional), lap value

    // Set texture base name for dynamic texture discovery
    setTextureBaseName("lap_widget");

    // Set all configurable defaults
    resetToDefaults();

    rebuildRenderData();
}

bool LapWidget::handlesDataType(DataChangeType dataType) const {
    return dataType == DataChangeType::SessionData ||
           dataType == DataChangeType::Standings ||
           dataType == DataChangeType::SpectateTarget;
}

void LapWidget::update() {
    // OPTIMIZATION: Skip processing when not visible
    if (!isVisible()) {
        clearDataDirty();
        clearLayoutDirty();
        return;
    }

    // Get current lap and total laps
    const PluginData& pluginData = PluginData::getInstance();
    const SessionData& sessionData = pluginData.getSessionData();
    int displayRaceNum = pluginData.getDisplayRaceNum();

    int currentLap = 0;
    int totalLaps = sessionData.sessionNumLaps;
    int sessionLength = sessionData.sessionLength;

    // Get rider's current lap from standings
    if (displayRaceNum > 0) {
        const StandingsData* standing = pluginData.getStanding(displayRaceNum);
        if (standing) {
            currentLap = standing->numLaps + 1;  // numLaps is completed laps, so add 1 for current lap
        }
    }

    // Check if lap data changed (including sessionLength since it affects display format)
    if (currentLap != m_cachedCurrentLap || totalLaps != m_cachedTotalLaps || sessionLength != m_cachedSessionLength) {
        setDataDirty();
    }

    // Check data dirty first (takes precedence)
    if (isDataDirty()) {
        rebuildRenderData();
        m_cachedCurrentLap = currentLap;
        m_cachedTotalLaps = totalLaps;
        m_cachedSessionLength = sessionLength;
        clearDataDirty();
        clearLayoutDirty();
    }
    else if (isLayoutDirty()) {
        rebuildLayout();
        clearLayoutDirty();
    }
}

void LapWidget::rebuildLayout() {
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

    setBounds(startX, startY, startX + backgroundWidth, startY + backgroundHeight);

    // Update background quad position (applies offset internally)
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

    // Lap value (extra large font - spans 2 lines)
    positionString(stringIndex, contentStartX, currentY);
}

void LapWidget::rebuildRenderData() {
    // Clear render data
    clearStrings();
    m_quads.clear();

    auto dim = getScaledDimensions();

    // Get lap data
    const PluginData& pluginData = PluginData::getInstance();
    const SessionData& sessionData = pluginData.getSessionData();
    int displayRaceNum = pluginData.getDisplayRaceNum();

    int currentLap = 0;
    int totalLaps = sessionData.sessionNumLaps;

    // Get rider's current lap from standings
    if (displayRaceNum > 0) {
        const StandingsData* standing = pluginData.getStanding(displayRaceNum);
        if (standing) {
            currentLap = standing->numLaps + 1;  // numLaps is completed laps, so add 1 for current lap
        }
    }

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
    unsigned long textColor = this->getColor(ColorSlot::PRIMARY);

    // Label (optional, controlled by title toggle)
    if (m_bShowTitle) {
        addString("Lap", contentStartX, currentY, Justify::LEFT, this->getFont(FontCategory::TITLE), textColor, dim.fontSize);
        currentY += labelHeight;
    }

    // Determine if we should show total laps
    // Don't show total laps if:
    // - totalLaps is 0 (time-based session)
    // - sessionLength > 0 && totalLaps > 0 (time+laps session)
    int sessionLength = sessionData.sessionLength;
    bool showTotalLaps = (totalLaps > 0) && (sessionLength <= 0);

    // Build lap value string (e.g., "2/5" or "2" or "-")
    char lapValueBuffer[32];
    if (currentLap <= 0) {
        snprintf(lapValueBuffer, sizeof(lapValueBuffer), "%s", Placeholders::GENERIC);
    } else if (showTotalLaps) {
        snprintf(lapValueBuffer, sizeof(lapValueBuffer), "%d/%d", currentLap, totalLaps);
    } else {
        snprintf(lapValueBuffer, sizeof(lapValueBuffer), "%d", currentLap);
    }

    // Add lap value (extra large font - spans 2 lines)
    addString(lapValueBuffer, contentStartX, currentY, Justify::LEFT,
        this->getFont(FontCategory::TITLE), textColor, dim.fontSizeExtraLarge);

    // Set bounds for drag detection
    setBounds(startX, startY, startX + backgroundWidth, startY + backgroundHeight);
}

void LapWidget::resetToDefaults() {
    m_bVisible = true;
    m_bShowTitle = true;
    setTextureVariant(0);  // No texture by default
    m_fBackgroundOpacity = 0.1f;
    m_fScale = 1.0f;
    setPosition(0.099f, 0.0111f);
    setDataDirty();
}
