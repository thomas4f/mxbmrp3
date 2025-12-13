// ============================================================================
// hud/timing_hud.cpp
// Timing HUD - displays accumulated split and lap times as they happen
// Shows accumulated times and gaps (default position: center of screen)
// Supports real-time elapsed timer with per-column visibility modes
// Example: S1: 30.00s, S2: 60.00s (accumulated), Lap: 90.00s
// ============================================================================
#include "timing_hud.h"

#include <cstdio>
#include <cmath>
#include <string>

#include "../diagnostics/logger.h"
#include "../diagnostics/timer.h"
#include "../core/plugin_utils.h"
#include "../core/widget_constants.h"
#include "../core/color_config.h"

using namespace PluginConstants;
using namespace CenterDisplayPositions;

TimingHud::TimingHud()
    : m_displayDurationMs(DEFAULT_DURATION_MS)
    , m_cachedSplit1(-1)
    , m_cachedSplit2(-1)
    , m_cachedLastCompletedLapNum(-1)
    , m_cachedDisplayRaceNum(-1)
    , m_cachedSession(-1)
    , m_cachedPitState(-1)
    , m_currentLapNum(0)
    , m_isFrozen(false)
{
    // Initialize column modes to defaults
    m_columnModes[COL_LABEL] = ColumnMode::SPLITS;
    m_columnModes[COL_TIME] = ColumnMode::ALWAYS;
    m_columnModes[COL_GAP] = ColumnMode::SPLITS;

    // NOTE: Does not use initializeWidget() helper due to special requirements:
    // - Requires quad reservation (timing HUDs need background quads)
    // This is an intentional design decision - see base_hud.h initializeWidget() docs
    DEBUG_INFO("TimingHud created");
    setDraggable(true);  // Allow repositioning from default center position

    // Set defaults to match user configuration
    m_bShowTitle = false;  // No title displayed (consistent with BarsWidget)
    m_fBackgroundOpacity = 0.1f;

    // Pre-allocate vectors
    m_quads.reserve(4);    // Four background quads (reverse + label + time + gap)
    m_strings.reserve(4);  // Reverse notice + label + time + gap

    rebuildRenderData();
}

bool TimingHud::handlesDataType(DataChangeType dataType) const {
    return dataType == DataChangeType::SessionBest ||
           dataType == DataChangeType::SpectateTarget ||
           dataType == DataChangeType::SessionData ||  // Reset on new session/event
           dataType == DataChangeType::Standings;       // Detect pit entry/exit
}

