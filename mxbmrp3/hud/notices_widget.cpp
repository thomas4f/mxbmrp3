// ============================================================================
// hud/notices_widget.cpp
// Notices widget - displays warnings and PB notifications
// Shows centered notices above the timing HUD area
// ============================================================================
#include "notices_widget.h"

#include <cstdio>
#include <cmath>
#include <string>

#include "../diagnostics/logger.h"
#include "../core/plugin_utils.h"
#include "../core/widget_constants.h"
#include "../core/color_config.h"

using namespace PluginConstants;

// Center display positioning constants (fixed center-screen layout)
namespace {
    constexpr float CENTER_X = 0.5f;
    constexpr float TIMING_DIVIDER_Y = 0.1665f;
}

NoticesWidget::NoticesWidget()
    : m_bIsWrongWay(false)
    , m_sessionStartTime(0)
    , m_lastSessionState(-1)
    , m_bShowLastLap(false)
    , m_bShowFinished(false)
    , m_bLastLapTriggered(false)
    , m_bFinishedTriggered(false)
    , m_bShowSessionPB(false)
    , m_bShowFastestLap(false)
    , m_bShowAllTimePB(false)
{
    // One-time setup
    DEBUG_INFO("NoticesWidget created");
    setDraggable(true);
    m_quads.reserve(1);
    m_strings.reserve(1);

    // Set texture base name for dynamic texture discovery
    setTextureBaseName("notices_widget");

    // Set all configurable defaults
    resetToDefaults();

    rebuildRenderData();
}

bool NoticesWidget::handlesDataType(DataChangeType /*dataType*/) const {
    return false;  // We poll PluginData directly in update()
}

bool NoticesWidget::isTimedNoticeActive(std::chrono::steady_clock::time_point triggerTime) const {
    auto elapsed = std::chrono::steady_clock::now() - triggerTime;
    return std::chrono::duration_cast<std::chrono::milliseconds>(elapsed).count() < static_cast<long long>(m_noticeDurationMs);
}

