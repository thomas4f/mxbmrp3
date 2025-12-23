// ============================================================================
// hud/lap_log_hud.cpp
// Lap Log - displays recent lap times with sector splits and personal best
// ============================================================================
#include "lap_log_hud.h"
#include "../diagnostics/logger.h"
#include "../diagnostics/timer.h"
#include "../core/plugin_utils.h"
#include "../core/plugin_constants.h"
#include "../core/plugin_data.h"
#include "../core/color_config.h"
#include <cstring>
#include <cstdio>

using namespace PluginConstants;

// Compile-time check: Display limit must not exceed storage capacity
static_assert(LapLogHud::MAX_DISPLAY_LAPS <= HudLimits::MAX_LAP_LOG_CAPACITY,
              "MAX_DISPLAY_LAPS cannot exceed MAX_LAP_LOG_CAPACITY");

LapLogHud::ColumnPositions::ColumnPositions(float contentStartX, float scale, uint32_t enabledColumns) {
    float scaledFontSize = FontSizes::NORMAL * scale;
    float current = contentStartX;

    // Use helper function to set column positions (eliminates duplicated lambda)
    // Calculate positions for enabled columns only
    PluginUtils::setColumnPosition(enabledColumns, COL_LAP, COL_LAP_WIDTH, scaledFontSize, current, lap);
    PluginUtils::setColumnPosition(enabledColumns, COL_S1, COL_TIME_WIDTH, scaledFontSize, current, s1);
    PluginUtils::setColumnPosition(enabledColumns, COL_S2, COL_TIME_WIDTH, scaledFontSize, current, s2);
    PluginUtils::setColumnPosition(enabledColumns, COL_S3, COL_TIME_WIDTH, scaledFontSize, current, s3);
    PluginUtils::setColumnPosition(enabledColumns, COL_TIME, COL_TIME_WIDTH, scaledFontSize, current, time);
}

LapLogHud::LapLogHud()
    : m_columns(START_X + Padding::HUD_HORIZONTAL, m_fScale, m_enabledColumns)
{
    // One-time setup
    DEBUG_INFO("LapLogHud created");
    setDraggable(true);
    m_quads.reserve(1);
    m_strings.reserve(1 + m_maxDisplayLaps * NUM_COLUMNS);  // Title + data rows (NUM_COLUMNS strings per row)

    // Set texture base name for dynamic texture discovery
    setTextureBaseName("lap_log_hud");

    // Set all configurable defaults
    resetToDefaults();

    rebuildRenderData();
}

bool LapLogHud::handlesDataType(DataChangeType dataType) const {
    return (dataType == DataChangeType::LapLog ||
            dataType == DataChangeType::IdealLap ||  // For live sector updates (current lap splits)
            dataType == DataChangeType::SpectateTarget);
}

int LapLogHud::getBackgroundWidthChars() const {
    int width = 0;
    if (m_enabledColumns & COL_LAP) width += COL_LAP_WIDTH;
    if (m_enabledColumns & COL_S1) width += COL_TIME_WIDTH;
    if (m_enabledColumns & COL_S2) width += COL_TIME_WIDTH;
    if (m_enabledColumns & COL_S3) width += COL_TIME_WIDTH;
    if (m_enabledColumns & COL_TIME) width += COL_LAST_TIME_WIDTH;  // Last column has no gap
    return width;
}

