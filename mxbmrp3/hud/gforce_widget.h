// ============================================================================
// hud/gforce_widget.h
// G-force widget - displays chassis-local lateral/longitudinal G as a dot
// on a G-G diagram (motorsport telemetry style)
// ============================================================================
#pragma once

#include "base_hud.h"
#include "../core/plugin_data.h"
#include "../core/plugin_constants.h"
#include "../core/widget_constants.h"

class GForceWidget : public BaseHud {
public:
    GForceWidget();
    virtual ~GForceWidget() = default;

    void update() override;
    bool handlesDataType(DataChangeType dataType) const override;
    void resetToDefaults();

    // Public for settings access (INI-only)
    bool m_bShowMaxText = true;            // Show recorded max-g text inside the ring
    bool m_bShowMaxMarker = true;          // Show lingering max-position marker on gauge
    int m_maxMarkerLingerFrames = 60;      // Frames to keep the max marker visible after dot moves
    float m_maxScale = 5.0f;               // Full-scale of the ring in g (INI-tunable; bumped to 5
                                           // so MX landings/hard hits visibly saturate to red rather
                                           // than pegging on a tiny 2g ring)

protected:
    void rebuildLayout() override;

private:
    void rebuildRenderData() override;
    void resetTracking();
    // FMX-style arc segment renderer (uses quads, so opacity is honored)
    void addArcSegment(float centerX, float centerY, float innerRadius, float outerRadius,
                       float startAngleRad, float endAngleRad, unsigned long color, int numSegments);
    // Render a textured sprite (icon) centered at (x,y), or fall back to a solid quad
    // if the icon isn't available. Size is the full edge length in screen units.
    void addIconDot(float x, float y, int spriteIndex, unsigned long color, float size);
    // Piecewise color gradient driven by current G magnitude:
    // 0 → POSITIVE, m_maxScale/2 → NEUTRAL, m_maxScale (or above) → NEGATIVE.
    unsigned long getMagnitudeColor(float magnitude) const;

    static constexpr int RING_SEGMENTS = 30;           // Match FMX COMBO_ARC_SEGMENTS
    static constexpr float DOT_SMOOTH_FACTOR = 0.10f;  // 0.0-1.0: lower = smoother, higher = faster response
                                                       // Tighter than tacho/speedo (0.15) and lean (0.2)
                                                       // since acceleration is a noisier derivative signal

    // Smoothed display values — felt-G convention (iRacing/AC-style live HUD): left
    // turn pushes the rider right, brake throws them forward. See rebuildRenderData.
    float m_smoothedLat = 0.0f;            // Smoothed -accelX (right positive on screen)
    float m_smoothedLong = 0.0f;           // Smoothed -accelZ (brake/decel positive = up, throttle negative = down)

    // Recent-peak marker (drives both the marker dot's position on the ring and the
    // bottom-line "peak" text). Lingers for m_maxMarkerLingerFrames after the live
    // signal drops below the recorded peak, then resets — same pattern as
    // BarsWidget's m_markerValues. So a one-off spawn jolt at session start doesn't
    // permanently inflate the displayed peak; it shows for a beat and clears.
    float m_markerLat = 0.0f;
    float m_markerLong = 0.0f;
    float m_markerMagnitude = 0.0f;
    int m_markerFramesRemaining = 0;

    // Crash recovery / session / spectate target tracking (mirrors LeanWidget)
    bool m_wasCrashed = false;
    int m_cachedSessionGeneration = -1;
    int m_lastDisplayedRaceNum = -1;
};
