// ============================================================================
// hud/notices_hud.cpp
// Notices HUD - displays warnings and PB notifications
// Shows centered notices above the timing HUD area
// ============================================================================
#include "notices_hud.h"

#include <cstdio>
#include <cmath>
#include <string>

#include "../diagnostics/logger.h"
#include "../core/plugin_utils.h"
#include "../core/widget_constants.h"
#include "../core/color_config.h"
#include "notice_priority.h"
#include "center_stack.h"

using namespace PluginConstants;

// Ordinal suffix for position display (1ST, 2ND, 3RD, 4TH, ...)
static const char* ordinalSuffix(int n) {
    if (n % 100 >= 11 && n % 100 <= 13) return "TH";
    switch (n % 10) {
        case 1: return "ST";
        case 2: return "ND";
        case 3: return "RD";
        default: return "TH";
    }
}

// Center display positioning constants (fixed center-screen layout)
namespace {
    // The notice grows UP, with its BOTTOM one row-gap above this divider -- which is
    // the same line the Timing box starts on. Cell 11; see hud/center_stack.h for the
    // stack's specification and why this is computed rather than written out.
    // Stable regardless of the Timing HUD's height (it's below and grows away).
    // Shared with the Timing HUD so the two centered top-stack panels are the same width.
    constexpr int NOTICE_WIDTH_CHARS = WidgetDimensions::CENTER_STACK_WIDTH_CHARS;

}

NoticesHud::NoticesHud()
    : m_bIsWrongWay(false)
    , m_bShowOvertime(false)
    , m_bOvertimeTriggered(false)
    , m_bShowLastLap(false)
    , m_bShowFinished(false)
    , m_finishedPosition(-1)
    , m_bLastLapTriggered(false)
    , m_bFinishedTriggered(false)
    , m_bShowSessionPB(false)
    , m_bShowFastestLap(false)
    , m_bShowAllTimePB(false)
    , m_bShowDefaultSetup(false)
    , m_bShowSegment(false)
{
    // TITLE RESTORED, TEMPORARILY. This panel was one of the three the caption was taken
    // from (see BaseHud::m_titleSupported for the twelve that keep it off). It is back so
    // the reason the caption was unwanted can be shown rather than described -- nothing
    // else about this HUD reverted with it: the panel, its body card, the coloured
    // block's outset and the stack spacing are all as the last few commits left them.
    // A PANEL AND A BODY CARD, like both siblings in the centre stack, and set here
    // like theirs rather than derived per rebuild. It used to be `m_bContentCard =
    // m_bShowTitle`: untitled, this widget drew NO panel at all -- the coloured slab
    // was the whole thing -- and switching the caption on was what promoted it to a
    // real panel. With the caption gone that left the slab as the only reachable
    // shape, a bare button-coloured blob beside a framed Gap Bar. Reported as "what
    // has happened to the content band of the notices, shouldn't it look like the gap
    // bar".
    //
    // It costs NO height, which is what made the old gating look necessary and is not:
    // the slab already paid a padding at each end when it was the widget, and the panel
    // pays exactly that now, so panelH is the number CenterStack::noticesBoxHeight()
    // already states. The slab moves inside the card instead of being the box.
    m_bContentCard = true;
    // One-time setup
    DEBUG_INFO("NoticesHud created");
    setDraggable(true);
    m_quads.reserve(1);
    m_strings.reserve(1);

    // Set texture base name for dynamic texture discovery
    setTextureBaseName("notices_hud");

    // Set all configurable defaults
    resetToDefaults();

    rebuildRenderData();
}

bool NoticesHud::handlesDataType(DataChangeType /*dataType*/) const {
    return false;  // We poll PluginData directly in update()
}

bool NoticesHud::isTimedNoticeActive(std::chrono::steady_clock::time_point triggerTime) const {
    auto elapsed = std::chrono::steady_clock::now() - triggerTime;
    return std::chrono::duration_cast<std::chrono::milliseconds>(elapsed).count() < static_cast<long long>(m_noticeDurationMs);
}

