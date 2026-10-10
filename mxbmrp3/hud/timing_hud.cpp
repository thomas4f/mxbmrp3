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
#include "center_stack.h"
#include "../core/color_config.h"
#include "../core/stats_manager.h"
#include "../core/hud_manager.h"
#include "fuel_widget.h"       // the readout row reads its estimate rather than re-deriving one

using namespace PluginConstants;

// Positioning constants
namespace {
    constexpr float START_X = 0.0f;
    constexpr float START_Y = 0.0f;

    // Default vertical position is LAST in the center-top stack, at CenterStack::timingTop()
    // -- see hud/center_stack.h. The Timing HUD grows DOWN, so sitting at the bottom means it
    // never overlaps the notice/gapbar above however many comparison rows are enabled.
}

TimingHud::TimingHud()
    : m_displayDurationMs(FreezeDuration::FOLLOW_DEFAULT)
    , m_showTime(true)
    , m_enabledComparisons(GAP_DEFAULT_ENABLED)
    , m_cachedDisplayRaceNum(-1)
    , m_cachedSessionGeneration(-1)
    , m_cachedPBScope(PBScope::CATEGORY)
    , m_cachedPitState(-1)
    , m_previousAllTimeLap(-1)
    , m_previousAllTimeSector1(-1)
    , m_previousAllTimeS1PlusS2(-1)
    , m_previousAllTimeS1PlusS2PlusS3(-1)
{
    // TITLE RESTORED, TEMPORARILY. This panel was one of the three the caption was taken
    // from (see BaseHud::m_titleSupported for the twelve that keep it off). It is back so
    // the reason the caption was unwanted can be shown rather than described -- nothing
    // else about this HUD reverted with it: the panel, its body card, the coloured
    // block's outset and the stack spacing are all as the last few commits left them.
    // One-time setup
    setDraggable(true);
    // Body cards, one PER SECTION, because this panel is two things: the lap TIME,
    // and what that time is being COMPARED against. One card around both says they
    // are one list, and the big time glyph then reads as the first row of it.
    // See BaseHud::m_bContentSections.
    m_bContentCard = true;
    m_bContentSections = true;
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
    // The Gap section's freeze watches the crossings whether or not the panel or the row
    // is shown (see OfficialGapFreeze): a split crossed while hidden must not read as
    // new the moment it is shown.
    if (m_liveGapFreeze.update(getLiveGapReference(), freezeMs()) && m_liveGapOn) {
        setDataDirty();
    }

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

        // Full reset on spectate change (the crossings adopt the new rider's
        // current splits without triggering a display)
        resetLiveTimingState();
        m_cachedDisplayRaceNum = currentDisplayRaceNum;
        m_cachedPitState = -1;  // Reset pit state cache for new rider

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
        // Whether the lap that ends at the next S/F went through the pits is
        // PluginData's fact now (markLapViaPits), read off the LapLogEntry when the
        // lap completes. The latch that lived here missed a pit taken from the
        // menu: it keyed on the pit flag while the sim ran, and the sim stops first.
        m_cachedPitState = currentPitState;
    }

    // The Gap section ticks while its gap is live (needsFrequentUpdates); the flips into
    // and out of having one must redraw too, or the last number outlives the gap.
    if (m_liveGapOn) {
        const bool live = pluginData.hasValidLiveGap(getLiveGapReference());
        if (live != m_liveGapWasLive) {
            m_liveGapWasLive = live;
            setDataDirty();
        }
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
            if (freezeMs() > 0 && seg.lastSeg >= 0) {
                m_segHold.start();
            }
            setDataDirty();
        }
        if (m_segHold.expire(freezeMs())) setDataDirty();
        if (seg.segmentCount() < 1) m_segHold.stop();  // no segments -> nothing to hold
    }

    // Handle dirty flags using base class helper
    processDirtyFlags();
}

