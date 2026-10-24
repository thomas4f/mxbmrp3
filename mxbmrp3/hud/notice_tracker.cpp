// ============================================================================
// hud/notice_tracker.cpp
// Which race notice is up right now - see notice_tracker.h.
// ============================================================================
#include "notice_tracker.h"

#include <cstdio>
#include <cstring>

#include "notices_hud.h"
#include "notice_priority.h"
#include "../core/plugin_constants.h"

namespace {
// Ordinal suffix for position display (1ST, 2ND, 3RD, 4TH, ...)
const char* ordinalSuffix(int n) {
    if (n % 100 >= 11 && n % 100 <= 13) return "TH";
    switch (n % 10) {
        case 1: return "ST";
        case 2: return "ND";
        case 3: return "RD";
        default: return "TH";
    }
}
}  // namespace

NoticeTracker& NoticeTracker::getInstance() {
    static NoticeTracker instance;
    return instance;
}

void NoticeTracker::reset() {
    const uint32_t revision = m_revision;
    *this = NoticeTracker();
    // A reset is a change for anything that rendered the old notice.
    m_revision = revision + 1;
}

bool NoticeTracker::isTimedNoticeActive(std::chrono::steady_clock::time_point triggerTime,
                                        int durationMs) const {
    auto elapsed = std::chrono::steady_clock::now() - triggerTime;
    return std::chrono::duration_cast<std::chrono::milliseconds>(elapsed).count() < static_cast<long long>(durationMs);
}

NoticeTracker::StatusTier NoticeTracker::computeStatusTier(uint32_t enabledNotices) const {
    // The render ladder gives blue flag precedence over lapper, so lapper is
    // suppressed when blue flag shows.
    StatusTier s;
    s.wrongWay = m_bIsWrongWay && (enabledNotices & NoticesHud::NOTICE_WRONG_WAY);
    s.hazard   = m_bIsHazardAhead && !m_bFinishedTriggered;
    s.blueFlag = m_bIsBlueFlagged && !m_bFinishedTriggered && (enabledNotices & NoticesHud::NOTICE_BLUE_FLAG);
    s.lapping  = m_bIsLapping && !m_bFinishedTriggered && !s.blueFlag && (enabledNotices & NoticesHud::NOTICE_LAPPING);
    s.overtime = m_bShowOvertime && (enabledNotices & NoticesHud::NOTICE_OVERTIME);
    return s;
}

void NoticeTracker::drainExpired(int durationMs) {
    PluginData& pd = PluginData::getInstance();
    if (pd.hasNewAllTimePB() && !isTimedNoticeActive(pd.getAllTimePBTime(), durationMs))
        pd.clearAllTimePB();
    if (pd.hasNewFastestLap() && !isTimedNoticeActive(pd.getFastestLapTime(), durationMs))
        pd.clearFastestLap();
    if (pd.hasNewSessionPB() && !isTimedNoticeActive(pd.getSessionPBTime(), durationMs))
        pd.clearSessionPB();
    if (pd.hasDefaultSetupNotice() && !isTimedNoticeActive(pd.getDefaultSetupTime(), durationMs))
        pd.clearDefaultSetupNotice();
    if (pd.hasSegmentNotice() && !isTimedNoticeActive(pd.getSegmentNoticeTime(), durationMs))
        pd.clearSegmentNotice();
}

