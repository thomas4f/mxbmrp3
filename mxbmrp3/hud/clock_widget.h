// ============================================================================
// hud/clock_widget.h
// Clock widget - displays local time with optional UTC secondary display
// ============================================================================
#pragma once

#include "base_hud.h"
#include "../core/plugin_data.h"
#include "../core/plugin_constants.h"
#include "../core/widget_constants.h"

class ClockWidget : public BaseHud {
public:
    ClockWidget();
    virtual ~ClockWidget() = default;

    void update() override;
    bool handlesDataType(DataChangeType dataType) const override;
    void resetToDefaults();

    // Show UTC time below (or above) the local time
    void setShowUtc(bool show) {
        if (m_bShowUtc != show) {
            m_bShowUtc = show;
            setDataDirty();
        }
    }
    bool getShowUtc() const { return m_bShowUtc; }

    // Flip local/UTC positions (UTC on top, local on bottom)
    void setUtcOnTop(bool utcOnTop) {
        if (m_bUtcOnTop != utcOnTop) {
            m_bUtcOnTop = utcOnTop;
            setDataDirty();
        }
    }
    bool getUtcOnTop() const { return m_bUtcOnTop; }

    // 24-hour format (false = 12-hour with AM/PM)
    void setFormat24h(bool use24h) {
        if (m_bFormat24h != use24h) {
            m_bFormat24h = use24h;
            setDataDirty();
        }
    }
    bool getFormat24h() const { return m_bFormat24h; }

protected:
    void rebuildLayout() override;

private:
    void rebuildRenderData() override;

    // Cached minute to avoid unnecessary rebuilds (only update once per minute)
    int m_cachedLocalMinute;
    int m_cachedUtcMinute;

    // Configuration
    bool m_bShowUtc;     // Show UTC time as secondary line
    bool m_bUtcOnTop;    // Flip: UTC on top, local on bottom
    bool m_bFormat24h;   // 24-hour format (vs 12-hour)
};
