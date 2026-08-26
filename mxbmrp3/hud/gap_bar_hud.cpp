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
#include "../core/widget_constants.h"
#include "center_stack.h"

using namespace PluginConstants;

GapBarHud::GapBarHud()
    : m_bestLapTime(0)
    , m_hasBestLap(false)
    , m_currentTrackPos(0.0f)
    , m_currentLapNum(0)
    , m_observedLapStart(false)
    , m_cachedDisplayRaceNum(-1)
    , m_cachedSessionGeneration(-1)
    , m_cachedPitState(-1)
    , m_cachedLastCompletedLapNum(-1)
    , m_cachedSplit1(-1)
    , m_cachedSplit2(-1)
    , m_cachedPlayerRunning(true)
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
    // TITLE RESTORED, TEMPORARILY. This panel was one of the three the caption was taken
    // from (see BaseHud::m_titleSupported for the twelve that keep it off). It is back so
    // the reason the caption was unwanted can be shown rather than described -- nothing
    // else about this HUD reverted with it: the panel, its body card, the coloured
    // block's outset and the stack spacing are all as the last few commits left them.
    // One-time setup
    DEBUG_INFO("GapBarHud created");
    // A themed body card behind the bar, like every other table HUD -- this never opted
    // in, and the bar sat straight on the frame while Standings and the rest gave their
    // content a well to sit in. Safe now that the default offset is derived from this
    // HUD's OWN padding (CenterStack::gapBarOffsetY), so the card's border no longer
    // pushes the box off the top of the screen.
    m_bContentCard = true;
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

    // Detect session changes (new event/track/bike) and reset state
    int currentGeneration = sessionData.sessionGeneration;

    if (currentGeneration != m_cachedSessionGeneration) {
        DEBUG_INFO_F("GapBarHud: Session reset detected (generation %d -> %d)",
            m_cachedSessionGeneration, currentGeneration);
        resetTimingState();
        m_cachedSessionGeneration = currentGeneration;
        m_cachedPitState = -1;
        if (isVisibleAnySurface()) setDataDirty();
    }

    // Detect spectate target changes and reset state
    int currentDisplayRaceNum = pluginData.getDisplayRaceNum();
    const IdealLapData* idealLapData = pluginData.getIdealLapData();
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

        if (isVisibleAnySurface()) setDataDirty();
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
            if (isVisibleAnySurface()) setDataDirty();
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
        if (isVisibleAnySurface()) setDataDirty();
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
        //
        // A TEST-PLANTED gap is left alone: it exists precisely because the live
        // path cannot be driven headlessly, so letting this recompute overwrite it
        // made the fill's geometry cases depend on how long the harness took to get
        // from the plant to the draw. See GapBarHud::testForceGap.
        if (m_testGapForced) {
            PluginData::getInstance().setLiveGap(m_cachedGap, m_cachedGapValid);
        } else if (m_hasBestLap && m_anchor.valid) {
            m_cachedGap = calculateCurrentGap();
            m_cachedGapValid = true;
            PluginData::getInstance().setLiveGap(m_cachedGap, true);
        } else {
            m_cachedGap = 0;
            m_cachedGapValid = false;
            PluginData::getInstance().setLiveGap(0, false);
        }

        if (isVisibleAnySurface()) setDataDirty();
    }

    // OPTIMIZATION: Only process dirty flags and rebuild when visible
    if (isVisibleAnySurface()) {
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

    // THE PANEL IS WHAT LINES UP, so the width setting scales the PANEL and the bar is
    // whatever interior the plan hands back. Notices, Timing and Version each pass
    // CenterStack::boxWidth as their minPanelW; passing the same number here at the 50%
    // default makes all four panels identical -- including whatever the plan rounds to,
    // which arithmetic on the bar could not guarantee.
    //
    // It used to size the BAR to their box instead, which left this panel one padding
    // wider per side because the bar is content and their box is a panel. Measured at the
    // shipped defaults before this change: gap bar panel 159500 against 137500 for the
    // other two unthemed, 181500 against 148500 themed -- 2 * dim.paddingH in each case.
    // That was known and deliberately left ("a gap bar wider than the panels below it is
    // this HUD's documented exception"), on the grounds that narrowing the bar would move
    // every marker on it. The markers move; they are drawn from barWidth and re-derive.
    // A panel that does not line up with the stack it sits in is the worse of the two.
    // (Previously 43 chars at the normal font to match the Performance/Telemetry HUD; that
    // alignment is traded for matching the center-top stack.)
    float centerStackWidth = CenterStack::boxWidth(dim.fontSizeLarge, centerStackPaddingX());
    float basePanelWidth = 2.0f * centerStackWidth;
    float panelWidth = basePanelWidth * (static_cast<float>(m_barWidthPercent) / 100.0f);
    // Height = lineHeightLarge (large-font title band = 2x lineHeightNormal = 4 snap-grid
    // cells) so the bar lines up on the shared grid with the Notices/Timing rows. Inner
    // content (gap fill, markers, gap text) is positioned relative to barHeight below, so it
    // re-centers automatically. (Was paddingV + fontSizeLarge, ~4.56 cells - off-grid.)
    // One normal row, like the Version widget's content -- the bar was a lineHeightLarge
    // band, which is two. Its own gap text stays cell-centred: a normal font in a normal
    // row is exactly the case addString's rowCenterOffset is built for.
    float barHeight = bigValueRowHeight(dim);

    // The bar's OWN inner insets -- how far the coloured fill sits inside the box.
    // Deliberately not the panel padding: these shape the bar graphic itself.
    float innerInsetH = dim.gridH(1) * layout().labelPaddingX;  // 0.5 char widths
    float innerInsetV = dim.gridV(BAR_PADDING_V_SCALE);           // quarter line height

    // startX is derived from the bar the PLAN returns, so it is computed after
    // planPanel below rather than here.
    float startX = 0.0f;
    // THE PANEL ANCHORS AT THE OFFSET, like every other HUD: the box top is 0 and the
    // bar sits one padding inside it. It used to be the other way round -- the BAR was
    // at 0 and the box derived as (0 - paddingV) -- which made the panel's top edge a
    // function of the padding, and dim.paddingV IS contentPaddingY(). So a theme
    // that switches its body card off ([card] hud-content = 0) changed the padding and
    // slid this panel down while Timing and Notices, which anchor their tops, stayed
    // put. Reported from two screenshots of the same scene. Pinned by
    // tests/integration/tests/center_stack_theme_test.cpp.
    //
    // It also made the DEFAULT position padding-dependent (resetToDefaults added the
    // live paddingV back), so resetting under a theme and resetting without one wrote
    // different numbers, and a theme switched on afterwards put the box top at y = 0 --
    // flush against the screen edge with its top frame slice clipped, measured.
    const float boxTop = 0.0f;
    float startY = 0.0f;   // set from the plan below, once the panel is placed

    // ==== BACKGROUND QUAD ====
    // addBackgroundQuad, not a hand-rolled quad: this used to reimplement it exactly
    // -- offset, texture branch, opacity and all -- which meant it silently missed
    // the one thing the helper gained later, the themed nine-slice. A HUD that draws
    // its own panel background gets no theme, and that is the whole reason this HUD
    // and Notices rendered flat while every other panel was framed.
    // Panel padding wraps the bar, like every other HUD's box wraps its content. The
    // box used to BE the bar exactly, so panelPaddingXCells/panelPaddingYCells did nothing here at all.
    // Optional caption. Default OFF, like the two panels below it in the stack --
    // this one is a bar, and a header over it is usually noise -- but available.
    // The box grows DOWNWARD (title above content, as everywhere else), which moves
    // the bar down; see resetToDefaults for what that costs the centre stack.
    // BOX-MODEL: the bar is the section's content; the card is its border box.
    // The panel wraps both through the plan, so the bar's interior, the card the
    // user sees and the panel height are one computation instead of the
    // card-interior re-anchor the old chain needed (contentCardSpanY).
    BaseHud::PanelWant want;
    // The PANEL is the ask; the bar is read back from the plan below. contentW stays
    // 0 deliberately -- setting both would make the wider of the two win and put the
    // old off-by-two-paddings behaviour back.
    wantCenterStackWidth(want, panelWidth);
    want.sectionH = { barHeight };
    // LARGE, like every other full HUD. The tier is opt-in and defaults to
    // Normal, which is right for a gauge and wrong for a panel with a table in
    // it -- this HUD, Timing and Notices had all simply never said so, and wore
    // the widget caption size next to siblings that did.
    want.captionW = planTitleWidth(dim, "Gap Bar", TitleTier::Large);
    want.tier = TitleTier::Large;
    // The bar IS this panel: unthemed, its coloured fill takes the cell of panel
    // padding rather than sitting inside it. Same outer rect either way; with a
    // theme on, the frame keeps its ring. See PanelWant::contentFillsPanel.
    want.contentFillsPanel = true;
    PanelPlan& plan = planPanel(dim, want);
    // Draw at what the plan RETURNED, not what was asked: the last section's
    // box absorbs the panel's ceil remainder (panel_box.h), and a bar sized
    // to the ask stops a strip above the card it is meant to fill.
    barHeight = plan.sectionBoxH();
    // The bar IS the plan's content column, so it inherits the panel's rounding and
    // the four panels share an edge exactly.
    float barWidth = plan.contentW();

    const float insetL = static_cast<float>(plan.g.rowsX) * plan.cellW;
    const float boxWidth = plan.width();
    const float boxHeight = plan.height();

    // CENTRE-ANCHORED, as all four centred elements now are: offsetX is this
    // panel's CENTRE, so the layout is just half its own width to the left of it and
    // a width change grows the bar symmetrically instead of walking an edge.
    //
    // A LAYOUT MUST NOT READ m_fOffsetX. It used to snap against the LIVE offset
    // (`snapEdgeX(m_fOffsetX + startX) - ...`), which made m_fBoundsLeft a function of
    // the offset -- and the drag path snaps `m_fBoundsLeft + newOffsetX` itself
    // (base_hud.cpp). Two snaps, each reading the other's output a frame late: the bar
    // jittered left and right under the cursor for as long as you held it. Reported as
    // "extremely erratic when I try to place it". Every other HUD's bounds are a pure
    // function of its layout, and that is what makes the one snap in the drag path the
    // only one there is.
    const float boxLeft = centerAnchoredPanelLeft(boxWidth);
    startX = boxLeft + insetL;
    addPlanBackground(plan, boxLeft, boxTop);
    addPlanTitle(plan, "Gap Bar", this->getFont(FontCategory::TITLE),
                 this->getColor(ColorSlot::PRIMARY));
    // THE CARD'S DRAWN BOX, not the content band inside it: the bar IS the card here,
    // so its fill, its markers and its gap text all belong to the box the player sees.
    // The band is the same thing while [content] border is symmetric and sits high in
    // the card when it is not (PanelPlan::sectionBoxY) -- which is what put the gap
    // text above centre and left the fill short of the content it is drawn behind.
    startY = plan.sectionBoxY();

    // Common inner dimensions
    float innerWidth = barWidth - innerInsetH * 2.0f;
    float innerHeight = barHeight - innerInsetV * 2.0f;

    // THE SLAB THE GAP TEXT LANDS ON, or 0 for none. The text is CENTER-justified on
    // exactly the point the fill grows from (both take plan.sectionBoxCenterX()), so
    // whenever a fill is drawn, half the digits sit on it -- and the text's own colour
    // is a NEGATIVE/POSITIVE slot too, which is red-on-red the moment the two agree.
    // The Notices slabs beside this one have been correcting for that since
    // captionOnSlabColor landed; this panel was the half of that comment that was
    // aspirational. Reported from the game as unreadable at high background opacity.
    //
    // Recorded rather than re-derived, because the fill and the text do NOT always
    // read the same number: the fill is always the LIVE gap (see the header just
    // below), while the text may be showing a FROZEN one, so their signs can differ
    // and the ink is legible over the opposite colour. What the correction needs is
    // the slab actually under the glyphs.
    unsigned long gapTextSlab = 0;

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

        // THE FILL'S TRAVEL SPANS THE CARD'S DRAWN BOX, the horizontal half of the
        // same change the outset below makes vertically -- stated in the card's own
        // terms (PanelPlan::sectionBoxW), not as barWidth plus the LEFT inset mirrored,
        // which reached the card's edges only while the [content] terms were
        // left/right symmetric. Its extremes were once the INNER rect -- inset by
        // innerInsetH, which exists for the rider markers -- so a maxed-out gap
        // stopped short of the box on both axes at once.
        //
        // THE CONSEQUENCE, stated because it is a real trade: the fill and the rider
        // markers do not share one scale. The markers stay on the inner rect, so a
        // full-scale fill reaches slightly past where a marker can sit. They measure
        // different things -- the fill is your gap, the markers are riders -- and the
        // alternative was a fill that cannot touch the box it lives in.
        float halfWidth = plan.sectionBoxW() / 2.0f;
        float centerX = plan.sectionBoxCenterX();

        if (std::abs(gapRatio) > 0.001f) {
            float quadX, quadWidth;
            unsigned long fillColor;

            if (gapRatio > 0.0f) {
                // Behind (slower) - grow left from center, red
                quadWidth = halfWidth * gapRatio;
                quadX = centerX - quadWidth;
                gapTextSlab = this->getColor(ColorSlot::NEGATIVE);
                fillColor = PluginUtils::applyOpacity(gapTextSlab, m_fBackgroundOpacity);
            } else {
                // Ahead (faster) - grow right from center, green
                quadWidth = halfWidth * (-gapRatio);
                quadX = centerX;
                gapTextSlab = this->getColor(ColorSlot::POSITIVE);
                fillColor = PluginUtils::applyOpacity(gapTextSlab, m_fBackgroundOpacity);
            }

            // FULL HEIGHT OF THE CONTENT BOX, not the marker inset. The fill is the one
            // thing here whose SIZE is the reading -- how far off the reference you are
            // -- and it was drawn a quarter row short at each end, floating inside the
            // card with a dark margin above and below. innerInsetV exists to keep the
            // rider MARKERS clear of the box edges (icons, not a bar), and the fill was
            // borrowing it for no reason of its own.
            //
            // Only the vertical is freed. The fill's WIDTH stays on the inner rect,
            // because that is the coordinate system the markers are placed in: a fill
            // scaled to the box while markers are scaled to the inner rect would put a
            // full-scale fill edge somewhere no marker can ever reach.
            // THROUGH THE THEME'S BUTTON SLICES, like the Notices slab beside it: both
            // are a coloured block whose colour is the reading, and a theme that gives
            // its buttons a shape should give these the same one. Hand-rolled here as a
            // SOLID_COLOR quad until now, which is the duplication addButtonQuad exists
            // to end -- and it meant a themed gap bar drew a flat rectangle inside a
            // bevelled card.
            //
            // NOT opaque: like the notice slabs, this is a translucent reading over the
            // track rather than a control, and the button rule (a thing you click stays
            // legible) would flatten it to a solid box.
            // ...and it spans the card exactly, because startY/barHeight ARE the card
            // now. This used to add an outset back on: `startY - sections[0].top` at
            // BOTH ends, which is the top inset twice and therefore short at the bottom
            // by however much the two differ -- invisible on a symmetric border and
            // the reason the fill did not reach its own content on an asymmetric one.
            addButtonQuad(quadX, startY, quadWidth, barHeight, fillColor, /*opaque=*/false);
        }
    }

    // ==== RIDER MARKERS (icons instead of vertical bars) ====
    // Renders self, ghost, and/or opponents based on marker mode
    renderRiderMarkers(startX + innerInsetH, startY + innerInsetV, innerWidth, innerHeight, dim);

    // ==== GAP TEXT (centered inside bar, primary color) - conditionally rendered ====
    if (!m_showGapText) {
        // Skip gap text - user wants pure flat map mode
        // The BOX, not the bar: the box is what the user sees and grabs, and with a
    // title on, bar-only bounds would leave the caption undraggable.
    setBounds(boxLeft, boxTop, boxLeft + boxWidth, boxTop + boxHeight);
        return;
    }

    // ==== GAP TEXT (centered inside bar, primary color) ====
    // X: the CARD's centre (PanelPlan::sectionBoxCenterX -- the bar's own centre
    // only while the [content] terms are symmetric), Y: INK-centred in the bar,
    // the same solve TimingHud's big time and the Notices message use.
    float gapTextX = plan.sectionBoxCenterX();
    // This was `startY + (barHeight - fontSize) / 2 - rowCenterOffset(fontSize)`, which
    // reads like a centring but is not one: barHeight IS lineHeightNormal here, so
    // (barHeight - fontSize)/2 is rowCenterOffset exactly, the two cancel, and the whole
    // expression came to plain `startY` -- a hand-rolled reimplementation of what
    // addString already does. What it centred was the glyph CELL, and a digit's ink sits
    // 0.11 of its cell above the cell's middle, so the gap text floated that much high
    // inside a bar it is drawn on top of (~3px at 1080p, measured against the bar edges).
    // inkCenteredY over barHeight, not bigValueTextY: that helper centres in a
    // bigValueRowHeight box, and barHeight is the CARD's interior when there is a card.
    // Passing the row would leave the text where it used to be while the bar moved.
    //
    // THE LARGE SIZE, matching the Timing panel's big time below it: the two are the
    // same kind of reading in the same stack, and the gap was set a third smaller for
    // no reason the code states. It costs no height -- the row is bigValueRowHeight
    // (one NORMAL row) whatever the glyph is, and inkCenteredY solves the placement
    // for the size it is handed, which is the case that helper exists for and the one
    // Timing has always used.
    float gapTextY = inkCenteredY(startY, barHeight, dim.fontSizeLarge);

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
        // MUTED, like the Timing panel's big time with no lap to show. A placeholder
        // in the primary colour reads as a value -- the one thing it is not -- and at
        // the large size it now draws at, a bright "-" is the loudest thing in the
        // stack while saying the least.
        gapColor = this->getColor(ColorSlot::MUTED);
    }

    // Lift the ink off the slab if the two are too close in luma -- see gapTextSlab
    // above for why the slab is carried down here rather than re-derived from the sign
    // of the number being printed. A no-op when no fill was drawn, and a no-op when
    // the ink already clears the slab (legibleOnFill keeps the hue whenever it can).
    if (gapTextSlab != 0) {
        gapColor = inkOnSlabColor(gapColor, gapTextSlab, m_fBackgroundOpacity);
    }

    // Gap text (monospace font, large size to match Timing, centered)
    addString(gapBuffer, gapTextX, gapTextY, Justify::CENTER,
              this->getFont(FontCategory::DIGITS), gapColor, dim.fontSizeLarge);

    // Set bounds for drag detection
    // The BOX, not the bar: the box is what the user sees and grabs, and with a
    // title on, bar-only bounds would leave the caption undraggable.
    setBounds(boxLeft, boxTop, boxLeft + boxWidth, boxTop + boxHeight);
}

