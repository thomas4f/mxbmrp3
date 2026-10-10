// ============================================================================
// hud/lap_log_hud.cpp
// Lap Log - displays recent lap times with sector splits and personal best
// ============================================================================
#include "lap_log_hud.h"
#include "../core/layout_config.h"
#include "../diagnostics/logger.h"
#include "../diagnostics/timer.h"
#include "../core/plugin_utils.h"
#include "../core/plugin_constants.h"
#include "../core/plugin_data.h"
#include "../core/color_config.h"
#include "../game/game_config.h"
#include <cstring>
#include <cstdio>

using namespace PluginConstants;

// Compile-time check: Display limit must not exceed storage capacity
static_assert(LapLogHud::MAX_DISPLAY_LAPS <= HudLimits::MAX_LAP_LOG_CAPACITY,
              "MAX_DISPLAY_LAPS cannot exceed MAX_LAP_LOG_CAPACITY");

LapLogHud::ColumnPositions::ColumnPositions(float contentStartX, float scale, uint32_t enabledColumns) {
    float scaledFontSize = layoutDefaults().fontSizeNormal * scale;
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
#if GAME_SECTOR_COUNT >= 4
        s4 = current;
        current += PluginUtils::calculateMonospaceTextWidth(COL_TIME_WIDTH, scaledFontSize);
#else
        s4 = -1.0f;  // Not used in 3-sector games
#endif
    } else {
        s1 = s2 = s3 = s4 = -1.0f;  // Not shown
    }

    // Time column (always shown)
    time = current;
}

LapLogHud::LapLogHud()
    : m_columns(START_X + layoutDefaults().panelPaddingX, m_fScale, m_enabledColumns)
{
    // One-time setup
    // Body card: this HUD's content is a block the theme can frame -- exactly what
    // a themed body card is for. Opt-in; see BaseHud::m_bContentCard.
    m_bContentCard = true;
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
        width += COL_TIME_WIDTH * GAME_SECTOR_COUNT;  // S1, S2, S3 (+ S4 for 4-sector games)
    }
    width += COL_LAST_TIME_WIDTH;  // Time column (always shown, no trailing gap)
    return width;
}