void TimingHud::update() {
    const PluginData& pluginData = PluginData::getInstance();
    const SessionData& sessionData = pluginData.getSessionData();

    // Detect session changes (new event) and reset state
    // Check both session type AND if session data was cleared (lastCompletedLapNum reset to -1)
    int currentSession = sessionData.session;
    const SessionBestData* sessionBest = pluginData.getSessionBestData();
    int currentLastCompletedLap = sessionBest ? sessionBest->lastCompletedLapNum : -1;

    bool sessionTypeChanged = (currentSession != m_cachedSession);
    bool sessionDataCleared = (m_cachedLastCompletedLapNum >= 0 && currentLastCompletedLap < 0);

    if (sessionTypeChanged || sessionDataCleared) {
        DEBUG_INFO_F("TimingHud: Session reset detected (type changed: %d, data cleared: %d)",
            sessionTypeChanged, sessionDataCleared);
        resetLiveTimingState();
        m_cachedSession = currentSession;
        m_cachedPitState = -1;  // Reset pit state cache for new session
        setDataDirty();
    }

    // Detect spectate target changes and reset state
    int currentDisplayRaceNum = pluginData.getDisplayRaceNum();
    if (currentDisplayRaceNum != m_cachedDisplayRaceNum) {
        DEBUG_INFO_F("TimingHud: Spectate target changed from %d to %d", m_cachedDisplayRaceNum, currentDisplayRaceNum);

        // Full reset on spectate change
        resetLiveTimingState();
        m_cachedDisplayRaceNum = currentDisplayRaceNum;
        m_cachedPitState = -1;  // Reset pit state cache for new rider

        // Update cached values with new rider's current data (without triggering display)
        const CurrentLapData* currentLap = pluginData.getCurrentLapData();
        const SessionBestData* sessionBest = pluginData.getSessionBestData();
        if (currentLap) {
            m_cachedSplit1 = currentLap->split1;
            m_cachedSplit2 = currentLap->split2;
        }
        if (sessionBest) {
            m_cachedLastCompletedLapNum = sessionBest->lastCompletedLapNum;
        }

        setDataDirty();
    }

    // Detect pit entry/exit and reset anchor (but keep official data for gap)
    const StandingsData* standing = pluginData.getStanding(currentDisplayRaceNum);
    if (standing) {
        int currentPitState = standing->pit;
        if (m_cachedPitState != -1 && currentPitState != m_cachedPitState) {
            DEBUG_INFO_F("TimingHud: Pit state changed from %d to %d", m_cachedPitState, currentPitState);
            // Soft reset - clear anchor but keep official gap data
            softResetAnchor();
            setDataDirty();
        }
        m_cachedPitState = currentPitState;
    }

    // Process any split/lap completion updates
    processTimingUpdates();

    // Check if freeze period has expired
    checkFreezeExpiration();

    // Check if we need frequent updates for ticking timer
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

    // Check data dirty first (takes precedence)
    if (isDataDirty()) {
        rebuildRenderData();
        clearDataDirty();
        clearLayoutDirty();
    }
    else if (isLayoutDirty()) {
        rebuildLayout();
        clearLayoutDirty();
    }
}

