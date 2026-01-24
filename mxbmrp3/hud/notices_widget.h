// ============================================================================
// hud/notices_widget.h
// Notices widget - displays wrong way and blue flag warnings
// Shows centered notices above the timing HUD area
// ============================================================================
#pragma once

#include "base_hud.h"
#include "../core/plugin_data.h"
#include "../core/plugin_constants.h"
#include "../core/widget_constants.h"
#include <vector>

class NoticesWidget : public BaseHud {
public:
    // Notice visibility flags (bitfield) - configurable via INI
    enum NoticeFlags : uint32_t {
        NOTICE_WRONG_WAY = 1 << 0,
        NOTICE_BLUE_FLAG = 1 << 1,
        NOTICE_LAST_LAP  = 1 << 2,
        NOTICE_FINISHED  = 1 << 3,
        NOTICE_DEFAULT   = NOTICE_WRONG_WAY | NOTICE_LAST_LAP | NOTICE_FINISHED
    };

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

    // Notice state
    bool m_bIsWrongWay;                    // Whether player is currently going wrong way
    std::vector<int> m_blueFlagRaceNums;   // Race numbers of riders to let past (blue flag)
    bool m_bIsLastLap;                     // Whether player is on their last lap
    bool m_bIsFinished;                    // Whether player has finished the race

    // Session tracking for wrong-way grace period
    int m_sessionStartTime;    // Session time when race went to "in progress" state
    int m_lastSessionState;    // Previous session state to detect transitions

    // Wrong way grace period (10 seconds after race start)
    static constexpr int WRONG_WAY_GRACE_PERIOD_MS = 10000;

    // Settings (configurable via INI)
    uint32_t m_enabledNotices = NOTICE_DEFAULT;  // Bitfield of enabled notices
};
