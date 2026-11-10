// ============================================================================
// hud/slide_widget.cpp
// Slide widget - how far the bike is travelling sideways of where it points,
// on the Lean widget's half arc turned upside down
// ============================================================================
#include "slide_widget.h"
#include "../core/i18n.h"
#include "peak_marker.h"
#include "severity_ramp.h"

#include <cstdio>
#include <cmath>

#include "../core/plugin_utils.h"
#include "../core/color_config.h"

using namespace PluginConstants;
using namespace PluginConstants::Math;

SlideWidget::SlideWidget()
{
    m_panelKind = PanelKind::Widget;
    m_bContentCard = true;
    setDraggable(true);
    // Background arc + fill arc + max markers, plus the panel's own chrome
    // (background, card, title band) so a themed panel never regrows it.
    m_quads.reserve(ARC_SEGMENTS * 2 + 32);
    m_strings.reserve(2);                   // the angle value + the title

    // Set texture base name for dynamic texture discovery
    setTextureBaseName("slide_widget");

    // Set all configurable defaults
    resetToDefaults();

    rebuildRenderData();
}

bool SlideWidget::handlesDataType(DataChangeType dataType) const {
    return dataType == DataChangeType::InputTelemetry ||
           dataType == DataChangeType::SpectateTarget ||
           dataType == DataChangeType::SessionData;  // Reset max values on new session
}

void SlideWidget::resetTracking() {
    PeakMarker::clear(m_markerValue[0], m_maxFramesRemaining[0]);
    PeakMarker::clear(m_markerValue[1], m_maxFramesRemaining[1]);
    m_smoothedSlide = 0.0f;
}

void SlideWidget::update() {
    // OPTIMIZATION: Skip processing when not visible
    if (!isVisibleAnySurface()) {
        // Same reason as LeanWidget: becoming visible mid-crash must see the rising edge.
        m_wasCrashed = false;
        clearDataDirty();
        clearLayoutDirty();
        return;
    }

    const PluginData& pluginData = PluginData::getInstance();

    // New session: drop the peaks and the needle
    const int currentGeneration = pluginData.getSessionData().sessionGeneration;
    if (currentGeneration != m_cachedSessionGeneration) {
        resetTracking();
        m_wasCrashed = false;
        m_cachedSessionGeneration = currentGeneration;
        setDataDirty();
    }

    // Spectate target change: the peaks were somebody else's
    const int currentDisplayRaceNum = pluginData.getDisplayRaceNum();
    if (m_lastDisplayedRaceNum != -1 && currentDisplayRaceNum != m_lastDisplayedRaceNum) {
        resetTracking();
        setDataDirty();
    }
    m_lastDisplayedRaceNum = currentDisplayRaceNum;

    // Always rebuild - the slide angle moves at telemetry rate, like the lean arc
    rebuildAndRecord();
    clearDataDirty();
    clearLayoutDirty();
}

void SlideWidget::rebuildLayout() {
    // Fast path - for this widget, full rebuild is still cheap
    rebuildRenderData();
}

float SlideWidget::getContentLines() const {
    if (m_enabledRows & ROW_ARC) return WidgetDimensions::SMALL_WIDGET_CONTENT_LINES;
    if (m_enabledRows & ROW_VALUE) return 1.0f;
    return 0.0f;
}