void TimingHud::processTimingUpdates() {
    const PluginData& pluginData = PluginData::getInstance();
    const CurrentLapData* currentLap = pluginData.getCurrentLapData();
    const SessionBestData* sessionBest = pluginData.getSessionBestData();
    const LapLogEntry* personalBest = pluginData.getBestLapEntry();

    // Check current lap splits (CurrentLapData tracks accumulated times for current lap)
    if (currentLap) {
        // Check split 1 (accumulated time to S1)
        if (currentLap->split1 > 0 && currentLap->split1 != m_cachedSplit1) {
            int splitTime = currentLap->split1;
            int bestTime = personalBest ? personalBest->sector1 : -1;
            int previousBestTime = sessionBest ? sessionBest->previousBestSector1 : -1;

            // Calculate gap
            int gap = calculateGapToBest(splitTime, bestTime);
            if (gap == 0 && previousBestTime > 0) {
                gap = splitTime - previousBestTime;  // New PB - compare to previous
            }

            // Update official data cache
            m_officialData.time = splitTime;
            m_officialData.gap = gap;
            m_officialData.splitIndex = 0;
            m_officialData.lapNum = currentLap->lapNum;
            m_officialData.hasGap = (bestTime > 0 || previousBestTime > 0);
            m_officialData.isFaster = (gap < 0);
            m_officialData.isSlower = (gap > 0);
            m_officialData.isInvalid = false;

            // Set anchor at this split (uses wall clock time internally)
            m_anchor.set(splitTime);
            m_currentLapNum = currentLap->lapNum;

            // Freeze display (if freeze is enabled)
            if (m_displayDurationMs > 0) {
                m_isFrozen = true;
                m_frozenAt = std::chrono::steady_clock::now();
            }

            m_cachedSplit1 = currentLap->split1;
            DEBUG_INFO_F("TimingHud: Split 1 crossed, accumulated=%d ms, gap=%d ms, lap=%d", splitTime, gap, currentLap->lapNum);
            setDataDirty();
        }
        // Check split 2 (accumulated time to S2)
        else if (currentLap->split2 > 0 && currentLap->split2 != m_cachedSplit2) {
            int splitTime = currentLap->split2;

            // Compare against PB lap's accumulated time to S2 (sector1 + sector2)
            int bestTime = -1;
            int previousBestTime = -1;
            if (personalBest && personalBest->sector1 > 0 && personalBest->sector2 > 0) {
                bestTime = personalBest->sector1 + personalBest->sector2;
            }
            if (sessionBest && sessionBest->previousBestSector1 > 0 && sessionBest->previousBestSector2 > 0) {
                previousBestTime = sessionBest->previousBestSector1 + sessionBest->previousBestSector2;
            }

            // Calculate gap
            int gap = calculateGapToBest(splitTime, bestTime);
            if (gap == 0 && previousBestTime > 0) {
                gap = splitTime - previousBestTime;
            }

            // Update official data cache
            m_officialData.time = splitTime;
            m_officialData.gap = gap;
            m_officialData.splitIndex = 1;
            m_officialData.lapNum = currentLap->lapNum;
            m_officialData.hasGap = (bestTime > 0 || previousBestTime > 0);
            m_officialData.isFaster = (gap < 0);
            m_officialData.isSlower = (gap > 0);
            m_officialData.isInvalid = false;

            // Set anchor at this split (uses wall clock time internally)
            m_anchor.set(splitTime);
            m_currentLapNum = currentLap->lapNum;

            // Freeze display (if freeze is enabled)
            if (m_displayDurationMs > 0) {
                m_isFrozen = true;
                m_frozenAt = std::chrono::steady_clock::now();
            }

            m_cachedSplit2 = currentLap->split2;
            DEBUG_INFO_F("TimingHud: Split 2 crossed, accumulated=%d ms, gap=%d ms, lap=%d", splitTime, gap, currentLap->lapNum);
            setDataDirty();
        }
    }

    // Check for lap completion (split 3 / finish line)
    if (sessionBest && sessionBest->lastCompletedLapNum >= 0 &&
        sessionBest->lastCompletedLapNum != m_cachedLastCompletedLapNum) {

        int lapTime = sessionBest->lastLapTime;
        int bestTime = personalBest ? personalBest->lapTime : -1;
        int previousBestTime = sessionBest->previousBestLapTime;

        // Check if this lap was valid by looking at the lap log
        bool isValid = true;
        int completedLapNum = sessionBest->lastCompletedLapNum;
        const std::vector<LapLogEntry>* lapLog = pluginData.getLapLog();
        if (lapLog && !lapLog->empty()) {
            const LapLogEntry& mostRecentLap = (*lapLog)[0];
            isValid = mostRecentLap.isValid;
            if (mostRecentLap.lapNum >= 0) {
                completedLapNum = mostRecentLap.lapNum;
            }
        }

        // Calculate gap
        int gap = 0;
        if (isValid && lapTime > 0) {
            gap = calculateGapToBest(lapTime, bestTime);
            if (gap == 0 && previousBestTime > 0) {
                gap = lapTime - previousBestTime;
            }
        }

        // Update official data cache
        m_officialData.time = lapTime;
        m_officialData.gap = gap;
        m_officialData.splitIndex = -1;  // Indicates lap complete
        m_officialData.lapNum = completedLapNum;
        m_officialData.hasGap = isValid && (bestTime > 0 || previousBestTime > 0);
        m_officialData.isFaster = (gap < 0);
        m_officialData.isSlower = (gap > 0) || !isValid;
        m_officialData.isInvalid = !isValid;

        // Reset anchor for new lap (accumulated = 0, uses wall clock time internally)
        m_anchor.set(0);
        m_currentLapNum = completedLapNum + 1;

        // Reset split caches for next lap
        m_cachedSplit1 = -1;
        m_cachedSplit2 = -1;

        // Freeze display (if freeze is enabled)
        if (m_displayDurationMs > 0) {
            m_isFrozen = true;
            m_frozenAt = std::chrono::steady_clock::now();
        }

        m_cachedLastCompletedLapNum = sessionBest->lastCompletedLapNum;
        DEBUG_INFO_F("TimingHud: Lap %d completed, time=%d ms, gap=%d ms, valid=%d", completedLapNum, lapTime, gap, isValid);
        setDataDirty();
    }
}

