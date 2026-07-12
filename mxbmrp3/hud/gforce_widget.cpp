// ============================================================================
// hud/gforce_widget.cpp
// G-force widget - chassis-local lateral/longitudinal G as a dot on a G-G
// diagram, styled to match the FMX HUD combo-arc meter.
// ============================================================================
#include "gforce_widget.h"

#include <cstdio>
#include <cmath>
#include <algorithm>

#include "../diagnostics/logger.h"
#include "../core/plugin_utils.h"
#include "../core/color_config.h"
#include "../core/asset_manager.h"

using namespace PluginConstants;
using namespace PluginConstants::Math;

GForceWidget::GForceWidget()
{
    DEBUG_INFO("GForceWidget created");
    setDraggable(true);
    m_quads.reserve(RING_SEGMENTS + 4);   // bg quad + ring arc segments + 2 dots
    m_strings.reserve(3);                 // optional title + current + peak

    setTextureBaseName("gforce_widget");

    resetToDefaults();
    rebuildRenderData();
}

bool GForceWidget::handlesDataType(DataChangeType dataType) const {
    return dataType == DataChangeType::InputTelemetry ||
           dataType == DataChangeType::SpectateTarget ||
           dataType == DataChangeType::SessionData;
}

void GForceWidget::resetTracking() {
    m_smoothedLat = 0.0f;
    m_smoothedLong = 0.0f;
    m_markerLat = 0.0f;
    m_markerLong = 0.0f;
    m_markerMagnitude = 0.0f;
    m_markerFramesRemaining = 0;
}

void GForceWidget::update() {
    if (!isVisibleAnySurface()) {
        // While hidden, drop any "was crashed" memory so that becoming visible while
        // currently crashed re-fires the rising-edge snap. Without this, the sequence
        // [crash → hide → recover → crash again → show] would miss the second snap
        // because m_wasCrashed stayed true throughout invisibility.
        m_wasCrashed = false;
        clearDataDirty();
        clearLayoutDirty();
        return;
    }

    const PluginData& pluginData = PluginData::getInstance();

    const SessionData& sessionData = pluginData.getSessionData();
    int currentGeneration = sessionData.sessionGeneration;
    if (currentGeneration != m_cachedSessionGeneration) {
        resetTracking();
        m_wasCrashed = false;
        m_cachedSessionGeneration = currentGeneration;
        setDataDirty();
    }

    int currentDisplayRaceNum = pluginData.getDisplayRaceNum();
    if (m_lastDisplayedRaceNum != -1 && currentDisplayRaceNum != m_lastDisplayedRaceNum) {
        resetTracking();
        setDataDirty();
    }
    m_lastDisplayedRaceNum = currentDisplayRaceNum;

    rebuildRenderData();
    clearDataDirty();
    clearLayoutDirty();
}

void GForceWidget::rebuildLayout() {
    rebuildRenderData();
}

void GForceWidget::addIconDot(float x, float y, int spriteIndex, unsigned long color, float size) {
    if (spriteIndex <= 0) {
        // Fallback: solid colored quad
        addDot(x, y, color, size);
        return;
    }
    applyOffset(x, y);
    float halfX = (size * 0.5f) / UI_ASPECT_RATIO;
    float halfY = size * 0.5f;
    SPluginQuad_t quad;
    quad.m_aafPos[0][0] = x - halfX; quad.m_aafPos[0][1] = y - halfY;
    quad.m_aafPos[1][0] = x - halfX; quad.m_aafPos[1][1] = y + halfY;
    quad.m_aafPos[2][0] = x + halfX; quad.m_aafPos[2][1] = y + halfY;
    quad.m_aafPos[3][0] = x + halfX; quad.m_aafPos[3][1] = y - halfY;
    quad.m_iSprite = spriteIndex;
    quad.m_ulColor = color;
    m_quads.push_back(quad);
}

