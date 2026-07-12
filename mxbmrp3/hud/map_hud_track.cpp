// ============================================================================
// mxbmrp3/hud/map_hud_track.cpp
// Track ribbon + start/direction/race/segment marker rendering
// (extracted verbatim from map_hud.cpp; no behavior change)
// ============================================================================

#include "map_hud.h"
#include "map_hud_internal.h"
#include "../core/plugin_data.h"
#include "../core/plugin_manager.h"
#include "../core/plugin_constants.h"
#include "../core/plugin_utils.h"
#include "../core/color_config.h"
#include "../core/ui_config.h"
#include "../core/asset_manager.h"
#include "../core/tracked_riders_manager.h"
#include "../diagnostics/logger.h"
#include <cmath>
#include <algorithm>
#include <unordered_map>

using namespace PluginConstants;
using namespace PluginConstants::Math;

using namespace map_hud_detail;

void MapHud::renderTrack(const RotationCache& rotation, unsigned long trackColor, float widthMultiplier,
                         float clipLeft, float clipTop, float clipRight, float clipBottom) {
    if (m_trackSegments.empty()) {
        return;
    }

    // Get dimensions for title offset
    auto dim = getScaledDimensions();
    float titleOffset = m_bShowTitle ? dim.lineHeightLarge : 0.0f;

    // Calculate base track width from track dimensions
    float trackWidth = m_maxX - m_minX;
    float trackHeight = m_maxY - m_minY;
    float baseWidthMeters = std::min(trackWidth, trackHeight) * TRACK_WIDTH_BASE_RATIO;
    // Apply user scale and clamp to reasonable bounds (1m - 30m)
    float effectiveWidthMeters = std::clamp(baseWidthMeters * m_fTrackWidthScale, 1.0f, 30.0f);

    // Track half-width in world coordinates (ribbon edge offset from centerline)
    float halfWidth = effectiveWidthMeters * 0.5f * widthMultiplier;

    // --- Spatial culling setup ---
    // Expand bounds by track width + some margin to ensure we don't clip visible track
    float cullMargin = effectiveWidthMeters * 2.0f;
    float cullMinX = m_minX - cullMargin;
    float cullMaxX = m_maxX + cullMargin;
    float cullMinY = m_minY - cullMargin;
    float cullMaxY = m_maxY + cullMargin;

    // Resolve LOD to ribbon subdivision spacing (meters per quad).
    // AUTO: adaptive — targets ~3-4 px between quads at typical 1080p viewport.
    //       m_fTrackScale converts world meters to normalized screen units
    //       and is already zoom-aware (overridden in zoom mode), so this
    //       gives correct density across zoom levels without extra fudge.
    // Fixed presets: predictable density in meters. Zoom mode reduces the
    //       configured spacing proportionally so close zoom stays smooth.
    const bool autoLod = (m_detail == Detail::AUTO);
    float lodSpacing;
    if (autoLod) {
        // Set conservatively so detail at close zoom and high-DPI displays
        // still looks clean; raise carefully if quad count becomes a problem
        // on a new track.
        constexpr float AUTO_TARGET_NORM_STEP = 0.002f;
        lodSpacing = AUTO_TARGET_NORM_STEP / std::max(m_fTrackScale, 1e-6f);
        // Clamp so degenerate scales don't produce extreme values.
        lodSpacing = std::clamp(lodSpacing, 0.5f, 32.0f);
    } else {
        // AUTO is handled above; only fixed presets reach this branch.
        lodSpacing = (m_detail == Detail::HIGH) ? 1.0f : 4.0f;
        if (m_bZoomEnabled) {
            lodSpacing = std::max(0.5f, lodSpacing * (m_fZoomDistance / MAX_ZOOM_DISTANCE));
        }
    }
    // Subdivision floor: AUTO allows tiny segments to collapse to 1 quad
    // (they're sub-pixel anyway). Fixed presets keep min=3 for consistency.
    const int curveMinSteps = autoLod ? 1 : 3;

    // (Re)build the view-independent world-space centerline for this LOD. This is
    // the expensive arc walk; it's cached across the per-frame rebuilds that
    // rotate/zoom trigger (see WorldRibbonPoint in the header) so those modes only
    // pay the cheap transform below, not a full re-tessellation.
    ensureWorldRibbon(lodSpacing, curveMinSteps);

    // Lambda to check if a point is within culling bounds
    auto isPointInBounds = [&](float x, float y) -> bool {
        return x >= cullMinX && x <= cullMaxX && y >= cullMinY && y <= cullMaxY;
    };

    // Lambda to check if a point is inside the clip region
    auto isPointInClip = [&](float x, float y) -> bool {
        return x >= clipLeft && x <= clipRight && y >= clipTop && y <= clipBottom;
    };

    // Lambda to check if quad centerline is inside clip region
    // Using centerline (not vertices) ensures outline and track clip at same position
    auto isQuadCenterlineInside = [&](float x0, float y0, float x1, float y1, float x2, float y2, float x3, float y3) -> bool {
        // Centerline points: midpoint of each edge pair (left-right)
        // Quad vertices: 0=prevLeft, 1=currLeft, 2=currRight, 3=prevRight
        float prevCenterX = (x0 + x3) * 0.5f;
        float prevCenterY = (y0 + y3) * 0.5f;
        float currCenterX = (x1 + x2) * 0.5f;
        float currCenterY = (y1 + y2) * 0.5f;
        return isPointInClip(prevCenterX, prevCenterY) && isPointInClip(currCenterX, currCenterY);
    };

    // Transform the cached world centerline into screen-space ribbon quads. The
    // per-frame work is: world-space cull (cheap bounds test) -> apply the pass
    // half-width to the cached unit perpendicular -> worldToScreen + HUD offset ->
    // clip -> emit. The world cull + clip reproduce the previous behavior exactly:
    // a point outside the (world) cull bounds breaks ribbon continuity (as the old
    // per-segment cull did), and a quad whose centerline falls outside the (screen)
    // clip rect is skipped while continuity is preserved (as createRibbonQuad did).
    float prevLeftX = 0.0f, prevLeftY = 0.0f, prevRightX = 0.0f, prevRightY = 0.0f;
    bool hasPrevPoint = false;
    for (const auto& p : m_worldRibbon) {
        // World-space cull: outside bounds breaks the ribbon (matches the old
        // per-segment cull; anything culled here maps outside the clip rect anyway).
        if (!isPointInBounds(p.cx, p.cy)) {
            hasPrevPoint = false;
            continue;
        }

        // Edge points = centerline +/- unit perpendicular * half-width (per pass).
        float leftX = p.cx + p.upx * halfWidth;
        float leftY = p.cy + p.upy * halfWidth;
        float rightX = p.cx - p.upx * halfWidth;
        float rightY = p.cy - p.upy * halfWidth;

        float screenLeftX, screenLeftY, screenRightX, screenRightY;
        worldToScreen(leftX, leftY, screenLeftX, screenLeftY, rotation);
        worldToScreen(rightX, rightY, screenRightX, screenRightY, rotation);
        screenLeftY += titleOffset;
        screenRightY += titleOffset;
        applyOffset(screenLeftX, screenLeftY);
        applyOffset(screenRightX, screenRightY);

        if (hasPrevPoint &&
            isQuadCenterlineInside(prevLeftX, prevLeftY, screenLeftX, screenLeftY,
                                   screenRightX, screenRightY, prevRightX, prevRightY)) {
            // Quad connecting previous edges to current (counter-clockwise to match engine)
            SPluginQuad_t quad;
            quad.m_aafPos[0][0] = prevLeftX;   quad.m_aafPos[0][1] = prevLeftY;
            quad.m_aafPos[1][0] = screenLeftX; quad.m_aafPos[1][1] = screenLeftY;
            quad.m_aafPos[2][0] = screenRightX; quad.m_aafPos[2][1] = screenRightY;
            quad.m_aafPos[3][0] = prevRightX;  quad.m_aafPos[3][1] = prevRightY;
            quad.m_iSprite = PluginConstants::SpriteIndex::SOLID_COLOR;
            quad.m_ulColor = trackColor;
            m_quads.push_back(quad);
        }

        // Continuity is preserved even when the quad was clipped (matches old behavior).
        prevLeftX = screenLeftX; prevLeftY = screenLeftY;
        prevRightX = screenRightX; prevRightY = screenRightY;
        hasPrevPoint = true;
    }
}