void TimingHud::processTimingUpdates() {
    const SplitCrossingDetector::Result crossing = m_crossings.poll();
    if (crossing.adopted) return;

    // The line first (split_crossing.h): it ends the lap.
    if (crossing.line) {
        // A pit lap is not a timed lap: the live timer was reset on pit exit and
        // re-anchors at this very S/F crossing, so there is no timing to
        // invalidate and INVALID must not flash; the freshly started lap just
        // ticks. A lap invalidated by cuts, with the timer running throughout,
        // still freezes and shows INVALID.
        const bool pitLap = crossing.lapViaPits;
        const int lapTime = crossing.lapTime;

        // Update official data cache
        m_officialData.time = lapTime;
        m_officialData.splitIndex = -1;  // Indicates lap complete
        m_officialData.lapNum = crossing.lapNum;
        m_officialData.isInvalid = !crossing.lapValid && !pitLap;

        // Calculate gaps for all enabled types (only if valid lap)
        if (crossing.lapValid && lapTime > 0) {
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

        // Freeze display (if freeze is enabled). Skip the freeze entirely for a pit-interrupted
        // lap - there's nothing meaningful to hold, so the live timer keeps counting the new lap.
        if (freezeMs() > 0 && !pitLap) {
            m_hold.start();
        }

        DEBUG_INFO_F("TimingHud: Lap %d completed, time=%d ms, valid=%d, pitLap=%d",
            crossing.lapNum, lapTime, crossing.lapValid ? 1 : 0, pitLap ? 1 : 0);
        setDataDirty();

        // Cache the updated all-time PB for next lap comparison
        // This captures the new PB (if set) after race_lap_handler has updated StatsManager
        cacheAllTimePB();
    }

    // Official splits (accumulated time to S1, S2 and, in 4-sector games, S3)
    if (crossing.splitIndex >= 0) {
        const CurrentLapData* currentLap = PluginData::getInstance().getCurrentLapData();

        // Update official data cache
        m_officialData.time = crossing.splitTime;
        m_officialData.splitIndex = crossing.splitIndex;
        m_officialData.lapNum = currentLap ? currentLap->lapNum : -1;
        m_officialData.isInvalid = false;

        // Calculate gaps for all enabled types
        calculateAllGaps(crossing.splitTime, crossing.splitIndex, false);

        // Freeze display (if freeze is enabled)
        if (freezeMs() > 0) {
            m_hold.start();
        }

        DEBUG_INFO_F("TimingHud: Split %d crossed, accumulated=%d ms", crossing.splitIndex + 1, crossing.splitTime);
        setDataDirty();
    }
}

void TimingHud::checkFreezeExpiration() {
    if (m_hold.expire(freezeMs())) setDataDirty();
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
            return m_hold.active();  // Only during the split/lap freeze
        case ColumnMode::ALWAYS:
            return true;
    }
    return false;
}

bool TimingHud::showingInvalid() const {
    // In segment mode the official split/lap machinery is swapped out, so INVALID never shows.
    if (segmentModeActive()) return false;
    return m_hold.active() && m_officialData.isInvalid;
}