NoticesHud::StatusTier NoticesHud::computeStatusTier() const {
    // Reads the member flags update() has already refreshed (wrong-way grace, hazard
    // poll, blue-flag, lapping, overtime timer). The render ladder gives blue flag
    // precedence over lapper, so lapper is suppressed when blue flag shows.
    StatusTier s;
    s.wrongWay = m_bIsWrongWay && (m_enabledNotices & NOTICE_WRONG_WAY);
    s.hazard   = m_bIsHazardAhead && !m_bFinishedTriggered;
    s.blueFlag = m_bIsBlueFlagged && !m_bFinishedTriggered && (m_enabledNotices & NOTICE_BLUE_FLAG);
    s.lapping  = m_bIsLapping && !m_bFinishedTriggered && !s.blueFlag && (m_enabledNotices & NOTICE_LAPPING);
    s.overtime = m_bShowOvertime && (m_enabledNotices & NOTICE_OVERTIME);
    return s;
}

void NoticesHud::update() {
    // When invisible, still clean up expired timed notice flags in PluginData.
    // Without this, flags linger until PluginData::clear() (session end), and toggling
    // the widget visible could briefly flash a stale notice.
    // When visible, checkTimedNotice() handles cleanup — no need to run both paths.
    if (!isVisibleAnySurface()) {
        PluginData& pd = PluginData::getInstance();
        if (pd.hasNewAllTimePB() && !isTimedNoticeActive(pd.getAllTimePBTime()))
            pd.clearAllTimePB();
        if (pd.hasNewFastestLap() && !isTimedNoticeActive(pd.getFastestLapTime()))
            pd.clearFastestLap();
        if (pd.hasNewSessionPB() && !isTimedNoticeActive(pd.getSessionPBTime()))
            pd.clearSessionPB();
        if (pd.hasDefaultSetupNotice() && !isTimedNoticeActive(pd.getDefaultSetupTime()))
            pd.clearDefaultSetupNotice();
        if (pd.hasSegmentNotice() && !isTimedNoticeActive(pd.getSegmentNoticeTime()))
            pd.clearSegmentNotice();
        clearDataDirty();
        clearLayoutDirty();
        return;
    }

    PluginData& pluginData = PluginData::getInstance();
    const SessionData& sessionData = pluginData.getSessionData();

    // Wrong-way notice, suppressed during a standing (grid) start until the rider clears the first
    // split. The grid launch (facing sideways/backward on the grid, then the run to S/F) routinely
    // trips wrong-way; the grid-start grace is sector-based (see PluginData::isInGridStartGrace),
    // so it covers races AND grid qualifying and adapts to the variable gate hold, with no fixed
    // duration or sessionTime math. Pit starts never enter this grace, so their behaviour is
    // unchanged (wrong-way shows as before).
    bool wrongWay = pluginData.isPlayerGoingWrongWay() && !pluginData.isInGridStartGrace();

    if (wrongWay != m_bIsWrongWay) {
        m_bIsWrongWay = wrongWay;
        setDataDirty();
    }

    // Check hazard ahead status (poll cached query, filter by enabled notice types)
    {
        bool hazardAhead = false;
        bool hazardIsWrongWay = false;
        bool stationaryEnabled = (m_enabledNotices & NOTICE_HAZARD_STATIONARY) != 0;
        bool wrongWayEnabled = (m_enabledNotices & NOTICE_HAZARD_WRONG_WAY) != 0;
        if (stationaryEnabled || wrongWayEnabled) {
            const auto& hazardRaceNums = pluginData.getHazardRaceNums();
            for (int raceNum : hazardRaceNums) {
                HazardType type = pluginData.getRiderHazardType(raceNum);
                if (wrongWayEnabled && type == HazardType::WrongWay) {
                    // Don't break: an oncoming rider outranks a stopped one for WORDING,
                    // so once one is found it wins regardless of scan order.
                    hazardAhead = true;
                    hazardIsWrongWay = true;
                    break;
                }
                if (stationaryEnabled && type == HazardType::Stationary) {
                    hazardAhead = true;   // keep scanning in case a wrong-way rider follows
                }
            }
        }
        if (hazardAhead != m_bIsHazardAhead || hazardIsWrongWay != m_bHazardIsWrongWay) {
            m_bIsHazardAhead = hazardAhead;
            m_bHazardIsWrongWay = hazardIsWrongWay;
            setDataDirty();
        }
    }

    // Check blue flag status
    bool isBlueFlagged = pluginData.isPlayerBlueFlagged();
    if (isBlueFlagged != m_bIsBlueFlagged) {
        m_bIsBlueFlagged = isBlueFlagged;
        setDataDirty();
    }

    // Check lapping status (mirror of blue flag — player closing on a backmarker ahead)
    bool isLapping = pluginData.isPlayerLapping();
    if (isLapping != m_bIsLapping) {
        m_bIsLapping = isLapping;
        setDataDirty();
    }

    // Check overtime status - trigger timed notice when time+laps race enters overtime
    {
        bool overtime = sessionData.overtimeStarted;
        if (overtime && !m_bOvertimeTriggered) {
            m_overtimeTriggerTime = std::chrono::steady_clock::now();
            m_bShowOvertime = true;
            m_bOvertimeTriggered = true;
            setDataDirty();
        }
        if (m_bShowOvertime && !isTimedNoticeActive(m_overtimeTriggerTime)) {
            m_bShowOvertime = false;
            setDataDirty();
        }
        // Reset triggered flag when overtime clears (new session)
        if (!overtime && m_bOvertimeTriggered) {
            m_bOvertimeTriggered = false;
        }
    }

    // Check last lap / finished status - trigger timed notices on transitions
    bool isLastLap = false;
    bool isFinished = false;
    int displayRaceNum = pluginData.getDisplayRaceNum();

    // Reset triggered and display flags when spectated rider changes
    if (displayRaceNum != m_lastDisplayRaceNum) {
        m_bLastLapTriggered = false;
        m_bFinishedTriggered = false;
        m_bShowLastLap = false;
        m_bShowFinished = false;
        m_lastDisplayRaceNum = displayRaceNum;
        setDataDirty();
    }
    const StandingsData* standing = (displayRaceNum > 0) ? pluginData.getStanding(displayRaceNum) : nullptr;
    if (standing && standing->numLaps >= 0) {
        isFinished = sessionData.isRiderFinished(standing->numLaps, standing->numLapsAtLeaderFinish);
        if (!isFinished && pluginData.isRaceSession()) {
            isLastLap = sessionData.isRiderOnLastLap(standing->numLaps, standing->numLapsAtLeaderFinish);
        }
    }

    // Trigger last lap timed notice on transition to last lap (once per last-lap period)
    if (isLastLap && !m_bLastLapTriggered) {
        m_lastLapTriggerTime = std::chrono::steady_clock::now();
        m_bShowLastLap = true;
        m_bLastLapTriggered = true;
        setDataDirty();
    }

    // Trigger finished timed notice on transition to finished (once per race)
    if (isFinished && !m_bFinishedTriggered) {
        m_finishedTriggerTime = std::chrono::steady_clock::now();
        m_bShowFinished = true;
        m_bFinishedTriggered = true;
        m_finishedPosition = pluginData.getDisplayPositionForRaceNum(displayRaceNum);
        setDataDirty();
    }

    // While finished notice is showing, track position changes (e.g., penalty applied)
    if (m_bShowFinished && displayRaceNum > 0) {
        int currentPos = pluginData.getDisplayPositionForRaceNum(displayRaceNum);
        if (currentPos != m_finishedPosition) {
            m_finishedPosition = currentPos;
            setDataDirty();
        }
    }

    // Expire last lap / finished timed notices
    if (m_bShowLastLap && !isTimedNoticeActive(m_lastLapTriggerTime)) {
        m_bShowLastLap = false;
        setDataDirty();
    }
    if (m_bShowFinished && !isTimedNoticeActive(m_finishedTriggerTime)) {
        m_bShowFinished = false;
        setDataDirty();
    }

    // Reset triggered flags when conditions clear (e.g. new race starts)
    // so the notices can fire again in subsequent races
    if (!isLastLap && m_bLastLapTriggered) {
        m_bLastLapTriggered = false;
    }
    if (!isFinished && m_bFinishedTriggered) {
        m_bFinishedTriggered = false;
    }

    // Higher-priority *status* notices that legitimately co-occur with a PB/setup/
    // segment on the same lap. While one of these is on screen, a consumable notice
    // masked behind it must not run down its display timer and get cleared unseen.
    // computeStatusTier() is the shared source with rebuildRenderData()'s render ladder,
    // so the mask predicate and the display precedence can't drift. (Overtime is timed
    // but still outranks the PB tier, so it masks too.)
    const bool statusMasking = computeStatusTier().anyShowing();

    auto nowMs = [] {
        return std::chrono::duration_cast<std::chrono::milliseconds>(
                   std::chrono::steady_clock::now().time_since_epoch()).count();
    }();
    auto toMs = [](std::chrono::steady_clock::time_point tp) {
        return std::chrono::duration_cast<std::chrono::milliseconds>(tp.time_since_epoch()).count();
    };
    const long long durationMs = static_cast<long long>(m_noticeDurationMs);

    // Check timed notice flags - single check per type. The window is measured from
    // when the notice became unmasked (see notice_priority.h), so a masked notice is
    // held rather than consumed. A disabled notice is never held (it can't be seen
    // anyway) so it drains normally.
    auto checkTimedNotice = [&](bool hasNew, bool enabled, std::chrono::steady_clock::time_point triggerTime,
                                long long& unmaskAnchor, auto clearFn, bool& showFlag) {
        NoticePriority::TimerOut r = NoticePriority::stepTimer(
            { hasNew, statusMasking && enabled, toMs(triggerTime), unmaskAnchor }, nowMs, durationMs);
        unmaskAnchor = r.unmaskAtMs;
        if (r.consume) clearFn();
        if (r.show != showFlag) { showFlag = r.show; setDataDirty(); }
    };

    checkTimedNotice(pluginData.hasNewAllTimePB(), (m_enabledNotices & NOTICE_ALLTIME_PB) != 0,
                     pluginData.getAllTimePBTime(), m_allTimePBUnmaskMs,
                     [&]() { pluginData.clearAllTimePB(); }, m_bShowAllTimePB);
    checkTimedNotice(pluginData.hasNewFastestLap(), (m_enabledNotices & NOTICE_FASTEST_LAP) != 0,
                     pluginData.getFastestLapTime(), m_fastestLapUnmaskMs,
                     [&]() { pluginData.clearFastestLap(); }, m_bShowFastestLap);
    checkTimedNotice(pluginData.hasNewSessionPB(), (m_enabledNotices & NOTICE_SESSION_PB) != 0,
                     pluginData.getSessionPBTime(), m_sessionPBUnmaskMs,
                     [&]() { pluginData.clearSessionPB(); }, m_bShowSessionPB);

    // Check default setup warning (only fires when RunHandler detects default setup)
    checkTimedNotice(pluginData.hasDefaultSetupNotice(), (m_enabledNotices & NOTICE_DEFAULT_SETUP) != 0,
                     pluginData.getDefaultSetupTime(), m_defaultSetupUnmaskMs,
                     [&]() { pluginData.clearDefaultSetupNotice(); }, m_bShowDefaultSetup);

    // Segment timer action notice (start set / end set / cleared) - carries a kind,
    // so it can't use the checkTimedNotice helper directly. Re-render on kind change
    // too (not just show/hide), so a second press replaces the notice immediately
    // instead of queueing behind the one still on screen. (Always-enabled, so it is
    // held while a status notice masks it, same as the PB tier.)
    {
        NoticePriority::TimerOut r = NoticePriority::stepTimer(
            { pluginData.hasSegmentNotice(), statusMasking, toMs(pluginData.getSegmentNoticeTime()), m_segmentUnmaskMs },
            nowMs, durationMs);
        m_segmentUnmaskMs = r.unmaskAtMs;
        if (r.consume) pluginData.clearSegmentNotice();
        bool active = r.show;
        PluginData::SegmentNoticeKind kind = active ? pluginData.getSegmentNoticeKind()
                                                    : PluginData::SegmentNoticeKind::None;
        int number = active ? pluginData.getSegmentNoticeNumber() : 0;
        if (active != m_bShowSegment || kind != m_segmentNoticeKind ||
            number != m_segmentNoticeNumber) {
            m_bShowSegment = active;
            m_segmentNoticeKind = kind;
            m_segmentNoticeNumber = number;
            setDataDirty();
        }
    }

    // Handle dirty flags using base class helper
    processDirtyFlags();
}

