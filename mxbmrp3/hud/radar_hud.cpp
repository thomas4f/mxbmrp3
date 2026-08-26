// ============================================================================
// hud/radar_hud.cpp
// Radar HUD implementation - displays a top-down view of nearby riders
// ============================================================================
#include "radar_hud.h"
#include "../core/plugin_data.h"
#include "../core/plugin_constants.h"
#include "../core/plugin_utils.h"
#include "../core/color_config.h"
#include "../core/asset_manager.h"
#include "../core/tracked_riders_manager.h"
#include "../diagnostics/logger.h"
#include <cmath>
#include <algorithm>

using namespace PluginConstants;
using namespace PluginConstants::Math;

// Default icon filenames
static constexpr const char* DEFAULT_RIDER_ICON = "circle";
static constexpr const char* DEFAULT_PROXIMITY_ARROW_ICON = "angle-up";

// SPRITE indices, and turning one back into a shape index goes through
// AssetManager::shapeIndexForSprite() -- never `sprite - firstIcon + 1`.
//
// getIconSpriteIndex() returns the active THEME's override when it has one, and an
// override sprite is registered PAST the base icon block, so the subtraction returns
// a number off the end of the vocabulary. renderRiderSprite() then range-rejects it
// and silently falls back to the ordinary rider marker -- while the colour override
// still applies. Under a theme shipping icons/flag.tga, a blue-flagged rider went blue
// and kept a circle instead of the flag.
//
// The arithmetic was correct until getIconSpriteIndex() gained theme overrides on this
// branch (the base-only lookup is getBaseIconSpriteIndex() now). asset_manager.h
// documents it as "THE TRAP THIS EXISTS FOR"; MapHud was already doing it right, and
// radar was the last site still open-coding it.
void RadarHud::CachedIcons::ensureInitialized() {
    if (initialized) return;
    const AssetManager& assets = AssetManager::getInstance();
    circleExclamation = assets.getIconSpriteIndex("circle-exclamation");
    flag = assets.getIconSpriteIndex("flag");
    flagCheckered = assets.getIconSpriteIndex("flag-checkered");
    initialized = true;
}

// Helper to get shape index from filename (returns 1 if not found)
static int getShapeIndexByFilename(const char* filename) {
    const auto& assetMgr = AssetManager::getInstance();
    int spriteIndex = assetMgr.getIconSpriteIndex(filename);
    if (spriteIndex <= 0) return 1;  // Fallback to first icon
    return assetMgr.shapeIndexForSprite(spriteIndex);
}

RadarHud::RadarHud()
    : m_fRadarRangeMeters(DEFAULT_RADAR_RANGE),
      m_riderColorMode(RiderColorMode::BRAND),
      m_radarMode(RadarMode::ON),
      m_proximityArrowMode(ProximityArrowMode::OFF),
      m_fAlertDistance(DEFAULT_ALERT_DISTANCE),
      m_labelMode(LabelMode::POSITION),
      m_riderShapeIndex(1),  // Will be set properly via settings or resetToDefaults
      m_proximityArrowShapeIndex(1),
      m_fProximityArrowScale(DEFAULT_PROXIMITY_ARROW_SCALE),
      m_proximityArrowColorMode(ProximityArrowColorMode::DISTANCE),
      m_fMarkerScale(DEFAULT_MARKER_SCALE) {
    // No caption on this panel -- see BaseHud::m_titleSupported.
    disableTitle();

    // One-time setup
    // The dial artwork IS this HUD -- see BaseHud::m_textureRequired.
    m_textureRequired = true;
    DEBUG_INFO("RadarHud created");
    setDraggable(true);
    // Body card: this HUD draws a content BLOCK under its title, which is what the
    // themed card frames. Opt-in; see BaseHud::m_bContentCard.
    m_bContentCard = true;
    m_riderPositions.reserve(GameLimits::MAX_CONNECTIONS);
    m_quads.reserve(RESERVE_QUADS);
    m_strings.reserve(RESERVE_STRINGS);

    // Set texture base name for dynamic texture discovery
    setTextureBaseName("radar_hud");

    // Set all configurable defaults
    resetToDefaults();
}

void RadarHud::update() {
    // OPTIMIZATION: Skip processing when not visible
    if (!isVisibleAnySurface()) {
        clearDataDirty();
        clearLayoutDirty();
        return;
    }

    // Handle dirty flags using base class helper
    processDirtyFlags();
}

bool RadarHud::handlesDataType(DataChangeType dataType) const {
    // Rebuild when standings change (for position labels)
    // Also rebuild when tracked riders change (color/shape)
    return dataType == DataChangeType::Standings ||
           dataType == DataChangeType::SpectateTarget ||
           dataType == DataChangeType::TrackedRiders;
}

void RadarHud::setRadarRange(float rangeMeters) {
    // Clamp to valid range
    if (rangeMeters < MIN_RADAR_RANGE) rangeMeters = MIN_RADAR_RANGE;
    if (rangeMeters > MAX_RADAR_RANGE) rangeMeters = MAX_RADAR_RANGE;

    if (m_fRadarRangeMeters != rangeMeters) {
        m_fRadarRangeMeters = rangeMeters;
        setDataDirty();
    }
}

void RadarHud::setAlertDistance(float meters) {
    // Clamp to valid range
    if (meters < MIN_ALERT_DISTANCE) meters = MIN_ALERT_DISTANCE;
    if (meters > MAX_ALERT_DISTANCE) meters = MAX_ALERT_DISTANCE;

    if (m_fAlertDistance != meters) {
        m_fAlertDistance = meters;
        setDataDirty();
    }
}

void RadarHud::setMarkerScale(float scale) {
    // Clamp to valid range
    if (scale < MIN_MARKER_SCALE) scale = MIN_MARKER_SCALE;
    if (scale > MAX_MARKER_SCALE) scale = MAX_MARKER_SCALE;

    if (m_fMarkerScale != scale) {
        m_fMarkerScale = scale;
        setDataDirty();
    }
}

