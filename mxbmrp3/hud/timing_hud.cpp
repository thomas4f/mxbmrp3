// ============================================================================
// hud/timing_hud.cpp
// Timing HUD - displays accumulated split and lap times as they happen
// Shows accumulated times and gaps (default position: center of screen)
// Supports real-time elapsed timer with per-column visibility modes
// Example: S1: 30.00s, S2: 60.00s (accumulated), Lap: 90.00s
// ============================================================================
#include "timing_hud.h"
#include "records_hud.h"

#include "../game/game_config.h"

#include <cstdio>
#include <cstring>    // std::strlen
#include <cmath>
#include <string>
#include <chrono>
#include <algorithm>  // std::max

#include "../diagnostics/logger.h"
#include "../diagnostics/timer.h"
#include "../core/plugin_utils.h"
#include "../core/widget_constants.h"
#include "../core/color_config.h"
#include "../core/stats_manager.h"
#include "../core/hud_manager.h"

using namespace PluginConstants;

// Positioning constants
namespace {
    // Both layouts center horizontally on screen (around CENTER_X) so toggling layout doesn't jump
    constexpr float CENTER_X = 0.5f;
    constexpr float START_X = 0.0f;
    constexpr float START_Y = 0.0f;

    // Default vertical position: LAST in the center-top stack (GapBar -> Notices -> Timing, one
    // grid snap between each). The Timing HUD grows DOWN, so placing it at the bottom means it
    // never overlaps the notice/gapbar above no matter how many comparison rows are enabled.
    //   notice bottom (0.117336) + 1 vertical cell (0.011734) = 0.129069 (== notices divider).
    // All three boxes are now lineHeightLarge tall (4 cells), so the whole stack lands on the
    // grid: box tops at cells 1/6/11, box bottoms at cells 5/10.
    constexpr float DEFAULT_POSITION_Y = 0.129069f;
}

TimingHud::TimingHud()
    : m_displayDurationMs(DEFAULT_DURATION_MS)
    , m_showTime(true)
    , m_enabledComparisons(GAP_DEFAULT_ENABLED)
    , m_cachedSplit1(-1)
    , m_cachedSplit2(-1)
    , m_cachedSplit3(-1)
    , m_cachedLastCompletedLapNum(-1)
    , m_cachedDisplayRaceNum(-1)
    , m_cachedSessionGeneration(-1)
    , m_cachedPBScope(PBScope::CATEGORY)
    , m_cachedPitState(-1)
    , m_previousAllTimeLap(-1)
    , m_previousAllTimeSector1(-1)
    , m_previousAllTimeS1PlusS2(-1)
    , m_previousAllTimeS1PlusS2PlusS3(-1)
    , m_isFrozen(false)
{
    // One-time setup
    DEBUG_INFO("TimingHud created");
    setDraggable(true);
    m_quads.reserve(1);    // Single background quad (values carry colour via text, no strips)
    m_strings.reserve(8);  // Time + (name + value) per comparison row

    // Set texture base name for dynamic texture discovery
    setTextureBaseName("timing_hud");

    // Set all configurable defaults
    resetToDefaults();

    rebuildRenderData();
}

bool TimingHud::handlesDataType(DataChangeType dataType) const {
    return dataType == DataChangeType::IdealLap ||
           dataType == DataChangeType::SpectateTarget ||
           dataType == DataChangeType::SessionData ||  // Reset on new session/event
           dataType == DataChangeType::Standings;       // Detect pit entry/exit
}