void NoticesHud::rebuildLayout() {
    // FULL REBUILD, like TimingHud and GapBarHud beside it.
    //
    // This used to be a hand-written fast path that rewrote m_quads[0] as the notice
    // slab. That index is only the slab while this widget draws nothing else -- with
    // a title it is the panel's first frame slice, so the fast path would drag the
    // frame and leave the slab behind. The path had already cost two desync bugs
    // (its own comments recorded both: a 1-quad vector written with 9 slices, and a
    // rewrite using the wrong slice set), and it duplicated every dimension in
    // rebuildRenderData to do it.
    //
    // Cheap enough to be the right trade: at most one notice is ever on screen, so a
    // rebuild is a background, a caption and one slab.
    rebuildRenderData();
}

// One notice background. A notice is a coloured slab whose COLOUR is the message
// (red = wrong way, blue = blue flag), which is exactly what the theme's button
// slices are for -- they are white+alpha and always take the caller's colour, so a
// baked-colour theme cannot multiply the meaning away. The plain quad is the
// fallback when the theme has no button set.
//
// Every branch below goes through here so the quad COUNT is uniform: the layout
// fast path rewrites this span by index, and a branch that pushed its own single
// quad would desync it.
// A notice IS a button shape: a small coloured rectangle whose colour is state.
// This reimplemented addButtonQuad exactly -- themed branch, offset, sprite, colour
// -- which is the duplication that helper was written to end.
void NoticesHud::addNoticeBackground(float x, float y, float w, float h, unsigned long color) {
    // THE THEME'S BUTTON SLICES, and the Gap Bar's fill goes through the same call.
    // Both are a coloured block whose colour is the reading, so a theme that shapes its
    // buttons shapes these too -- one decision for the pair rather than one panel
    // borrowing the art and its neighbour drawing a flat rectangle.
    //
    // The rect is the card's INTERIOR, which is what makes the borrowed shape sit
    // flush: a button bevel does not colour its own edge slices, so the visible colour
    // is inset from this rect by the theme's [button] size. That inset is the theme's
    // to choose -- a theme wanting the colour edge to edge ships flat button art.
    //
    // NOT opaque: a notice borrows a button's shape but is not a control. These ship at
    // 10% background opacity -- a whisper over the track -- and the button rule (a thing
    // you click stays legible) would make every one a solid box.
    addButtonQuad(x, y, w, h, color, /*opaque=*/false);
}