void NoticeTracker::step(uint32_t enabledNotices, int durationMs, bool watched) {
    // Nothing shows a notice: drain the expired timed flags in PluginData so one does
    // not linger until session end and flash when a panel is switched on.
    if (!watched) {
        drainExpired(durationMs);
        return;
    }

    PluginData& pluginData = PluginData::getInstance();
    const SessionData& sessionData = pluginData.getSessionData();

    // Wrong-way notice, suppressed during a standing (grid) start until the rider clears the first
    // split. The grid launch (facing sideways/backward on the grid, then the run to S/F) routinely
    // trips wrong-way; the grid-start grace is sector-based (see PluginData::isInGridStartGrace),
    // so it covers races AND grid qualifying and adapts to the variable gate hold. Pit starts never
    // enter this grace, so wrong-way shows there as before.
    m_bIsWrongWay = pluginData.isPlayerGoingWrongWay() && !pluginData.isInGridStartGrace();

    // Hazard ahead (poll cached query, filter by enabled notice types)
    {
        bool hazardAhead = false;
        bool hazardIsWrongWay = false;
        const bool stationaryEnabled = (enabledNotices & NoticesHud::NOTICE_HAZARD_STATIONARY) != 0;
        const bool wrongWayEnabled = (enabledNotices & NoticesHud::NOTICE_HAZARD_WRONG_WAY) != 0;
        if (stationaryEnabled || wrongWayEnabled) {
            const auto& hazardRaceNums = pluginData.getHazardRaceNums();
            for (int raceNum : hazardRaceNums) {
                HazardType type = pluginData.getRiderHazardType(raceNum);
                if (wrongWayEnabled && type == HazardType::WrongWay) {
                    // An oncoming rider outranks a stopped one for WORDING, so once
                    // one is found it wins regardless of scan order.
                    hazardAhead = true;
                    hazardIsWrongWay = true;
                    break;
                }
                if (stationaryEnabled && type == HazardType::Stationary) {
                    hazardAhead = true;   // keep scanning in case a wrong-way rider follows
                }
            }
        }
        m_bIsHazardAhead = hazardAhead;
        m_bHazardIsWrongWay = hazardIsWrongWay;
    }

    m_bIsBlueFlagged = pluginData.isPlayerBlueFlagged();
    // Lapping: the mirror of blue flag -- the player closing on a backmarker ahead
    m_bIsLapping = pluginData.isPlayerLapping();

    // Overtime - a timed notice when a time+laps race enters overtime
    {
        const bool overtime = sessionData.overtimeStarted;
        if (overtime && !m_bOvertimeTriggered) {
            m_overtimeTriggerTime = std::chrono::steady_clock::now();
            m_bShowOvertime = true;
            m_bOvertimeTriggered = true;
        }
        if (m_bShowOvertime && !isTimedNoticeActive(m_overtimeTriggerTime, durationMs)) {
            m_bShowOvertime = false;
        }
        // Reset triggered flag when overtime clears (new session)
        if (!overtime && m_bOvertimeTriggered) {
            m_bOvertimeTriggered = false;
        }
    }

    // Last lap / finished - timed notices on the transitions
    bool isLastLap = false;
    bool isFinished = false;
    const int displayRaceNum = pluginData.getDisplayRaceNum();

    // Reset triggered and display flags when the spectated rider changes
    if (displayRaceNum != m_lastDisplayRaceNum) {
        m_bLastLapTriggered = false;
        m_bFinishedTriggered = false;
        m_bShowLastLap = false;
        m_bShowFinished = false;
        m_lastDisplayRaceNum = displayRaceNum;
    }
    const StandingsData* standing = (displayRaceNum > 0) ? pluginData.getStanding(displayRaceNum) : nullptr;
    if (standing && standing->numLaps >= 0) {
        isFinished = sessionData.isRiderFinished(standing->numLaps, standing->numLapsAtLeaderFinish);
        if (!isFinished && pluginData.isRaceSession()) {
            isLastLap = sessionData.isRiderOnLastLap(standing->numLaps, standing->numLapsAtLeaderFinish);
        }
    }

    // Last lap: once per last-lap period
    if (isLastLap && !m_bLastLapTriggered) {
        m_lastLapTriggerTime = std::chrono::steady_clock::now();
        m_bShowLastLap = true;
        m_bLastLapTriggered = true;
    }

    // Finished: once per race
    if (isFinished && !m_bFinishedTriggered) {
        m_finishedTriggerTime = std::chrono::steady_clock::now();
        m_bShowFinished = true;
        m_bFinishedTriggered = true;
        m_finishedPosition = pluginData.getDisplayPositionForRaceNum(displayRaceNum);
    }

    // While the finished notice is up, follow position changes (e.g. a penalty)
    if (m_bShowFinished && displayRaceNum > 0) {
        m_finishedPosition = pluginData.getDisplayPositionForRaceNum(displayRaceNum);
    }

    if (m_bShowLastLap && !isTimedNoticeActive(m_lastLapTriggerTime, durationMs)) {
        m_bShowLastLap = false;
    }
    if (m_bShowFinished && !isTimedNoticeActive(m_finishedTriggerTime, durationMs)) {
        m_bShowFinished = false;
    }

    // Reset triggered flags when conditions clear (e.g. a new race starts) so the
    // notices can fire again
    if (!isLastLap && m_bLastLapTriggered) {
        m_bLastLapTriggered = false;
    }
    if (!isFinished && m_bFinishedTriggered) {
        m_bFinishedTriggered = false;
    }

    // Higher-priority *status* notices that legitimately co-occur with a PB/setup/
    // segment on the same lap. While one is up, a consumable notice masked behind it
    // must not run down its display timer and get cleared unseen. (Overtime is timed
    // but still outranks the PB tier, so it masks too.)
    const bool statusMasking = computeStatusTier(enabledNotices).anyShowing();

    const long long nowMs = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count();
    auto toMs = [](std::chrono::steady_clock::time_point tp) {
        return std::chrono::duration_cast<std::chrono::milliseconds>(tp.time_since_epoch()).count();
    };
    const long long windowMs = static_cast<long long>(durationMs);

    // The window is measured from when the notice became unmasked (see
    // notice_priority.h), so a masked notice is held rather than consumed. A
    // disabled notice is never held (it can't be seen anyway) so it drains normally.
    auto checkTimedNotice = [&](bool hasNew, bool enabled, std::chrono::steady_clock::time_point triggerTime,
                                NoticePriority::Timer& timer, auto clearFn, bool& showFlag) {
        NoticePriority::TimerOut r = NoticePriority::stepTimer(
            { hasNew, statusMasking && enabled, toMs(triggerTime), timer }, nowMs, windowMs);
        timer = r.timer;
        if (r.consume) clearFn();
        showFlag = r.show;
    };

    checkTimedNotice(pluginData.hasNewAllTimePB(), (enabledNotices & NoticesHud::NOTICE_ALLTIME_PB) != 0,
                     pluginData.getAllTimePBTime(), m_allTimePBTimer,
                     [&]() { pluginData.clearAllTimePB(); }, m_bShowAllTimePB);
    checkTimedNotice(pluginData.hasNewFastestLap(), (enabledNotices & NoticesHud::NOTICE_FASTEST_LAP) != 0,
                     pluginData.getFastestLapTime(), m_fastestLapTimer,
                     [&]() { pluginData.clearFastestLap(); }, m_bShowFastestLap);
    checkTimedNotice(pluginData.hasNewSessionPB(), (enabledNotices & NoticesHud::NOTICE_SESSION_PB) != 0,
                     pluginData.getSessionPBTime(), m_sessionPBTimer,
                     [&]() { pluginData.clearSessionPB(); }, m_bShowSessionPB);

    // Default setup warning (only fires when RunHandler detects a default setup)
    checkTimedNotice(pluginData.hasDefaultSetupNotice(), (enabledNotices & NoticesHud::NOTICE_DEFAULT_SETUP) != 0,
                     pluginData.getDefaultSetupTime(), m_defaultSetupTimer,
                     [&]() { pluginData.clearDefaultSetupNotice(); }, m_bShowDefaultSetup);

    // Segment timer action notice (start set / end set / cleared) - carries a kind,
    // so it can't use the helper. Re-read on kind change too, so a second press
    // replaces the notice immediately instead of queueing behind the one up. (Always
    // enabled, so it is held while a status notice masks it, like the PB tier.)
    {
        NoticePriority::TimerOut r = NoticePriority::stepTimer(
            { pluginData.hasSegmentNotice(), statusMasking, toMs(pluginData.getSegmentNoticeTime()), m_segmentTimer },
            nowMs, windowMs);
        m_segmentTimer = r.timer;
        if (r.consume) pluginData.clearSegmentNotice();
        m_bShowSegment = r.show;
        m_segmentNoticeKind = r.show ? pluginData.getSegmentNoticeKind() : PluginData::SegmentNoticeKind::None;
        m_segmentNoticeNumber = r.show ? pluginData.getSegmentNoticeNumber() : 0;
    }

    resolveCurrent(enabledNotices);
}