// Build the view-independent world-space ribbon centerline (center + unit
// perpendicular per sample) for the current track at the given LOD. This is the
// arc-walk half of the old renderTrack loop, lifted out and cached: it emits the
// SAME sequence of sample points the old loop generated (same subdivision, same
// exact advanceAlongArc positions, same per-point heading), but unconditionally
// (no view cull, no screen conversion) so the result depends only on the track
// shape and LOD. renderTrack() then culls + transforms these per frame.
void MapHud::ensureWorldRibbon(float lodSpacing, int curveMinSteps) {
    WorldRibbonKey key;
    key.detail = static_cast<int>(m_detail);
    key.zoomEnabled = m_bZoomEnabled;
    key.lodSpacing = lodSpacing;
    if (m_worldRibbonValid && key == m_worldRibbonKey) {
        return;  // Same track + LOD: reuse (the win in rotate/zoom)
    }

    m_worldRibbon.clear();
    if (m_trackSegments.empty()) {
        m_worldRibbonKey = key;
        m_worldRibbonValid = true;
        return;
    }
    m_worldRibbon.reserve(m_trackSegments.size() * 4);

    // Emit one centerline sample: store position + UNIT perpendicular (half-width is
    // applied per-frame per pass in renderTrack, so it isn't baked in here).
    auto emit = [&](float cx, float cy, float headingDeg) {
        float perpRad = (headingDeg + 90.0f) * DEG_TO_RAD;
        m_worldRibbon.push_back({ cx, cy, std::sin(perpRad), std::cos(perpRad) });
    };

    float currentX = m_trackSegments[0].startX;
    float currentY = m_trackSegments[0].startY;
    float currentAngle = m_trackSegments[0].angle;

    for (const auto& segment : m_trackSegments) {
        float startX = currentX;
        float startY = currentY;

        if (segment.type == TrackSegmentType::STRAIGHT) {
            float angleRad = currentAngle * DEG_TO_RAD;
            float dx = std::sin(angleRad) * segment.length;
            float dy = std::cos(angleRad) * segment.length;

            // 1 quad when not zoomed (optimal); subdivide with LOD when zoomed for
            // clean clipping at close range. Perpendicular is constant on a straight.
            int numSteps = 1;
            if (m_bZoomEnabled) {
                numSteps = std::max(1, static_cast<int>(segment.length / lodSpacing));
            }
            for (int i = 0; i <= numSteps; ++i) {
                float t = static_cast<float>(i) / numSteps;
                emit(startX + dx * t, startY + dy * t, currentAngle);
            }
            currentX += dx;
            currentY += dy;
        } else {
            // Curve: subdivide, placing each point with exact arc geometry so the
            // ribbon and the marker positions (centerlinePositionAt) agree.
            float segRadius = segment.radius;  // signed (positive = right, negative = left)
            float arcLength = segment.length;
            int numSteps = std::max(curveMinSteps, static_cast<int>(arcLength / lodSpacing));
            float stepLength = arcLength / numSteps;
            for (int i = 0; i <= numSteps; ++i) {
                float tempX = startX, tempY = startY, tempAngle = currentAngle;
                advanceAlongArc(tempX, tempY, tempAngle, segRadius, stepLength * i);
                emit(tempX, tempY, tempAngle);
            }
            advanceAlongArc(currentX, currentY, currentAngle, segRadius, arcLength);
        }
    }

    m_worldRibbonKey = key;
    m_worldRibbonValid = true;
}

