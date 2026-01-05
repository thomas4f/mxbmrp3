// ============================================================================
// hud/lean_widget.h
// Lean widget - displays bike lean/roll angle with half-donut arc gauge
// ============================================================================
#pragma once

#include "base_hud.h"
#include "../core/plugin_data.h"
#include "../core/plugin_constants.h"
#include "../core/widget_constants.h"

class LeanWidget : public BaseHud {
public:
    // Row visibility flags (configurable via INI file)
    // Layout: Arc (rows 1-2), Lean value (row 2), Steer bar (row 3), Steer value (row 4)
    enum RowFlags : uint32_t {
        ROW_ARC         = 1 << 0,  // Arc gauge (spans 2 rows height)
        ROW_LEAN_VALUE  = 1 << 1,  // Lean angle number (below arc)
        ROW_STEER_BAR   = 1 << 2,  // Steering bar
        ROW_STEER_VALUE = 1 << 3,  // Steer angle number (below bar)

        ROW_DEFAULT = 0x0F  // All rows enabled (binary: 1111)
    };

    LeanWidget();
    virtual ~LeanWidget() = default;

    void update() override;
    bool handlesDataType(DataChangeType dataType) const override;
    void resetToDefaults();

    // Arc fill color (configurable via INI) - background uses ColorConfig::getMuted() like BarsWidget
    void setArcFillColor(unsigned long color) { m_arcFillColor = color; setDataDirty(); }
    unsigned long getArcFillColor() const { return m_arcFillColor; }
    static constexpr unsigned long DEFAULT_ARC_FILL_COLOR = PluginUtils::makeColor(255, 255, 255);      // White

    // Max lean values (read-only, tracked internally)
    float getMaxLeanLeft() const { return m_maxLeanLeft; }
    float getMaxLeanRight() const { return m_maxLeanRight; }
    void resetMaxLean() { m_maxLeanLeft = 0.0f; m_maxLeanRight = 0.0f; setDataDirty(); }

    // Public for settings access
    uint32_t m_enabledRows = ROW_DEFAULT;  // Bitfield of enabled rows (INI-configurable)
    bool m_bShowMaxMarkers = true;         // Show peak value markers (default ON for lean/steer)
    int m_maxMarkerLingerFrames = 60;      // How long max markers linger (~frames, 60 = 1 second at 60fps)

    // Helper to calculate content height based on enabled rows
    int getRowCount() const;

protected:
    void rebuildLayout() override;

private:
    void rebuildRenderData() override;
    void addArcSegment(float centerX, float centerY, float innerRadius, float outerRadius,
                       float startAngleRad, float endAngleRad, unsigned long color, int numSegments);

    // Lean gauge constants
    static constexpr float MAX_LEAN_ANGLE = 90.0f;     // Max lean angle on gauge (degrees)
    static constexpr float ARC_START_ANGLE = -90.0f;   // Left end of arc (degrees, 0 = up)
    static constexpr float ARC_END_ANGLE = 90.0f;      // Right end of arc (degrees, 0 = up)
    static constexpr int ARC_SEGMENTS = 30;            // Number of segments for smooth arc

    // Steer bar constants
    static constexpr float MAX_STEER_ANGLE = 30.0f;    // Max steer angle on bar (degrees, approximate)

    // Lean smoothing (simulates physical inertia of analog gauge)
    static constexpr float LEAN_SMOOTH_FACTOR = 0.2f;  // 0.0-1.0: lower = smoother, higher = faster response
    float m_smoothedLean = 0.0f;  // Current smoothed lean value for display

    // Maximum lean angle tracking (left is negative, right is positive)
    float m_maxLeanLeft = 0.0f;   // Maximum lean angle to the left (stored as positive value)
    float m_maxLeanRight = 0.0f;  // Maximum lean angle to the right (stored as positive value)

    // Max marker tracking - shows when value starts decreasing, hides when increasing
    int m_maxFramesRemaining[2] = {0, 0};
    float m_markerValueLeft = 0.0f;   // Marker position for left
    float m_markerValueRight = 0.0f;  // Marker position for right
    float m_prevLeanLeft = 0.0f;      // Previous frame's left lean value
    float m_prevLeanRight = 0.0f;     // Previous frame's right lean value

    // Crash recovery detection - reset max lean when recovering from crash
    bool m_wasCrashed = false;

    // Spectator tracking - reset max values when switching viewed rider
    int m_lastDisplayedRaceNum = -1;

    // Frozen values when crashed (display freezes during crash)
    float m_frozenSteer = 0.0f;

    // Steer max marker tracking (index 0 = left, 1 = right)
    int m_steerFramesRemaining[2] = {0, 0};
    float m_steerMarkerLeft = 0.0f;   // Max steer left (stored as positive)
    float m_steerMarkerRight = 0.0f;  // Max steer right (stored as positive)

    // Arc fill color (background uses dynamic ColorConfig::getMuted())
    unsigned long m_arcFillColor = DEFAULT_ARC_FILL_COLOR;
};
