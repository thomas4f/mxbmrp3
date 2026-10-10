// ============================================================================
// hud/rpm_widget.h
// RPM widget - a strip of shift lights across the top half of the rev range.
//
// WHAT IT ADDS next to its neighbours: Gear turns red at the shift point and
// Tacho is a dial, both of which have to be LOOKED AT. A strip of lights is read
// from the corner of the eye, so it is sized to sit under Gear + Speed and span
// both by default.
//
// Segments light left to right from half the limiter up (half the max rpm on a
// vehicle without one): green, amber for the last stretch before the shift
// point, red from the shift point on. On the limiter the red segments flash
// while the rest stay lit. The shift point is the
// PLAYER's bike setup (the gate GearWidget applies to its shift colour), so
// while spectating the strip shows plain rpm: a fixed red share, no flash.
// ============================================================================
#pragma once

#include "base_hud.h"
#include "../core/plugin_constants.h"
#include "../core/widget_constants.h"

class RpmWidget : public BaseHud {
public:
    RpmWidget();
    virtual ~RpmWidget() = default;

    void update() override;
    bool handlesDataType(DataChangeType dataType) const override;
    void resetToDefaults();

    // INI-only settings
    static constexpr int DEFAULT_WIDTH = 15;
    static constexpr int MIN_SEGMENTS = 3;
    static constexpr int MAX_SEGMENTS = 40;
    static constexpr int MIN_WIDTH = WidgetDimensions::SMALL_WIDGET_WIDTH;
    static constexpr int MAX_WIDTH = 60;
    int m_segments = 15;         // number of lights
    bool m_bSegmentGaps = true;  // off = one continuous bar
    bool m_bVertical = false;    // on = stood on its end, Gear/Speed height
    // Content width in normal-font characters (horizontal only). The default spans Gear + Speed at
    // their default positions (rpm_widget_test pins that).
    int m_widthChars = DEFAULT_WIDTH;

    friend class SettingsManager;

protected:
    void rebuildLayout() override;

private:
    void rebuildRenderData() override;
    // How many of the strip's segments are red (shift point to limiter).
    static int redSegments(int segments, int shiftRPM, int limiterRPM);

    // The state the quads depend on. update() recomputes it every tick and only
    // rebuilds when it moves: RPM changes every tick, the lit count rarely does.
    int m_lit = 0;
    bool m_flashOn = false;
    bool m_onLimiter = false;
    bool m_hasRange = false;
    int m_red = 1;
};
