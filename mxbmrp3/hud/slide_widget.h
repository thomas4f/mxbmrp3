// ============================================================================
// hud/slide_widget.h
// Slide widget - how far the bike is travelling sideways of where it points,
// on the Lean widget's half arc turned upside down
// ============================================================================
#pragma once

#include "base_hud.h"
#include "../core/plugin_data.h"
#include "../core/plugin_constants.h"
#include "../core/widget_constants.h"

// THE SIGNAL is the slip angle (core/slip_angle.h): the angle between the chassis
// heading and the velocity of its centre of gravity, in the ground plane, while a
// wheel is on the ground and the bike moves faster than walking pace. It is the
// angle FMX's drift tricks and the rumble slide effect already read.
//
// THE PICTURE is the Lean arc hung upside down, like the rear of the bike seen
// from behind: when the rear steps out to the right (the bike travels to the right
// of its nose), the fill swings out to the RIGHT of the bottom-centre gap, and
// to the left for the mirror slide. Full scale is MAX_SLIDE_ANGLE either way.
//
// Player telemetry only: the game sends no velocity or heading for a spectated
// rider's chassis, so spectating and replays show the empty arc and "N/A", the
// way the Lean widget's steer readout does.
class SlideWidget : public BaseHud {
public:
    // Row visibility flags (configurable via INI file). The arc carries the value
    // inside its bowl, so the standard three-line gauge box holds both.
    enum RowFlags : uint32_t {
        ROW_ARC   = 1 << 0,  // Arc gauge (the standard three-line gauge box)
        ROW_VALUE = 1 << 1,  // Slide angle number (inside the arc, or one row alone)

        ROW_DEFAULT = 0x03
    };

    SlideWidget();
    virtual ~SlideWidget() = default;

    void update() override;
    bool handlesDataType(DataChangeType dataType) const override;
    void resetToDefaults();

    // Public for settings access
    uint32_t m_enabledRows = ROW_DEFAULT;  // Bitfield of enabled rows (INI-configurable)
    bool m_bShowMaxMarkers = true;         // Show peak slide markers
    int m_maxMarkerLingerFrames = 60;      // How long max markers linger (~frames, 60 = 1 second at 60fps)

    // Content height in line-height units for the enabled rows.
    float getContentLines() const;

    // Full scale of the arc, degrees either way. A committed motocross slide sits
    // around 15-30 degrees; 45 leaves the ramp's red for a real crossed-up moment.
    static constexpr float MAX_SLIDE_ANGLE = 45.0f;

protected:
    void rebuildLayout() override;

private:
    void rebuildRenderData() override;
    void resetTracking();

    static constexpr int ARC_SEGMENTS = 30;  // Same smoothness as the Lean arc
    // Gauge smoothing, the Lean arc's needle inertia (0..1: higher = faster).
    static constexpr float SLIDE_SMOOTH_FACTOR = 0.2f;

    void updateTracking(float slideDeg, bool hasData, bool isCrashed);
    void addArc(float centerX, float topY, float outerRadius, float barWidthRef, bool hasData, bool isCrashed);
    void addValueText(float x, float y, bool hasData, const ScaledDimensions& dim);

    float m_smoothedSlide = 0.0f;  // signed degrees, positive = rear out to the right

    // Max markers (index 0 = left, 1 = right), PeakMarker semantics like Lean
    int m_maxFramesRemaining[2] = {0, 0};
    float m_markerValue[2] = {0.0f, 0.0f};

    bool m_wasCrashed = false;
    int m_cachedSessionGeneration = -1;
    int m_lastDisplayedRaceNum = -1;

    // The value text, formatted only when the shown whole degree changes.
    int m_shownDegrees = -1;
    char m_valueText[8] = {};
};