void SlideWidget::rebuildRenderData() {
    clearStrings();
    m_quads.clear();

    auto dim = getScaledDimensions();

    const PluginData& pluginData = PluginData::getInstance();
    const BikeTelemetryData& bikeData = pluginData.getBikeTelemetry();

    // Velocity and heading are the player's own telemetry: nothing to show while
    // spectating or in a replay (see the header).
    const bool hasData = bikeData.isValid && pluginData.getDrawState() == ViewState::ON_TRACK;

    const float startX = 0.0f;
    const float startY = 0.0f;
    const float contentWidth = PluginUtils::calculateMonospaceTextWidth(WidgetDimensions::SMALL_WIDGET_WIDTH, dim.fontSize);

    // BOX-MODEL: the plan owns padding, chrome, the title band and the body card.
    PanelWant want;
    want.contentW = contentWidth;
    want.sectionH = { dim.lineHeightNormal * getContentLines() };
    want.captionW = planTitleWidth(dim, TR("Slide"));
    PanelPlan& p = planPanel(dim, want);

    addPlanBackground(p, startX, startY);
    addPlanTitle(p, TR("Slide"), this->getColor(ColorSlot::PRIMARY));
    setBounds(startX, startY, startX + p.width(), startY + p.height());

    // Crash: freeze the needle and the peaks while down, reset on recovery (as Lean)
    const RiderTrackState* playerPos = pluginData.getPlayerTrackPosition();
    const bool isCrashed = playerPos && playerPos->crashed;
    if (m_wasCrashed && !isCrashed) {
        resetTracking();
    }
    if (isCrashed && !m_wasCrashed) {
        // Snap a peak at the impact moment unless one is already on screen, and keep
        // only one, so the freeze shows the slide the crash came out of.
        const float s = m_smoothedSlide;
        if (s > 0.0f) {
            PeakMarker::snapOnImpact(m_markerValue[1], m_maxFramesRemaining[1], s, 0.5f, m_maxMarkerLingerFrames);
        } else {
            PeakMarker::snapOnImpact(m_markerValue[0], m_maxFramesRemaining[0], -s, 0.5f, m_maxMarkerLingerFrames);
        }
        PeakMarker::collapseToMostRecent(m_markerValue[0], m_maxFramesRemaining[0],
                                         m_markerValue[1], m_maxFramesRemaining[1]);
    }
    m_wasCrashed = isCrashed;

    updateTracking(bikeData.slideAngle, hasData, isCrashed);

    const float centerX = p.sectionBoxCenterX();
    const float currentY = p.contentY();
    const float barWidthRef = PluginUtils::calculateMonospaceTextWidth(1, dim.fontSize);

    if (m_enabledRows & ROW_ARC) {
        // The Lean arc's own radius and thickness, so the pair read as one family;
        // its 1.8-line bowl is centred in the three-line gauge box.
        const float outerRadius = dim.lineHeightNormal * 1.8f;
        const float arcTopY = currentY + (dim.lineHeightNormal * getContentLines() - outerRadius) * 0.5f;
        addArc(centerX, arcTopY, outerRadius, barWidthRef, hasData, isCrashed);
        if (m_enabledRows & ROW_VALUE) {
            // Inside the bowl: Lean's value row turned over with its arc, so the
            // text sits the same 0.18 line across the arc's centre line.
            addValueText(centerX, arcTopY - dim.lineHeightNormal * 0.18f, hasData, dim);
        }
    } else if (m_enabledRows & ROW_VALUE) {
        addValueText(centerX, currentY, hasData, dim);
    }
}

// Needle smoothing and the peak markers. Frozen while crashed; without data the
// needle rests at the centre and the peaks clear.
void SlideWidget::updateTracking(float slideDeg, bool hasData, bool isCrashed) {
    if (isCrashed) return;
    if (!hasData) {
        resetTracking();
        return;
    }

    constexpr float SLIDE_THRESHOLD = 1.0f;  // 1 degree of deadband, as Lean
    if (slideDeg > 0.0f) {
        PeakMarker::advanceActive(m_markerValue[1], m_maxFramesRemaining[1],
                                  slideDeg, SLIDE_THRESHOLD, m_maxMarkerLingerFrames);
        PeakMarker::advanceIdle(m_markerValue[0], m_maxFramesRemaining[0]);
    } else if (slideDeg < 0.0f) {
        PeakMarker::advanceActive(m_markerValue[0], m_maxFramesRemaining[0],
                                  -slideDeg, SLIDE_THRESHOLD, m_maxMarkerLingerFrames);
        PeakMarker::advanceIdle(m_markerValue[1], m_maxFramesRemaining[1]);
    } else {
        PeakMarker::advanceIdle(m_markerValue[0], m_maxFramesRemaining[0]);
        PeakMarker::advanceIdle(m_markerValue[1], m_maxFramesRemaining[1]);
    }

    m_smoothedSlide += (slideDeg - m_smoothedSlide) * SLIDE_SMOOTH_FACTOR;
}

