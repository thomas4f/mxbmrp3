// ============================================================================
// hud/compass_widget.h
// Compass widget - shows the bike's heading (degrees from north) as a dial.
// Classic style (default): a fixed north-up ring with a red/white needle whose
// red half points to the heading. Modern style: a rotating card with the heading
// at the top under a fixed index and a numeric readout in the center.
// ============================================================================
#pragma once

#include "base_hud.h"
#include "../core/plugin_data.h"
#include "../core/plugin_constants.h"
#include "../core/widget_constants.h"
#include "../game/unified_types.h"

class CompassWidget : public BaseHud {
public:
    CompassWidget();
    virtual ~CompassWidget() = default;

    void update() override;
    bool handlesDataType(DataChangeType dataType) const override;
    void resetToDefaults();

    // Fed directly from HudManager on every RaceTrackPosition batch (same fast
    // path MapHud/RadarHud use). Heading isn't cached in PluginData, and the
    // change-notification system isn't driven for track positions, so the
    // compass takes the displayed rider's yaw straight from the position array.
    void updateRiderPositions(int numVehicles, const Unified::TrackPositionData* positions);

    // Dial style:
    //   Classic - fixed north-up cardinal ring (N at top) with a red/white needle whose
    //             red half points to the player's heading. Face north -> red points up
    //             at N; turn right -> red swings right toward E. The default.
    //   Modern  - rotating card: the cardinal ring spins so the current heading sits
    //             at the top under a fixed index tick.
    enum class Style { Classic, Modern };

    // Public for settings access (INI-only)
    Style m_style = Style::Classic;

protected:
    void rebuildLayout() override;

private:
    void rebuildRenderData() override;
    void resetTracking();  // Snap smoothing to the current target (session/spectate change)

    // FMX-style arc segment renderer — builds connected inner/outer-edge quads
    // so opacity is honored (same as LeanWidget/GForceWidget rings).
    void addArcSegment(float centerX, float centerY, float innerRadius, float outerRadius,
                       float startAngleRad, float endAngleRad, unsigned long color, int numSegments);

    // One half of the classic magnetic needle: a triangle with its base centered on
    // the pivot and its apex at the outer tip. Two opposite halves meet exactly at the
    // pivot with no overlap behind it (unlike BaseHud::addNeedleQuad, whose base sits
    // behind center).
    void addNeedleHalf(float centerX, float centerY, float angleRad,
                       float length, float baseHalfWidth, unsigned long color);

    // Ring geometry pinned to the GForceWidget donut so the compass shares the exact
    // gauge-widget footprint (8 chars wide, 3 content rows).
    static constexpr int RING_SEGMENTS = 30;           // Match GForceWidget ring
    static constexpr float ARC_MID_RADIUS_BASE = 0.035f;
    static constexpr float ARC_THICKNESS_BASE = 0.006f;
    // 0.0-1.0: lower = smoother, higher = faster response. Heading is a slower,
    // smoother signal than accel, so a gentle factor reads well.
    static constexpr float YAW_SMOOTH_FACTOR = 0.20f;

    // Heading state. m_targetYaw is the latest sample (degrees from north,
    // normalized 0..360); m_smoothedYaw eases toward it (shortest path).
    float m_targetYaw = 0.0f;
    float m_smoothedYaw = 0.0f;
    bool m_hasHeading = false;  // True once a heading has been received this session

    // Session and spectate tracking - snap heading on change (avoid spinning the dial)
    int m_cachedSessionGeneration = -1;
    int m_lastDisplayedRaceNum = -1;
};
