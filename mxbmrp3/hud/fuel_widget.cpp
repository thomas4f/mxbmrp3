// ============================================================================
// hud/fuel_widget.cpp
// Fuel calculator widget - displays fuel level, avg consumption, and estimated laps
// ============================================================================
#include "fuel_widget.h"

#include "../core/fuel_estimate.h"

#include <algorithm>
#include <cstdio>
#include <cmath>
#include <numeric>

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

FuelWidget::FuelWidget()
    : m_cachedSessionGeneration(-1)
    , m_fuelAtRunStart(0.0f)
    , m_fuelAtLapStart(0.0f)
    , m_lastTrackedLapNum(-1)
    , m_bTrackingActive(false)
    , m_totalLapsRecorded(0)
{
    m_panelKind = PanelKind::Widget;
    m_bContentCard = true;
    // One-time setup
    DEBUG_INFO("FuelWidget created");
    setDraggable(true);
    m_strings.reserve(9);  // title + 4 labels + 4 values
    m_fuelPerLap.reserve(MAX_FUEL_HISTORY);

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
    // Detect session changes (new track/bike/session) and reset fuel tracking
    const SessionData& sessionData = PluginData::getInstance().getSessionData();
    int currentGeneration = sessionData.sessionGeneration;
    if (currentGeneration != m_cachedSessionGeneration) {
        resetFuelTracking();
        m_cachedSessionGeneration = currentGeneration;
    }

    // NOTE: Fuel tracking always runs so history accumulates even when hidden.
    // This ensures accurate fuel/lap data is available when widget is enabled.
    updateFuelTracking();

    // OPTIMIZATION: Only rebuild render data when visible
    if (isVisibleAnySurface()) {
        rebuildAndRecord();
    }
    clearDataDirty();
    clearLayoutDirty();
}

void FuelWidget::updateFuelTracking() {
    const PluginData& pluginData = PluginData::getInstance();
    const BikeTelemetryData& bikeData = pluginData.getBikeTelemetry();

    // Only track fuel for player (not spectated riders)
    bool isViewingPlayerBike = (pluginData.getDisplayRaceNum() == pluginData.getPlayerRaceNum());
    if (!isViewingPlayerBike || !bikeData.isValid) {
        return;
    }

    // Start tracking as soon as we have valid telemetry (captures fuel at race start)
    if (!m_bTrackingActive) {
        m_fuelAtRunStart = bikeData.fuel;
        m_fuelAtLapStart = bikeData.fuel;
        m_bTrackingActive = true;
        DEBUG_INFO_F("FuelWidget: Started tracking with %.2fL", m_fuelAtRunStart);
    }

    // Detect refueling: if current fuel exceeds what we started with, rider refueled in pits
    // Reset tracking to avoid negative usage values
    if (m_bTrackingActive && bikeData.fuel > m_fuelAtRunStart) {
        DEBUG_INFO_F("FuelWidget: Detected refueling (%.2fL > %.2fL start), resetting tracking",
                    bikeData.fuel, m_fuelAtRunStart);
        m_fuelAtRunStart = bikeData.fuel;
        m_fuelAtLapStart = bikeData.fuel;
        // Keep lap history for averaging, just reset the run start reference
    }

    // Get current lap number from ideal lap data
    const IdealLapData* idealLapData = pluginData.getIdealLapData(pluginData.getPlayerRaceNum());
    if (!idealLapData) {
        return;
    }

    int currentLapNum = idealLapData->lastCompletedLapNum;

    // Check if a new lap was completed
    if (currentLapNum > m_lastTrackedLapNum) {
        // Only record fuel consumption for valid laps (has a positive lap time).
        // Invalid laps (pit-in without completing, cut track, etc.) have lastLapTime <= 0
        // and would skew the average with unrealistic fuel consumption values.
        bool isValidLap = (idealLapData->lastLapTime > 0);

        if (isValidLap) {
            // Calculate fuel used this lap
            float fuelUsed = m_fuelAtLapStart - bikeData.fuel;

            // Only record if fuel was actually consumed (sanity check)
            if (fuelUsed > 0.0f && fuelUsed < bikeData.maxFuel) {
                m_fuelPerLap.push_back(fuelUsed);
                m_totalLapsRecorded++;

                // Keep only the last MAX_FUEL_HISTORY entries
                if (m_fuelPerLap.size() > MAX_FUEL_HISTORY) {
                    m_fuelPerLap.erase(m_fuelPerLap.begin());
                }

                DEBUG_INFO_F("FuelWidget: Lap %d consumed %.2fL (avg: %.2fL)",
                            currentLapNum + 1, fuelUsed,
                            m_fuelPerLap.empty() ? 0.0f :
                            std::accumulate(m_fuelPerLap.begin(), m_fuelPerLap.end(), 0.0f) / m_fuelPerLap.size());
            }
        } else {
            DEBUG_INFO_F("FuelWidget: Lap %d skipped (invalid lap, no timing data)", currentLapNum + 1);
        }

        // Always update lap start reference and lap number, even for invalid laps,
        // so the next valid lap measures from the correct fuel level
        m_fuelAtLapStart = bikeData.fuel;
        m_lastTrackedLapNum = currentLapNum;
    }
}