// The upside-down half arc: background split at the bottom centre, the fill swinging
// out to the side the rear has stepped out to, and the lingering peaks.
//
// Angles are BaseHud::addArcSegment's: 0 = up, clockwise positive, so PI is the
// bottom, PI/2 the right end and 3PI/2 the left end of the bowl.
void SlideWidget::addArc(float centerX, float topY, float outerRadius, float barWidthRef,
                         bool hasData, bool isCrashed) {
    const float innerRadius = outerRadius - barWidthRef * UI_ASPECT_RATIO;
    const float arcCenterY = topY;  // the bowl hangs below its centre

    constexpr float BOTTOM = PI;
    constexpr float HALF_SWEEP = PI * 0.5f;
    // The Lean arc's centre gap, so the divider is the same size on both.
    constexpr float CENTER_GAP_HALF_RAD = 0.02f;
    constexpr float MARKER_HALF_RAD = 0.02f;
    const int halfSegments = std::max(3, ARC_SEGMENTS / 2);

    // Background at the Lean arc's fixed 50% (a readout, not the panel backdrop)
    const unsigned long arcBgColor = PluginUtils::applyOpacity(this->getColor(ColorSlot::MUTED), 0.5f);
    addArcSegment(centerX, arcCenterY, innerRadius, outerRadius,
                  BOTTOM - HALF_SWEEP, BOTTOM - CENTER_GAP_HALF_RAD, arcBgColor, halfSegments);
    addArcSegment(centerX, arcCenterY, innerRadius, outerRadius,
                  BOTTOM + CENTER_GAP_HALF_RAD, BOTTOM + HALF_SWEEP, arcBgColor, halfSegments);

    if (!hasData) return;

    // Fill from the gap outward, coloured by how close to full scale (either side)
    if (std::abs(m_smoothedSlide) > 0.5f) {
        float ratio = m_smoothedSlide / MAX_SLIDE_ANGLE;
        ratio = std::max(-1.0f, std::min(1.0f, ratio));
        const float sweep = std::abs(ratio) * HALF_SWEEP;
        if (sweep > CENTER_GAP_HALF_RAD) {
            const int fillSegments = std::max(3, static_cast<int>(std::abs(ratio) * halfSegments));
            const unsigned long fillColor = SeverityRamp::at(std::abs(ratio),
                                                             this->getColor(ColorSlot::POSITIVE),
                                                             this->getColor(ColorSlot::NEUTRAL),
                                                             this->getColor(ColorSlot::NEGATIVE));
            if (ratio > 0.0f) {
                // Rear out to the right: the right-hand side of the bowl
                addArcSegment(centerX, arcCenterY, innerRadius, outerRadius,
                              BOTTOM - sweep, BOTTOM - CENTER_GAP_HALF_RAD, fillColor, fillSegments);
            } else {
                addArcSegment(centerX, arcCenterY, innerRadius, outerRadius,
                              BOTTOM + CENTER_GAP_HALF_RAD, BOTTOM + sweep, fillColor, fillSegments);
            }
        }
    }

    // Lingering peaks: flat NEGATIVE while crashed so the impact slide stands out
    if (m_bShowMaxMarkers || isCrashed) {
        const unsigned long markerColor = isCrashed
            ? this->getColor(ColorSlot::NEGATIVE)
            : this->getColor(ColorSlot::PRIMARY);
        for (int side = 0; side < 2; ++side) {
            if (m_maxFramesRemaining[side] <= 0 || m_markerValue[side] <= 0.5f) continue;
            const float ratio = std::min(1.0f, m_markerValue[side] / MAX_SLIDE_ANGLE);
            const float angle = side == 1 ? BOTTOM - ratio * HALF_SWEEP : BOTTOM + ratio * HALF_SWEEP;
            addArcSegment(centerX, arcCenterY, innerRadius, outerRadius,
                          angle - MARKER_HALF_RAD, angle + MARKER_HALF_RAD, markerColor, 1);
        }
    }
}

// The slide angle in whole degrees, formatted only when that number changes.
void SlideWidget::addValueText(float x, float y, bool hasData, const ScaledDimensions& dim) {
    if (!hasData) {
        addString(Placeholders::NOT_AVAILABLE, x, y, Justify::CENTER,
                  this->getFont(FontCategory::DIGITS), this->getColor(ColorSlot::MUTED), dim.fontSize);
        return;
    }
    const int degrees = static_cast<int>(std::abs(m_smoothedSlide) + 0.5f);
    if (degrees != m_shownDegrees) {
        m_shownDegrees = degrees;
        snprintf(m_valueText, sizeof(m_valueText), "%d", degrees);
    }
    addString(m_valueText, x, y, Justify::CENTER,
              this->getFont(FontCategory::DIGITS), this->getColor(ColorSlot::SECONDARY), dim.fontSize);
}

void SlideWidget::resetToDefaults() {
    m_bVisible = false;    // Disabled by default
    m_bShowTitle = false;  // No title for gauge widgets
    setTextureVariant(0);  // No texture by default
    m_fBackgroundOpacity = 1.0f;
    setScale(1.0f);
    m_enabledRows = ROW_DEFAULT;
    m_bShowMaxMarkers = true;
    m_maxMarkerLingerFrames = 60;  // ~1 second at 60fps
    // The widget strip's next free slot, left of ECU (default_slots.h: only the
    // strip gives each member a slot of its own).
    setPosition(cellsX(44), cellsY(74));
    resetTracking();
    m_wasCrashed = false;
    m_lastDisplayedRaceNum = -1;
    m_shownDegrees = -1;
    setDataDirty();
}
