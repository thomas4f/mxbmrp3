// ============================================================================
// hud/timing_widget.cpp
// Timing widget - displays accumulated split and lap times as they happen
// Shows accumulated times and gaps in center of screen for 3 seconds
// Example: S1: 30.00s, S2: 60.00s (accumulated), Lap: 90.00s
// ============================================================================
#include "timing_widget.h"

#include <cstdio>
#include <cmath>
#include <string>

#include "../diagnostics/logger.h"
#include "../diagnostics/timer.h"
#include "../core/plugin_utils.h"
#include "../core/widget_constants.h"

using namespace PluginConstants;
using namespace CenterDisplayPositions;

TimingWidget::TimingWidget()
    : m_cachedSplit1(-1)
    , m_cachedSplit2(-1)
    , m_cachedLastCompletedLapNum(-1)
    , m_cachedDisplayRaceNum(-1)
    , m_displayedTime(-1)
    , m_bestTime(-1)
    , m_previousBestTime(-1)
    , m_splitType(SplitType::SPLIT_1)
    , m_displayedLapNum(-1)
    , m_bIsDisplaying(false)
    , m_bIsInvalidLap(false)
{
    // NOTE: Does not use initializeWidget() helper due to special requirements:
    // - Non-draggable (center display position)
    // - Requires quad reservation (timing widgets need background quads)
    // This is an intentional design decision - see base_hud.h initializeWidget() docs
    DEBUG_INFO("TimingWidget created");
    setDraggable(false);  // Center display shouldn't be draggable

    // Set defaults to match user configuration
    m_bShowTitle = false;  // No title displayed (consistent with BarsWidget)
    m_fBackgroundOpacity = 0.1f;

    // Pre-allocate vectors
    m_quads.reserve(4);    // Four background quads (reverse + label + time + gap)
    m_strings.reserve(4);  // Reverse notice + label + time + gap

    rebuildRenderData();
}

bool TimingWidget::handlesDataType(DataChangeType dataType) const {
    return dataType == DataChangeType::SessionBest ||
           dataType == DataChangeType::SpectateTarget;
}