void TimingHud::update() {
    // OPTIMIZATION: Skip all processing when not visible
    // State tracking (splits, gaps) is only meaningful when displaying
    if (!isVisibleAnySurface()) {
        clearDataDirty();
        clearLayoutDirty();
        return;
    }

    const PluginData& pluginData = PluginData::getInstance();
    const SessionData& sessionData = pluginData.getSessionData();

    // Detect session changes and reset state
    // sessionGeneration is incremented on every RaceSession callback (track switch,
    // bike change, practice→race, etc.), so comparing it reliably catches all transitions.
    int currentGeneration = sessionData.sessionGeneration;

    if (currentGeneration != m_cachedSessionGeneration) {
        DEBUG_INFO_F("TimingHud: New session detected (generation %d -> %d)",
            m_cachedSessionGeneration, currentGeneration);
        resetLiveTimingState();
        m_cachedSessionGeneration = currentGeneration;
        m_cachedPitState = -1;  // Reset pit state cache for new session
        setDataDirty();
    }

    // Detect PB scope change (user toggled Bike/Category in settings)
    PBScope currentPBScope = UiConfig::getInstance().getPBScope();
    if (currentPBScope != m_cachedPBScope) {
        cacheAllTimePB();
        m_cachedPBScope = currentPBScope;
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
        const IdealLapData* idealLap = pluginData.getIdealLapData();
        if (currentLap) {
            m_cachedSplit1 = currentLap->split1;
            m_cachedSplit2 = currentLap->split2;
            m_cachedSplit3 = currentLap->split3;
        }
        if (idealLap) {
            m_cachedLastCompletedLapNum = idealLap->lastCompletedLapNum;
        }

        setDataDirty();
    }

    // Detect pit entry/exit (for cache tracking)
    // Note: Anchor reset is now handled centrally by PluginData's track position monitoring
    const StandingsData* standing = pluginData.getStanding(currentDisplayRaceNum);
    if (standing) {
        int currentPitState = standing->pit;
        if (m_cachedPitState != -1 && currentPitState != m_cachedPitState) {
            DEBUG_INFO_F("TimingHud: Pit state changed from %d to %d", m_cachedPitState, currentPitState);
            // Just trigger a redraw - centralized timer handles anchor reset automatically
            setDataDirty();
        }
        // A lap during which the rider was in the pits is not a genuine timed lap: the live
        // timer is dropped on pit exit and re-anchored at the next S/F crossing (that S/F
        // crossing is where this lap "completes"). Remember it so the pit out-lap's completion
        // doesn't flash INVALID - there's no timing to invalidate. Cleared when the lap
        // completes (processTimingUpdates) or on a session/spectate reset.
        //
        // Only latch while a timed lap is actually underway (the lap timer is anchored). At the
        // START of a practice/qualify session the rider sits in the garage/pit (pit==1) BEFORE
        // ever crossing S/F, so there is no lap to interrupt yet - the out-lap from the garage
        // produces no lap-completion event, so nothing here would consume the flag, and it would
        // wrongly carry the pre-lap garage sit into the FIRST genuine flying lap and suppress its
        // freeze (the reported "first lap didn't freeze" bug). A real mid-lap pit keeps the
        // anchor valid until pit EXIT, so it still latches here. (In spectate/replay the anchor
        // is the only gate; on track isLapTimerValid also requires the sim to be running, which
        // it is while riding through the pits.)
        if (currentPitState == 1 && pluginData.isLapTimerValid()) {
            m_lapInterruptedByPit = true;
        }
        m_cachedPitState = currentPitState;
    }

    // Process any split/lap completion updates
    processTimingUpdates();

    // Check if freeze period has expired
    checkFreezeExpiration();

    // Check if we need frequent updates for ticking timer (uses BaseHud helper)
    checkFrequentUpdates();

    // Segment-timer state changes (points added/removed, run start, chain index) must
    // refresh the line even when no official timing event fires. Live ticking while a
    // segment runs is covered by needsFrequentUpdates above; this catches transitions.
    {
        const PluginData::SegmentTimerData& seg = pluginData.getSegmentTimer();
        long long sig = static_cast<long long>(seg.points.size())
                      | (static_cast<long long>(seg.runningSeg + 1) << 16)
                      | (static_cast<long long>(seg.completionCounter) << 32);
        if (sig != m_cachedSegmentSig) {
            m_cachedSegmentSig = sig;
            setDataDirty();
        }

        // A new segment completion starts the split-style freeze (hold its time on
        // screen for the display duration). With duration 0, no freeze - just live.
        if (seg.completionCounter != m_segCachedCompletion) {
            m_segCachedCompletion = seg.completionCounter;
            if (m_displayDurationMs > 0 && seg.lastSeg >= 0) {
                m_segFrozen = true;
                m_segFrozenAt = std::chrono::steady_clock::now();
            }
            setDataDirty();
        }
        if (m_segFrozen) {
            auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::steady_clock::now() - m_segFrozenAt).count();
            if (elapsed >= m_displayDurationMs) {
                m_segFrozen = false;
                setDataDirty();
            }
        }
        if (seg.segmentCount() < 1) m_segFrozen = false;  // no segments -> nothing to hold
    }

    // Handle dirty flags using base class helper
    processDirtyFlags();
}

