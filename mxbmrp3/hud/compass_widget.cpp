// ============================================================================
// hud/compass_widget.cpp
// Compass widget - shows the bike's heading (degrees from north) as a dial.
// Classic style (default): a fixed north-up ring with a red/white needle whose
// red half points to the heading. Modern style: a rotating card with the heading
// at the top under a fixed index and a numeric readout in the center.
// ============================================================================
#include "compass_widget.h"

#include <cstdio>
#include <cmath>

#include "../diagnostics/logger.h"
#include "../core/plugin_utils.h"
#include "../core/color_config.h"

using namespace PluginConstants;
using namespace PluginConstants::Math;

namespace {
    // Cardinal ring labels (bearing in degrees from north). North is flagged so it
    // can be emphasized like a real compass bezel.
    struct CardinalLabel { float bearing; const char* label; bool isNorth; };
    const CardinalLabel CARDINALS[] = {
        {0.0f,   "N", true},
        {90.0f,  "E", false},
        {180.0f, "S", false},
        {270.0f, "W", false},
    };
}

CompassWidget::CompassWidget()
{
    DEBUG_INFO("CompassWidget created");
    setDraggable(true);
    m_quads.reserve(RING_SEGMENTS + 3);  // bg + ring + 2 needle halves (or index tick)
    m_strings.reserve(6);                // optional title + 4 cardinal labels + heading number

    setTextureBaseName("compass_widget");

    resetToDefaults();
    rebuildRenderData();
}

bool CompassWidget::handlesDataType(DataChangeType dataType) const {
    // Heading is fed via updateRiderPositions (RaceTrackPosition fast path); these
    // types only drive the session/spectate reset handled in update().
    return dataType == DataChangeType::SpectateTarget ||
           dataType == DataChangeType::SessionData;
}

void CompassWidget::resetTracking() {
    // Drop the heading so the next sample snaps the dial instead of spinning it
    // across the wrap from a stale value (e.g. on a new session or spectate switch).
    m_hasHeading = false;
}

void CompassWidget::updateRiderPositions(int numVehicles, const Unified::TrackPositionData* positions) {
    if (!positions || numVehicles <= 0) return;

    int displayRaceNum = PluginData::getInstance().getDisplayRaceNum();
    for (int i = 0; i < numVehicles; ++i) {
        if (positions[i].raceNum != displayRaceNum) continue;

        // Freeze the heading while crashed - a tumbling bike's yaw is noise, and
        // freezing mirrors MapHud's rotate-to-player crash behavior.
        // Skip non-finite yaw (NaN/Inf): a single NaN propagates through fmod and the
        // smoothing accumulator and permanently kills the dial, so freeze instead.
        if (!positions[i].crashed && std::isfinite(positions[i].yaw)) {
            float yaw = positions[i].yaw;  // degrees from north
            // Normalize to 0..360 for display and shortest-path smoothing.
            yaw = std::fmod(yaw, 360.0f);
            if (yaw < 0.0f) yaw += 360.0f;
            m_targetYaw = yaw;
            if (!m_hasHeading) {
                m_smoothedYaw = yaw;  // Snap on first sample (no spin-up)
                m_hasHeading = true;
            }
        }
        setDataDirty();
        break;
    }
}

void CompassWidget::update() {
    if (!isVisible()) {
        clearDataDirty();
        clearLayoutDirty();
        return;
    }

    const PluginData& pluginData = PluginData::getInstance();

    // Reset heading on a new session so the dial snaps rather than spins.
    const SessionData& sessionData = pluginData.getSessionData();
    if (sessionData.sessionGeneration != m_cachedSessionGeneration) {
        resetTracking();
        m_cachedSessionGeneration = sessionData.sessionGeneration;
    }

    // Reset on spectate-target change (now viewing a different rider's heading).
    int currentDisplayRaceNum = pluginData.getDisplayRaceNum();
    if (m_lastDisplayedRaceNum != -1 && currentDisplayRaceNum != m_lastDisplayedRaceNum) {
        resetTracking();
    }
    m_lastDisplayedRaceNum = currentDisplayRaceNum;

    // Always rebuild while visible so the smoothing advances every frame (the dial
    // eases between heading samples regardless of position-update rate).
    rebuildRenderData();
    clearDataDirty();
    clearLayoutDirty();
}

void CompassWidget::rebuildLayout() {
    rebuildRenderData();
}

