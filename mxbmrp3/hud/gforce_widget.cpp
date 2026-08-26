// ============================================================================
// hud/gforce_widget.cpp
// G-force widget - chassis-local lateral/longitudinal G as a dot on a G-G
// diagram, styled to match the FMX HUD combo-arc meter.
// ============================================================================
#include "gforce_widget.h"
#include "severity_ramp.h"

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
    m_panelKind = PanelKind::Widget;
    m_bContentCard = true;
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

    rebuildAndRecord();
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
    // The ramp itself now lives in SeverityRamp::at -- LeanWidget says the same
    // thing about lean angle, and two gauges reading "fine / serious / at the
    // limit" have to agree on where the middle is.
    const float denom = (m_maxScale > 0.01f) ? m_maxScale : 1.0f;
    return SeverityRamp::at(magnitude / denom,
                            this->getColor(ColorSlot::POSITIVE),
                            this->getColor(ColorSlot::NEUTRAL),
                            this->getColor(ColorSlot::NEGATIVE));
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
    // LeanWidget/FuelWidget. The donut may extend into the vertical padding region
    // (still inside the background quad).
    //
    // Donut dimensions are a FRACTION OF THE GAUGE AREA. They used to be absolute
    // normalized lengths pinned to the FMX rotation-arc constants -- 0.035 and 0.006,
    // scaled only by the HUD's own scale setting -- so raising [font] size grew this
    // widget's PANEL (rows * line-height) and left the donut exactly the size it was.
    // The widget appeared to ignore the one knob advertised as moving the whole UI.
    // The ratios reproduce the shipped donut to within a pixel at the default font
    // (gauge area 0.0704; 0.0704 * 0.5 = 0.0352 against the old 0.035).
    constexpr int GAUGE_ROWS = 3;

    float contentWidth = PluginUtils::calculateMonospaceTextWidth(WidgetDimensions::GFORCE_WIDTH, dim.fontSize);
    float gaugeAreaHeight = dim.lineHeightNormal * GAUGE_ROWS;

    // BOX-MODEL: the plan owns padding, chrome, the title band and the body card;
    // the gauge area is the single section's content.
    BaseHud::PanelWant want;
    want.contentW = contentWidth;
    want.sectionH = { gaugeAreaHeight };
    want.captionW = planTitleWidth(dim, "G-Force");
    PanelPlan& p = planPanel(dim, want);
    const float backgroundWidth = p.width();
    const float backgroundHeight = p.height();

    // Sized from the gauge area, which already carries the font size AND the HUD's
    // scale -- so the donut tracks [font] size like the panel around it.
    float arcThickness = gaugeAreaHeight * WidgetDimensions::GAUGE_RING_THICKNESS_RATIO;
    float ringMidRadius = gaugeAreaHeight * WidgetDimensions::GAUGE_RING_MID_RADIUS_RATIO;
    float outerRadius = ringMidRadius + arcThickness * 0.5f;
    float innerRadius = ringMidRadius - arcThickness * 0.5f;

    addPlanBackground(p, startX, startY);
    addPlanTitle(p, "G-Force", this->getFont(FontCategory::TITLE),
        this->getColor(ColorSlot::PRIMARY));
    setBounds(startX, startY, startX + backgroundWidth, startY + backgroundHeight);

    // Gauge content is centered on the CARD (PanelPlan::sectionBoxCenterX).
    float centerX = p.sectionBoxCenterX();
    float currentY = p.contentY();

    // Donut centered both horizontally and vertically in the gauge area
    float arcCenterX = centerX;
    float arcCenterY = currentY + gaugeAreaHeight * 0.5f;

    // Crash recovery detection
    const RiderTrackState* playerPos = pluginData.getPlayerTrackPosition();
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
    // recorded peak this stint. DIGITS at the SMALL size, matching TyreTempWidget's
    // readout rather than LeanWidget's: both of these sit INSIDE a gauge whose
    // geometry they must not crowd, where Lean's value has a row to itself.
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
        // blockHeight = lineHeight + fontSize so the block visually centers on
        // arcCenterY, and rows advance by one line. SMALL throughout: the row
        // advance has to follow the font, or two small values sit a normal line
        // apart and the block stops reading as a pair.
        const float valueFontSize = dim.fontSizeSmall;
        float blockHeight = dim.lineHeightSmall + valueFontSize;
        // Minus what addString adds back -- an explicit block centring compounds with
        // its per-string row centring.
        float textY1 = arcCenterY - blockHeight * 0.5f - rowCenterOffset(valueFontSize);
        float textY2 = textY1 + dim.lineHeightSmall;
        addString(currentBuf, arcCenterX, textY1, Justify::CENTER,
            this->getFont(FontCategory::DIGITS), currentColor, valueFontSize);
        addString(peakBuf, arcCenterX, textY2, Justify::CENTER,
            this->getFont(FontCategory::DIGITS), peakColor, valueFontSize);
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
    setPosition(cellsX(96), cellsY(74));
    resetTracking();
    m_wasCrashed = false;
    m_lastDisplayedRaceNum = -1;
    setDataDirty();
}
