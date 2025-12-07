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
    DEBUG_INFO("LapLogHud created");
    setDraggable(true);

    // Set defaults to match user configuration
    m_bShowTitle = true;
    m_fBackgroundOpacity = SettingsLimits::DEFAULT_OPACITY;
    setPosition(-0.0055f, -0.0111f);

    // Pre-allocate vectors
    m_quads.reserve(1);
    m_strings.reserve(1 + m_maxDisplayLaps * NUM_COLUMNS);  // Title + data rows (NUM_COLUMNS strings per row)

    rebuildRenderData();
}

bool LapLogHud::handlesDataType(DataChangeType dataType) const {
    return (dataType == DataChangeType::LapLog ||
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

    const std::vector<LapLogEntry>* lapLog = data.getLapLog();

    // Apply scale to all dimensions
    auto dim = getScaledDimensions();

    // Build display list: best lap first (if not in recent), then recent laps oldest to newest
    // This ensures the best lap is always visible at the top and HUD grows downward
    struct DisplayEntry {
        int historyIndex;  // Index into lapLog vector (-3 for best lap, -2 for placeholder)
        DisplayEntry(int idx) : historyIndex(idx) {}
    };
    std::vector<DisplayEntry> displayList;

    // Get the best lap entry (stored separately)
    const LapLogEntry* bestLapEntry = data.getBestLapEntry();

    // Check if best lap is in the recent history (first N laps)
    bool bestLapInRecent = false;
    if (bestLapEntry && lapLog) {
        for (int i = 0; i < m_maxDisplayLaps && i < static_cast<int>(lapLog->size()); i++) {
            if ((*lapLog)[i].lapNum == bestLapEntry->lapNum) {
                bestLapInRecent = true;
                break;
            }
        }
    }

    // If best lap is not in the recent window, add it first
    int maxRecentLaps = m_maxDisplayLaps;
    if (bestLapEntry && !bestLapInRecent) {
        displayList.push_back(DisplayEntry(-3));  // -3 = use bestLapEntry
        maxRecentLaps = m_maxDisplayLaps - 1;  // Leave room for best lap
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

    // Get best sector times from cached session data (performance optimization)
    // Previously recalculated from all laps on every rebuild - now use cached values
    const SessionBestData* sessionBest = data.getSessionBestData();
    int bestSector1 = sessionBest ? sessionBest->bestSector1 : -1;
    int bestSector2 = sessionBest ? sessionBest->bestSector2 : -1;
    int bestSector3 = sessionBest ? sessionBest->bestSector3 : -1;

    // Best lap time: use the separately-stored best lap entry if available
    int bestLapTime = (bestLapEntry && bestLapEntry->isComplete) ? bestLapEntry->lapTime : -1;

    // Get color configuration
    const ColorConfig& colors = ColorConfig::getInstance();

    // Render title at TOP (if shown)
    addTitleString("Lap Log", contentStartX, currentY, Justify::LEFT,
        Fonts::ENTER_SANSMAN, colors.getPrimary(), dim.fontSizeLarge);
    currentY += titleHeight;

    // Render data rows from top to bottom (best lap first, then oldest to newest)
    for (int displayIdx = 0; displayIdx < static_cast<int>(displayList.size()); displayIdx++) {
        const DisplayEntry& displayEntry = displayList[displayIdx];

        char lapStr[8];
        char s1Str[16];
        char s2Str[16];
        char s3Str[16];
        char timeStr[16];

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
                fontLapTime = Fonts::ROBOTO_MONO;
            } else {
                colorTime = (entry.lapTime == bestLapTime) ? colors.getPositive() : colors.getPrimary();
                fontLapTime = Fonts::ROBOTO_MONO_BOLD;
            }

            // Always add all NUM_COLUMNS strings for index consistency (use empty string if column disabled)
            addString((m_enabledColumns & COL_LAP) ? lapStr : "", m_columns.lap, currentY, Justify::LEFT, Fonts::ROBOTO_MONO, colorLap, dim.fontSize);
            addString((m_enabledColumns & COL_S1) ? s1Str : "", m_columns.s1, currentY, Justify::LEFT, Fonts::ROBOTO_MONO, colorS1, dim.fontSize);
            addString((m_enabledColumns & COL_S2) ? s2Str : "", m_columns.s2, currentY, Justify::LEFT, Fonts::ROBOTO_MONO, colorS2, dim.fontSize);
            addString((m_enabledColumns & COL_S3) ? s3Str : "", m_columns.s3, currentY, Justify::LEFT, Fonts::ROBOTO_MONO, colorS3, dim.fontSize);
            addString((m_enabledColumns & COL_TIME) ? timeStr : "", m_columns.time, currentY, Justify::LEFT, fontLapTime, colorTime, dim.fontSize);
        } else {
            // Placeholder row - always add all NUM_COLUMNS strings for index consistency
            strcpy_s(lapStr, sizeof(lapStr), Placeholders::GENERIC);
            strcpy_s(s1Str, sizeof(s1Str), Placeholders::GENERIC);
            strcpy_s(s2Str, sizeof(s2Str), Placeholders::GENERIC);
            strcpy_s(s3Str, sizeof(s3Str), Placeholders::GENERIC);
            strcpy_s(timeStr, sizeof(timeStr), Placeholders::LAP_TIME);

            addString((m_enabledColumns & COL_LAP) ? lapStr : "", m_columns.lap, currentY, Justify::LEFT, Fonts::ROBOTO_MONO, colors.getMuted(), dim.fontSize);
            addString((m_enabledColumns & COL_S1) ? s1Str : "", m_columns.s1, currentY, Justify::LEFT, Fonts::ROBOTO_MONO, colors.getMuted(), dim.fontSize);
            addString((m_enabledColumns & COL_S2) ? s2Str : "", m_columns.s2, currentY, Justify::LEFT, Fonts::ROBOTO_MONO, colors.getMuted(), dim.fontSize);
            addString((m_enabledColumns & COL_S3) ? s3Str : "", m_columns.s3, currentY, Justify::LEFT, Fonts::ROBOTO_MONO, colors.getMuted(), dim.fontSize);
            addString((m_enabledColumns & COL_TIME) ? timeStr : "", m_columns.time, currentY, Justify::LEFT, Fonts::ROBOTO_MONO, colors.getMuted(), dim.fontSize);
        }

        currentY += dim.lineHeightNormal;  // Move down to next row
    }
}

void LapLogHud::resetToDefaults() {
    m_bVisible = true;
    m_bShowTitle = true;
    m_bShowBackgroundTexture = false;  // No texture by default
    m_fBackgroundOpacity = SettingsLimits::DEFAULT_OPACITY;
    m_fScale = 1.0f;
    setPosition(-0.0055f, -0.0111f);
    m_enabledColumns = COL_DEFAULT;
    m_maxDisplayLaps = 6;
    setDataDirty();
}
