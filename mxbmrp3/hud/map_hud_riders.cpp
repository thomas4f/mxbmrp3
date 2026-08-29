// ============================================================================
// mxbmrp3/hud/map_hud_riders.cpp
// Rider icon/label rendering
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
#include <limits>
#include <unordered_map>

using namespace PluginConstants;
using namespace PluginConstants::Math;

using namespace map_hud_detail;

void MapHud::renderRiders(const RotationCache& rotation,
                          float clipLeft, float clipTop, float clipRight, float clipBottom) {
    if (m_riderPositions.empty() || !m_bHasTrackData) {
        return;
    }

    // Helper to check if a point is inside the clip region
    auto isPointInClip = [&](float x, float y) -> bool {
        return x >= clipLeft && x <= clipRight && y >= clipTop && y <= clipBottom;
    };

    auto dim = getScaledDimensions();

    // Scale cone size by HUD scale factor
    constexpr float baseConeSize = 0.006f;
    float scaledConeSize = baseConeSize * m_fScale * m_fMarkerScale;

    // Get plugin data to access rider names/numbers
    const PluginData& pluginData = PluginData::getInstance();
    int displayRaceNum = pluginData.getDisplayRaceNum();

    // Helper lambda to render a single rider (used for both passes)
    auto renderRider = [&](const Unified::TrackPositionData& pos, bool isLocalPlayer) {
        // Get rider entry data
        const RaceEntryData* entry = pluginData.getRaceEntry(pos.raceNum);
        if (!entry) {
            return;  // Skip if we don't have race entry data
        }

        // For the active player with rotation mode enabled, use cached position if crashed
        // to keep screen position stable. But use actual yaw so arrow can spin in place.
        float renderX = pos.posX;
        float renderZ = pos.posZ;
        if (isLocalPlayer && pos.crashed && m_bRotateToPlayer) {
            renderX = m_fLastPlayerX;
            renderZ = m_fLastPlayerZ;
        }

        float renderYaw = pos.yaw;  // Always use current yaw for arrow direction

        // Convert world coordinates to screen coordinates
        // Use X and Z for ground plane (Y is altitude, not used for top-down map)
        float screenX, screenY;
        worldToScreen(renderX, renderZ, screenX, screenY, rotation);
        screenX += m_fContentDX;
        screenY += m_fContentDY;

        unsigned long riderColor;

        // Check if rider is tracked - tracked riders use their configured color with position modulation
        const TrackedRidersManager& trackedMgr = TrackedRidersManager::getInstance();
        const TrackedRiderConfig* trackedConfig = trackedMgr.getTrackedRider(entry->name);

        // Tracked riders use their own icon index, non-tracked use global shape
        // trackedSpriteIndex: -1 = use global shape, otherwise direct sprite index
        int trackedSpriteIndex = -1;
        if (trackedConfig) {
            // Convert shapeIndex to sprite index (dynamically assigned)
            trackedSpriteIndex = AssetManager::getInstance().iconSpriteForShape(trackedConfig->shapeIndex);
        }

        if (trackedConfig) {
            // Tracked rider - use their configured color with position-based modulation
            unsigned long baseColor = trackedConfig->color;

            // Apply position-based color modulation (lighten if ahead by laps, darken if behind by laps)
            // Only in race sessions where lap position matters
            if (pluginData.isRaceSession()) {
                const StandingsData* playerStanding = pluginData.getStanding(displayRaceNum);
                const StandingsData* riderStanding = pluginData.getStanding(pos.raceNum);
                int playerLaps = playerStanding ? playerStanding->numLaps : 0;
                int riderLaps = riderStanding ? riderStanding->numLaps : 0;
                int lapDiff = riderLaps - playerLaps;

                if (lapDiff >= 1) {
                    // Rider is ahead by laps - lighten color
                    riderColor = PluginUtils::lightenColor(baseColor, 0.4f);
                } else if (lapDiff <= -1) {
                    // Rider is behind by laps - darken color
                    riderColor = PluginUtils::darkenColor(baseColor, 0.6f);
                } else {
                    // Same lap - use base color
                    riderColor = baseColor;
                }
            } else {
                // Non-race session - use base color without modulation
                riderColor = baseColor;
            }
        } else if (isLocalPlayer) {
            // Accent is reserved for the player in every mode except Brand, where all
            // riders (player included) show their bike brand color.
            if (m_riderColorMode == RiderColorMode::BRAND) {
                riderColor = entry->bikeBrandColor;
            } else {
                riderColor = this->getColor(ColorSlot::ACCENT);
            }
        } else if (m_riderColorMode == RiderColorMode::RELATIVE_POS) {
            // Relative position coloring - only meaningful in race sessions
            if (pluginData.isRaceSession()) {
                const StandingsData* playerStanding = pluginData.getStanding(displayRaceNum);
                const StandingsData* riderStanding = pluginData.getStanding(pos.raceNum);
                int playerPosition = pluginData.getDisplayPositionForRaceNum(displayRaceNum);
                int riderPosition = pluginData.getDisplayPositionForRaceNum(pos.raceNum);
                int playerLaps = playerStanding ? playerStanding->numLaps : 0;
                int riderLaps = riderStanding ? riderStanding->numLaps : 0;

                riderColor = PluginUtils::getRelativePositionColor(
                    playerPosition, riderPosition, playerLaps, riderLaps,
                    this->getColor(ColorSlot::NEUTRAL),
                    this->getColor(ColorSlot::WARNING),
                    this->getColor(ColorSlot::TERTIARY));
            } else {
                // Non-race: positions are meaningless, use uniform color
                riderColor = this->getColor(ColorSlot::NEUTRAL);
            }
        } else if (m_riderColorMode == RiderColorMode::BRAND) {
            // Brand colors at full opacity
            riderColor = entry->bikeBrandColor;
        } else {
            // Uniform: other riders use the primary color (matching their name color in
            // the standings); accent is reserved for the player.
            riderColor = this->getColor(ColorSlot::PRIMARY);
        }

        // The local player's own marker (and its label below) render a touch larger than
        // the pack so it's easy to pick out at a glance — the accent colour alone can be
        // hard to spot on a busy map.
        const float playerBoost = MarkerLabel::boost(isLocalPlayer);

        // Render sprite quad centered on rider position, rotated to match heading
        float spriteHalfSize = scaledConeSize * playerBoost;

        // All icons use uniform baseline scale

        // OFF THE MAP. Everyone else is dropped; the local player is CLAMPED to the
        // edge instead (below, once the icon's rotation is known).
        //
        // The player rides off the map when they crash off-track or get lost, which is
        // exactly when the map is worth looking at -- and dropping the icon then leaves
        // the one panel that could point them back showing nothing at all. Clamped, the
        // icon says which side of the map they are on and, because it keeps its
        // heading, which way they are currently facing. Between the two they can turn
        // themselves around.
        //
        // Not a setting: it only ever fires in a state that is already abnormal, and
        // the alternative it replaces shows strictly less. Zoom mode never reaches it
        // (the view follows the player, so the player is always centred).
        float centerXClip = screenX, centerYClip = screenY;
        applyOffset(centerXClip, centerYClip);
        const bool offMap = !isPointInClip(centerXClip, centerYClip);
        if (offMap && !isLocalPlayer) {
            return;
        }

        // Determine sprite index and shape index for rotation check
        int spriteIndex;
        int shapeIndex;
        if (trackedSpriteIndex >= 0) {
            // Tracked rider - use their assigned icon
            spriteIndex = trackedSpriteIndex;
            shapeIndex = AssetManager::getInstance().shapeIndexForSprite(spriteIndex);
        } else {
            // Non-tracked rider - use global shape (0=OFF defaults to circle for local player)
            shapeIndex = (m_riderShapeIndex > 0) ? m_riderShapeIndex : getShapeIndexByFilename(DEFAULT_RIDER_ICON);
            spriteIndex = AssetManager::getInstance().iconSpriteForShape(shapeIndex);
        }

        // Hazard/flag icon overrides — skip for the local player so their
        // own icon and color always stay consistent on the map.
        if (!isLocalPlayer) {
            HazardType hazardType = pluginData.getRiderHazardType(pos.raceNum);
            if (hazardType != HazardType::None) {
                m_iconCache.ensureInitialized();
                if (hazardType == HazardType::WrongWay) {
                    if (m_iconCache.circleExclamation > 0) {
                        spriteIndex = m_iconCache.circleExclamation;
                        riderColor = ColorPalette::RED;
                    }
                } else {
                    if (m_iconCache.flag > 0) {
                        spriteIndex = m_iconCache.flag;
                        riderColor = ColorPalette::BRIGHT_YELLOW;
                    }
                }
                shapeIndex = AssetManager::getInstance().shapeIndexForSprite(spriteIndex);
            }

            if (hazardType == HazardType::None && pluginData.isRiderBlueFlagged(pos.raceNum)) {
                m_iconCache.ensureInitialized();
                if (m_iconCache.flag > 0) {
                    spriteIndex = m_iconCache.flag;
                    shapeIndex = AssetManager::getInstance().shapeIndexForSprite(spriteIndex);
                    riderColor = ColorPalette::BLUE;
                }
            } else if (hazardType == HazardType::None) {
                const StandingsData* standing = pluginData.getStanding(pos.raceNum);
                if (standing && pluginData.getSessionData().isRiderFinished(standing->numLaps, standing->numLapsAtLeaderFinish)) {
                    m_iconCache.ensureInitialized();
                    if (m_iconCache.flagCheckered > 0) {
                        spriteIndex = m_iconCache.flagCheckered;
                        shapeIndex = AssetManager::getInstance().shapeIndexForSprite(spriteIndex);
                        riderColor = ColorPalette::WHITE;
                    }
                }
            }
        }

        // Calculate rotation only for directional icons
        float cosYaw = 1.0f;
        float sinYaw = 0.0f;
        if (TrackedRidersManager::shouldRotate(shapeIndex)) {
            float adjustedYaw = renderYaw - rotation.angle;
            float yawRad = adjustedYaw * DEG_TO_RAD;
            cosYaw = std::cos(yawRad);
            sinYaw = std::sin(yawRad);
        }

        // THE CLAMP, here rather than at the clip test above because the inset owed is
        // the ROTATED icon's half-extent, and cosYaw/sinYaw only exist by now. A square
        // of half-side h turned by yaw reaches h * (|cos| + |sin|) on each axis -- up
        // to 1.41h at 45 degrees -- so insetting by h alone would let a diagonal icon
        // hang out over the panel's frame.
        //
        // PER-AXIS clamping is what produces the sliding the icon needs to be useful:
        // off the left of the map, x pins to the left edge while y still tracks the
        // player up and down it, so the icon sits beside where they actually are rather
        // than collapsing into a corner. Both axes pin only when they are genuinely
        // past both.
        //
        // screenX/screenY are what everything downstream reads -- the sprite, the click
        // region, the name label -- so the write-back is through them, via the offset
        // delta measured at the clip test rather than a second applyOffset (which is
        // not guaranteed to be its own inverse).
        if (offMap) {
            // The offset delta measured at the clip test above, so the clamp can be
            // done in clip space (where the bounds live) and written back to the draw
            // coordinates -- rather than a second applyOffset, which is not guaranteed
            // to be its own inverse.
            const float offsetDX = centerXClip - screenX;
            const float offsetDY = centerYClip - screenY;
            clampMarkerToClip(centerXClip, centerYClip, spriteHalfSize, cosYaw, sinYaw,
                              clipLeft, clipTop, clipRight, clipBottom);
            screenX = centerXClip - offsetDX;
            screenY = centerYClip - offsetDY;
        }

        // Render rider sprite (outline baked into sprite asset)
        addRotatedSpriteQuad(screenX, screenY, spriteHalfSize, cosYaw, sinYaw,
                             spriteIndex, riderColor);

        // Add click region for this rider (for spectator switching). Gated like every other
        // surface: the map draws a marker for anyone with a track position, which includes
        // riders who have retired or pulled into the garage but are still on the last
        // reported spot — clicking those did nothing at all. The marker still renders; only
        // the click target is withheld.
        if (pluginData.isRiderSpectatable(pos.raceNum)) {
            // Click region is a rectangle centered on the sprite, with offset applied
            RiderClickRegion clickRegion;
            float clickWidth = spriteHalfSize * 2.0f / UI_ASPECT_RATIO;  // Account for aspect ratio
            float clickHeight = spriteHalfSize * 2.0f;
            clickRegion.x = screenX - spriteHalfSize / UI_ASPECT_RATIO + m_fOffsetX;
            clickRegion.y = screenY - spriteHalfSize + m_fOffsetY;
            clickRegion.width = clickWidth;
            clickRegion.height = clickHeight;
            clickRegion.raceNum = pos.raceNum;
            m_riderClickRegions.push_back(clickRegion);
        }

        // Render label centered on arrow based on label mode
        if (m_labelMode != LabelMode::NONE) {
            // Scale font size by marker scale (and the local-player boost, so the player's
            // label grows in step with their enlarged marker).
            const float labelFontSize = dim.fontSizeSmall * m_fMarkerScale * playerBoost;

            // Where the label sits relative to the icon: shared with RadarHud and
            // GapBarHud (marker_label.h), which is where the anchor arithmetic, the
            // gap ratio and the side-anchor centring all live now.
            const MarkerLabel::Placement lp = MarkerLabel::place(
                m_labelAnchor, screenX, screenY, spriteHalfSize, labelFontSize);
            const float labelX = lp.x;
            const float labelY = lp.y;
            const int labelJustify = lp.justify;

            char labelStr[20];  // Sized for "P100" (5) + "#999" (5) = "P100#999" (9 + null)
            int position = pluginData.getDisplayPositionForRaceNum(pos.raceNum);

            if (MarkerLabel::format(m_labelMode, position, pos.raceNum,
                                    labelStr, sizeof(labelStr))) {
                // Podium colors for position labels (P1/P2/P3)
                unsigned long labelColor =
                    MarkerLabel::color(m_labelMode, position, this->getColor(ColorSlot::PRIMARY));

                // Render the label with the standard drop shadow (single bottom-right
                // offset from [Display] dropShadowOffsetX/Y, honoring the global
                // toggle) — same convention as every other HUD text, applied by
                // HudManager::collectRenderData for non-skipped strings. (Map rider
                // ICONS stay separate sprite quads with their own baked outlines and
                // never take the drop shadow — this is only the text label.)
                addString(labelStr, labelX, labelY, labelJustify,
                         this->getFont(FontCategory::SMALL), labelColor, labelFontSize, false);
            }
        }
    };

    // First pass: render all other riders (not local player)
    for (const auto& pos : m_riderPositions) {
        if (pos.raceNum == displayRaceNum) continue;  // Skip player, render last

        // Skip non-tracked riders if global shape is OFF (0)
        // Tracked riders always render with their own shape
        if (m_riderShapeIndex == 0) {
            const RaceEntryData* entry = pluginData.getRaceEntry(pos.raceNum);
            if (!entry || !TrackedRidersManager::getInstance().isTracked(entry->name)) {
                continue;
            }
        }

        renderRider(pos, false);
    }

    // Second pass: render local player LAST (always on top)
    for (const auto& pos : m_riderPositions) {
        if (pos.raceNum == displayRaceNum) {
            renderRider(pos, true);
            break;  // Found and rendered player, done
        }
    }
}