void TimingHud::processTimingUpdates() {
    const PluginData& pluginData = PluginData::getInstance();
    const CurrentLapData* currentLap = pluginData.getCurrentLapData();
    const IdealLapData* idealLapData = pluginData.getIdealLapData();

    // Check current lap splits (CurrentLapData tracks accumulated times for current lap)
    if (currentLap) {
        // Check split 1 (accumulated time to S1)
        if (currentLap->split1 > 0 && currentLap->split1 != m_cachedSplit1) {
            int splitTime = currentLap->split1;

            // Update official data cache
            m_officialData.time = splitTime;
            m_officialData.splitIndex = 0;
            m_officialData.lapNum = currentLap->lapNum;
            m_officialData.isInvalid = false;

            // Calculate gaps for all enabled types
            calculateAllGaps(splitTime, 0, false);

            // Freeze display (if freeze is enabled)
            if (m_displayDurationMs > 0) {
                m_isFrozen = true;
                m_frozenAt = std::chrono::steady_clock::now();
            }

            m_cachedSplit1 = currentLap->split1;
            DEBUG_INFO_F("TimingHud: Split 1 crossed, accumulated=%d ms, lap=%d", splitTime, currentLap->lapNum);
            setDataDirty();
        }
        // Check split 2 (accumulated time to S2)
        else if (currentLap->split2 > 0 && currentLap->split2 != m_cachedSplit2) {
            int splitTime = currentLap->split2;

            // Update official data cache
            m_officialData.time = splitTime;
            m_officialData.splitIndex = 1;
            m_officialData.lapNum = currentLap->lapNum;
            m_officialData.isInvalid = false;

            // Calculate gaps for all enabled types
            calculateAllGaps(splitTime, 1, false);

            // Freeze display (if freeze is enabled)
            if (m_displayDurationMs > 0) {
                m_isFrozen = true;
                m_frozenAt = std::chrono::steady_clock::now();
            }

            m_cachedSplit2 = currentLap->split2;
            DEBUG_INFO_F("TimingHud: Split 2 crossed, accumulated=%d ms, lap=%d", splitTime, currentLap->lapNum);
            setDataDirty();
        }
#if GAME_SECTOR_COUNT >= 4
        // Check split 3 (accumulated time to S3) - 4-sector games only
        else if (currentLap->split3 > 0 && currentLap->split3 != m_cachedSplit3) {
            int splitTime = currentLap->split3;

            // Update official data cache
            m_officialData.time = splitTime;
            m_officialData.splitIndex = 2;
            m_officialData.lapNum = currentLap->lapNum;
            m_officialData.isInvalid = false;

            // Calculate gaps for all enabled types
            calculateAllGaps(splitTime, 2, false);

            // Freeze display (if freeze is enabled)
            if (m_displayDurationMs > 0) {
                m_isFrozen = true;
                m_frozenAt = std::chrono::steady_clock::now();
            }

            m_cachedSplit3 = currentLap->split3;
            DEBUG_INFO_F("TimingHud: Split 3 crossed, accumulated=%d ms, lap=%d", splitTime, currentLap->lapNum);
            setDataDirty();
        }
#endif
    }

    // Check for lap completion (split 3/4 / finish line)
    if (idealLapData && idealLapData->lastCompletedLapNum >= 0 &&
        idealLapData->lastCompletedLapNum != m_cachedLastCompletedLapNum) {

        int lapTime = idealLapData->lastLapTime;

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

        // A lap that passed through the pits isn't a genuine timed lap: the live timer was
        // reset on pit exit and re-anchors at this very S/F crossing. There's no timing to
        // invalidate, so don't freeze on it or flash INVALID - just let the freshly started
        // lap tick. (A lap invalidated by cuts, with the timer running throughout, still
        // freezes and shows INVALID.) Consume the flag: the fresh lap starts clean.
        bool pitLap = m_lapInterruptedByPit;
        m_lapInterruptedByPit = false;

        // Update official data cache
        m_officialData.time = lapTime;
        m_officialData.splitIndex = -1;  // Indicates lap complete
        m_officialData.lapNum = completedLapNum;
        m_officialData.isInvalid = !isValid && !pitLap;

        // Calculate gaps for all enabled types (only if valid lap)
        if (isValid && lapTime > 0) {
            calculateAllGaps(lapTime, -1, true);
        } else {
            // Invalid lap - clear all gaps
            m_officialData.gapToPB.reset();
            m_officialData.gapToIdeal.reset();
            m_officialData.gapToOverall.reset();
            m_officialData.gapToAllTime.reset();
            m_officialData.gapToRecord.reset();
            m_officialData.gapToLastLap.reset();
        }

        // Reset split caches for next lap
        m_cachedSplit1 = -1;
        m_cachedSplit2 = -1;
        m_cachedSplit3 = -1;

        // Freeze display (if freeze is enabled). Skip the freeze entirely for a pit-interrupted
        // lap - there's nothing meaningful to hold, so the live timer keeps counting the new lap.
        if (m_displayDurationMs > 0 && !pitLap) {
            m_isFrozen = true;
            m_frozenAt = std::chrono::steady_clock::now();
        }

        m_cachedLastCompletedLapNum = idealLapData->lastCompletedLapNum;
        DEBUG_INFO_F("TimingHud: Lap %d completed, time=%d ms, valid=%d, pitLap=%d",
            completedLapNum, lapTime, isValid, pitLap ? 1 : 0);
        setDataDirty();

        // Cache the updated all-time PB for next lap comparison
        // This captures the new PB (if set) after race_lap_handler has updated StatsManager
        cacheAllTimePB();
    }
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

bool TimingHud::segmentModeActive() const {
    // Segments are a LOCAL-PLAYER training tool, fed only by the player's own RunTelemetry
    // (which flows only while riding). So the panel is in segment mode only when the player
    // is actually on track — never while spectating or watching a replay, where it instead
    // shows the watched rider's regular timing. Gating on ON_TRACK also matches exactly when
    // the segment data is being fed, so a stale run can't surface in spectate/replay.
    const PluginData& data = PluginData::getInstance();
    return data.getSegmentTimer().segmentCount() >= 1
        && data.getDrawState() == PluginConstants::ViewState::ON_TRACK;
}

bool TimingHud::contentVisible() const {
    switch (m_displayMode) {
        case ColumnMode::OFF:
            return false;
        case ColumnMode::SPLITS:
            // In segment mode the panel shows continuously (like ALWAYS), not just on freeze.
            if (segmentModeActive()) return true;
            return m_isFrozen;  // Only during the split/lap freeze
        case ColumnMode::ALWAYS:
            return true;
    }
    return false;
}

bool TimingHud::showingInvalid() const {
    // In segment mode the official split/lap machinery is swapped out, so INVALID never shows.
    if (segmentModeActive()) return false;
    return m_isFrozen && m_officialData.isInvalid;
}

bool TimingHud::needsFrequentUpdates() const {
    const PluginData& data = PluginData::getInstance();

    // Segment mode: a running segment ticks live regardless of display mode / official
    // timer state (including after a session finish — it's a training tool that keeps
    // going on the cool-down lap), but not while spectating/replaying another rider.
    if (segmentModeActive() && data.getSegmentTimer().runningSeg >= 0) return true;

    // Need frequent updates when the ticking time is shown (ALWAYS mode), not frozen, timer valid.
    if (m_isFrozen) return false;
    if (m_displayMode != ColumnMode::ALWAYS || !m_showTime) return false;

    if (!data.isLapTimerValid()) return false;
    if (data.isDisplayRiderFinished()) return false;  // Timer stopped after finish

    return true;
}

void TimingHud::rebuildLayout() {
    // Layout changes require full rebuild since columns are dynamic
    rebuildRenderData();
}

void TimingHud::rebuildRenderData() {
    // Clear render data
    clearStrings();
    m_quads.clear();

    const PluginData& pluginData = PluginData::getInstance();

    // Segment mode: when at least one segment is armed (two boundary points), this
    // timing line shows the custom segment timer instead of the official split/lap.
    // Like the official timer it AGGREGATES: the shown time is the running total
    // from the chain's first point through the current boundary, and the "Best" row
    // is that total vs the summed per-segment bests (so points on the official
    // splits read identically to the regular HUD). Off a clean run it degrades to
    // the isolated arc. The official-timing machinery still runs in update(); we
    // only swap what's rendered. The shown segment is a just-completed one held
    // during the split-style freeze, otherwise the one currently being driven.
    // The segment timer is a training tool that doesn't affect the game result, so it
    // keeps timing through a session finish (warmup/race over) — you can drill segments
    // on the cool-down lap; it stops only when you actually leave the track (handled by
    // segmentModeActive's ON_TRACK gate).
    const PluginData::SegmentTimerData& seg = pluginData.getSegmentTimer();
    bool segmentMode = segmentModeActive();  // off while spectating/replaying another rider
    int segShownIndex = -1;
    bool segShowFrozen = false;
    if (segmentMode) {
        if (m_segFrozen && seg.lastSeg >= 0 && seg.lastSeg < seg.segmentCount()) {
            segShownIndex = seg.lastSeg;
            segShowFrozen = true;
        } else if (seg.runningSeg >= 0 && seg.runningSeg < seg.segmentCount()) {
            segShownIndex = seg.runningSeg;
        }
    }
    // Cumulative "Best" target through segment idx: the summed per-segment bests
    // (the ideal you're chasing), matching the official timer's summed best sectors.
    // -1 if any segment up to idx has no best yet (no clean cumulative reference).
    auto cumBestMsThrough = [&](int idx) -> int {
        if (idx < 0) return -1;
        float sum = 0.0f;
        for (int k = 0; k <= idx; ++k) {
            if (!seg.hasBest[k]) return -1;
            sum += seg.bests[k];
        }
        return static_cast<int>(sum * 1000.0f + 0.5f);
    };
    // Passive "Best" reference: the cumulative target on a clean run (aggregated
    // from the chain start), else this one segment's best (the isolated-arc
    // fallback when no contiguous run is active).
    bool segRunActive = segShowFrozen ? seg.cum.lastValid : seg.cum.active;
    int segRefBestMs = -1;
    if (segShownIndex >= 0) {
        if (segRunActive) {
            segRefBestMs = cumBestMsThrough(segShownIndex);
        } else if (seg.hasBest[segShownIndex]) {
            segRefBestMs = static_cast<int>(seg.bests[segShownIndex] * 1000.0f + 0.5f);
        }
    } else if (segmentMode) {
        // Dead zone: not inside a segment (before the first one, or an untimed stretch on
        // an open chain). Show the whole chain's cumulative best as a passive target so
        // the line is never blank — the analog of the regular timer showing the full-lap
        // PB on the out-lap. -1 (→ "-") until every segment has a best this session.
        segRefBestMs = cumBestMsThrough(seg.segmentCount() - 1);
    }
    GapData segGap;  // cumulative delta-to-best, used only in segment mode

    // Nothing to show right now (Off, or At-Splits between freezes) -> collapse to zero size.
    if (!contentVisible()) {
        setBounds(0.0f, 0.0f, 0.0f, 0.0f);
        return;
    }

    // Rider finished -> hold the total race time (regular timing only; the segment timer
    // keeps running through the finish, see above).
    bool riderFinished = pluginData.isDisplayRiderFinished();
    int riderFinishTime = -1;
    if (riderFinished) {
        const StandingsData* standing = pluginData.getStanding(pluginData.getDisplayRaceNum());
        if (standing) riderFinishTime = standing->finishTime;
    }

    // === TIME CONTENT ===
    // Invalid lap -> "INVALID" in the time cell (comparisons just fall back to their reference).
    // Otherwise: frozen official split/lap time -> finish time -> live elapsed time -> placeholder.
    char timeBuffer[32];
    bool timePlaceholder = false;
    bool timeInvalid = showingInvalid();  // (segmentMode already excluded inside)
    if (timeInvalid) {
        strcpy_s(timeBuffer, sizeof(timeBuffer), "INVALID");
    } else if (m_isFrozen) {
        if (m_officialData.time > 0) {
            PluginUtils::formatLapTime(m_officialData.time, timeBuffer, sizeof(timeBuffer));
        } else {
            strcpy_s(timeBuffer, sizeof(timeBuffer), Placeholders::LAP_TIME);
            timePlaceholder = true;
        }
    } else if (riderFinished && riderFinishTime > 0) {
        PluginUtils::formatLapTime(riderFinishTime, timeBuffer, sizeof(timeBuffer));
    } else {
        int elapsed = pluginData.getElapsedLapTime();
        if (elapsed >= 0) {
            PluginUtils::formatLapTime(elapsed, timeBuffer, sizeof(timeBuffer));
        } else {
            strcpy_s(timeBuffer, sizeof(timeBuffer), Placeholders::LAP_TIME);
            timePlaceholder = true;
        }
    }

    // === SEGMENT MODE OVERRIDE ===
    // Swap the time for the shown segment's, and stage its delta-to-best (rendered below as the
    // single "Best" comparison row).
    if (segmentMode) {
        if (segShowFrozen) {
            // Frozen just-completed boundary: the running total from the chain start
            // (or the isolated arc off a clean run) and its cumulative delta-to-best.
            PluginUtils::formatLapTime(static_cast<int>(seg.cum.lastTime * 1000.0f + 0.5f),
                                       timeBuffer, sizeof(timeBuffer));
            timePlaceholder = false;
            if (seg.cum.lastHasDelta) {
                float dsec = seg.cum.lastTime - seg.cum.lastBest;
                int deltaMs = static_cast<int>(dsec * 1000.0f + (dsec < 0.0f ? -0.5f : 0.5f));
                int refMs = static_cast<int>(seg.cum.lastBest * 1000.0f + 0.5f);
                segGap.set(deltaMs, refMs);
            } else {
                segGap.reset();
            }
        } else if (seg.runningSeg >= 0) {
            // Live: running total from the chain's first point (completed arcs + the
            // live arc) on a clean run, else the isolated live arc. Keeps ticking through
            // a session finish — it's a training tool that ignores the game result.
            double liveArcSec = std::chrono::duration<double>(
                std::chrono::steady_clock::now() - seg.runStart).count();
            double shownSec = seg.cum.active ? (static_cast<double>(seg.cum.time) + liveArcSec)
                                             : liveArcSec;
            PluginUtils::formatLapTime(static_cast<int>(shownSec * 1000.0 + 0.5),
                                       timeBuffer, sizeof(timeBuffer));
            timePlaceholder = false;
            segGap.reset();
        } else {
            strcpy_s(timeBuffer, sizeof(timeBuffer), Placeholders::LAP_TIME);
            timePlaceholder = true;
            segGap.reset();
        }
    }

    // === COMPARISON VALUE RESOLUTION (normal, non-segment rows) ===
    auto getGapDataForType = [&](GapTypeFlags type) -> const GapData* {
        switch (type) {
            case GAP_TO_PB: return &m_officialData.gapToPB;
            case GAP_TO_ALLTIME: return &m_officialData.gapToAllTime;
            case GAP_TO_IDEAL: return &m_officialData.gapToIdeal;
            case GAP_TO_OVERALL: return &m_officialData.gapToOverall;
#if GAME_HAS_RECORDS_PROVIDER
            case GAP_TO_RECORD: return &m_officialData.gapToRecord;
#endif
            case GAP_TO_LASTLAP: return &m_officialData.gapToLastLap;
            default: return nullptr;
        }
    };
    // The split boundary the rider is driving toward, so the passive reference tracks the sector.
    int targetSplit = segmentMode ? -1 : currentTargetSplit();
    // Show the +/- delta while frozen on a split/lap; otherwise the progressive reference time.
    // (An invalid lap clears the gaps, so those cells just fall back to their reference — the
    // "INVALID" flag is shown once, in the time cell.)
    bool showGapData = segmentMode ? (segShowFrozen && seg.cum.lastHasDelta) : m_isFrozen;

    // One rendered comparison value: the +/- delta (active), the target time (passive), or a
    // "-"/"N/A" placeholder. isFaster/isSlower drive the semantic text colour.
    struct RowValue {
        char value[16] = "";
        bool isFaster = false;
        bool isSlower = false;
        bool isReference = false;   // a target time (neutral) vs a delta (green/red) / placeholder (muted)
    };
    auto buildComparison = [&](GapTypeFlags type) -> RowValue {
        RowValue out;
        const GapData* gapData = getGapDataForType(type);
        if (showGapData && gapData && gapData->hasGap) {
            PluginUtils::formatTimeDiff(out.value, sizeof(out.value), gapData->gap);
            out.isFaster = gapData->isFaster;
            out.isSlower = gapData->isSlower;
        } else {
            int refTime = cumulativeReferenceMs(type, targetSplit);
            if (refTime > 0) {
                PluginUtils::formatLapTime(refTime, out.value, sizeof(out.value));
                out.isReference = true;
            } else {
                const char* missing = (type == GAP_TO_RECORD) ? Placeholders::NOT_AVAILABLE : Placeholders::GENERIC;
                strcpy_s(out.value, sizeof(out.value), missing);
            }
        }
        return out;
    };
    auto valueColor = [&](const RowValue& g) -> unsigned long {
        if (g.isFaster) return this->getColor(ColorSlot::POSITIVE);
        if (g.isSlower) return this->getColor(ColorSlot::NEGATIVE);
        if (g.isReference) return this->getColor(ColorSlot::SECONDARY);
        return this->getColor(ColorSlot::MUTED);
    };

    // === BUILD THE ROW LIST (name + value) ===
    struct Row { const char* name; RowValue val; };
    Row rows[GAP_TYPE_COUNT + 1];   // +1 for the segment "Best" row
    int rowCount = 0;
    if (segmentMode) {
        // A custom segment has only its own session best, shown as a single "Best" row.
        RowValue segRow;
        if (showGapData && segGap.hasGap) {
            PluginUtils::formatTimeDiff(segRow.value, sizeof(segRow.value), segGap.gap);
            segRow.isFaster = segGap.isFaster;
            segRow.isSlower = segGap.isSlower;
        } else if (segRefBestMs > 0) {
            PluginUtils::formatLapTime(segRefBestMs, segRow.value, sizeof(segRow.value));
            segRow.isReference = true;
        } else {
            strcpy_s(segRow.value, sizeof(segRow.value), Placeholders::GENERIC);
        }
        rows[rowCount++] = { "Best", segRow };
    } else {
        for (int i = 0; i < GAP_TYPE_COUNT; i++) {
            GapTypeFlags flag = GAP_TYPE_INFO[i].flag;
            if (!(m_enabledComparisons & flag)) continue;
            rows[rowCount++] = { GAP_TYPE_INFO[i].name, buildComparison(flag) };
        }
    }

    if (!m_showTime && rowCount == 0) {
        setBounds(0.0f, 0.0f, 0.0f, 0.0f);
        return;
    }

    // === LAYOUT: a centered vertical stack (big time on top, comparison rows below) ===
    auto dim = getScaledDimensions();

    // Center the panel on CENTER_X, quantizing the anchor to the grid when snapping is on (so the
    // snapped drag offset keeps the left edge on the shared lattice — like the other centered HUDs).
    auto snapCenteringToGrid = [](float x) -> float {
        return UiConfig::getInstance().getGridSnapping()
            ? PluginConstants::HudGrid::SNAP_TO_GRID_X(x)
            : x;
    };

    // Fixed width, matching the NoticesHud (CENTER_STACK_WIDTH_CHARS at the large font + padding),
    // so the two centered top-stack panels line up. Comfortably fits the time and any comparison
    // row (name + value), and a fixed width keeps the panel from jittering as a value flips
    // between a delta and a reference time.
    float innerW = PluginUtils::calculateMonospaceTextWidth(
        WidgetDimensions::CENTER_STACK_WIDTH_CHARS, dim.fontSizeLarge);
    float backgroundWidth = dim.paddingH + innerW + dim.paddingH;

    float bgLeftX = snapCenteringToGrid(CENTER_X - backgroundWidth / 2.0f);

    // Height is a stack of grid-aligned bands: the time row is one lineHeightLarge band (4 snap
    // cells, glyph centered), each comparison row a lineHeightNormal band (2 cells, content
    // centered). No outer padding, so a time-only panel is exactly lineHeightLarge tall —
    // identical to the Notices and Gap Bar boxes — and the whole center-top stack lands on the
    // vertical grid (see TIMING_DIVIDER_Y in notices_hud.cpp).
    float backgroundHeight = (m_showTime ? dim.lineHeightLarge : 0.0f)
                           + rowCount * dim.lineHeightNormal;

    addBackgroundQuad(bgLeftX, START_Y, backgroundWidth, backgroundHeight);

    // Text inset is HALF the width padding (1 grid cell instead of 2), so the horizontal gap
    // from the box edge to the edge-aligned label/value matches the vertical gap of the LARGE
    // glyph in its band — roughly uniform padding all round. The box WIDTH still budgets the
    // full HUD_HORIZONTAL each side above (it stays locked to the Notices / center-stack width),
    // so this only shifts the left/right-aligned text one grid cell outward each side; both
    // insets remain on the snap grid (backgroundWidth is a whole number of cells). The centered
    // time is unaffected. (Notices needs no equivalent — its text is center-justified.)
    const float textInsetH = dim.paddingH * 0.5f;
    const float leftTextX  = bgLeftX + textInsetH;
    const float rightTextX = bgLeftX + backgroundWidth - textInsetH;
    const float centerX    = bgLeftX + backgroundWidth / 2.0f;

    float y = START_Y;

    // Big time row: the large glyph centered in its lineHeightLarge band, using the same
    // centering formula as the Notices/Gap Bar boxes so the time value sits at an identical
    // vertical position within its band. Red on an invalid lap, muted for a placeholder, else
    // primary.
    if (m_showTime) {
        unsigned long timeColor = timeInvalid   ? this->getColor(ColorSlot::NEGATIVE)
                                : timePlaceholder ? this->getColor(ColorSlot::MUTED)
                                                  : this->getColor(ColorSlot::PRIMARY);
        float timeY = y + (dim.lineHeightLarge - dim.fontSizeLarge) * 0.5f;
        addString(timeBuffer, centerX, timeY, Justify::CENTER,
            this->getFont(FontCategory::DIGITS), timeColor, dim.fontSizeLarge);
        y += dim.lineHeightLarge;
    }

    // Comparison rows: name (left) + value (right), each vertically centered in its
    // lineHeightNormal band. The name is a row label — STRONG font at the Small size,
    // row-centered — via addLabel(), matching the other HUDs' labels (and the old secondary
    // chips); the value is the data font (DIGITS) at the normal size, centered in the band the
    // same way. Only the VALUE carries the semantic colour (green faster / red slower / neutral
    // reference); no colored background strips.
    float valueRowOffset = (dim.lineHeightNormal - dim.fontSize) * 0.5f;
    for (int i = 0; i < rowCount; i++) {
        const Row& r = rows[i];
        addLabel(r.name, leftTextX, y, Justify::LEFT,
            this->getFont(FontCategory::STRONG), this->getColor(ColorSlot::TERTIARY), dim);
        addString(r.val.value, rightTextX, y + valueRowOffset, Justify::RIGHT,
            this->getFont(FontCategory::DIGITS), valueColor(r.val), dim.fontSize);
        y += dim.lineHeightNormal;
    }

    setBounds(bgLeftX, START_Y, bgLeftX + backgroundWidth, START_Y + backgroundHeight);
}


void TimingHud::resetToDefaults() {
    // On by default (changed from off in v1.27.1). UPGRADE NOTE: under sparse-save,
    // a user who explicitly disabled Timing while OFF was the default saved no
    // `visible` key (it matched the default), so on upgrade they are indistinguishable
    // from "never touched" and Timing re-appears. This is inherent to any default
    // flip with sparse persistence — call it out in the release notes.
    m_bVisible = true;
    m_bShowTitle = false;
    setTextureVariant(0);  // No texture by default
    m_fBackgroundOpacity = 0.1f;
    m_fScale = 1.0f;
    setPosition(0.0f, DEFAULT_POSITION_Y);

    // Show mode: Always show by default (content shows continuously, references passive)
    m_displayMode = ColumnMode::ALWAYS;
    m_showTime = true;                           // big time row on by default
    m_displayDurationMs = DEFAULT_DURATION_MS;   // 5 seconds freeze

    // Comparison rows: Session PB + All-Time PB by default
    m_enabledComparisons = GAP_DEFAULT_ENABLED;

    // Reset live timing state
    resetLiveTimingState();

    setDataDirty();
}
