// ============================================================================
// hud/wheelie_arc.h
// The Wheelie widget's protractor arithmetic, header-only so a unit test can pin
// it (the blue_flag_detect.h pattern: BaseHud-derived code cannot link into a
// unit TU).
//
// The widget draws a quarter ring pivoted on its content box's bottom-left
// corner, stretched sideways to fill a box that is wider than tall (see
// wheelie_widget.h). On a circle the fill for `degrees` would simply end at
// 90 - degrees; on the stretched ring that point lies at a different angle from
// the pivot, so the end is solved on the ring instead, and the fill's edge stays
// on the bike's real angle off the ground.
// ============================================================================
#pragma once

#include <algorithm>
#include <cmath>

namespace WheelieArc {

// Full scale: vertical. A protractor, so this is geometry, not a setting.
constexpr float MAX_ANGLE = 90.0f;

// The addArcSegment angle (radians, 0 = up, pi/2 = right) at which a ring with
// horizontal radius radiusX and vertical radius radiusY (same units) is crossed
// by the ray `degrees` above level from its centre. Clamped to [0, MAX_ANGLE].
inline float angleOnArc(float degrees, float radiusX, float radiusY) {
    constexpr float kDegToRad = 3.14159265358979f / 180.0f;
    const float a = std::clamp(degrees, 0.0f, MAX_ANGLE) * kDegToRad;
    if (!(radiusX > 0.0f) || !(radiusY > 0.0f)) return 3.14159265358979f * 0.5f - a;
    // The ring point (sin t * rx, cos t * ry) lies on that ray when
    // sin t : cos t = cos a / rx : sin a / ry.
    return std::atan2(std::cos(a) / radiusX, std::sin(a) / radiusY);
}

}  // namespace WheelieArc