void LapLogHud::update() {
    // Check if we need frequent updates for ticking timer (uses BaseHud helper)
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

bool LapLogHud::needsFrequentUpdates() const {
    // Need frequent updates when live timing is enabled and timer is valid
    if (!m_showLiveTiming) return false;
    if (!m_bVisible) return false;

    const PluginData& data = PluginData::getInstance();
    if (!data.isLapTimerValid()) return false;
    if (data.isDisplayRiderFinished()) return false;  // Timer stopped after finish

    return true;
}

int LapLogHud::getCurrentActiveSector() const {
    const PluginData& data = PluginData::getInstance();
    if (!data.isLapTimerValid()) return -1;
    if (data.isDisplayRiderFinished()) return -1;  // No active sector after finish

    return data.getLapTimerCurrentSector();
}

void LapLogHud::rebuildLayout() {
    // Fast path - only update positions, don't rebuild strings
    auto dim = getScaledDimensions();

    int widthChars = getBackgroundWidthChars();
    float backgroundWidth = PluginUtils::calculateMonospaceTextWidth(widthChars, dim.fontSize)
        + dim.paddingH + dim.paddingH;
    // Title (large if shown) + actual data rows (use cached count from rebuildRenderData)
    float titleHeight = m_bShowTitle ? dim.lineHeightLarge : 0.0f;
    float backgroundHeight = dim.paddingV + titleHeight + (dim.lineHeightNormal * m_cachedNumDataRows) + dim.paddingV;

    setBounds(START_X, START_Y, START_X + backgroundWidth, START_Y + backgroundHeight);

    // Update background quad position (grows downward from START_Y)
    updateBackgroundQuadPosition(START_X, START_Y, backgroundWidth, backgroundHeight);

    // Update string positions (title at top if shown, data flows downward)
    float contentStartX = START_X + dim.paddingH;
    // Start at title position (top of HUD)
    float currentY = START_Y + dim.paddingV;

    // Recalculate column positions for current scale
    m_columns = ColumnPositions(contentStartX, m_fScale, m_enabledColumns);

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

    // Handle data rows - process all rows (NUM_COLUMNS columns each)
    // Use explicit column array for clarity and robustness
    const float columns[NUM_COLUMNS] = {
        m_columns.lap,
        m_columns.s1,
        m_columns.s2,
        m_columns.s3,
        m_columns.time
    };

    // Calculate expected number of data rows
    const int expectedRows = m_cachedNumDataRows;
    const int expectedStrings = expectedRows * NUM_COLUMNS;

    // Process all data rows
    for (int row = 0; row < expectedRows && stringIndex < m_strings.size(); ++row) {
        // Process all columns in this row
        for (int col = 0; col < NUM_COLUMNS && stringIndex < m_strings.size(); ++col) {
            float x = columns[col];
            float y = currentY;
            applyOffset(x, y);
            m_strings[stringIndex].m_afPos[0] = x;
            m_strings[stringIndex].m_afPos[1] = y;
            stringIndex++;
        }

        // Move down to next row
        currentY += dim.lineHeightNormal;
    }
}

void LapLogHud::rebuildRenderData() {
    m_strings.clear();
    m_quads.clear();

    // Get display rider data (player or spectated rider)
    const PluginData& data = PluginData::getInstance();

    const std::deque<LapLogEntry>* lapLog = data.getLapLog();
    const CurrentLapData* currentLap = data.getCurrentLapData();

    // Apply scale to all dimensions
    auto dim = getScaledDimensions();

    // Check if we should show a live "current lap in progress" row
    // Don't show live timing if rider has finished (timer is meaningless after checkered flag)
    bool showCurrentLapRow = m_showLiveTiming && data.isLapTimerValid() && !data.isDisplayRiderFinished();

    // Build display list: current lap first (if live), then best lap (if not in recent), then recent laps
    // This ensures the current lap is always visible at the top and HUD grows downward
    struct DisplayEntry {
        int historyIndex;  // Index into lapLog vector (-4 for current lap, -3 for best lap, -2 for placeholder)
        DisplayEntry(int idx) : historyIndex(idx) {}
    };
    std::vector<DisplayEntry> displayList;

    // Reserve first row for current lap in progress (if enabled and valid)
    int maxRecentLaps = m_maxDisplayLaps;
    if (showCurrentLapRow) {
        displayList.push_back(DisplayEntry(-4));  // -4 = current lap in progress
        maxRecentLaps = m_maxDisplayLaps - 1;  // Leave room for current lap
    }

    // Get the best lap entry (stored separately)
    const LapLogEntry* bestLapEntry = data.getBestLapEntry();

    // Check if best lap is in the recent history (first N laps)
    bool bestLapInRecent = false;
    if (bestLapEntry && lapLog) {
        for (int i = 0; i < maxRecentLaps && i < static_cast<int>(lapLog->size()); i++) {
            if ((*lapLog)[i].lapNum == bestLapEntry->lapNum) {
                bestLapInRecent = true;
                break;
            }
        }
    }

    // If best lap is not in the recent window, add it
    if (bestLapEntry && !bestLapInRecent) {
        displayList.push_back(DisplayEntry(-3));  // -3 = use bestLapEntry
        maxRecentLaps = maxRecentLaps - 1;  // Leave room for best lap
    }

    // Add recent laps in reverse order (oldest to newest, so HUD grows downward)
    // Handle case where lapLog is nullptr (no data yet) - will fill with placeholders below
    int lapLogSize = lapLog ? static_cast<int>(lapLog->size()) : 0;
    int numRecentLaps = (maxRecentLaps < lapLogSize) ? maxRecentLaps : lapLogSize;
    for (int i = numRecentLaps - 1; i >= 0; i--) {
        displayList.push_back(DisplayEntry(i));
    }

    // Calculate actual height based on number of rows to display
    int numDataRows = static_cast<int>(displayList.size());
    if (numDataRows < m_maxDisplayLaps) {
        // Fill remaining rows with placeholders
        for (int i = numDataRows; i < m_maxDisplayLaps; i++) {
            displayList.push_back(DisplayEntry(-2));  // -2 = placeholder
        }
        numDataRows = m_maxDisplayLaps;
    }

    // Cache for rebuildLayout to use
    m_cachedNumDataRows = numDataRows;

    int widthChars = getBackgroundWidthChars();
    float backgroundWidth = PluginUtils::calculateMonospaceTextWidth(widthChars, dim.fontSize)
        + dim.paddingH + dim.paddingH;
    // Title (large if shown) + data rows (normal)
    float titleHeight = m_bShowTitle ? dim.lineHeightLarge : 0.0f;
    float backgroundHeight = dim.paddingV + titleHeight + (dim.lineHeightNormal * numDataRows) + dim.paddingV;

    setBounds(START_X, START_Y, START_X + backgroundWidth, START_Y + backgroundHeight);
    addBackgroundQuad(START_X, START_Y, backgroundWidth, backgroundHeight);

    float contentStartX = START_X + dim.paddingH;
    // Title at top (if shown), data flows downward
    float currentY = START_Y + dim.paddingV;

    // Recalculate column positions for current scale
    m_columns = ColumnPositions(contentStartX, m_fScale, m_enabledColumns);

    // Get best sector times from cached ideal lap data (performance optimization)
    // Previously recalculated from all laps on every rebuild - now use cached values
    const IdealLapData* idealLapData = data.getIdealLapData();
    int bestSector1 = idealLapData ? idealLapData->bestSector1 : -1;
    int bestSector2 = idealLapData ? idealLapData->bestSector2 : -1;
    int bestSector3 = idealLapData ? idealLapData->bestSector3 : -1;

    // Best lap time: use the separately-stored best lap entry if available
    int bestLapTime = (bestLapEntry && bestLapEntry->isComplete) ? bestLapEntry->lapTime : -1;

    // Get color configuration
    const ColorConfig& colors = ColorConfig::getInstance();

    // Render title at TOP (if shown)
    addTitleString("Lap Log", contentStartX, currentY, Justify::LEFT,
        Fonts::getTitle(), colors.getPrimary(), dim.fontSizeLarge);
    currentY += titleHeight;

    // Render data rows from top to bottom (current lap, best lap, then oldest to newest)
    for (int displayIdx = 0; displayIdx < static_cast<int>(displayList.size()); displayIdx++) {
        const DisplayEntry& displayEntry = displayList[displayIdx];

        char lapStr[8];
        char s1Str[16];
        char s2Str[16];
        char s3Str[16];
        char timeStr[16];

        // Handle current lap in progress (live timing row)
        if (displayEntry.historyIndex == -4) {
            // Current lap in progress - show live timing
            int currentLapNum = data.getLapTimerCurrentLap();
            int activeSector = getCurrentActiveSector();

            // Lap number (1-based display)
            snprintf(lapStr, sizeof(lapStr), "L%d", currentLapNum + 1);

            // Get official split times from currentLap data (if available)
            int officialS1 = (currentLap && currentLap->split1 > 0) ? currentLap->split1 : -1;
            int officialS2 = -1;
            if (currentLap && currentLap->split2 > 0 && currentLap->split1 > 0) {
                officialS2 = currentLap->split2 - currentLap->split1;
            }

            // Format S1: official time if crossed, else live elapsed if in S1
            if (officialS1 > 0) {
                PluginUtils::formatLapTime(officialS1, s1Str, sizeof(s1Str));
            } else if (activeSector == 0) {
                int elapsed = data.getElapsedSectorTime(0);
                if (elapsed > 0) {
                    PluginUtils::formatLapTime(elapsed, s1Str, sizeof(s1Str));
                } else {
                    strcpy_s(s1Str, sizeof(s1Str), Placeholders::GENERIC);
                }
            } else {
                strcpy_s(s1Str, sizeof(s1Str), Placeholders::GENERIC);
            }

            // Format S2: official time if crossed, else live elapsed if in S2
            if (officialS2 > 0) {
                PluginUtils::formatLapTime(officialS2, s2Str, sizeof(s2Str));
            } else if (activeSector == 1) {
                int elapsed = data.getElapsedSectorTime(1);
                if (elapsed > 0) {
                    PluginUtils::formatLapTime(elapsed, s2Str, sizeof(s2Str));
                } else {
                    strcpy_s(s2Str, sizeof(s2Str), Placeholders::GENERIC);
                }
            } else {
                strcpy_s(s2Str, sizeof(s2Str), Placeholders::GENERIC);
            }

            // Format S3: live elapsed if in S3, else placeholder
            if (activeSector == 2) {
                int elapsed = data.getElapsedSectorTime(2);
                if (elapsed > 0) {
                    PluginUtils::formatLapTime(elapsed, s3Str, sizeof(s3Str));
                } else {
                    strcpy_s(s3Str, sizeof(s3Str), Placeholders::GENERIC);
                }
            } else {
                strcpy_s(s3Str, sizeof(s3Str), Placeholders::GENERIC);
            }

            // Format lap time: live elapsed time
            int elapsedLapTime = data.getElapsedLapTime();
            if (elapsedLapTime > 0) {
                PluginUtils::formatLapTime(elapsedLapTime, timeStr, sizeof(timeStr));
            } else {
                strcpy_s(timeStr, sizeof(timeStr), Placeholders::LAP_TIME);
            }

            // Colors for live timing: use muted color for ticking values, primary for official
            unsigned long colorLap = colors.getSecondary();
            unsigned long colorS1 = (officialS1 > 0) ? colors.getPrimary() : colors.getMuted();
            unsigned long colorS2 = (officialS2 > 0) ? colors.getPrimary() : colors.getMuted();
            unsigned long colorS3 = colors.getMuted();  // S3 is always in progress or placeholder

            // Color lap time based on live gap (green = on pace or ahead, red = behind PB)
            unsigned long colorTime = colors.getMuted();  // Default when no valid gap
            if (data.hasValidLiveGap()) {
                int liveGap = data.getLiveGap();
                if (liveGap <= 0) {
                    colorTime = colors.getPositive();  // On pace or ahead (green)
                } else {
                    colorTime = colors.getNegative();  // Behind PB (red)
                }
            }

            addString((m_enabledColumns & COL_LAP) ? lapStr : "", m_columns.lap, currentY, Justify::LEFT, Fonts::getNormal(), colorLap, dim.fontSize);
            addString((m_enabledColumns & COL_S1) ? s1Str : "", m_columns.s1, currentY, Justify::LEFT, Fonts::getNormal(), colorS1, dim.fontSize);
            addString((m_enabledColumns & COL_S2) ? s2Str : "", m_columns.s2, currentY, Justify::LEFT, Fonts::getNormal(), colorS2, dim.fontSize);
            addString((m_enabledColumns & COL_S3) ? s3Str : "", m_columns.s3, currentY, Justify::LEFT, Fonts::getNormal(), colorS3, dim.fontSize);
            addString((m_enabledColumns & COL_TIME) ? timeStr : "", m_columns.time, currentY, Justify::LEFT, Fonts::getNormal(), colorTime, dim.fontSize);

            currentY += dim.lineHeightNormal;
            continue;
        }

        // Determine which entry to display
        const LapLogEntry* entryPtr = nullptr;
        if (displayEntry.historyIndex == -3 && bestLapEntry) {
            // Use best lap entry (stored separately)
            entryPtr = bestLapEntry;
        } else if (lapLog && displayEntry.historyIndex >= 0 && displayEntry.historyIndex < static_cast<int>(lapLog->size())) {
            // Use entry from history
            entryPtr = &(*lapLog)[displayEntry.historyIndex];
        }

        if (entryPtr) {
            const LapLogEntry& entry = *entryPtr;

            // Lap number with "L" prefix (display as 1-based for consistency with other HUDs)
            snprintf(lapStr, sizeof(lapStr), "L%d", entry.lapNum + 1);

            // Format sector times using central formatting (M:SS.mmm)
            if (entry.sector1 > 0) {
                PluginUtils::formatLapTime(entry.sector1, s1Str, sizeof(s1Str));
            } else {
                strcpy_s(s1Str, sizeof(s1Str), Placeholders::GENERIC);
            }

            if (entry.sector2 > 0) {
                PluginUtils::formatLapTime(entry.sector2, s2Str, sizeof(s2Str));
            } else {
                strcpy_s(s2Str, sizeof(s2Str), Placeholders::GENERIC);
            }

            if (entry.sector3 > 0) {
                PluginUtils::formatLapTime(entry.sector3, s3Str, sizeof(s3Str));
            } else {
                strcpy_s(s3Str, sizeof(s3Str), Placeholders::GENERIC);
            }

            // Format lap time
            if (entry.lapTime > 0 && entry.isComplete) {
                PluginUtils::formatLapTime(entry.lapTime, timeStr, sizeof(timeStr));
            } else {
                strcpy_s(timeStr, sizeof(timeStr), Placeholders::LAP_TIME);
            }

            // Determine colors and fonts
            // Invalid laps (track cuts in race mode) show muted times
            unsigned long colorLap = colors.getSecondary();  // Lap number always secondary
            unsigned long colorS1, colorS2, colorS3, colorTime;
            int fontLapTime;

            // For invalid laps, show all timing data as muted
            // For valid laps, highlight PBs in green, others in primary
            if (!entry.isValid || entry.sector1 <= 0) {
                colorS1 = colors.getMuted();
            } else {
                colorS1 = (entry.sector1 == bestSector1) ? colors.getPositive() : colors.getPrimary();
            }

            if (!entry.isValid || entry.sector2 <= 0) {
                colorS2 = colors.getMuted();
            } else {
                colorS2 = (entry.sector2 == bestSector2) ? colors.getPositive() : colors.getPrimary();
            }

            if (!entry.isValid || entry.sector3 <= 0) {
                colorS3 = colors.getMuted();
            } else {
                colorS3 = (entry.sector3 == bestSector3) ? colors.getPositive() : colors.getPrimary();
            }

            bool hasLapTime = (entry.lapTime > 0 && entry.isComplete);
            if (!entry.isValid || !hasLapTime) {
                colorTime = colors.getMuted();
                fontLapTime = Fonts::getNormal();
            } else {
                colorTime = (entry.lapTime == bestLapTime) ? colors.getPositive() : colors.getPrimary();
                fontLapTime = Fonts::getStrong();
            }

            // Always add all NUM_COLUMNS strings for index consistency (use empty string if column disabled)
            addString((m_enabledColumns & COL_LAP) ? lapStr : "", m_columns.lap, currentY, Justify::LEFT, Fonts::getNormal(), colorLap, dim.fontSize);
            addString((m_enabledColumns & COL_S1) ? s1Str : "", m_columns.s1, currentY, Justify::LEFT, Fonts::getNormal(), colorS1, dim.fontSize);
            addString((m_enabledColumns & COL_S2) ? s2Str : "", m_columns.s2, currentY, Justify::LEFT, Fonts::getNormal(), colorS2, dim.fontSize);
            addString((m_enabledColumns & COL_S3) ? s3Str : "", m_columns.s3, currentY, Justify::LEFT, Fonts::getNormal(), colorS3, dim.fontSize);
            addString((m_enabledColumns & COL_TIME) ? timeStr : "", m_columns.time, currentY, Justify::LEFT, fontLapTime, colorTime, dim.fontSize);
        } else {
            // Placeholder row - always add all NUM_COLUMNS strings for index consistency
            strcpy_s(lapStr, sizeof(lapStr), Placeholders::GENERIC);
            strcpy_s(s1Str, sizeof(s1Str), Placeholders::GENERIC);
            strcpy_s(s2Str, sizeof(s2Str), Placeholders::GENERIC);
            strcpy_s(s3Str, sizeof(s3Str), Placeholders::GENERIC);
            strcpy_s(timeStr, sizeof(timeStr), Placeholders::LAP_TIME);

            addString((m_enabledColumns & COL_LAP) ? lapStr : "", m_columns.lap, currentY, Justify::LEFT, Fonts::getNormal(), colors.getMuted(), dim.fontSize);
            addString((m_enabledColumns & COL_S1) ? s1Str : "", m_columns.s1, currentY, Justify::LEFT, Fonts::getNormal(), colors.getMuted(), dim.fontSize);
            addString((m_enabledColumns & COL_S2) ? s2Str : "", m_columns.s2, currentY, Justify::LEFT, Fonts::getNormal(), colors.getMuted(), dim.fontSize);
            addString((m_enabledColumns & COL_S3) ? s3Str : "", m_columns.s3, currentY, Justify::LEFT, Fonts::getNormal(), colors.getMuted(), dim.fontSize);
            addString((m_enabledColumns & COL_TIME) ? timeStr : "", m_columns.time, currentY, Justify::LEFT, Fonts::getNormal(), colors.getMuted(), dim.fontSize);
        }

        currentY += dim.lineHeightNormal;  // Move down to next row
    }
}

void LapLogHud::resetToDefaults() {
    m_bVisible = true;
    m_bShowTitle = true;
    setTextureVariant(0);  // No texture by default
    m_fBackgroundOpacity = SettingsLimits::DEFAULT_OPACITY;
    m_fScale = 1.0f;
    setPosition(0.0055f, 0.7659f);
    m_enabledColumns = COL_DEFAULT;
    m_maxDisplayLaps = 6;
    m_showLiveTiming = true;
    setDataDirty();
}
