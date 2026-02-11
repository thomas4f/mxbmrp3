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
#include "../core/asset_manager.h"
#include "../core/tracked_riders_manager.h"

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
    , m_cachedPlayerRunning(true)
    , m_bikeBrandColor(ColorPalette::WHITE)
    , m_isFrozen(false)
    , m_frozenGap(0)
    , m_frozenSplitIndex(-1)
    , m_freezeDurationMs(DEFAULT_FREEZE_MS)
    , m_markerMode(MarkerMode::GHOST)
    , m_labelMode(LabelMode::NONE)
    , m_riderColorMode(RiderColorMode::RELATIVE_POS)
    , m_riderIconIndex(0)
    , m_showGapText(true)
    , m_showGapBar(true)
    , m_gapRangeMs(DEFAULT_RANGE_MS)
    , m_barWidthPercent(DEFAULT_WIDTH_PERCENT)
    , m_fMarkerScale(DEFAULT_MARKER_SCALE)
{
    // One-time setup
    DEBUG_INFO("GapBarHud created");
    setDraggable(true);
    m_quads.reserve(4);    // Background, progress bar, best lap marker
    m_strings.reserve(1);  // Gap text

    // Set texture base name for dynamic texture discovery
    setTextureBaseName("gap_bar_hud");

    // Set all configurable defaults
    resetToDefaults();

    rebuildRenderData();
}

bool GapBarHud::handlesDataType(DataChangeType dataType) const {
    return dataType == DataChangeType::IdealLap ||
           dataType == DataChangeType::SpectateTarget ||
           dataType == DataChangeType::SessionData ||
           dataType == DataChangeType::Standings ||
           dataType == DataChangeType::LapLog ||
           dataType == DataChangeType::TrackedRiders;
}

void GapBarHud::update() {
    // NOTE: State tracking runs even when not visible so live gap is published
    // to PluginData for LapLogHud. Only rendering is skipped when hidden.

    const PluginData& pluginData = PluginData::getInstance();
    const SessionData& sessionData = pluginData.getSessionData();

    // Handle pause/resume - sync anchor pause state with player running state
    // Only check pause when on track (spectate/replay don't have pause concept)
    bool playerRunning = pluginData.isPlayerRunning();
    bool onTrack = (pluginData.getDrawState() == PluginConstants::ViewState::ON_TRACK);
    if (onTrack && playerRunning != m_cachedPlayerRunning) {
        if (playerRunning) {
            m_anchor.resume();
        } else {
            m_anchor.pause();
        }
        m_cachedPlayerRunning = playerRunning;
    }

    // Detect session changes (new event) and reset state
    int currentSession = sessionData.session;
    const IdealLapData* idealLapData = pluginData.getIdealLapData();
    int currentLastCompletedLap = idealLapData ? idealLapData->lastCompletedLapNum : -1;

    bool sessionTypeChanged = (currentSession != m_cachedSession);
    bool sessionDataCleared = (m_cachedLastCompletedLapNum >= 0 && currentLastCompletedLap < 0);

    if (sessionTypeChanged || sessionDataCleared) {
        DEBUG_INFO_F("GapBarHud: Session reset detected (type changed: %d, data cleared: %d)",
            sessionTypeChanged, sessionDataCleared);
        resetTimingState();
        m_cachedSession = currentSession;
        m_cachedPitState = -1;
        if (isVisible()) setDataDirty();
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
        if (idealLapData) {
            m_cachedLastCompletedLapNum = idealLapData->lastCompletedLapNum;
        }

        // Get bike brand color for the new target
        const RaceEntryData* entry = pluginData.getRaceEntry(currentDisplayRaceNum);
        if (entry) {
            m_bikeBrandColor = entry->bikeBrandColor;
        }

        if (isVisible()) setDataDirty();
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
            if (isVisible()) setDataDirty();
        }
        m_cachedPitState = currentPitState;
    }

    // Process split updates (like TimingHud's processTimingUpdates)
    processSplitUpdates();

    // Check if freeze period has expired
    checkFreezeExpiration();

    // Check for lap completion - mirrors TimingHud's processTimingUpdates() logic
    if (idealLapData && idealLapData->lastCompletedLapNum >= 0 &&
        idealLapData->lastCompletedLapNum != m_cachedLastCompletedLapNum) {

        // Lap completion means S/F was crossed - mark as observed
        // This handles the race condition where lap completion callback fires
        // before track position callback (which normally sets this flag)
        m_observedLapStart = true;

        // Check if this lap was a PB and save timing data
        checkAndSavePreviousLap();

        const LapLogEntry* personalBest = pluginData.getBestLapEntry();
        int lapTime = idealLapData->lastLapTime;
        int bestTime = personalBest ? personalBest->lapTime : -1;
        int previousBestTime = idealLapData->previousBestLapTime;

        // Check if this lap was valid by looking at the lap log
        bool isValid = true;
        int completedLapNum = idealLapData->lastCompletedLapNum;
        const std::deque<LapLogEntry>* lapLog = pluginData.getLapLog();
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

        m_cachedLastCompletedLapNum = idealLapData->lastCompletedLapNum;
        if (isVisible()) setDataDirty();
    }

    // Rate-limited updates for smooth animation
    auto now = std::chrono::steady_clock::now();
    auto sinceLastUpdate = std::chrono::duration_cast<std::chrono::milliseconds>(
        now - m_lastUpdate).count();

    if (sinceLastUpdate >= UPDATE_INTERVAL_MS) {
        m_lastUpdate = now;
        updateCurrentLapTiming();

        // Publish live gap to PluginData for use by LapLogHud and other HUDs
        // This runs even when hidden so LapLogHud can display gap
        if (m_hasBestLap && m_anchor.valid) {
            m_cachedGap = calculateCurrentGap();
            m_cachedGapValid = true;
            PluginData::getInstance().setLiveGap(m_cachedGap, true);
        } else {
            m_cachedGap = 0;
            m_cachedGapValid = false;
            PluginData::getInstance().setLiveGap(0, false);
        }

        if (isVisible()) setDataDirty();
    }

    // OPTIMIZATION: Only process dirty flags and rebuild when visible
    if (isVisible()) {
        processDirtyFlags();
    } else {
        clearDataDirty();
        clearLayoutDirty();
    }
}