// No setScale override: this panel is CENTRE-ANCHORED (offsetX is its centre),
// so the layout recentres on every width change and scaling needs no offset
// compensation. The old override called setScaleKeepingCenter ON TOP of that
// recentring, which double-compensated: each scale step walked the stored
// centre sideways by half the width change. Pinned by center_stack_theme_test's
// scale case.

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
    // Off by DEFAULT, not unavailable -- the toggle is in the Gap Bar tab. Switching
    // it on grows the box DOWNWARD, so the bar and everything under it move down by
    // the band's height; center_stack.h derives the two panels below from box heights
    // it computes WITHOUT a title, so expect to nudge them.
    m_bShowTitle = false;
    setTextureVariant(0);  // No texture by default
    m_fBackgroundOpacity = 0.1f;
    m_fScale = 1.0f;
    // First box of the center-top stack; see hud/center_stack.h for the whole
    // specification. One cell down from the screen edge, aligning with the
    // settings/camera buttons' row.
    // The BOX TOP, plainly, because that is now what the offset means here (see
    // rebuildRenderData). This used to add the live paddingV back, which made the
    // default depend on the theme in force when it was written.
    setPosition(CENTER_ANCHOR_X, CenterStack::stackBoxTop());

    // Settings
    m_freezeDurationMs = DEFAULT_FREEZE_MS;
    m_markerMode = MarkerMode::GHOST;  // Default to ghost-only (original behavior)
    m_labelMode = LabelMode::NONE;     // No labels by default (like MapHud default)
    m_labelAnchor = LabelAnchor::BELOW;  // ...and under the marker, like the other two
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
        if (isVisibleAnySurface()) {
            setDataDirty();
        }
    }
}

