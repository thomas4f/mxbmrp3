// ============================================================================
// hud/speed_widget.cpp
// Speed widget - displays speedometer (ground speed)
// ============================================================================
#include "speed_widget.h"

#include <cstdio>
#include <cmath>

#include "../diagnostics/logger.h"
#include "../diagnostics/timer.h"
#include "../core/plugin_utils.h"
#include "../core/color_config.h"

using namespace PluginConstants;

SpeedWidget::SpeedWidget()
{
    m_panelKind = PanelKind::Widget;
    m_bContentCard = true;
    // One-time setup
    DEBUG_INFO("SpeedWidget created");
    setDraggable(true);
    m_strings.reserve(3);  // Title (optional) + speed value + units

    // Set texture base name for dynamic texture discovery
    setTextureBaseName("speed_widget");

    // Set all configurable defaults
    resetToDefaults();

    rebuildRenderData();
}

bool SpeedWidget::handlesDataType(DataChangeType dataType) const {
    // Update on telemetry changes (bike data)
    return dataType == DataChangeType::InputTelemetry ||
           dataType == DataChangeType::SpectateTarget;
}

void SpeedWidget::update() {
    // OPTIMIZATION: Skip processing when not visible
    if (!isVisibleAnySurface()) {
        clearDataDirty();
        clearLayoutDirty();
        return;
    }

    // Always rebuild - speed updates at high frequency (telemetry rate)
    // Rebuild is cheap (single snprintf), no need for caching
    rebuildAndRecord();
    clearDataDirty();
    clearLayoutDirty();
}

void SpeedWidget::rebuildLayout() {
    // BOX-MODEL: one source of geometry — rebuild rather than reposition a
    // duplicated copy of the sizing arithmetic (see version_widget).
    rebuildRenderData();
}

void SpeedWidget::rebuildRenderData() {
    // Clear render data
    clearStrings();
    m_quads.clear();

    auto dim = getScaledDimensions();

    // Get bike telemetry data
    const PluginData& pluginData = PluginData::getInstance();
    const BikeTelemetryData& bikeData = pluginData.getBikeTelemetry();

    float startX = 0.0f;
    float startY = 0.0f;

    // BOX-MODEL: the plan owns padding, chrome, the title band and the card;
    // the widget states only its content — the value row plus the optional
    // units row.
    BaseHud::PanelWant want;
    want.contentW = PluginUtils::calculateMonospaceTextWidth(WidgetDimensions::SPEED_WIDTH, dim.fontSize);
    float sectionH = dim.lineHeightLarge;  // Speed value always shown
    if (m_enabledRows & ROW_UNITS) sectionH += dim.lineHeightNormal;
    want.sectionH = { sectionH };
    want.captionW = planTitleWidth(dim, "Speed");
    PanelPlan& p = planPanel(dim, want);

    const float backgroundWidth = p.width();
    const float backgroundHeight = p.height();

    // Use full opacity for text
    unsigned long textColor = this->getColor(ColorSlot::PRIMARY);

    addPlanBackground(p, startX, startY);
    addPlanTitle(p, "Speed", this->getFont(FontCategory::TITLE), textColor);
    float currentY = p.contentY();

    // Build speed value string
    char speedValueBuffer[8];

    if (!bikeData.isValid) {
        snprintf(speedValueBuffer, sizeof(speedValueBuffer), "%s", Placeholders::GENERIC);
    } else {
        int speed;
        if (m_speedUnit == SpeedUnit::KMH) {
            speed = static_cast<int>(bikeData.speedometer * UnitConversion::MS_TO_KMH + 0.5f);
        } else {
            speed = static_cast<int>(bikeData.speedometer * UnitConversion::MS_TO_MPH + 0.5f);
        }
        snprintf(speedValueBuffer, sizeof(speedValueBuffer), "%d", speed);
    }

    // Value and units are centered on the CARD (PanelPlan::sectionBoxCenterX --
    // the panel's centre is the same place only while the [content] terms are
    // left/right symmetric); the caption sits at the plan's own column.
    float centerX = p.sectionBoxCenterX();

    // Speed value (extra large font) - always shown, centered
    addString(speedValueBuffer, centerX, currentY, Justify::CENTER,
        this->getFont(FontCategory::TITLE), textColor, dim.fontSizeExtraLarge);
    currentY += dim.lineHeightLarge;

    // Add units label (normal font) - centered
    if (m_enabledRows & ROW_UNITS) {
        const char* unitsLabel = (m_speedUnit == SpeedUnit::KMH) ? "km/h" : "mph";
        addString(unitsLabel, centerX, currentY, Justify::CENTER,
            this->getFont(FontCategory::TITLE), textColor, dim.fontSize);
    }

    // Set bounds for drag detection
    setBounds(startX, startY, startX + backgroundWidth, startY + backgroundHeight);
}

void SpeedWidget::resetToDefaults() {
    m_bVisible = true;
    m_bShowTitle = false;  // Title disabled by default
    setTextureVariant(0);  // No texture by default
    m_fBackgroundOpacity = 0.0f;  // Transparent by default
    m_fScale = 1.0f;
    m_enabledRows = ROW_DEFAULT;  // Reset row visibility
    // Note: speedUnit is NOT reset here - it's a global preference, not per-profile
    setPosition(cellsX(168), cellsY(74));
    setDataDirty();
}
