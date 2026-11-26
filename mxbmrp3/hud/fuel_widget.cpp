// ============================================================================
// hud/fuel_widget.cpp
// Fuel calculator widget - displays fuel level, avg consumption, and estimated laps
// ============================================================================
#include "fuel_widget.h"
#include "../core/i18n.h"

#include "../core/fuel_estimate.h"

#include <algorithm>
#include <cstdio>
#include <cmath>

#include "../diagnostics/logger.h"
#include "../core/plugin_utils.h"
#include "../core/color_config.h"
#include "../core/widget_constants.h"   // SMALL_WIDGET_WIDTH

using namespace PluginConstants;

namespace {
    // Widget dimensions
    // The shared small-widget width, not a local 8: this widget tiles with the
    // gauges beside it, and a private copy is how that stops being true.
    constexpr int FUEL_WIDGET_WIDTH = WidgetDimensions::SMALL_WIDGET_WIDTH;
    constexpr float FUEL_WIDGET_Y = 0.2776f;  // Base Y position for fuel widget

    // Format a value (+ optional unit) into the compact widget. Once the value
    // reaches double digits the extra character would push a right-justified
    // value left into the row label, so drop the decimal at that point.
    void formatFuelValue(char* buf, size_t bufSize, float value, const char* unit) {
        const char* fmt = (value >= 10.0f) ? "%.0f%s" : "%.1f%s";
        snprintf(buf, bufSize, fmt, value, unit);
    }
}

FuelWidget::FuelWidget() {
    m_panelKind = PanelKind::Widget;
    m_bContentCard = true;
    // One-time setup
    setDraggable(true);
    m_strings.reserve(9);  // title + 4 labels + 4 values

    // Set texture base name for dynamic texture discovery
    setTextureBaseName("fuel_widget");

    // Set all configurable defaults
    resetToDefaults();

    rebuildRenderData();
}

bool FuelWidget::handlesDataType(DataChangeType dataType) const {
    // Update on telemetry changes (fuel data) and lap log changes (lap completion)
    return dataType == DataChangeType::InputTelemetry ||
           dataType == DataChangeType::LapLog ||
           dataType == DataChangeType::SpectateTarget ||
           dataType == DataChangeType::SessionData;
}

void FuelWidget::update() {
    // The fuel history is PluginData's (FuelEstimate::Tracker), kept whether
    // or not this is shown; what is drawn changes only with the data above.
    // Hidden, a change waits: shown on either surface, it draws then
    if (!isVisibleAnySurface()) return;
    if (isDataDirty() || isLayoutDirty()) rebuildAndRecord();
    clearDataDirty();
    clearLayoutDirty();
}

int FuelWidget::getEnabledRowCount() const {
    int count = 0;
    if (m_enabledRows & ROW_FUEL) count++;
    if (m_enabledRows & ROW_USED) count++;
    if (m_enabledRows & ROW_AVG) count++;
    if (m_enabledRows & ROW_EST) count++;
    return count;
}

void FuelWidget::rebuildLayout() {
    // BOX-MODEL: one source of geometry — rebuild rather than reposition a
    // duplicated copy of the sizing arithmetic.
    rebuildRenderData();
}