// ============================================================================
// Calculate rider color based on color mode setting (like MapHud/RadarHud)
// ============================================================================
unsigned long GapBarHud::calculateRiderColor(int riderRaceNum, int displayRaceNum) const {
    const PluginData& pluginData = PluginData::getInstance();

    // Get lap data for position-based modulation (only in race sessions)
    const StandingsData* playerStanding = pluginData.getStanding(displayRaceNum);
    const StandingsData* riderStanding = pluginData.getStanding(riderRaceNum);
    bool isRace = pluginData.isRaceSession();
    int playerLaps = (isRace && playerStanding) ? playerStanding->numLaps : 0;
    int riderLaps = (isRace && riderStanding) ? riderStanding->numLaps : 0;
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
            // lapDiff is already zeroed in non-race sessions above
            if (lapDiff >= 1) {
                // Rider is ahead by laps - lighten color
                baseColor = PluginUtils::lightenColor(baseColor, 0.4f);
            } else if (lapDiff <= -1) {
                // Rider is behind by laps - darken color
                baseColor = PluginUtils::darkenColor(baseColor, 0.6f);
            }

            return baseColor;
        }
    }

    // Apply color based on selected mode
    switch (m_riderColorMode) {
        case RiderColorMode::RELATIVE_POS: {
            // Position-based coloring - only meaningful in race sessions
            if (!pluginData.isRaceSession()) {
                return this->getColor(ColorSlot::NEUTRAL);
            }
            int playerPosition = pluginData.getDisplayPositionForRaceNum(displayRaceNum);
            int riderPosition = pluginData.getDisplayPositionForRaceNum(riderRaceNum);

            return PluginUtils::getRelativePositionColor(
                playerPosition, riderPosition, playerLaps, riderLaps,
                this->getColor(ColorSlot::NEUTRAL),
                this->getColor(ColorSlot::WARNING),
                this->getColor(ColorSlot::TERTIARY));
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
        spriteIndex = assetMgr.iconSpriteForShape(m_riderIconIndex);
        globalShapeIndex = m_riderIconIndex;
    } else {
        // Default to circle-chevron-up
        spriteIndex = assetMgr.getIconSpriteIndex("circle-chevron-up");
        globalShapeIndex = assetMgr.shapeIndexForSprite(spriteIndex);
    }

    // Icon size scaled with HUD and marker scale (matches MapHud/StandingsHud pattern)
    // DEFAULT_MARKER_BASE_SIZE is full size, so halfSize = 0.006 * scale * markerScale
    float iconSize = DEFAULT_MARKER_BASE_SIZE * m_fScale * m_fMarkerScale;
    float iconHalfSize = iconSize / 2.0f;

    // Y centre of the bar -- of the ICON when there is no label, and of the icon and
    // its label TOGETHER when there is. This bar is one text row tall, so a label
    // centred the naive way hangs half out of the panel; marker_label.h owns the
    // shift, and it is per-marker because the local player's icon and label are both
    // boosted and so want a bigger one.
    const float boxCenterY = innerY + innerHeight / 2.0f;
    auto centerFor = [&](float halfSize, float boost) {
        if (m_labelMode == LabelMode::NONE) return boxCenterY;
        return boxCenterY + MarkerLabel::blockCenterShift(
            m_labelAnchor, halfSize, dim.fontSizeSmall * m_fMarkerScale * boost);
    };
    const float markerY = centerFor(iconHalfSize, 1.0f);

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
                    riderSpriteIndex = assetMgr.iconSpriteForShape(trackedConfig->shapeIndex);
                    riderShapeIndex = trackedConfig->shapeIndex;
                }
            }

            // Calculate X position on bar
            float markerX = innerX + (innerWidth * trackPos);

            // Render icon (only rotates if directional icon like chevron)
            renderMarkerIcon(markerX, markerY, iconSize, riderSpriteIndex, riderColor, riderShapeIndex);

            // Render label if enabled
            if (m_labelMode != LabelMode::NONE) {
                int position = pluginData.getDisplayPositionForRaceNum(pos.raceNum);
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

            // Ghost is a dimmed version of the live self marker: darkened tracked color
            // when tracked, otherwise a darkened accent (matching the self marker default).
            ghostColor = PluginUtils::darkenColor(this->getColor(ColorSlot::ACCENT), 0.5f);

            const RaceEntryData* selfEntry = pluginData.getRaceEntry(displayRaceNum);
            if (selfEntry) {
                const TrackedRiderConfig* selfTrackedConfig = TrackedRidersManager::getInstance().getTrackedRider(selfEntry->name);
                if (selfTrackedConfig) {
                    ghostColor = PluginUtils::darkenColor(selfTrackedConfig->color, 0.5f);
                    ghostSpriteIndex = assetMgr.iconSpriteForShape(selfTrackedConfig->shapeIndex);
                    ghostShapeIndex = selfTrackedConfig->shapeIndex;
                }
            }

            renderMarkerIcon(markerX, markerY, iconSize, ghostSpriteIndex, ghostColor, ghostShapeIndex);
            // No label for ghost - it's your own best lap
        }
    }

    // === Render self marker (always on top) ===
    if (m_currentTrackPos > 0.001f) {
        float markerX = innerX + (innerWidth * m_currentTrackPos);
        // A touch larger than the pack, like MapHud's local-player marker -- on a bar
        // whose whole subject is YOUR gap, the one marker that is you should be the
        // one you find first. See MarkerLabel::PLAYER_BOOST.
        const float selfBoost = MarkerLabel::boost(true);
        const float selfIconSize = iconSize * selfBoost;

        // Check if player is tracked - use their configured color and shape (like RadarHud).
        // Default to the accent slot so the player's own marker matches StandingsHud/MapHud.
        unsigned long selfColor = this->getColor(ColorSlot::ACCENT);
        int selfSpriteIndex = spriteIndex;
        int selfShapeIndex = globalShapeIndex;

        const RaceEntryData* selfEntry = pluginData.getRaceEntry(displayRaceNum);
        if (selfEntry) {
            const TrackedRiderConfig* selfTrackedConfig = TrackedRidersManager::getInstance().getTrackedRider(selfEntry->name);
            if (selfTrackedConfig) {
                selfColor = selfTrackedConfig->color;
                selfSpriteIndex = assetMgr.iconSpriteForShape(selfTrackedConfig->shapeIndex);
                selfShapeIndex = selfTrackedConfig->shapeIndex;
            }
        }

        const float selfY = centerFor(selfIconSize / 2.0f, selfBoost);
        renderMarkerIcon(markerX, selfY, selfIconSize, selfSpriteIndex, selfColor, selfShapeIndex);

        // Render label for self if enabled
        if (m_labelMode != LabelMode::NONE) {
            int position = pluginData.getDisplayPositionForRaceNum(displayRaceNum);
            renderMarkerLabel(markerX, selfY, selfIconSize / 2.0f, displayRaceNum, position,
                              dim, selfBoost);
        }
    }
}