void RadarHud::setRiderShape(int shapeIndex) {
    // Clamp to valid range (1 to icon count)
    int maxShape = static_cast<int>(AssetManager::getInstance().getIconCount());
    if (shapeIndex < 1) shapeIndex = 1;
    if (shapeIndex > maxShape) shapeIndex = maxShape;

    if (m_riderShapeIndex != shapeIndex) {
        m_riderShapeIndex = shapeIndex;
        setDataDirty();
    }
}

void RadarHud::setProximityArrowShape(int shapeIndex) {
    // Clamp to valid range (1 to icon count)
    int maxShape = static_cast<int>(AssetManager::getInstance().getIconCount());
    if (shapeIndex < 1) shapeIndex = 1;
    if (shapeIndex > maxShape) shapeIndex = maxShape;

    if (m_proximityArrowShapeIndex != shapeIndex) {
        m_proximityArrowShapeIndex = shapeIndex;
        setDataDirty();
    }
}

void RadarHud::setProximityArrowScale(float scale) {
    // Clamp to valid range
    if (scale < MIN_PROXIMITY_ARROW_SCALE) scale = MIN_PROXIMITY_ARROW_SCALE;
    if (scale > MAX_PROXIMITY_ARROW_SCALE) scale = MAX_PROXIMITY_ARROW_SCALE;

    if (m_fProximityArrowScale != scale) {
        m_fProximityArrowScale = scale;
        setDataDirty();
    }
}

void RadarHud::updateRiderPositions(int numVehicles, const Unified::TrackPositionData* positions) {
    if (numVehicles <= 0 || positions == nullptr) {
        m_riderPositions.clear();
        return;
    }

    // Copy rider positions (fast operation - runs at high frequency)
    m_riderPositions.assign(positions, positions + numVehicles);

    // Mark data as dirty to trigger render update
    setDataDirty();
}

void RadarHud::renderRiderSprite(float radarX, float radarY, float yaw, unsigned long color,
                                   float centerX, float centerY, float radarRadius,
                                   int shapeOverride) {
    // Scale sprite size by HUD scale factor and marker scale
    constexpr float baseConeSize = 0.006f;
    float scaledConeSize = baseConeSize * m_fScale * m_fMarkerScale;
    float spriteHalfSize = scaledConeSize;

    // Determine effective shape (use override if provided, otherwise use global setting)
    // shapeOverride uses TrackedRidersManager values: 1-N for all rider icons
    // m_riderShape uses RadarHud values: 0-9 for the 10 selectable icons
    const int iconCount = static_cast<int>(AssetManager::getInstance().getIconCount());
    // Use override shape if provided, otherwise use the configured rider shape
    int effectiveShape;
    if (shapeOverride >= 1 && shapeOverride <= iconCount) {
        effectiveShape = shapeOverride;
    } else {
        effectiveShape = m_riderShapeIndex;
    }

    // All icons use uniform baseline scale

    // Convert radar coordinates (-1 to 1) to screen coordinates
    float screenX = centerX + (radarX * radarRadius) / UI_ASPECT_RATIO;
    float screenY = centerY - radarY * radarRadius;

    // Calculate rotation only for directional icons
    float cosYaw = 1.0f;
    float sinYaw = 0.0f;
    if (TrackedRidersManager::shouldRotate(effectiveShape)) {
        float yawRad = yaw * DEG_TO_RAD;
        cosYaw = std::cos(yawRad);
        sinYaw = std::sin(yawRad);
    }

    // Determine sprite index - directly convert shape index to sprite index
    // shapeIndex 1-N maps to icon sprite indices (dynamically assigned), and the
    // active theme's override for that name if it has one.
    int spriteIndex = AssetManager::getInstance().iconSpriteForShape(effectiveShape);

    // Render rider sprite (outline baked into sprite asset)
    addRotatedSpriteQuad(screenX, screenY, spriteHalfSize, cosYaw, sinYaw,
                         spriteIndex, color);
}

void RadarHud::renderRiderLabel(float radarX, float radarY, int raceNum, int position,
                                  float centerX, float centerY, float radarRadius, float opacity) {
    if (m_labelMode == LabelMode::NONE) return;

    auto dim = getScaledDimensions();

    // Scale font size by marker scale
    float labelFontSize = dim.fontSizeSmall * m_fMarkerScale;

    // Calculate scaled icon size (must match renderRiderSprite)
    constexpr float baseConeSize = 0.006f;
    float scaledConeSize = baseConeSize * m_fScale * m_fMarkerScale;

    // Convert radar coordinates to screen coordinates
    float screenX = centerX + (radarX * radarRadius) / UI_ASPECT_RATIO;
    float screenY = centerY - radarY * radarRadius;

    // Where the label sits relative to the icon: shared with MapHud and GapBarHud
    // (marker_label.h). No player boost -- the radar never draws the player, whose
    // marker IS its centre, so there is no boosted label here to size.
    const MarkerLabel::Placement lp = MarkerLabel::place(
        m_labelAnchor, screenX, screenY, scaledConeSize, labelFontSize);

    char labelStr[20];
    if (MarkerLabel::format(m_labelMode, position, raceNum, labelStr, sizeof(labelStr))) {
        // Podium colors for position labels
        unsigned long labelColor =
            MarkerLabel::color(m_labelMode, position, this->getColor(ColorSlot::PRIMARY));

        // Apply opacity to colors to match sprite fading
        labelColor = PluginUtils::applyOpacity(labelColor, opacity);

        // THE STANDARD DROP SHADOW, exactly as MapHud and the gap bar draw the same
        // label: one string with skipShadow=false, and HudManager::collectRenderData
        // lays the shadow in behind it from [Display] dropShadowOffsetX/Y, honouring
        // the global toggle and any per-HUD override.
        //
        // This used to hand-roll an outline -- the string four more times in black at
        // +-5% of the font -- gated on the same toggle so it at least switched with it,
        // but drawing a four-way surround where the other two draw a bottom-right
        // shadow, at five strings per label against their two.
        //
        // THE FADE IS WHY IT COULD NOT MOVE BEFORE: this label dims as its rider leaves
        // proximity range, the outline was faded to match by hand, and the shared
        // shadow used to write the configured colour verbatim -- a solid shadow behind
        // half-visible text. It modulates by the string's own alpha now
        // (PluginUtils::modulateAlpha), so the shadow fades with the label and the
        // outline has nothing left to do. (Rider ICONS are separate sprite quads with
        // their own baked outlines and never take the shadow -- this is only the text.)
        addString(labelStr, lp.x, lp.y, lp.justify,
                 this->getFont(FontCategory::SMALL), labelColor, labelFontSize, false);
    }
}