bool TimingHud::needsFrequentUpdates() const {
    const PluginData& data = PluginData::getInstance();

    // Segment mode: a running segment ticks live regardless of display mode / official
    // timer state (including after a session finish — it's a training tool that keeps
    // going on the cool-down lap), but not while spectating/replaying another rider.
    if (segmentModeActive() && data.getSegmentTimer().runningSeg >= 0) return true;

    // The Gap section moves with the clock while it reads live (not while it holds an
    // official gap, and not in segment mode, which has its own single reference).
    if (m_liveGapOn && !segmentModeActive() && !m_liveGapFreeze.isFrozen() && contentVisible() &&
        data.hasValidLiveGap(getLiveGapReference())) {
        return true;
    }

    // Need frequent updates when the ticking time is shown (ALWAYS mode), not frozen, timer valid.
    if (m_hold.active()) return false;
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

    // Segment mode: what the custom segment timer shows instead of the official
    // split/lap (see resolveSegmentView).
    SegmentView sv = resolveSegmentView(pluginData);

    // Nothing to show right now (Off, or At-Splits between freezes) -> collapse to zero size.
    //
    // ...unless the Timing tab is open (isPreviewing). At-Splits shows the panel
    // for a few seconds a lap, which is no time to drag it into place; the
    // readouts below already draw their own placeholders when they have no value.
    // OFF is excluded deliberately: that switch is the player saying they do not
    // want this panel, and a preview would argue with it.
    const bool preview = isPreviewing() && m_displayMode != ColumnMode::OFF;
    if (!contentVisible() && !preview) {
        setBounds(0.0f, 0.0f, 0.0f, 0.0f);
        return;
    }

    // === TIME CONTENT === (and, in segment mode, the shown segment's time + delta)
    TimeCell timeCell;
    formatTimeCell(pluginData, sv, timeCell);

    // === COMPARISON VALUE RESOLUTION (normal, non-segment rows) ===
    const PluginData::SegmentTimerData& seg = pluginData.getSegmentTimer();
    // The split boundary the rider is driving toward, so the passive reference tracks the sector.
    int targetSplit = sv.active ? -1 : currentTargetSplit();
    // Show the +/- delta while frozen on a split/lap; otherwise the progressive reference time.
    // (An invalid lap clears the gaps, so those cells just fall back to their reference — the
    // "INVALID" flag is shown once, in the time cell.)
    bool showGapData = sv.active ? (sv.frozen && seg.cum.lastHasDelta) : m_hold.active();

    // === BUILD THE ROW LIST (name + value) ===
    Row rows[GAP_TYPE_COUNT + 1];   // +1 for the segment "Best" row
    const int rowCount = buildComparisonRows(rows, sv, showGapData, targetSplit);

    // Hoisted above the readout build, which sizes its two TEXT rows from the font
    // metrics; the layout section below reuses this rather than reading twice.
    auto dim = getScaledDimensions();

    Readout readouts[READOUT_COUNT];
    const int readoutCount = buildReadouts(readouts);

    // The Gap section: the Gap Bar's reading, large, in its own card under the time.
    // Not in segment mode, which compares against its own single reference.
    const bool showGap = m_liveGapOn && !sv.active;

    if (!m_showTime && !showGap && rowCount == 0 && readoutCount == 0) {
        setBounds(0.0f, 0.0f, 0.0f, 0.0f);
        return;
    }

    // === LAYOUT: a centered vertical stack (big time on top, comparison rows below) ===
    // BOX-MODEL: two sibling section cards — the big time in one, the
    // comparison rows in the other. The seam between them is the sum of the
    // facing [content] margins (the model's rule), so the overlap the old
    // sectionGapY() reservation existed to prevent cannot arise, and the panel
    // height is the engine's ceil rather than a hand-summed stack.
    PanelWant want;
    // Fixed width, matching the NoticesHud, so the centered top-stack panels
    // line up: the stack's shared width rides as the panel MINIMUM.
    wantCenterStackWidth(want, dim);   // the stack minimum owns the width
    if (m_showTime) want.sectionH.push_back(bigValueRowHeight(dim));
    if (showGap) want.sectionH.push_back(bigValueRowHeight(dim));
    if (rowCount > 0) want.sectionH.push_back(rowCount * dim.lineHeightNormal);
    if (readoutCount > 0) want.sectionH.push_back(readoutCount * dim.lineHeightNormal);
    want.captionW = planTitleWidth(dim, "Timing", TitleTier::Large);
    want.tier = TitleTier::Large;
    PanelPlan& p = planPanel(dim, want);

    const float backgroundWidth = p.width();
    const float backgroundHeight = p.height();
    // CENTRE-ANCHORED, like the rest of the centre stack: offsetX is the CENTRE
    // (a stored delta from it until the settings v7 migration). Half this panel's
    // own width to the left of that, so it stays centred as the width changes and
    // keeps sharing edges with the equally wide NoticesHud above it.
    const float bgLeftX = centerAnchoredPanelLeft(backgroundWidth);

    addPlanBackground(p, bgLeftX, START_Y);
    addPlanTitle(p, "Timing",
                 this->getColor(ColorSlot::PRIMARY));

    // Text columns: the rows' own content box, both edges read from the plan. The
    // right one used to be the LEFT inset mirrored, on the reasoning quoted here that
    // "the box is symmetric unless a theme sets per-side terms" -- which is true, and
    // is exactly the case it got wrong: a theme CAN set them, and one that did pulled
    // every right-aligned value a whole left border inward.
    const float leftTextX = p.contentX();
    const float rightTextX = p.contentRight();
    // Centred text anchors at the CARD's centre (PanelPlan::sectionBoxCenterX),
    // the horizontal half of the sectionBox centring the big time gets below.
    const float centerX = p.sectionBoxCenterX();

    size_t section = 0;
    if (m_showTime) {
        addTimeSection(p, section, timeCell, centerX, dim);
        section++;
    }

    if (showGap) {
        addGapSection(p, section, centerX, dim);
        section++;
    }

    if (rowCount > 0) {
        addComparisonRows(rows, rowCount, p.contentY(section), leftTextX, rightTextX, dim);
        section++;
    }


    // Same label-left / value-right shape as the comparison rows above, in the
    // neutral colour: these are readings, not deltas, so there is nothing for the
    // faster/slower colouring to say about them.
    if (readoutCount > 0) {
        addReadoutRows(readouts, readoutCount, p.contentY(section), leftTextX, rightTextX, dim);
    }


    // What the box plan actually spent, for timing_reference_test. The panel is
    // on the box model, so its chrome is boxPanelPadding — NOT the legacy
    // ScaledDimensions::paddingV, which still reports panelPaddingYCells for
    // the panels that have not moved. Reporting the plan's own numbers is what
    // keeps that test an assertion about this panel rather than about which
    // padding vocabulary it happens to be written in.
    // MINUS the ceil slack, which the last section absorbs: that remainder is the
    // panel rounding itself to a whole cell, not a cost the rows asked for, and
    // leaving it in makes "what does a row cost" unanswerable.
    m_fTestContentTop = p.Y(p.g.sections.front().top) - START_Y;
    m_fTestContentBot = p.Y(p.g.sections.back().bot - p.g.slackY) - START_Y;

    setBounds(bgLeftX, START_Y, bgLeftX + backgroundWidth, START_Y + backgroundHeight);
}