// ============================================================================
// clampMarkerToClip / renderOffTrackPointer — the two halves of "the map still
// tells you something when you are off it".
//
// They answer OPPOSITE failures of the same situation, one per view mode:
//   * FULL-TRACK view fits the whole circuit, so the track is always on screen
//     and the PLAYER is what leaves it -> clamp the player (renderRiders above).
//   * ZOOM view centres on the player (calculateZoomBounds), so the player can
//     never leave and the TRACK is what leaves -> point at the track (below).
// Neither covers the other's case, which is why both exist.
// ============================================================================
bool MapHud::clampMarkerToClip(float& x, float& y, float halfSize,
                               float cosYaw, float sinYaw,
                               float clipLeft, float clipTop,
                               float clipRight, float clipBottom) const {
    // The inset owed is the ROTATED square's half-extent: a square of half-side h
    // turned by yaw reaches h * (|cos| + |sin|) on each axis, up to 1.41h at 45
    // degrees. Insetting by h alone lets a diagonal icon hang over the frame.
    const float rotatedHalf = halfSize * (std::fabs(cosYaw) + std::fabs(sinYaw));
    // X is squashed by the aspect at draw time (addRotatedSpriteQuad divides the
    // rotated offset by UI_ASPECT_RATIO), so its inset is squashed to match.
    const float insetX = rotatedHalf / PluginConstants::UI_ASPECT_RATIO;
    const float insetY = rotatedHalf;

    const float loX = clipLeft + insetX, hiX = clipRight - insetX;
    const float loY = clipTop + insetY, hiY = clipBottom - insetY;
    // A map area narrower than the icon gives an empty range; centre in it rather
    // than letting the clamp bounds cross (std::clamp with lo > hi is UB).
    const float cx = (loX <= hiX) ? std::clamp(x, loX, hiX)
                                  : (clipLeft + clipRight) * 0.5f;
    const float cy = (loY <= hiY) ? std::clamp(y, loY, hiY)
                                  : (clipTop + clipBottom) * 0.5f;
    const bool moved = (cx != x) || (cy != y);
    x = cx;
    y = cy;
    return moved;
}

