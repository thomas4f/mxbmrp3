// ============================================================================
// hud/tacho_widget.h
// Tacho widget - displays rotating needle (0-15000 RPM) with dial background
// ============================================================================
#pragma once

#include "base_hud.h"
#include "../core/plugin_data.h"
#include "../core/plugin_constants.h"
#include "../core/widget_constants.h"

class TachoWidget : public BaseHud {
public:
    TachoWidget();
    virtual ~TachoWidget() = default;

    void update() override;
    bool handlesDataType(DataChangeType dataType) const override;
    void resetToDefaults();

protected:
    void rebuildLayout() override;

private:
    void rebuildRenderData() override;
    void addNeedleQuad(float centerX, float centerY, float angleRad, float needleLength, float needleWidth);

    // Tacho constants
    static constexpr float MIN_RPM = 0.0f;
    static constexpr float MAX_RPM = 15000.0f;        // Max RPM on dial
    static constexpr float MIN_ANGLE_DEG = -158.0f;   // Angle at 0 RPM
    static constexpr float MAX_ANGLE_DEG = 142.0f;    // Angle at 15000 RPM
    static constexpr float DIAL_SIZE = 0.15f;         // Base dial size in normalized coordinates

    // Needle smoothing (simulates physical inertia of analog gauge)
    static constexpr float NEEDLE_SMOOTH_FACTOR = 0.15f;  // 0.0-1.0: lower = smoother, higher = faster response
    float m_smoothedRpm = 0.0f;  // Current smoothed RPM value for needle display
};
