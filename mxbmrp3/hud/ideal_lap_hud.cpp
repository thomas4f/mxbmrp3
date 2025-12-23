// ============================================================================
// hud/ideal_lap_hud.cpp
// Displays ideal lap (best individual sectors) with comparison to current
// ============================================================================
#include "ideal_lap_hud.h"

#include <cstring>
#include <cstdio>

#include "../diagnostics/logger.h"
#include "../diagnostics/timer.h"
#include "../core/plugin_utils.h"
#include "../core/plugin_constants.h"
#include "../core/plugin_data.h"
#include "../core/color_config.h"

using namespace PluginConstants;

IdealLapHud::ColumnPositions::ColumnPositions(float contentStartX, float scale) {
    float scaledFontSize = FontSizes::NORMAL * scale;
    label = contentStartX;
    time = label + PluginUtils::calculateMonospaceTextWidth(COL_LABEL_WIDTH, scaledFontSize);
    diff = time + PluginUtils::calculateMonospaceTextWidth(COL_TIME_WIDTH, scaledFontSize);
}

IdealLapHud::IdealLapHud()
    : m_columns(START_X + Padding::HUD_HORIZONTAL, m_fScale)
{
    // One-time setup
    DEBUG_INFO("IdealLapHud created");
    setDraggable(true);
    m_quads.reserve(1);
    m_strings.reserve(20);  // Header row + 6 data rows

    // Set texture base name for dynamic texture discovery
    setTextureBaseName("ideal_lap_hud");

    // Set all configurable defaults
    resetToDefaults();

    rebuildRenderData();
}

bool IdealLapHud::handlesDataType(DataChangeType dataType) const {
    return (dataType == DataChangeType::IdealLap ||
            dataType == DataChangeType::LapLog ||
            dataType == DataChangeType::SpectateTarget);
}

int IdealLapHud::getEnabledRowCount() const {
    int count = 0;
    if (m_enabledRows & ROW_S1) count++;
    if (m_enabledRows & ROW_S2) count++;
    if (m_enabledRows & ROW_S3) count++;
    if (m_enabledRows & ROW_LAST) count++;
    if (m_enabledRows & ROW_BEST) count++;
    if (m_enabledRows & ROW_IDEAL) count++;
    return count;
}

void IdealLapHud::update() {
    // Check if we need frequent updates for ticking sector time (uses BaseHud helper)
    checkFrequentUpdates();

    // Check if data changed or layout dirty
    if (isDataDirty()) {
        // Data changed - full rebuild needed (also rebuilds layout)
        rebuildRenderData();
        clearDataDirty();
        clearLayoutDirty();  // Clear layout dirty too since full rebuild done
    }
    else if (isLayoutDirty()) {
        // Only layout changed (e.g., dragging) - fast path
        rebuildLayout();
        clearLayoutDirty();
    }
}

bool IdealLapHud::needsFrequentUpdates() const {
    // IdealLapHud only updates on split/lap events, no live timing display
    return false;
}