void LapLogHud::update() {
    // The gap row's freeze tracks crossings while hidden too (see
    // official_gap_freeze.h); only the rebuild is gated on visibility.
    if (m_gapFreeze.update(gapReference(), HudDefaults::freezeMs(m_freezeDurationMs)) && isVisibleAnySurface()) {
        setDataDirty();
    }

    // OPTIMIZATION: Skip processing when not visible
    if (!isVisibleAnySurface()) {
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
    if (!isVisibleAnySurface()) return false;   // tick for live timing if shown on either surface

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
    // Right-aligned numeric columns and the optional header row make a coordinated
    // position-only fast path error-prone, so rebuild everything (cheap here; strings
    // bake their offset in at creation). Matches RecordsHud.
    rebuildRenderData();
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
    // Don't show live timing if rider has finished (timer is meaningless after checkered flag).
    // A lap that has been through the pits keeps its row, saying PIT, from pit entry to the
    // line that closes it: the timer is paused, then dropped at pit exit, so without this
    // the row froze and only the line revealed the lap as a pit lap.
    const bool pitLap = data.isLapViaPits(data.getDisplayRaceNum());
    bool showCurrentLapRow = m_showLiveTiming && (data.isLapTimerValid() || pitLap) &&
                             !data.isDisplayRiderFinished();

    // Get the best lap entry (stored separately)
    const LapLogEntry* bestLapEntry = data.getBestLapEntry();

    // Show gap row when enabled AND live timing is on (gap data requires live timing)
    bool showGapRow = m_showGapRow && m_showLiveTiming;

    computeRowPlan(lapLog, bestLapEntry, showCurrentLapRow, showGapRow);
    const std::vector<int>& displayList = m_plan.rows;

    // Calculate height: m_maxDisplayLaps rows plus gap row if enabled
    int numDataRows = m_plan.dataRowCount;

    // BOX-MODEL: optional header row + data rows as one section; the caption
    // band is the plan's.
    float headerHeight = m_bShowHeaders ? dim.lineHeightNormal : 0.0f;
    PanelWant want;
    want.contentW = PluginUtils::calculateMonospaceTextWidth(getBackgroundWidthChars(), dim.fontSize);
    want.sectionH = { headerHeight + dim.lineHeightNormal * numDataRows };
    want.captionW = planTitleWidth(dim, "Lap Log", TitleTier::Large);
    want.tier = TitleTier::Large;
    PanelPlan& plan = planPanel(dim, want);
    setBounds(START_X, START_Y, START_X + plan.width(), START_Y + plan.height());
    addPlanBackground(plan, START_X, START_Y);

    float contentStartX = plan.contentX();
    float currentY = plan.contentY();

    // Recalculate column positions for current scale
    m_columns = ColumnPositions(contentStartX, m_fScale, m_enabledColumns);

    const LapColumnX cx = computeColumnAnchors(dim);
    const BestTimes best = resolveBestTimes(data, bestLapEntry);

    // Render title at TOP (if shown) — the plan's caption row, above currentY.
    addPlanTitle(plan, "Lap Log",
                 this->getColor(ColorSlot::PRIMARY));

    // Optional column-header row (Strong font), numeric columns follow their right-aligned data
    if (m_bShowHeaders) {
        addHeaderRow(cx, currentY, dim);
        currentY += dim.lineHeightNormal;
    }

    // Render data rows from top to bottom (current lap, best lap, then oldest to newest)
    for (int displayIdx = 0; displayIdx < static_cast<int>(displayList.size()); displayIdx++) {
        const int rowKind = displayList[displayIdx];

        if (rowKind == LapLogPlan::kCurrentLap) {
            // Current lap in progress (live timing row)
            addCurrentLapRow(cx, data, currentLap, pitLap, currentY, dim);
        } else if (rowKind == LapLogPlan::kGap) {
            // Gap row (shows live gap to PB, colorized)
            addGapRow(cx, currentY, dim);
        } else if (rowKind == LapLogPlan::kPlaceholder) {
            // Handle placeholder row - show muted dash placeholders so empty slots read as
            // a table awaiting data (filled in as laps complete) rather than blank rows.
            addPlaceholderRow(cx, currentY, dim);
        } else {
            // Determine which entry to display
            const LapLogEntry* entryPtr = nullptr;
            if (rowKind == LapLogPlan::kBestLap && bestLapEntry) {
                // Use best lap entry (stored separately)
                entryPtr = bestLapEntry;
            } else if (lapLog && rowKind >= 0 && rowKind < static_cast<int>(lapLog->size())) {
                // Use entry from history
                entryPtr = &(*lapLog)[rowKind];
            }

            if (entryPtr) {
                addLapEntryRow(cx, best, *entryPtr, currentY, dim);
            } else {
                // Entry not found - show muted dash placeholders (same as the -2 placeholder row)
                addPlaceholderRow(cx, currentY, dim);
            }
        }

        currentY += dim.lineHeightNormal;  // Move down to next row
    }
}

// Which rows to draw and in what order (fills m_plan).
void LapLogHud::computeRowPlan(const std::deque<LapLogEntry>* lapLog, const LapLogEntry* bestLapEntry,
                               bool showCurrentLapRow, bool showGapRow) {
    // Which rows to draw and in what order — pure slot arithmetic, unit-tested in
    // tests/unit/test_lap_log_plan.cpp. The lap numbers go in as a flat array so
    // the planner needs nothing from PluginData.
    m_planLapNums.clear();
    if (lapLog) {
        m_planLapNums.reserve(lapLog->size());
        for (const LapLogEntry& e : *lapLog) m_planLapNums.push_back(e.lapNum);
    }

    LapLogPlan::Input planIn;
    planIn.lapLogSize = static_cast<int>(m_planLapNums.size());
    planIn.maxDisplayLaps = m_maxDisplayLaps;
    planIn.lapNums = m_planLapNums.empty() ? nullptr : m_planLapNums.data();
    planIn.lapNumCount = static_cast<int>(m_planLapNums.size());
    planIn.hasBestLap = (bestLapEntry != nullptr);
    planIn.bestLapNum = bestLapEntry ? bestLapEntry->lapNum : -1;
    planIn.showCurrentLap = showCurrentLapRow;
    planIn.showGapRow = showGapRow;
    planIn.order = (m_displayOrder == DisplayOrder::OLDEST_FIRST)
        ? LapLogPlan::Order::OLDEST_FIRST
        : LapLogPlan::Order::NEWEST_FIRST;

    LapLogPlan::compute(planIn, m_plan);
}

LapLogHud::LapColumnX LapLogHud::computeColumnAnchors(const ScaledDimensions& dim) const {
    LapColumnX cx;
    // Right-edge anchors for the numeric columns. Times vary in width (compact format
    // drops the leading "0:" for sub-minute sectors), so right-aligning lines up the
    // decimals. Each column reserves COL_TIME_WIDTH chars (content + 1 gap); the time
    // column is the last one (no trailing gap). Content is 9 chars wide either way.
    float colRightOffset = PluginUtils::calculateMonospaceTextWidth(COL_TIME_WIDTH - 1, dim.fontSize);
    // Lap numbers reach double digits (L10+), so right-align them too (content is
    // COL_LAP_WIDTH - 1 chars wide, the rest is the trailing gap).
    cx.lapRightX = m_columns.lap + PluginUtils::calculateMonospaceTextWidth(COL_LAP_WIDTH - 1, dim.fontSize);
    cx.s1RightX = m_columns.s1 + colRightOffset;
    cx.s2RightX = m_columns.s2 + colRightOffset;
    cx.s3RightX = m_columns.s3 + colRightOffset;
#if GAME_SECTOR_COUNT >= 4
    cx.s4RightX = m_columns.s4 + colRightOffset;
#endif
    cx.timeRightX = m_columns.time + colRightOffset;

    // Check if sectors are enabled
    cx.showSectors = (m_enabledColumns & COL_SECTORS) != 0;
    return cx;
}

LapLogHud::BestTimes LapLogHud::resolveBestTimes(const PluginData& data, const LapLogEntry* bestLapEntry) const {
    BestTimes best;
    // Best sector times from cached ideal lap data, not recalculated from all laps
    // on every rebuild.
    const IdealLapData* idealLapData = data.getIdealLapData();
    best.bestSector1 = idealLapData ? idealLapData->bestSector1 : -1;
    best.bestSector2 = idealLapData ? idealLapData->bestSector2 : -1;
    best.bestSector3 = idealLapData ? idealLapData->bestSector3 : -1;
#if GAME_SECTOR_COUNT >= 4
    best.bestSector4 = idealLapData ? idealLapData->bestSector4 : -1;
#endif

    // Best lap time: use the separately-stored best lap entry if available
    best.bestLapTime = (bestLapEntry && bestLapEntry->isComplete) ? bestLapEntry->lapTime : -1;
    return best;
}

// Optional column-header row (Strong font), numeric columns follow their right-aligned data
void LapLogHud::addHeaderRow(const LapColumnX& cx, float currentY, const ScaledDimensions& dim) {
    unsigned long headerColor = this->getColor(ColorSlot::TERTIARY);
    addLabel("Lap", cx.lapRightX, currentY, Justify::RIGHT, headerColor, dim);
    addLabel(cx.showSectors ? "S1" : "", cx.s1RightX, currentY, Justify::RIGHT, headerColor, dim);
    addLabel(cx.showSectors ? "S2" : "", cx.s2RightX, currentY, Justify::RIGHT, headerColor, dim);
    addLabel(cx.showSectors ? "S3" : "", cx.s3RightX, currentY, Justify::RIGHT, headerColor, dim);
#if GAME_SECTOR_COUNT >= 4
    addLabel(cx.showSectors ? "S4" : "", cx.s4RightX, currentY, Justify::RIGHT, headerColor, dim);
#endif
    addLabel("Time", cx.timeRightX, currentY, Justify::RIGHT, headerColor, dim);
}

void LapLogHud::addCurrentLapRow(const LapColumnX& cx, const PluginData& data, const CurrentLapData* currentLap,
                                 bool pitLap, float currentY, const ScaledDimensions& dim) {
    char lapStr[8];
    char s1Str[16];
    char s2Str[16];
    char s3Str[16];
#if GAME_SECTOR_COUNT >= 4
    char s4Str[16];
#endif
    char timeStr[16];

    // Current lap in progress - show live timing. A pit lap has nothing live left:
    // the sectors crossed before the pits, then PIT where the time would tick.
    int currentLapNum = data.getLapTimerCurrentLap();
    int activeSector = pitLap ? -1 : getCurrentActiveSector();

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

#if GAME_SECTOR_COUNT >= 4
    // Format S4: live elapsed if in S4, else placeholder (4-sector games only)
    if (activeSector == 3) {
        int elapsed = data.getElapsedSectorTime(3);
        if (elapsed > 0) {
            PluginUtils::formatLapTime(elapsed, s4Str, sizeof(s4Str));
        } else {
            strcpy_s(s4Str, sizeof(s4Str), Placeholders::GENERIC);
        }
    } else {
        strcpy_s(s4Str, sizeof(s4Str), Placeholders::GENERIC);
    }
#endif

    // Format lap time: live elapsed time, or PIT (a word: the text font, as on a
    // completed pit lap's row)
    int elapsedLapTime = pitLap ? -1 : data.getElapsedLapTime();
    FontCategory timeFont = FontCategory::DIGITS;
    if (pitLap) {
        strcpy_s(timeStr, sizeof(timeStr), Placeholders::PIT_LAP);
        timeFont = FontCategory::NORMAL;
    } else if (elapsedLapTime > 0) {
        PluginUtils::formatLapTime(elapsedLapTime, timeStr, sizeof(timeStr));
    } else {
        strcpy_s(timeStr, sizeof(timeStr), Placeholders::GENERIC);
    }

    // Colors for live timing: primary for official, secondary for ticking values, muted for placeholders
    unsigned long colorLap = this->getColor(ColorSlot::TERTIARY);  // Lap number uses the label color
    // A pit lap's crossed sectors are muted, as on its completed row: the lap does not count.
    const unsigned long colorOfficial = pitLap ? this->getColor(ColorSlot::MUTED) : this->getColor(ColorSlot::PRIMARY);
    unsigned long colorS1 = (officialS1 > 0) ? colorOfficial
        : (activeSector == 0 && data.getElapsedSectorTime(0) > 0) ? this->getColor(ColorSlot::SECONDARY)
        : this->getColor(ColorSlot::MUTED);
    unsigned long colorS2 = (officialS2 > 0) ? colorOfficial
        : (activeSector == 1 && data.getElapsedSectorTime(1) > 0) ? this->getColor(ColorSlot::SECONDARY)
        : this->getColor(ColorSlot::MUTED);
    unsigned long colorS3 = (activeSector == 2 && data.getElapsedSectorTime(2) > 0)
        ? this->getColor(ColorSlot::SECONDARY) : this->getColor(ColorSlot::MUTED);
#if GAME_SECTOR_COUNT >= 4
    unsigned long colorS4 = (activeSector == 3 && data.getElapsedSectorTime(3) > 0)
        ? this->getColor(ColorSlot::SECONDARY) : this->getColor(ColorSlot::MUTED);
#endif
    unsigned long colorTime = (elapsedLapTime > 0)
        ? this->getColor(ColorSlot::SECONDARY) : this->getColor(ColorSlot::MUTED);

    addLabel(lapStr, cx.lapRightX, currentY, Justify::RIGHT, colorLap, dim);
    addString(cx.showSectors ? s1Str : "", cx.s1RightX, currentY, Justify::RIGHT, this->getFont(FontCategory::DIGITS), colorS1, dim.fontSize);
    addString(cx.showSectors ? s2Str : "", cx.s2RightX, currentY, Justify::RIGHT, this->getFont(FontCategory::DIGITS), colorS2, dim.fontSize);
    addString(cx.showSectors ? s3Str : "", cx.s3RightX, currentY, Justify::RIGHT, this->getFont(FontCategory::DIGITS), colorS3, dim.fontSize);
#if GAME_SECTOR_COUNT >= 4
    addString(cx.showSectors ? s4Str : "", cx.s4RightX, currentY, Justify::RIGHT, this->getFont(FontCategory::DIGITS), colorS4, dim.fontSize);
#endif
    addString(timeStr, cx.timeRightX, currentY, Justify::RIGHT, this->getFont(timeFont), colorTime, dim.fontSize);
}

void LapLogHud::addGapRow(const LapColumnX& cx, float currentY, const ScaledDimensions& dim) {
    char gapStr[32];
    unsigned long gapColor = this->getColor(ColorSlot::MUTED);

    // Against the lap this HUD's own Reference setting names: session
    // PB, all-time PB or last lap (the Gap Bar has its own). Just after a
    // split or the line, the official gap the freeze holds instead.
    int gap = 0;
    if (m_gapFreeze.shownGap(gapReference(), &gap)) {
        PluginUtils::formatTimeDiff(gapStr, sizeof(gapStr), gap);
        gapColor = this->deltaColor(gap);
    } else {
        strcpy_s(gapStr, sizeof(gapStr), Placeholders::GENERIC);
    }

    // Gap row: empty columns except for the gap, right-aligned with the time column
    addString("", cx.lapRightX, currentY, Justify::RIGHT, this->getFont(FontCategory::NORMAL), this->getColor(ColorSlot::MUTED), dim.fontSize);
    addString("", cx.s1RightX, currentY, Justify::RIGHT, this->getFont(FontCategory::NORMAL), this->getColor(ColorSlot::MUTED), dim.fontSize);
    addString("", cx.s2RightX, currentY, Justify::RIGHT, this->getFont(FontCategory::NORMAL), this->getColor(ColorSlot::MUTED), dim.fontSize);
    addString("", cx.s3RightX, currentY, Justify::RIGHT, this->getFont(FontCategory::NORMAL), this->getColor(ColorSlot::MUTED), dim.fontSize);
#if GAME_SECTOR_COUNT >= 4
    addString("", cx.s4RightX, currentY, Justify::RIGHT, this->getFont(FontCategory::NORMAL), this->getColor(ColorSlot::MUTED), dim.fontSize);
#endif
    addString(gapStr, cx.timeRightX, currentY, Justify::RIGHT, this->getFont(FontCategory::DIGITS), gapColor, dim.fontSize);
}

void LapLogHud::addPlaceholderRow(const LapColumnX& cx, float currentY, const ScaledDimensions& dim) {
    unsigned long mutedColor = this->getColor(ColorSlot::MUTED);
    addLabel(Placeholders::GENERIC, cx.lapRightX, currentY, Justify::RIGHT, mutedColor, dim);
    addString(cx.showSectors ? Placeholders::GENERIC : "", cx.s1RightX, currentY, Justify::RIGHT, this->getFont(FontCategory::DIGITS), mutedColor, dim.fontSize);
    addString(cx.showSectors ? Placeholders::GENERIC : "", cx.s2RightX, currentY, Justify::RIGHT, this->getFont(FontCategory::DIGITS), mutedColor, dim.fontSize);
    addString(cx.showSectors ? Placeholders::GENERIC : "", cx.s3RightX, currentY, Justify::RIGHT, this->getFont(FontCategory::DIGITS), mutedColor, dim.fontSize);
#if GAME_SECTOR_COUNT >= 4
    addString(cx.showSectors ? Placeholders::GENERIC : "", cx.s4RightX, currentY, Justify::RIGHT, this->getFont(FontCategory::DIGITS), mutedColor, dim.fontSize);
#endif
    addString(Placeholders::GENERIC, cx.timeRightX, currentY, Justify::RIGHT, this->getFont(FontCategory::DIGITS), mutedColor, dim.fontSize);
}

void LapLogHud::addLapEntryRow(const LapColumnX& cx, const BestTimes& best, const LapLogEntry& entry,
                               float currentY, const ScaledDimensions& dim) {
    char lapStr[8];
    char s1Str[16];
    char s2Str[16];
    char s3Str[16];
#if GAME_SECTOR_COUNT >= 4
    char s4Str[16];
#endif
    char timeStr[16];

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

#if GAME_SECTOR_COUNT >= 4
    if (entry.sector4 > 0) {
        PluginUtils::formatLapTime(entry.sector4, s4Str, sizeof(s4Str));
    } else {
        strcpy_s(s4Str, sizeof(s4Str), Placeholders::GENERIC);
    }
#endif

    // Format lap time. A lap with a time shows it (muted below when invalid);
    // one without says why, on its own row: through the pits or struck out.
    // Those are words, so they take the text font, not Digits.
    FontCategory timeFont = FontCategory::DIGITS;
    if (entry.lapTime > 0 && entry.isComplete) {
        PluginUtils::formatLapTime(entry.lapTime, timeStr, sizeof(timeStr));
    } else if (entry.viaPits) {
        strcpy_s(timeStr, sizeof(timeStr), Placeholders::PIT_LAP);
        timeFont = FontCategory::NORMAL;
    } else if (!entry.isValid) {
        strcpy_s(timeStr, sizeof(timeStr), Placeholders::INVALID_LAP);
        timeFont = FontCategory::NORMAL;
    } else {
        strcpy_s(timeStr, sizeof(timeStr), Placeholders::GENERIC);
    }

    // Determine colors
    // Invalid laps (track cuts in race mode) show muted times
    unsigned long colorLap = this->getColor(ColorSlot::TERTIARY);  // Lap number uses the label color
    unsigned long colorS1, colorS2, colorS3, colorTime;
#if GAME_SECTOR_COUNT >= 4
    unsigned long colorS4;
#endif

    // For invalid laps, show all timing data as muted
    // For valid laps, highlight PBs in green, others in primary
    if (!entry.isValid || entry.sector1 <= 0) {
        colorS1 = this->getColor(ColorSlot::MUTED);
    } else {
        colorS1 = (entry.sector1 == best.bestSector1) ? this->getColor(ColorSlot::POSITIVE) : this->getColor(ColorSlot::PRIMARY);
    }

    if (!entry.isValid || entry.sector2 <= 0) {
        colorS2 = this->getColor(ColorSlot::MUTED);
    } else {
        colorS2 = (entry.sector2 == best.bestSector2) ? this->getColor(ColorSlot::POSITIVE) : this->getColor(ColorSlot::PRIMARY);
    }

    if (!entry.isValid || entry.sector3 <= 0) {
        colorS3 = this->getColor(ColorSlot::MUTED);
    } else {
        colorS3 = (entry.sector3 == best.bestSector3) ? this->getColor(ColorSlot::POSITIVE) : this->getColor(ColorSlot::PRIMARY);
    }

#if GAME_SECTOR_COUNT >= 4
    if (!entry.isValid || entry.sector4 <= 0) {
        colorS4 = this->getColor(ColorSlot::MUTED);
    } else {
        colorS4 = (entry.sector4 == best.bestSector4) ? this->getColor(ColorSlot::POSITIVE) : this->getColor(ColorSlot::PRIMARY);
    }
#endif

    bool hasLapTime = (entry.lapTime > 0 && entry.isComplete);
    if (!entry.isValid || !hasLapTime) {
        colorTime = this->getColor(ColorSlot::MUTED);
    } else {
        colorTime = (entry.lapTime == best.bestLapTime) ? this->getColor(ColorSlot::POSITIVE) : this->getColor(ColorSlot::PRIMARY);
    }

    // Render lap data row
    addLabel(lapStr, cx.lapRightX, currentY, Justify::RIGHT, colorLap, dim);
    addString(cx.showSectors ? s1Str : "", cx.s1RightX, currentY, Justify::RIGHT, this->getFont(FontCategory::DIGITS), colorS1, dim.fontSize);
    addString(cx.showSectors ? s2Str : "", cx.s2RightX, currentY, Justify::RIGHT, this->getFont(FontCategory::DIGITS), colorS2, dim.fontSize);
    addString(cx.showSectors ? s3Str : "", cx.s3RightX, currentY, Justify::RIGHT, this->getFont(FontCategory::DIGITS), colorS3, dim.fontSize);
#if GAME_SECTOR_COUNT >= 4
    addString(cx.showSectors ? s4Str : "", cx.s4RightX, currentY, Justify::RIGHT, this->getFont(FontCategory::DIGITS), colorS4, dim.fontSize);
#endif
    addString(timeStr, cx.timeRightX, currentY, Justify::RIGHT, this->getFont(timeFont), colorTime, dim.fontSize);
}

void LapLogHud::resetToDefaults() {
    m_bVisible = false;  // Lap log is off by default; users can enable via settings
    m_bShowTitle = true;
    setTextureVariant(0);  // No texture by default
    m_fBackgroundOpacity = SettingsLimits::DEFAULT_OPACITY;
    setScale(1.0f);
    setPosition(cellsX(1), cellsY(64));
    m_enabledColumns = COL_DEFAULT;
    m_maxDisplayLaps = 5;
    m_showLiveTiming = true;
    m_showGapRow = true;
    m_gapReference = PbGapTracker::Ref::SESSION_PB;
    m_gapReferenceDefault = true;
    m_freezeDurationMs = FreezeDuration::FOLLOW_DEFAULT;
    m_gapFreeze.reset();
    m_bShowHeaders = false;
    m_displayOrder = DisplayOrder::OLDEST_FIRST;
    setDataDirty();
}