ProximityGradient RadarHud::buildProximityGradient() const {
    auto extractR = [](unsigned long v) { return static_cast<unsigned char>(v & 0xFF); };
    auto extractG = [](unsigned long v) { return static_cast<unsigned char>((v >> 8) & 0xFF); };
    auto extractB = [](unsigned long v) { return static_cast<unsigned char>((v >> 16) & 0xFF); };
    unsigned long c = getColor(ColorSlot::NEGATIVE);
    unsigned long m = getColor(ColorSlot::NEUTRAL);
    unsigned long f = getColor(ColorSlot::POSITIVE);
    return {
        extractR(c), extractG(c), extractB(c),
        extractR(m), extractG(m), extractB(m),
        extractR(f), extractG(f), extractB(f)
    };
}

void RadarHud::rebuildRenderData() {
    m_quads.clear();
    clearStrings();

    // Calculate dimensions
    auto dim = getScaledDimensions();

    float titleHeight = reservedTitleHeight(dim, TitleTier::Large);

    // Radar size based on screen height
    float radarDiameter = RADAR_SIZE * m_fScale;
    float radarRadius = radarDiameter * 0.5f;

    // Calculate width in screen coords (account for aspect ratio)
    float width = radarDiameter / UI_ASPECT_RATIO + dim.paddingH * 2;
    float height = radarDiameter + titleHeight + dim.paddingV * 2;

    float y = 0.0f;

    // The BOX lands on the lattice; the radar circle inside it does not change size.
    // Same treatment MapHud gets, and for the same reason -- both size themselves from
    // a diameter divided by UI_ASPECT_RATIO, which is the one thing in a panel's size
    // that has no reason to land on a cell boundary. See fitPanelToGrid.
    const GridFit fit = fitPanelToGrid(width, height);

    // CENTRE-ANCHORED, like the centre stack: half the DRAWN box to the left of the
    // stored centre, so the dial stays put as scale changes its width.
    float x = centerAnchoredPanelLeft(fit.w);

    // Set bounds for dragging
    setBounds(x, y, x + fit.w, y + fit.h);
    x += fit.padX;   // content re-centred in the snapped box; `width`/`height` below
    y += fit.padY;   // stay the CONTENT's size, so centerX/centerY still land right

    // Get plugin data and find local player (needed for opacity calculation)
    const PluginData& pluginData = PluginData::getInstance();
    int displayRaceNum = pluginData.getDisplayRaceNum();

    const Unified::TrackPositionData* localPlayer = nullptr;
    for (const auto& pos : m_riderPositions) {
        if (pos.raceNum == displayRaceNum) {
            localPlayer = &pos;
            break;
        }
    }

    // Pre-calculate player position for proximity arrows (needed even when radar is off)
    float playerX = localPlayer ? localPlayer->posX : 0.0f;
    float playerZ = localPlayer ? localPlayer->posZ : 0.0f;
    float cosYaw = 1.0f, sinYaw = 0.0f;
    if (localPlayer) {
        float yawRad = localPlayer->yaw * DEG_TO_RAD;
        cosYaw = std::cos(yawRad);
        sinYaw = std::sin(yawRad);
    }

    // Build proximity gradient once (shared by sector overlay and proximity arrows)
    const ProximityGradient gradient = buildProximityGradient();

    // If radar mode is OFF, skip radar rendering but still render proximity arrows
    if (m_radarMode == RadarMode::OFF) {
        // This path never reaches addBackgroundQuad, which is what ARMS the panel rect
        // and the fill strips. Without clearing them, finalizeThemedFill re-cuts using
        // last rebuild's indices -- which now hold proximity arrows -- and stretches one
        // across the old panel. NoticesHud's comment called itself "the one such caller";
        // this is the second.
        invalidatePanelRect();
        renderProximityArrows(localPlayer, playerX, playerZ, cosYaw, sinYaw, gradient);
        return;
    }

    // Pre-calculate max rider opacity for background fade (only if auto-hide is enabled).
    // The fade math is pure — unit-tested in tests/unit/test_radar_fade.cpp.
    float maxRiderOpacity = 1.0f;  // Default: fully visible
    if (m_radarMode == RadarMode::AUTO_HIDE && localPlayer) {
        maxRiderOpacity = RadarFade::maxRiderOpacity(
            m_riderPositions.empty() ? nullptr : m_riderPositions.data(),
            static_cast<int>(m_riderPositions.size()), displayRaceNum,
            playerX, playerZ, localPlayer->trackPos,
            m_fRadarRangeMeters, pluginData.getSessionData().trackLength);
    }

    // Add background (opacity scaled by max rider visibility when fade enabled)
    float savedOpacity = m_fBackgroundOpacity;
    m_fBackgroundOpacity = savedOpacity * maxRiderOpacity;
    addBackgroundQuad(x, y, width, height);
    m_fBackgroundOpacity = savedOpacity;

    // THE BODY CARD, asked for directly. This called addTitleString("RADAR", ...) to
    // get it, which was the legacy chain's only remaining purpose here: the radar
    // disableTitle()s in its constructor, so that call could only ever take the
    // title-hidden early-out -- emit the card, emit an empty string, return. The
    // empty string was the caller's to want and this HUD never did.
    //
    // The card still matters even though the radar's default is theme-less: the
    // opt-out is a per-HUD SETTING (setThemeOverride(THEME_NONE) at defaults), so a
    // player who names a theme for the radar gets a frame, and this is what fills it.
    emitContentCard(0.0f);

    // Calculate radar center position
    float centerX = x + width * 0.5f;
    float centerY = y + titleHeight + dim.paddingV + radarRadius;

    // Number of sectors for proximity highlighting (4 = front, right, back, left)
    constexpr int NUM_SECTORS = 4;

    // Track closest rider distance per section (for intensity-based highlighting)
    // Section angles (in radar space where 0° = forward/up, 90° each):
    // Section 0: 315°-45° (front)
    // Section 1: 45°-135° (right)
    // Section 2: 135°-225° (back)
    // Section 3: 225°-315° (left)
    float sectionClosestDist[NUM_SECTORS] = { -1.0f, -1.0f, -1.0f, -1.0f };

    // Track if any section has a rider about to lap the player (race mode only)
    // These riders are +1 lap ahead and approaching from behind
    bool sectionHasLapper[NUM_SECTORS] = { false, false, false, false };
    float sectionLapperDist[NUM_SECTORS] = { -1.0f, -1.0f, -1.0f, -1.0f };
    bool isRace = pluginData.isRaceSession();

    // Player position and rotation already calculated at start of function
    if (localPlayer) {
        // Hoist player standing lookup outside loop (same value every iteration)
        const StandingsData* playerStanding = isRace ? pluginData.getStanding(displayRaceNum) : nullptr;
        int playerLaps = playerStanding ? playerStanding->numLaps : 0;

        // First pass: calculate section distances for all riders
        for (const auto& pos : m_riderPositions) {
            if (pos.raceNum == displayRaceNum) continue;

            float relX = pos.posX - playerX;
            float relZ = pos.posZ - playerZ;

            // Rotate to radar space
            float rotatedX = relX * cosYaw - relZ * sinYaw;
            float rotatedZ = relX * sinYaw + relZ * cosYaw;

            float distance = std::sqrt(rotatedX * rotatedX + rotatedZ * rotatedZ);

            // Only track section distances for riders within alert distance
            if (distance > m_fAlertDistance) continue;

            // Filter by track distance (skip riders on parallel straights).
            // Distance only — which side of us they are on doesn't matter here.
            const float trackDist =
                trackSeparation(pos.trackPos, localPlayer->trackPos);

            float trackLength = pluginData.getSessionData().trackLength;
            if (trackLength > 0.0f) {
                float trackDistMeters = trackDist * trackLength;
                if (trackDistMeters > m_fAlertDistance) continue;
            } else {
                // Fallback: use 5% of track as threshold if track length unknown
                constexpr float FALLBACK_THRESHOLD = 0.05f;
                if (trackDist > FALLBACK_THRESHOLD) continue;
            }

            // Calculate angle in radar space (0° = forward, clockwise positive)
            // atan2(x, z) gives angle where 0 = forward (+Z), 90 = right (+X)
            float angle = std::atan2(rotatedX, rotatedZ);  // radians, -PI to PI

            // Convert to degrees and normalize to 0-360
            float angleDeg = angle * RAD_TO_DEG;
            if (angleDeg < 0) angleDeg += 360.0f;

            // Determine section (each section is 90°)
            // Section 0: 315-360 and 0-45 (front)
            // Section 1: 45-135 (right)
            // Section 2: 135-225 (back)
            // Section 3: 225-315 (left)
            int section;
            if (angleDeg >= 315.0f || angleDeg < 45.0f) {
                section = 0;  // Front
            } else if (angleDeg < 135.0f) {
                section = 1;  // Right
            } else if (angleDeg < 225.0f) {
                section = 2;  // Back
            } else {
                section = 3;  // Left
            }

            // Track closest distance in this section
            if (sectionClosestDist[section] < 0 || distance < sectionClosestDist[section]) {
                sectionClosestDist[section] = distance;
            }

            // Check if this rider is about to lap the player (race mode only)
            // A lapper is +1 or more laps ahead of the player
            if (isRace) {
                const StandingsData* riderStanding = pluginData.getStanding(pos.raceNum);
                int riderLaps = riderStanding ? riderStanding->numLaps : 0;
                int lapDiff = riderLaps - playerLaps;

                if (lapDiff >= 1) {
                    // Rider is ahead by 1+ laps - track closest lapper in this section
                    if (sectionLapperDist[section] < 0 || distance < sectionLapperDist[section]) {
                        sectionLapperDist[section] = distance;
                        sectionHasLapper[section] = true;
                    }
                }
            }
        }
    }

    // Draw proximity highlight sectors using rotated sprite
    // The sprite points up (0°/front), rotate for each sector direction
    // Section angles: 0=0°(front), 1=90°(right), 2=180°(back), 3=270°(left)
    // Skip front sector (0) - you can see ahead anyway
    for (int i = 1; i < NUM_SECTORS; ++i) {
        if (sectionClosestDist[i] < 0) continue;  // No rider in this section

        float normalizedDist;
        unsigned long sectorColor;

        // Blue takes priority when a lapper is in this sector (race mode only)
        if (sectionHasLapper[i]) {
            // Blue color for riders about to lap the player
            normalizedDist = sectionLapperDist[i] / m_fAlertDistance;
            float intensity = 0.4f + 0.6f * (1.0f - normalizedDist);  // 0.4 to 1.0 range
            sectorColor = PluginUtils::applyOpacity(ColorPalette::BLUE, intensity);
        } else {
            // Calculate normalized distance (0 = touching, 1 = at max alert distance)
            normalizedDist = sectionClosestDist[i] / m_fAlertDistance;

            unsigned char r, g, b;
            if (normalizedDist < 0.5f) {
                float t = normalizedDist * 2.0f;
                r = static_cast<unsigned char>(gradient.closeR + t * (gradient.midR - gradient.closeR));
                g = static_cast<unsigned char>(gradient.closeG + t * (gradient.midG - gradient.closeG));
                b = static_cast<unsigned char>(gradient.closeB + t * (gradient.midB - gradient.closeB));
            } else {
                float t = (normalizedDist - 0.5f) * 2.0f;
                r = static_cast<unsigned char>(gradient.midR + t * (gradient.farR - gradient.midR));
                g = static_cast<unsigned char>(gradient.midG + t * (gradient.farG - gradient.midG));
                b = static_cast<unsigned char>(gradient.midB + t * (gradient.farB - gradient.midB));
            }

            // Intensity affects opacity (closer = more opaque)
            float intensity = 0.4f + 0.6f * (1.0f - normalizedDist);  // 0.4 to 1.0 range
            sectorColor = PluginUtils::makeColor(r, g, b, static_cast<unsigned char>(255 * intensity));
        }

        // Section rotation angle (in radians, clockwise from up)
        float sectionAngle = (i * 90.0f) * DEG_TO_RAD;

        // Create a square quad centered on radar, sized to fit the radar
        // Sprite quad corners (relative to center, before rotation)
        float halfSize = radarRadius;
        float halfSizeX = halfSize / UI_ASPECT_RATIO;

        // Vertex order: TL, BL, BR, TR (counter-clockwise, matching setQuadPositions)
        float corners[4][2] = {
            { -halfSizeX, -halfSize },  // [0] top-left
            { -halfSizeX,  halfSize },  // [1] bottom-left
            {  halfSizeX,  halfSize },  // [2] bottom-right
            {  halfSizeX, -halfSize }   // [3] top-right
        };

        // Rotate corners around center by section angle
        float cosA = std::cos(sectionAngle);
        float sinA = std::sin(sectionAngle);

        SPluginQuad_t sector;
        for (int j = 0; j < 4; ++j) {
            // Rotate (need to account for aspect ratio when rotating)
            float rx = corners[j][0] * UI_ASPECT_RATIO;  // Convert to uniform space
            float ry = corners[j][1];
            float rotX = rx * cosA - ry * sinA;
            float rotY = rx * sinA + ry * cosA;
            rotX /= UI_ASPECT_RATIO;  // Convert back to screen space

            float px = centerX + rotX;
            float py = centerY + rotY;
            applyOffset(px, py);
            sector.m_aafPos[j][0] = px;
            sector.m_aafPos[j][1] = py;
        }

        sector.m_iSprite = AssetManager::getInstance().getSpriteIndex("radar_sector", 1);
        sector.m_ulColor = sectorColor;
        m_quads.push_back(sector);
    }

    // If no local player found, just show the radar background
    if (!localPlayer) {
        return;
    }

    // Render other riders first (player rendered last to appear on top)
    for (const auto& pos : m_riderPositions) {
        if (pos.raceNum == displayRaceNum) continue;

        const RaceEntryData* entry = pluginData.getRaceEntry(pos.raceNum);
        if (!entry) continue;

        float relX = pos.posX - playerX;
        float relZ = pos.posZ - playerZ;

        float rotatedX = relX * cosYaw - relZ * sinYaw;
        float rotatedZ = relX * sinYaw + relZ * cosYaw;

        float distance = std::sqrt(rotatedX * rotatedX + rotatedZ * rotatedZ);
        if (distance > m_fRadarRangeMeters) continue;

        // Calculate track distance fade (riders on parallel straights fade out)
        float trackFadeOpacity = 1.0f;
        float trackDist = trackSeparation(pos.trackPos, localPlayer->trackPos);

        float trackLength = pluginData.getSessionData().trackLength;
        if (trackLength > 0.0f) {
            // Use actual track distance in meters, tied to radar range
            float trackDistMeters = trackDist * trackLength;
            if (trackDistMeters >= m_fRadarRangeMeters) {
                continue;  // Beyond radar range on track
            }
            trackFadeOpacity = 1.0f - (trackDistMeters / m_fRadarRangeMeters);
        } else {
            // Fallback: use 5% of track as threshold if track length unknown
            constexpr float FALLBACK_THRESHOLD = 0.05f;
            if (trackDist >= FALLBACK_THRESHOLD) {
                continue;
            }
            trackFadeOpacity = 1.0f - (trackDist / FALLBACK_THRESHOLD);
        }

        float radarX = rotatedX / m_fRadarRangeMeters;
        float radarY = rotatedZ / m_fRadarRangeMeters;

        float relativeYaw = pos.yaw - localPlayer->yaw;
        while (relativeYaw > 180.0f) relativeYaw -= 360.0f;
        while (relativeYaw < -180.0f) relativeYaw += 360.0f;

        unsigned long riderColor;
        int trackedShape = -1;  // -1 = use global shape, 1-3 = tracked rider's shape

        // Check if rider is tracked - tracked riders use their configured color with position modulation
        const TrackedRidersManager& trackedMgr = TrackedRidersManager::getInstance();
        const TrackedRiderConfig* trackedConfig = trackedMgr.getTrackedRider(entry->name);

        if (trackedConfig) {
            // Tracked rider - use their configured color with position-based modulation
            unsigned long baseColor = trackedConfig->color;
            trackedShape = trackedConfig->shapeIndex;

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
                    baseColor = PluginUtils::lightenColor(baseColor, 0.4f);
                } else if (lapDiff <= -1) {
                    // Rider is behind by laps - darken color
                    baseColor = PluginUtils::darkenColor(baseColor, 0.6f);
                }
            }

            riderColor = PluginUtils::applyOpacity(baseColor, trackFadeOpacity);
        } else if (m_riderColorMode == RiderColorMode::RELATIVE_POS) {
            // Relative position coloring - only meaningful in race sessions
            unsigned long baseColor;
            if (pluginData.isRaceSession()) {
                const StandingsData* playerStanding = pluginData.getStanding(displayRaceNum);
                const StandingsData* riderStanding = pluginData.getStanding(pos.raceNum);
                int playerPosition = pluginData.getDisplayPositionForRaceNum(displayRaceNum);
                int riderPosition = pluginData.getDisplayPositionForRaceNum(pos.raceNum);
                int playerLaps = playerStanding ? playerStanding->numLaps : 0;
                int riderLaps = riderStanding ? riderStanding->numLaps : 0;

                baseColor = PluginUtils::getRelativePositionColor(
                    playerPosition, riderPosition, playerLaps, riderLaps,
                    this->getColor(ColorSlot::NEUTRAL),
                    this->getColor(ColorSlot::WARNING),
                    this->getColor(ColorSlot::TERTIARY));
            } else {
                // Non-race: positions are meaningless, use uniform color
                baseColor = this->getColor(ColorSlot::NEUTRAL);
            }
            riderColor = PluginUtils::applyOpacity(baseColor, trackFadeOpacity);
        } else if (m_riderColorMode == RiderColorMode::BRAND) {
            riderColor = PluginUtils::applyOpacity(entry->bikeBrandColor, 0.75f * trackFadeOpacity);
        } else {
            // Uniform: riders use the primary color (matching their name color in the
            // standings); accent is reserved for the player, who isn't drawn on the radar
            // but is marked with accent on the Map HUD.
            riderColor = PluginUtils::applyOpacity(this->getColor(ColorSlot::PRIMARY), trackFadeOpacity);
        }

        // Hazard icon override: circle-exclamation for wrong-way, flag for stationary
        HazardType hazardType = pluginData.getRiderHazardType(pos.raceNum);
        if (hazardType != HazardType::None) {
            m_iconCache.ensureInitialized();
            if (hazardType == HazardType::WrongWay) {
                if (m_iconCache.circleExclamation > 0) {
                    trackedShape = AssetManager::getInstance().shapeIndexForSprite(m_iconCache.circleExclamation);
                    riderColor = PluginUtils::applyOpacity(ColorPalette::RED, trackFadeOpacity);
                }
            } else {
                if (m_iconCache.flag > 0) {
                    trackedShape = AssetManager::getInstance().shapeIndexForSprite(m_iconCache.flag);
                    riderColor = PluginUtils::applyOpacity(ColorPalette::YELLOW, trackFadeOpacity);
                }
            }
        }

        // Blue flag icon override (lower priority than hazard)
        if (hazardType == HazardType::None && pluginData.isRiderBlueFlagged(pos.raceNum)) {
            m_iconCache.ensureInitialized();
            if (m_iconCache.flag > 0) {
                trackedShape = AssetManager::getInstance().shapeIndexForSprite(m_iconCache.flag);
                riderColor = PluginUtils::applyOpacity(ColorPalette::BLUE, trackFadeOpacity);
            }
        }
        // Checkered flag for finished riders (lower priority than hazard and blue flag)
        else if (hazardType == HazardType::None) {
            const StandingsData* standing = pluginData.getStanding(pos.raceNum);
            if (standing && pluginData.getSessionData().isRiderFinished(standing->numLaps, standing->numLapsAtLeaderFinish)) {
                m_iconCache.ensureInitialized();
                if (m_iconCache.flagCheckered > 0) {
                    trackedShape = AssetManager::getInstance().shapeIndexForSprite(m_iconCache.flagCheckered);
                    riderColor = PluginUtils::applyOpacity(ColorPalette::WHITE, trackFadeOpacity);
                }
            }
        }

        // Render rider sprite with relative heading (pass tracked shape if available)
        renderRiderSprite(radarX, radarY, relativeYaw, riderColor,
                         centerX, centerY, radarRadius, trackedShape);

        // Render label with matching fade opacity
        int position = pluginData.getDisplayPositionForRaceNum(pos.raceNum);
        renderRiderLabel(radarX, radarY, pos.raceNum, position,
                        centerX, centerY, radarRadius, trackFadeOpacity);
    }

    // Render proximity arrows at screen edges (independent of radar position)
    renderProximityArrows(localPlayer, playerX, playerZ, cosYaw, sinYaw, gradient);
}

