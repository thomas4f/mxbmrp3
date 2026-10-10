// ============================================================================
// hud/map_hud_internal.h
// Shared internal helpers for the MapHud translation units (map_hud*.cpp).
// Extracted verbatim from map_hud.cpp when it was split into focused TUs; the
// values and logic are unchanged. Header-inline (was file-local `static` in the
// single TU) so every MapHud TU sees one definition without ODR conflicts.
// ============================================================================
#pragma once

#include "../core/asset_manager.h"
#include "../core/plugin_constants.h"
#include <cmath>

namespace map_hud_detail {

// Track width is calculated as a percentage of the smaller track dimension
// This ensures consistent visual appearance across different track sizes
inline constexpr float TRACK_WIDTH_BASE_RATIO = 0.036f;  // 3.6% of smaller dimension

// Track outline width as a multiplier of the fill width (1.4 = 40% wider).
// Also used to size race-data marker triangles (S/F, splits, holeshot) so
// their base spans the outline edges, not the fill edges - much more visible
// against the white outline.
inline constexpr float OUTLINE_WIDTH_MULTIPLIER = 1.4f;

// Default icon filename
inline constexpr const char* DEFAULT_RIDER_ICON = "circle-chevron-up";

// Advance (x, y, headingDeg) by `dist` meters along a circular arc of signed
// radius (the heading convention is move = sin/cos of heading, turn rate =
// 1/radius). This is the *exact* arc position - independent of how finely the
// curve is subdivided - so the rendered ribbon (renderTrack) and the marker
// positions that index into it (centerlinePositionAt) agree exactly. Reduces to a
// straight line as |radius| grows. Previously both used forward-Euler stepping
// with different step counts, so markers drifted off the ribbon through curves.
inline void advanceAlongArc(float& x, float& y, float& headingDeg, float radius, float dist) {
    using namespace PluginConstants::Math;
    float h0 = headingDeg * DEG_TO_RAD;
    if (std::abs(radius) < 0.01f) {  // effectively straight
        x += std::sin(h0) * dist;
        y += std::cos(h0) * dist;
        return;
    }
    float theta = dist / radius;  // signed turn (radians)
    x += radius * (std::cos(h0) - std::cos(h0 + theta));
    y += radius * (std::sin(h0 + theta) - std::sin(h0));
    headingDeg += theta * RAD_TO_DEG;
}

// A colour with its own opacity scaled by `fade` (0..1): the zoomed map's edge
// fade (MapHud::edgeFade). fade 1 returns the colour unchanged.
inline unsigned long fadeAlpha(unsigned long color, float fade) {
    if (fade >= 1.0f) return color;
    const float a = static_cast<float>((color >> 24) & 0xFF) * std::fmax(fade, 0.0f);
    return (color & 0x00FFFFFFul) | (static_cast<unsigned long>(a + 0.5f) << 24);
}

// Liang-Barsky: the part [t0, t1] of the segment (x0,y0)-(x1,y1) inside the
// rect, or false when none of it is. The zoomed map cuts its ribbon with it, so
// the track ends exactly at the map's edge instead of a whole quad at a time.
inline bool clipSegment(float x0, float y0, float x1, float y1,
                        float left, float top, float right, float bottom,
                        float& t0, float& t1) {
    t0 = 0.0f;
    t1 = 1.0f;
    const float dx = x1 - x0, dy = y1 - y0;
    const float p[4] = { -dx, dx, -dy, dy };
    const float q[4] = { x0 - left, right - x0, y0 - top, bottom - y0 };
    for (int k = 0; k < 4; ++k) {
        if (p[k] == 0.0f) {
            if (q[k] < 0.0f) return false;   // parallel to this edge and outside it
            continue;
        }
        const float r = q[k] / p[k];
        if (p[k] < 0.0f) t0 = std::fmax(t0, r);
        else t1 = std::fmin(t1, r);
        if (t0 > t1) return false;
    }
    return true;
}

// Helper to get shape index from filename (returns 1 if not found)
inline int getShapeIndexByFilename(const char* filename) {
    const auto& assetMgr = AssetManager::getInstance();
    // BASE index: this maps a name to its position in the vocabulary, which a theme
    // override must not move (see AssetManager::getBaseIconSpriteIndex).
    int spriteIndex = assetMgr.getBaseIconSpriteIndex(filename);
    if (spriteIndex <= 0) return 1;  // Fallback to first icon
    return assetMgr.shapeIndexForSprite(spriteIndex);
}

}  // namespace map_hud_detail
