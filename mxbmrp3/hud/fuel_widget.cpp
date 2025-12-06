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
    initializeWidget("FuelWidget", 9, 1.0f);  // 9 strings: title + 4 labels + 4 values
    m_bShowTitle = false;  // No title by default
    setPosition(0.462f, 0.5772f);
    m_fuelPerLap.reserve(MAX_FUEL_HISTORY);
}

bool FuelWidget::handlesDataType(DataChangeType dataType) const {
    // Update on telemetry changes (fuel data) and lap log changes (lap completion)
    return dataType == DataChangeType::InputTelemetry ||
           dataType == DataChangeType::LapLog ||
           dataType == DataChangeType::SpectateTarget ||
           dataType == DataChangeType::SessionData;
}

void FuelWidget::update() {
    // Update fuel tracking logic
    updateFuelTracking();

    // Always rebuild - fuel updates at telemetry rate
    rebuildRenderData();
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

    // Get current lap number from session best data
    const SessionBestData* sessionBest = pluginData.getSessionBestData(pluginData.getPlayerRaceNum());
    if (!sessionBest) {
        return;
    }

    int currentLapNum = sessionBest->lastCompletedLapNum;

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

void FuelWidget::rebuildLayout() {
    // Fast path - only update positions
    auto dim = getScaledDimensions();

    float startX = WidgetPositions::WIDGET_STACK_X;
    float startY = FUEL_WIDGET_Y;

    // Calculate dimensions
    float backgroundWidth = calculateBackgroundWidth(FUEL_WIDGET_WIDTH);
    float titleHeight = m_bShowTitle ? dim.lineHeightNormal : 0.0f;
    float contentHeight = titleHeight + dim.lineHeightNormal * 4;  // title (optional) + 4 data rows
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
    positionString(stringIndex++, contentStartX, currentY);
    positionString(stringIndex++, rightX, currentY);
    currentY += dim.lineHeightNormal;

    // Row 2: Use label and value
    positionString(stringIndex++, contentStartX, currentY);
    positionString(stringIndex++, rightX, currentY);
    currentY += dim.lineHeightNormal;

    // Row 3: Avg label and value
    positionString(stringIndex++, contentStartX, currentY);
    positionString(stringIndex++, rightX, currentY);
    currentY += dim.lineHeightNormal;

    // Row 4: Est label and value
    positionString(stringIndex++, contentStartX, currentY);
    positionString(stringIndex, rightX, currentY);
}

void FuelWidget::rebuildRenderData() {
    m_strings.clear();
    m_quads.clear();

    auto dim = getScaledDimensions();

    // Get telemetry data
    const PluginData& pluginData = PluginData::getInstance();
    const BikeTelemetryData& bikeData = pluginData.getBikeTelemetry();

    float startX = WidgetPositions::WIDGET_STACK_X;
    float startY = FUEL_WIDGET_Y;

    // Calculate dimensions
    float backgroundWidth = calculateBackgroundWidth(FUEL_WIDGET_WIDTH);
    float titleHeight = m_bShowTitle ? dim.lineHeightNormal : 0.0f;
    float contentHeight = titleHeight + dim.lineHeightNormal * 4;  // title (optional) + 4 data rows
    float backgroundHeight = dim.paddingV + contentHeight + dim.paddingV;

    // Add background quad
    addBackgroundQuad(startX, startY, backgroundWidth, backgroundHeight);

    float contentStartX = startX + dim.paddingH;
    float contentStartY = startY + dim.paddingV;
    float rightX = startX + backgroundWidth - dim.paddingH;
    float currentY = contentStartY;

    unsigned long labelColor = TextColors::SECONDARY;
    unsigned long valueColor = TextColors::PRIMARY;
    unsigned long estColor = TextColors::PRIMARY;

    // Prepare display values
    char fuelValueBuffer[16];
    char usedValueBuffer[16];
    char avgValueBuffer[16];
    char lapsValueBuffer[16];

    if (!bikeData.isValid) {
        // Show placeholders when telemetry not available
        snprintf(fuelValueBuffer, sizeof(fuelValueBuffer), "%s", Placeholders::GENERIC);
        snprintf(usedValueBuffer, sizeof(usedValueBuffer), "%s", Placeholders::GENERIC);
        snprintf(avgValueBuffer, sizeof(avgValueBuffer), "%s", Placeholders::GENERIC);
        snprintf(lapsValueBuffer, sizeof(lapsValueBuffer), "%s", Placeholders::GENERIC);
    } else {
        // Current fuel (liters)
        snprintf(fuelValueBuffer, sizeof(fuelValueBuffer), "%.1fL", bikeData.fuel);

        // Total fuel used this run
        if (m_bTrackingActive && m_fuelAtRunStart > 0.0f) {
            float fuelUsed = m_fuelAtRunStart - bikeData.fuel;
            snprintf(usedValueBuffer, sizeof(usedValueBuffer), "%.1fL", fuelUsed);
        } else {
            snprintf(usedValueBuffer, sizeof(usedValueBuffer), "%s", Placeholders::GENERIC);
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
            snprintf(avgValueBuffer, sizeof(avgValueBuffer), "%.1fL", avgFuelPerLap);

            // Estimated laps remaining
            float estimatedLaps = bikeData.fuel / avgFuelPerLap;
            snprintf(lapsValueBuffer, sizeof(lapsValueBuffer), "%.1f", estimatedLaps);

            // Color code estimated laps (red if < 2 laps, yellow if < 4)
            if (estimatedLaps < 2.0f) {
                estColor = Colors::RED;
            } else if (estimatedLaps < 4.0f) {
                estColor = Colors::YELLOW;
            }
        } else {
            // No lap data yet - show dashes
            snprintf(avgValueBuffer, sizeof(avgValueBuffer), "%s", Placeholders::GENERIC);
            snprintf(lapsValueBuffer, sizeof(lapsValueBuffer), "%s", Placeholders::GENERIC);
        }
    }

    // Title (optional)
    if (m_bShowTitle) {
        addString("Fuel", contentStartX, currentY, Justify::LEFT,
            Fonts::ENTER_SANSMAN, valueColor, dim.fontSize);
        currentY += titleHeight;
    }

    // Row 1: Fuel level
    addString("Fue", contentStartX, currentY, Justify::LEFT,
        Fonts::ROBOTO_MONO, labelColor, dim.fontSize);
    addString(fuelValueBuffer, rightX, currentY, Justify::RIGHT,
        Fonts::ROBOTO_MONO, valueColor, dim.fontSize);
    currentY += dim.lineHeightNormal;

    // Row 2: Use (total fuel used this run)
    addString("Use", contentStartX, currentY, Justify::LEFT,
        Fonts::ROBOTO_MONO, labelColor, dim.fontSize);
    addString(usedValueBuffer, rightX, currentY, Justify::RIGHT,
        Fonts::ROBOTO_MONO, valueColor, dim.fontSize);
    currentY += dim.lineHeightNormal;

    // Row 3: Avg (abbreviated from Avg/Lap)
    addString("Avg", contentStartX, currentY, Justify::LEFT,
        Fonts::ROBOTO_MONO, labelColor, dim.fontSize);
    addString(avgValueBuffer, rightX, currentY, Justify::RIGHT,
        Fonts::ROBOTO_MONO, valueColor, dim.fontSize);
    currentY += dim.lineHeightNormal;

    // Row 4: Est (abbreviated from Est Laps)
    addString("Est", contentStartX, currentY, Justify::LEFT,
        Fonts::ROBOTO_MONO, labelColor, dim.fontSize);
    addString(lapsValueBuffer, rightX, currentY, Justify::RIGHT,
        Fonts::ROBOTO_MONO, estColor, dim.fontSize);

    // Set bounds for drag detection
    setBounds(startX, startY, startX + backgroundWidth, startY + backgroundHeight);
}

void FuelWidget::resetToDefaults() {
    m_bVisible = true;
    m_bShowTitle = false;  // No title by default
    m_bShowBackgroundTexture = false;
    m_fBackgroundOpacity = 1.0f;
    m_fScale = 1.0f;
    setPosition(0.462f, 0.5772f);
    resetFuelTracking();
    setDataDirty();
}
