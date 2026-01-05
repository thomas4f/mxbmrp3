// ============================================================================
// hud/notices_widget.cpp
// Notices widget - displays wrong way and blue flag warnings
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
    , m_bIsLastLap(false)
    , m_bIsFinished(false)
{
    // One-time setup
    DEBUG_INFO("NoticesWidget created");
    setDraggable(false);  // Center display shouldn't be draggable
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

void NoticesWidget::update() {
    const PluginData& pluginData = PluginData::getInstance();

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

    // Check blue flag status
    std::vector<int> blueFlagRaceNums = pluginData.getBlueFlagRaceNums();
    if (blueFlagRaceNums != m_blueFlagRaceNums) {
        m_blueFlagRaceNums = blueFlagRaceNums;
        setDataDirty();
    }

    // Check last lap / finished status
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

    if (isLastLap != m_bIsLastLap) {
        m_bIsLastLap = isLastLap;
        setDataDirty();
    }
    if (isFinished != m_bIsFinished) {
        m_bIsFinished = isFinished;
        setDataDirty();
    }

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

    // Update notice quad position
    setQuadPositions(m_quads[0], noticeQuadX, noticeQuadY, noticeQuadWidth, noticeQuadHeight);

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
    m_strings.clear();
    m_quads.clear();

    // Check which notices are both active and enabled
    bool showWrongWay = m_bIsWrongWay && (m_enabledNotices & NOTICE_WRONG_WAY);
    bool showBlueFlag = !m_blueFlagRaceNums.empty() && (m_enabledNotices & NOTICE_BLUE_FLAG);
    bool showFinished = m_bIsFinished && (m_enabledNotices & NOTICE_FINISHED);
    bool showLastLap = m_bIsLastLap && (m_enabledNotices & NOTICE_LAST_LAP);

    // Only render if there's something to show
    // Priority: WRONG WAY > BLUE FLAG > FINISHED > LAST LAP
    if (!showWrongWay && !showBlueFlag && !showLastLap && !showFinished) {
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

    const ColorConfig& colors = ColorConfig::getInstance();

    if (showWrongWay) {
        // Add notice background (red for warning)
        SPluginQuad_t noticeQuad;
        float quadX = noticeQuadX;
        float quadY = noticeQuadY;
        applyOffset(quadX, quadY);
        setQuadPositions(noticeQuad, quadX, quadY, noticeQuadWidth, noticeQuadHeight);
        noticeQuad.m_iSprite = SpriteIndex::SOLID_COLOR;
        noticeQuad.m_ulColor = PluginUtils::applyOpacity(colors.getNegative(), m_fBackgroundOpacity);
        m_quads.push_back(noticeQuad);

        // Add notice text (red)
        addString("WRONG WAY", CENTER_X, noticeY, Justify::CENTER,
            Fonts::getTitle(), colors.getNegative(), dim.fontSizeLarge);
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
            Fonts::getTitle(), ColorPalette::BLUE, dim.fontSizeLarge);
    }
    else if (showFinished) {
        // Add notice background (semantic background color for finished)
        SPluginQuad_t noticeQuad;
        float quadX = noticeQuadX;
        float quadY = noticeQuadY;
        applyOffset(quadX, quadY);
        setQuadPositions(noticeQuad, quadX, quadY, noticeQuadWidth, noticeQuadHeight);
        noticeQuad.m_iSprite = SpriteIndex::SOLID_COLOR;
        noticeQuad.m_ulColor = PluginUtils::applyOpacity(colors.getBackground(), m_fBackgroundOpacity);
        m_quads.push_back(noticeQuad);

        // Add notice text (white)
        addString("FINISHED", CENTER_X, noticeY, Justify::CENTER,
            Fonts::getTitle(), colors.getPrimary(), dim.fontSizeLarge);
    }
    else if (showLastLap) {
        // Add notice background (semantic neutral color for last lap)
        SPluginQuad_t noticeQuad;
        float quadX = noticeQuadX;
        float quadY = noticeQuadY;
        applyOffset(quadX, quadY);
        setQuadPositions(noticeQuad, quadX, quadY, noticeQuadWidth, noticeQuadHeight);
        noticeQuad.m_iSprite = SpriteIndex::SOLID_COLOR;
        noticeQuad.m_ulColor = PluginUtils::applyOpacity(colors.getNeutral(), m_fBackgroundOpacity);
        m_quads.push_back(noticeQuad);

        // Add notice text (white)
        addString("LAST LAP", CENTER_X, noticeY, Justify::CENTER,
            Fonts::getTitle(), colors.getPrimary(), dim.fontSizeLarge);
    }

    setBounds(noticeQuadX, noticeQuadY, noticeQuadX + noticeQuadWidth, noticeQuadY + noticeQuadHeight);
}

void NoticesWidget::resetToDefaults() {
    m_bVisible = false;  // Disabled by default
    m_bShowTitle = false;
    setTextureVariant(0);  // No texture by default
    m_fBackgroundOpacity = 0.1f;
    m_fScale = 1.0f;
    setPosition(0.0f, 0.0f);
    m_enabledNotices = NOTICE_DEFAULT;
    setDataDirty();
}
