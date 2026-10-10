// ============================================================================
// hud/map_hud_view.cpp
// The Map's view transforms beyond the flat map:
//
//  - TILT (Map > Tilt, zoomed only): the map laid on the ground and seen
//    from above and behind, the rider below the centre looking ahead. It is a
//    per-point step at the end of worldToScreen, so the track, markers and
//    riders all follow it and the output is still the same flat quads the
//    engine draws.
//  - ADAPTIVE RANGE (Map > Adaptive range, zoomed only): the window Follow
//    shows grows with speed. It only changes the zoom bounds, which every
//    Follow rebuild recomputes anyway (they move with the rider).
//  - EDGE FADE (zoom): what runs off the map fades out over the last stretch
//    inside it instead of popping. The engine has one colour per quad, so the
//    fade is per quad - the ribbon's quads are a few pixels long, so it reads
//    as smooth.
// ============================================================================
#include "map_hud.h"
#include "digit_roll.h"
#include "../core/plugin_constants.h"
#include "../core/plugin_data.h"

#include <algorithm>
#include <cmath>

// The tilted map: a ground point g half-heights below the centre (toward the
// viewer) lands at y = g cos(TILT) k, and its offset across scales by
// k = VIEW_DIST / (VIEW_DIST - g sin(TILT)): farther is smaller and closer.
namespace {
constexpr float VIEW_DIST = 3.0f;   // map half-heights
// The rider (the zoom centre) drops below centre as the map lays down: this
// many half-heights at 40 degrees, in proportion to sin(tilt), so a slight
// tilt stays close to the flat map's centred rider.
constexpr float REF_DROP = 0.3f;
constexpr float REF_TILT_SIN = 0.6428f;   // sin(40 degrees)
// The ground worked out for the view is grown by this much, so rounding never
// lets the track reach the edge of the map before its cull does.
constexpr float VIEW_MARGIN = 1.03f;
// Reference pixels per normalized unit, for a fade distance that looks the same
// on both axes.
constexpr float RW = 1920.0f, RH = 1080.0f;
}  // namespace

void MapHud::tiltPoint(float& screenX, float& screenY) const {
    const float cx = m_fBaseMapWidth * m_fScale * 0.5f;
    const float cy = m_fBaseMapHeight * m_fScale * 0.5f;
    if (cy <= 0.0f) return;
    const float g = std::min((screenY - cy) / cy, m_tilt.gMax);
    const float k = VIEW_DIST / (VIEW_DIST - g * m_tilt.sin);
    const float y = g * m_tilt.cos * k;
    screenX = cx + (screenX - cx) * k;
    screenY = cy + (y + m_tilt.drop) * cy;
}

// The projection for m_tiltMode, and the ground it shows: solving
// y = g cos k for g gives g = y D / (cos D + y sin), which at the map's top
// edge (y = -1 - drop) is the ground AHEAD, at its bottom edge (y = 1 - drop)
// the ground BEHIND, and 1 / k there ACROSS (the top corners, the widest).
void MapHud::setTiltMode(int degrees) {
    m_tiltMode = degrees;
    if (m_tilt.deg == m_tiltMode) return;
    m_tilt.deg = m_tiltMode;
    const float rad = static_cast<float>(m_tiltMode) * PluginConstants::Math::DEG_TO_RAD;
    const float s = std::sin(rad), c = std::cos(rad);
    m_tilt.sin = s;
    m_tilt.cos = c;
    m_tilt.drop = REF_DROP * s / REF_TILT_SIN;
    // Far behind the viewer the projection flips over the horizon; clamping the
    // ground offset well short of that keeps those points below the map, where
    // they are clipped, instead of wrapping to the top.
    m_tilt.gMax = s > 0.0f ? 0.8f * VIEW_DIST / s : 1e9f;
    const float yTop = -1.0f - m_tilt.drop, yBottom = 1.0f - m_tilt.drop;
    const float ahead = -yTop * VIEW_DIST / (c * VIEW_DIST + yTop * s);
    const float behind = yBottom * VIEW_DIST / (c * VIEW_DIST + yBottom * s);
    m_tilt.ahead = ahead * VIEW_MARGIN;
    m_tilt.behind = behind * VIEW_MARGIN;
    m_tilt.across = (VIEW_DIST + ahead * s) / VIEW_DIST * VIEW_MARGIN;
}