void MapHud::renderStartMarker(const RotationCache& rotation,
                               float clipLeft, float clipTop, float clipRight, float clipBottom) {
    if (m_trackSegments.empty()) {
        return;
    }

    // Calculate effective track width (same formula as renderTrack)
    float trackWidth = m_maxX - m_minX;
    float trackHeight = m_maxY - m_minY;
    float baseWidthMeters = std::min(trackWidth, trackHeight) * TRACK_WIDTH_BASE_RATIO;
    float effectiveWidthMeters = std::clamp(baseWidthMeters * m_fTrackWidthScale, 1.0f, 30.0f);

    // Get start position. Prefer the actual S/F line from raceData (resolved on
    // track update); fall back to segment[0] (the data start) if unavailable
    // (older game build, or N/A in raceData).
    float startX, startY, startAngle;
    if (m_raceMarkers[MARKER_SF].valid) {
        startX = m_raceMarkers[MARKER_SF].worldX;
        startY = m_raceMarkers[MARKER_SF].worldY;
        startAngle = m_raceMarkers[MARKER_SF].angleDeg;
    } else {
        startX = m_trackSegments[0].startX;
        startY = m_trackSegments[0].startY;
        startAngle = m_trackSegments[0].angle;
    }

    // Cull if start marker is outside current bounds (with margin for marker size)
    float cullMargin = effectiveWidthMeters;
    if (startX < m_minX - cullMargin || startX > m_maxX + cullMargin ||
        startY < m_minY - cullMargin || startY > m_maxY + cullMargin) {
        return;
    }

    // Get dimensions for title offset
    auto dim = getScaledDimensions();
    float titleOffset = m_bShowTitle ? dim.lineHeightLarge : 0.0f;

    // Draw triangle quad at track start pointing in direction.
    // Base spans the outline-edge width (wider than the track fill) for visibility,
    // and the tip is scaled the same way so the triangle keeps its proportions.
    float forwardAngleRad = startAngle * DEG_TO_RAD;
    float baseHalfWidth = effectiveWidthMeters * 0.5f * OUTLINE_WIDTH_MULTIPLIER;
    float pointLength   = effectiveWidthMeters * 0.5f * OUTLINE_WIDTH_MULTIPLIER;
    float pointX = startX + std::sin(forwardAngleRad) * pointLength;
    float pointY = startY + std::cos(forwardAngleRad) * pointLength;

    // Base endpoints (perpendicular to track at start)
    float perpAngle = startAngle + 90.0f;
    float perpAngleRad = perpAngle * DEG_TO_RAD;

    float baseLeftX = startX + std::sin(perpAngleRad) * baseHalfWidth;
    float baseLeftY = startY + std::cos(perpAngleRad) * baseHalfWidth;
    float baseRightX = startX - std::sin(perpAngleRad) * baseHalfWidth;
    float baseRightY = startY - std::cos(perpAngleRad) * baseHalfWidth;

    // Convert to screen coordinates
    float screenPointX, screenPointY;
    worldToScreen(pointX, pointY, screenPointX, screenPointY, rotation);
    screenPointY += titleOffset;

    float screenBaseLeftX, screenBaseLeftY;
    worldToScreen(baseLeftX, baseLeftY, screenBaseLeftX, screenBaseLeftY, rotation);
    screenBaseLeftY += titleOffset;

    float screenBaseRightX, screenBaseRightY;
    worldToScreen(baseRightX, baseRightY, screenBaseRightX, screenBaseRightY, rotation);
    screenBaseRightY += titleOffset;

    // Apply HUD offset
    applyOffset(screenPointX, screenPointY);
    applyOffset(screenBaseLeftX, screenBaseLeftY);
    applyOffset(screenBaseRightX, screenBaseRightY);

    // Skip if any vertex is outside clip bounds (clean cutoff, no distortion)
    auto isPointInClip = [&](float x, float y) -> bool {
        return x >= clipLeft && x <= clipRight && y >= clipTop && y <= clipBottom;
    };
    if (!isPointInClip(screenPointX, screenPointY) ||
        !isPointInClip(screenBaseLeftX, screenBaseLeftY) ||
        !isPointInClip(screenBaseRightX, screenBaseRightY)) {
        return;
    }

    // Create triangle quad (duplicate one vertex to make 4 points)
    SPluginQuad_t triangle;
    triangle.m_aafPos[0][0] = screenPointX;       // Point
    triangle.m_aafPos[0][1] = screenPointY;
    triangle.m_aafPos[1][0] = screenBaseRightX;   // Base right
    triangle.m_aafPos[1][1] = screenBaseRightY;
    triangle.m_aafPos[2][0] = screenBaseLeftX;    // Base left
    triangle.m_aafPos[2][1] = screenBaseLeftY;
    triangle.m_aafPos[3][0] = screenBaseLeftX;    // Duplicate base left to complete quad
    triangle.m_aafPos[3][1] = screenBaseLeftY;

    triangle.m_iSprite = PluginConstants::SpriteIndex::SOLID_COLOR;
    triangle.m_ulColor = this->getColor(ColorSlot::PRIMARY);  // White start/finish indicator
    m_quads.push_back(triangle);
}