unsigned long GForceWidget::getMagnitudeColor(float magnitude) const {
    float denom = (m_maxScale > 0.01f) ? m_maxScale : 1.0f;
    float t = std::min(1.0f, std::max(0.0f, magnitude / denom));

    auto extractR = [](unsigned long v) { return static_cast<uint8_t>(v & 0xFF); };
    auto extractG = [](unsigned long v) { return static_cast<uint8_t>((v >> 8) & 0xFF); };
    auto extractB = [](unsigned long v) { return static_cast<uint8_t>((v >> 16) & 0xFF); };
    auto extractA = [](unsigned long v) { return static_cast<uint8_t>((v >> 24) & 0xFF); };

    unsigned long lo = this->getColor(ColorSlot::POSITIVE);
    unsigned long mid = this->getColor(ColorSlot::NEUTRAL);
    unsigned long hi = this->getColor(ColorSlot::NEGATIVE);

    uint8_t r, g, b, a;
    // Piecewise linear (mirrors RadarHud proximity gradient):
    // 0.0–0.5 → POSITIVE → NEUTRAL, 0.5–1.0 → NEUTRAL → NEGATIVE.
    // Alpha is lerped alongside RGB so a user customizing palette slots with non-FF
    // alpha doesn't silently get fully opaque output.
    if (t < 0.5f) {
        float u = t * 2.0f;
        r = static_cast<uint8_t>(extractR(lo) + u * (extractR(mid) - extractR(lo)));
        g = static_cast<uint8_t>(extractG(lo) + u * (extractG(mid) - extractG(lo)));
        b = static_cast<uint8_t>(extractB(lo) + u * (extractB(mid) - extractB(lo)));
        a = static_cast<uint8_t>(extractA(lo) + u * (extractA(mid) - extractA(lo)));
    } else {
        float u = (t - 0.5f) * 2.0f;
        r = static_cast<uint8_t>(extractR(mid) + u * (extractR(hi) - extractR(mid)));
        g = static_cast<uint8_t>(extractG(mid) + u * (extractG(hi) - extractG(mid)));
        b = static_cast<uint8_t>(extractB(mid) + u * (extractB(hi) - extractB(mid)));
        a = static_cast<uint8_t>(extractA(mid) + u * (extractA(hi) - extractA(mid)));
    }
    return PluginUtils::makeColor(r, g, b, a);
}

// FMX-style arc segment renderer — builds connected inner/outer-edge quads.
// Color (incl. alpha) is preserved, unlike addLineSegment which forces full alpha.
void GForceWidget::addArcSegment(float centerX, float centerY, float innerRadius, float outerRadius,
                                  float startAngleRad, float endAngleRad,
                                  unsigned long color, int numSegments)
{
    if (numSegments < 1) numSegments = 1;
    float angleStep = (endAngleRad - startAngleRad) / numSegments;

    float prevInnerX = 0.0f, prevInnerY = 0.0f;
    float prevOuterX = 0.0f, prevOuterY = 0.0f;
    bool hasPrev = false;

    for (int i = 0; i <= numSegments; ++i) {
        float angle = startAngleRad + i * angleStep;
        float innerX = centerX + std::sin(angle) * innerRadius / UI_ASPECT_RATIO;
        float innerY = centerY - std::cos(angle) * innerRadius;
        float outerX = centerX + std::sin(angle) * outerRadius / UI_ASPECT_RATIO;
        float outerY = centerY - std::cos(angle) * outerRadius;

        if (hasPrev) {
            float sPIX = prevInnerX, sPIY = prevInnerY;
            float sPOX = prevOuterX, sPOY = prevOuterY;
            float sIX = innerX, sIY = innerY;
            float sOX = outerX, sOY = outerY;
            applyOffset(sPIX, sPIY);
            applyOffset(sPOX, sPOY);
            applyOffset(sIX, sIY);
            applyOffset(sOX, sOY);

            SPluginQuad_t quad;
            quad.m_aafPos[0][0] = sPOX; quad.m_aafPos[0][1] = sPOY;
            quad.m_aafPos[1][0] = sPIX; quad.m_aafPos[1][1] = sPIY;
            quad.m_aafPos[2][0] = sIX;  quad.m_aafPos[2][1] = sIY;
            quad.m_aafPos[3][0] = sOX;  quad.m_aafPos[3][1] = sOY;
            quad.m_iSprite = SpriteIndex::SOLID_COLOR;
            quad.m_ulColor = color;
            m_quads.push_back(quad);
        }

        prevInnerX = innerX; prevInnerY = innerY;
        prevOuterX = outerX; prevOuterY = outerY;
        hasPrev = true;
    }
}

