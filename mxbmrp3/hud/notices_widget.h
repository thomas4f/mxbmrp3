// ============================================================================
// hud/notices_widget.h
// Notices widget - displays wrong way and blue flag warnings
// Shows centered notices above the timing widget area
// ============================================================================
#pragma once

#include "base_hud.h"
#include "../core/plugin_data.h"
#include "../core/plugin_constants.h"
#include "../core/widget_constants.h"
#include <vector>

class NoticesWidget : public BaseHud {
public:
    NoticesWidget();
    virtual ~NoticesWidget() = default;

    void update() override;
    bool handlesDataType(DataChangeType dataType) const override;
    void resetToDefaults();

protected:
    void rebuildLayout() override;

private:
    void rebuildRenderData() override;

    // Notice state
    bool m_bIsWrongWay;                    // Whether player is currently going wrong way
    std::vector<int> m_blueFlagRaceNums;   // Race numbers of riders to let past (blue flag)

    // Session tracking for wrong-way grace period
    int m_sessionStartTime;    // Session time when race went to "in progress" state
    int m_lastSessionState;    // Previous session state to detect transitions

    // Wrong way grace period (10 seconds after race start)
    static constexpr int WRONG_WAY_GRACE_PERIOD_MS = 10000;
};