void NoticeTracker::resolveCurrent(uint32_t enabledNotices) {
    // Priority: WRONG WAY > HAZARD AHEAD > BLUE FLAG > LAPPER AHEAD > OVERTIME > ALL-TIME PB >
    // FASTEST LAP > SESSION PB > SEGMENT > FINISHED > LAST LAP > DEFAULT SETUP
    //
    // Runs every frame, so it only DECIDES here (a kind, a fixed text, a number);
    // the text is formatted below, once, when that decision changes.
    const StatusTier status = computeStatusTier(enabledNotices);
    Kind kind = Kind::None;
    const char* text = "";
    int number = 0;   // the segment, or the finishing position; 0 = none
    PbNoticeLap lap;   // a PB's lap, for the detail line
    const PluginData& pluginData = PluginData::getInstance();
    if (status.wrongWay) {
        kind = Kind::WrongWay;  text = "WRONG WAY";
    } else if (status.hazard) {
        // Same slot either way; only the wording separates a rider coming at you
        // from one stopped on the track, which call for opposite reactions.
        kind = m_bHazardIsWrongWay ? Kind::RiderOncoming : Kind::HazardAhead;
        text = m_bHazardIsWrongWay ? "RIDER ONCOMING" : "HAZARD AHEAD";
    } else if (status.blueFlag) {
        kind = Kind::BlueFlag;  text = "BLUE FLAG";
    } else if (status.lapping) {
        kind = Kind::LapperAhead;  text = "LAPPER AHEAD";
    } else if (status.overtime) {
        kind = Kind::Overtime;  text = "OVERTIME";
    } else if (m_bShowAllTimePB && (enabledNotices & NoticesHud::NOTICE_ALLTIME_PB)) {
        kind = Kind::AllTimePB;  text = "ALL-TIME PB";  lap = pluginData.getAllTimePBLap();
    } else if (m_bShowFastestLap && (enabledNotices & NoticesHud::NOTICE_FASTEST_LAP)) {
        kind = Kind::FastestLap;  text = "FASTEST LAP";  lap = pluginData.getFastestLapLap();
    } else if (m_bShowSessionPB && (enabledNotices & NoticesHud::NOTICE_SESSION_PB)) {
        kind = Kind::SessionPB;  text = "SESSION PB";  lap = pluginData.getSessionPBLap();
    } else if (m_bShowSegment) {  // always on -- self-gated by the segment hotkey action
        // Named by the point's 1-based position ("SEG 3 ADDED"), so notices count up
        // as points are placed and down as they are removed.
        const bool added = (m_segmentNoticeKind == PluginData::SegmentNoticeKind::Added);
        kind = added ? Kind::SegmentAdded : Kind::SegmentRemoved;
        if (m_segmentNoticeKind == PluginData::SegmentNoticeKind::None) text = "SEGMENT";
        else if (m_segmentNoticeNumber >= 1) number = m_segmentNoticeNumber;
        else text = added ? "SEG ADDED" : "SEG REMOVED";
    } else if (m_bShowFinished && (enabledNotices & NoticesHud::NOTICE_FINISHED)) {
        kind = Kind::Finished;
        if (m_finishedPosition > 0) number = m_finishedPosition;
        else text = "FINISHED";
    } else if (m_bShowLastLap && (enabledNotices & NoticesHud::NOTICE_LAST_LAP)) {
        kind = Kind::LastLap;  text = PluginConstants::DisplayStrings::SessionClock::FINAL_LAP;
    } else if (m_bShowDefaultSetup && (enabledNotices & NoticesHud::NOTICE_DEFAULT_SETUP)) {
        kind = Kind::DefaultSetup;  text = "DEFAULT SETUP";
    }

    // A fixed text is compared as-is; a numbered one by its number (the text it
    // formats to follows from kind + number).
    // A PB beaten again while its notice is up keeps kind and text: the lap tells.
    const bool same = kind == m_current.kind && number == m_currentNumber &&
                      (number != 0 || std::strcmp(text, m_current.text) == 0) &&
                      lap.lapMs == m_currentLap.lapMs && lap.previousMs == m_currentLap.previousMs;
    if (same) return;

    m_current.kind = kind;
    m_currentNumber = number;
    m_currentLap = lap;
    m_current.detail[0] = '\0';
    if (lap.lapMs > 0) {
        char time[16];
        PluginUtils::formatLapTime(lap.lapMs, time, sizeof(time));
        if (lap.hasGain()) {
            char gain[16];
            PluginUtils::formatTimeDiff(gain, sizeof(gain), lap.gainMs());
            snprintf(m_current.detail, sizeof(m_current.detail), "%s  %s", time, gain);
        } else {
            snprintf(m_current.detail, sizeof(m_current.detail), "%s", time);
        }
    }
    if (number == 0) {
        snprintf(m_current.text, sizeof(m_current.text), "%s", text);
    } else if (kind == Kind::Finished) {
        snprintf(m_current.text, sizeof(m_current.text), "FINISHED %d%s", number, ordinalSuffix(number));
    } else {
        snprintf(m_current.text, sizeof(m_current.text),
                 kind == Kind::SegmentAdded ? "SEG %d ADDED" : "SEG %d REMOVED", number);
    }
    ++m_revision;
}