// The tilt turns directions too: a heading drawn flat at angle a points along
// (sin a, -cos a) in square screen units. Projecting a short step along it and
// measuring the result gives the heading the tilted track actually runs.
void MapHud::tiltHeading(float flatX, float flatY, float& cosYaw, float& sinYaw) const {
    using PluginConstants::UI_ASPECT_RATIO;
    constexpr float STEP = 0.005f;
    float ax = flatX, ay = flatY;
    float bx = flatX + STEP * sinYaw / UI_ASPECT_RATIO, by = flatY - STEP * cosYaw;
    tiltPoint(ax, ay);
    tiltPoint(bx, by);
    const float dx = (bx - ax) * UI_ASPECT_RATIO, dy = by - ay;
    const float len = std::sqrt(dx * dx + dy * dy);
    if (len < 1e-7f) return;   // past gMax both ends clamp; keep the flat heading
    sinYaw = dx / len;
    cosYaw = -dy / len;
}

// The range this rebuild draws. The speed is the display rider's (the player's,
// or the spectated rider's), the clock Motion's, so a test can step it. A gap
// longer than the ease (the map hidden, a pause) snaps instead of easing in.
void MapHud::updateRangeNow() {
    if (!m_bAdaptiveRange) {
        m_fRangeNow = m_fZoomDistance;
        m_rangeStampUs = -1;
        return;
    }
    const float speed = PluginData::getInstance().getBikeTelemetry().speedometer;
    float t = (speed - ADAPTIVE_RANGE_SLOW) / (ADAPTIVE_RANGE_FAST - ADAPTIVE_RANGE_SLOW);
    t = std::clamp(t, 0.0f, 1.0f);
    t = t * t * (3.0f - 2.0f * t);
    // Never past the largest Range: a window the setting itself can't reach
    // would also draw more track than any setting does.
    const float target = std::min(m_fZoomDistance * (1.0f + (ADAPTIVE_RANGE_MAX - 1.0f) * t),
                                  MAX_ZOOM_DISTANCE);
    const long long now = DigitRoll::nowUs();
    const long long dt = now - m_rangeStampUs;
    m_rangeStampUs = now;
    if (m_fRangeNow <= 0.0f || dt <= 0 || dt > ADAPTIVE_RANGE_EASE_US) {
        m_fRangeNow = target;
        return;
    }
    const float k = 1.0f - std::exp(-static_cast<float>(dt) / static_cast<float>(ADAPTIVE_RANGE_EASE_US / 3));
    m_fRangeNow += (target - m_fRangeNow) * k;
}

void MapHud::viewCullRect(const RotationCache& rotation, float margin,
                          float& minX, float& minY, float& maxX, float& maxY) const {
    if (m_tiltMode == 0) {
        minX = m_minX - margin; maxX = m_maxX + margin;
        minY = m_minY - margin; maxY = m_maxY + margin;
        return;
    }
    // The view's footprint (across, ahead) turned into world space: screen-up
    // is world (sin, cos) of the rotation, screen-right (cos, -sin); unrotated,
    // +Y and +X. The larger half-span stands in for both axes, which only errs
    // wide.
    const float cx = (m_minX + m_maxX) * 0.5f, cy = (m_minY + m_maxY) * 0.5f;
    const float h = std::max(m_maxX - m_minX, m_maxY - m_minY) * 0.5f;
    const float s = rotation.hasRotation ? rotation.sinAngle : 0.0f;
    const float c = rotation.hasRotation ? rotation.cosAngle : 1.0f;
    minX = maxX = cx;
    minY = maxY = cy;
    for (const float a : { -m_tilt.across, m_tilt.across }) {
        for (const float b : { -m_tilt.behind, m_tilt.ahead }) {
            const float x = cx + (a * c + b * s) * h;
            const float y = cy + (b * c - a * s) * h;
            minX = std::min(minX, x); maxX = std::max(maxX, x);
            minY = std::min(minY, y); maxY = std::max(maxY, y);
        }
    }
    minX -= margin; maxX += margin;
    minY -= margin; maxY += margin;
}

// 0 outside the clip rect, rising to 1 over the last stretch inside it (a
// smoothstep, so the fade has no visible start), 1 everywhere while the map is
// not zoomed. (x, y) are post-offset coordinates, as the clip rect is.
float MapHud::edgeFade(float x, float y) const {
    if (!m_fadeEdges) return 1.0f;
    const float cl = m_fadeClip[0] * RW, ct = m_fadeClip[1] * RH;
    const float cr = m_fadeClip[2] * RW, cb = m_fadeClip[3] * RH;
    const float px = x * RW, py = y * RH;
    const float d = std::min(std::min(px - cl, cr - px), std::min(py - ct, cb - py));
    if (d <= 0.0f) return 0.0f;
    const float fadePx = std::clamp(0.14f * std::min(cr - cl, cb - ct), 10.0f, 48.0f);
    const float t = std::min(1.0f, d / fadePx);
    return t * t * (3.0f - 2.0f * t);
}
