// ============================================================================
// hud/wheelie_widget.cpp
// Wheelie widget - how far the bike is up on its back wheel, on a quarter arc
// ============================================================================
#include "wheelie_widget.h"
#include "../core/i18n.h"
#include "peak_marker.h"
#include "severity_ramp.h"
#include "wheelie_arc.h"

#include <algorithm>
#include <cmath>
#include <cstdio>

#include "../core/plugin_utils.h"
#include "../core/color_config.h"

using namespace PluginConstants;
using namespace PluginConstants::Math;

WheelieWidget::WheelieWidget()
{
    m_panelKind = PanelKind::Widget;
    m_bContentCard = true;
    setDraggable(true);
    // Background arc + fill arc + peak tick, plus the panel's own chrome
    // (background, card, title band) so a themed panel never regrows it.
    m_quads.reserve(ARC_SEGMENTS * 2 + 32);
    m_strings.reserve(2);                   // the angle + the title

    setTextureBaseName("wheelie_widget");
    resetToDefaults();
    rebuildRenderData();
}

bool WheelieWidget::handlesDataType(DataChangeType dataType) const {
    return dataType == DataChangeType::InputTelemetry ||
           dataType == DataChangeType::SpectateTarget ||
           dataType == DataChangeType::SessionData;  // Reset the peak on a new session
}

void WheelieWidget::resetTracking() {
    m_smoothed = 0.0f;
    PeakMarker::clear(m_markerValue, m_markerFramesRemaining);
}

void WheelieWidget::update() {
    if (!isVisibleAnySurface()) {
        // Same as LeanWidget: becoming visible while crashed must re-fire the
        // impact snap, so the crash memory does not survive being hidden.
        m_wasCrashed = false;
        clearDataDirty();
        clearLayoutDirty();
        return;
    }

    const PluginData& pluginData = PluginData::getInstance();

    const int currentGeneration = pluginData.getSessionData().sessionGeneration;
    if (currentGeneration != m_cachedSessionGeneration) {
        resetTracking();
        m_wasCrashed = false;
        m_cachedSessionGeneration = currentGeneration;
    }

    const int currentDisplayRaceNum = pluginData.getDisplayRaceNum();
    if (m_lastDisplayedRaceNum != -1 && currentDisplayRaceNum != m_lastDisplayedRaceNum) {
        resetTracking();
    }
    m_lastDisplayedRaceNum = currentDisplayRaceNum;

    // Always rebuild: the angle moves at telemetry rate, like the lean arc.
    rebuildAndRecord();
    clearDataDirty();
    clearLayoutDirty();
}

void WheelieWidget::rebuildLayout() {
    rebuildRenderData();
}

// Smoothing and the peak, frozen while crashed (the loop-out is the reading worth
// keeping) and cleared while there is nothing to read.
void WheelieWidget::updateTracking(const BikeTelemetryData& bikeData, bool live, bool isCrashed) {
    if (m_wasCrashed && !isCrashed) resetTracking();

    if (isCrashed && !m_wasCrashed && live) {
        PeakMarker::snapOnImpact(m_markerValue, m_markerFramesRemaining,
                                 m_smoothed, MARKER_THRESHOLD, m_maxMarkerLingerFrames);
    }
    m_wasCrashed = isCrashed;

    if (!live) {
        resetTracking();
        return;
    }
    if (isCrashed) return;

    const float current = bikeData.wheelieAngle;
    m_smoothed += (current - m_smoothed) * SMOOTH_FACTOR;
    if (current > 0.0f) {
        PeakMarker::advanceActive(m_markerValue, m_markerFramesRemaining,
                                  current, MARKER_THRESHOLD, m_maxMarkerLingerFrames);
    } else {
        PeakMarker::advanceIdle(m_markerValue, m_markerFramesRemaining);
    }
}