void TimingHud::updateTrackPosition(int raceNum, float trackPos, int lapNum) {
    // Only process for the rider we're currently displaying
    if (raceNum != m_cachedDisplayRaceNum) {
        return;
    }

    if (!m_trackMonitor.initialized) {
        m_trackMonitor.lastTrackPos = trackPos;
        m_trackMonitor.lastLapNum = lapNum;
        m_trackMonitor.initialized = true;
        return;
    }

    float delta = trackPos - m_trackMonitor.lastTrackPos;

    // Detect S/F crossing: large negative delta (0.95 → 0.05 gives delta ~ -0.9)
    if (delta < -TrackPositionMonitor::WRAP_THRESHOLD) {
        // Crossed S/F line - set anchor if we don't have one or lap changed
        if (!m_anchor.valid || lapNum != m_trackMonitor.lastLapNum) {
            m_anchor.set(0);  // Start timing from 0, uses wall clock time internally
            // lapNum is number of completed laps (0-based), which matches m_currentLapNum indexing
            // Display adds +1, so lapNum=0 shows "Lap 1", lapNum=1 shows "Lap 2", etc.
            m_currentLapNum = lapNum;
            DEBUG_INFO_F("TimingHud: S/F crossing detected via track position, lap=%d", m_currentLapNum);
            // Don't update m_officialData - this is estimated, not official
            // Gap column retains previous official value
            setDataDirty();
        }
    }

    m_trackMonitor.lastTrackPos = trackPos;
    m_trackMonitor.lastLapNum = lapNum;
}

void TimingHud::checkFreezeExpiration() {
    if (!m_isFrozen) return;

    auto now = std::chrono::steady_clock::now();
    auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
        now - m_frozenAt
    ).count();

    if (elapsed >= m_displayDurationMs) {
        m_isFrozen = false;
        setDataDirty();
    }
}

bool TimingHud::shouldShowColumn(Column col) const {
    ColumnMode mode = m_columnModes[col];

    switch (mode) {
        case ColumnMode::OFF:
            return false;

        case ColumnMode::SPLITS:
            return m_isFrozen;  // Only during freeze

        case ColumnMode::ALWAYS:
            return true;  // Always visible
    }

    return false;
}

bool TimingHud::needsFrequentUpdates() const {
    // Need frequent updates when time column is in ALWAYS mode, not frozen, and we have an anchor
    if (m_isFrozen) return false;
    if (m_columnModes[COL_TIME] != ColumnMode::ALWAYS) return false;
    return m_anchor.valid;
}

int TimingHud::calculateElapsedTime() const {
    if (!m_anchor.valid) {
        return -1;  // No anchor - show placeholder
    }

    // Use wall clock time for elapsed calculation (works regardless of session time direction)
    auto now = std::chrono::steady_clock::now();
    auto wallElapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
        now - m_anchor.wallClockTime
    ).count();

    int elapsed = m_anchor.accumulatedTime + static_cast<int>(wallElapsed);

    // Sanity check - don't show negative time
    if (elapsed < 0) elapsed = 0;

    return elapsed;
}

int TimingHud::calculateGapToBest(int currentTime, int bestTime) const {
    if (currentTime <= 0 || bestTime <= 0) {
        return 0;
    }
    return currentTime - bestTime;
}

int TimingHud::getVisibleColumnCount() const {
    int count = 0;
    for (int i = 0; i < COL_COUNT; i++) {
        if (shouldShowColumn(static_cast<Column>(i))) count++;
    }
    return count;
}

void TimingHud::resetLiveTimingState() {
    m_anchor.reset();
    m_trackMonitor.reset();
    m_isFrozen = false;
    m_currentLapNum = 0;
    m_officialData.reset();
    m_cachedSplit1 = -1;
    m_cachedSplit2 = -1;
    m_cachedLastCompletedLapNum = -1;
}

void TimingHud::softResetAnchor() {
    m_anchor.reset();
    m_trackMonitor.reset();
    // Keep m_officialData - gap still relevant
}

