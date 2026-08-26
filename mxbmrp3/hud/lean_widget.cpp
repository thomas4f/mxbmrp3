// ============================================================================
// hud/lean_widget.cpp
// Lean widget - displays bike lean/roll angle with half-donut arc gauge
// ============================================================================
#include "lean_widget.h"
#include "severity_ramp.h"

#include <cstdio>
#include <cmath>

#include "../diagnostics/logger.h"
#include "../core/plugin_utils.h"
#include "../core/color_config.h"

using namespace PluginConstants;
using namespace PluginConstants::Math;

LeanWidget::LeanWidget()
{
    m_panelKind = PanelKind::Widget;
    m_bContentCard = true;
    DEBUG_INFO("LeanWidget created");
    setDraggable(true);
    m_quads.reserve(ARC_SEGMENTS * 2 + 2);  // background arc + fill arc + center marker
    m_strings.reserve(4);  // angle value + max left + max right + "max" label

    // Set texture base name for dynamic texture discovery
    setTextureBaseName("lean_widget");

    // Set all configurable defaults
    resetToDefaults();

    rebuildRenderData();
}

bool LeanWidget::handlesDataType(DataChangeType dataType) const {
    // Update on telemetry changes (bike data)
    return dataType == DataChangeType::InputTelemetry ||
           dataType == DataChangeType::SpectateTarget ||
           dataType == DataChangeType::SessionData;  // Reset max values on new session
}

void LeanWidget::resetTracking() {
    m_markerValueLeft = 0.0f;
    m_markerValueRight = 0.0f;
    m_maxFramesRemaining[0] = 0;
    m_maxFramesRemaining[1] = 0;
    m_steerMarkerLeft = 0.0f;
    m_steerMarkerRight = 0.0f;
    m_steerFramesRemaining[0] = 0;
    m_steerFramesRemaining[1] = 0;
    m_smoothedLean = 0.0f;
}

void LeanWidget::update() {
    // OPTIMIZATION: Skip processing when not visible
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

    // Detect session change - reset max values on new track/bike/session
    const SessionData& sessionData = pluginData.getSessionData();
    int currentGeneration = sessionData.sessionGeneration;
    if (currentGeneration != m_cachedSessionGeneration) {
        resetTracking();
        m_wasCrashed = false;
        m_cachedSessionGeneration = currentGeneration;
        setDataDirty();
    }

    // Detect spectate target change - reset max values when switching viewed rider
    int currentDisplayRaceNum = pluginData.getDisplayRaceNum();
    if (m_lastDisplayedRaceNum != -1 && currentDisplayRaceNum != m_lastDisplayedRaceNum) {
        resetTracking();
        setDataDirty();
    }
    m_lastDisplayedRaceNum = currentDisplayRaceNum;

    // Always rebuild - lean angle updates at high frequency (telemetry rate)
    rebuildAndRecord();
    clearDataDirty();
    clearLayoutDirty();
}

void LeanWidget::rebuildLayout() {
    // Fast path - for this widget, full rebuild is still cheap
    rebuildRenderData();
}

int LeanWidget::getRowCount() const {
    int count = 0;
    // Arc takes 2 rows, lean value is positioned within row 2 of the arc
    if (m_enabledRows & ROW_ARC) count += 2;
    else if (m_enabledRows & ROW_LEAN_VALUE) count += 1;  // Just lean text if no arc
    if (m_enabledRows & ROW_STEER_BAR) count += 1;
    if (m_enabledRows & ROW_STEER_VALUE) count += 1;
    return count;
}


unsigned long LeanWidget::fillColorFor(float ratio) const {
    if (m_fillColorMode == FillColorMode::FIXED) return m_arcFillColor;
    return SeverityRamp::at(std::abs(ratio),
                            this->getColor(ColorSlot::POSITIVE),
                            this->getColor(ColorSlot::NEUTRAL),
                            this->getColor(ColorSlot::NEGATIVE));
}