void NoticesHud::rebuildRenderData() {
    // Clear render data
    clearStrings();
    m_quads.clear();

    // Check which notices are both active and enabled
    // Status tier (wrong way / hazard / blue flag / lapper / overtime) — shared with
    // update()'s masking predicate via computeStatusTier() so the two stay in lockstep.
    const StatusTier status = computeStatusTier();
    bool showWrongWay  = status.wrongWay;
    bool showHazard    = status.hazard;
    bool showBlueFlag  = status.blueFlag;
    bool showLapping   = status.lapping;
    bool showOvertime  = status.overtime;
    bool showAllTimePB = m_bShowAllTimePB && (m_enabledNotices & NOTICE_ALLTIME_PB);
    bool showFastestLap = m_bShowFastestLap && (m_enabledNotices & NOTICE_FASTEST_LAP);
    bool showSessionPB = m_bShowSessionPB && (m_enabledNotices & NOTICE_SESSION_PB);
    bool showFinished  = m_bShowFinished && (m_enabledNotices & NOTICE_FINISHED);
    bool showLastLap   = m_bShowLastLap && (m_enabledNotices & NOTICE_LAST_LAP);
    bool showDefaultSetup = m_bShowDefaultSetup && (m_enabledNotices & NOTICE_DEFAULT_SETUP);
    bool showSegment   = m_bShowSegment;  // always on -- self-gated by the segment hotkey action

    // Only render if there's something to show
    // Priority: WRONG WAY > HAZARD AHEAD > BLUE FLAG > LAPPER AHEAD > OVERTIME > ALL-TIME PB > FASTEST LAP > SESSION PB > SEGMENT > FINISHED > LAST LAP > SETUP NAME
    if (!showWrongWay && !showHazard && !showBlueFlag && !showLapping && !showOvertime && !showAllTimePB && !showFastestLap &&
        !showSessionPB && !showSegment && !showLastLap && !showFinished && !showDefaultSetup) {
        setBounds(0.0f, 0.0f, 0.0f, 0.0f);
        return;
    }

    auto dim = getScaledDimensions();

    // BOX-MODEL: one section whose content is the coloured slab's row. The slab
    // IS the section card's border box when one is drawn — the "block meets the
    // card's outer edge on all four sides" rule, now by construction rather than
    // by outsetting the interior back over the border (the old
    // contentCardSpanY + drawnCardBorder* dance). Unthemed, the card box
    // degenerates to the content box and the slab is the row, as before.
    //
    // The centre-stack width rides as the panel MINIMUM (see CenterStack::boxWidth);
    // its own stack line is measured DOWN and snapped, as before.
    BaseHud::PanelWant want;
    wantCenterStackWidth(want, dim);   // stack width as the minimum, nothing competing
    want.sectionH = { bigValueRowHeight(dim) };
    want.captionW = planTitleWidth(dim, "Notices", TitleTier::Large);
    want.tier = TitleTier::Large;
    // The message slab IS this panel: unthemed, it takes the cell of panel padding
    // rather than floating inside it. Same outer rect either way; with a theme on,
    // the frame keeps its ring. See PanelWant::contentFillsPanel.
    want.contentFillsPanel = true;
    PanelPlan& plan = planPanel(dim, want);

    const float panelW = plan.width();
    // CENTRE-ANCHORED: offsetX is this panel's CENTRE, so a width change recentres
    // instead of walking an edge. It stored a DELTA from the computed centre until
    // settings v7; the migration adds the anchor in.
    const float panelX = centerAnchoredPanelLeft(panelW);
    // ZERO, like GapBar's boxTop and Timing's START_Y: the panel anchors at the
    // offset and the stored offset IS its top. This used to bake noticesBoxTop() in
    // and treat the offset as a delta from it, which meant the same offsetY in the
    // INI put this panel somewhere else than the two it stacks with -- invisible
    // while the three had different defaults, and wrong the moment they share one.
    const float panelY = snapEdgeY(0.0f);
    addPlanBackground(plan, panelX, panelY);
    addPlanTitle(plan, "Notices", this->getFont(FontCategory::TITLE),
                 this->getColor(ColorSlot::PRIMARY));

    // The SLAB (the card's drawn box; = the row unthemed), via the shared
    // accessors -- the message centres on it both ways now. Its width used to be
    // the card minus the LEFT content inset mirrored onto both sides, the same
    // idiom that pulled Fuel's value inward (see PanelPlan::contentRight).
    const float slabX = plan.sectionBoxX();
    const float slabW = plan.sectionBoxW();
    const float slabY = plan.sectionBoxY();
    const float slabH = plan.sectionBoxH();

    // INK-centred in THE SLAB, like the Timing panel's big time below it -- the message
    // is drawn ON the slab, so the slab is the box it has to look centred in. It used
    // to centre in the row, which is the same place until [content] border is
    // asymmetric and a cell high when it is (PanelPlan::sectionBoxY).
    float noticeY = inkCenteredY(slabY, slabH, dim.fontSizeLarge);
    float noticeCenterX = slabX + slabW / 2.0f;

    if (showWrongWay) {
        // Add notice background (red for warning)
        addNoticeBackground(slabX, slabY, slabW, slabH, PluginUtils::applyOpacity(this->getColor(ColorSlot::NEGATIVE), m_fBackgroundOpacity));

        // Add notice text (red)
        addString("WRONG WAY", noticeCenterX, noticeY, Justify::CENTER,
            this->getFont(FontCategory::TITLE),
            captionOnSlabColor(this->getColor(ColorSlot::NEGATIVE), m_fBackgroundOpacity),
            dim.fontSizeLarge);
    }
    else if (showHazard) {
        // Add notice background (yellow/warning for hazard)
        addNoticeBackground(slabX, slabY, slabW, slabH, PluginUtils::applyOpacity(this->getColor(ColorSlot::WARNING), m_fBackgroundOpacity));

        // Same slot, same WARNING colour — only the wording separates a rider coming at
        // you from one stopped on the track, which call for opposite reactions.
        addString(m_bHazardIsWrongWay ? "RIDER ONCOMING" : "HAZARD AHEAD",
            noticeCenterX, noticeY, Justify::CENTER,
            this->getFont(FontCategory::TITLE),
            captionOnSlabColor(this->getColor(ColorSlot::WARNING), m_fBackgroundOpacity),
            dim.fontSizeLarge);
    }
    else if (showBlueFlag) {
        // Add notice background (blue for blue flag)
        addNoticeBackground(slabX, slabY, slabW, slabH, PluginUtils::applyOpacity(ColorPalette::BLUE, m_fBackgroundOpacity));

        addString("BLUE FLAG", noticeCenterX, noticeY, Justify::CENTER,
            this->getFont(FontCategory::TITLE),
            captionOnSlabColor(ColorPalette::BLUE, m_fBackgroundOpacity), dim.fontSizeLarge);
    }
    else if (showLapping) {
        // Add notice background (neutral/yellow — informational caution; distinct from
        // the orange WARNING that hazards use)
        addNoticeBackground(slabX, slabY, slabW, slabH, PluginUtils::applyOpacity(this->getColor(ColorSlot::NEUTRAL), m_fBackgroundOpacity));

        addString("LAPPER AHEAD", noticeCenterX, noticeY, Justify::CENTER,
            this->getFont(FontCategory::TITLE),
            captionOnSlabColor(this->getColor(ColorSlot::NEUTRAL), m_fBackgroundOpacity),
            dim.fontSizeLarge);
    }
    else if (showOvertime) {
        // Add notice background (neutral for overtime - informational race event)
        addNoticeBackground(slabX, slabY, slabW, slabH, PluginUtils::applyOpacity(this->getColor(ColorSlot::BACKGROUND), m_fBackgroundOpacity));

        addString("OVERTIME", noticeCenterX, noticeY, Justify::CENTER,
            this->getFont(FontCategory::TITLE), this->getColor(ColorSlot::PRIMARY), dim.fontSizeLarge);
    }
    else if (showAllTimePB || showFastestLap || showSessionPB) {
        // All positive notices share the same rendering (green bg + green text)
        const char* text = showAllTimePB ? "ALL-TIME PB" :
                           showFastestLap ? "FASTEST LAP" : "SESSION PB";

        addNoticeBackground(slabX, slabY, slabW, slabH, PluginUtils::applyOpacity(this->getColor(ColorSlot::POSITIVE), m_fBackgroundOpacity));

        addString(text, noticeCenterX, noticeY, Justify::CENTER,
            this->getFont(FontCategory::TITLE),
            captionOnSlabColor(this->getColor(ColorSlot::POSITIVE), m_fBackgroundOpacity),
            dim.fontSizeLarge);
    }
    else if (showSegment) {
        // Segment-timer action feedback. Adding a point = positive (green); removing = neutral.
        bool isAdd = (m_segmentNoticeKind == PluginData::SegmentNoticeKind::Added);
        ColorSlot slot = isAdd ? ColorSlot::POSITIVE : ColorSlot::PRIMARY;

        // Name the point involved by its 1-based position ("SEG 3 ADDED"), so notices count
        // up as you place points and down as you remove them — the last removal is
        // "SEG 1 REMOVED" (no special "cleared" state). (The >= 1 guard is defensive —
        // add/remove always carry a positive ordinal.)
        char text[24] = "SEGMENT";
        switch (m_segmentNoticeKind) {
            case PluginData::SegmentNoticeKind::Added:
                if (m_segmentNoticeNumber >= 1)
                    snprintf(text, sizeof(text), "SEG %d ADDED", m_segmentNoticeNumber);
                else
                    strcpy_s(text, sizeof(text), "SEG ADDED");
                break;
            case PluginData::SegmentNoticeKind::Removed:
                if (m_segmentNoticeNumber >= 1)
                    snprintf(text, sizeof(text), "SEG %d REMOVED", m_segmentNoticeNumber);
                else
                    strcpy_s(text, sizeof(text), "SEG REMOVED");
                break;
            default: break;
        }

        addNoticeBackground(slabX, slabY, slabW, slabH, PluginUtils::applyOpacity(this->getColor(isAdd ? ColorSlot::POSITIVE : ColorSlot::BACKGROUND), m_fBackgroundOpacity));

        addString(text, noticeCenterX, noticeY, Justify::CENTER,
            this->getFont(FontCategory::TITLE), this->getColor(slot), dim.fontSizeLarge);
    }
    else if (showFinished) {
        // Add notice background (semantic background color for finished)
        addNoticeBackground(slabX, slabY, slabW, slabH, PluginUtils::applyOpacity(this->getColor(ColorSlot::BACKGROUND), m_fBackgroundOpacity));

        // Add notice text with position (e.g., "FINISHED 1ST")
        char finishedText[32];
        if (m_finishedPosition > 0) {
            snprintf(finishedText, sizeof(finishedText), "FINISHED %d%s", m_finishedPosition, ordinalSuffix(m_finishedPosition));
        } else {
            snprintf(finishedText, sizeof(finishedText), "FINISHED");
        }
        addString(finishedText, noticeCenterX, noticeY, Justify::CENTER,
            this->getFont(FontCategory::TITLE), this->getColor(ColorSlot::PRIMARY), dim.fontSizeLarge);
    }
    else if (showLastLap) {
        // Add notice background (white for last lap)
        addNoticeBackground(slabX, slabY, slabW, slabH, PluginUtils::applyOpacity(this->getColor(ColorSlot::BACKGROUND), m_fBackgroundOpacity));

        // Add notice text (white)
        addString("FINAL LAP", noticeCenterX, noticeY, Justify::CENTER,
            this->getFont(FontCategory::TITLE), this->getColor(ColorSlot::PRIMARY), dim.fontSizeLarge);
    }
    else if (showDefaultSetup) {
        // Warn when using default setup (only fires for default/empty setups)
        addNoticeBackground(slabX, slabY, slabW, slabH, PluginUtils::applyOpacity(this->getColor(ColorSlot::WARNING), m_fBackgroundOpacity));

        addString("DEFAULT SETUP", noticeCenterX, noticeY, Justify::CENTER,
            this->getFont(FontCategory::TITLE),
            captionOnSlabColor(this->getColor(ColorSlot::WARNING), m_fBackgroundOpacity),
            dim.fontSizeLarge);
    }

    setBounds(panelX, panelY, panelX + panelW, panelY + plan.height());
}