void MapHud::renderOffTrackPointer(const RotationCache& rotation,
                                   float clipLeft, float clipTop,
                                   float clipRight, float clipBottom) {
    // ZOOM ONLY. In the full-track view the circuit is fitted to the panel by
    // construction, so a pointer at the edge would either never fire or fire on a
    // rounding error -- and the player clamp already covers that mode.
    if (!m_bZoomEnabled || !m_bHasTrackData || m_worldRibbon.empty()) return;

    // The player's world position, on the same terms renderRiders uses: the cached
    // one while crashed, so a pointer does not jitter with a tumbling bike.
    const PluginData& pluginData = PluginData::getInstance();
    const int displayRaceNum = pluginData.getDisplayRaceNum();
    float playerX = 0.0f, playerZ = 0.0f;
    bool found = false;
    for (const auto& pos : m_riderPositions) {
        if (pos.raceNum != displayRaceNum) continue;
        if (pos.crashed) { playerX = m_fLastPlayerX; playerZ = m_fLastPlayerZ; }
        else             { playerX = pos.posX;       playerZ = pos.posZ; }
        found = true;
        break;
    }
    if (!found) return;

    // NEAREST POINT ON THE CENTERLINE, in three steps: a coarse probe for the right
    // stretch of track, hysteresis so that choice does not flip, and a projection
    // onto the actual segment so the answer is not quantised to a vertex.
    //
    // WHY ALL THREE. Riding alongside the track but off it is where this pointer is
    // most used and where the naive version visibly jittered, from two independent
    // causes:
    //
    //   * VERTEX QUANTISATION. Taking the nearest SAMPLE means that running parallel
    //     to a straight, the winner changes as you pass each vertex and the bearing
    //     sawtooths by about atan(half-spacing / distance) -- a couple of degrees at
    //     30m out, fifteen or more up close, and it snaps back each time the next
    //     vertex takes over. Projecting onto the segment removes it outright: the
    //     perpendicular foot slides along with you and the bearing is continuous.
    //
    //   * LOBE FLIPPING. The coarse probe samples every `stride`th vertex, so when
    //     two stretches of track are at similar distance -- the bit ahead and the
    //     bit behind, or a parallel section -- the winner can change lobe from one
    //     frame to the next and the arrow jumps somewhere else entirely. An earlier
    //     comment here dismissed that as costing nothing "because a lobe of track is
    //     still track". That is true of finding your way back and false of how it
    //     reads; it was reported as jitter. Hysteresis keeps last frame's stretch
    //     unless a challenger is properly closer.
    //
    // COARSE-THEN-REFINE, not a full scan. The ribbon runs to ~1300 samples at high
    // detail and a full scan of it MEASURED at 3.8us per frame -- most of this
    // function's cost, and nearly twice the whole rider phase (run_perf.sh: zoom
    // riders 2.1us with the pointer off, 2.3us with the pointer but no scan, 6.1us
    // with the scan). It is only paid while the player is off-view, but "the map
    // gets more expensive the moment you are lost" is precisely backwards.
    //
    // The ribbon is a closed loop, so the refine window wraps.
    const size_t n = m_worldRibbon.size();
    auto d2At = [&](size_t i) {
        const float dx = m_worldRibbon[i].cx - playerX;
        const float dy = m_worldRibbon[i].cy - playerZ;
        return dx * dx + dy * dy;
    };
    constexpr size_t COARSE_PROBES = 64;
    const size_t stride = (n > COARSE_PROBES) ? (n / COARSE_PROBES) : 1;
    size_t best = 0;
    float bestD2 = std::numeric_limits<float>::max();
    for (size_t i = 0; i < n; i += stride) {
        const float d2 = d2At(i);
        if (d2 < bestD2) { bestD2 = d2; best = i; }
    }
    if (stride > 1) {
        for (size_t k = 1; k <= stride; ++k) {
            const size_t lo = (best + n - (k % n)) % n;
            const size_t hi = (best + k) % n;
            float d2 = d2At(lo);
            if (d2 < bestD2) { bestD2 = d2; best = lo; }
            d2 = d2At(hi);
            if (d2 < bestD2) { bestD2 = d2; best = hi; }
        }
    }

    // HYSTERESIS. Re-refine around LAST frame's answer -- the coarse grid may not
    // even contain it -- and keep it unless the new winner is meaningfully closer.
    //
    // The margin is on squared distance, so 0.7 is about 16% closer in real terms:
    // loose enough that genuinely rounding a corner onto a nearer stretch still
    // switches, tight enough that two lobes trading places by centimetres do not.
    // Without the local re-refine this would compare a projected-onto answer against
    // a coarse one and switch almost every frame.
    if (m_pointerLastValid && m_pointerLastSample < n) {
        size_t prev = m_pointerLastSample;
        float prevD2 = d2At(prev);
        for (size_t k = 1; k <= stride; ++k) {
            const size_t lo = (prev + n - (k % n)) % n;
            const size_t hi = (prev + k) % n;
            float d2 = d2At(lo);
            if (d2 < prevD2) { prevD2 = d2; prev = lo; }
            d2 = d2At(hi);
            if (d2 < prevD2) { prevD2 = d2; prev = hi; }
        }
        constexpr float SWITCH_MARGIN = 0.7f;
        if (bestD2 > prevD2 * SWITCH_MARGIN) {
            best = prev;
            bestD2 = prevD2;
        }
    }
    m_pointerLastSample = best;
    m_pointerLastValid = true;

    // PROJECT onto the two segments meeting at the winning vertex, clamped to each,
    // and take the nearer foot. Clamped because the true nearest point can be the
    // vertex itself (the outside of a corner), and an unclamped projection would run
    // off the end of the segment and point at nothing.
    float aimX = m_worldRibbon[best].cx;
    float aimY = m_worldRibbon[best].cy;
    {
        float footD2 = bestD2;
        const size_t prevIdx = (best + n - 1) % n;
        const size_t nextIdx = (best + 1) % n;
        for (const size_t other : { prevIdx, nextIdx }) {
            const float ax = m_worldRibbon[best].cx,  ay = m_worldRibbon[best].cy;
            const float bx = m_worldRibbon[other].cx, by = m_worldRibbon[other].cy;
            const float ex = bx - ax, ey = by - ay;
            const float len2 = ex * ex + ey * ey;
            if (len2 <= 1e-9f) continue;            // duplicate samples; nothing to project onto
            float t = ((playerX - ax) * ex + (playerZ - ay) * ey) / len2;
            t = (t < 0.0f) ? 0.0f : (t > 1.0f ? 1.0f : t);
            const float fx = ax + ex * t, fy = ay + ey * t;
            const float dx = fx - playerX, dy = fy - playerZ;
            const float d2 = dx * dx + dy * dy;
            if (d2 < footD2) { footD2 = d2; aimX = fx; aimY = fy; }
        }
    }

    // TWO SPACES, and mixing them up is the trap. worldToScreen + m_fContentD* gives
    // DRAW coordinates, which is what addRotatedSpriteQuad wants -- it applies the
    // HUD offset itself, per corner. The clip bounds are in POST-offset space. So the
    // rect test and the clamp happen on an offset COPY, and the result is converted
    // back through the same delta before drawing. Clamping in clip space and handing
    // that straight to addRotatedSpriteQuad offsets it twice, which put the arrow at
    // x=1.7 on a panel ending at 0.99 -- off the screen entirely, which is a
    // remarkable way for a "look over here" marker to fail.
    float drawX, drawY;
    worldToScreen(aimX, aimY, drawX, drawY, rotation);
    drawX += m_fContentDX;
    drawY += m_fContentDY;
    float clipX = drawX, clipY = drawY;
    applyOffset(clipX, clipY);
    const float offsetDX = clipX - drawX;
    const float offsetDY = clipY - drawY;

    // ON SCREEN -> nothing to point at. This is the normal case every frame the
    // player is anywhere near the track, and it costs one rect test.
    if (clipX >= clipLeft && clipX <= clipRight &&
        clipY >= clipTop  && clipY <= clipBottom) {
        return;
    }

    m_iconCache.ensureInitialized();
    int spriteIndex = m_iconCache.angleUp;
    if (spriteIndex <= 0) {
        // DEGRADED INSTALL (an icon set without the chevron, or none discovered at
        // all): fall back to whatever the rider markers resolve and draw anyway,
        // rather than returning. The rider path emits its quad whatever the lookup
        // gives back, so a pointer that silently vanished because one .tga is
        // missing would be the odd one out -- and the failure it hides ("the map
        // stopped telling me where the track is") is worse than the wrong glyph.
        //
        // This is also the path the headless tests take: run_tests.sh stages no
        // assets, so an early return here made the whole feature untestable, which
        // is how the fallback came to be written.
        spriteIndex = AssetManager::getInstance().iconSpriteForShape(
            getShapeIndexByFilename(DEFAULT_RIDER_ICON));
    }

    // The direction from the MAP'S CENTRE, not from the player's icon: in zoom mode
    // those are the same point, and the centre is the one that stays true if that
    // ever stops being so.
    const float centreX = (clipLeft + clipRight) * 0.5f;
    const float centreY = (clipTop + clipBottom) * 0.5f;
    // Undo the aspect squash before taking the angle, or the arrow points wrong
    // everywhere except the four axes -- screen X is compressed relative to Y.
    const float dirX = (clipX - centreX) * PluginConstants::UI_ASPECT_RATIO;
    const float dirY = clipY - centreY;
    if (dirX == 0.0f && dirY == 0.0f) return;

    // The chevron points UP unrotated, so a heading of 0 is -Y. atan2(dirX, -dirY)
    // is that convention: the same one the rider markers' yaw uses, which is what
    // lets this share addRotatedSpriteQuad with them.
    const float angle = std::atan2(dirX, -dirY);
    const float cosYaw = std::cos(angle);
    const float sinYaw = std::sin(angle);

    const float halfSize = 0.006f * m_fScale * m_fMarkerScale;

    // A RING AROUND THE PLAYER, not the panel's edge.
    //
    // The edge is where this started, and on a 390px map it put the arrow ~240px
    // from the rider -- almost the furthest away it could be, reading as an
    // unrelated marker parked in a corner rather than as "the track is THIS way
    // from you". The direction is the whole message, and a direction is read from
    // something close enough to the subject to be seen with it.
    //
    // The radius is DERIVED from what it has to clear, not chosen: the player's own
    // boosted icon, the label gap under it, the label's own line, and this arrow's
    // half-size -- each from the same constants those things are drawn with, so a
    // change to marker scale, HUD scale or the label font moves the ring with them
    // instead of leaving it overlapping or adrift. RING_CLEARANCE is the only taste
    // in it: one extra arrow-width of air so the two never read as one glyph.
    const auto dim = getScaledDimensions();
    constexpr float RING_CLEARANCE = 1.0f;
    const float playerHalf = 0.006f * m_fScale * m_fMarkerScale * MarkerLabel::PLAYER_BOOST;
    const float labelFont  = dim.fontSizeSmall * m_fMarkerScale * MarkerLabel::PLAYER_BOOST;
    const float ringRadius = playerHalf * (1.0f + MarkerLabel::GAP_RATIO)
                           + labelFont
                           + halfSize * (1.0f + RING_CLEARANCE);

    // dirX/dirY are in UNIFORM space (the aspect was undone above to take the
    // angle), so the ring is a circle there and the x component is squashed back on
    // the way out -- exactly what addRotatedSpriteQuad does to the sprite itself.
    const float dirLen = std::sqrt(dirX * dirX + dirY * dirY);
    clipX = centreX + (dirX / dirLen) * ringRadius / PluginConstants::UI_ASPECT_RATIO;
    clipY = centreY + (dirY / dirLen) * ringRadius;

    // Still clamped, for the map small enough that the ring does not fit inside it:
    // the arrow gives up its distance from the player before it gives up being on
    // the panel at all.
    clampMarkerToClip(clipX, clipY, halfSize, cosYaw, sinYaw,
                      clipLeft, clipTop, clipRight, clipBottom);

    // Drawn in SECONDARY, deliberately not the accent the player's own icon owns:
    // an arrow on the edge of the map must not read as another rider parked there.
    addRotatedSpriteQuad(clipX - offsetDX, clipY - offsetDY, halfSize, cosYaw, sinYaw,
                         spriteIndex, this->getColor(ColorSlot::SECONDARY));
}