void MapHud::drawDirectionMarker(const RaceMarker& marker, unsigned long color,
                                 const RotationCache& rotation,
                                 float clipLeft, float clipTop, float clipRight, float clipBottom) {
    if (!marker.valid) return;

    // Effective track width (matches renderTrack and renderStartMarker)
    float trackWidth = m_maxX - m_minX;
    float trackHeight = m_maxY - m_minY;
    float baseWidthMeters = std::min(trackWidth, trackHeight) * TRACK_WIDTH_BASE_RATIO;
    float effectiveWidthMeters = std::clamp(baseWidthMeters * m_fTrackWidthScale, 1.0f, 30.0f);

    // Triangle dimensions (matches renderStartMarker). Base sits at the marker
    // position; the point extends forward in travel direction.
    float baseHalfWidth = effectiveWidthMeters * 0.5f * OUTLINE_WIDTH_MULTIPLIER;
    float pointLength   = effectiveWidthMeters * 0.5f * OUTLINE_WIDTH_MULTIPLIER;
    float cullMargin = effectiveWidthMeters;

    auto dim = getScaledDimensions();
    float titleOffset = m_bShowTitle ? dim.lineHeightLarge : 0.0f;

    // Cull if marker is outside current bounds
    if (marker.worldX < m_minX - cullMargin || marker.worldX > m_maxX + cullMargin ||
        marker.worldY < m_minY - cullMargin || marker.worldY > m_maxY + cullMargin) {
        return;
    }

    // Triangle vertices: base sits at the marker position, point extends forward.
    float fwdRad = marker.angleDeg * DEG_TO_RAD;
    float perpRad = (marker.angleDeg + 90.0f) * DEG_TO_RAD;

    float pointX = marker.worldX + std::sin(fwdRad) * pointLength;
    float pointY = marker.worldY + std::cos(fwdRad) * pointLength;
    float baseLeftX  = marker.worldX + std::sin(perpRad) * baseHalfWidth;
    float baseLeftY  = marker.worldY + std::cos(perpRad) * baseHalfWidth;
    float baseRightX = marker.worldX - std::sin(perpRad) * baseHalfWidth;
    float baseRightY = marker.worldY - std::cos(perpRad) * baseHalfWidth;

    // Convert to screen coordinates
    float sPointX, sPointY, sLeftX, sLeftY, sRightX, sRightY;
    worldToScreen(pointX, pointY, sPointX, sPointY, rotation);
    worldToScreen(baseLeftX, baseLeftY, sLeftX, sLeftY, rotation);
    worldToScreen(baseRightX, baseRightY, sRightX, sRightY, rotation);
    sPointY += titleOffset;
    sLeftY  += titleOffset;
    sRightY += titleOffset;
    applyOffset(sPointX, sPointY);
    applyOffset(sLeftX,  sLeftY);
    applyOffset(sRightX, sRightY);

    // Skip if any vertex outside clip bounds (clean cutoff, no distortion)
    auto isPointInClip = [&](float x, float y) -> bool {
        return x >= clipLeft && x <= clipRight && y >= clipTop && y <= clipBottom;
    };
    if (!isPointInClip(sPointX, sPointY) ||
        !isPointInClip(sLeftX, sLeftY) ||
        !isPointInClip(sRightX, sRightY)) {
        return;
    }

    // Triangle as a quad with one duplicated vertex (matches renderStartMarker)
    SPluginQuad_t triangle;
    triangle.m_aafPos[0][0] = sPointX;   triangle.m_aafPos[0][1] = sPointY;
    triangle.m_aafPos[1][0] = sRightX;   triangle.m_aafPos[1][1] = sRightY;
    triangle.m_aafPos[2][0] = sLeftX;    triangle.m_aafPos[2][1] = sLeftY;
    triangle.m_aafPos[3][0] = sLeftX;    triangle.m_aafPos[3][1] = sLeftY;
    triangle.m_iSprite = PluginConstants::SpriteIndex::SOLID_COLOR;
    triangle.m_ulColor = color;
    m_quads.push_back(triangle);
}

