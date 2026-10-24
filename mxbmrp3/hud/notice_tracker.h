// ============================================================================
// hud/notice_tracker.h
// Which race notice is up right now (WRONG WAY, BLUE FLAG, ALL-TIME PB, ...):
// the state machine the Notices HUD used to own, moved out so every panel that
// shows a notice reads the SAME one.
//
// WHY ONE OWNER. Most notices are timed off a PluginData flag that is CONSUMED
// when its display window runs out (clearAllTimePB() and friends). Two panels
// each stepping their own copy would race to consume it, and the second one
// would never see the notice. So HudManager steps this once per frame, before
// any HUD updates, and the Notices HUD and the Dash both render current().
//
// Which notices are enabled and how long a timed one stays up are the Notices
// tab's settings (NoticesHud::m_enabledNotices / m_noticeDurationMs), passed to
// step() rather than duplicated here.
//
// The priority ladder and the masking rule (a PB held, not consumed, while a
// status notice covers it) are notice_priority.h's; this file applies them.
// ============================================================================
#pragma once

#include <chrono>
#include <cstdint>

#include "notice_priority.h"
#include "../core/plugin_data.h"

class NoticeTracker {
public:
    // The notice showing, in the ladder's precedence order (highest first).
    enum class Kind : uint8_t {
        None,
        WrongWay,
        HazardAhead,
        RiderOncoming,
        BlueFlag,
        LapperAhead,
        Overtime,
        AllTimePB,
        FastestLap,
        SessionPB,
        SegmentAdded,
        SegmentRemoved,
        Finished,
        LastLap,
        DefaultSetup
    };

    // The notice and its text, formatted once when it changes so a renderer only
    // copies it.
    struct Notice {
        Kind kind = Kind::None;
        char text[24] = "";
        // A second line where the notice has one, else empty: a PB's lap time
        // and its gain on the best it beat ("1:23.456  -0.312"). The Notices
        // HUD's one-row slab leaves it out; the Dash's taller card shows it.
        char detail[24] = "";
    };

    static NoticeTracker& getInstance();

    // Once per frame. watched: some visible panel shows notices this frame. While
    // nothing does, only the expired timed flags are drained (so a panel switched
    // on later does not flash a stale one); the status state is left as it was.
    void step(uint32_t enabledNotices, int durationMs, bool watched);

    const Notice& current() const { return m_current; }
    // Bumped whenever current() changes, so a renderer can rebuild only then.
    uint32_t revision() const { return m_revision; }

    // Back to the start-up state (the Notices HUD's reset).
    void reset();

private:
    NoticeTracker() = default;

    struct StatusTier {
        bool wrongWay = false, hazard = false, blueFlag = false, lapping = false, overtime = false;
        bool anyShowing() const { return wrongWay || hazard || blueFlag || lapping || overtime; }
    };
    // The status-tier notices that outrank and therefore MASK the consumable
    // PB/setup/segment tier. The SINGLE SOURCE for both the mask step() applies to
    // the timers and the ladder resolveCurrent() renders, so they cannot drift.
    StatusTier computeStatusTier(uint32_t enabledNotices) const;
    bool isTimedNoticeActive(std::chrono::steady_clock::time_point triggerTime, int durationMs) const;
    void drainExpired(int durationMs);
    // Re-derive current() from the flags below; bumps the revision on a change.
    void resolveCurrent(uint32_t enabledNotices);

    Notice m_current;
    int m_currentNumber = 0;   // the number m_current.text was formatted with, 0 = none
    PbNoticeLap m_currentLap;   // the lap m_current.detail was formatted with
    uint32_t m_revision = 0;

    // Status notices
    bool m_bIsWrongWay = false;
    bool m_bIsHazardAhead = false;
    // A rider coming AT you rather than one stopped on the track: same slot and
    // priority, only the wording differs.
    bool m_bHazardIsWrongWay = false;
    bool m_bIsBlueFlagged = false;
    bool m_bIsLapping = false;

    // Timed notices
    bool m_bShowOvertime = false;
    bool m_bOvertimeTriggered = false;      // resets on a new session
    bool m_bShowLastLap = false;
    bool m_bShowFinished = false;
    int m_finishedPosition = -1;            // follows a penalty while the notice is up
    bool m_bLastLapTriggered = false;
    bool m_bFinishedTriggered = false;
    bool m_bShowSessionPB = false;
    bool m_bShowFastestLap = false;
    bool m_bShowAllTimePB = false;
    bool m_bShowDefaultSetup = false;
    bool m_bShowSegment = false;
    PluginData::SegmentNoticeKind m_segmentNoticeKind = PluginData::SegmentNoticeKind::None;
    int m_segmentNoticeNumber = 0;          // 1-based segment the notice refers to

    std::chrono::steady_clock::time_point m_overtimeTriggerTime{};
    std::chrono::steady_clock::time_point m_lastLapTriggerTime{};
    std::chrono::steady_clock::time_point m_finishedTriggerTime{};

    // The consumable notices' display windows (ms since steady_clock epoch);
    // see notice_priority.h.
    NoticePriority::Timer m_allTimePBTimer;
    NoticePriority::Timer m_fastestLapTimer;
    NoticePriority::Timer m_sessionPBTimer;
    NoticePriority::Timer m_defaultSetupTimer;
    NoticePriority::Timer m_segmentTimer;

    // Spectate tracking: the triggered flags restart when the displayed rider changes
    int m_lastDisplayRaceNum = -1;
};
