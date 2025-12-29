// ============================================================================
// hud/speedo_widget.h
// Speedo widget - displays rotating needle (0-230 km/h) with dial background
// ============================================================================
#pragma once

#include "base_hud.h"
#include "../core/plugin_data.h"
#include "../core/plugin_constants.h"
#include "../core/widget_constants.h"

class SpeedoWidget : public BaseHud {
public:
    SpeedoWidget();
    virtual ~SpeedoWidget() = default;

    void update() override;
    bool handlesDataType(DataChangeType dataType) const override;
    void resetToDefaults();

    // Needle color (configurable via INI)
    void setNeedleColor(unsigned long color) { m_needleColor = color; setDataDirty(); }
    unsigned long getNeedleColor() const { return m_needleColor; }
    static constexpr unsigned long DEFAULT_NEEDLE_COLOR = PluginUtils::makeColor(255, 0, 0);  // Red

protected:
    void rebuildLayout() override;

private:
    void rebuildRenderData() override;
    void addNeedleQuad(float centerX, float centerY, float angleRad, float needleLength, float needleWidth);

    // Speedo constants
    static constexpr float MIN_SPEED_KMH = 0.0f;
    static constexpr float MAX_SPEED_KMH = 230.0f;   // ~143 mph
    static constexpr float MIN_ANGLE_DEG = -158.0f;  // Angle at 0 km/h
    static constexpr float MAX_ANGLE_DEG = 142.0f;   // Angle at 230 km/h
    static constexpr float DIAL_SIZE = 0.15f;        // Base dial size in normalized coordinates

    // Needle smoothing (simulates physical inertia of analog gauge)
    static constexpr float NEEDLE_SMOOTH_FACTOR = 0.15f;  // 0.0-1.0: lower = smoother, higher = faster response
    float m_smoothedSpeed = 0.0f;  // Current smoothed speed value for needle display

    // Needle appearance
    unsigned long m_needleColor = DEFAULT_NEEDLE_COLOR;
};