void TimingWidget::update() {
    const PluginData& pluginData = PluginData::getInstance();

    // Detect spectate target changes and reset caches
    int currentDisplayRaceNum = pluginData.getDisplayRaceNum();
    if (currentDisplayRaceNum != m_cachedDisplayRaceNum) {
        // Spectate target changed - reset all cached values
        m_cachedSplit1 = -1;
        m_cachedSplit2 = -1;
        m_cachedLastCompletedLapNum = -1;
        m_cachedDisplayRaceNum = currentDisplayRaceNum;

        // Hide any current display
        if (m_bIsDisplaying) {
            m_bIsDisplaying = false;
            setDataDirty();
        }

        // Update cached values with new rider's current data (without displaying)
        const CurrentLapData* currentLap = pluginData.getCurrentLapData();
        const SessionBestData* sessionBest = pluginData.getSessionBestData();
        if (currentLap) {
            m_cachedSplit1 = currentLap->split1;
            m_cachedSplit2 = currentLap->split2;
        }
        if (sessionBest) {
            m_cachedLastCompletedLapNum = sessionBest->lastCompletedLapNum;
        }
    }

    // Process any split/lap completion updates
    processTimingUpdates();

    // Check if we should still be displaying
    if (m_bIsDisplaying && !shouldDisplayTime()) {
        m_bIsDisplaying = false;
        setDataDirty();
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

void TimingWidget::processTimingUpdates() {
    const PluginData& pluginData = PluginData::getInstance();
    const CurrentLapData* currentLap = pluginData.getCurrentLapData();
    const SessionBestData* sessionBest = pluginData.getSessionBestData();
    const LapLogEntry* personalBest = pluginData.getBestLapEntry();

    int newTime = -1;
    int bestTime = -1;
    int previousBestTime = -1;

    // Check current lap splits (CurrentLapData tracks accumulated times for current lap)
    if (currentLap) {
        // Check split 1 (accumulated time to S1)
        if (currentLap->split1 > 0 && currentLap->split1 != m_cachedSplit1) {
            newTime = currentLap->split1;  // Display accumulated time to S1
            // Compare against PB lap's accumulated time to S1 (which is just sector1)
            bestTime = personalBest ? personalBest->sector1 : -1;
            previousBestTime = sessionBest ? sessionBest->previousBestSector1 : -1;
            m_cachedSplit1 = currentLap->split1;
            m_splitType = SplitType::SPLIT_1;
            m_displayedLapNum = currentLap->lapNum;  // Capture lap number from current lap
            m_bIsInvalidLap = false;  // Splits are always valid (can't know until lap completes)
            DEBUG_INFO_F("TimingWidget: Split 1 crossed, accumulated=%d ms, PB S1 accumulated=%d ms, lap=%d", newTime, bestTime, currentLap->lapNum);
        }
        // Check split 2 (accumulated time to S2)
        else if (currentLap->split2 > 0 && currentLap->split2 != m_cachedSplit2) {
            // Display accumulated time to S2 (not sector time)
            newTime = currentLap->split2;
            // Compare against PB lap's accumulated time to S2 (sector1 + sector2)
            if (personalBest && personalBest->sector1 > 0 && personalBest->sector2 > 0) {
                bestTime = personalBest->sector1 + personalBest->sector2;
            } else {
                bestTime = -1;
            }
            // Previous best accumulated to S2
            if (sessionBest && sessionBest->previousBestSector1 > 0 && sessionBest->previousBestSector2 > 0) {
                previousBestTime = sessionBest->previousBestSector1 + sessionBest->previousBestSector2;
            } else {
                previousBestTime = -1;
            }
            m_cachedSplit2 = currentLap->split2;
            m_splitType = SplitType::SPLIT_2;
            m_displayedLapNum = currentLap->lapNum;  // Capture lap number from current lap
            m_bIsInvalidLap = false;  // Splits are always valid (can't know until lap completes)
            DEBUG_INFO_F("TimingWidget: Split 2 crossed, accumulated=%d ms, PB S2 accumulated=%d ms, lap=%d", newTime, bestTime, currentLap->lapNum);
        }
    }

    // Check for lap completion (split 3 / finish line)
    // Detect via lastCompletedLapNum to catch ALL laps (including invalid ones with no timing)
    if (sessionBest && sessionBest->lastCompletedLapNum >= 0 &&
        sessionBest->lastCompletedLapNum != m_cachedLastCompletedLapNum) {

        // Display lap time (0 for invalid laps without timing data - will show placeholder)
        newTime = sessionBest->lastLapTime;

        // Compare against PB lap time (consistent with session_best_hud)
        bestTime = personalBest ? personalBest->lapTime : -1;
        previousBestTime = sessionBest->previousBestLapTime;

        // Check if this lap was valid by looking at the lap log
        bool isValid = true;  // Default to valid
        const std::vector<LapLogEntry>* lapLog = pluginData.getLapLog();
        if (lapLog && !lapLog->empty()) {
            // Most recent lap is at index 0 (lap log inserts at front)
            const LapLogEntry& mostRecentLap = (*lapLog)[0];
            isValid = mostRecentLap.isValid;
            // Only update lap number if we got a valid one from lap log
            // Otherwise keep the lap number captured from the splits
            if (mostRecentLap.lapNum >= 0) {
                m_displayedLapNum = mostRecentLap.lapNum;
            }
        }

        m_cachedLastCompletedLapNum = sessionBest->lastCompletedLapNum;
        // Reset split caches for next lap
        m_cachedSplit1 = -1;
        m_cachedSplit2 = -1;
        m_splitType = SplitType::LAP;
        DEBUG_INFO_F("TimingWidget: Lap completed, time=%d ms, PB lap=%d ms, valid=%d, lap=%d", newTime, bestTime, isValid, m_displayedLapNum);

        // Store validity for display
        m_bIsInvalidLap = !isValid;

        // Trigger display (even for invalid laps with no timing)
        m_displayedTime = newTime;
        m_bestTime = bestTime;
        m_previousBestTime = previousBestTime;
        m_displayStartTime = std::chrono::steady_clock::now();
        m_bIsDisplaying = true;
        setDataDirty();
    }

    // If we detected a new split time, start displaying it
    if (newTime > 0 && m_splitType != SplitType::LAP) {
        m_displayedTime = newTime;
        m_bestTime = bestTime;
        m_previousBestTime = previousBestTime;
        m_displayStartTime = std::chrono::steady_clock::now();
        m_bIsDisplaying = true;
        setDataDirty();
    }
}

bool TimingWidget::shouldDisplayTime() const {
    if (!m_bIsDisplaying) {
        return false;
    }

    auto now = std::chrono::steady_clock::now();
    auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(now - m_displayStartTime).count();

    return elapsed < DISPLAY_DURATION_MS;
}

int TimingWidget::calculateGapToBest(int currentTime, int bestTime) const {
    if (currentTime <= 0) {
        return 0;  // No gap to calculate
    }

    // If bestTime is invalid, no comparison
    if (bestTime <= 0) {
        return 0;
    }

    int diff = currentTime - bestTime;

    // If diff is exactly zero, this is a new PB
    // Compare against previous PB instead to show improvement
    if (diff == 0 && m_previousBestTime > 0) {
        return currentTime - m_previousBestTime;
    }

    return diff;
}

void TimingWidget::rebuildLayout() {
    // Fast path - only update positions (not colors/opacity)
    if (m_quads.empty()) {
        setBounds(0.0f, 0.0f, 0.0f, 0.0f);
        return;
    }

    auto dim = getScaledDimensions();

    // All text uses large font (12 chars each)
    float labelTextWidth = PluginUtils::calculateMonospaceTextWidth(WidgetDimensions::STANDARD_WIDTH, dim.fontSizeLarge);
    float timeTextWidth = PluginUtils::calculateMonospaceTextWidth(WidgetDimensions::STANDARD_WIDTH, dim.fontSizeLarge);
    float gapTextWidth = PluginUtils::calculateMonospaceTextWidth(WidgetDimensions::STANDARD_WIDTH, dim.fontSizeLarge);
    float charGap = PluginUtils::calculateMonospaceTextWidth(1, dim.fontSizeLarge);

    // Quad dimensions (use font size for height, not line height, half padding)
    float labelQuadWidth = dim.paddingH + labelTextWidth + dim.paddingH;
    float timeQuadWidth = dim.paddingH + timeTextWidth + dim.paddingH;
    float gapQuadWidth = dim.paddingH + gapTextWidth + dim.paddingH;
    float quadHeight = dim.paddingV + dim.fontSizeLarge;

    // Calculate total width of all three quads (label + time + gap) for centering
    float totalWidth = labelQuadWidth + charGap + timeQuadWidth + charGap + gapQuadWidth;

    // Label + Time + Gap (all centered as a group)
    // Top edge anchored at divider line (grows down)
    float labelQuadX = CENTER_X - totalWidth / 2.0f;
    float labelQuadY = TIMING_DIVIDER_Y + DIVIDER_GAP;
    float labelY = labelQuadY + dim.paddingV * 0.5f;

    float timeQuadX = labelQuadX + labelQuadWidth + charGap;
    float timeQuadY = TIMING_DIVIDER_Y + DIVIDER_GAP;
    float timeY = timeQuadY + dim.paddingV * 0.5f;

    float gapQuadX = timeQuadX + timeQuadWidth + charGap;
    float gapQuadY = timeQuadY;
    float gapY = gapQuadY + dim.paddingV * 0.5f;

    // Update quad positions
    if (m_quads.size() > 0) {
        setQuadPositions(m_quads[0], labelQuadX, labelQuadY, labelQuadWidth, quadHeight);
    }
    if (m_quads.size() > 1) {
        setQuadPositions(m_quads[1], timeQuadX, timeQuadY, timeQuadWidth, quadHeight);
    }
    if (m_quads.size() > 2) {
        setQuadPositions(m_quads[2], gapQuadX, gapQuadY, gapQuadWidth, quadHeight);
    }

    // Update string positions
    if (m_strings.size() > 0) {
        float labelX = labelQuadX + labelQuadWidth - dim.paddingH;
        applyOffset(labelX, labelY);
        m_strings[0].m_afPos[0] = labelX;
        m_strings[0].m_afPos[1] = labelY;
    }
    if (m_strings.size() > 1) {
        float timeX = timeQuadX + timeQuadWidth / 2.0f;
        applyOffset(timeX, timeY);
        m_strings[1].m_afPos[0] = timeX;
        m_strings[1].m_afPos[1] = timeY;
    }
    if (m_strings.size() > 2) {
        float gapX = gapQuadX + dim.paddingH;
        applyOffset(gapX, gapY);
        m_strings[2].m_afPos[0] = gapX;
        m_strings[2].m_afPos[1] = gapY;
    }

    // Set bounds
    float leftX = labelQuadX;
    float rightX = gapQuadX + gapQuadWidth;
    float bottomY = timeQuadY + quadHeight;
    setBounds(leftX, labelQuadY, rightX, bottomY);
}

void TimingWidget::rebuildRenderData() {
    // Clear render data
    m_strings.clear();
    m_quads.clear();

    // Only render when displaying a split/lap time
    if (!m_bIsDisplaying) {
        setBounds(0.0f, 0.0f, 0.0f, 0.0f);
        return;
    }

    auto dim = getScaledDimensions();

    // All text uses large font (12 chars each)
    float labelTextWidth = PluginUtils::calculateMonospaceTextWidth(WidgetDimensions::STANDARD_WIDTH, dim.fontSizeLarge);
    float timeTextWidth = PluginUtils::calculateMonospaceTextWidth(WidgetDimensions::STANDARD_WIDTH, dim.fontSizeLarge);
    float gapTextWidth = PluginUtils::calculateMonospaceTextWidth(WidgetDimensions::STANDARD_WIDTH, dim.fontSizeLarge);
    float charGap = PluginUtils::calculateMonospaceTextWidth(1, dim.fontSizeLarge);

    // Quad dimensions (use font size for height, not line height, half padding)
    float labelQuadWidth = dim.paddingH + labelTextWidth + dim.paddingH;
    float timeQuadWidth = dim.paddingH + timeTextWidth + dim.paddingH;
    float gapQuadWidth = dim.paddingH + gapTextWidth + dim.paddingH;
    float quadHeight = dim.paddingV + dim.fontSizeLarge;

    // Calculate total width of all three quads (label + time + gap) for centering
    float totalWidth = labelQuadWidth + charGap + timeQuadWidth + charGap + gapQuadWidth;

    // Label + Time + Gap (all centered as a group)
    // Top edge anchored at divider line (grows down)
    float labelQuadX = CENTER_X - totalWidth / 2.0f;
    float labelQuadY = TIMING_DIVIDER_Y + DIVIDER_GAP;
    float labelY = labelQuadY + dim.paddingV * 0.5f;

    float timeQuadX = labelQuadX + labelQuadWidth + charGap;
    float timeQuadY = TIMING_DIVIDER_Y + DIVIDER_GAP;
    float timeY = timeQuadY + dim.paddingV * 0.5f;

    float gapQuadX = timeQuadX + timeQuadWidth + charGap;
    float gapQuadY = timeQuadY;
    float gapY = gapQuadY + dim.paddingV * 0.5f;

    // Calculate gap to determine quad background color
    char gapBuffer[32];
    bool isFaster = false;
    bool isSlower = false;

    // If lap is invalid, show "INVALID" with red background
    if (m_bIsInvalidLap) {
        strcpy_s(gapBuffer, sizeof(gapBuffer), "INVALID");
        isSlower = true;  // Red background
    } else {
        int gap = calculateGapToBest(m_displayedTime, m_bestTime);

        if (gap == 0 || m_bestTime <= 0) {
            // No gap or no best time to compare - show placeholder
            strcpy_s(gapBuffer, sizeof(gapBuffer), Placeholders::GENERIC);
        } else {
            // Format gap as delta
            PluginUtils::formatTimeDiff(gapBuffer, sizeof(gapBuffer), gap);

            // Determine background color based on performance
            if (gap < 0) {
                isFaster = true;   // Green background
            } else if (gap > 0) {
                isSlower = true;   // Red background
            }
        }
    }

    // Add label quad (standard black background)
    addBackgroundQuad(labelQuadX, labelQuadY, labelQuadWidth, quadHeight);

    // Add time quad (standard black background)
    addBackgroundQuad(timeQuadX, timeQuadY, timeQuadWidth, quadHeight);

    // Add gap quad with colored background
    SPluginQuad_t gapQuad;
    float gapQuadXOffset = gapQuadX;
    float gapQuadYOffset = gapQuadY;
    applyOffset(gapQuadXOffset, gapQuadYOffset);
    setQuadPositions(gapQuad, gapQuadXOffset, gapQuadYOffset, gapQuadWidth, quadHeight);
    gapQuad.m_iSprite = SpriteIndex::SOLID_COLOR;

    // Set background color based on performance
    unsigned long baseColor;
    if (isFaster) {
        baseColor = SemanticColors::POSITIVE;  // Green for faster
    } else if (isSlower) {
        baseColor = SemanticColors::NEGATIVE;  // Red for slower/invalid
    } else {
        baseColor = TextColors::BACKGROUND;    // Black for neutral
    }

    gapQuad.m_ulColor = PluginUtils::applyOpacity(baseColor, m_fBackgroundOpacity);
    m_quads.push_back(gapQuad);

    // Format the label (Split 1, Split 2, or Lap x)
    char labelBuffer[16];
    if (m_splitType == SplitType::SPLIT_1) {
        strcpy_s(labelBuffer, sizeof(labelBuffer), "Split 1");
    } else if (m_splitType == SplitType::SPLIT_2) {
        strcpy_s(labelBuffer, sizeof(labelBuffer), "Split 2");
    } else {
        // LAP (game stores lap numbers 0-indexed, display as 1-indexed for user)
        if (m_displayedLapNum >= 0) {
            snprintf(labelBuffer, sizeof(labelBuffer), "Lap %d", m_displayedLapNum + 1);
        } else {
            strcpy_s(labelBuffer, sizeof(labelBuffer), "Lap -");
        }
    }

    // Add label string (right-aligned in label quad, white text)
    float labelX = labelQuadX + labelQuadWidth - dim.paddingH;
    addString(labelBuffer, labelX, labelY, Justify::RIGHT,
        Fonts::ENTER_SANSMAN, TextColors::PRIMARY, dim.fontSizeLarge);

    // Format the time (show placeholder if no timing data)
    char timeBuffer[32];
    bool hasTimingData = (m_displayedTime > 0);
    if (hasTimingData) {
        PluginUtils::formatLapTime(m_displayedTime, timeBuffer, sizeof(timeBuffer));
    } else {
        strcpy_s(timeBuffer, sizeof(timeBuffer), Placeholders::LAP_TIME);
    }

    // Add time string (centered in time quad, always primary color)
    float timeX = timeQuadX + timeQuadWidth / 2.0f;
    addString(timeBuffer, timeX, timeY, Justify::CENTER,
        Fonts::ENTER_SANSMAN, TextColors::PRIMARY, dim.fontSizeLarge);

    // Add gap string (left-aligned in gap quad, white text)
    float gapX = gapQuadX + dim.paddingH;
    addString(gapBuffer, gapX, gapY, Justify::LEFT,
        Fonts::ENTER_SANSMAN, TextColors::PRIMARY, dim.fontSizeLarge);

    // Set bounds for timing display
    float leftX = labelQuadX;
    float rightX = gapQuadX + gapQuadWidth;
    float bottomY = timeQuadY + quadHeight;
    setBounds(leftX, labelQuadY, rightX, bottomY);
}

void TimingWidget::resetToDefaults() {
    m_bVisible = true;
    m_bShowTitle = false;  // No title displayed (consistent with BarsWidget)
    m_bShowBackgroundTexture = false;  // No texture by default
    m_fBackgroundOpacity = 0.1f;
    m_fScale = 1.0f;
    setPosition(0.0f, 0.0f);
    setDataDirty();
}