void NoticesHud::resetToDefaults() {
    m_bVisible = true;
    // Off by DEFAULT, not unavailable -- the toggle is in the Notices tab. Switching
    // it on promotes this widget from a bare coloured slab to a real panel with the
    // slab as its content; see rebuildRenderData for why the panel keeps the slab's
    // outer width instead of growing around it.
    m_bShowTitle = false;
    setTextureVariant(0);  // No texture by default
    m_fBackgroundOpacity = 0.1f;
    m_fScale = 1.0f;
    // The stack's shared anchor: offsetX is the CENTRE (all four centred elements
    // agree since settings v7), offsetY the shared top.
    setPosition(CENTER_ANCHOR_X, CenterStack::stackBoxTop());
    m_enabledNotices = NOTICE_DEFAULT;
    m_noticeDurationMs = DEFAULT_NOTICE_DURATION_MS;

    // Reset notice state
    m_bIsWrongWay = false;
    m_bIsHazardAhead = false;
    m_bHazardIsWrongWay = false;
    m_bIsBlueFlagged = false;
    m_bShowOvertime = false;
    m_bOvertimeTriggered = false;
    m_bShowLastLap = false;
    m_bShowFinished = false;
    m_finishedPosition = -1;
    m_bLastLapTriggered = false;
    m_bFinishedTriggered = false;
    m_bShowSessionPB = false;
    m_bShowFastestLap = false;
    m_bShowAllTimePB = false;
    m_bShowDefaultSetup = false;
    m_bShowSegment = false;
    m_segmentNoticeKind = PluginData::SegmentNoticeKind::None;
    m_overtimeTriggerTime = {};
    m_lastLapTriggerTime = {};
    m_finishedTriggerTime = {};
    m_allTimePBUnmaskMs = 0;
    m_fastestLapUnmaskMs = 0;
    m_sessionPBUnmaskMs = 0;
    m_defaultSetupUnmaskMs = 0;
    m_segmentUnmaskMs = 0;
    m_lastDisplayRaceNum = -1;

    setDataDirty();
}