void CompassWidget::addArcSegment(float centerX, float centerY, float innerRadius, float outerRadius,
                                   float startAngleRad, float endAngleRad,
                                   unsigned long color, int numSegments) {
    if (numSegments < 1) numSegments = 1;
    float angleStep = (endAngleRad - startAngleRad) / numSegments;

    float prevInnerX = 0.0f, prevInnerY = 0.0f;
    float prevOuterX = 0.0f, prevOuterY = 0.0f;
    bool hasPrev = false;

    for (int i = 0; i <= numSegments; ++i) {
        float angle = startAngleRad + i * angleStep;
        // sin/cos so 0 rad = up, positive = clockwise (matches Lean/GForce rings).
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

void CompassWidget::addNeedleHalf(float centerX, float centerY, float angleRad,
                                   float length, float baseHalfWidth, unsigned long color) {
    // Apex at the outer tip; base centered on the pivot (no extension behind center).
    float tipX = centerX + std::sin(angleRad) * length / UI_ASPECT_RATIO;
    float tipY = centerY - std::cos(angleRad) * length;

    float perpAngle = angleRad + PI * 0.5f;
    float baseLeftX = centerX + std::sin(perpAngle) * baseHalfWidth / UI_ASPECT_RATIO;
    float baseLeftY = centerY - std::cos(perpAngle) * baseHalfWidth;
    float baseRightX = centerX - std::sin(perpAngle) * baseHalfWidth / UI_ASPECT_RATIO;
    float baseRightY = centerY + std::cos(perpAngle) * baseHalfWidth;

    applyOffset(tipX, tipY);
    applyOffset(baseLeftX, baseLeftY);
    applyOffset(baseRightX, baseRightY);

    // Match BaseHud::addNeedleQuad winding (front, front, baseRight, baseLeft) with the
    // tip used for both front vertices so the quad renders as a sharp triangle.
    SPluginQuad_t quad;
    quad.m_aafPos[0][0] = tipX;       quad.m_aafPos[0][1] = tipY;
    quad.m_aafPos[1][0] = tipX;       quad.m_aafPos[1][1] = tipY;
    quad.m_aafPos[2][0] = baseRightX; quad.m_aafPos[2][1] = baseRightY;
    quad.m_aafPos[3][0] = baseLeftX;  quad.m_aafPos[3][1] = baseLeftY;
    quad.m_iSprite = SpriteIndex::SOLID_COLOR;
    quad.m_ulColor = color;
    m_quads.push_back(quad);
}

void CompassWidget::rebuildRenderData() {
    clearStrings();
    m_quads.clear();

    auto dim = getScaledDimensions();

    float startX = 0.0f;
    float startY = 0.0f;

    // Standard small-widget footprint - identical to GForceWidget (3 content rows).
    constexpr int GAUGE_ROWS = 3;
    float contentWidth = PluginUtils::calculateMonospaceTextWidth(WidgetDimensions::COMPASS_WIDTH, dim.fontSize);
    float backgroundWidth = contentWidth + dim.paddingH + dim.paddingH;

    float titleHeight = m_bShowTitle ? dim.lineHeightNormal : 0.0f;
    float gaugeAreaHeight = dim.lineHeightNormal * GAUGE_ROWS;
    float contentHeight = titleHeight + gaugeAreaHeight;
    float backgroundHeight = dim.paddingV + contentHeight + dim.paddingV;

    // Ring geometry (scaled), mirroring the GForceWidget donut.
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
        addString("Compass", contentStartX, currentY, Justify::LEFT,
            this->getFont(FontCategory::TITLE), this->getColor(ColorSlot::PRIMARY), dim.fontSize);
        currentY += titleHeight;
    }

    float arcCenterX = centerX;
    float arcCenterY = currentY + gaugeAreaHeight * 0.5f;

    // Advance smoothing toward the latest heading (shortest angular path) so wrap
    // across 0/360 doesn't spin the dial the long way around.
    if (m_hasHeading) {
        float diff = m_targetYaw - m_smoothedYaw;
        while (diff > 180.0f) diff -= 360.0f;
        while (diff < -180.0f) diff += 360.0f;
        m_smoothedYaw += diff * YAW_SMOOTH_FACTOR;
        m_smoothedYaw = std::fmod(m_smoothedYaw, 360.0f);
        if (m_smoothedYaw < 0.0f) m_smoothedYaw += 360.0f;
    }

    float displayYaw = m_hasHeading ? m_smoothedYaw : 0.0f;

    // Background ring. Muted gray when there's no heading (reads as "no data"
    // rather than a misleading North-up lock). Fixed 50% opacity keeps it behind
    // the labels and unaffected by the background-opacity slider.
    unsigned long ringColor = PluginUtils::applyOpacity(this->getColor(ColorSlot::MUTED), 0.5f);
    addArcSegment(arcCenterX, arcCenterY, innerRadius, outerRadius,
                  0.0f, 2.0f * PI, ringColor, RING_SEGMENTS);

    // Cardinal labels. Classic style: the ring is fixed (N stays at top), so labels sit
    // at their true bearing, just outside the ring in the padding band (like BarsWidget's
    // edge labels) - freeing the interior for a longer needle. Modern style: the whole
    // card rotates, so a bearing b sits at screen angle (b - heading) and the faced
    // direction lands at the top under the fixed index, with labels inside the ring.
    bool classic = (m_style == Style::Classic);
    float labelRadius = classic
        ? (outerRadius + dim.fontSizeSmall * 0.55f)
        : (innerRadius - dim.fontSizeSmall * 0.6f);
    for (const auto& c : CARDINALS) {
        float dialBearing = classic ? c.bearing : (c.bearing - displayYaw);
        float angleRad = dialBearing * DEG_TO_RAD;
        float lx = arcCenterX + std::sin(angleRad) * labelRadius / UI_ASPECT_RATIO;
        float ly = arcCenterY - std::cos(angleRad) * labelRadius;
        // addString positions by the text top edge; nudge up to vertically center.
        ly -= dim.fontSizeSmall * 0.5f;

        unsigned long labelColor;
        if (!m_hasHeading) {
            labelColor = this->getColor(ColorSlot::MUTED);
        } else if (c.isNorth) {
            labelColor = this->getColor(ColorSlot::NEGATIVE);   // North marked (compass convention)
        } else {
            labelColor = this->getColor(ColorSlot::SECONDARY);
        }

        addString(c.label, lx, ly, Justify::CENTER,
            this->getFont(FontCategory::STRONG), labelColor, dim.fontSizeSmall);
    }

    if (classic) {
        // Two-tone needle (slim diamond) on the fixed north-up ring. On a north-up dial
        // the bezel bearing b sits at screen angle b, so the red half points to the
        // player's heading (screen angle = heading) and the white half is the tail.
        // Face north -> red points up at N; turn right -> red swings right toward E. The
        // ring stays put (no spinning letters), and with the labels in the padding the
        // needle reaches near the inner edge. The two halves meet exactly at the pivot
        // (no overlap, no hub needed).
        float needleLength = innerRadius * 0.92f;
        float needleHalfBase = ringMidRadius * 0.28f;
        float headingAngleRad = displayYaw * DEG_TO_RAD;
        float tailAngleRad = (displayYaw + 180.0f) * DEG_TO_RAD;
        unsigned long headingColor = m_hasHeading ? this->getColor(ColorSlot::NEGATIVE)
                                                  : this->getColor(ColorSlot::MUTED);
        unsigned long tailColor = m_hasHeading ? this->getColor(ColorSlot::PRIMARY)
                                               : this->getColor(ColorSlot::MUTED);
        addNeedleHalf(arcCenterX, arcCenterY, headingAngleRad, needleLength, needleHalfBase, headingColor);
        addNeedleHalf(arcCenterX, arcCenterY, tailAngleRad, needleLength, needleHalfBase, tailColor);
    } else {
        // Fixed top index: a bold radial tick at 12 o'clock pointing into the dial.
        unsigned long indexColor = this->getColor(ColorSlot::PRIMARY);
        addLineSegment(arcCenterX, arcCenterY - outerRadius - dim.scale * 0.005f,
                       arcCenterX, arcCenterY - innerRadius,
                       indexColor, dim.scale * 0.004f);
    }

    // Modern style only: the integer heading, perfectly centered. The classic needle
    // dial stays clean (no number over the needle).
    if (!classic) {
        char headingBuf[8];
        unsigned long textColor;
        if (!m_hasHeading) {
            snprintf(headingBuf, sizeof(headingBuf), "%s", Placeholders::GENERIC);
            textColor = this->getColor(ColorSlot::MUTED);
        } else {
            int headingInt = static_cast<int>(displayYaw + 0.5f) % 360;
            snprintf(headingBuf, sizeof(headingBuf), "%d", headingInt);
            textColor = this->getColor(ColorSlot::SECONDARY);
        }
        // addString positions by the text top edge; nudge up to vertically center.
        addString(headingBuf, arcCenterX, arcCenterY - dim.fontSizeSmall * 0.5f, Justify::CENTER,
            this->getFont(FontCategory::DIGITS), textColor, dim.fontSizeSmall);
    }
}

void CompassWidget::resetToDefaults() {
    m_bVisible = false;            // Disabled by default
    m_bShowTitle = false;         // No title for gauge widgets
    setTextureVariant(0);         // No texture by default
    m_fBackgroundOpacity = 1.0f;  // Full opacity (100%)
    m_fScale = 1.0f;
    m_style = Style::Classic;
    // Bottom gauge row (evenly spaced, pitch 0.0715, same y). G-Force is the leftmost
    // all-game gauge at 0.5995 (Bars/Lean/Fuel sit to its right); the compass takes the
    // next free slot to G-Force's left.
    setPosition(0.528f, 0.8769f);
    m_targetYaw = 0.0f;
    m_smoothedYaw = 0.0f;
    m_hasHeading = false;
    m_lastDisplayedRaceNum = -1;
    setDataDirty();
}