void GForceWidget::rebuildRenderData() {
    clearStrings();
    m_quads.clear();

    auto dim = getScaledDimensions();

    const PluginData& pluginData = PluginData::getInstance();
    const BikeTelemetryData& bikeData = pluginData.getBikeTelemetry();

    float startX = 0.0f;
    float startY = 0.0f;

    // Standard small-widget footprint (3 content rows × GFORCE_WIDTH chars), same as
    // LeanWidget/FuelWidget. Donut dimensions are pinned to the FMX rotation-arc
    // constants so visually it reads as one of those arcs, regardless of the widget's
    // content-area row count. The donut may extend into the vertical padding region
    // (still inside the background quad).
    constexpr int GAUGE_ROWS = 3;
    constexpr float ARC_MID_RADIUS_BASE = 0.035f;  // FMX rotation-arc ARC_RADIUS
    constexpr float ARC_THICKNESS_BASE = 0.006f;   // FMX rotation-arc ARC_THICKNESS

    float contentWidth = PluginUtils::calculateMonospaceTextWidth(WidgetDimensions::GFORCE_WIDTH, dim.fontSize);
    float backgroundWidth = contentWidth + dim.paddingH + dim.paddingH;

    float titleHeight = m_bShowTitle ? dim.lineHeightNormal : 0.0f;
    float gaugeAreaHeight = dim.lineHeightNormal * GAUGE_ROWS;
    float contentHeight = titleHeight + gaugeAreaHeight;
    float backgroundHeight = dim.paddingV + contentHeight + dim.paddingV;

    // FMX-style: middle radius scaled by m_fScale, thickness 17% of middle radius
    float arcThickness = ARC_THICKNESS_BASE * dim.scale;
    float ringMidRadius = ARC_MID_RADIUS_BASE * dim.scale;
    float outerRadius = ringMidRadius + arcThickness * 0.5f;
    float innerRadius = ringMidRadius - arcThickness * 0.5f;

    addBackgroundQuad(startX, startY, backgroundWidth, backgroundHeight);
    setBounds(startX, startY, startX + backgroundWidth, startY + backgroundHeight);

    float contentStartX = startX + dim.paddingH;
    float contentStartY = startY + dim.paddingV;
    float centerX = contentStartX + (contentWidth / 2.0f);

    float currentY = contentStartY;

    if (m_bShowTitle) {
        addString("G-Force", contentStartX, currentY, Justify::LEFT,
            this->getFont(FontCategory::TITLE), this->getColor(ColorSlot::PRIMARY), dim.fontSize);
        currentY += titleHeight;
    }

    // Donut centered both horizontally and vertically in the gauge area
    float arcCenterX = centerX;
    float arcCenterY = currentY + gaugeAreaHeight * 0.5f;

    // Crash recovery detection
    const TrackPositionData* playerPos = pluginData.getPlayerTrackPosition();
    bool isCrashed = playerPos && playerPos->crashed;
    if (m_wasCrashed && !isCrashed) {
        resetTracking();
    }
    // Rising-edge detection for "snap marker to current at crash start" — see below.
    bool crashJustStarted = isCrashed && !m_wasCrashed;
    m_wasCrashed = isCrashed;

    // Accel is only sampled for the local player (RunTelemetry). Gate display when spectating.
    bool hasAccelData = (pluginData.getDrawState() == ViewState::ON_TRACK);

    // Display convention: felt G / inertial reaction (iRacing/AC-style live HUD), not the
    // top-down vehicle-frame MoTeC plot. Left turn → rider thrown right (dot right); brake →
    // rider thrown forward (dot up); throttle → rider pushed back (dot down).
    //   lat = -accelX  (vehicle accel right → rider felt-G left → dot left)
    //   lon = -accelZ  (vehicle accel forward → rider felt-G back → dot down;
    //                   brake = vehicle accel backward → dot up)
    // dotLive — we have raw accel samples to drive the live dot (true even during a
    //           crash; tumbling forces are still real Gs worth showing).
    // peakLive — we should also update the peak marker. Suppressed while crashed so
    //            tumbling doesn't overwrite the impact's peak nor age it out via linger.
    float lat = 0.0f;
    float lon = 0.0f;
    bool dotLive = bikeData.isValid && hasAccelData;
    bool peakLive = dotLive && !isCrashed;

    if (dotLive) {
        lat = -bikeData.accelX;
        lon = -bikeData.accelZ;
        m_smoothedLat += (lat - m_smoothedLat) * DOT_SMOOTH_FACTOR;
        m_smoothedLong += (lon - m_smoothedLong) * DOT_SMOOTH_FACTOR;
    } else {
        // No telemetry at all (e.g. not on track) — snap smoothing to center so the
        // dot doesn't sit at a stale position when telemetry comes back.
        m_smoothedLat = 0.0f;
        m_smoothedLong = 0.0f;
    }

    if (peakLive) {
        // Peak tracking uses raw samples (true physical peak, not the smoothed live
        // dot's lagged trail). The marker dot and the bottom-line "peak" text both
        // read from m_markerMagnitude, which lingers for m_maxMarkerLingerFrames
        // after the live signal drops below it and then resets — same pattern as
        // BarsWidget's m_markerValues. So a spawn settling-spike at session start
        // shows briefly and clears, rather than inflating the displayed peak for
        // the entire stint.
        constexpr float MAGNITUDE_THRESHOLD = 0.05f;
        float currentMagnitude = std::sqrt(lat * lat + lon * lon);

        if (currentMagnitude > m_markerMagnitude + MAGNITUDE_THRESHOLD) {
            m_markerMagnitude = currentMagnitude;
            m_markerLat = lat;
            m_markerLong = lon;
            m_markerFramesRemaining = 0;
        } else if (currentMagnitude < m_markerMagnitude - MAGNITUDE_THRESHOLD &&
                   m_markerFramesRemaining == 0 && m_markerMagnitude > MAGNITUDE_THRESHOLD) {
            m_markerFramesRemaining = m_maxMarkerLingerFrames;
        } else if (m_markerFramesRemaining > 0) {
            m_markerFramesRemaining--;
            if (m_markerFramesRemaining == 0) {
                m_markerMagnitude = 0.0f;
            }
        }
    } else if (isCrashed) {
        // On the first frame of the crash, force the marker to render at the current
        // G if no marker is currently being drawn (linger inactive). Captures impact-
        // moment telemetry even when the rider was holding a steady value pre-crash
        // (which keeps m_markerMagnitude updated but never arms the linger). Doesn't
        // touch a still-rendering higher peak — that pre-crash reading is more
        // informative.
        if (crashJustStarted && dotLive && m_markerFramesRemaining == 0) {
            float impactMag = std::sqrt(lat * lat + lon * lon);
            if (impactMag > 0.05f) {
                m_markerLat = lat;
                m_markerLong = lon;
                m_markerMagnitude = impactMag;
                m_markerFramesRemaining = m_maxMarkerLingerFrames;
            }
        }
        // Otherwise: freeze the peak marker + linger countdown while crashed so the
        // rider can read the impact peak after the tumble settles, without the linger
        // expiring mid-roll or a softer post-impact bounce overwriting it. On crash
        // recovery, resetTracking() at the top of rebuildRenderData() wipes the marker
        // (mirrors LeanWidget's recovery behavior); peaks then accumulate normally
        // from zero. The live dot keeps moving with current tumbling forces (above) —
        // only the max is held.
    } else {
        // No telemetry — clear the marker too.
        m_markerFramesRemaining = 0;
        m_markerMagnitude = 0.0f;
    }

    // Single background ring (FMX combo-arc style, full 360°). Tinted green→yellow→red by
    // the *lingering peak* magnitude (m_markerMagnitude), not the instantaneous value:
    // real impacts are often a sub-frame spike the live value sheds before you can read it,
    // so the live magnitude leaves the ring green almost all the time. The peak tracks up
    // instantly (responsive on the way up), then holds at the peak for the marker linger
    // window (~1s) before resuming — long enough to register an impact. Because the peak
    // marker is frozen while crashed, the ring also freezes at the impact color until
    // recovery, mirroring the max-marker freeze. Idle (0 g) reads green, full-scale (at
    // m_maxScale) reads red. When there's no live data (spectate/replay/invalid) it stays
    // muted gray so an empty ring reads as "no data" rather than a misleading green "0 g".
    // Fixed 50% opacity keeps it behind the dot and unaffected by the background-opacity slider.
    unsigned long arcBgColor;
    if (dotLive) {
        arcBgColor = PluginUtils::applyOpacity(getMagnitudeColor(m_markerMagnitude), 0.5f);
    } else {
        arcBgColor = PluginUtils::applyOpacity(this->getColor(ColorSlot::MUTED), 0.5f);
    }
    addArcSegment(arcCenterX, arcCenterY, innerRadius, outerRadius,
                  0.0f, 2.0f * PI, arcBgColor, RING_SEGMENTS);

    auto plotPoint = [&](float gLat, float gLong, float& outX, float& outY) {
        float clampedLat = std::max(-m_maxScale, std::min(m_maxScale, gLat));
        float clampedLong = std::max(-m_maxScale, std::min(m_maxScale, gLong));
        // Use innerRadius so the dot stays inside the ring's inner edge
        float plotRadius = innerRadius * 0.95f;
        outX = arcCenterX + (clampedLat / m_maxScale) * plotRadius / UI_ASPECT_RATIO;
        outY = arcCenterY - (clampedLong / m_maxScale) * plotRadius;
    };

    // Look up the "circle" icon (filled dot). Falls back to a solid quad if not found.
    int circleSpriteIndex = AssetManager::getInstance().getIconSpriteIndex("circle");

    // Draw order: live dot first, then max marker on top — the marker is smaller, so if
    // we drew it first it'd disappear under the live dot whenever they overlap.

    // Live G dot — color shifts POSITIVE → NEUTRAL → NEGATIVE as magnitude approaches
    // m_maxScale. Draws normally even while crashed (the tumbling Gs are still real).
    if (dotLive) {
        float currentMag = std::sqrt(m_smoothedLat * m_smoothedLat + m_smoothedLong * m_smoothedLong);
        float dx, dy;
        plotPoint(m_smoothedLat, m_smoothedLong, dx, dy);
        unsigned long dotColor = getMagnitudeColor(currentMag);
        addIconDot(dx, dy, circleSpriteIndex, dotColor, outerRadius * 0.32f);
    }

    // Lingering max-position marker (gradient-colored at peak magnitude, or flat
    // NEGATIVE/red while crashed to make the impact peak pop visually).
    if ((m_bShowMaxMarker || isCrashed) && m_markerFramesRemaining > 0 && m_markerMagnitude > 0.05f) {
        float mx, my;
        plotPoint(m_markerLat, m_markerLong, mx, my);
        unsigned long markerColor = isCrashed
            ? this->getColor(ColorSlot::NEGATIVE)
            : getMagnitudeColor(m_markerMagnitude);
        addIconDot(mx, my, circleSpriteIndex, markerColor, outerRadius * 0.22f);
    }

    // Center text inside the ring — top line = current live G magnitude, bottom line =
    // recorded peak this stint. Font matches LeanWidget's value style: DIGITS at fontSize.
    // Spectate / data-invalid handling mirrors LeanWidget's steer value (lean_widget.cpp:589):
    //   - spectate/replay → "N/A" in MUTED
    //   - on track but telemetry invalid → "-" in MUTED
    //   - otherwise → value in normal color (SECONDARY for both rows)
    if (m_bShowMaxText) {
        float currentMag = std::sqrt(m_smoothedLat * m_smoothedLat + m_smoothedLong * m_smoothedLong);

        char currentBuf[16];
        char peakBuf[16];
        unsigned long currentColor = this->getColor(ColorSlot::SECONDARY);
        unsigned long peakColor = this->getColor(ColorSlot::SECONDARY);

        if (!hasAccelData) {
            // Spectating/replay — accel data structurally unavailable for non-player riders
            snprintf(currentBuf, sizeof(currentBuf), "%s", Placeholders::NOT_AVAILABLE);
            snprintf(peakBuf, sizeof(peakBuf), "%s", Placeholders::NOT_AVAILABLE);
            currentColor = this->getColor(ColorSlot::MUTED);
            peakColor = this->getColor(ColorSlot::MUTED);
        } else if (!bikeData.isValid) {
            snprintf(currentBuf, sizeof(currentBuf), "%s", Placeholders::GENERIC);
            snprintf(peakBuf, sizeof(peakBuf), "%s", Placeholders::GENERIC);
            currentColor = this->getColor(ColorSlot::MUTED);
            peakColor = this->getColor(ColorSlot::MUTED);
        } else {
            snprintf(currentBuf, sizeof(currentBuf), "%.1f", currentMag);
            snprintf(peakBuf, sizeof(peakBuf), "%.1f", m_markerMagnitude);
        }

        // Two-row text block centered vertically — same formula as FMX rotation arcs:
        // blockHeight = lineHeightNormal + fontSize so the block visually centers on
        // arcCenterY, and rows advance by lineHeightNormal.
        float blockHeight = dim.lineHeightNormal + dim.fontSize;
        float textY1 = arcCenterY - blockHeight * 0.5f;
        float textY2 = textY1 + dim.lineHeightNormal;
        addString(currentBuf, arcCenterX, textY1, Justify::CENTER,
            this->getFont(FontCategory::DIGITS), currentColor, dim.fontSize);
        addString(peakBuf, arcCenterX, textY2, Justify::CENTER,
            this->getFont(FontCategory::DIGITS), peakColor, dim.fontSize);
    }
}

void GForceWidget::resetToDefaults() {
    m_bVisible = false;
    m_bShowTitle = false;
    setTextureVariant(0);
    m_fBackgroundOpacity = 1.0f;
    m_fScale = 1.0f;
    m_bShowMaxText = true;
    m_bShowMaxMarker = true;
    m_maxMarkerLingerFrames = 60;
    m_maxScale = 20.0f;
    setPosition(0.5995f, 0.86828f);
    resetTracking();
    m_wasCrashed = false;
    m_lastDisplayedRaceNum = -1;
    setDataDirty();
}