void MapHud::renderRaceMarkers(const RotationCache& rotation,
                               float clipLeft, float clipTop, float clipRight, float clipBottom) {
    if (!m_bHasTrackData || m_trackSegments.empty()) {
        return;
    }

    unsigned long splitColor = this->getColor(ColorSlot::POSITIVE);   // green for splits
    unsigned long holeshotColor = this->getColor(ColorSlot::NEUTRAL);  // yellow for holeshot
                                                                       // (accent is reserved for
                                                                       // the player's own segments)

    drawDirectionMarker(m_raceMarkers[MARKER_SPLIT_1], splitColor, rotation, clipLeft, clipTop, clipRight, clipBottom);
    drawDirectionMarker(m_raceMarkers[MARKER_SPLIT_2], splitColor, rotation, clipLeft, clipTop, clipRight, clipBottom);
    drawDirectionMarker(m_raceMarkers[MARKER_HOLESHOT], holeshotColor, rotation, clipLeft, clipTop, clipRight, clipBottom);
}

void MapHud::renderSegmentMarkers(const RotationCache& rotation,
                                  float clipLeft, float clipTop, float clipRight, float clipBottom) {
    if (!m_bHasTrackData || m_trackSegments.empty()) {
        return;
    }

    // Custom segment-timer boundary points (training tool). Unlike the fixed race
    // markers, these are placed live via hotkey, so resolve them at render time.
    // Each point's trackPos (0-1) is S/F-relative (0 at start/finish), but
    // centerlinePositionAt measures meters from the centerline's data start. Map
    // back by adding the S/F offset (raceData[0]) and wrapping: meters =
    // (pos*totalLength + sfMeters) mod totalLength.
    const PluginData::SegmentTimerData& seg = PluginData::getInstance().getSegmentTimer();
    if (seg.points.empty()) {
        return;
    }

    float totalLength = 0.0f;
    for (const auto& s : m_trackSegments) totalLength += s.length;
    if (totalLength <= 0.0f) return;

    float sfOffset = (m_sfMeters > 0.0f) ? m_sfMeters : 0.0f;
    unsigned long segColor = this->getColor(ColorSlot::ACCENT);  // accent = the player's own segments
    for (float pos : seg.points) {
        float meters = std::fmod(pos * totalLength + sfOffset, totalLength);
        if (meters < 0.0f) meters += totalLength;
        RaceMarker m;
        if (centerlinePositionAt(meters, m.worldX, m.worldY, m.angleDeg)) {
            m.valid = true;
            drawDirectionMarker(m, segColor, rotation, clipLeft, clipTop, clipRight, clipBottom);
        }
    }
}