void GapBarHud::updateTrackPosition(int raceNum, float trackPos, int lapNum) {
    // NOTE: Track position updates always run (even when hidden) for gap tracking
    // Only process for the rider we're currently displaying
    if (raceNum != m_cachedDisplayRaceNum) {
        return;
    }

    // Clamp track position to valid range (defensive - API should provide valid values)
    trackPos = std::clamp(trackPos, 0.0f, 1.0f);
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
    const IdealLapData* idealLapData = pluginData.getIdealLapData();
    const LapLogEntry* personalBest = pluginData.getBestLapEntry();

    // Check if this lap was a PB
    if (personalBest && idealLapData && idealLapData->lastLapTime > 0 &&
        idealLapData->lastLapTime == personalBest->lapTime) {

        // Only save timing data if we observed the lap start at S/F
        // This prevents saving partial data when joining mid-lap
        if (m_observedLapStart) {
            DEBUG_INFO_F("GapBarHud: New PB! Lap time: %d ms", idealLapData->lastLapTime);
            m_bestLapTimingPoints = m_currentLapTimingPoints;
            m_bestLapTime = idealLapData->lastLapTime;
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
    const IdealLapData* idealLapData = pluginData.getIdealLapData();
    const LapLogEntry* personalBest = pluginData.getBestLapEntry();

    if (!currentLap) return;

    // Check split 1 (accumulated time to S1)
    if (currentLap->split1 > 0 && currentLap->split1 != m_cachedSplit1) {
        int splitTime = currentLap->split1;
        int bestTime = personalBest ? personalBest->sector1 : -1;
        int previousBestTime = idealLapData ? idealLapData->previousBestSector1 : -1;

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
        if (idealLapData && idealLapData->previousBestSector1 > 0 && idealLapData->previousBestSector2 > 0) {
            previousBestTime = idealLapData->previousBestSector1 + idealLapData->previousBestSector2;
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
    m_cachedPlayerRunning = true;
    m_isFrozen = false;
    m_frozenGap = 0;
    m_frozenSplitIndex = -1;
    m_cachedGap = 0;
    m_cachedGapValid = false;
    m_bestLapTimingPoints.fill(BestLapTimingPoint());
    m_currentLapTimingPoints.fill(BestLapTimingPoint());

    // Clear live gap in PluginData
    PluginData::getInstance().setLiveGap(0, false);
}

void GapBarHud::rebuildLayout() {
    // Layout changes require full rebuild
    rebuildRenderData();
}

void GapBarHud::rebuildRenderData() {
    clearStrings();
    m_quads.clear();

    // Get scaled dimensions
    auto dim = getScaledDimensions();

    // Calculate bar width to match Performance/Telemetry HUD full width (43 chars = 33 graph + 1 gap + 9 legend)
    // Use dim.fontSize (not fontSizeLarge) to match those HUDs exactly
    constexpr int BACKGROUND_WIDTH_CHARS = 43;
    float textWidth = PluginUtils::calculateMonospaceTextWidth(BACKGROUND_WIDTH_CHARS, dim.fontSize);
    float baseBarWidth = dim.paddingH + textWidth + dim.paddingH;
    float barWidth = baseBarWidth * (static_cast<float>(m_barWidthPercent) / 100.0f);
    float barHeight = dim.paddingV + dim.fontSizeLarge;

    // Use minimal HUD padding (scaled for aspect ratio)
    float paddingH = dim.gridH(1) * HudSpacing::BG_PADDING_H_SCALE;  // 0.5 char widths
    float paddingV = dim.gridV(BAR_PADDING_V_SCALE);  // Quarter line height (compact)

    // Starting position - X is centered (offset from bar center), Y is top-aligned
    float startX = -barWidth / 2.0f;
    float startY = 0.0f;

    // ==== BACKGROUND QUAD ====
    SPluginQuad_t bgQuad;
    float bgX = startX;
    float bgY = startY;
    applyOffset(bgX, bgY);
    setQuadPositions(bgQuad, bgX, bgY, barWidth, barHeight);

    // Check if background texture should be used
    if (m_bShowBackgroundTexture && m_iBackgroundTextureIndex > 0) {
        bgQuad.m_iSprite = m_iBackgroundTextureIndex;
        bgQuad.m_ulColor = PluginUtils::applyOpacity(ColorPalette::WHITE, m_fBackgroundOpacity);
    } else {
        bgQuad.m_iSprite = SpriteIndex::SOLID_COLOR;
        bgQuad.m_ulColor = PluginUtils::applyOpacity(
            this->getColor(ColorSlot::BACKGROUND), m_fBackgroundOpacity);
    }
    m_quads.push_back(bgQuad);

    // Common inner dimensions
    float innerWidth = barWidth - paddingH * 2.0f;
    float innerHeight = barHeight - paddingV * 2.0f;

    // ==== GAP BAR (grows from center based on live gap - never frozen) ====
    if (m_showGapBar) {
        // Always use live gap for the bar visualization (use cached value)
        int gap = 0;
        const LapLogEntry* personalBest = PluginData::getInstance().getBestLapEntry();

        if (m_cachedGapValid && personalBest) {
            gap = m_cachedGap;
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
                    this->getColor(ColorSlot::NEGATIVE), m_fBackgroundOpacity);
            } else {
                // Ahead (faster) - grow right from center, green
                quadWidth = halfWidth * (-gapRatio);
                quadX = centerX;
                gapQuad.m_ulColor = PluginUtils::applyOpacity(
                    this->getColor(ColorSlot::POSITIVE), m_fBackgroundOpacity);
            }

            float qY = startY + paddingV;
            applyOffset(quadX, qY);
            setQuadPositions(gapQuad, quadX, qY, quadWidth, innerHeight);
            gapQuad.m_iSprite = SpriteIndex::SOLID_COLOR;
            m_quads.push_back(gapQuad);
        }
    }

    // ==== RIDER MARKERS (icons instead of vertical bars) ====
    // Renders self, ghost, and/or opponents based on marker mode
    renderRiderMarkers(startX + paddingH, startY + paddingV, innerWidth, innerHeight, dim);

    // ==== GAP TEXT (centered inside bar, primary color) - conditionally rendered ====
    if (!m_showGapText) {
        // Skip gap text - user wants pure flat map mode
        setBounds(startX, startY, startX + barWidth, startY + barHeight);
        return;
    }

    // ==== GAP TEXT (centered inside bar, primary color) ====
    // X: center of bar, Y: vertically centered within bar height
    float gapTextX = startX + barWidth / 2.0f;
    float gapTextY = startY + (barHeight - dim.fontSize) / 2.0f;

    char gapBuffer[32];
    unsigned long gapColor = this->getColor(ColorSlot::PRIMARY);

    // Only show gaps when we have a PB to compare against (like TimingHud)
    const LapLogEntry* personalBest = PluginData::getInstance().getBestLapEntry();

    if (m_isFrozen && personalBest) {
        // Show frozen official gap from split/lap crossing (full precision)
        PluginUtils::formatTimeDiff(gapBuffer, sizeof(gapBuffer), m_frozenGap);
        // Colorize based on gap value: positive = slower (red), negative = faster (green)
        if (m_frozenGap > 0) {
            gapColor = this->getColor(ColorSlot::NEGATIVE);
        } else if (m_frozenGap < 0) {
            gapColor = this->getColor(ColorSlot::POSITIVE);
        }
    } else if (m_cachedGapValid && personalBest) {
        // Show live gap (full precision, use cached value)
        PluginUtils::formatTimeDiff(gapBuffer, sizeof(gapBuffer), m_cachedGap);
        // Colorize based on gap value: positive = slower (red), negative = faster (green)
        if (m_cachedGap > 0) {
            gapColor = this->getColor(ColorSlot::NEGATIVE);
        } else if (m_cachedGap < 0) {
            gapColor = this->getColor(ColorSlot::POSITIVE);
        }
    } else {
        // No best lap - show placeholder in primary color
        strcpy_s(gapBuffer, sizeof(gapBuffer), Placeholders::GENERIC);
    }

    // Gap text (monospace font, normal size, centered)
    addString(gapBuffer, gapTextX, gapTextY, Justify::CENTER,
              this->getFont(FontCategory::DIGITS), gapColor, dim.fontSize);

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
    setTextureVariant(0);  // No texture by default
    m_fBackgroundOpacity = 0.1f;
    m_fScale = 1.0f;
    setPosition(0.5f, 0.043f);

    // Settings
    m_freezeDurationMs = DEFAULT_FREEZE_MS;
    m_markerMode = MarkerMode::GHOST;  // Default to ghost-only (original behavior)
    m_labelMode = LabelMode::NONE;     // No labels by default (like MapHud default)
    m_riderColorMode = RiderColorMode::RELATIVE_POS;  // Default to position-based coloring
    m_riderIconIndex = 0;              // 0 = use default icon (circle-chevron-up)
    m_showGapText = true;              // Show gap text by default
    m_showGapBar = true;               // Show gap visualization bars by default
    m_gapRangeMs = DEFAULT_RANGE_MS;
    m_barWidthPercent = DEFAULT_WIDTH_PERCENT;
    m_fMarkerScale = DEFAULT_MARKER_SCALE;

    resetTimingState();
    setDataDirty();
}

// ============================================================================
// Rider position update for flat map mode
// ============================================================================
void GapBarHud::updateRiderPositions(int numVehicles, const Unified::TrackPositionData* positions) {
    if (numVehicles <= 0 || positions == nullptr) {
        m_riderPositions.clear();
        return;
    }

    // Only store if we're showing opponents
    if (m_markerMode == MarkerMode::OPPONENTS || m_markerMode == MarkerMode::GHOST_OPPONENTS) {
        m_riderPositions.assign(positions, positions + numVehicles);
        if (isVisible()) {
            setDataDirty();
        }
    }
}

// ============================================================================
// Calculate rider color based on color mode setting (like MapHud/RadarHud)
// ============================================================================
unsigned long GapBarHud::calculateRiderColor(int riderRaceNum, int displayRaceNum) const {
    const PluginData& pluginData = PluginData::getInstance();

    // Get lap data for position-based modulation
    const StandingsData* playerStanding = pluginData.getStanding(displayRaceNum);
    const StandingsData* riderStanding = pluginData.getStanding(riderRaceNum);
    int playerLaps = playerStanding ? playerStanding->numLaps : 0;
    int riderLaps = riderStanding ? riderStanding->numLaps : 0;
    int lapDiff = riderLaps - playerLaps;

    // Check if this is a tracked rider (has custom color - overrides color mode)
    const RaceEntryData* entry = pluginData.getRaceEntry(riderRaceNum);
    if (entry) {
        const TrackedRidersManager& trackedMgr = TrackedRidersManager::getInstance();
        const TrackedRiderConfig* trackedConfig = trackedMgr.getTrackedRider(entry->name);
        if (trackedConfig) {
            // Tracked rider - use their configured color with lap-based modulation
            unsigned long baseColor = trackedConfig->color;

            // Apply position-based color modulation (like RadarHud)
            // Only in race sessions where lap position matters
            if (pluginData.isRaceSession()) {
                if (lapDiff >= 1) {
                    // Rider is ahead by laps - lighten color
                    baseColor = PluginUtils::lightenColor(baseColor, 0.4f);
                } else if (lapDiff <= -1) {
                    // Rider is behind by laps - darken color
                    baseColor = PluginUtils::darkenColor(baseColor, 0.6f);
                }
            }

            return baseColor;
        }
    }

    // Apply color based on selected mode
    switch (m_riderColorMode) {
        case RiderColorMode::RELATIVE_POS: {
            // Position-based coloring (original behavior)
            int playerPosition = pluginData.getPositionForRaceNum(displayRaceNum);
            int riderPosition = pluginData.getPositionForRaceNum(riderRaceNum);

            return PluginUtils::getRelativePositionColor(
                playerPosition, riderPosition, playerLaps, riderLaps,
                this->getColor(ColorSlot::NEUTRAL),    // Same position/lap = neutral
                this->getColor(ColorSlot::WARNING),    // Ahead = warning (orange)
                this->getColor(ColorSlot::TERTIARY));  // Behind = tertiary (gray)
        }

        case RiderColorMode::BRAND: {
            // Bike brand color
            if (entry) {
                return PluginUtils::applyOpacity(entry->bikeBrandColor, 0.75f);
            }
            return this->getColor(ColorSlot::TERTIARY);  // Fallback if no entry
        }

        case RiderColorMode::UNIFORM:
        default:
            // Uniform gray for all riders
            return this->getColor(ColorSlot::TERTIARY);
    }
}

// ============================================================================
// Render a marker icon (rotated 90° right for directional icons only)
// ============================================================================
void GapBarHud::renderMarkerIcon(float centerX, float centerY, float size,
                                  int spriteIndex, unsigned long color, int shapeIndex) {
    // Define corner offsets (square icon)
    float halfSize = size / 2.0f;

    // Only rotate directional icons (like chevrons) - non-directional icons (like circles) stay upright
    bool shouldRotate = TrackedRidersManager::shouldRotate(shapeIndex);

    // Rotation: 90° clockwise to point right (direction of travel)
    // cos(90°) = 0, sin(90°) = 1
    float cosAngle = shouldRotate ? 0.0f : 1.0f;
    float sinAngle = shouldRotate ? 1.0f : 0.0f;

    float corners[4][2] = {
        {-halfSize, -halfSize},  // Top-left
        {-halfSize,  halfSize},  // Bottom-left
        { halfSize,  halfSize},  // Bottom-right
        { halfSize, -halfSize}   // Top-right
    };

    SPluginQuad_t sprite;
    for (int i = 0; i < 4; i++) {
        float dx = corners[i][0];
        float dy = corners[i][1];

        // Rotate in uniform space
        float rotX = dx * cosAngle - dy * sinAngle;
        float rotY = dx * sinAngle + dy * cosAngle;

        // Apply aspect ratio to X and position
        sprite.m_aafPos[i][0] = centerX + rotX / UI_ASPECT_RATIO;
        sprite.m_aafPos[i][1] = centerY + rotY;
        applyOffset(sprite.m_aafPos[i][0], sprite.m_aafPos[i][1]);
    }

    sprite.m_iSprite = spriteIndex;
    sprite.m_ulColor = color;
    m_quads.push_back(sprite);
}

// ============================================================================
// Render all rider markers (self, ghost, opponents based on mode)
// ============================================================================
void GapBarHud::renderRiderMarkers(float innerX, float innerY, float innerWidth, float innerHeight,
                                    const ScaledDimensions& dim) {
    const PluginData& pluginData = PluginData::getInstance();
    int displayRaceNum = pluginData.getDisplayRaceNum();

    // Get icon sprite index and shape index for rotation check
    const AssetManager& assetMgr = AssetManager::getInstance();
    int spriteIndex;
    int globalShapeIndex;
    if (m_riderIconIndex > 0) {
        // User selected a specific icon
        spriteIndex = assetMgr.getFirstIconSpriteIndex() + m_riderIconIndex - 1;
        globalShapeIndex = m_riderIconIndex;
    } else {
        // Default to circle-chevron-up
        spriteIndex = assetMgr.getIconSpriteIndex("circle-chevron-up");
        globalShapeIndex = spriteIndex - assetMgr.getFirstIconSpriteIndex() + 1;
    }

    // Icon size scaled with HUD and marker scale (matches MapHud/StandingsHud pattern)
    // DEFAULT_MARKER_BASE_SIZE is full size, so halfSize = 0.006 * scale * markerScale
    float iconSize = DEFAULT_MARKER_BASE_SIZE * m_fScale * m_fMarkerScale;
    float iconHalfSize = iconSize / 2.0f;

    // Y center of bar
    float markerY = innerY + innerHeight / 2.0f;

    // === Render opponent markers (if enabled) - render FIRST so they're behind ===
    if (m_markerMode == MarkerMode::OPPONENTS || m_markerMode == MarkerMode::GHOST_OPPONENTS) {
        const TrackedRidersManager& trackedMgr = TrackedRidersManager::getInstance();

        for (const auto& pos : m_riderPositions) {
            if (pos.raceNum == displayRaceNum) continue;  // Skip self

            float trackPos = pos.trackPos;
            if (trackPos < 0.0f || trackPos > 1.0f) continue;

            // Calculate color using RELATIVE_POS logic (handles tracked rider colors)
            unsigned long riderColor = calculateRiderColor(pos.raceNum, displayRaceNum);

            // Check for tracked rider custom icon
            int riderSpriteIndex = spriteIndex;  // Default to global icon
            int riderShapeIndex = globalShapeIndex;
            const RaceEntryData* entry = pluginData.getRaceEntry(pos.raceNum);
            if (entry) {
                const TrackedRiderConfig* trackedConfig = trackedMgr.getTrackedRider(entry->name);
                if (trackedConfig) {
                    riderSpriteIndex = assetMgr.getFirstIconSpriteIndex() + trackedConfig->shapeIndex - 1;
                    riderShapeIndex = trackedConfig->shapeIndex;
                }
            }

            // Calculate X position on bar
            float markerX = innerX + (innerWidth * trackPos);

            // Render icon (only rotates if directional icon like chevron)
            renderMarkerIcon(markerX, markerY, iconSize, riderSpriteIndex, riderColor, riderShapeIndex);

            // Render label if enabled
            if (m_labelMode != LabelMode::NONE) {
                int position = pluginData.getPositionForRaceNum(pos.raceNum);
                renderMarkerLabel(markerX, markerY, iconHalfSize, pos.raceNum, position, dim);
            }
        }
    }

    // === Render ghost (best lap) marker ===
    if ((m_markerMode == MarkerMode::GHOST || m_markerMode == MarkerMode::GHOST_OPPONENTS) &&
        m_hasBestLap && m_anchor.valid) {
        float bestLapProgress = calculateBestLapProgress();
        if (bestLapProgress >= 0.0f && bestLapProgress <= 1.0f) {
            float markerX = innerX + (innerWidth * bestLapProgress);

            // Ghost uses darkened color - check if player is tracked
            unsigned long ghostColor;
            int ghostSpriteIndex = spriteIndex;
            int ghostShapeIndex = globalShapeIndex;

            const RaceEntryData* selfEntry = pluginData.getRaceEntry(displayRaceNum);
            if (selfEntry) {
                const TrackedRiderConfig* selfTrackedConfig = TrackedRidersManager::getInstance().getTrackedRider(selfEntry->name);
                if (selfTrackedConfig) {
                    ghostColor = PluginUtils::darkenColor(selfTrackedConfig->color, 0.5f);
                    ghostSpriteIndex = assetMgr.getFirstIconSpriteIndex() + selfTrackedConfig->shapeIndex - 1;
                    ghostShapeIndex = selfTrackedConfig->shapeIndex;
                } else {
                    ghostColor = PluginUtils::darkenColor(m_bikeBrandColor, 0.5f);
                }
            } else {
                ghostColor = PluginUtils::darkenColor(m_bikeBrandColor, 0.5f);
            }

            renderMarkerIcon(markerX, markerY, iconSize, ghostSpriteIndex, ghostColor, ghostShapeIndex);
            // No label for ghost - it's your own best lap
        }
    }

    // === Render self marker (always on top) ===
    if (m_currentTrackPos > 0.001f) {
        float markerX = innerX + (innerWidth * m_currentTrackPos);

        // Check if player is tracked - use their configured color and shape (like RadarHud)
        unsigned long selfColor = this->getColor(ColorSlot::POSITIVE);
        int selfSpriteIndex = spriteIndex;
        int selfShapeIndex = globalShapeIndex;

        const RaceEntryData* selfEntry = pluginData.getRaceEntry(displayRaceNum);
        if (selfEntry) {
            const TrackedRiderConfig* selfTrackedConfig = TrackedRidersManager::getInstance().getTrackedRider(selfEntry->name);
            if (selfTrackedConfig) {
                selfColor = selfTrackedConfig->color;
                selfSpriteIndex = assetMgr.getFirstIconSpriteIndex() + selfTrackedConfig->shapeIndex - 1;
                selfShapeIndex = selfTrackedConfig->shapeIndex;
            }
        }

        renderMarkerIcon(markerX, markerY, iconSize, selfSpriteIndex, selfColor, selfShapeIndex);

        // Render label for self if enabled
        if (m_labelMode != LabelMode::NONE) {
            int position = pluginData.getPositionForRaceNum(displayRaceNum);
            renderMarkerLabel(markerX, markerY, iconHalfSize, displayRaceNum, position, dim);
        }
    }
}

// ============================================================================
// Render a label below a marker (position and/or race number) - matches MapHud style
// ============================================================================
void GapBarHud::renderMarkerLabel(float centerX, float centerY, float iconHalfSize,
                                   int raceNum, int position, const ScaledDimensions& dim) {
    if (m_labelMode == LabelMode::NONE) return;

    // Scale font size by marker scale (like MapHud)
    float labelFontSize = dim.fontSizeSmall * m_fMarkerScale;

    // Position label BELOW the icon with a small gap (like MapHud)
    float labelGap = iconHalfSize * 0.2f;
    float labelY = centerY + iconHalfSize + labelGap;

    // Build label string based on mode (matching MapHud format)
    char labelStr[20];
    switch (m_labelMode) {
        case LabelMode::POSITION:
            if (position > 0) {
                snprintf(labelStr, sizeof(labelStr), "P%d", position);
            } else {
                return;  // No valid position, skip label
            }
            break;
        case LabelMode::RACE_NUM:
            // MapHud uses no prefix for race number
            snprintf(labelStr, sizeof(labelStr), "%d", raceNum);
            break;
        case LabelMode::BOTH:
            if (position > 0) {
                snprintf(labelStr, sizeof(labelStr), "P%d #%d", position, raceNum);
            } else {
                snprintf(labelStr, sizeof(labelStr), "#%d", raceNum);
            }
            break;
        default:
            return;
    }

    // Use podium colors for position labels (P1/P2/P3) like MapHud
    unsigned long labelColor = this->getColor(ColorSlot::PRIMARY);
    if (m_labelMode == LabelMode::POSITION || m_labelMode == LabelMode::BOTH) {
        if (position == Position::FIRST) {
            labelColor = PodiumColors::GOLD;
        } else if (position == Position::SECOND) {
            labelColor = PodiumColors::SILVER;
        } else if (position == Position::THIRD) {
            labelColor = PodiumColors::BRONZE;
        }
    }

    // Create text outline by rendering dark text at offsets first (like MapHud)
    float outlineOffset = labelFontSize * 0.05f;
    unsigned long outlineColor = 0xFF000000;  // Black with full opacity

    // Render outline at 4 cardinal directions
    addString(labelStr, centerX - outlineOffset, labelY, Justify::CENTER,
             this->getFont(FontCategory::SMALL), outlineColor, labelFontSize, true);
    addString(labelStr, centerX + outlineOffset, labelY, Justify::CENTER,
             this->getFont(FontCategory::SMALL), outlineColor, labelFontSize, true);
    addString(labelStr, centerX, labelY - outlineOffset, Justify::CENTER,
             this->getFont(FontCategory::SMALL), outlineColor, labelFontSize, true);
    addString(labelStr, centerX, labelY + outlineOffset, Justify::CENTER,
             this->getFont(FontCategory::SMALL), outlineColor, labelFontSize, true);

    // Render main text on top
    addString(labelStr, centerX, labelY, Justify::CENTER,
              this->getFont(FontCategory::SMALL), labelColor, labelFontSize, true);
}
