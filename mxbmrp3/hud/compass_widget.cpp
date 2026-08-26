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
    m_panelKind = PanelKind::Widget;
    m_bContentCard = true;
    DEBUG_INFO("CompassWidget created");
    setDraggable(true);
    m_quads.reserve(4);                  // bg + 2 needle halves (or the index tick)
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
    if (!isVisibleAnySurface()) {
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
    rebuildAndRecord();
    clearDataDirty();
    clearLayoutDirty();
}

void CompassWidget::rebuildLayout() {
    rebuildRenderData();
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
    float gaugeAreaHeight = dim.lineHeightNormal * GAUGE_ROWS;

    // BOX-MODEL: the plan owns padding, chrome, the title band and the body card;
    // the gauge area is the single section's content.
    BaseHud::PanelWant want;
    want.contentW = contentWidth;
    want.sectionH = { gaugeAreaHeight };
    want.captionW = planTitleWidth(dim, "Compass");
    PanelPlan& p = planPanel(dim, want);
    const float backgroundWidth = p.width();
    const float backgroundHeight = p.height();

    // THE DIAL IS THE GAUGE AREA, sized from it -- which already carries both the
    // font size and the HUD's scale, so it tracks [font] size like the panel around
    // it. There is no drawn ring: the four cardinals mark the rim and the needle
    // points between them, which is all a compass has to say. The bezel it used to
    // draw was a muted band carrying no information the letters did not, and it was
    // the thing that would not fit inside the panel.
    const float dialRadius = gaugeAreaHeight * 0.5f;

    addPlanBackground(p, startX, startY);
    addPlanTitle(p, "Compass", this->getFont(FontCategory::TITLE),
        this->getColor(ColorSlot::PRIMARY));
    setBounds(startX, startY, startX + backgroundWidth, startY + backgroundHeight);

    // Gauge content is centered on the CARD (PanelPlan::sectionBoxCenterX).
    float centerX = p.sectionBoxCenterX();
    float currentY = p.contentY();

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

    // Cardinal labels ON THE RIM: their outer edge is the dial's, so they mark it
    // the way a bezel's engraving does and cannot leave the gauge area. (0.35 is
    // about half a cap's ink at the small size.) Classic style: the dial is fixed
    // (N stays at top), so a label sits at its true bearing. Modern style: the whole
    // card rotates, so a bearing b sits at screen angle (b - heading) and the faced
    // direction lands at the top under the fixed index.
    //
    // Classic used to put them OUTSIDE the drawn ring, in the panel's padding band,
    // to free the interior for a longer needle. That is air the widget does not own:
    // set the box model's terms to 0 and all four letters land outside the panel's
    // own background.
    bool classic = (m_style == Style::Classic);
    // Inset by HALF A CAP'S INK, so the letters' ink reaches the rim exactly:
    // the furthest out they can sit while N still clears the top of the gauge
    // area, and the mark BarsWidget aligns its own label row to. The 0.35 this
    // replaces was an estimate of the same quantity, a hair too generous, which
    // left S sitting above a neighbouring bars widget's letters.
    float labelRadius = dialRadius
                      - dim.fontSizeSmall * WidgetDimensions::CAP_INK_RATIO * 0.5f;
    for (const auto& c : CARDINALS) {
        float dialBearing = classic ? c.bearing : (c.bearing - displayYaw);
        float angleRad = dialBearing * DEG_TO_RAD;
        float lx = arcCenterX + std::sin(angleRad) * labelRadius / UI_ASPECT_RATIO;
        float ly = arcCenterY - std::cos(angleRad) * labelRadius;
        // addString positions by the text top edge; nudge up to vertically center.
        // The second term is what addString adds back (rowCenterOffset): this is an
        // explicit centring on the arc, so without it the two compound.
        ly -= dim.fontSizeSmall * 0.5f + rowCenterOffset(dim.fontSizeSmall);

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
        // Up to the letters and no further: they are the rim now.
        float needleLength = labelRadius - dim.fontSizeSmall * 0.5f;
        // ONE base width for both halves, deliberately: they are two triangles
        // meeting at the pivot to make a single diamond, and a head wider than
        // its tail reads as a lopsided shape rather than as a needle. What was
        // wrong was the width itself -- at 0.26 the diamond was barely longer
        // than it was wide, which reads as an arrowhead. A real needle is slim.
        float needleHalfBase = dialRadius * 0.15f;
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
        // Sized off the gauge area like the ring, not off dim.scale alone, so it
        // keeps its proportion when [font] size moves the widget.
        unsigned long indexColor = this->getColor(ColorSlot::PRIMARY);
        // From the rim inward, so it reads as an index over the rotating card
        // and starts at the dial's own edge rather than outside the panel.
        addLineSegment(arcCenterX, arcCenterY - dialRadius,
                       arcCenterX, arcCenterY - dialRadius + gaugeAreaHeight * 0.13f,
                       indexColor, gaugeAreaHeight * 0.057f);
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
        // addString positions by the text top edge; nudge up to vertically center,
        // minus what addString itself adds back (see the cardinal labels above).
        addString(headingBuf, arcCenterX,
            arcCenterY - dim.fontSizeSmall * 0.5f - rowCenterOffset(dim.fontSizeSmall),
            Justify::CENTER,
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
    setPosition(cellsX(83), cellsY(74));
    m_targetYaw = 0.0f;
    m_smoothedYaw = 0.0f;
    m_hasHeading = false;
    m_lastDisplayedRaceNum = -1;
    setDataDirty();
}
