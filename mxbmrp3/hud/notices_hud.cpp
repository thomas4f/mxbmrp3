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
    constexpr float TIMING_DIVIDER_Y = 0.1665f;
    constexpr int NOTICE_WIDTH_CHARS = 14;  // Wider than STANDARD_WIDTH to fit "DEFAULT SETUP"
}

NoticesHud::NoticesHud()
    : m_bIsWrongWay(false)
    , m_sessionStartTime(0)
    , m_lastSessionState(-1)
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

void NoticesHud::update() {
    // When invisible, still clean up expired timed notice flags in PluginData.
    // Without this, flags linger until PluginData::clear() (session end), and toggling
    // the widget visible could briefly flash a stale notice.
    // When visible, checkTimedNotice() handles cleanup — no need to run both paths.
    if (!isVisible()) {
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

    // Track session state transitions to detect race start
    const SessionData& sessionData = pluginData.getSessionData();
    int currentSessionState = sessionData.sessionState;
    int currentSessionTime = pluginData.getSessionTime();
    bool isRaceSession = pluginData.isRaceSession();

    // Detect transition to "in progress" state for race sessions to start grace period
    if (isRaceSession && currentSessionState == SessionState::IN_PROGRESS && m_lastSessionState != SessionState::IN_PROGRESS) {
        // Race session just transitioned to "in progress" - store start time
        m_sessionStartTime = currentSessionTime;
        DEBUG_INFO_F("NoticesHud: Race started (in progress), sessionTime=%d ms", currentSessionTime);
    }
    m_lastSessionState = currentSessionState;

    // Check wrong-way status with grace period (only for race sessions)
    bool wrongWay = false;
    if (pluginData.isPlayerGoingWrongWay()) {
        // Player is going wrong way - check if we're within grace period (race sessions only)
        bool inGracePeriod = false;
        if (isRaceSession && currentSessionState == SessionState::IN_PROGRESS) {  // Only apply grace period for race sessions when "in progress"
            int elapsedTime = std::abs(currentSessionTime - m_sessionStartTime);
            inGracePeriod = (elapsedTime < WRONG_WAY_GRACE_PERIOD_MS);
        }

        // Only set wrong way if not in grace period
        wrongWay = !inGracePeriod;
    }

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

    // Check timed notice flags - single check per type, clear expired flags
    auto checkTimedNotice = [&](bool hasNew, std::chrono::steady_clock::time_point time,
                                auto clearFn, bool& showFlag) {
        bool active = hasNew && isTimedNoticeActive(time);
        if (hasNew && !active) clearFn();
        if (active != showFlag) { showFlag = active; setDataDirty(); }
    };

    checkTimedNotice(pluginData.hasNewAllTimePB(), pluginData.getAllTimePBTime(),
                     [&]() { pluginData.clearAllTimePB(); }, m_bShowAllTimePB);
    checkTimedNotice(pluginData.hasNewFastestLap(), pluginData.getFastestLapTime(),
                     [&]() { pluginData.clearFastestLap(); }, m_bShowFastestLap);
    checkTimedNotice(pluginData.hasNewSessionPB(), pluginData.getSessionPBTime(),
                     [&]() { pluginData.clearSessionPB(); }, m_bShowSessionPB);

    // Check default setup warning (only fires when RunHandler detects default setup)
    checkTimedNotice(pluginData.hasDefaultSetupNotice(), pluginData.getDefaultSetupTime(),
                     [&]() { pluginData.clearDefaultSetupNotice(); }, m_bShowDefaultSetup);

    // Segment timer action notice (start set / end set / cleared) - carries a kind,
    // so it can't use the checkTimedNotice helper directly. Re-render on kind change
    // too (not just show/hide), so a second press replaces the notice immediately
    // instead of queueing behind the one still on screen.
    {
        bool active = pluginData.hasSegmentNotice() && isTimedNoticeActive(pluginData.getSegmentNoticeTime());
        if (pluginData.hasSegmentNotice() && !active) pluginData.clearSegmentNotice();
        PluginData::SegmentNoticeKind kind = active ? pluginData.getSegmentNoticeKind()
                                                    : PluginData::SegmentNoticeKind::None;
        if (active != m_bShowSegment || kind != m_segmentNoticeKind) {
            m_bShowSegment = active;
            m_segmentNoticeKind = kind;
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
    float noticeQuadHeight = dim.paddingV + dim.fontSizeLarge;

    // Position notice with bottom edge at divider line (grows up)
    // Use original gap formula (half line height) for proper spacing
    float rowGap = dim.lineHeightNormal / 2.0f;
    float noticeQuadX = CENTER_X - noticeQuadWidth / 2.0f;
    float noticeQuadY = TIMING_DIVIDER_Y - rowGap - noticeQuadHeight;
    float noticeY = noticeQuadY + dim.paddingV * 0.5f;

    // Update notice quad position (apply drag offset)
    float quadX = noticeQuadX;
    float quadY = noticeQuadY;
    applyOffset(quadX, quadY);
    setQuadPositions(m_quads[0], quadX, quadY, noticeQuadWidth, noticeQuadHeight);

    // Update notice string position
    if (!m_strings.empty()) {
        float noticeX = CENTER_X;
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
    bool showWrongWay  = m_bIsWrongWay && (m_enabledNotices & NOTICE_WRONG_WAY);
    bool showHazard    = m_bIsHazardAhead && !m_bFinishedTriggered;
    bool showBlueFlag  = m_bIsBlueFlagged && !m_bFinishedTriggered && (m_enabledNotices & NOTICE_BLUE_FLAG);
    bool showOvertime  = m_bShowOvertime && (m_enabledNotices & NOTICE_OVERTIME);
    bool showAllTimePB = m_bShowAllTimePB && (m_enabledNotices & NOTICE_ALLTIME_PB);
    bool showFastestLap = m_bShowFastestLap && (m_enabledNotices & NOTICE_FASTEST_LAP);
    bool showSessionPB = m_bShowSessionPB && (m_enabledNotices & NOTICE_SESSION_PB);
    bool showFinished  = m_bShowFinished && (m_enabledNotices & NOTICE_FINISHED);
    bool showLastLap   = m_bShowLastLap && (m_enabledNotices & NOTICE_LAST_LAP);
    bool showDefaultSetup = m_bShowDefaultSetup && (m_enabledNotices & NOTICE_DEFAULT_SETUP);
    bool showSegment   = m_bShowSegment && (m_enabledNotices & NOTICE_SEGMENT);

    // Only render if there's something to show
    // Priority: WRONG WAY > HAZARD AHEAD > BLUE FLAG > OVERTIME > ALL-TIME PB > FASTEST LAP > SESSION PB > SEGMENT > FINISHED > LAST LAP > SETUP NAME
    if (!showWrongWay && !showHazard && !showBlueFlag && !showOvertime && !showAllTimePB && !showFastestLap &&
        !showSessionPB && !showSegment && !showLastLap && !showFinished && !showDefaultSetup) {
        setBounds(0.0f, 0.0f, 0.0f, 0.0f);
        return;
    }

    auto dim = getScaledDimensions();

    // Notice dimensions (uses own scale - independent of TimingHud)
    float noticeTextWidth = PluginUtils::calculateMonospaceTextWidth(NOTICE_WIDTH_CHARS, dim.fontSizeLarge);
    float noticeQuadWidth = dim.paddingH + noticeTextWidth + dim.paddingH;
    float noticeQuadHeight = dim.paddingV + dim.fontSizeLarge;

    // Position notice with bottom edge at divider line (grows up)
    // Use original gap formula (half line height) for proper spacing
    float rowGap = dim.lineHeightNormal / 2.0f;
    float noticeQuadX = CENTER_X - noticeQuadWidth / 2.0f;
    float noticeQuadY = TIMING_DIVIDER_Y - rowGap - noticeQuadHeight;
    float noticeY = noticeQuadY + dim.paddingV * 0.5f;

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
        addString("WRONG WAY", CENTER_X, noticeY, Justify::CENTER,
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

        addString("HAZARD AHEAD", CENTER_X, noticeY, Justify::CENTER,
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

        addString("BLUE FLAG", CENTER_X, noticeY, Justify::CENTER,
            this->getFont(FontCategory::TITLE), ColorPalette::BLUE, dim.fontSizeLarge);
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

        addString("OVERTIME", CENTER_X, noticeY, Justify::CENTER,
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

        addString(text, CENTER_X, noticeY, Justify::CENTER,
            this->getFont(FontCategory::TITLE), this->getColor(ColorSlot::POSITIVE), dim.fontSizeLarge);
    }
    else if (showSegment) {
        // Segment-timer action feedback. Adding a point = positive (green); removing
        // or clearing = neutral background.
        bool isAdd = (m_segmentNoticeKind == PluginData::SegmentNoticeKind::Added);
        ColorSlot slot = isAdd ? ColorSlot::POSITIVE : ColorSlot::PRIMARY;

        const char* text = "SEGMENT";
        switch (m_segmentNoticeKind) {
            case PluginData::SegmentNoticeKind::Added:   text = "SEG ADDED";   break;
            case PluginData::SegmentNoticeKind::Removed: text = "SEG REMOVED"; break;
            case PluginData::SegmentNoticeKind::Cleared: text = "SEG CLEARED"; break;
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

        addString(text, CENTER_X, noticeY, Justify::CENTER,
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
        addString(finishedText, CENTER_X, noticeY, Justify::CENTER,
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
        addString("FINAL LAP", CENTER_X, noticeY, Justify::CENTER,
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

        addString("DEFAULT SETUP", CENTER_X, noticeY, Justify::CENTER,
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
    m_lastDisplayRaceNum = -1;
    m_sessionStartTime = 0;
    m_lastSessionState = -1;

    setDataDirty();
}
