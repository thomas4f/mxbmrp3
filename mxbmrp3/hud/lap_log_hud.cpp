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
    bool showSectors = (enabledColumns & COL_SECTORS) != 0;

    // Lap column (always shown)
    lap = current;
    current += PluginUtils::calculateMonospaceTextWidth(COL_LAP_WIDTH, scaledFontSize);

    // Sector columns (optional, toggled together)
    if (showSectors) {
        s1 = current;
        current += PluginUtils::calculateMonospaceTextWidth(COL_TIME_WIDTH, scaledFontSize);
        s2 = current;
        current += PluginUtils::calculateMonospaceTextWidth(COL_TIME_WIDTH, scaledFontSize);
        s3 = current;
        current += PluginUtils::calculateMonospaceTextWidth(COL_TIME_WIDTH, scaledFontSize);
    } else {
        s1 = s2 = s3 = -1.0f;  // Not shown
    }

    // Time column (always shown)
    time = current;
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
    int width = COL_LAP_WIDTH;  // Lap column (always shown)
    if (m_enabledColumns & COL_SECTORS) {
        width += COL_TIME_WIDTH * 3;  // S1, S2, S3
    }
    width += COL_LAST_TIME_WIDTH;  // Time column (always shown, no trailing gap)
    return width;
}

void LapLogHud::update() {
    // OPTIMIZATION: Skip processing when not visible
    if (!isVisible()) {
        clearDataDirty();
        clearLayoutDirty();
        return;
    }

    // Check if we need frequent updates for ticking timer (uses BaseHud helper)
    checkFrequentUpdates();

    // Handle dirty flags using base class helper
    processDirtyFlags();
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
    clearStrings();
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

    // Build display list: order depends on m_displayOrder setting
    // Special indices: -5 = gap row, -4 = current lap, -3 = best lap, -2 = placeholder
    struct DisplayEntry {
        int historyIndex;
        DisplayEntry(int idx) : historyIndex(idx) {}
    };
    std::vector<DisplayEntry> displayList;

    // Get the best lap entry (stored separately)
    const LapLogEntry* bestLapEntry = data.getBestLapEntry();

    // Calculate how many slots are available for recent laps
    int maxRecentLaps = m_maxDisplayLaps;
    if (showCurrentLapRow) {
        maxRecentLaps--;  // Reserve slot for current lap
    }

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

    // If best lap is not in the recent window, reserve a slot for it
    bool showBestLapSeparately = bestLapEntry && !bestLapInRecent;
    if (showBestLapSeparately) {
        maxRecentLaps--;  // Reserve slot for best lap
    }

    // Calculate number of recent laps to display
    int lapLogSize = lapLog ? static_cast<int>(lapLog->size()) : 0;
    int numRecentLaps = (maxRecentLaps < lapLogSize) ? maxRecentLaps : lapLogSize;

    // Show gap row when enabled AND live timing is on (gap data requires live timing)
    bool showGapRow = m_showGapRow && m_showLiveTiming;

    // Calculate how many placeholder rows we need to fill the configured size
    // The HUD always shows m_maxDisplayLaps rows (plus gap row if enabled)
    int filledSlots = numRecentLaps + (showCurrentLapRow ? 1 : 0) + (showBestLapSeparately ? 1 : 0);
    int placeholderCount = m_maxDisplayLaps - filledSlots;
    if (placeholderCount < 0) placeholderCount = 0;

    // Build display list based on display order
    if (m_displayOrder == DisplayOrder::OLDEST_FIRST) {
        // OLDEST_FIRST: best lap (if separate) at top, placeholders, oldest->newest, current lap, gap row at bottom
        if (showBestLapSeparately) {
            displayList.push_back(DisplayEntry(-3));  // -3 = best lap at top
        }
        // Placeholders at top (after best lap)
        for (int i = 0; i < placeholderCount; i++) {
            displayList.push_back(DisplayEntry(-2));  // -2 = placeholder
        }
        // Add recent laps in reverse order (oldest to newest)
        for (int i = numRecentLaps - 1; i >= 0; i--) {
            displayList.push_back(DisplayEntry(i));
        }
        if (showCurrentLapRow) {
            displayList.push_back(DisplayEntry(-4));  // -4 = current lap
        }
        if (showGapRow) {
            displayList.push_back(DisplayEntry(-5));  // -5 = gap row at bottom edge
        }
    } else {
        // NEWEST_FIRST: gap row at top edge, current lap, newest->oldest, placeholders, best lap (if separate) at bottom
        if (showGapRow) {
            displayList.push_back(DisplayEntry(-5));  // -5 = gap row at top edge
        }
        if (showCurrentLapRow) {
            displayList.push_back(DisplayEntry(-4));  // -4 = current lap
        }
        // Add recent laps in order (newest to oldest)
        for (int i = 0; i < numRecentLaps; i++) {
            displayList.push_back(DisplayEntry(i));
        }
        // Placeholders at bottom (before best lap)
        for (int i = 0; i < placeholderCount; i++) {
            displayList.push_back(DisplayEntry(-2));  // -2 = placeholder
        }
        if (showBestLapSeparately) {
            displayList.push_back(DisplayEntry(-3));  // -3 = best lap at bottom
        }
    }

    // Calculate height: m_maxDisplayLaps rows plus gap row if enabled
    int numDataRows = m_maxDisplayLaps + (showGapRow ? 1 : 0);

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

    // Check if sectors are enabled
    bool showSectors = (m_enabledColumns & COL_SECTORS) != 0;

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
            unsigned long colorTime = colors.getMuted();  // Lap time uses muted color (gap shown separately)

            addString(lapStr, m_columns.lap, currentY, Justify::LEFT, Fonts::getNormal(), colorLap, dim.fontSize);
            addString(showSectors ? s1Str : "", m_columns.s1, currentY, Justify::LEFT, Fonts::getNormal(), colorS1, dim.fontSize);
            addString(showSectors ? s2Str : "", m_columns.s2, currentY, Justify::LEFT, Fonts::getNormal(), colorS2, dim.fontSize);
            addString(showSectors ? s3Str : "", m_columns.s3, currentY, Justify::LEFT, Fonts::getNormal(), colorS3, dim.fontSize);
            addString(timeStr, m_columns.time, currentY, Justify::LEFT, Fonts::getNormal(), colorTime, dim.fontSize);

            currentY += dim.lineHeightNormal;
            continue;
        }

        // Handle gap row (shows live gap to PB, colorized)
        if (displayEntry.historyIndex == -5) {
            char gapStr[32];
            unsigned long gapColor = colors.getMuted();
            float gapX = m_columns.time;  // Default position aligned with time column

            if (data.hasValidLiveGap()) {
                int liveGap = data.getLiveGap();
                PluginUtils::formatTimeDiff(gapStr, sizeof(gapStr), liveGap);
                if (liveGap > 0) {
                    gapColor = colors.getNegative();  // Behind PB (red)
                } else if (liveGap < 0) {
                    gapColor = colors.getPositive();  // Ahead of PB (green)
                }
                // Offset by one char width so +/- sign is outside column and numbers align with lap times
                gapX -= PluginUtils::calculateMonospaceTextWidth(1, dim.fontSize);
            } else {
                strcpy_s(gapStr, sizeof(gapStr), Placeholders::GENERIC);
                // No offset for placeholder - align with time column
            }

            // Gap row: empty columns except for time column showing the gap
            addString("", m_columns.lap, currentY, Justify::LEFT, Fonts::getNormal(), colors.getMuted(), dim.fontSize);
            addString("", m_columns.s1, currentY, Justify::LEFT, Fonts::getNormal(), colors.getMuted(), dim.fontSize);
            addString("", m_columns.s2, currentY, Justify::LEFT, Fonts::getNormal(), colors.getMuted(), dim.fontSize);
            addString("", m_columns.s3, currentY, Justify::LEFT, Fonts::getNormal(), colors.getMuted(), dim.fontSize);
            addString(gapStr, gapX, currentY, Justify::LEFT, Fonts::getNormal(), gapColor, dim.fontSize);

            currentY += dim.lineHeightNormal;
            continue;
        }

        // Handle placeholder row (shows placeholders in all columns)
        if (displayEntry.historyIndex == -2) {
            addString(Placeholders::GENERIC, m_columns.lap, currentY, Justify::LEFT, Fonts::getNormal(), colors.getMuted(), dim.fontSize);
            addString(showSectors ? Placeholders::GENERIC : "", m_columns.s1, currentY, Justify::LEFT, Fonts::getNormal(), colors.getMuted(), dim.fontSize);
            addString(showSectors ? Placeholders::GENERIC : "", m_columns.s2, currentY, Justify::LEFT, Fonts::getNormal(), colors.getMuted(), dim.fontSize);
            addString(showSectors ? Placeholders::GENERIC : "", m_columns.s3, currentY, Justify::LEFT, Fonts::getNormal(), colors.getMuted(), dim.fontSize);
            addString(Placeholders::LAP_TIME, m_columns.time, currentY, Justify::LEFT, Fonts::getNormal(), colors.getMuted(), dim.fontSize);
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

            // Render lap data row
            addString(lapStr, m_columns.lap, currentY, Justify::LEFT, Fonts::getNormal(), colorLap, dim.fontSize);
            addString(showSectors ? s1Str : "", m_columns.s1, currentY, Justify::LEFT, Fonts::getNormal(), colorS1, dim.fontSize);
            addString(showSectors ? s2Str : "", m_columns.s2, currentY, Justify::LEFT, Fonts::getNormal(), colorS2, dim.fontSize);
            addString(showSectors ? s3Str : "", m_columns.s3, currentY, Justify::LEFT, Fonts::getNormal(), colorS3, dim.fontSize);
            addString(timeStr, m_columns.time, currentY, Justify::LEFT, fontLapTime, colorTime, dim.fontSize);
        } else {
            // Placeholder row (entry not found)
            addString(Placeholders::GENERIC, m_columns.lap, currentY, Justify::LEFT, Fonts::getNormal(), colors.getMuted(), dim.fontSize);
            addString(showSectors ? Placeholders::GENERIC : "", m_columns.s1, currentY, Justify::LEFT, Fonts::getNormal(), colors.getMuted(), dim.fontSize);
            addString(showSectors ? Placeholders::GENERIC : "", m_columns.s2, currentY, Justify::LEFT, Fonts::getNormal(), colors.getMuted(), dim.fontSize);
            addString(showSectors ? Placeholders::GENERIC : "", m_columns.s3, currentY, Justify::LEFT, Fonts::getNormal(), colors.getMuted(), dim.fontSize);
            addString(Placeholders::LAP_TIME, m_columns.time, currentY, Justify::LEFT, Fonts::getNormal(), colors.getMuted(), dim.fontSize);
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
    m_maxDisplayLaps = 5;
    m_showLiveTiming = true;
    m_showGapRow = true;
    m_displayOrder = DisplayOrder::OLDEST_FIRST;
    setDataDirty();
}
