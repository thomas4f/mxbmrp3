// ============================================================================
// hud/session_best_hud.cpp
// Displays session best split times with comparison to personal best
// ============================================================================
#include "session_best_hud.h"

#include <cstring>
#include <cstdio>

#include "../diagnostics/logger.h"
#include "../diagnostics/timer.h"
#include "../core/plugin_utils.h"
#include "../core/plugin_constants.h"
#include "../core/plugin_data.h"
#include "../core/color_config.h"

using namespace PluginConstants;

SessionBestHud::ColumnPositions::ColumnPositions(float contentStartX, float scale) {
    float scaledFontSize = FontSizes::NORMAL * scale;
    label = contentStartX;
    time = label + PluginUtils::calculateMonospaceTextWidth(COL_LABEL_WIDTH, scaledFontSize);
    diff = time + PluginUtils::calculateMonospaceTextWidth(COL_TIME_WIDTH, scaledFontSize);
}

SessionBestHud::SessionBestHud()
    : m_columns(START_X + Padding::HUD_HORIZONTAL, m_fScale)
{
    DEBUG_INFO("SessionBestHud created");
    setDraggable(true);

    // Set defaults to match user configuration
    m_bVisible = true;  // Enabled by default
    m_bShowTitle = true;
    m_fBackgroundOpacity = SettingsLimits::DEFAULT_OPACITY;
    setPosition(0.0935f, 0.0999f);

    // Pre-allocate vectors
    m_quads.reserve(1);
    m_strings.reserve(20);  // Header row + 6 data rows

    rebuildRenderData();
}

bool SessionBestHud::handlesDataType(DataChangeType dataType) const {
    return (dataType == DataChangeType::SessionBest ||
            dataType == DataChangeType::LapLog ||
            dataType == DataChangeType::SpectateTarget);
}

int SessionBestHud::getEnabledRowCount() const {
    int count = 0;
    if (m_enabledRows & ROW_S1) count++;
    if (m_enabledRows & ROW_S2) count++;
    if (m_enabledRows & ROW_S3) count++;
    if (m_enabledRows & ROW_LAST) count++;
    if (m_enabledRows & ROW_BEST) count++;
    if (m_enabledRows & ROW_IDEAL) count++;
    return count;
}

