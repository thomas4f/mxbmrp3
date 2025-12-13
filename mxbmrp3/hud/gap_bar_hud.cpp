// ============================================================================
// hud/gap_bar_hud.cpp
// Gap Bar HUD - visualizes current lap progress vs best lap timing
// Shows a horizontal bar with current position, best lap marker, and live gap
// ============================================================================
#include "gap_bar_hud.h"

#include <cstdio>
#include <cmath>
#include <algorithm>

#include "../diagnostics/logger.h"
#include "../core/plugin_utils.h"
#include "../core/color_config.h"
#include "../core/widget_constants.h"

using namespace PluginConstants;

GapBarHud::GapBarHud()
    : m_bestLapTime(0)
    , m_hasBestLap(false)
    , m_currentTrackPos(0.0f)
    , m_currentLapNum(0)
    , m_observedLapStart(false)
    , m_cachedDisplayRaceNum(-1)
    , m_cachedSession(-1)
    , m_cachedPitState(-1)
    , m_cachedLastCompletedLapNum(-1)
    , m_cachedSplit1(-1)
    , m_cachedSplit2(-1)
    , m_bikeBrandColor(ColorPalette::WHITE)
    , m_isFrozen(false)
    , m_frozenGap(0)
    , m_frozenSplitIndex(-1)
    , m_freezeDurationMs(DEFAULT_FREEZE_MS)
    , m_showMarkers(true)
    , m_gapRangeMs(DEFAULT_RANGE_MS)
    , m_barWidthPercent(DEFAULT_WIDTH_PERCENT)
{
    DEBUG_INFO("GapBarHud created");
    setDraggable(true);

    // Default position: centered horizontally, above notices with matching gap
    // X offset is bar center (0.5 = screen center), Y is top edge
    // Y position gives 17px gap to notices (same as notices-to-timing gap at 1080p)
    setPosition(0.5f, 0.043f);

    // Set defaults
    m_bVisible = false;  // Disabled by default
    m_bShowTitle = false;
    m_fBackgroundOpacity = 0.1f;

    // Pre-allocate vectors
    m_quads.reserve(4);    // Background, progress bar, best lap marker
    m_strings.reserve(1);  // Gap text

    rebuildRenderData();
}

bool GapBarHud::handlesDataType(DataChangeType dataType) const {
    return dataType == DataChangeType::SessionBest ||
           dataType == DataChangeType::SpectateTarget ||
           dataType == DataChangeType::SessionData ||
           dataType == DataChangeType::Standings ||
           dataType == DataChangeType::LapLog;
}