// The dial's own width, without a theme; see the declaration for who else needs it.
float RadarHud::unthemedContentWidth(float scale) {
    const LayoutMetrics& L = layoutDefaults();
    return RADAR_SIZE * scale / UI_ASPECT_RATIO
         + 2.0f * (L.panelPaddingXCells * L.cellW * scale);
}

// No setScale override: this panel is CENTRE-ANCHORED (offsetX is its centre), so the
// layout recentres on every width change and a scale change needs no offset
// compensation. It used to call setScaleKeepingCenter, which kept the dial centred by
// REWRITING the stored offset on each scale step -- a persisted setting edited as a
// side effect of another one, computed from bounds left over from the previous render,
// so it was only right once the panel had drawn at least once.

void RadarHud::resetToDefaults() {
    // NEVER THEMED, by default. This panel is a TEXTURE with markers on it -- the dial
    // is the art and the transparency around it is the point -- so a frame, a title band
    // and a content card have nothing to frame and only eat the space the dial needs.
    // Reported from the game after the theme treatment landed: there is no version of
    // this panel that a nine-slice improves.
    //
    // THEME_NONE rather than deleting the code paths: a sprite background already
    // bypasses theming upstream (resolveActiveTheme), so the texture-on default was
    // unthemed anyway -- this covers the case where the user switches the texture OFF,
    // which is what still let a theme in. It stays a per-HUD SETTING, so anyone who
    // wants the frame can name a theme for this HUD and get it back.
    setThemeOverride(THEME_NONE);
    m_bVisible = false;
    m_bShowTitle = false;  // No title for radar (compact display)
    setTextureVariant(1);  // Use first texture variant by default
    m_fBackgroundOpacity = 0.1f;
    m_fScale = 1.0f;
    m_fRadarRangeMeters = DEFAULT_RADAR_RANGE;
    m_riderColorMode = RiderColorMode::BRAND;  // Default to bike brand colors
    m_radarMode = RadarMode::ON;  // Default to always visible
    m_proximityArrowMode = ProximityArrowMode::OFF;  // Disable proximity arrows by default
    m_fAlertDistance = DEFAULT_ALERT_DISTANCE;
    m_labelMode = LabelMode::POSITION;
    m_labelAnchor = LabelAnchor::BELOW;
    m_riderShapeIndex = getShapeIndexByFilename(DEFAULT_RIDER_ICON);
    m_proximityArrowShapeIndex = getShapeIndexByFilename(DEFAULT_PROXIMITY_ARROW_ICON);
    m_fProximityArrowScale = DEFAULT_PROXIMITY_ARROW_SCALE;
    m_proximityArrowColorMode = ProximityArrowColorMode::DISTANCE;
    m_fMarkerScale = DEFAULT_MARKER_SCALE;
    // CENTRE-ANCHORED: offsetX is the dial's centre, so this is centred at EVERY
    // scale. It was 0.43275f, "horizontally centered at scale 1.0" -- and that frozen
    // decimal was 0.5 minus half the CONTENT width, so it did not even centre the
    // drawn box, which fitPanelToGrid rounds up to whole cells. About 3px out at
    // 1080p, and further at every scale but one.
    setPosition(CENTER_ANCHOR_X, cellsY(1));
    setDataDirty();
}