void LeanWidget::rebuildRenderData() {
    clearStrings();
    m_quads.clear();

    auto dim = getScaledDimensions();

    const PluginData& pluginData = PluginData::getInstance();
    const BikeTelemetryData& bikeData = pluginData.getBikeTelemetry();

    // Steer data is only available when player is on track (not when spectating/replay)
    bool hasSteerData = (pluginData.getDrawState() == ViewState::ON_TRACK);

    float startX = 0.0f;
    float startY = 0.0f;

    // Use same width as SpeedWidget
    float contentWidth = PluginUtils::calculateMonospaceTextWidth(WidgetDimensions::LEAN_WIDTH, dim.fontSize);

    // Calculate dynamic height based on enabled rows
    // Each row is lineHeightNormal (arc takes 2 rows)
    int rowCount = getRowCount();

    // BOX-MODEL: the plan owns padding, chrome, the title band and the body card.
    BaseHud::PanelWant want;
    want.contentW = contentWidth;
    want.sectionH = { dim.lineHeightNormal * rowCount };
    want.captionW = planTitleWidth(dim, "Lean");
    PanelPlan& p = planPanel(dim, want);
    const float backgroundWidth = p.width();
    const float backgroundHeight = p.height();

    // Background, title band and body card at the plan's coordinates
    addPlanBackground(p, startX, startY);
    addPlanTitle(p, "Lean", this->getFont(FontCategory::TITLE),
        this->getColor(ColorSlot::PRIMARY));

    // Set bounds for drag detection
    setBounds(startX, startY, startX + backgroundWidth, startY + backgroundHeight);

    // Gauge content is centered on the CARD (PanelPlan::sectionBoxCenterX);
    // the steer bar spans contentWidth around that center.
    float centerX = p.sectionBoxCenterX();
    float contentStartX = centerX - contentWidth / 2.0f;

    // Track current Y position for row-based layout
    float currentY = p.contentY();

    // Check for crash recovery - reset max lean when recovering from crash
    const RiderTrackState* playerPos = pluginData.getPlayerTrackPosition();
    bool isCrashed = playerPos && playerPos->crashed;
    if (m_wasCrashed && !isCrashed) {
        resetTracking();
    }
    // Capture steer angle before updating crash state (for freezing)
    // Read steer directly from InputTelemetryData (not history buffer, which is only
    // populated when TelemetryHud is visible for graph visualization)
    const InputTelemetryData& inputTelemetry = pluginData.getInputTelemetry();
    float currentSteer = inputTelemetry.steer;

    // On the first frame of the crash, capture impact-moment state so the rider can
    // see what was happening at the point of impact:
    //   - Freeze the steer value (used for display while crashed)
    //   - Snap the steer and lean max markers to current values *if* no marker is
    //     currently visible (don't overwrite a still-visible higher peak — that's
    //     the more informative reading).
    if (isCrashed && !m_wasCrashed) {
        m_frozenSteer = currentSteer;

        // Snap steer max marker on the side matching current steer direction, but only
        // if no marker is currently being rendered (linger inactive). The render gate
        // is m_steerFramesRemaining > 0; the steer value alone may be non-zero from a
        // steady hold without the linger ever arming, so checking the value isn't
        // enough — we need to check the linger state directly.
        // Positive steer == left in this API, so the sides map straight across.
        if (currentSteer > 0.0f) {
            PeakMarker::snapOnImpact(m_steerMarkerLeft, m_steerFramesRemaining[0],
                                     currentSteer, 1.0f, m_maxMarkerLingerFrames);
        } else {
            PeakMarker::snapOnImpact(m_steerMarkerRight, m_steerFramesRemaining[1],
                                     std::abs(currentSteer), 1.0f, m_maxMarkerLingerFrames);
        }

        // Snap lean max marker on the side matching current lean (same gate semantics)
        if (bikeData.isValid) {
            // Lean uses the opposite sign convention to steer: negative == left.
            float impactLean = bikeData.roll;
            if (impactLean < 0.0f) {
                PeakMarker::snapOnImpact(m_markerValueLeft, m_maxFramesRemaining[0],
                                         std::abs(impactLean), 0.5f, m_maxMarkerLingerFrames);
            } else {
                PeakMarker::snapOnImpact(m_markerValueRight, m_maxFramesRemaining[1],
                                         impactLean, 0.5f, m_maxMarkerLingerFrames);
            }
        }

        // Collapse to a single marker per gauge so the freeze shows one peak, not two.
        // Higher linger count == more recently set.
        PeakMarker::collapseToMostRecent(m_markerValueLeft, m_maxFramesRemaining[0],
                                         m_markerValueRight, m_maxFramesRemaining[1]);
        PeakMarker::collapseToMostRecent(m_steerMarkerLeft, m_steerFramesRemaining[0],
                                         m_steerMarkerRight, m_steerFramesRemaining[1]);
    }
    m_wasCrashed = isCrashed;

    // Steer max marker tracking (similar to lean markers)
    constexpr float STEER_THRESHOLD = 1.0f;  // 1 degree threshold
    if (!isCrashed) {
        // Positive steer == left in this API. Exactly zero leaves both sides idle.
        if (currentSteer > 0) {
            PeakMarker::advanceActive(m_steerMarkerLeft, m_steerFramesRemaining[0],
                                      currentSteer, STEER_THRESHOLD, m_maxMarkerLingerFrames);
            PeakMarker::advanceIdle(m_steerMarkerRight, m_steerFramesRemaining[1]);
        } else if (currentSteer < 0) {
            PeakMarker::advanceActive(m_steerMarkerRight, m_steerFramesRemaining[1],
                                      std::abs(currentSteer), STEER_THRESHOLD, m_maxMarkerLingerFrames);
            PeakMarker::advanceIdle(m_steerMarkerLeft, m_steerFramesRemaining[0]);
        } else {
            PeakMarker::advanceIdle(m_steerMarkerLeft, m_steerFramesRemaining[0]);
            PeakMarker::advanceIdle(m_steerMarkerRight, m_steerFramesRemaining[1]);
        }
    } else {
        // Freeze steer max-marker linger while crashed so the rider can read the
        // peak after the bike settles. On crash recovery, resetTracking() wipes
        // the markers outright (see top of rebuildRenderData).
    }

    // Get current lean angle from telemetry
    float currentLean = 0.0f;
    constexpr float LEAN_THRESHOLD = 1.0f;  // 1 degree threshold to avoid jitter

    if (bikeData.isValid && !isCrashed) {
        // Only update when not crashed
        currentLean = bikeData.roll;

        // Update lean marker tracking
        // Lean uses the opposite sign convention to steer: negative == left.
        if (currentLean < 0) {
            PeakMarker::advanceActive(m_markerValueLeft, m_maxFramesRemaining[0],
                                      std::abs(currentLean), LEAN_THRESHOLD, m_maxMarkerLingerFrames);
            PeakMarker::advanceIdle(m_markerValueRight, m_maxFramesRemaining[1]);
        } else if (currentLean > 0) {
            PeakMarker::advanceActive(m_markerValueRight, m_maxFramesRemaining[1],
                                      currentLean, LEAN_THRESHOLD, m_maxMarkerLingerFrames);
            PeakMarker::advanceIdle(m_markerValueLeft, m_maxFramesRemaining[0]);
        } else {
            PeakMarker::advanceIdle(m_markerValueLeft, m_maxFramesRemaining[0]);
            PeakMarker::advanceIdle(m_markerValueRight, m_maxFramesRemaining[1]);
        }

        // Apply smoothing only when not crashed
        m_smoothedLean += (currentLean - m_smoothedLean) * LEAN_SMOOTH_FACTOR;
    }
    else if (isCrashed) {
        // When crashed, m_smoothedLean stays frozen at last value (intentional —
        // tumbling through 180°+ would just thrash the gauge with no useful info).
        // Lean max-marker linger is also frozen so the rider can read the
        // pre-crash peak after the bike settles. On crash recovery,
        // resetTracking() wipes the markers outright.
    }
    else {
        // Data invalid - reset lean to 0 (same as other widgets)
        m_smoothedLean = 0.0f;
        PeakMarker::clear(m_markerValueLeft, m_maxFramesRemaining[0]);
        PeakMarker::clear(m_markerValueRight, m_maxFramesRemaining[1]);
    }

    // Common values used by multiple elements
    float barWidthRef = PluginUtils::calculateMonospaceTextWidth(1, dim.fontSize);
    unsigned long textColor = this->getColor(ColorSlot::SECONDARY);

    // === Row 1-2: Arc (if enabled) ===
    if (m_enabledRows & ROW_ARC) {
        // Arc dimensions - arc should visually fill rows 1-2 with text inside
        float arcHeight = dim.lineHeightNormal * 2.0f;
        float arcThickness = barWidthRef * UI_ASPECT_RATIO;

        // Arc radius sized so the arc visually spans both rows
        // The arc center is at the bottom of row 1, so radius = arcHeight positions
        // the top of arc at row 1 start and bottom (opening) at row 2 end
        float outerRadius = arcHeight * 0.9f;  // Large arc covering most of 2 rows
        float innerRadius = outerRadius - arcThickness;

        // Position arc center at bottom of the 2-row area (opening faces down)
        float arcCenterY = currentY + arcHeight - outerRadius * 0.1f;

        // Convert angle constants to radians
        float arcStartRad = ARC_START_ANGLE * DEG_TO_RAD;
        float arcEndRad = ARC_END_ANGLE * DEG_TO_RAD;

        // Center gap: instead of overlaying a black marker on a continuous arc, the
        // background and fill arcs are drawn as two halves separated by a real gap
        // around 0°. Gap half-width matches the old black-marker half-width so the
        // visual size of the divider stays the same.
        constexpr float CENTER_GAP_HALF_RAD = 0.02f;
        float markerWidth = 0.02f;  // still used for max-marker rendering below

        // Draw background arc (full gauge range, split around center) - same color as BarsWidget backgrounds.
        // Fixed 50% opacity: the arc is part of the gauge readout, so it stays legible regardless of the
        // background-opacity slider (only addBackgroundQuad follows that). Matches the GForceWidget ring.
        unsigned long arcBgColor = PluginUtils::applyOpacity(this->getColor(ColorSlot::MUTED), 0.5f);
        int halfSegments = std::max(3, ARC_SEGMENTS / 2);
        addArcSegment(centerX, arcCenterY, innerRadius, outerRadius,
                      arcStartRad, -CENTER_GAP_HALF_RAD, arcBgColor, halfSegments);
        addArcSegment(centerX, arcCenterY, innerRadius, outerRadius,
                      CENTER_GAP_HALF_RAD, arcEndRad, arcBgColor, halfSegments);

        // Draw filled arc from the gap edge outward to current lean angle
        float displayLean = -m_smoothedLean;
        if (std::abs(displayLean) > 0.5f) {
            float leanRatio = displayLean / MAX_LEAN_ANGLE;
            float fillAngleRad = leanRatio * arcEndRad;

            if (fillAngleRad < arcStartRad) fillAngleRad = arcStartRad;
            if (fillAngleRad > arcEndRad) fillAngleRad = arcEndRad;

            int fillSegments = std::max(3, static_cast<int>(std::abs(fillAngleRad / (arcEndRad - arcStartRad)) * ARC_SEGMENTS));

            // Coloured by how far the lean is toward full scale, not by which way
            // it leans: the ramp answers "how close to the limit", and a mirrored
            // lean is the same answer.
            const unsigned long fillColor = fillColorFor(leanRatio);
            if (fillAngleRad < -CENTER_GAP_HALF_RAD) {
                addArcSegment(centerX, arcCenterY, innerRadius, outerRadius,
                              fillAngleRad, -CENTER_GAP_HALF_RAD, fillColor, fillSegments);
            } else if (fillAngleRad > CENTER_GAP_HALF_RAD) {
                addArcSegment(centerX, arcCenterY, innerRadius, outerRadius,
                              CENTER_GAP_HALF_RAD, fillAngleRad, fillColor, fillSegments);
            }
        }

        // Draw lingering max markers on the arc (if enabled)
        if (m_bShowMaxMarkers || isCrashed) {
            // Flat NEGATIVE/red while crashed so the impact peak is easy to spot;
            // PRIMARY (white) otherwise.
            unsigned long maxMarkerColor = isCrashed
                ? this->getColor(ColorSlot::NEGATIVE)
                : this->getColor(ColorSlot::PRIMARY);

            if (m_maxFramesRemaining[0] > 0 && m_markerValueLeft > 0.5f) {
                float leanRatio = m_markerValueLeft / MAX_LEAN_ANGLE;
                float maxAngleRad = leanRatio * arcEndRad;
                if (maxAngleRad > arcEndRad) maxAngleRad = arcEndRad;
                addArcSegment(centerX, arcCenterY, innerRadius, outerRadius,
                              maxAngleRad - markerWidth, maxAngleRad + markerWidth, maxMarkerColor, 1);
            }

            if (m_maxFramesRemaining[1] > 0 && m_markerValueRight > 0.5f) {
                float leanRatio = m_markerValueRight / MAX_LEAN_ANGLE;
                float maxAngleRad = -leanRatio * arcEndRad;
                if (maxAngleRad < arcStartRad) maxAngleRad = arcStartRad;
                addArcSegment(centerX, arcCenterY, innerRadius, outerRadius,
                              maxAngleRad - markerWidth, maxAngleRad + markerWidth, maxMarkerColor, 1);
            }
        }

        // Lean value text positioned at row 2 (aligns with FuelWidget "used" row)
        if (m_enabledRows & ROW_LEAN_VALUE) {
            char angleBuffer[16];
            if (!bikeData.isValid) {
                snprintf(angleBuffer, sizeof(angleBuffer), "%s", Placeholders::GENERIC);
            } else {
                float absAngle = std::abs(m_smoothedLean);
                int displayAngle = static_cast<int>(absAngle + 0.5f);
                snprintf(angleBuffer, sizeof(angleBuffer), "%d", displayAngle);
            }
            // Position at row 2 (same as FuelWidget "used" row)
            float leanTextY = currentY + dim.lineHeightNormal;
            addString(angleBuffer, centerX, leanTextY, Justify::CENTER,
                this->getFont(FontCategory::DIGITS), textColor, dim.fontSize);
        }

        currentY += dim.lineHeightNormal * 2;  // Arc takes 2 rows
    } else if (m_enabledRows & ROW_LEAN_VALUE) {
        // No arc, just lean value text (takes 1 row)
        char angleBuffer[16];
        if (!bikeData.isValid) {
            snprintf(angleBuffer, sizeof(angleBuffer), "%s", Placeholders::GENERIC);
        } else {
            float absAngle = std::abs(m_smoothedLean);
            int displayAngle = static_cast<int>(absAngle + 0.5f);
            snprintf(angleBuffer, sizeof(angleBuffer), "%d", displayAngle);
        }
        addString(angleBuffer, centerX, currentY, Justify::CENTER,
            this->getFont(FontCategory::DIGITS), textColor, dim.fontSize);
        currentY += dim.lineHeightNormal;
    }

    // === Row 3: Steer Bar (if enabled) ===
    if (m_enabledRows & ROW_STEER_BAR) {
        float steerBarHeight = barWidthRef * UI_ASPECT_RATIO;
        // Center bar vertically within the row
        float steerBarY = currentY + (dim.lineHeightNormal - steerBarHeight) / 2.0f;

        float steerAngle = isCrashed ? m_frozenSteer : currentSteer;
        float maxSteer = pluginData.getSessionData().steerLock;
        if (maxSteer < 1.0f) maxSteer = MAX_STEER_ANGLE;

        // Real gap at center instead of an overlaid marker. Gap width matches the old
        // black center-marker width so the visual divider is the same size.
        const float steerCenterGap = contentWidth * 0.02f;
        const float steerCenterGapHalf = steerCenterGap * 0.5f;
        float halfWidth = contentWidth / 2.0f;
        float halfWidthAvail = halfWidth - steerCenterGapHalf;  // effective half-bar (post-gap)
        // Fixed 50% — steer-bar background is part of the gauge, not the panel backdrop, so it
        // doesn't follow the background-opacity slider (matches the arc background above).
        unsigned long bgColor = PluginUtils::applyOpacity(this->getColor(ColorSlot::MUTED), 0.5f);

        // Left bar background
        {
            SPluginQuad_t barBgQuad;
            float bgX = contentStartX, bgY = steerBarY;
            applyOffset(bgX, bgY);
            setQuadPositions(barBgQuad, bgX, bgY, halfWidthAvail, steerBarHeight);
            barBgQuad.m_iSprite = PluginConstants::SpriteIndex::SOLID_COLOR;
            barBgQuad.m_ulColor = bgColor;
            m_quads.push_back(barBgQuad);
        }
        // Right bar background
        {
            SPluginQuad_t barBgQuad;
            float bgX = centerX + steerCenterGapHalf, bgY = steerBarY;
            applyOffset(bgX, bgY);
            setQuadPositions(barBgQuad, bgX, bgY, halfWidthAvail, steerBarHeight);
            barBgQuad.m_iSprite = PluginConstants::SpriteIndex::SOLID_COLOR;
            barBgQuad.m_ulColor = bgColor;
            m_quads.push_back(barBgQuad);
        }

        // Draw fill from the gap edge outward, based on steer direction (only when steer data available).
        // Fill width scales against halfWidthAvail so full lock fills the entire available half-bar.
        if (hasSteerData) {
            float steerRatio = steerAngle / maxSteer;
            steerRatio = std::max(-1.0f, std::min(1.0f, steerRatio));

            if (std::abs(steerRatio) > 0.01f) {
                float fillWidth = std::abs(steerRatio) * halfWidthAvail;
                float fillX = (steerRatio > 0)
                    ? ((centerX - steerCenterGapHalf) - fillWidth)  // left fill grows leftward from inner gap edge
                    : (centerX + steerCenterGapHalf);                // right fill grows rightward from inner gap edge
                SPluginQuad_t fillQuad;
                float fX = fillX, fY = steerBarY;
                applyOffset(fX, fY);
                setQuadPositions(fillQuad, fX, fY, fillWidth, steerBarHeight);
                fillQuad.m_iSprite = PluginConstants::SpriteIndex::SOLID_COLOR;
                fillQuad.m_ulColor = fillColorFor(steerRatio);
                m_quads.push_back(fillQuad);
            }
        }

        // Draw steer max markers (if enabled and steer data available)
        if ((m_bShowMaxMarkers || isCrashed) && hasSteerData) {
            float steerMaxMarkerWidth = contentWidth * 0.02f;
            unsigned long steerMaxMarkerColor = isCrashed
                ? this->getColor(ColorSlot::NEGATIVE)
                : this->getColor(ColorSlot::PRIMARY);

            if (m_steerFramesRemaining[0] > 0 && m_steerMarkerLeft > 0.5f) {
                float markerRatio = std::min(1.0f, m_steerMarkerLeft / maxSteer);
                float markerX = (centerX - steerCenterGapHalf) - markerRatio * halfWidthAvail - steerMaxMarkerWidth / 2.0f;
                SPluginQuad_t maxMarkerQuad;
                float mmX = markerX, mmY = steerBarY;
                applyOffset(mmX, mmY);
                setQuadPositions(maxMarkerQuad, mmX, mmY, steerMaxMarkerWidth, steerBarHeight);
                maxMarkerQuad.m_iSprite = PluginConstants::SpriteIndex::SOLID_COLOR;
                maxMarkerQuad.m_ulColor = steerMaxMarkerColor;
                m_quads.push_back(maxMarkerQuad);
            }

            if (m_steerFramesRemaining[1] > 0 && m_steerMarkerRight > 0.5f) {
                float markerRatio = std::min(1.0f, m_steerMarkerRight / maxSteer);
                float markerX = (centerX + steerCenterGapHalf) + markerRatio * halfWidthAvail - steerMaxMarkerWidth / 2.0f;
                SPluginQuad_t maxMarkerQuad;
                float mmX = markerX, mmY = steerBarY;
                applyOffset(mmX, mmY);
                setQuadPositions(maxMarkerQuad, mmX, mmY, steerMaxMarkerWidth, steerBarHeight);
                maxMarkerQuad.m_iSprite = PluginConstants::SpriteIndex::SOLID_COLOR;
                maxMarkerQuad.m_ulColor = steerMaxMarkerColor;
                m_quads.push_back(maxMarkerQuad);
            }
        }

        currentY += dim.lineHeightNormal;
    }

    // === Row 4: Steer Value (if enabled) ===
    if (m_enabledRows & ROW_STEER_VALUE) {
        char steerBuffer[16];
        unsigned long steerTextColor = textColor;
        if (!hasSteerData) {
            // Show N/A when spectating/replay (steer data structurally unavailable)
            snprintf(steerBuffer, sizeof(steerBuffer), "%s", Placeholders::NOT_AVAILABLE);
            steerTextColor = this->getColor(ColorSlot::MUTED);
        } else if (!bikeData.isValid) {
            snprintf(steerBuffer, sizeof(steerBuffer), "%s", Placeholders::GENERIC);
            steerTextColor = this->getColor(ColorSlot::MUTED);
        } else {
            float steerAngle = isCrashed ? m_frozenSteer : currentSteer;
            int displaySteer = static_cast<int>(std::abs(steerAngle) + 0.5f);
            snprintf(steerBuffer, sizeof(steerBuffer), "%d", displaySteer);
        }
        addString(steerBuffer, centerX, currentY, Justify::CENTER,
            this->getFont(FontCategory::DIGITS), steerTextColor, dim.fontSize);
        currentY += dim.lineHeightNormal;
    }
}

void LeanWidget::resetToDefaults() {
    m_bVisible = false;  // Disabled by default
    m_bShowTitle = false;  // No title for gauge widgets
    setTextureVariant(0);  // No texture by default
    m_fBackgroundOpacity = 1.0f;  // Full opacity (100%)
    m_fScale = 1.0f;
    m_enabledRows = ROW_DEFAULT;  // All rows enabled
    m_bShowMaxMarkers = true;     // Max markers ON by default for lean/steer
    m_maxMarkerLingerFrames = 60; // ~1 second at 60fps
    m_fillColorMode = FillColorMode::RAMP;
    setPosition(cellsX(122), cellsY(74));
    m_smoothedLean = 0.0f;
    m_markerValueLeft = 0.0f;
    m_markerValueRight = 0.0f;
    m_maxFramesRemaining[0] = 0;
    m_maxFramesRemaining[1] = 0;
    m_steerMarkerLeft = 0.0f;
    m_steerMarkerRight = 0.0f;
    m_steerFramesRemaining[0] = 0;
    m_steerFramesRemaining[1] = 0;
    m_wasCrashed = false;
    m_lastDisplayedRaceNum = -1;
    m_arcFillColor = DEFAULT_ARC_FILL_COLOR;
    setDataDirty();
}