void SessionBestHud::update() {
    // Check if we need frequent updates for ticking sector time
    if (needsFrequentUpdates()) {
        auto now = std::chrono::steady_clock::now();
        auto sinceLastTick = std::chrono::duration_cast<std::chrono::milliseconds>(
            now - m_lastTickUpdate
        ).count();

        if (sinceLastTick >= TICK_UPDATE_INTERVAL_MS) {
            m_lastTickUpdate = now;
            setDataDirty();
        }
    }

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

bool SessionBestHud::needsFrequentUpdates() const {
    // Need frequent updates when showing live sector time and timer is valid
    if (!m_bShowLiveSectorTime) return false;
    if (!m_bVisible) return false;

    const PluginData& data = PluginData::getInstance();
    return data.isLapTimerValid();
}

int SessionBestHud::getCurrentActiveSector() const {
    // Determine which sector is currently being timed (not yet completed)
    // Returns 0 for S1, 1 for S2, 2 for S3, -1 if no active timing
    const PluginData& data = PluginData::getInstance();
    const CurrentLapData* currentLap = data.getCurrentLapData();

    if (!data.isLapTimerValid()) {
        return -1;  // No active timing
    }

    if (!currentLap || currentLap->split1 < 0) {
        return 0;  // S1 not crossed yet - timing S1
    } else if (currentLap->split2 < 0) {
        return 1;  // S1 crossed, S2 not - timing S2
    } else {
        return 2;  // S2 crossed - timing S3
    }
}

void SessionBestHud::rebuildLayout() {
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

void SessionBestHud::rebuildRenderData() {
    m_strings.clear();
    m_quads.clear();

    // Get player data
    const PluginData& data = PluginData::getInstance();

    const CurrentLapData* currentLap = data.getCurrentLapData();
    const SessionBestData* sessionBest = data.getSessionBestData();
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
    addTitleString("Session Best", contentStartX, currentY, Justify::LEFT,
        Fonts::ENTER_SANSMAN, ColorConfig::getInstance().getPrimary(), dim.fontSizeLarge);
    currentY += titleHeight;

    // Recalculate column positions for current scale
    m_columns = ColumnPositions(contentStartX, m_fScale);

    // Calculate sector times from current lap (accumulated times)
    // Note: While the S3 split event fires when crossing the finish line, currentLap is
    // immediately cleared by the lap handler. Therefore, S3 is always displayed from
    // sessionBest->lastLapSector3 (never from currentLap).
    int currentSector1 = -1, currentSector2 = -1;
    if (currentLap) {
        currentSector1 = currentLap->split1;
        if (currentLap->split2 > 0 && currentLap->split1 > 0) {
            currentSector2 = currentLap->split2 - currentLap->split1;
        } else if (currentLap->split2 > 0) {
            // Corrupted data: S2 crossed but S1 not set
            DEBUG_WARN_F("Invalid current lap data: split2=%d but split1=%d",
                         currentLap->split2, currentLap->split1);
            currentSector2 = -1;
        }
    }

    // Determine which split times to display
    // With live sector timing enabled, show elapsed time for the current sector
    // - If no splits crossed yet: Show elapsed S1 (ticking) or last lap data
    // - If S1 crossed: Show official S1, elapsed S2 (ticking) or placeholder
    // - If S1+S2 crossed: Show official S1/S2, elapsed S3 (ticking) or placeholder
    // - After lap completes: Show last lap data (currentLap is cleared)
    int displaySector1, displaySector2, displaySector3;
    bool sector1IsLive = false, sector2IsLive = false, sector3IsLive = false;

    // Check if we have active timing for live sector display
    int activeSector = m_bShowLiveSectorTime ? getCurrentActiveSector() : -1;

    if (currentSector1 >= 0) {
        // We've crossed S1 in current lap - show official S1 time
        displaySector1 = currentSector1;

        if (currentSector2 >= 0) {
            // We've crossed S2 as well - show official S2 time
            displaySector2 = currentSector2;

            // S3 is in progress - show elapsed time if live timing enabled
            if (activeSector == 2) {
                displaySector3 = data.getElapsedSectorTime(2);
                sector3IsLive = (displaySector3 >= 0);
            } else {
                displaySector3 = -1;  // Show placeholder
            }
        } else {
            // Only S1 crossed - S2 is in progress
            if (activeSector == 1) {
                displaySector2 = data.getElapsedSectorTime(1);
                sector2IsLive = (displaySector2 >= 0);
            } else {
                displaySector2 = -1;  // Show placeholder
            }
            displaySector3 = -1;  // Show placeholder
        }
    } else {
        // Haven't crossed any splits yet
        if (activeSector == 0) {
            // S1 is in progress - show elapsed time
            displaySector1 = data.getElapsedSectorTime(0);
            sector1IsLive = (displaySector1 >= 0);
            displaySector2 = -1;  // Show placeholder
            displaySector3 = -1;  // Show placeholder
        } else {
            // No active timing - show last lap data
            displaySector1 = sessionBest ? sessionBest->lastLapSector1 : -1;
            displaySector2 = sessionBest ? sessionBest->lastLapSector2 : -1;
            displaySector3 = sessionBest ? sessionBest->lastLapSector3 : -1;
        }
    }

    // Helper for adding a row (only adds strings for enabled rows)
    // isLive: true if showing real-time ticking sector time (use muted color, no diff)
    auto addRow = [&](bool enabled, const char* label, int timeMs, int pbTimeMs, int previousPbTimeMs, bool showDiff, bool isLive = false, int timeFont = Fonts::ROBOTO_MONO) {
        if (!enabled) {
            return;  // Skip disabled rows entirely - don't add strings or advance Y
        }

        char timeStr[16];
        char diffStr[16];
        char paddedLabel[8];

        // Right-align label by padding with spaces (max label is "Ideal" = 5 chars)
        snprintf(paddedLabel, sizeof(paddedLabel), "%5s", label);

        // Label in secondary color
        addString(paddedLabel, m_columns.label, currentY, Justify::LEFT, Fonts::ROBOTO_MONO, ColorConfig::getInstance().getSecondary(), dim.fontSize);

        // Show time or placeholder
        if (timeMs > 0) {
            PluginUtils::formatLapTime(timeMs, timeStr, sizeof(timeStr));
            // Live (ticking) times use muted color to distinguish from official times
            // Official times use primary color (or specified font for Best)
            unsigned long timeColor = isLive ? ColorConfig::getInstance().getMuted() : ColorConfig::getInstance().getPrimary();
            addString(timeStr, m_columns.time, currentY, Justify::LEFT, timeFont, timeColor, dim.fontSize);
        } else {
            strcpy_s(timeStr, sizeof(timeStr), Placeholders::LAP_TIME);
            // Placeholder in muted color
            addString(timeStr, m_columns.time, currentY, Justify::LEFT, Fonts::ROBOTO_MONO, ColorConfig::getInstance().getMuted(), dim.fontSize);
        }

        // Always add a third string to maintain 3 strings per row (required for layout)
        // Show diff, or "-" if no PB to compare against, or empty for non-diff rows
        // Live times don't show diff (meaningless to compare incomplete sector to completed PB)
        if (showDiff && !isLive) {
            if (pbTimeMs > 0 && timeMs >= 0) {
                int diff = timeMs - pbTimeMs;
                if (diff != 0) {
                    // Show colored diff for improvement/loss using central formatting
                    PluginUtils::formatTimeDiff(diffStr, sizeof(diffStr), diff);
                    unsigned long diffColor = (diff < 0) ? ColorConfig::getInstance().getPositive() : ColorConfig::getInstance().getNegative();
                    addString(diffStr, m_columns.diff, currentY, Justify::LEFT, Fonts::ROBOTO_MONO, diffColor, dim.fontSize);
                } else {
                    // Diff is exactly zero - this is a new PB
                    // Compare against previous PB instead to show improvement
                    if (previousPbTimeMs > 0) {
                        int prevDiff = timeMs - previousPbTimeMs;
                        PluginUtils::formatTimeDiff(diffStr, sizeof(diffStr), prevDiff);
                        unsigned long diffColor = (prevDiff < 0) ? ColorConfig::getInstance().getPositive() : ColorConfig::getInstance().getNegative();
                        addString(diffStr, m_columns.diff, currentY, Justify::LEFT, Fonts::ROBOTO_MONO, diffColor, dim.fontSize);
                    } else {
                        // No previous PB to compare - this is the first PB
                        addString(Placeholders::GENERIC, m_columns.diff, currentY, Justify::LEFT, Fonts::ROBOTO_MONO, ColorConfig::getInstance().getMuted(), dim.fontSize);
                    }
                }
            } else {
                // No PB to compare or no valid time - show placeholder in muted color
                addString(Placeholders::GENERIC, m_columns.diff, currentY, Justify::LEFT, Fonts::ROBOTO_MONO, ColorConfig::getInstance().getMuted(), dim.fontSize);
            }
        } else {
            // For rows without diff (Best, Ideal, or live times), add empty string to maintain layout
            addString("", m_columns.diff, currentY, Justify::LEFT, Fonts::ROBOTO_MONO, ColorConfig::getInstance().getMuted(), dim.fontSize);
        }

        currentY += dim.lineHeightNormal;
    };

    // Add rows (only enabled rows will be added)
    // Lambda returns early for disabled rows
    // Compare sectors against actual PB lap's sectors (not purple/ideal sectors)
    // Pass isLive flag to indicate which sector is currently being timed (ticking display)
    addRow(m_enabledRows & ROW_S1, "S1", displaySector1, personalBest ? personalBest->sector1 : -1, sessionBest ? sessionBest->previousBestSector1 : -1, true, sector1IsLive);
    addRow(m_enabledRows & ROW_S2, "S2", displaySector2, personalBest ? personalBest->sector2 : -1, sessionBest ? sessionBest->previousBestSector2 : -1, true, sector2IsLive);
    addRow(m_enabledRows & ROW_S3, "S3", displaySector3, personalBest ? personalBest->sector3 : -1, sessionBest ? sessionBest->previousBestSector3 : -1, true, sector3IsLive);

    int lastLap = (sessionBest && sessionBest->lastLapTime > 0) ? sessionBest->lastLapTime : -1;
    addRow(m_enabledRows & ROW_LAST, "Last", lastLap, personalBest ? personalBest->lapTime : -1, sessionBest ? sessionBest->previousBestLapTime : -1, true, false);

    int bestLap = personalBest ? personalBest->lapTime : -1;
    addRow(m_enabledRows & ROW_BEST, "Best", bestLap, -1, -1, false, false, Fonts::ROBOTO_MONO_BOLD);

    int idealLap = sessionBest ? sessionBest->getIdealLapTime() : -1;
    addRow(m_enabledRows & ROW_IDEAL, "Ideal", idealLap, -1, -1, false, false);
}

void SessionBestHud::resetToDefaults() {
    m_bVisible = true;  // Enabled by default
    m_bShowTitle = true;
    m_bShowBackgroundTexture = false;  // No texture by default
    m_fBackgroundOpacity = SettingsLimits::DEFAULT_OPACITY;
    m_fScale = 1.0f;
    setPosition(0.0935f, 0.0999f);
    m_enabledRows = ROW_DEFAULT;
    m_bShowLiveSectorTime = true;  // Live sector time enabled by default
    setDataDirty();
}
