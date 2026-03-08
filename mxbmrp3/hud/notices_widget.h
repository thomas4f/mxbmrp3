// ============================================================================
// hud/notices_widget.h
// Notices widget - displays warnings and PB notifications
// Shows centered notices above the timing HUD area
// ============================================================================
#pragma once

#include "base_hud.h"
#include "../core/plugin_data.h"
#include "../core/plugin_constants.h"
#include "../core/widget_constants.h"
#include <vector>
#include <chrono>

class NoticesWidget : public BaseHud {
public:
    // Notice visibility flags (bitfield) - configurable via INI
    enum NoticeFlags : uint32_t {
        NOTICE_WRONG_WAY   = 1 << 0,
        NOTICE_BLUE_FLAG   = 1 << 1,
        NOTICE_LAST_LAP    = 1 << 2,
        NOTICE_FINISHED    = 1 << 3,
        NOTICE_ALLTIME_PB  = 1 << 4,
        NOTICE_FASTEST_LAP = 1 << 5,
        NOTICE_SESSION_PB  = 1 << 6,
        NOTICE_DEFAULT     = NOTICE_WRONG_WAY | NOTICE_LAST_LAP | NOTICE_FINISHED
                           | NOTICE_ALLTIME_PB | NOTICE_FASTEST_LAP | NOTICE_SESSION_PB
    };

    // Timed notice display duration bounds (covers PB notices)
    static constexpr int MIN_NOTICE_DURATION_MS = 500;
    static constexpr int DEFAULT_NOTICE_DURATION_MS = 5000;
    static constexpr int MAX_NOTICE_DURATION_MS = 30000;

    NoticesWidget();
    virtual ~NoticesWidget() = default;

    void update() override;
    bool handlesDataType(DataChangeType dataType) const override;
    void resetToDefaults();

    // Allow SettingsManager to access private members
    friend class SettingsManager;

protected:
    void rebuildLayout() override;

private:
    void rebuildRenderData() override;

    // Check if a timed notice is still within its display duration
    bool isTimedNoticeActive(std::chrono::steady_clock::time_point triggerTime) const;

    // Notice state
    bool m_bIsWrongWay;                    // Whether player is currently going wrong way
    std::vector<int> m_blueFlagRaceNums;   // Race numbers of riders to let past (blue flag)

    // Timed notice state (transient - timed display)
    bool m_bShowLastLap;                   // Currently showing last lap notice
    bool m_bShowFinished;                  // Currently showing finished notice
    bool m_bLastLapTriggered;              // Last lap notice already triggered (resets when no longer on last lap)
    bool m_bFinishedTriggered;             // Finished notice already triggered (resets when no longer finished)
    bool m_bShowSessionPB;                 // Currently showing session PB notice
    bool m_bShowFastestLap;                // Currently showing fastest lap notice
    bool m_bShowAllTimePB;                 // Currently showing all-time PB notice

    // Trigger timestamps for timed notices
    std::chrono::steady_clock::time_point m_lastLapTriggerTime;
    std::chrono::steady_clock::time_point m_finishedTriggerTime;

    // Session tracking for wrong-way grace period
    int m_sessionStartTime;    // Session time when race went to "in progress" state
    int m_lastSessionState;    // Previous session state to detect transitions

    // Wrong way grace period (10 seconds after race start)
    static constexpr int WRONG_WAY_GRACE_PERIOD_MS = 10000;

    // Settings (configurable via INI)
    uint32_t m_enabledNotices = NOTICE_DEFAULT;          // Bitfield of enabled notices
    int m_noticeDurationMs = DEFAULT_NOTICE_DURATION_MS; // How long timed notices display (ms)
};