void TimingHud::rebuildLayout() {
    // Layout changes require full rebuild since columns are dynamic
    rebuildRenderData();
}

void TimingHud::rebuildRenderData() {
    // Clear render data
    m_strings.clear();
    m_quads.clear();

    // Check if any columns are visible
    int visibleCount = getVisibleColumnCount();
    if (visibleCount == 0) {
        setBounds(0.0f, 0.0f, 0.0f, 0.0f);
        return;
    }

    auto dim = getScaledDimensions();

    // Column dimensions
    float columnTextWidth = PluginUtils::calculateMonospaceTextWidth(WidgetDimensions::STANDARD_WIDTH, dim.fontSizeLarge);
    float charGap = PluginUtils::calculateMonospaceTextWidth(1, dim.fontSizeLarge);
    float columnQuadWidth = dim.paddingH + columnTextWidth + dim.paddingH;
    float quadHeight = dim.paddingV + dim.fontSizeLarge;

    // Calculate total width based on visible columns
    float totalWidth = visibleCount * columnQuadWidth + (visibleCount - 1) * charGap;

    // Starting X position (centered)
    float currentX = CENTER_X - totalWidth / 2.0f;
    float quadY = TIMING_DIVIDER_Y + DIVIDER_GAP;
    float textY = quadY + dim.paddingV * 0.5f;

    // Track bounds
    float leftX = currentX;
    float rightX = currentX;

    // Prepare content for each column
    char labelBuffer[16];
    char timeBuffer[32];
    char gapBuffer[32];
    bool gapIsFaster = false;
    bool gapIsSlower = false;

    // === LABEL COLUMN CONTENT ===
    if (m_isFrozen) {
        // Show official label
        if (m_officialData.splitIndex == 0) {
            strcpy_s(labelBuffer, sizeof(labelBuffer), "Split 1");
        } else if (m_officialData.splitIndex == 1) {
            strcpy_s(labelBuffer, sizeof(labelBuffer), "Split 2");
        } else {
            // Lap complete
            if (m_officialData.lapNum >= 0) {
                snprintf(labelBuffer, sizeof(labelBuffer), "Lap %d", m_officialData.lapNum + 1);
            } else {
                strcpy_s(labelBuffer, sizeof(labelBuffer), "Lap -");
            }
        }
    } else {
        // Ticking - show current lap (or placeholder if no timing context yet)
        if (m_anchor.valid) {
            snprintf(labelBuffer, sizeof(labelBuffer), "Lap %d", m_currentLapNum + 1);
        } else {
            strcpy_s(labelBuffer, sizeof(labelBuffer), "Lap -");
        }
    }

    // === TIME COLUMN CONTENT ===
    if (m_isFrozen) {
        // Show official time
        if (m_officialData.time > 0) {
            PluginUtils::formatLapTime(m_officialData.time, timeBuffer, sizeof(timeBuffer));
        } else {
            strcpy_s(timeBuffer, sizeof(timeBuffer), Placeholders::LAP_TIME);
        }
    } else {
        // Calculate elapsed time
        int elapsed = calculateElapsedTime();
        if (elapsed >= 0) {
            PluginUtils::formatLapTime(elapsed, timeBuffer, sizeof(timeBuffer));
        } else {
            strcpy_s(timeBuffer, sizeof(timeBuffer), Placeholders::LAP_TIME);
        }
    }

    // === GAP COLUMN CONTENT ===
    // When frozen: show official gap data
    // When ticking: show placeholder (gap is only meaningful at timing events)
    if (m_isFrozen) {
        if (m_officialData.isInvalid) {
            strcpy_s(gapBuffer, sizeof(gapBuffer), "INVALID");
            gapIsSlower = true;
        } else if (!m_officialData.hasGap) {
            strcpy_s(gapBuffer, sizeof(gapBuffer), Placeholders::GENERIC);
        } else {
            PluginUtils::formatTimeDiff(gapBuffer, sizeof(gapBuffer), m_officialData.gap);
            gapIsFaster = m_officialData.isFaster;
            gapIsSlower = m_officialData.isSlower;
        }
    } else {
        // Ticking - show placeholder
        strcpy_s(gapBuffer, sizeof(gapBuffer), Placeholders::GENERIC);
    }

    // === RENDER COLUMNS ===

    // Add LABEL column if visible
    if (shouldShowColumn(COL_LABEL)) {
        addBackgroundQuad(currentX, quadY, columnQuadWidth, quadHeight);
        float labelX = (visibleCount == 1)
            ? currentX + columnQuadWidth / 2.0f
            : currentX + columnQuadWidth - dim.paddingH;
        int labelJustify = (visibleCount == 1) ? Justify::CENTER : Justify::RIGHT;
        addString(labelBuffer, labelX, textY, labelJustify,
            Fonts::ENTER_SANSMAN, ColorConfig::getInstance().getPrimary(), dim.fontSizeLarge);
        currentX += columnQuadWidth + charGap;
    }

    // Add TIME column if visible
    if (shouldShowColumn(COL_TIME)) {
        addBackgroundQuad(currentX, quadY, columnQuadWidth, quadHeight);
        float timeX = currentX + columnQuadWidth / 2.0f;
        addString(timeBuffer, timeX, textY, Justify::CENTER,
            Fonts::ENTER_SANSMAN, ColorConfig::getInstance().getPrimary(), dim.fontSizeLarge);
        currentX += columnQuadWidth + charGap;
    }

    // Add GAP column if visible (with colored background)
    if (shouldShowColumn(COL_GAP)) {
        SPluginQuad_t gapQuad;
        float gapQuadX = currentX;
        float gapQuadY = quadY;
        applyOffset(gapQuadX, gapQuadY);
        setQuadPositions(gapQuad, gapQuadX, gapQuadY, columnQuadWidth, quadHeight);
        gapQuad.m_iSprite = SpriteIndex::SOLID_COLOR;

        unsigned long baseColor;
        if (gapIsFaster) {
            baseColor = ColorConfig::getInstance().getPositive();
        } else if (gapIsSlower) {
            baseColor = ColorConfig::getInstance().getNegative();
        } else {
            baseColor = ColorConfig::getInstance().getBackground();
        }
        gapQuad.m_ulColor = PluginUtils::applyOpacity(baseColor, m_fBackgroundOpacity);
        m_quads.push_back(gapQuad);

        float gapX = (visibleCount == 1)
            ? currentX + columnQuadWidth / 2.0f
            : currentX + dim.paddingH;
        int gapJustify = (visibleCount == 1) ? Justify::CENTER : Justify::LEFT;

        // Use colored text for gap (green for faster, red for slower)
        unsigned long gapTextColor;
        if (gapIsFaster) {
            gapTextColor = ColorConfig::getInstance().getPositive();
        } else if (gapIsSlower) {
            gapTextColor = ColorConfig::getInstance().getNegative();
        } else {
            gapTextColor = ColorConfig::getInstance().getPrimary();
        }
        addString(gapBuffer, gapX, textY, gapJustify,
            Fonts::ENTER_SANSMAN, gapTextColor, dim.fontSizeLarge);
        currentX += columnQuadWidth;
    }

    // Set bounds
    rightX = currentX;
    float bottomY = quadY + quadHeight;
    setBounds(leftX, quadY, rightX, bottomY);
}

void TimingHud::resetToDefaults() {
    m_bVisible = true;
    m_bShowTitle = false;
    m_bShowBackgroundTexture = false;
    m_fBackgroundOpacity = 0.1f;
    m_fScale = 1.0f;
    setPosition(0.0f, 0.0f);

    // Reset column modes to defaults
    m_columnModes[COL_LABEL] = ColumnMode::SPLITS;
    m_columnModes[COL_TIME] = ColumnMode::ALWAYS;
    m_columnModes[COL_GAP] = ColumnMode::SPLITS;

    m_displayDurationMs = DEFAULT_DURATION_MS;

    // Reset live timing state
    resetLiveTimingState();

    setDataDirty();
}
