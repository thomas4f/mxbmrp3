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
#include "../core/ui_config.h"
#include "notice_priority.h"

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
    constexpr float CENTER_X = 0.5f;
    // Center-top stack (one grid snap between each), top to bottom: GapBar (first row) ->
    // Notices -> Timing HUD (which grows DOWN, so it sits last and never overlaps anything
    // below). The notice grows UP with its BOTTOM one row-gap below this divider. All three
    // boxes are lineHeightLarge tall (4 cells), so the whole stack lands on the grid.
    // Derivation (cell = 0.0117335, box height = 0.046934):
    //   GapBar top 0.011734 + GapBar height 0.046934 + 1 cell -> notice top   = 0.070402
    //   notice top + noticeQuadHeight (0.046934) + rowGap (1 cell) -> divider  = 0.129069
    //   notice bottom (0.117336) + 1 cell -> Timing top (timing_hud.cpp)       = 0.129069
    // Stable regardless of the Timing HUD's height (it's below and grows away).
    constexpr float TIMING_DIVIDER_Y = 0.129069f;
    // Shared with the Timing HUD so the two centered top-stack panels are the same width.
    constexpr int NOTICE_WIDTH_CHARS = WidgetDimensions::CENTER_STACK_WIDTH_CHARS;

    // Match TimingHud's centering exactly: when grid snapping is on, quantize the centering
    // anchor to the horizontal grid so the notice's left edge lands on the same lattice as the
    // Timing panel below it. Both panels are the same width, so same anchor + same snap => same
    // left edge. Without this, two equal-width panels drift up to half a grid cell apart and
    // read as misaligned even though their widths match.
    inline float snapCenteringX(float x) {
        return UiConfig::getInstance().getGridSnapping()
            ? PluginConstants::HudGrid::SNAP_TO_GRID_X(x)
            : x;
    }
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
        bool stationaryEnabled = (m_enabledNotices & NOTICE_HAZARD_STATIONARY) != 0;
        bool wrongWayEnabled = (m_enabledNotices & NOTICE_HAZARD_WRONG_WAY) != 0;
        if (stationaryEnabled || wrongWayEnabled) {
            const auto& hazardRaceNums = pluginData.getHazardRaceNums();
            for (int raceNum : hazardRaceNums) {
                HazardType type = pluginData.getRiderHazardType(raceNum);
                if ((stationaryEnabled && type == HazardType::Stationary) ||
                    (wrongWayEnabled && type == HazardType::WrongWay)) {
                    hazardAhead = true;
                    break;
                }
            }
        }
        if (hazardAhead != m_bIsHazardAhead) {
            m_bIsHazardAhead = hazardAhead;
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
    // Fast path - only update positions (not colors/opacity)
    if (m_quads.empty()) {
        setBounds(0.0f, 0.0f, 0.0f, 0.0f);
        return;
    }

    auto dim = getScaledDimensions();

    // Notice dimensions (uses own scale - independent of TimingHud)
    float noticeTextWidth = PluginUtils::calculateMonospaceTextWidth(NOTICE_WIDTH_CHARS, dim.fontSizeLarge);
    float noticeQuadWidth = dim.paddingH + noticeTextWidth + dim.paddingH;
    // Height = lineHeightLarge (the large-font title band, exactly 2x lineHeightNormal =
    // 4 snap-grid cells) so this box lines up on the shared grid with the Timing/Gap Bar
    // rows. (The old paddingV + fontSizeLarge was ~4.56 cells - off-grid.)
    float noticeQuadHeight = dim.lineHeightLarge;

    // Position notice with bottom edge at divider line (grows up)
    // Use original gap formula (half line height) for proper spacing
    float rowGap = dim.lineHeightNormal / 2.0f;
    float noticeQuadX = snapCenteringX(CENTER_X - noticeQuadWidth / 2.0f);
    float noticeQuadY = TIMING_DIVIDER_Y - rowGap - noticeQuadHeight;
    float noticeY = noticeQuadY + (noticeQuadHeight - dim.fontSizeLarge) * 0.5f;

    // Update notice quad position (apply drag offset)
    float quadX = noticeQuadX;
    float quadY = noticeQuadY;
    applyOffset(quadX, quadY);
    setQuadPositions(m_quads[0], quadX, quadY, noticeQuadWidth, noticeQuadHeight);

    // Update notice string position
    if (!m_strings.empty()) {
        float noticeX = noticeQuadX + noticeQuadWidth / 2.0f;
        applyOffset(noticeX, noticeY);
        m_strings[0].m_afPos[0] = noticeX;
        m_strings[0].m_afPos[1] = noticeY;
    }

    setBounds(noticeQuadX, noticeQuadY, noticeQuadX + noticeQuadWidth, noticeQuadY + noticeQuadHeight);
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

    // Notice dimensions (uses own scale - independent of TimingHud)
    float noticeTextWidth = PluginUtils::calculateMonospaceTextWidth(NOTICE_WIDTH_CHARS, dim.fontSizeLarge);
    float noticeQuadWidth = dim.paddingH + noticeTextWidth + dim.paddingH;
    // Height = lineHeightLarge (the large-font title band, exactly 2x lineHeightNormal =
    // 4 snap-grid cells) so this box lines up on the shared grid with the Timing/Gap Bar
    // rows. (The old paddingV + fontSizeLarge was ~4.56 cells - off-grid.)
    float noticeQuadHeight = dim.lineHeightLarge;

    // Position notice with bottom edge at divider line (grows up)
    // Use original gap formula (half line height) for proper spacing
    float rowGap = dim.lineHeightNormal / 2.0f;
    float noticeQuadX = snapCenteringX(CENTER_X - noticeQuadWidth / 2.0f);
    float noticeQuadY = TIMING_DIVIDER_Y - rowGap - noticeQuadHeight;
    float noticeY = noticeQuadY + (noticeQuadHeight - dim.fontSizeLarge) * 0.5f;
    // Center text on the (snapped) box center, not raw CENTER_X, so the label stays centered
    // inside the box after the box left edge is grid-snapped (matches TimingHud).
    float noticeCenterX = noticeQuadX + noticeQuadWidth / 2.0f;

    if (showWrongWay) {
        // Add notice background (red for warning)
        SPluginQuad_t noticeQuad;
        float quadX = noticeQuadX;
        float quadY = noticeQuadY;
        applyOffset(quadX, quadY);
        setQuadPositions(noticeQuad, quadX, quadY, noticeQuadWidth, noticeQuadHeight);
        noticeQuad.m_iSprite = SpriteIndex::SOLID_COLOR;
        noticeQuad.m_ulColor = PluginUtils::applyOpacity(this->getColor(ColorSlot::NEGATIVE), m_fBackgroundOpacity);
        m_quads.push_back(noticeQuad);

        // Add notice text (red)
        addString("WRONG WAY", noticeCenterX, noticeY, Justify::CENTER,
            this->getFont(FontCategory::TITLE), this->getColor(ColorSlot::NEGATIVE), dim.fontSizeLarge);
    }
    else if (showHazard) {
        // Add notice background (yellow/warning for hazard)
        SPluginQuad_t noticeQuad;
        float quadX = noticeQuadX;
        float quadY = noticeQuadY;
        applyOffset(quadX, quadY);
        setQuadPositions(noticeQuad, quadX, quadY, noticeQuadWidth, noticeQuadHeight);
        noticeQuad.m_iSprite = SpriteIndex::SOLID_COLOR;
        noticeQuad.m_ulColor = PluginUtils::applyOpacity(this->getColor(ColorSlot::WARNING), m_fBackgroundOpacity);
        m_quads.push_back(noticeQuad);

        addString("HAZARD AHEAD", noticeCenterX, noticeY, Justify::CENTER,
            this->getFont(FontCategory::TITLE), this->getColor(ColorSlot::WARNING), dim.fontSizeLarge);
    }
    else if (showBlueFlag) {
        // Add notice background (blue for blue flag)
        SPluginQuad_t noticeQuad;
        float quadX = noticeQuadX;
        float quadY = noticeQuadY;
        applyOffset(quadX, quadY);
        setQuadPositions(noticeQuad, quadX, quadY, noticeQuadWidth, noticeQuadHeight);
        noticeQuad.m_iSprite = SpriteIndex::SOLID_COLOR;
        noticeQuad.m_ulColor = PluginUtils::applyOpacity(ColorPalette::BLUE, m_fBackgroundOpacity);
        m_quads.push_back(noticeQuad);

        addString("BLUE FLAG", noticeCenterX, noticeY, Justify::CENTER,
            this->getFont(FontCategory::TITLE), ColorPalette::BLUE, dim.fontSizeLarge);
    }
    else if (showLapping) {
        // Add notice background (neutral/yellow — informational caution; distinct from
        // the orange WARNING that hazards use)
        SPluginQuad_t noticeQuad;
        float quadX = noticeQuadX;
        float quadY = noticeQuadY;
        applyOffset(quadX, quadY);
        setQuadPositions(noticeQuad, quadX, quadY, noticeQuadWidth, noticeQuadHeight);
        noticeQuad.m_iSprite = SpriteIndex::SOLID_COLOR;
        noticeQuad.m_ulColor = PluginUtils::applyOpacity(this->getColor(ColorSlot::NEUTRAL), m_fBackgroundOpacity);
        m_quads.push_back(noticeQuad);

        addString("LAPPER AHEAD", noticeCenterX, noticeY, Justify::CENTER,
            this->getFont(FontCategory::TITLE), this->getColor(ColorSlot::NEUTRAL), dim.fontSizeLarge);
    }
    else if (showOvertime) {
        // Add notice background (neutral for overtime - informational race event)
        SPluginQuad_t noticeQuad;
        float quadX = noticeQuadX;
        float quadY = noticeQuadY;
        applyOffset(quadX, quadY);
        setQuadPositions(noticeQuad, quadX, quadY, noticeQuadWidth, noticeQuadHeight);
        noticeQuad.m_iSprite = SpriteIndex::SOLID_COLOR;
        noticeQuad.m_ulColor = PluginUtils::applyOpacity(this->getColor(ColorSlot::BACKGROUND), m_fBackgroundOpacity);
        m_quads.push_back(noticeQuad);

        addString("OVERTIME", noticeCenterX, noticeY, Justify::CENTER,
            this->getFont(FontCategory::TITLE), this->getColor(ColorSlot::PRIMARY), dim.fontSizeLarge);
    }
    else if (showAllTimePB || showFastestLap || showSessionPB) {
        // All positive notices share the same rendering (green bg + green text)
        const char* text = showAllTimePB ? "ALL-TIME PB" :
                           showFastestLap ? "FASTEST LAP" : "SESSION PB";

        SPluginQuad_t noticeQuad;
        float quadX = noticeQuadX;
        float quadY = noticeQuadY;
        applyOffset(quadX, quadY);
        setQuadPositions(noticeQuad, quadX, quadY, noticeQuadWidth, noticeQuadHeight);
        noticeQuad.m_iSprite = SpriteIndex::SOLID_COLOR;
        noticeQuad.m_ulColor = PluginUtils::applyOpacity(this->getColor(ColorSlot::POSITIVE), m_fBackgroundOpacity);
        m_quads.push_back(noticeQuad);

        addString(text, noticeCenterX, noticeY, Justify::CENTER,
            this->getFont(FontCategory::TITLE), this->getColor(ColorSlot::POSITIVE), dim.fontSizeLarge);
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

        SPluginQuad_t noticeQuad;
        float quadX = noticeQuadX;
        float quadY = noticeQuadY;
        applyOffset(quadX, quadY);
        setQuadPositions(noticeQuad, quadX, quadY, noticeQuadWidth, noticeQuadHeight);
        noticeQuad.m_iSprite = SpriteIndex::SOLID_COLOR;
        noticeQuad.m_ulColor = PluginUtils::applyOpacity(this->getColor(isAdd ? ColorSlot::POSITIVE : ColorSlot::BACKGROUND), m_fBackgroundOpacity);
        m_quads.push_back(noticeQuad);

        addString(text, noticeCenterX, noticeY, Justify::CENTER,
            this->getFont(FontCategory::TITLE), this->getColor(slot), dim.fontSizeLarge);
    }
    else if (showFinished) {
        // Add notice background (semantic background color for finished)
        SPluginQuad_t noticeQuad;
        float quadX = noticeQuadX;
        float quadY = noticeQuadY;
        applyOffset(quadX, quadY);
        setQuadPositions(noticeQuad, quadX, quadY, noticeQuadWidth, noticeQuadHeight);
        noticeQuad.m_iSprite = SpriteIndex::SOLID_COLOR;
        noticeQuad.m_ulColor = PluginUtils::applyOpacity(this->getColor(ColorSlot::BACKGROUND), m_fBackgroundOpacity);
        m_quads.push_back(noticeQuad);

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
        SPluginQuad_t noticeQuad;
        float quadX = noticeQuadX;
        float quadY = noticeQuadY;
        applyOffset(quadX, quadY);
        setQuadPositions(noticeQuad, quadX, quadY, noticeQuadWidth, noticeQuadHeight);
        noticeQuad.m_iSprite = SpriteIndex::SOLID_COLOR;
        noticeQuad.m_ulColor = PluginUtils::applyOpacity(this->getColor(ColorSlot::BACKGROUND), m_fBackgroundOpacity);
        m_quads.push_back(noticeQuad);

        // Add notice text (white)
        addString("FINAL LAP", noticeCenterX, noticeY, Justify::CENTER,
            this->getFont(FontCategory::TITLE), this->getColor(ColorSlot::PRIMARY), dim.fontSizeLarge);
    }
    else if (showDefaultSetup) {
        // Warn when using default setup (only fires for default/empty setups)
        SPluginQuad_t noticeQuad;
        float quadX = noticeQuadX;
        float quadY = noticeQuadY;
        applyOffset(quadX, quadY);
        setQuadPositions(noticeQuad, quadX, quadY, noticeQuadWidth, noticeQuadHeight);
        noticeQuad.m_iSprite = SpriteIndex::SOLID_COLOR;
        noticeQuad.m_ulColor = PluginUtils::applyOpacity(this->getColor(ColorSlot::WARNING), m_fBackgroundOpacity);
        m_quads.push_back(noticeQuad);

        addString("DEFAULT SETUP", noticeCenterX, noticeY, Justify::CENTER,
            this->getFont(FontCategory::TITLE), this->getColor(ColorSlot::WARNING), dim.fontSizeLarge);
    }

    setBounds(noticeQuadX, noticeQuadY, noticeQuadX + noticeQuadWidth, noticeQuadY + noticeQuadHeight);
}

void NoticesHud::resetToDefaults() {
    m_bVisible = true;
    m_bShowTitle = false;
    setTextureVariant(0);  // No texture by default
    m_fBackgroundOpacity = 0.1f;
    m_fScale = 1.0f;
    setPosition(0.0f, 0.0f);
    m_enabledNotices = NOTICE_DEFAULT;
    m_noticeDurationMs = DEFAULT_NOTICE_DURATION_MS;

    // Reset notice state
    m_bIsWrongWay = false;
    m_bIsHazardAhead = false;
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