void GapBarHud::update() {
    const PluginData& pluginData = PluginData::getInstance();
    const SessionData& sessionData = pluginData.getSessionData();

    // Detect session changes (new event) and reset state
    int currentSession = sessionData.session;
    const SessionBestData* sessionBest = pluginData.getSessionBestData();
    int currentLastCompletedLap = sessionBest ? sessionBest->lastCompletedLapNum : -1;

    bool sessionTypeChanged = (currentSession != m_cachedSession);
    bool sessionDataCleared = (m_cachedLastCompletedLapNum >= 0 && currentLastCompletedLap < 0);

    if (sessionTypeChanged || sessionDataCleared) {
        DEBUG_INFO_F("GapBarHud: Session reset detected (type changed: %d, data cleared: %d)",
            sessionTypeChanged, sessionDataCleared);
        resetTimingState();
        m_cachedSession = currentSession;
        m_cachedPitState = -1;
        setDataDirty();
    }

    // Detect spectate target changes and reset state
    int currentDisplayRaceNum = pluginData.getDisplayRaceNum();
    if (currentDisplayRaceNum != m_cachedDisplayRaceNum) {
        DEBUG_INFO_F("GapBarHud: Spectate target changed from %d to %d",
            m_cachedDisplayRaceNum, currentDisplayRaceNum);

        // Full reset on spectate change
        resetTimingState();
        m_cachedDisplayRaceNum = currentDisplayRaceNum;
        m_cachedPitState = -1;

        // Update cached values with new rider's current data (without triggering display)
        // This prevents stale splits from the previous rider triggering freeze
        const CurrentLapData* currentLap = pluginData.getCurrentLapData();
        if (currentLap) {
            m_cachedSplit1 = currentLap->split1;
            m_cachedSplit2 = currentLap->split2;
        }
        if (sessionBest) {
            m_cachedLastCompletedLapNum = sessionBest->lastCompletedLapNum;
        }

        // Get bike brand color for the new target
        const RaceEntryData* entry = pluginData.getRaceEntry(currentDisplayRaceNum);
        if (entry) {
            m_bikeBrandColor = entry->bikeBrandColor;
        }

        setDataDirty();
    }

    // Detect pit entry/exit and reset anchor (but keep best lap data)
    const StandingsData* standing = pluginData.getStanding(currentDisplayRaceNum);
    if (standing) {
        int currentPitState = standing->pit;
        if (m_cachedPitState != -1 && currentPitState != m_cachedPitState) {
            DEBUG_INFO_F("GapBarHud: Pit state changed from %d to %d",
                m_cachedPitState, currentPitState);
            // Soft reset - clear current lap timing but keep best lap data
            m_anchor.reset();
            m_trackMonitor.reset();
            m_currentLapTimingPoints.fill(BestLapTimingPoint());
            setDataDirty();
        }
        m_cachedPitState = currentPitState;
    }

    // Process split updates (like TimingHud's processTimingUpdates)
    processSplitUpdates();

    // Check if freeze period has expired
    checkFreezeExpiration();

    // Check for lap completion - mirrors TimingHud's processTimingUpdates() logic
    if (sessionBest && sessionBest->lastCompletedLapNum >= 0 &&
        sessionBest->lastCompletedLapNum != m_cachedLastCompletedLapNum) {

        // Check if this lap was a PB and save timing data
        checkAndSavePreviousLap();

        const LapLogEntry* personalBest = pluginData.getBestLapEntry();
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

        // Calculate gap (like TimingHud)
        int gap = 0;
        if (isValid && lapTime > 0) {
            gap = (lapTime > 0 && bestTime > 0) ? lapTime - bestTime : 0;
            if (gap == 0 && previousBestTime > 0) {
                gap = lapTime - previousBestTime;  // New PB - compare to previous
            }
        }

        // Freeze to show official gap (if freeze is enabled)
        if (m_freezeDurationMs > 0) {
            m_frozenGap = gap;
            m_frozenSplitIndex = -1;  // -1 = lap complete
            m_isFrozen = true;
            m_frozenAt = std::chrono::steady_clock::now();
        }

        // Reset anchor for new lap (like TimingHud does at line 279)
        m_anchor.set();
        m_currentLapNum = completedLapNum + 1;

        // Clear timing points for next lap
        m_currentLapTimingPoints.fill(BestLapTimingPoint());

        // Reset split cache for new lap
        m_cachedSplit1 = -1;
        m_cachedSplit2 = -1;

        m_cachedLastCompletedLapNum = sessionBest->lastCompletedLapNum;
        setDataDirty();
    }

    // Rate-limited updates for smooth animation
    auto now = std::chrono::steady_clock::now();
    auto sinceLastUpdate = std::chrono::duration_cast<std::chrono::milliseconds>(
        now - m_lastUpdate).count();

    if (sinceLastUpdate >= UPDATE_INTERVAL_MS) {
        m_lastUpdate = now;
        updateCurrentLapTiming();
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

void GapBarHud::updateTrackPosition(int raceNum, float trackPos, int lapNum) {
    // Only process for the rider we're currently displaying
    if (raceNum != m_cachedDisplayRaceNum) {
        return;
    }

    m_currentTrackPos = trackPos;

    if (!m_trackMonitor.initialized) {
        m_trackMonitor.lastTrackPos = trackPos;
        m_trackMonitor.lastLapNum = lapNum;
        m_trackMonitor.initialized = true;
        // Don't set anchor here - wait for S/F crossing or lap completion
        // This prevents pit-to-S/F time from counting as lap timing
        return;
    }

    float delta = trackPos - m_trackMonitor.lastTrackPos;

    // Detect S/F crossing: large negative delta (0.95 -> 0.05 gives delta ~ -0.9)
    // Like TimingHud, just set anchor here - lap completion handles timing point management
    if (delta < -GapBarTrackMonitor::WRAP_THRESHOLD) {
        if (!m_anchor.valid || lapNum != m_trackMonitor.lastLapNum) {
            m_anchor.set();
            m_currentLapNum = lapNum;
            m_observedLapStart = true;  // We saw the lap start at S/F
        }
    }

    m_trackMonitor.lastTrackPos = trackPos;
    m_trackMonitor.lastLapNum = lapNum;
}

void GapBarHud::checkAndSavePreviousLap() {
    const PluginData& pluginData = PluginData::getInstance();
    const SessionBestData* sessionBest = pluginData.getSessionBestData();
    const LapLogEntry* personalBest = pluginData.getBestLapEntry();

    // Check if this lap was a PB
    if (personalBest && sessionBest && sessionBest->lastLapTime > 0 &&
        sessionBest->lastLapTime == personalBest->lapTime) {

        // Only save timing data if we observed the lap start at S/F
        // This prevents saving partial data when joining mid-lap
        if (m_observedLapStart) {
            DEBUG_INFO_F("GapBarHud: New PB! Lap time: %d ms", sessionBest->lastLapTime);
            m_bestLapTimingPoints = m_currentLapTimingPoints;
            m_bestLapTime = sessionBest->lastLapTime;
            m_hasBestLap = true;
        }
    }
}

void GapBarHud::updateCurrentLapTiming() {
    if (!m_anchor.valid) {
        return;
    }

    // Calculate current timing point index
    int positionIndex = static_cast<int>(m_currentTrackPos * static_cast<float>(NUM_TIMING_POINTS));
    positionIndex = std::max(0, std::min(positionIndex, NUM_TIMING_POINTS - 1));

    // Store current elapsed time at this position
    int elapsedTime = m_anchor.getElapsedMs();
    m_currentLapTimingPoints[positionIndex] = BestLapTimingPoint(elapsedTime);
}

void GapBarHud::processSplitUpdates() {
    const PluginData& pluginData = PluginData::getInstance();
    const CurrentLapData* currentLap = pluginData.getCurrentLapData();
    const SessionBestData* sessionBest = pluginData.getSessionBestData();
    const LapLogEntry* personalBest = pluginData.getBestLapEntry();

    if (!currentLap) return;

    // Check split 1 (accumulated time to S1)
    if (currentLap->split1 > 0 && currentLap->split1 != m_cachedSplit1) {
        int splitTime = currentLap->split1;
        int bestTime = personalBest ? personalBest->sector1 : -1;
        int previousBestTime = sessionBest ? sessionBest->previousBestSector1 : -1;

        // Calculate gap (like TimingHud::calculateGapToBest)
        int gap = (splitTime > 0 && bestTime > 0) ? splitTime - bestTime : 0;
        if (gap == 0 && previousBestTime > 0) {
            gap = splitTime - previousBestTime;  // New PB - compare to previous
        }

        // Freeze to show official gap (if freeze is enabled)
        if (m_freezeDurationMs > 0) {
            m_frozenGap = gap;
            m_frozenSplitIndex = 0;  // S1
            m_isFrozen = true;
            m_frozenAt = std::chrono::steady_clock::now();
        }

        // Resync anchor with official split time (keeps live gap accurate)
        m_anchor.set(splitTime);

        m_cachedSplit1 = currentLap->split1;
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
        int gap = (splitTime > 0 && bestTime > 0) ? splitTime - bestTime : 0;
        if (gap == 0 && previousBestTime > 0) {
            gap = splitTime - previousBestTime;
        }

        // Freeze to show official gap (if freeze is enabled)
        if (m_freezeDurationMs > 0) {
            m_frozenGap = gap;
            m_frozenSplitIndex = 1;  // S2
            m_isFrozen = true;
            m_frozenAt = std::chrono::steady_clock::now();
        }

        // Resync anchor with official split time (keeps live gap accurate)
        m_anchor.set(splitTime);

        m_cachedSplit2 = currentLap->split2;
        setDataDirty();
    }
}

void GapBarHud::checkFreezeExpiration() {
    if (!m_isFrozen) return;

    auto now = std::chrono::steady_clock::now();
    auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
        now - m_frozenAt
    ).count();

    if (elapsed >= m_freezeDurationMs) {
        m_isFrozen = false;
        setDataDirty();
    }
}

int GapBarHud::calculateCurrentGap() const {
    if (!m_hasBestLap || !m_anchor.valid) {
        return 0;
    }

    // Calculate exact position index (floating point for interpolation)
    float exactIndex = m_currentTrackPos * static_cast<float>(NUM_TIMING_POINTS);
    int lowerIndex = static_cast<int>(exactIndex);
    int upperIndex = lowerIndex + 1;
    float fraction = exactIndex - static_cast<float>(lowerIndex);

    // Clamp indices to valid range
    lowerIndex = std::max(0, std::min(lowerIndex, NUM_TIMING_POINTS - 1));
    upperIndex = std::max(0, std::min(upperIndex, NUM_TIMING_POINTS - 1));

    // Get timing points for interpolation
    const BestLapTimingPoint& lowerPoint = m_bestLapTimingPoints[lowerIndex];
    const BestLapTimingPoint& upperPoint = m_bestLapTimingPoints[upperIndex];

    // Find valid timing points, searching backward if needed
    int bestLapTime = 0;
    if (lowerPoint.valid && upperPoint.valid) {
        // Both valid - interpolate for smooth gap
        bestLapTime = lowerPoint.elapsedTime +
            static_cast<int>(fraction * static_cast<float>(upperPoint.elapsedTime - lowerPoint.elapsedTime));
    } else if (lowerPoint.valid) {
        // Only lower valid - use it directly
        bestLapTime = lowerPoint.elapsedTime;
    } else if (upperPoint.valid) {
        // Only upper valid - use it directly
        bestLapTime = upperPoint.elapsedTime;
    } else {
        // Neither valid - search backward for any valid point
        for (int offset = 1; offset < 10; offset++) {
            int searchIdx = lowerIndex - offset;
            if (searchIdx >= 0 && m_bestLapTimingPoints[searchIdx].valid) {
                bestLapTime = m_bestLapTimingPoints[searchIdx].elapsedTime;
                break;
            }
        }
        if (bestLapTime == 0) {
            return 0;  // No valid timing data found
        }
    }

    // Current lap elapsed time
    int currentElapsed = m_anchor.getElapsedMs();

    // Gap = current - best (positive = slower/behind, negative = faster/ahead)
    return currentElapsed - bestLapTime;
}

float GapBarHud::calculateBestLapProgress() const {
    if (!m_hasBestLap || !m_anchor.valid || m_bestLapTime <= 0) {
        return -1.0f;  // Invalid - don't show marker
    }

    // How far into the current lap are we (by time)?
    int currentElapsed = m_anchor.getElapsedMs();

    // What position would we be at on the best lap at this elapsed time?
    // Search through best lap timing points to find matching position
    for (int i = 0; i < NUM_TIMING_POINTS; i++) {
        if (m_bestLapTimingPoints[i].valid &&
            m_bestLapTimingPoints[i].elapsedTime >= currentElapsed) {
            // Found the first timing point where best lap elapsed time >= current elapsed
            // Interpolate for smooth marker movement
            if (i > 0 && m_bestLapTimingPoints[i - 1].valid) {
                int prevTime = m_bestLapTimingPoints[i - 1].elapsedTime;
                int thisTime = m_bestLapTimingPoints[i].elapsedTime;
                if (thisTime > prevTime) {
                    float fraction = static_cast<float>(currentElapsed - prevTime) /
                                   static_cast<float>(thisTime - prevTime);
                    return (static_cast<float>(i - 1) + fraction) / static_cast<float>(NUM_TIMING_POINTS);
                }
            }
            return static_cast<float>(i) / static_cast<float>(NUM_TIMING_POINTS);
        }
    }

    // Current elapsed exceeds best lap time - marker would be past finish
    // Clamp to end of bar
    return 1.0f;
}

void GapBarHud::resetTimingState() {
    m_anchor.reset();
    m_trackMonitor.reset();
    m_hasBestLap = false;
    m_bestLapTime = 0;
    m_currentTrackPos = 0.0f;
    m_currentLapNum = 0;
    m_observedLapStart = false;
    m_cachedLastCompletedLapNum = -1;
    m_cachedSplit1 = -1;
    m_cachedSplit2 = -1;
    m_isFrozen = false;
    m_frozenGap = 0;
    m_frozenSplitIndex = -1;
    m_bestLapTimingPoints.fill(BestLapTimingPoint());
    m_currentLapTimingPoints.fill(BestLapTimingPoint());
}

void GapBarHud::rebuildLayout() {
    // Layout changes require full rebuild
    rebuildRenderData();
}

void GapBarHud::rebuildRenderData() {
    m_strings.clear();
    m_quads.clear();

    // Get scaled dimensions
    auto dim = getScaledDimensions();

    // Calculate bar dimensions to match notices widget:
    // Width: STANDARD_WIDTH (12 chars) in fontSizeLarge + paddingH on each side
    // Height: paddingV + fontSizeLarge (same as notices widget)
    float textWidth = PluginUtils::calculateMonospaceTextWidth(
        WidgetDimensions::STANDARD_WIDTH, dim.fontSizeLarge);
    float baseBarWidth = dim.paddingH + textWidth + dim.paddingH;
    float barWidth = baseBarWidth * (static_cast<float>(m_barWidthPercent) / 100.0f);
    float barHeight = dim.paddingV + dim.fontSizeLarge;

    // Use minimal HUD padding (scaled for aspect ratio)
    float paddingH = dim.gridH(1) * HudSpacing::BG_PADDING_H_SCALE;  // 0.5 char widths
    float paddingV = dim.gridV(BAR_PADDING_V_SCALE);  // Quarter line height (compact)

    // Starting position - X is centered (offset from bar center), Y is top-aligned
    float startX = -barWidth / 2.0f;
    float startY = 0.0f;

    // ==== BACKGROUND QUAD (BACKGROUND color) ====
    SPluginQuad_t bgQuad;
    float bgX = startX;
    float bgY = startY;
    applyOffset(bgX, bgY);
    setQuadPositions(bgQuad, bgX, bgY, barWidth, barHeight);
    bgQuad.m_iSprite = SpriteIndex::SOLID_COLOR;
    bgQuad.m_ulColor = PluginUtils::applyOpacity(
        ColorConfig::getInstance().getBackground(), m_fBackgroundOpacity);
    m_quads.push_back(bgQuad);

    // Common inner dimensions
    float innerWidth = barWidth - paddingH * 2.0f;
    float innerHeight = barHeight - paddingV * 2.0f;
    float markerWidth = PluginUtils::calculateMonospaceTextWidth(1, dim.fontSize) * MARKER_WIDTH_CHARS;

    // ==== GAP BAR (grows from center based on live gap - never frozen) ====
    {
        // Always use live gap for the bar visualization
        int gap = 0;
        const LapLogEntry* personalBest = PluginData::getInstance().getBestLapEntry();

        if (m_hasBestLap && m_anchor.valid && personalBest) {
            gap = calculateCurrentGap();
        }

        // Calculate bar extent: gap / range = percentage of half-bar
        // Positive gap (behind) = grow left (red), negative gap (ahead) = grow right (green)
        float gapRatio = static_cast<float>(gap) / static_cast<float>(m_gapRangeMs);
        gapRatio = std::max(-1.0f, std::min(1.0f, gapRatio));  // Clamp to -1..1

        float halfWidth = innerWidth / 2.0f;
        float centerX = startX + paddingH + halfWidth;

        if (std::abs(gapRatio) > 0.001f) {
            SPluginQuad_t gapQuad;
            float quadX, quadWidth;

            if (gapRatio > 0.0f) {
                // Behind (slower) - grow left from center, red
                quadWidth = halfWidth * gapRatio;
                quadX = centerX - quadWidth;
                gapQuad.m_ulColor = PluginUtils::applyOpacity(
                    ColorConfig::getInstance().getNegative(), m_fBackgroundOpacity);
            } else {
                // Ahead (faster) - grow right from center, green
                quadWidth = halfWidth * (-gapRatio);
                quadX = centerX;
                gapQuad.m_ulColor = PluginUtils::applyOpacity(
                    ColorConfig::getInstance().getPositive(), m_fBackgroundOpacity);
            }

            float qY = startY + paddingV;
            applyOffset(quadX, qY);
            setQuadPositions(gapQuad, quadX, qY, quadWidth, innerHeight);
            gapQuad.m_iSprite = SpriteIndex::SOLID_COLOR;
            m_quads.push_back(gapQuad);
        }
    }

    // ==== BEST LAP MARKER (thin vertical bar, darkened bike brand color) ====
    if (m_showMarkers && m_hasBestLap && m_anchor.valid) {
        float bestLapProgress = calculateBestLapProgress();
        if (bestLapProgress >= 0.0f && bestLapProgress <= 1.0f) {
            float markerX = startX + paddingH + (innerWidth * bestLapProgress) - (markerWidth / 2.0f);

            // Clamp marker to inner bar bounds
            markerX = std::max(startX + paddingH, std::min(markerX, startX + barWidth - paddingH - markerWidth));

            SPluginQuad_t markerQuad;
            float mX = markerX;
            float mY = startY + paddingV;
            applyOffset(mX, mY);
            setQuadPositions(markerQuad, mX, mY, markerWidth, innerHeight);
            markerQuad.m_iSprite = SpriteIndex::SOLID_COLOR;

            // Use darkened bike brand color for best lap marker
            markerQuad.m_ulColor = PluginUtils::applyOpacity(
                PluginUtils::darkenColor(m_bikeBrandColor, 0.5f), 1.0f);
            m_quads.push_back(markerQuad);
        }
    }

    // ==== CURRENT POSITION MARKER (thin vertical bar, bike brand color) ====
    if (m_showMarkers && m_currentTrackPos > 0.001f) {
        float markerX = startX + paddingH + (innerWidth * m_currentTrackPos) - (markerWidth / 2.0f);

        // Clamp marker to inner bar bounds
        markerX = std::max(startX + paddingH, std::min(markerX, startX + barWidth - paddingH - markerWidth));

        SPluginQuad_t markerQuad;
        float mX = markerX;
        float mY = startY + paddingV;
        applyOffset(mX, mY);
        setQuadPositions(markerQuad, mX, mY, markerWidth, innerHeight);
        markerQuad.m_iSprite = SpriteIndex::SOLID_COLOR;

        // Use bike brand color for current position marker (full brightness)
        markerQuad.m_ulColor = PluginUtils::applyOpacity(m_bikeBrandColor, 1.0f);
        m_quads.push_back(markerQuad);
    }

    // ==== GAP TEXT (centered inside bar, primary color) ====
    // X: center of bar, Y: vertically centered within bar height
    float gapTextX = startX + barWidth / 2.0f;
    float gapTextY = startY + (barHeight - dim.fontSize) / 2.0f;

    char gapBuffer[32];
    unsigned long gapColor = ColorConfig::getInstance().getPrimary();

    // Only show gaps when we have a PB to compare against (like TimingHud)
    const LapLogEntry* personalBest = PluginData::getInstance().getBestLapEntry();

    if (m_isFrozen && personalBest) {
        // Show frozen official gap from split/lap crossing (full precision)
        PluginUtils::formatTimeDiff(gapBuffer, sizeof(gapBuffer), m_frozenGap);
    } else if (m_hasBestLap && m_anchor.valid && personalBest) {
        // Show live gap (compact format)
        int gap = calculateCurrentGap();
        PluginUtils::formatGapCompact(gapBuffer, sizeof(gapBuffer), gap);
    } else {
        // No best lap - show placeholder in primary color
        strcpy_s(gapBuffer, sizeof(gapBuffer), Placeholders::GENERIC);
    }

    // Gap text (monospace font, normal size, centered)
    addString(gapBuffer, gapTextX, gapTextY, Justify::CENTER,
              Fonts::ROBOTO_MONO, gapColor, dim.fontSize);

    // Set bounds for drag detection
    setBounds(startX, startY, startX + barWidth, startY + barHeight);
}

void GapBarHud::setScale(float scale) {
    if (scale <= 0.0f) scale = 0.1f;
    float oldScale = m_fScale;
    if (oldScale == scale) return;

    // Calculate current dimensions
    float oldWidth = m_fBoundsRight - m_fBoundsLeft;
    float oldHeight = m_fBoundsBottom - m_fBoundsTop;

    // Calculate new dimensions (scale changes proportionally)
    float ratio = scale / oldScale;
    float newWidth = oldWidth * ratio;
    float newHeight = oldHeight * ratio;

    // Adjust offset to keep center fixed
    float deltaX = (oldWidth - newWidth) / 2.0f;
    float deltaY = (oldHeight - newHeight) / 2.0f;
    setPosition(m_fOffsetX + deltaX, m_fOffsetY + deltaY);

    // Apply the new scale
    m_fScale = scale;
    setDataDirty();
}

void GapBarHud::setBarWidth(int percent) {
    // Clamp to valid range
    percent = std::max(MIN_WIDTH_PERCENT, std::min(percent, MAX_WIDTH_PERCENT));
    if (percent == m_barWidthPercent) return;

    // Apply the new width - no position adjustment needed since offset is bar center
    m_barWidthPercent = percent;
    setDataDirty();
}

void GapBarHud::resetToDefaults() {
    m_bVisible = false;  // Disabled by default
    m_bShowTitle = false;
    m_bShowBackgroundTexture = false;
    m_fBackgroundOpacity = 0.1f;
    m_fScale = 1.0f;
    setPosition(0.5f, 0.043f);

    // Settings
    m_freezeDurationMs = DEFAULT_FREEZE_MS;
    m_showMarkers = true;
    m_gapRangeMs = DEFAULT_RANGE_MS;
    m_barWidthPercent = DEFAULT_WIDTH_PERCENT;

    resetTimingState();
    setDataDirty();
}