// ============================================================================
// Render a label below a marker (position and/or race number) - matches MapHud style
// ============================================================================
void GapBarHud::renderMarkerLabel(float centerX, float centerY, float iconHalfSize,
                                   int raceNum, int position, const ScaledDimensions& dim,
                                   float playerBoost) {
    if (m_labelMode == LabelMode::NONE) return;

    // Scale font size by marker scale, and by the local player's boost so the self
    // marker's label grows with the marker (MapHud does the same; see marker_label.h).
    const float labelFontSize = dim.fontSizeSmall * m_fMarkerScale * playerBoost;

    // Where the label sits relative to the icon: shared with MapHud and RadarHud.
    const MarkerLabel::Placement lp = MarkerLabel::place(
        m_labelAnchor, centerX, centerY, iconHalfSize, labelFontSize);

    // Build label string based on mode (matching MapHud format)
    char labelStr[20];
    if (!MarkerLabel::format(m_labelMode, position, raceNum, labelStr, sizeof(labelStr))) {
        return;  // Nothing to render (e.g. POSITION mode with no valid position)
    }

    // Podium colors for position labels (P1/P2/P3) like MapHud
    unsigned long labelColor =
        MarkerLabel::color(m_labelMode, position, this->getColor(ColorSlot::PRIMARY));

    // THE STANDARD DROP SHADOW, exactly as MapHud draws the same label: one string
    // with skipShadow=false, and HudManager::collectRenderData lays the shadow in
    // behind it from [Display] dropShadowOffsetX/Y, honouring the global toggle and
    // any per-HUD dropShadow override.
    //
    // This used to hand-roll a black outline -- the same string four times at
    // +-5% of the font -- and its comment said "like MapHud", which was true when it
    // was written. Map moved to the standard shadow and this did not, so the two
    // labels drifted apart: four quads instead of one, a shadow the [Display]
    // offsets could not move, and an outline that stayed on with drop shadows
    // switched OFF. (Rider ICONS are sprite quads with their own baked outlines and
    // take no shadow either way -- this is only the text.)
    addString(labelStr, lp.x, lp.y, lp.justify,
              this->getFont(FontCategory::SMALL), labelColor, labelFontSize, false);
}