void IdealLapHud::rebuildLayout() {
    // Fast path - only update positions, don't rebuild strings
    auto dim = getScaledDimensions();
    float titleHeight = m_bShowTitle ? dim.lineHeightLarge : 0.0f;

    // Recalculate column positions for current scale
    float contentStartX = START_X + dim.paddingH;
    m_columns = ColumnPositions(contentStartX, m_fScale);

    // Calculate row count from actual string count
    // Title string + 3 strings per row (label, time, diff)
    size_t stringCount = m_strings.size();
    if (stringCount <= 1) {
        return;  // Nothing to update (0 or just title)
    }
    int rowCount = static_cast<int>((stringCount - 1) / 3);  // Subtract title, divide by 3 strings per row

    // Calculate background dimensions using helpers
    float backgroundWidth = calculateBackgroundWidth(BACKGROUND_WIDTH_CHARS);
    float backgroundHeight = calculateBackgroundHeight(rowCount);

    setBounds(START_X, START_Y, START_X + backgroundWidth, START_Y + backgroundHeight);

    // Update background quad position (if background is shown)
    updateBackgroundQuadPosition(START_X, START_Y, backgroundWidth, backgroundHeight);

    // Update string positions
    float contentStartY = START_Y + dim.paddingV;
    float currentY = contentStartY;

    size_t stringIndex = 0;
    // Title string (always exists, but may be empty if hidden)
    if (stringIndex < m_strings.size()) {
        float x = contentStartX;
        float y = currentY;
        applyOffset(x, y);
        m_strings[stringIndex].m_afPos[0] = x;
        m_strings[stringIndex].m_afPos[1] = y;
        stringIndex++;
    }
    currentY += titleHeight;

    // Handle data rows
    for (size_t i = stringIndex; i < m_strings.size(); i++) {
        float x = contentStartX;
        float y = currentY;

        // Data rows: 3 strings per row (label, time, diff)
        size_t dataIndex = i - stringIndex;
        size_t colInRow = dataIndex % 3;

        if (colInRow == 1) {
            x = m_columns.time;  // Time column
        } else if (colInRow == 2) {
            x = m_columns.diff;  // Diff column
        }
        // else: colInRow == 0, label column, x already = contentStartX

        applyOffset(x, y);
        m_strings[i].m_afPos[0] = x;
        m_strings[i].m_afPos[1] = y;

        // Move to next row after every 3 data strings
        if (colInRow == 2) {
            currentY += dim.lineHeightNormal;
        }
    }
}

