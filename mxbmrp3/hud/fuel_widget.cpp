// ============================================================================
// hud/fuel_widget.cpp
// Fuel calculator widget - displays fuel level, avg consumption, and estimated laps
// ============================================================================
#include "fuel_widget.h"

#include <cstdio>
#include <cmath>
#include <numeric>

#include "../diagnostics/logger.h"
#include "../core/plugin_utils.h"
#include "../core/color_config.h"

using namespace PluginConstants;

namespace {
    // Widget dimensions
    constexpr int FUEL_WIDGET_WIDTH = 8;  // Width in characters (compact)
    constexpr float FUEL_WIDGET_Y = 0.2776f;  // Base Y position for fuel widget
}

FuelWidget::FuelWidget()
    : m_fuelAtRunStart(0.0f)
    , m_fuelAtLapStart(0.0f)
    , m_lastTrackedLapNum(-1)
    , m_bTrackingActive(false)
    , m_totalLapsRecorded(0)
{
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
    // NOTE: Fuel tracking always runs so history accumulates even when hidden.
    // This ensures accurate fuel/lap data is available when widget is enabled.
    updateFuelTracking();

    // OPTIMIZATION: Only rebuild render data when visible
    if (isVisible()) {
        rebuildRenderData();
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

        // Record fuel for next lap
        m_fuelAtLapStart = bikeData.fuel;
        m_lastTrackedLapNum = currentLapNum;
    }
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
    // Fast path - only update positions
    auto dim = getScaledDimensions();

    float startX = 0.0f;
    float startY = 0.0f;

    // Calculate dimensions based on enabled rows
    int rowCount = getEnabledRowCount();
    float backgroundWidth = calculateBackgroundWidth(FUEL_WIDGET_WIDTH);
    float titleHeight = m_bShowTitle ? dim.lineHeightNormal : 0.0f;
    float contentHeight = titleHeight + dim.lineHeightNormal * rowCount;
    float backgroundHeight = dim.paddingV + contentHeight + dim.paddingV;

    // Set bounds for drag detection
    setBounds(startX, startY, startX + backgroundWidth, startY + backgroundHeight);

    // Update background quad position
    updateBackgroundQuadPosition(startX, startY, backgroundWidth, backgroundHeight);

    float contentStartX = startX + dim.paddingH;
    float contentStartY = startY + dim.paddingV;
    float rightX = startX + backgroundWidth - dim.paddingH;
    float currentY = contentStartY;

    // Position strings if they exist
    size_t stringIndex = 0;

    // Title (optional)
    if (m_bShowTitle && positionString(stringIndex, contentStartX, currentY)) {
        stringIndex++;
        currentY += titleHeight;
    }

    // Row 1: Fuel label and value
    if (m_enabledRows & ROW_FUEL) {
        positionString(stringIndex++, contentStartX, currentY);
        positionString(stringIndex++, rightX, currentY);
        currentY += dim.lineHeightNormal;
    }

    // Row 2: Use label and value
    if (m_enabledRows & ROW_USED) {
        positionString(stringIndex++, contentStartX, currentY);
        positionString(stringIndex++, rightX, currentY);
        currentY += dim.lineHeightNormal;
    }

    // Row 3: Avg label and value
    if (m_enabledRows & ROW_AVG) {
        positionString(stringIndex++, contentStartX, currentY);
        positionString(stringIndex++, rightX, currentY);
        currentY += dim.lineHeightNormal;
    }

    // Row 4: Est label and value
    if (m_enabledRows & ROW_EST) {
        positionString(stringIndex++, contentStartX, currentY);
        positionString(stringIndex, rightX, currentY);
    }
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

    // Calculate dimensions based on enabled rows
    int rowCount = getEnabledRowCount();
    float backgroundWidth = calculateBackgroundWidth(FUEL_WIDGET_WIDTH);
    float titleHeight = m_bShowTitle ? dim.lineHeightNormal : 0.0f;
    float contentHeight = titleHeight + dim.lineHeightNormal * rowCount;
    float backgroundHeight = dim.paddingV + contentHeight + dim.paddingV;

    // Add background quad
    addBackgroundQuad(startX, startY, backgroundWidth, backgroundHeight);

    float contentStartX = startX + dim.paddingH;
    float contentStartY = startY + dim.paddingV;
    float rightX = startX + backgroundWidth - dim.paddingH;
    float currentY = contentStartY;

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
        snprintf(fuelValueBuffer, sizeof(fuelValueBuffer), "%.1f%s", displayFuel, unitLabel);

        // Total fuel used this run
        if (m_bTrackingActive && m_fuelAtRunStart > 0.0f) {
            float fuelUsed = (m_fuelAtRunStart - bikeData.fuel) * unitConversion;
            snprintf(usedValueBuffer, sizeof(usedValueBuffer), "%.1f%s", fuelUsed, unitLabel);
        } else {
            snprintf(usedValueBuffer, sizeof(usedValueBuffer), "%s", Placeholders::GENERIC);
            usedColor = mutedColor;
        }

        // Calculate average fuel per lap
        // Skip the first lap if we have 2+ laps AND first lap is still in buffer
        // (first lap includes grid time, which inflates consumption)
        float avgFuelPerLap = 0.0f;
        if (!m_fuelPerLap.empty()) {
            float totalFuel = 0.0f;
            // First lap is still in buffer if totalLapsRecorded == size (no rollover yet)
            bool firstLapInBuffer = (m_totalLapsRecorded == m_fuelPerLap.size());
            size_t startIdx = (firstLapInBuffer && m_fuelPerLap.size() > 1) ? 1 : 0;
            for (size_t i = startIdx; i < m_fuelPerLap.size(); ++i) {
                totalFuel += m_fuelPerLap[i];
            }
            avgFuelPerLap = totalFuel / static_cast<float>(m_fuelPerLap.size() - startIdx);
        }

        if (avgFuelPerLap > 0.0f) {
            float displayAvg = avgFuelPerLap * unitConversion;
            snprintf(avgValueBuffer, sizeof(avgValueBuffer), "%.1f%s", displayAvg, unitLabel);

            // Estimated laps remaining
            float estimatedLaps = bikeData.fuel / avgFuelPerLap;
            snprintf(lapsValueBuffer, sizeof(lapsValueBuffer), "%.1f", estimatedLaps);

            // Color code estimated laps (negative if < 2 laps, warning if < 4)
            if (estimatedLaps < 2.0f) {
                estColor = this->getColor(ColorSlot::NEGATIVE);
            } else if (estimatedLaps < 4.0f) {
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

    // Title (optional)
    if (m_bShowTitle) {
        addString("Fuel", contentStartX, currentY, Justify::LEFT,
            this->getFont(FontCategory::TITLE), valueColor, dim.fontSize);
        currentY += titleHeight;
    }

    // Row 1: Fuel level
    if (m_enabledRows & ROW_FUEL) {
        addString("Fue", contentStartX, currentY, Justify::LEFT,
            this->getFont(FontCategory::NORMAL), labelColor, dim.fontSize);
        addString(fuelValueBuffer, rightX, currentY, Justify::RIGHT,
            this->getFont(FontCategory::DIGITS), fuelColor, dim.fontSize);
        currentY += dim.lineHeightNormal;
    }

    // Row 2: Use (total fuel used this run)
    if (m_enabledRows & ROW_USED) {
        addString("Use", contentStartX, currentY, Justify::LEFT,
            this->getFont(FontCategory::NORMAL), labelColor, dim.fontSize);
        addString(usedValueBuffer, rightX, currentY, Justify::RIGHT,
            this->getFont(FontCategory::DIGITS), usedColor, dim.fontSize);
        currentY += dim.lineHeightNormal;
    }

    // Row 3: Avg (abbreviated from Avg/Lap)
    if (m_enabledRows & ROW_AVG) {
        addString("Avg", contentStartX, currentY, Justify::LEFT,
            this->getFont(FontCategory::NORMAL), labelColor, dim.fontSize);
        addString(avgValueBuffer, rightX, currentY, Justify::RIGHT,
            this->getFont(FontCategory::DIGITS), avgColor, dim.fontSize);
        currentY += dim.lineHeightNormal;
    }

    // Row 4: Est (abbreviated from Est Laps)
    if (m_enabledRows & ROW_EST) {
        addString("Est", contentStartX, currentY, Justify::LEFT,
            this->getFont(FontCategory::NORMAL), labelColor, dim.fontSize);
        addString(lapsValueBuffer, rightX, currentY, Justify::RIGHT,
            this->getFont(FontCategory::DIGITS), estColor, dim.fontSize);
    }

    // Set bounds for drag detection
    setBounds(startX, startY, startX + backgroundWidth, startY + backgroundHeight);
}

void FuelWidget::resetToDefaults() {
    m_bVisible = true;
    m_bShowTitle = false;  // No title by default
    setTextureVariant(0);  // No texture by default
    m_fBackgroundOpacity = 1.0f;
    m_fScale = 1.0f;
    m_enabledRows = ROW_DEFAULT;  // Reset row visibility
    // Note: fuelUnit is NOT reset here - it's a global preference, not per-profile
    setPosition(0.9295f, 0.8547f);
    resetFuelTracking();
    setDataDirty();
}
