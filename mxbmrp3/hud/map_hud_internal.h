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

// Helper to get shape index from filename (returns 1 if not found)
inline int getShapeIndexByFilename(const char* filename) {
    const auto& assetMgr = AssetManager::getInstance();
    int spriteIndex = assetMgr.getIconSpriteIndex(filename);
    if (spriteIndex <= 0) return 1;  // Fallback to first icon
    return spriteIndex - assetMgr.getFirstIconSpriteIndex() + 1;
}

}  // namespace map_hud_detail