TimingHud::SegmentView TimingHud::resolveSegmentView(const PluginData& pluginData) const {
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
        if (m_segHold.active() && seg.lastSeg >= 0 && seg.lastSeg < seg.segmentCount()) {
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

    SegmentView sv;
    sv.active = segmentMode;
    sv.frozen = segShowFrozen;
    sv.refBestMs = segRefBestMs;
    return sv;
}

// The big time cell. Stages segment mode's delta-to-best into sv.gap.
void TimingHud::formatTimeCell(const PluginData& pluginData, SegmentView& sv, TimeCell& cell) const {
    const PluginData::SegmentTimerData& seg = pluginData.getSegmentTimer();
    const bool segmentMode = sv.active;
    const bool segShowFrozen = sv.frozen;
    GapData& segGap = sv.gap;
    char (&timeBuffer)[32] = cell.text;
    bool& timePlaceholder = cell.placeholder;
    bool& timeInvalid = cell.invalid;

    // Rider finished -> hold the total race time (regular timing only; the segment timer
    // keeps running through the finish, see resolveSegmentView).
    bool riderFinished = pluginData.isDisplayRiderFinished();
    int riderFinishTime = -1;
    if (riderFinished) {
        const StandingsData* standing = pluginData.getStanding(pluginData.getDisplayRaceNum());
        if (standing) riderFinishTime = standing->finishTime;
    }

    // === TIME CONTENT ===
    // Invalid lap -> "INVALID" in the time cell (comparisons just fall back to their reference).
    // Otherwise: frozen official split/lap time -> finish time -> live elapsed time -> placeholder.
    timeInvalid = showingInvalid();  // (segmentMode already excluded inside)
    if (timeInvalid) {
        strcpy_s(timeBuffer, sizeof(timeBuffer), Placeholders::INVALID_LAP);
    } else if (m_hold.active()) {
        if (m_officialData.time > 0) {
            PluginUtils::formatLapTime(m_officialData.time, timeBuffer, sizeof(timeBuffer));
        } else {
            strcpy_s(timeBuffer, sizeof(timeBuffer), Placeholders::GENERIC);
            timePlaceholder = true;
        }
    } else if (riderFinished && riderFinishTime > 0) {
        PluginUtils::formatLapTime(riderFinishTime, timeBuffer, sizeof(timeBuffer));
    } else {
        int elapsed = pluginData.getElapsedLapTime();
        if (elapsed >= 0) {
            PluginUtils::formatLapTime(elapsed, timeBuffer, sizeof(timeBuffer));
        } else {
            strcpy_s(timeBuffer, sizeof(timeBuffer), Placeholders::GENERIC);
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
            strcpy_s(timeBuffer, sizeof(timeBuffer), Placeholders::GENERIC);
            timePlaceholder = true;
            segGap.reset();
        }
    }
}

// The comparison rows (name + value): one "Best" row in segment mode, else one per
// enabled comparison. Returns the row count.
int TimingHud::buildComparisonRows(Row* rows, const SegmentView& sv, bool showGapData, int targetSplit) const {
    const bool segmentMode = sv.active;
    const int segRefBestMs = sv.refBestMs;
    const GapData& segGap = sv.gap;

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
    auto buildComparison = [&](GapTypeFlags type) -> RowValue {
        RowValue out;
        const GapData* gapData = getGapDataForType(type);
        if (showGapData && gapData && gapData->hasGap) {
            PluginUtils::formatTimeDiff(out.value, sizeof(out.value), gapData->gap);
            out.delta = gapData->gap;
            out.isDelta = true;
        } else {
            int refTime = cumulativeReferenceMs(type, targetSplit);
            if (refTime > 0) {
                PluginUtils::formatLapTime(refTime, out.value, sizeof(out.value));
                out.isReference = true;
            } else {
                // "N/A" (not "-") when the comparison genuinely doesn't apply: no record
                // fetched, or a player-scoped reference while spectating someone else.
                // "-" stays for a reference that simply has no time YET this session.
                const bool notApplicable = (type == GAP_TO_RECORD) ||
                                           !comparisonAppliesToDisplayRider(type);
                const char* missing = notApplicable ? Placeholders::NOT_AVAILABLE : Placeholders::GENERIC;
                strcpy_s(out.value, sizeof(out.value), missing);
            }
        }
        return out;
    };

    int rowCount = 0;
    if (segmentMode) {
        // A custom segment has only its own session best, shown as a single "Best" row.
        RowValue segRow;
        if (showGapData && segGap.hasGap) {
            PluginUtils::formatTimeDiff(segRow.value, sizeof(segRow.value), segGap.gap);
            segRow.delta = segGap.gap;
            segRow.isDelta = true;
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
    return rowCount;
}

// The readout rows (the second, non-comparison section). Returns the row count.
int TimingHud::buildReadouts(Readout* readouts) const {
    //
    // Every value here is READ from the source that already owns it, never
    // re-derived: the session clock through formatSessionClock (the one source
    // in-game and the web overlay share, overtime labels and all), the fuel
    // estimate through FuelWidget, which accumulates the per-lap history the
    // estimate needs. A second accumulation would be a second answer.
    int readoutCount = 0;
    if (m_enabledReadouts != READOUT_NONE) {
        const PluginData& pd = PluginData::getInstance();
        const SessionData& sd = pd.getSessionData();
        const int me = pd.getDisplayRaceNum();
        auto add = [&](ReadoutFlags flag, const char* fmt, auto... args) {
            if (!(m_enabledReadouts & flag)) return;
            Readout& r = readouts[readoutCount++];
            r.name = READOUT_INFO[readoutIndexOf(flag)].name;
            snprintf(r.value, sizeof(r.value), fmt, args...);
        };

        const int position = pd.getDisplayPositionForRaceNum(me);
        const int entries = static_cast<int>(pd.getDisplayClassificationOrder().size());
        if (position > 0 && entries > 0) add(READOUT_POSITION, "%d/%d", position, entries);
        else                             add(READOUT_POSITION, "%s", Placeholders::GENERIC);

        const StandingsData* mine = pd.getStanding(me);
        const int lap = mine ? mine->numLaps + 1 : 0;   // numLaps counts COMPLETED laps
        if (lap <= 0)                 add(READOUT_LAP, "%s", Placeholders::GENERIC);
        // A total only for a pure lap race, the rule LapWidget and Standings use: in
        // a time+laps race sessionNumLaps is the overtime laps, so "5/2" would read
        // as lap five of two.
        else if (sd.sessionNumLaps > 0 && sd.sessionLength <= 0)
                                        add(READOUT_LAP, "%d/%d", lap, sd.sessionNumLaps);
        else                            add(READOUT_LAP, "%d", lap);

        if (m_enabledReadouts & READOUT_TIME) {
            Readout& r = readouts[readoutCount++];
            r.name = READOUT_INFO[readoutIndexOf(READOUT_TIME)].name;
            PluginUtils::formatSessionClock(pd.getLeaderLapsToGo(), pd.getSessionTime(),
                                            r.value, sizeof(r.value));
        }

        if (m_enabledReadouts & READOUT_SESSION) {
            Readout& r = readouts[readoutCount++];
            r.name = READOUT_INFO[readoutIndexOf(READOUT_SESSION)].name;
            // The same helper SessionHud prints, so "10:00 + 2L" reads
            // identically in both places and a format change lands in one.
            PluginUtils::formatSessionFormat(sd.sessionLength, sd.sessionNumLaps,
                                             r.value, sizeof(r.value));
            // An unlimited session (practice with no clock and no lap target) has
            // no format to state, and the helper answers with an empty string. A
            // row with nothing after its label reads as broken, so it takes the
            // same placeholder every other row here uses when it has no value.
            if (r.value[0] == '\0') strcpy_s(r.value, sizeof(r.value), Placeholders::GENERIC);
        }

        if (m_enabledReadouts & READOUT_FUEL) {
            Readout& r = readouts[readoutCount++];
            r.name = READOUT_INFO[readoutIndexOf(READOUT_FUEL)].name;
            const float laps = HudManager::getInstance().getFuelWidget().getLapsRemaining();
            // THE UNIT IS IN THE VALUE, not the label. A bare "3.2" beside "Fuel"
            // reads as litres as easily as laps, and the label column is the one
            // that cannot grow -- it is sized by "Last Lap" above. The value column
            // already carries "10:00 + 2L", so "3.2 laps" costs nothing.
            if (laps >= 0.0f) snprintf(r.value, sizeof(r.value), "%.1f laps", laps);
            else strcpy_s(r.value, sizeof(r.value), Placeholders::GENERIC);
        }

        // SERVER and TRACK, read from the same places the Session panel reads them --
        // PluginUtils::serverLabel (which answers "Online" for a game whose API has no
        // serverType once a real opponent is present) and SessionData::trackName. A
        // second derivation here would be a second answer to "what server am I on".
        auto addText = [&](ReadoutFlags flag, const char* text) {
            if (!(m_enabledReadouts & flag)) return;
            Readout& r = readouts[readoutCount++];
            r.name = READOUT_INFO[readoutIndexOf(flag)].name;
            r.isText = true;
            const char* src = (text && text[0] != '\0') ? text : Placeholders::GENERIC;
            // Stored WHOLE. What fits is a property of the drawn row -- its label, the
            // fonts, the panel's content width -- and none of that is known until the
            // plan below exists, so the fitting happens at draw.
            strncpy_s(r.value, sizeof(r.value), src, _TRUNCATE);
        };
        addText(READOUT_SERVER,
                PluginUtils::serverLabel(sd.serverType, sd.serverName,
                                         static_cast<int>(pd.getRaceEntries().size())));
        addText(READOUT_TRACK, sd.trackName);
    }
    return readoutCount;
}

void TimingHud::addTimeSection(const PanelPlan& p, size_t section, const TimeCell& cell, float centerX,
                               const ScaledDimensions& dim) {
    const bool timeInvalid = cell.invalid;
    const bool timePlaceholder = cell.placeholder;
    const char* timeBuffer = cell.text;
    unsigned long timeColor = timeInvalid   ? this->getColor(ColorSlot::NEGATIVE)
                            : timePlaceholder ? this->getColor(ColorSlot::MUTED)
                                              : this->getColor(ColorSlot::PRIMARY);
    // INK-centred in the section's box. Card or not, sections[].top/bot is
    // the drawn extent — cardless it degenerates to the content rows, and
    // the last section carries the panel's ceil remainder either way, so
    // centring here keeps the digits centred in what the player sees.
    // The section's DRAWN box, via the shared accessor -- this HUD spelled it by
    // hand and was the only panel getting it right; see PanelPlan::sectionBoxY.
    float timeY = inkCenteredY(p.sectionBoxY(section), p.sectionBoxH(section),
                               dim.fontSizeLarge);
    // INVALID is a word, so it takes the text font, not Digits.
    addString(timeBuffer, centerX, timeY, Justify::CENTER,
        this->getFont(timeInvalid ? FontCategory::NORMAL : FontCategory::DIGITS), timeColor, dim.fontSizeLarge);
}

void TimingHud::addGapSection(const PanelPlan& p, size_t section, float centerX,
                              const ScaledDimensions& dim) {
    // Exactly the Gap Bar's text: the shown gap (OfficialGapFreeze::shownGap) as a
    // delta in its colour, else a MUTED placeholder -- drawn like the big time above.
    char text[32];
    unsigned long color;
    int gapMs = 0;
    if (m_liveGapFreeze.shownGap(getLiveGapReference(), &gapMs)) {
        PluginUtils::formatTimeDiff(text, sizeof(text), gapMs);
        color = this->deltaColor(gapMs);
    } else {
        strcpy_s(text, sizeof(text), Placeholders::GENERIC);
        color = this->getColor(ColorSlot::MUTED);
    }
    const float y = inkCenteredY(p.sectionBoxY(section), p.sectionBoxH(section), dim.fontSizeLarge);
    addString(text, centerX, y, Justify::CENTER,
        this->getFont(FontCategory::DIGITS), color, dim.fontSizeLarge);
}

void TimingHud::addComparisonRows(const Row* rows, int rowCount, float y, float leftTextX, float rightTextX,
                                  const ScaledDimensions& dim) {
    auto valueColor = [&](const RowValue& g) -> unsigned long {
        if (g.isDelta) return this->deltaColor(g.delta);
        if (g.isReference) return this->getColor(ColorSlot::SECONDARY);
        return this->getColor(ColorSlot::MUTED);
    };
    for (int i = 0; i < rowCount; i++) {
        const Row& r = rows[i];
        addLabel(r.name, leftTextX, y, Justify::LEFT, this->getColor(ColorSlot::TERTIARY), dim);
        addString(r.val.value, rightTextX, y, Justify::RIGHT,
            this->getFont(FontCategory::DIGITS), valueColor(r.val), dim.fontSize);
        y += dim.lineHeightNormal;
    }
}

void TimingHud::addReadoutRows(Readout* readouts, int readoutCount, float y, float leftTextX, float rightTextX,
                               const ScaledDimensions& dim) {
    // WHAT ACTUALLY FITS, measured against the row rather than assumed.
    //
    // The value is right-justified at contentRight() and the label left-justified
    // at contentX(), so a value may use the row MINUS its own label and a space.
    // Per row, because the label is what it competes with: "Position" costs the
    // Position row two characters and costs Server nothing.
    //
    // THE FIRST VERSION OF THIS GOT IT BADLY WRONG, and the arithmetic is worth
    // stating so it is not repeated. It derived the budget from
    // CENTER_STACK_WIDTH_CHARS, treating that 14 as normal-size characters -- but
    // CenterStack::boxWidth measures those 14 at fontSizeLARGE, the size the big
    // time above is drawn in. The panel is therefore half again wider in
    // normal-size characters than the constant suggests, and the budget came out
    // at 8 when the row had room for far more. Reported from a screenshot with
    // "Demo Ser" cut short beside an obviously empty column.
    const float rowW      = rightTextX - leftTextX;
    const float valueChar = PluginUtils::calculateMonospaceTextWidth(1, dim.fontSize);

    // ONE budget for the section, sized by the LONGEST label in it rather than
    // per row. Per-row was the first attempt and it is worse in two ways: the
    // column appears to change width from row to row for no reason a reader can
    // see, and it gives a test no single number to assert against. The cost is a
    // character or two on the short-labelled rows, and only Server and Track are
    // ever long enough to notice.
    float widestLabel = 0.0f;
    for (int i = 0; i < readoutCount; i++) {
        widestLabel = (std::max)(widestLabel, PluginUtils::calculateMonospaceTextWidth(
            static_cast<int>(std::strlen(readouts[i].name)), dim.fontSizeSmall));
    }
    // One value-character of air between the columns, so a full-width value
    // cannot touch its label.
    m_lastReadoutBudget = (std::max)(1, static_cast<int>(
        (rowW - widestLabel - valueChar) / valueChar));

    for (int i = 0; i < readoutCount; i++) {
        addLabel(readouts[i].name, leftTextX, y, Justify::LEFT, this->getColor(ColorSlot::TERTIARY), dim);

            // Cut in place (PluginUtils::fitTextInPlace): no per-row std::string.
            const size_t len = std::strlen(readouts[i].value);
            if (static_cast<int>(len) > m_lastReadoutBudget) {
                PluginUtils::fitTextInPlace(readouts[i].value, len, m_lastReadoutBudget);
            }
            const char* value = readouts[i].value;

        addString(value, rightTextX, y, Justify::RIGHT,
            this->getFont(readouts[i].isText ? FontCategory::NORMAL : FontCategory::DIGITS),
            this->getColor(ColorSlot::SECONDARY), dim.fontSize);
        y += dim.lineHeightNormal;
    }
}


void TimingHud::resetToDefaults() {
    // On by default (changed from off in v1.27.1). UPGRADE NOTE: under sparse-save,
    // a user who explicitly disabled Timing while OFF was the default saved no
    // `visible` key (it matched the default), so on upgrade they are indistinguishable
    // from "never touched" and Timing re-appears. This is inherent to any default
    // flip with sparse persistence — call it out in the release notes.
    m_bVisible = true;
    // Off by DEFAULT, not unavailable -- the toggle is in the Timing tab. A caption
    // over a panel this close to the centre of the screen is usually noise, which is
    // why it starts off rather than why it does not exist.
    m_bShowTitle = false;
    setTextureVariant(0);  // No texture by default
    m_fBackgroundOpacity = 0.1f;
    setScale(1.0f);
    setPosition(CENTER_ANCHOR_X, CenterStack::stackBoxTop());

    // Show mode: Always show by default (content shows continuously, references passive)
    m_displayMode = ColumnMode::ALWAYS;
    m_showTime = true;                           // big time row on by default
    m_displayDurationMs = FreezeDuration::FOLLOW_DEFAULT;

    // Comparison rows: Session PB + All-Time PB by default
    m_enabledComparisons = GAP_DEFAULT_ENABLED;
    // Readout rows: none. This panel is read at a glance mid-corner, so extra
    // rows are opt-in rather than a new default (see ReadoutFlags).
    m_enabledReadouts = READOUT_DEFAULT_ENABLED;
    // Gap section: off, following General's reference once switched on.
    m_liveGapOn = false;
    m_liveGapDefault = true;
    m_liveGapRef = GapRef::SESSION_PB;
    m_liveGapFreeze.reset();

    // Reset live timing state
    resetLiveTimingState();

    setDataDirty();
}