void RadarHud::renderProximityArrows(const Unified::TrackPositionData* localPlayer,
                                      float playerX, float playerZ,
                                      float cosYaw, float sinYaw,
                                      const ProximityGradient& gradient) {
    if (m_proximityArrowMode == ProximityArrowMode::OFF || !localPlayer) return;

    const PluginData& pluginData = PluginData::getInstance();
    int displayRaceNum = pluginData.getDisplayRaceNum();
    float trackLength = pluginData.getSessionData().trackLength;

    // Screen edge margins and arrow size
    constexpr float EDGE_MARGIN = 0.03f;      // Distance from screen edge
    constexpr float ARROW_SIZE = 0.025f;      // Arrow sprite size (in screen height units)

    // Circle mode settings
    constexpr float CIRCLE_RADIUS = 0.42f;    // Radius of circular path (in screen height units)
    constexpr float CIRCLE_CENTER_X = 0.5f;   // Screen center X
    constexpr float CIRCLE_CENTER_Y = 0.5f;   // Screen center Y

    // Get sprite index for the configured arrow shape
    int arrowSpriteIndex = AssetManager::getInstance().iconSpriteForShape(m_proximityArrowShapeIndex);
    bool arrowShouldRotate = TrackedRidersManager::shouldRotate(m_proximityArrowShapeIndex);

    // Process each rider and render arrows for those within alert distance
    for (const auto& pos : m_riderPositions) {
        if (pos.raceNum == displayRaceNum) continue;

        // Calculate relative position
        float relX = pos.posX - playerX;
        float relZ = pos.posZ - playerZ;

        // Rotate to player's heading (so forward = up on screen)
        float rotatedX = relX * cosYaw - relZ * sinYaw;
        float rotatedZ = relX * sinYaw + relZ * cosYaw;

        float distance = std::sqrt(rotatedX * rotatedX + rotatedZ * rotatedZ);

        // Only show arrows for riders within alert distance
        if (distance > m_fAlertDistance || distance < 1.0f) continue;

        // Filter by track distance (skip riders on parallel straights)
        float trackDist = trackSeparation(pos.trackPos, localPlayer->trackPos);

        if (trackLength > 0.0f) {
            float trackDistMeters = trackDist * trackLength;
            if (trackDistMeters > m_fAlertDistance) continue;
        } else {
            constexpr float FALLBACK_THRESHOLD = 0.05f;
            if (trackDist > FALLBACK_THRESHOLD) continue;
        }

        // Calculate angle in radar space (0° = forward/up, clockwise positive)
        // atan2(x, z) gives angle where 0 = forward (+Z), positive = right
        float angle = std::atan2(rotatedX, rotatedZ);  // radians, -PI to PI

        // Skip riders in the front arc (9 o'clock to 3 o'clock = -90° to +90°)
        // You can see riders in front, arrows only needed for blind spots
        float absAngle = std::abs(angle);
        if (absAngle < PI * 0.5f) continue;  // Skip front 180° arc

        float screenX, screenY;
        float arrowRotation;  // Rotation of arrow sprite (pointing outward toward rider)

        if (m_proximityArrowMode == ProximityArrowMode::CIRCLE) {
            // Circle mode: arrows follow a circular path around screen center
            // Angle 0 = top (forward), rotates clockwise
            screenX = CIRCLE_CENTER_X + (CIRCLE_RADIUS / UI_ASPECT_RATIO) * std::sin(angle);
            screenY = CIRCLE_CENTER_Y - CIRCLE_RADIUS * std::cos(angle);

            // Arrow points outward (toward the rider) - same direction as angle
            arrowRotation = angle * RAD_TO_DEG;
        } else {
            // Edge mode: arrows follow screen edges (rectangular path)
            // Convert to normalized angle (0 to 1, where 0 = forward, 0.25 = right, 0.5 = back, 0.75 = left)
            float normalizedAngle = angle / (2.0f * PI);
            if (normalizedAngle < 0) normalizedAngle += 1.0f;

            // Top edge: angle -45° to 45° (normalized: 0.875-1.0 and 0.0-0.125)
            // Right edge: angle 45° to 135° (normalized: 0.125-0.375)
            // Bottom edge: angle 135° to 225° (normalized: 0.375-0.625)
            // Left edge: angle 225° to 315° (normalized: 0.625-0.875)

            if (normalizedAngle < 0.125f || normalizedAngle >= 0.875f) {
                // Top edge (forward)
                float edgePos = (normalizedAngle < 0.5f) ? (normalizedAngle + 0.125f) : (normalizedAngle - 0.875f);
                edgePos = edgePos / 0.25f;  // Normalize to 0-1 along edge
                screenX = 0.5f + (edgePos - 0.5f) * (1.0f - 2.0f * EDGE_MARGIN);
                screenY = EDGE_MARGIN;
                arrowRotation = 0.0f;  // Point up (outward)
            } else if (normalizedAngle < 0.375f) {
                // Right edge
                float edgePos = (normalizedAngle - 0.125f) / 0.25f;
                screenX = 1.0f - EDGE_MARGIN;
                screenY = EDGE_MARGIN + edgePos * (1.0f - 2.0f * EDGE_MARGIN);
                arrowRotation = 90.0f;  // Point right (outward)
            } else if (normalizedAngle < 0.625f) {
                // Bottom edge (behind)
                float edgePos = (normalizedAngle - 0.375f) / 0.25f;
                screenX = 0.5f + (0.5f - edgePos) * (1.0f - 2.0f * EDGE_MARGIN);
                screenY = 1.0f - EDGE_MARGIN;
                arrowRotation = 180.0f;  // Point down (outward)
            } else {
                // Left edge
                float edgePos = (normalizedAngle - 0.625f) / 0.25f;
                screenX = EDGE_MARGIN;
                screenY = EDGE_MARGIN + (1.0f - edgePos) * (1.0f - 2.0f * EDGE_MARGIN);
                arrowRotation = 270.0f;  // Point left (outward)
            }
        }

        // Calculate normalized distance for opacity and size scaling
        float normalizedDist = distance / m_fAlertDistance;

        // Opacity based on distance - fade in/out smoothly at range boundary
        // 0% at edge of range, 100% when very close
        float opacity = 1.0f - normalizedDist;
        unsigned char alpha = static_cast<unsigned char>(255 * opacity);

        // Calculate color based on color mode
        unsigned long arrowColor;
        if (m_proximityArrowColorMode == ProximityArrowColorMode::POSITION) {
            // Position-based coloring - only meaningful in race sessions
            unsigned long baseColor;
            if (pluginData.isRaceSession()) {
                int playerPosition = pluginData.getDisplayPositionForRaceNum(displayRaceNum);
                int riderPosition = pluginData.getDisplayPositionForRaceNum(pos.raceNum);
                const StandingsData* playerStanding = pluginData.getStanding(displayRaceNum);
                const StandingsData* riderStanding = pluginData.getStanding(pos.raceNum);
                int playerLaps = playerStanding ? playerStanding->numLaps : 0;
                int riderLaps = riderStanding ? riderStanding->numLaps : 0;

                baseColor = PluginUtils::getRelativePositionColor(
                    playerPosition, riderPosition, playerLaps, riderLaps,
                    this->getColor(ColorSlot::NEUTRAL),
                    this->getColor(ColorSlot::WARNING),
                    this->getColor(ColorSlot::TERTIARY));
            } else {
                // Non-race: positions are meaningless, use uniform color
                baseColor = this->getColor(ColorSlot::NEUTRAL);
            }
            arrowColor = PluginUtils::applyOpacity(baseColor, opacity);
        } else {
            // Distance-based coloring using proximity gradient
            unsigned char r, g, b;
            if (normalizedDist < 0.5f) {
                float t = normalizedDist * 2.0f;
                r = static_cast<unsigned char>(gradient.closeR + t * (gradient.midR - gradient.closeR));
                g = static_cast<unsigned char>(gradient.closeG + t * (gradient.midG - gradient.closeG));
                b = static_cast<unsigned char>(gradient.closeB + t * (gradient.midB - gradient.closeB));
            } else {
                float t = (normalizedDist - 0.5f) * 2.0f;
                r = static_cast<unsigned char>(gradient.midR + t * (gradient.farR - gradient.midR));
                g = static_cast<unsigned char>(gradient.midG + t * (gradient.farG - gradient.midG));
                b = static_cast<unsigned char>(gradient.midB + t * (gradient.farB - gradient.midB));
            }
            arrowColor = PluginUtils::makeColor(r, g, b, alpha);
        }

        // Scale arrow size (closer = larger, plus user scale setting)
        float sizeScale = 1.0f + 0.5f * (1.0f - normalizedDist);
        float scaledArrowSize = ARROW_SIZE * sizeScale * m_fProximityArrowScale * m_fScale;

        // Create rotated arrow sprite
        float halfSize = scaledArrowSize;
        float halfSizeX = halfSize / UI_ASPECT_RATIO;

        // Only rotate directional icons
        float cosA = 1.0f;
        float sinA = 0.0f;
        if (arrowShouldRotate) {
            float arrowRad = arrowRotation * DEG_TO_RAD;
            cosA = std::cos(arrowRad);
            sinA = std::sin(arrowRad);
        }

        // Vertex order: TL, BL, BR, TR
        float corners[4][2] = {
            { -halfSizeX, -halfSize },
            { -halfSizeX,  halfSize },
            {  halfSizeX,  halfSize },
            {  halfSizeX, -halfSize }
        };

        SPluginQuad_t arrow;
        for (int j = 0; j < 4; ++j) {
            float rx = corners[j][0] * UI_ASPECT_RATIO;
            float ry = corners[j][1];
            float rotX = rx * cosA - ry * sinA;
            float rotY = rx * sinA + ry * cosA;
            rotX /= UI_ASPECT_RATIO;

            arrow.m_aafPos[j][0] = screenX + rotX;
            arrow.m_aafPos[j][1] = screenY + rotY;
        }

        arrow.m_iSprite = arrowSpriteIndex;
        arrow.m_ulColor = arrowColor;
        m_quads.push_back(arrow);
    }
}