void IdealLapHud::rebuildRenderData() {
    m_strings.clear();
    m_quads.clear();

    // Get player data
    const PluginData& data = PluginData::getInstance();

    const CurrentLapData* currentLap = data.getCurrentLapData();
    const IdealLapData* idealLapData = data.getIdealLapData();
    const LapLogEntry* personalBest = data.getBestLapEntry();

    // Calculate enabled row count and background dimensions using helpers
    int enabledRows = getEnabledRowCount();
    float backgroundWidth = calculateBackgroundWidth(BACKGROUND_WIDTH_CHARS);
    float backgroundHeight = calculateBackgroundHeight(enabledRows);

    // Get dimensions for positioning
    auto dim = getScaledDimensions();
    float titleHeight = m_bShowTitle ? dim.lineHeightLarge : 0.0f;

    setBounds(START_X, START_Y, START_X + backgroundWidth, START_Y + backgroundHeight);
    addBackgroundQuad(START_X, START_Y, backgroundWidth, backgroundHeight);

    float contentStartX = START_X + dim.paddingH;
    float contentStartY = START_Y + dim.paddingV;
    float currentY = contentStartY;

    // Title row
    addTitleString("Ideal Lap", contentStartX, currentY, Justify::LEFT,
        Fonts::getTitle(), ColorConfig::getInstance().getPrimary(), dim.fontSizeLarge);
    currentY += titleHeight;

    // Recalculate column positions for current scale
    m_columns = ColumnPositions(contentStartX, m_fScale);

    // Get ideal (purple) sector times - best individual sectors ever achieved
    int idealS1 = idealLapData ? idealLapData->bestSector1 : -1;
    int idealS2 = idealLapData ? idealLapData->bestSector2 : -1;
    int idealS3 = idealLapData ? idealLapData->bestSector3 : -1;
    int idealLapTime = idealLapData ? idealLapData->getIdealLapTime() : -1;

    // Calculate current sector times from current lap (accumulated times)
    int currentSector1 = -1, currentSector2 = -1;
    if (currentLap) {
        currentSector1 = currentLap->split1;
        if (currentLap->split2 > 0 && currentLap->split1 > 0) {
            currentSector2 = currentLap->split2 - currentLap->split1;
        }
    }


    // Get previous best times for showing improvement when setting new PB
    int prevBestS1 = idealLapData ? idealLapData->previousBestSector1 : -1;
    int prevBestS2 = idealLapData ? idealLapData->previousBestSector2 : -1;
    int prevBestS3 = idealLapData ? idealLapData->previousBestSector3 : -1;

    // Helper for adding a row
    // Shows the ideal time and gap from current to ideal
    // When gap is 0 and previousBest exists, shows improvement vs previous best instead
    auto addRow = [&](bool enabled, const char* label, int idealTimeMs, int currentTimeMs, int previousBestMs, int timeFont = Fonts::getNormal()) {
        if (!enabled) {
            return;  // Skip disabled rows entirely
        }

        char timeStr[16];
        char diffStr[16];
        char paddedLabel[8];

        // Right-align label by padding with spaces
        snprintf(paddedLabel, sizeof(paddedLabel), "%5s", label);

        // Label in secondary color
        addString(paddedLabel, m_columns.label, currentY, Justify::LEFT, Fonts::getNormal(), ColorConfig::getInstance().getSecondary(), dim.fontSize);

        // Show ideal time or placeholder
        if (idealTimeMs > 0) {
            PluginUtils::formatLapTime(idealTimeMs, timeStr, sizeof(timeStr));
            addString(timeStr, m_columns.time, currentY, Justify::LEFT, timeFont, ColorConfig::getInstance().getPrimary(), dim.fontSize);
        } else {
            strcpy_s(timeStr, sizeof(timeStr), Placeholders::LAP_TIME);
            addString(timeStr, m_columns.time, currentY, Justify::LEFT, Fonts::getNormal(), ColorConfig::getInstance().getMuted(), dim.fontSize);
        }

        // Show gap (current - ideal)
        // When gap is 0 (new PB), compare to previous best to show improvement
        if (idealTimeMs > 0 && currentTimeMs > 0) {
            int diff = currentTimeMs - idealTimeMs;
            if (diff == 0 && previousBestMs > 0) {
                // New PB equals current best - compare to previous best to show improvement
                diff = currentTimeMs - previousBestMs;
            }
            PluginUtils::formatTimeDiff(diffStr, sizeof(diffStr), diff);
            unsigned long diffColor = (diff <= 0)
                ? ColorConfig::getInstance().getPositive()   // On pace or faster (green)
                : ColorConfig::getInstance().getNegative();  // Slower (red)
            addString(diffStr, m_columns.diff, currentY, Justify::LEFT, Fonts::getNormal(), diffColor, dim.fontSize);
        } else {
            // No comparison available
            addString(Placeholders::GENERIC, m_columns.diff, currentY, Justify::LEFT, Fonts::getNormal(), ColorConfig::getInstance().getMuted(), dim.fontSize);
        }

        currentY += dim.lineHeightNormal;
    };

    // Determine what to show in gap column for each sector
    // If sector is complete (crossed), show gap to ideal
    // If sector is in progress, show placeholder (gap is meaningless until complete)
    // If sector not started, show placeholder

    // S1: Show ideal S1, gap = current S1 vs ideal S1
    int s1ForGap = (currentSector1 > 0) ? currentSector1 : -1;
    addRow(m_enabledRows & ROW_S1, "S1", idealS1, s1ForGap, prevBestS1);

    // S2: Show ideal S2, gap = current S2 vs ideal S2
    int s2ForGap = (currentSector2 > 0) ? currentSector2 : -1;
    addRow(m_enabledRows & ROW_S2, "S2", idealS2, s2ForGap, prevBestS2);

    // S3: Show ideal S3, gap = current S3 vs ideal S3
    // Note: S3 is never "crossed" in currentLap - lap completes and currentLap is cleared
    // So S3 gap column always shows placeholder (actual S3 gap visible in last lap row)
    addRow(m_enabledRows & ROW_S3, "S3", idealS3, -1, prevBestS3);

    // Calculate previous ideal lap time (sum of previous best sectors)
    // Used to show improvement when beating the ideal
    int prevIdealLapTime = -1;
    if (prevBestS1 > 0 && prevBestS2 > 0 && prevBestS3 > 0) {
        prevIdealLapTime = prevBestS1 + prevBestS2 + prevBestS3;
    }

    // Helper for lap rows (Last/Best) - shows actual lap time and gap to ideal
    // When gap is 0 (beat the ideal), compare to previous ideal to show improvement
    auto addLapRow = [&](bool enabled, const char* label, int actualLapTime, int idealTime, int prevIdealTime, int timeFont = Fonts::getNormal()) {
        if (!enabled) return;

        char timeStr[16];
        char diffStr[16];
        char paddedLabel[8];

        snprintf(paddedLabel, sizeof(paddedLabel), "%5s", label);
        addString(paddedLabel, m_columns.label, currentY, Justify::LEFT, Fonts::getNormal(), ColorConfig::getInstance().getSecondary(), dim.fontSize);

        // Show actual lap time
        if (actualLapTime > 0) {
            PluginUtils::formatLapTime(actualLapTime, timeStr, sizeof(timeStr));
            addString(timeStr, m_columns.time, currentY, Justify::LEFT, timeFont, ColorConfig::getInstance().getPrimary(), dim.fontSize);
        } else {
            strcpy_s(timeStr, sizeof(timeStr), Placeholders::LAP_TIME);
            addString(timeStr, m_columns.time, currentY, Justify::LEFT, Fonts::getNormal(), ColorConfig::getInstance().getMuted(), dim.fontSize);
        }

        // Show gap (actual - ideal)
        // When gap <= 0 (beat the ideal), compare to previous ideal to show improvement
        if (idealTime > 0 && actualLapTime > 0) {
            int diff = actualLapTime - idealTime;
            if (diff <= 0 && prevIdealTime > 0) {
                // Beat or matched ideal - compare to previous ideal to show improvement
                diff = actualLapTime - prevIdealTime;
            }
            PluginUtils::formatTimeDiff(diffStr, sizeof(diffStr), diff);
            unsigned long diffColor = (diff <= 0)
                ? ColorConfig::getInstance().getPositive()   // On pace or faster (green)
                : ColorConfig::getInstance().getNegative();  // Slower (red)
            addString(diffStr, m_columns.diff, currentY, Justify::LEFT, Fonts::getNormal(), diffColor, dim.fontSize);
        } else {
            addString(Placeholders::GENERIC, m_columns.diff, currentY, Justify::LEFT, Fonts::getNormal(), ColorConfig::getInstance().getMuted(), dim.fontSize);
        }

        currentY += dim.lineHeightNormal;
    };

    // Last: Show last lap time, gap = last lap vs ideal lap
    int lastLap = (idealLapData && idealLapData->lastLapTime > 0) ? idealLapData->lastLapTime : -1;
    addLapRow(m_enabledRows & ROW_LAST, "Last", lastLap, idealLapTime, prevIdealLapTime);

    // Best: Show best lap time, gap = best lap vs ideal lap
    int bestLap = personalBest ? personalBest->lapTime : -1;
    addLapRow(m_enabledRows & ROW_BEST, "Best", bestLap, idealLapTime, prevIdealLapTime, Fonts::getStrong());

    // Ideal: Show ideal lap time (no gap - it IS the ideal)
    // Use a special version that doesn't show gap
    if (m_enabledRows & ROW_IDEAL) {
        char timeStr[16];
        char paddedLabel[8];
        snprintf(paddedLabel, sizeof(paddedLabel), "%5s", "Ideal");
        addString(paddedLabel, m_columns.label, currentY, Justify::LEFT, Fonts::getNormal(), ColorConfig::getInstance().getSecondary(), dim.fontSize);

        if (idealLapTime > 0) {
            PluginUtils::formatLapTime(idealLapTime, timeStr, sizeof(timeStr));
            addString(timeStr, m_columns.time, currentY, Justify::LEFT, Fonts::getStrong(), ColorConfig::getInstance().getPositive(), dim.fontSize);
        } else {
            strcpy_s(timeStr, sizeof(timeStr), Placeholders::LAP_TIME);
            addString(timeStr, m_columns.time, currentY, Justify::LEFT, Fonts::getNormal(), ColorConfig::getInstance().getMuted(), dim.fontSize);
        }
        // No gap for ideal row
        addString("", m_columns.diff, currentY, Justify::LEFT, Fonts::getNormal(), ColorConfig::getInstance().getMuted(), dim.fontSize);
        currentY += dim.lineHeightNormal;
    }
}

void IdealLapHud::resetToDefaults() {
    m_bVisible = false;  // Disabled by default
    m_bShowTitle = true;
    setTextureVariant(0);  // No texture by default
    m_fBackgroundOpacity = SettingsLimits::DEFAULT_OPACITY;
    m_fScale = 1.0f;
    setPosition(0.2695f, 0.7659f);
    m_enabledRows = ROW_DEFAULT;
    setDataDirty();
}
