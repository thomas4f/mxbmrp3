// ============================================================================
// hud/notices_hud.h
// Notices HUD - displays warnings and PB notifications
// Shows centered notices above the timing HUD area
// ============================================================================
#pragma once

#include "base_hud.h"
#include "../core/plugin_data.h"
#include "../core/plugin_constants.h"
#include "../core/widget_constants.h"
#include <chrono>

class NoticesHud : public BaseHud {
public:
    // Notice visibility flags (bitfield)
    enum NoticeFlags : uint32_t {
        NOTICE_WRONG_WAY       = 1 << 0,
        NOTICE_BLUE_FLAG       = 1 << 1,
        NOTICE_LAST_LAP        = 1 << 2,
        NOTICE_FINISHED        = 1 << 3,
        NOTICE_ALLTIME_PB      = 1 << 4,
        NOTICE_FASTEST_LAP     = 1 << 5,
        NOTICE_SESSION_PB      = 1 << 6,
        NOTICE_DEFAULT_SETUP   = 1 << 7,
        NOTICE_HAZARD_STATIONARY = 1 << 8,
        NOTICE_HAZARD_WRONG_WAY  = 1 << 9,
        NOTICE_OVERTIME          = 1 << 10,
        NOTICE_SEGMENT           = 1 << 11,
        NOTICE_DEFAULT     = NOTICE_WRONG_WAY | NOTICE_LAST_LAP | NOTICE_FINISHED
                           | NOTICE_ALLTIME_PB | NOTICE_FASTEST_LAP | NOTICE_SESSION_PB
                           | NOTICE_DEFAULT_SETUP | NOTICE_HAZARD_STATIONARY | NOTICE_HAZARD_WRONG_WAY
                           | NOTICE_SEGMENT
    };

    // Timed notice display duration bounds (covers PB notices)
    static constexpr int MIN_NOTICE_DURATION_MS = 1000;
    static constexpr int DEFAULT_NOTICE_DURATION_MS = 5000;
    static constexpr int MAX_NOTICE_DURATION_MS = 30000;
    static constexpr int DURATION_STEP_MS = 1000;

    NoticesHud();
    virtual ~NoticesHud() = default;

    void update() override;
    bool handlesDataType(DataChangeType dataType) const override;
    const char* getIconName() const override { return "hud-notices"; }
    void resetToDefaults();

    // Allow settings system to access private members
    friend class SettingsHud;
    friend class SettingsManager;

protected:
    void rebuildLayout() override;

private:
    void rebuildRenderData() override;

    // Check if a timed notice is still within its display duration
    bool isTimedNoticeActive(std::chrono::steady_clock::time_point triggerTime) const;

    // Notice state
    bool m_bIsWrongWay = false;            // Whether player is currently going wrong way
    bool m_bIsHazardAhead = false;         // Whether there's a hazard rider ahead
    bool m_bIsBlueFlagged = false;         // Whether player should yield (blue flag)

    // Timed notice state (transient - timed display)
    bool m_bShowOvertime;                  // Currently showing overtime notice
    bool m_bOvertimeTriggered;             // Overtime notice already triggered (resets on new session)
    bool m_bShowLastLap;                   // Currently showing last lap notice
    bool m_bShowFinished;                  // Currently showing finished notice
    int m_finishedPosition;                // Current display position when finished (updates if penalties change it)
    bool m_bLastLapTriggered;              // Last lap notice already triggered (resets when no longer on last lap)
    bool m_bFinishedTriggered;             // Finished notice already triggered (resets when no longer finished)
    bool m_bShowSessionPB;                 // Currently showing session PB notice
    bool m_bShowFastestLap;                // Currently showing fastest lap notice
    bool m_bShowAllTimePB;                 // Currently showing all-time PB notice
    bool m_bShowDefaultSetup;              // Currently showing default setup warning (track entry)
    bool m_bShowSegment;                   // Currently showing a segment-timer action notice
    PluginData::SegmentNoticeKind m_segmentNoticeKind = PluginData::SegmentNoticeKind::None;  // Which segment action to show

    // Trigger timestamps for timed notices
    std::chrono::steady_clock::time_point m_overtimeTriggerTime;
    std::chrono::steady_clock::time_point m_lastLapTriggerTime;
    std::chrono::steady_clock::time_point m_finishedTriggerTime;

    // Spectate tracking - reset triggered flags when displayed rider changes
    int m_lastDisplayRaceNum = -1;

    // Session tracking for wrong-way grace period
    int m_sessionStartTime;    // Session time when race went to "in progress" state
    int m_lastSessionState;    // Previous session state to detect transitions

    // Wrong way grace period (10 seconds after race start)
    static constexpr int WRONG_WAY_GRACE_PERIOD_MS = 10000;

    // Settings
    uint32_t m_enabledNotices = NOTICE_DEFAULT;          // Bitfield of enabled notices
    int m_noticeDurationMs = DEFAULT_NOTICE_DURATION_MS; // How long timed notices display (ms)
};
