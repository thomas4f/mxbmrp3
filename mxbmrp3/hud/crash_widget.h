// ============================================================================
// hud/crash_widget.h
// Crash counter - a big tally with a Reset button under it.
//
// WHO THIS IS FOR, because it is not the stats page. StatsManager already counts
// crashes per track+bike, and that is the right shape for "how do I do at
// Southwick on the 450". This is for a STREAMER: one number their viewers watch
// climb across a whole stream, over practice, races, server hops and restarts,
// zeroed only when the rider decides the run starts now. Neither number
// substitutes for the other, which is why this is a tally of its own
// (GlobalStats::crashTally) rather than a view of the per-track history.
//
// Sized to the Gear and Speed widgets deliberately: it is the third member of
// that row for anyone who tiles them, so it takes their content height (a
// two-row value over a one-row extra) rather than the single row every other
// big-value widget uses.
// ============================================================================
#pragma once

#include "base_hud.h"
#include "../core/plugin_constants.h"
#include "../core/widget_constants.h"

class CrashWidget : public BaseHud {
public:
    CrashWidget();
    virtual ~CrashWidget() = default;

    void update() override;
    bool handlesDataType(DataChangeType dataType) const override;
    void resetToDefaults();

    // The Reset button's job. Also the CRASH_RESET hotkey's, which has no row in
    // the settings panel (that tab is what sizes the panel, and one more row put it
    // past the bottom of the screen) but binds by hand as `crash_reset_key`.
    void resetCounter();

    // INI-only: hide the button for a clean capture, and reset by hotkey alone.
    bool m_bShowResetButton = true;

    friend class SettingsManager;

protected:
    void rebuildLayout() override;

private:
    void rebuildRenderData() override;
    void handleClickDetection();

    // The button's bounds, stored at rebuild and hit-tested in update() -- the
    // same shape VersionWidget's notification buttons use, offsets applied at the
    // test rather than baked in, so dragging the widget moves the target with it.
    float m_resetLeft = 0.0f;
    float m_resetTop = 0.0f;
    float m_resetWidth = 0.0f;
    float m_resetHeight = 0.0f;
    bool m_resetHovered = false;
    bool m_wasLeftPressed = false;

    int m_cachedCount = -1;   // rebuild only when the number actually moves

};