void NoticesWidget::update() {
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

    // Detect transition to "in progress" state (16) for race sessions to start grace period
    if (isRaceSession && currentSessionState == 16 && m_lastSessionState != 16) {
        // Race session just transitioned to "in progress" - store start time
        m_sessionStartTime = currentSessionTime;
        DEBUG_INFO_F("NoticesWidget: Race started (in progress), sessionTime=%d ms", currentSessionTime);
    }
    m_lastSessionState = currentSessionState;

    // Check wrong-way status with grace period (only for race sessions)
    bool wrongWay = false;
    if (pluginData.isPlayerGoingWrongWay()) {
        // Player is going wrong way - check if we're within grace period (race sessions only)
        bool inGracePeriod = false;
        if (isRaceSession && currentSessionState == 16) {  // Only apply grace period for race sessions when "in progress"
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

    // Check blue flag status (returns cached const ref — no allocation)
    const auto& blueFlagRaceNums = pluginData.getBlueFlagRaceNums();
    if (blueFlagRaceNums != m_blueFlagRaceNums) {
        m_blueFlagRaceNums = blueFlagRaceNums;
        setDataDirty();
    }

    // Check last lap / finished status - trigger timed notices on transitions
    bool isLastLap = false;
    bool isFinished = false;
    int displayRaceNum = pluginData.getDisplayRaceNum();
    const StandingsData* standing = (displayRaceNum > 0) ? pluginData.getStanding(displayRaceNum) : nullptr;
    if (standing && standing->numLaps >= 0) {
        isFinished = sessionData.isRiderFinished(standing->numLaps);
        if (!isFinished) {
            isLastLap = sessionData.isRiderOnLastLap(standing->numLaps);
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
        setDataDirty();
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

    // Handle dirty flags using base class helper
    processDirtyFlags();
}

void NoticesWidget::rebuildLayout() {
    // Fast path - only update positions (not colors/opacity)
    if (m_quads.empty()) {
        setBounds(0.0f, 0.0f, 0.0f, 0.0f);
        return;
    }

    auto dim = getScaledDimensions();

    // Notice dimensions (uses own scale - independent of TimingHud)
    float noticeTextWidth = PluginUtils::calculateMonospaceTextWidth(WidgetDimensions::STANDARD_WIDTH, dim.fontSizeLarge);
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

void NoticesWidget::rebuildRenderData() {
    // Clear render data
    clearStrings();
    m_quads.clear();

    // Check which notices are both active and enabled
    bool showWrongWay  = m_bIsWrongWay && (m_enabledNotices & NOTICE_WRONG_WAY);
    bool showBlueFlag  = !m_blueFlagRaceNums.empty() && (m_enabledNotices & NOTICE_BLUE_FLAG);
    bool showAllTimePB = m_bShowAllTimePB && (m_enabledNotices & NOTICE_ALLTIME_PB);
    bool showFastestLap = m_bShowFastestLap && (m_enabledNotices & NOTICE_FASTEST_LAP);
    bool showSessionPB = m_bShowSessionPB && (m_enabledNotices & NOTICE_SESSION_PB);
    bool showFinished  = m_bShowFinished && (m_enabledNotices & NOTICE_FINISHED);
    bool showLastLap   = m_bShowLastLap && (m_enabledNotices & NOTICE_LAST_LAP);

    // Only render if there's something to show
    // Priority: WRONG WAY > BLUE FLAG > ALL-TIME PB > FASTEST LAP > SESSION PB > FINISHED > LAST LAP
    if (!showWrongWay && !showBlueFlag && !showAllTimePB && !showFastestLap &&
        !showSessionPB && !showLastLap && !showFinished) {
        setBounds(0.0f, 0.0f, 0.0f, 0.0f);
        return;
    }

    auto dim = getScaledDimensions();

    // Notice dimensions (uses own scale - independent of TimingHud)
    float noticeTextWidth = PluginUtils::calculateMonospaceTextWidth(WidgetDimensions::STANDARD_WIDTH, dim.fontSizeLarge);
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
    else if (showBlueFlag) {
        // Build blue flag text with race numbers only (max 2): "#XX #YY"
        std::string blueFlagText = "";
        int count = 0;
        for (int raceNum : m_blueFlagRaceNums) {
            if (count >= 2) break;  // Max 2 race numbers
            if (count > 0) blueFlagText += " ";  // Space between numbers
            blueFlagText += "#";
            blueFlagText += std::to_string(raceNum);
            count++;
        }

        // Add notice background (blue for blue flag)
        SPluginQuad_t noticeQuad;
        float quadX = noticeQuadX;
        float quadY = noticeQuadY;
        applyOffset(quadX, quadY);
        setQuadPositions(noticeQuad, quadX, quadY, noticeQuadWidth, noticeQuadHeight);
        noticeQuad.m_iSprite = SpriteIndex::SOLID_COLOR;

        // Racing blue background for blue flag notice
        noticeQuad.m_ulColor = PluginUtils::applyOpacity(ColorPalette::BLUE, m_fBackgroundOpacity);
        m_quads.push_back(noticeQuad);

        // Add notice text (blue)
        addString(blueFlagText.c_str(), CENTER_X, noticeY, Justify::CENTER,
            this->getFont(FontCategory::TITLE), ColorPalette::BLUE, dim.fontSizeLarge);
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

        // Add notice text (white)
        addString("FINISHED", CENTER_X, noticeY, Justify::CENTER,
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
        addString("LAST LAP", CENTER_X, noticeY, Justify::CENTER,
            this->getFont(FontCategory::TITLE), this->getColor(ColorSlot::PRIMARY), dim.fontSizeLarge);
    }

    setBounds(noticeQuadX, noticeQuadY, noticeQuadX + noticeQuadWidth, noticeQuadY + noticeQuadHeight);
}

void NoticesWidget::resetToDefaults() {
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
    m_blueFlagRaceNums.clear();
    m_bShowLastLap = false;
    m_bShowFinished = false;
    m_bLastLapTriggered = false;
    m_bFinishedTriggered = false;
    m_bShowSessionPB = false;
    m_bShowFastestLap = false;
    m_bShowAllTimePB = false;
    m_lastLapTriggerTime = {};
    m_finishedTriggerTime = {};
    m_sessionStartTime = 0;
    m_lastSessionState = -1;

    setDataDirty();
}
