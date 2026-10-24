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
// while the rest stay lit. The newest lit segment fades up over its stretch
// of revs, from its unlit opacity in LEAD_STEPS steps; it still comes on where
// a whole light would, so the first red one shows at the shift point, and it
// glows full from its first step (the act-now cue). The shift point is the
// PLAYER's bike setup (the gate GearWidget applies to its shift colour), so
// while spectating the strip shows plain rpm: a fixed red share, no flash.
// ============================================================================
#pragma once

#include "base_hud.h"
#include <algorithm>
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

    // THE LIGHTS, for any strip drawn from them (this widget, the Dash's RPM
    // page): which of `segments` are lit and how the red end behaves, from the
    // display rider's telemetry and the session's rev range. Integers only, so a
    // caller compares a fresh one with what it drew and rebuilds only when the
    // lights move, not at the telemetry rate.
    struct Lights {
        int lit = 0;
        int red = 1;              // how many of the segments are red (shift point on)
        bool onLimiter = false;
        bool flashOn = false;     // the red segments' limiter flash, lit half
        bool hasRange = false;
        int lead = 0;             // the newest light's (index lit - 1) step, 0..LEAD_STEPS-1
        bool operator!=(const Lights& o) const {
            return lit != o.lit || red != o.red || onLimiter != o.onLimiter ||
                   flashOn != o.flashOn || hasRange != o.hasRange || lead != o.lead;
        }
    };
    static Lights readLights(int segments);
    // With m_bSegmentGaps on, a gap is this share of one segment's pitch.
    static constexpr float SEGMENT_GAP_SHARE = 0.25f;
    // An unlit segment is its own colour at this opacity, so the strip's shape
    // and colour bands read before anything lights.
    static constexpr float UNLIT_OPACITY = 0.25f;
    // A light fades up in this many steps (the Dash's fuel bars too). Each step
    // rebuilds the strip, so this is the rebuild rate: LEAD_STEPS a light, not
    // the telemetry rate.
    static constexpr int LEAD_STEPS = 4;
    // A light `step` steps (0..LEAD_STEPS-1) into fading up: its share of the
    // way on, a step's worth as soon as it starts.
    static float stepShare(int step) {
        return static_cast<float>(step + 1) / static_cast<float>(LEAD_STEPS);
    }
    // The opacity of a light that far on: UNLIT_OPACITY at 0, whole at 1.
    static float onOpacity(float on) { return UNLIT_OPACITY + (1.0f - UNLIT_OPACITY) * on; }
    // Segment i's colour: green, amber for the stretch before the red, red from
    // the shift point on, dimmed while unlit and part way while the newest
    // fades up; the red ones flash on the limiter. segmentInk is the same
    // colour undimmed, for the glow.
    static unsigned long segmentColor(int i, int segments, const Lights& lights,
                                      unsigned long positive, unsigned long warning, unsigned long negative);
    static unsigned long segmentInk(int i, int segments, const Lights& lights,
                                    unsigned long positive, unsigned long warning, unsigned long negative);
    // Whether segment i is lit (its flash's lit half, on the limiter).
    static bool segmentLit(int i, int segments, const Lights& lights);
    // Whether segment i is one of the red ones (shift point to limiter).
    static bool segmentRed(int i, int segments, const Lights& lights) {
        return i >= segments - std::min(lights.red, segments);
    }
    // A lit segment's glow (BaseHud::addGlow): its reach as a share of the strip's
    // thickness, and its opacity. Shared with the Dash's RPM page.
    static constexpr float GLOW_SPREAD = 0.9f;
    static constexpr float GLOW_OPACITY = 0.45f;

    friend class SettingsManager;

protected:
    void rebuildLayout() override;

private:
    void rebuildRenderData() override;
    // How many of the strip's segments are red (shift point to limiter).
    static int redSegments(int segments, int shiftRPM, int limiterRPM);

    // The state the quads depend on. update() recomputes it every tick and only
    // rebuilds when it moves: RPM changes every tick, the lit count rarely does.
    Lights m_lights;
};