float FuelWidget::getLapsRemaining() const {
    const PluginData& pd = PluginData::getInstance();
    // Fuel is the PLAYER's telemetry and only exists on track — spectating or
    // in a replay there is nothing to estimate from.
    if (pd.getDrawState() != ViewState::ON_TRACK) return -1.0f;
    return FuelEstimate::lapsRemaining(
        pd.getBikeTelemetry().fuel,
        FuelEstimate::averagePerLap(m_fuelPerLap, m_totalLapsRecorded));
}

void FuelWidget::resetFuelTracking() {
    m_fuelPerLap.clear();
    m_fuelAtRunStart = 0.0f;
    m_fuelAtLapStart = 0.0f;
    m_lastTrackedLapNum = -1;
    m_bTrackingActive = false;
    m_totalLapsRecorded = 0;
    setDataDirty();
    DEBUG_INFO("FuelWidget: Fuel tracking reset");
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
    // duplicated copy of the sizing arithmetic (see version_widget).
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
    BaseHud::PanelWant want;
    want.contentW = PluginUtils::calculateMonospaceTextWidth(FUEL_WIDGET_WIDTH, dim.fontSize);
    want.sectionH = { dim.lineHeightNormal * rowCount };
    want.captionW = planTitleWidth(dim, "Fuel");
    PanelPlan& p = planPanel(dim, want);

    const float backgroundWidth = p.width();
    const float backgroundHeight = p.height();

    addPlanBackground(p, startX, startY);
    addPlanTitle(p, "Fuel", this->getFont(FontCategory::TITLE),
        this->getColor(ColorSlot::PRIMARY));

    float contentStartX = p.contentX();
    // The content box's own right edge. This mirrored the LEFT inset, which is the
    // same place only on a symmetric border -- see PanelPlan::contentRight.
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
        const char* unitLabel = (m_fuelUnit == FuelUnit::GALLONS) ? "g" : "L";
        float unitConversion = (m_fuelUnit == FuelUnit::GALLONS) ? UnitConversion::LITERS_TO_GALLONS : 1.0f;

        // Current fuel
        float displayFuel = bikeData.fuel * unitConversion;
        formatFuelValue(fuelValueBuffer, sizeof(fuelValueBuffer), displayFuel, unitLabel);

        // Total fuel used this run
        if (m_bTrackingActive && m_fuelAtRunStart > 0.0f) {
            float fuelUsed = (m_fuelAtRunStart - bikeData.fuel) * unitConversion;
            formatFuelValue(usedValueBuffer, sizeof(usedValueBuffer), fuelUsed, unitLabel);
        } else {
            snprintf(usedValueBuffer, sizeof(usedValueBuffer), "%s", Placeholders::GENERIC);
            usedColor = mutedColor;
        }

        // Calculate average fuel per lap
        // Shared with the spotter's fuel warning, so the number spoken and the
        // number shown cannot disagree (fuel_estimate.h — which also documents
        // why the first lap is skipped).
        const float avgFuelPerLap =
            FuelEstimate::averagePerLap(m_fuelPerLap, m_totalLapsRecorded);

        if (avgFuelPerLap > 0.001f) {
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
        addString("Fue", contentStartX, currentY, Justify::LEFT,
            this->getFont(FontCategory::STRONG), labelColor, dim.fontSize);
        addString(fuelValueBuffer, rightX, currentY, Justify::RIGHT,
            this->getFont(FontCategory::DIGITS), fuelColor, dim.fontSize);
        currentY += dim.lineHeightNormal;
    }

    // Row 2: Use (total fuel used this run)
    if (m_enabledRows & ROW_USED) {
        addString("Use", contentStartX, currentY, Justify::LEFT,
            this->getFont(FontCategory::STRONG), labelColor, dim.fontSize);
        addString(usedValueBuffer, rightX, currentY, Justify::RIGHT,
            this->getFont(FontCategory::DIGITS), usedColor, dim.fontSize);
        currentY += dim.lineHeightNormal;
    }

    // Row 3: Avg (abbreviated from Avg/Lap)
    if (m_enabledRows & ROW_AVG) {
        addString("Avg", contentStartX, currentY, Justify::LEFT,
            this->getFont(FontCategory::STRONG), labelColor, dim.fontSize);
        addString(avgValueBuffer, rightX, currentY, Justify::RIGHT,
            this->getFont(FontCategory::DIGITS), avgColor, dim.fontSize);
        currentY += dim.lineHeightNormal;
    }

    // Row 4: Est (abbreviated from Est Laps)
    if (m_enabledRows & ROW_EST) {
        addString("Est", contentStartX, currentY, Justify::LEFT,
            this->getFont(FontCategory::STRONG), labelColor, dim.fontSize);
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
    m_fScale = 1.0f;
    m_enabledRows = ROW_DEFAULT;  // Reset row visibility
    // Note: fuelUnit is NOT reset here - it's a global preference, not per-profile
    setPosition(cellsX(148), cellsY(74));
    resetFuelTracking();
    setDataDirty();
}