void WheelieWidget::rebuildRenderData() {
    clearStrings();
    m_quads.clear();

    auto dim = getScaledDimensions();

    const PluginData& pluginData = PluginData::getInstance();
    const BikeTelemetryData& bikeData = pluginData.getBikeTelemetry();

    // Player telemetry only: spectating there is no wheel contact to read.
    const bool hasData = (pluginData.getDrawState() == ViewState::ON_TRACK);
    const bool live = hasData && bikeData.isValid;

    const float contentWidth = PluginUtils::calculateMonospaceTextWidth(
        WidgetDimensions::SMALL_WIDGET_WIDTH, dim.fontSize);
    const float gaugeHeight = dim.lineHeightNormal * WidgetDimensions::SMALL_WIDGET_CONTENT_LINES;

    PanelWant want;
    want.contentW = contentWidth;
    want.sectionH = { gaugeHeight };
    want.captionW = planTitleWidth(dim, TR("Wheelie"));
    PanelPlan& p = planPanel(dim, want);

    const float startX = 0.0f;
    const float startY = 0.0f;
    addPlanBackground(p, startX, startY);
    addPlanTitle(p, TR("Wheelie"), this->getColor(ColorSlot::PRIMARY));
    setBounds(startX, startY, startX + p.width(), startY + p.height());

    const RiderTrackState* playerPos = pluginData.getPlayerTrackPosition();
    const bool isCrashed = playerPos && playerPos->crashed;
    updateTracking(bikeData, live, isCrashed);

    // The gauge box is centred on the CARD (PanelPlan::sectionBoxCenterX/Y): the
    // panel lands on the cell lattice, so the content band can carry a sliver of
    // rounding below it, and a quarter that hugs the band's corner shows it as a
    // bottom gap the top does not have. The pivot is that box's bottom-left
    // corner; the ring touches its top edge at the upper end and its right edge
    // at the lower one. Radii are in screen-HEIGHT units, as addArcSegment takes.
    const float pivotX = p.sectionBoxCenterX() - contentWidth * 0.5f;
    const float pivotY = p.sectionBoxY() + (p.sectionBoxH() + gaugeHeight) * 0.5f;
    const float radiusY = gaugeHeight;
    const float radiusX = contentWidth * UI_ASPECT_RATIO;
    const float stretchX = radiusX - radiusY;
    // As thick as the lean arc: one character, squared up.
    const float thickness = PluginUtils::calculateMonospaceTextWidth(1, dim.fontSize) * UI_ASPECT_RATIO;
    const float outerRadius = radiusY;
    const float innerRadius = outerRadius - thickness;
    const float midRadius = outerRadius - thickness * 0.5f;

    constexpr float LEVEL = PI * 0.5f;  // addArcSegment: 0 = up, pi/2 = right

    // Background: the whole quarter. Fixed 50% like the lean arc: part of the
    // readout, not the panel, so the background-opacity slider leaves it alone.
    addArcSegment(pivotX, pivotY, innerRadius, outerRadius, 0.0f, LEVEL,
                  PluginUtils::applyOpacity(this->getColor(ColorSlot::MUTED), 0.5f),
                  ARC_SEGMENTS, stretchX);

    // Fill: from level up to the current angle, ramped like the lean arc.
    if (live && m_smoothed > 0.5f) {
        const float ratio = std::min(1.0f, m_smoothed / WheelieArc::MAX_ANGLE);
        const float end = WheelieArc::angleOnArc(m_smoothed, midRadius + stretchX, midRadius);
        const int segments = std::max(2, static_cast<int>(ratio * ARC_SEGMENTS + 0.5f));
        addArcSegment(pivotX, pivotY, innerRadius, outerRadius, end, LEVEL,
                      SeverityRamp::at(ratio,
                                       this->getColor(ColorSlot::POSITIVE),
                                       this->getColor(ColorSlot::NEUTRAL),
                                       this->getColor(ColorSlot::NEGATIVE)),
                      segments, stretchX);
    }

    // Peak tick: PRIMARY, or NEGATIVE while crashed so the loop-out stands out.
    if ((m_bShowMaxMarker || isCrashed) && m_markerFramesRemaining > 0 && m_markerValue > 0.5f) {
        const float at = WheelieArc::angleOnArc(m_markerValue, midRadius + stretchX, midRadius);
        addArcSegment(pivotX, pivotY, innerRadius, outerRadius,
                      std::max(0.0f, at - MARKER_HALF_WIDTH), std::min(LEVEL, at + MARKER_HALF_WIDTH),
                      this->getColor(isCrashed ? ColorSlot::NEGATIVE : ColorSlot::PRIMARY), 1, stretchX);
    }

    // The angle, in the arc's corner: inside the quarter, clear of the ring at any
    // font the box is sized for, its digits' ink standing on the pivot line so
    // they end exactly where the ring's level end does (the BarsWidget label rule:
    // digits are cap-height ink, WidgetDimensions::CAP_INK_RATIO).
    if (m_bShowValue) {
        // Reformatted only when what it says changes: whole degrees, or which
        // placeholder. The widget rebuilds every frame; the number rarely moves.
        const char* text = m_valueText;
        unsigned long color = this->getColor(ColorSlot::SECONDARY);
        if (!hasData) {
            text = Placeholders::NOT_AVAILABLE;
            color = this->getColor(ColorSlot::MUTED);
        } else if (!bikeData.isValid) {
            text = Placeholders::GENERIC;
            color = this->getColor(ColorSlot::MUTED);
        } else {
            const int degrees = static_cast<int>(m_smoothed + 0.5f);
            if (degrees != m_shownDegrees) {
                snprintf(m_valueText, sizeof(m_valueText), "%d", degrees);
                m_shownDegrees = degrees;
            }
        }
        const float ink = dim.fontSize * WidgetDimensions::CAP_INK_RATIO;
        addString(text, pivotX, inkCenteredY(pivotY - ink, ink, dim.fontSize), Justify::LEFT,
                  this->getFont(FontCategory::DIGITS), color, dim.fontSize);
    }
}

void WheelieWidget::resetToDefaults() {
    m_bVisible = false;    // Disabled by default
    m_bShowTitle = false;  // No title for gauge widgets
    setTextureVariant(0);
    m_fBackgroundOpacity = 1.0f;
    setScale(1.0f);
    m_bShowValue = true;
    m_bShowMaxMarker = true;
    m_maxMarkerLingerFrames = 60;
    setPosition(cellsX(31), cellsY(74));
    resetTracking();
    m_wasCrashed = false;
    m_lastDisplayedRaceNum = -1;
    setDataDirty();
}