void FuelWidget::rebuildRenderData() {
    clearStrings();
    m_quads.clear();

    auto dim = getScaledDimensions();

    // Get telemetry data
    const PluginData& pluginData = PluginData::getInstance();
    const BikeTelemetryData& bikeData = pluginData.getBikeTelemetry();

    // Fuel data is only available when player is on track (not when spectating/replay)
    bool hasFuelData = (pluginData.getDrawState() == ViewState::ON_TRACK);

    float startX = 0.0f;
    float startY = 0.0f;

    // BOX-MODEL: the plan owns padding, chrome, the title band and the card;
    // the widget states only its content — one line per enabled row.
    int rowCount = getEnabledRowCount();
    PanelWant want;
    want.contentW = PluginUtils::calculateMonospaceTextWidth(FUEL_WIDGET_WIDTH, dim.fontSize);
    want.sectionH = { dim.lineHeightNormal * rowCount };
    want.captionW = planTitleWidth(dim, TR("Fuel"));
    PanelPlan& p = planPanel(dim, want);

    const float backgroundWidth = p.width();
    const float backgroundHeight = p.height();

    addPlanBackground(p, startX, startY);
    addPlanTitle(p, TR("Fuel"),
        this->getColor(ColorSlot::PRIMARY));

    float contentStartX = p.contentX();
    // The content box's own right edge, not a mirror of the LEFT inset -- that is
    // the same place only on a symmetric border. See PanelPlan::contentRight.
    float rightX = p.contentRight();
    float currentY = p.contentY();

    unsigned long labelColor = this->getColor(ColorSlot::TERTIARY);
    unsigned long valueColor = this->getColor(ColorSlot::SECONDARY);
    unsigned long mutedColor = this->getColor(ColorSlot::MUTED);

    // Prepare display values and their colors (muted for placeholders)
    char fuelValueBuffer[16];
    char usedValueBuffer[16];
    char avgValueBuffer[16];
    char lapsValueBuffer[16];
    unsigned long fuelColor = valueColor;
    unsigned long usedColor = valueColor;
    unsigned long avgColor = valueColor;
    unsigned long estColor = this->getColor(ColorSlot::PRIMARY);

    if (!hasFuelData) {
        // Show N/A when spectating/replay (fuel data structurally unavailable)
        snprintf(fuelValueBuffer, sizeof(fuelValueBuffer), "%s", Placeholders::NOT_AVAILABLE);
        snprintf(usedValueBuffer, sizeof(usedValueBuffer), "%s", Placeholders::NOT_AVAILABLE);
        snprintf(avgValueBuffer, sizeof(avgValueBuffer), "%s", Placeholders::NOT_AVAILABLE);
        snprintf(lapsValueBuffer, sizeof(lapsValueBuffer), "%s", Placeholders::NOT_AVAILABLE);
        fuelColor = usedColor = avgColor = estColor = mutedColor;
    } else if (!bikeData.isValid) {
        // Show placeholders when telemetry temporarily not available
        snprintf(fuelValueBuffer, sizeof(fuelValueBuffer), "%s", Placeholders::GENERIC);
        snprintf(usedValueBuffer, sizeof(usedValueBuffer), "%s", Placeholders::GENERIC);
        snprintf(avgValueBuffer, sizeof(avgValueBuffer), "%s", Placeholders::GENERIC);
        snprintf(lapsValueBuffer, sizeof(lapsValueBuffer), "%s", Placeholders::GENERIC);
        fuelColor = usedColor = avgColor = estColor = mutedColor;
    } else {
        // Determine unit label and conversion factor
        const char* unitLabel = UnitConversion::fuelLabel(m_fuelUnit == FuelUnit::GALLONS);
        float unitConversion = (m_fuelUnit == FuelUnit::GALLONS) ? UnitConversion::LITERS_TO_GALLONS : 1.0f;

        // Current fuel
        float displayFuel = bikeData.fuel * unitConversion;
        formatFuelValue(fuelValueBuffer, sizeof(fuelValueBuffer), displayFuel, unitLabel);

        // Total fuel used this run
        const FuelEstimate::Tracker& tracker = PluginData::getInstance().getFuel();
        if (tracker.hasFuelUsed()) {
            float fuelUsed = tracker.fuelUsed(bikeData.fuel) * unitConversion;
            formatFuelValue(usedValueBuffer, sizeof(usedValueBuffer), fuelUsed, unitLabel);
        } else {
            snprintf(usedValueBuffer, sizeof(usedValueBuffer), "%s", Placeholders::GENERIC);
            usedColor = mutedColor;
        }

        // Calculate average fuel per lap
        // Shared with the spotter's fuel warning, so the number spoken and the
        // number shown cannot disagree (fuel_estimate.h — which also documents
        // why the first lap is skipped).
        const float avgFuelPerLap = tracker.averagePerLap();

        if (FuelEstimate::hasAverage(avgFuelPerLap)) {
            float displayAvg = avgFuelPerLap * unitConversion;
            formatFuelValue(avgValueBuffer, sizeof(avgValueBuffer), displayAvg, unitLabel);

            const float estimatedLaps =
                FuelEstimate::lapsRemaining(bikeData.fuel, avgFuelPerLap);
            formatFuelValue(lapsValueBuffer, sizeof(lapsValueBuffer), estimatedLaps, "");

            // Color code estimated laps (negative if < 2 laps, warning if < 4)
            if (estimatedLaps < FuelEstimate::kCriticalLaps) {
                estColor = this->getColor(ColorSlot::NEGATIVE);
            } else if (estimatedLaps < FuelEstimate::kWarnLaps) {
                estColor = this->getColor(ColorSlot::WARNING);
            }
        } else {
            // No lap data yet - show dashes
            snprintf(avgValueBuffer, sizeof(avgValueBuffer), "%s", Placeholders::GENERIC);
            snprintf(lapsValueBuffer, sizeof(lapsValueBuffer), "%s", Placeholders::GENERIC);
            avgColor = mutedColor;
            estColor = mutedColor;
        }
    }

    // Row 1: Fuel level
    if (m_enabledRows & ROW_FUEL) {
        addLabel(TR("Fue"), contentStartX, currentY, Justify::LEFT, labelColor, dim);
        addString(fuelValueBuffer, rightX, currentY, Justify::RIGHT,
            this->getFont(FontCategory::DIGITS), fuelColor, dim.fontSize);
        currentY += dim.lineHeightNormal;
    }

    // Row 2: Use (total fuel used this run)
    if (m_enabledRows & ROW_USED) {
        addLabel(TR("Use"), contentStartX, currentY, Justify::LEFT, labelColor, dim);
        addString(usedValueBuffer, rightX, currentY, Justify::RIGHT,
            this->getFont(FontCategory::DIGITS), usedColor, dim.fontSize);
        currentY += dim.lineHeightNormal;
    }

    // Row 3: Avg (abbreviated from Avg/Lap)
    if (m_enabledRows & ROW_AVG) {
        addLabel(TR("Avg"), contentStartX, currentY, Justify::LEFT, labelColor, dim);
        addString(avgValueBuffer, rightX, currentY, Justify::RIGHT,
            this->getFont(FontCategory::DIGITS), avgColor, dim.fontSize);
        currentY += dim.lineHeightNormal;
    }

    // Row 4: Est (abbreviated from Est Laps)
    if (m_enabledRows & ROW_EST) {
        addLabel(TR("Est"), contentStartX, currentY, Justify::LEFT, labelColor, dim);
        addString(lapsValueBuffer, rightX, currentY, Justify::RIGHT,
            this->getFont(FontCategory::DIGITS), estColor, dim.fontSize);
    }

    // Set bounds for drag detection
    setBounds(startX, startY, startX + backgroundWidth, startY + backgroundHeight);
}

void FuelWidget::resetToDefaults() {
    m_bVisible = false;
    m_bShowTitle = false;  // No title by default
    setTextureVariant(0);  // No texture by default
    m_fBackgroundOpacity = 1.0f;
    setScale(1.0f);
    m_enabledRows = ROW_DEFAULT;  // Reset row visibility
    // Note: fuelUnit is NOT reset here - it's a global preference, not per-profile
    setPosition(cellsX(148), cellsY(74));
    setDataDirty();
}
