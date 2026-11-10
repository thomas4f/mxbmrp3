// ============================================================================
// hud/wheelie_widget.h
// Wheelie widget - how far the bike is up on its back wheel, on a quarter arc
//
// THE ARC IS A PROTRACTOR. Its pivot is the content box's bottom-left corner: it
// starts level at the bottom right and sweeps up to vertical at the top left, and
// the fill's edge is the bike's own angle off the ground. So 45 degrees reads
// half way round, and the gauge needs no scale.
//
// IT FILLS ITS BOX. A small widget's content box (SMALL_WIDGET_WIDTH characters by
// SMALL_WIDGET_CONTENT_LINES rows) is wider than it is tall, so a circle would
// either leave a strip at the right or poke out of the top. The ring is instead
// stretched sideways by the difference (BaseHud::addArcSegment's stretchX), which
// keeps its thickness constant, and the fill's end is solved on that shape
// (wheelie_arc.h) so the protractor reading stays true.
//
// The angle is PluginData's wheelieAngle (core/wheelie_angle.h, the rule the
// rumble and the FMX trick detector share): 0 unless the front wheel is up and the
// rear one down, so a hill climb does not read as a wheelie. Player telemetry
// only, so spectating shows N/A like the G-force widget.
// ============================================================================
#pragma once

#include "base_hud.h"
#include "../core/plugin_data.h"
#include "../core/plugin_constants.h"
#include "../core/widget_constants.h"

class WheelieWidget : public BaseHud {
public:
    WheelieWidget();
    virtual ~WheelieWidget() = default;

    void update() override;
    bool handlesDataType(DataChangeType dataType) const override;
    void resetToDefaults();

    // Public for settings access
    bool m_bShowValue = true;          // The angle in degrees, in the arc's corner
    bool m_bShowMaxMarker = true;      // Lingering peak tick on the arc
    int m_maxMarkerLingerFrames = 60;  // ~frames, 60 = 1 second at 60fps

protected:
    void rebuildLayout() override;

private:
    void rebuildRenderData() override;
    void resetTracking();
    void updateTracking(const BikeTelemetryData& bikeData, bool live, bool isCrashed);

    static constexpr int ARC_SEGMENTS = 24;            // Full quarter; the fill scales down from it
    static constexpr float SMOOTH_FACTOR = 0.2f;       // Same inertia as the lean arc
    static constexpr float MARKER_THRESHOLD = 1.0f;    // Degrees of jitter the peak ignores
    static constexpr float MARKER_HALF_WIDTH = 0.02f;  // Radians either side of the peak

    float m_smoothed = 0.0f;           // Displayed angle (smoothed)
    float m_markerValue = 0.0f;        // Peak angle under the linger
    int m_markerFramesRemaining = 0;

    // The value text, formatted when the whole-degree reading changes
    int m_shownDegrees = 0;
    char m_valueText[8] = "0";

    // Crash handling mirrors LeanWidget: freeze while crashed, reset on recovery
    bool m_wasCrashed = false;
    int m_cachedSessionGeneration = -1;
    int m_lastDisplayedRaceNum = -1;
};
