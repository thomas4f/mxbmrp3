// ============================================================================
// hud/lean_widget.cpp
// Lean widget - displays bike lean/roll angle with half-donut arc gauge
// ============================================================================
#include "lean_widget.h"

#include <cstdio>
#include <cmath>

#include "../diagnostics/logger.h"
#include "../core/plugin_utils.h"
#include "../core/color_config.h"

using namespace PluginConstants;
using namespace PluginConstants::Math;

LeanWidget::LeanWidget()
{
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
    if (!isVisible()) {
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
    rebuildRenderData();
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

void LeanWidget::addArcSegment(float centerX, float centerY, float innerRadius, float outerRadius,
                                float startAngleRad, float endAngleRad, unsigned long color, int numSegments) {
    // Create arc segments as quads connecting inner and outer edges
    // Similar to MapHud ribbon rendering but for circular arcs

    float angleStep = (endAngleRad - startAngleRad) / numSegments;

    float prevInnerX = 0.0f, prevInnerY = 0.0f;
    float prevOuterX = 0.0f, prevOuterY = 0.0f;
    bool hasPrevPoint = false;

    for (int i = 0; i <= numSegments; ++i) {
        float angle = startAngleRad + i * angleStep;

        // Calculate inner and outer edge points
        // sin/cos because 0 degrees = up, positive = clockwise
        float innerX = centerX + std::sin(angle) * innerRadius / UI_ASPECT_RATIO;
        float innerY = centerY - std::cos(angle) * innerRadius;
        float outerX = centerX + std::sin(angle) * outerRadius / UI_ASPECT_RATIO;
        float outerY = centerY - std::cos(angle) * outerRadius;

        if (hasPrevPoint) {
            // Create quad connecting previous to current edges
            float screenPrevInnerX = prevInnerX, screenPrevInnerY = prevInnerY;
            float screenPrevOuterX = prevOuterX, screenPrevOuterY = prevOuterY;
            float screenInnerX = innerX, screenInnerY = innerY;
            float screenOuterX = outerX, screenOuterY = outerY;

            // Apply HUD offset
            applyOffset(screenPrevInnerX, screenPrevInnerY);
            applyOffset(screenPrevOuterX, screenPrevOuterY);
            applyOffset(screenInnerX, screenInnerY);
            applyOffset(screenOuterX, screenOuterY);

            // Create quad: prevOuter -> prevInner -> currInner -> currOuter (counter-clockwise to match engine)
            SPluginQuad_t quad;
            quad.m_aafPos[0][0] = screenPrevOuterX;
            quad.m_aafPos[0][1] = screenPrevOuterY;
            quad.m_aafPos[1][0] = screenPrevInnerX;
            quad.m_aafPos[1][1] = screenPrevInnerY;
            quad.m_aafPos[2][0] = screenInnerX;
            quad.m_aafPos[2][1] = screenInnerY;
            quad.m_aafPos[3][0] = screenOuterX;
            quad.m_aafPos[3][1] = screenOuterY;

            quad.m_iSprite = SpriteIndex::SOLID_COLOR;
            quad.m_ulColor = color;
            m_quads.push_back(quad);
        }

        // Store current as previous
        prevInnerX = innerX;
        prevInnerY = innerY;
        prevOuterX = outerX;
        prevOuterY = outerY;
        hasPrevPoint = true;
    }
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
    float backgroundWidth = calculateBackgroundWidth(WidgetDimensions::LEAN_WIDTH);
    float contentWidth = PluginUtils::calculateMonospaceTextWidth(WidgetDimensions::LEAN_WIDTH, dim.fontSize);

    // Calculate dynamic height based on enabled rows
    // Each row is lineHeightNormal (arc takes 2 rows)
    int rowCount = getRowCount();
    float titleHeight = m_bShowTitle ? dim.lineHeightNormal : 0.0f;
    float contentHeight = titleHeight + dim.lineHeightNormal * rowCount;
    float backgroundHeight = dim.paddingV + contentHeight + dim.paddingV;

    // Add background quad
    addBackgroundQuad(startX, startY, backgroundWidth, backgroundHeight);

    // Set bounds for drag detection
    setBounds(startX, startY, startX + backgroundWidth, startY + backgroundHeight);

    float contentStartX = startX + dim.paddingH;
    float contentStartY = startY + dim.paddingV;

    // Calculate center X for centering elements
    float centerX = contentStartX + (contentWidth / 2.0f);

    // Track current Y position for row-based layout
    float currentY = contentStartY;

    // Title label (optional)
    if (m_bShowTitle) {
        addString("Lean", contentStartX, currentY, Justify::LEFT,
            this->getFont(FontCategory::TITLE), this->getColor(ColorSlot::PRIMARY), dim.fontSize);
        currentY += titleHeight;
    }

    // Check for crash recovery - reset max lean when recovering from crash
    const TrackPositionData* playerPos = pluginData.getPlayerTrackPosition();
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
        if (currentSteer > 1.0f && m_steerFramesRemaining[0] == 0) {
            m_steerMarkerLeft = currentSteer;
            m_steerFramesRemaining[0] = m_maxMarkerLingerFrames;
        } else if (currentSteer < -1.0f && m_steerFramesRemaining[1] == 0) {
            m_steerMarkerRight = std::abs(currentSteer);
            m_steerFramesRemaining[1] = m_maxMarkerLingerFrames;
        }

        // Snap lean max marker on the side matching current lean (same gate semantics)
        if (bikeData.isValid) {
            float impactLean = bikeData.roll;
            if (impactLean < -0.5f && m_maxFramesRemaining[0] == 0) {
                m_markerValueLeft = std::abs(impactLean);
                m_maxFramesRemaining[0] = m_maxMarkerLingerFrames;
            } else if (impactLean > 0.5f && m_maxFramesRemaining[1] == 0) {
                m_markerValueRight = impactLean;
                m_maxFramesRemaining[1] = m_maxMarkerLingerFrames;
            }
        }

        // Collapse to a single marker per gauge so the freeze shows one peak, not two.
        // Higher linger count == more recently set.
        if (m_maxFramesRemaining[0] > 0 && m_maxFramesRemaining[1] > 0) {
            if (m_maxFramesRemaining[0] >= m_maxFramesRemaining[1]) {
                m_maxFramesRemaining[1] = 0;
                m_markerValueRight = 0.0f;
            } else {
                m_maxFramesRemaining[0] = 0;
                m_markerValueLeft = 0.0f;
            }
        }
        if (m_steerFramesRemaining[0] > 0 && m_steerFramesRemaining[1] > 0) {
            if (m_steerFramesRemaining[0] >= m_steerFramesRemaining[1]) {
                m_steerFramesRemaining[1] = 0;
                m_steerMarkerRight = 0.0f;
            } else {
                m_steerFramesRemaining[0] = 0;
                m_steerMarkerLeft = 0.0f;
            }
        }
    }
    m_wasCrashed = isCrashed;

    // Steer max marker tracking (similar to lean markers)
    constexpr float STEER_THRESHOLD = 1.0f;  // 1 degree threshold
    if (!isCrashed) {
        if (currentSteer > 0) {
            // Steering left (positive = left in this API)
            if (currentSteer > m_steerMarkerLeft + STEER_THRESHOLD) {
                m_steerMarkerLeft = currentSteer;
                m_steerFramesRemaining[0] = 0;
            } else if (currentSteer < m_steerMarkerLeft - STEER_THRESHOLD && m_steerFramesRemaining[0] == 0) {
                m_steerFramesRemaining[0] = m_maxMarkerLingerFrames;
            } else if (m_steerFramesRemaining[0] > 0) {
                m_steerFramesRemaining[0]--;
                if (m_steerFramesRemaining[0] == 0) m_steerMarkerLeft = 0.0f;
            }
            // Countdown right marker if showing (switched from right to left)
            if (m_steerFramesRemaining[1] > 0) {
                m_steerFramesRemaining[1]--;
                if (m_steerFramesRemaining[1] == 0) m_steerMarkerRight = 0.0f;
            }
        } else if (currentSteer < 0) {
            // Steering right (negative = right)
            float absSteer = std::abs(currentSteer);
            if (absSteer > m_steerMarkerRight + STEER_THRESHOLD) {
                m_steerMarkerRight = absSteer;
                m_steerFramesRemaining[1] = 0;
            } else if (absSteer < m_steerMarkerRight - STEER_THRESHOLD && m_steerFramesRemaining[1] == 0) {
                m_steerFramesRemaining[1] = m_maxMarkerLingerFrames;
            } else if (m_steerFramesRemaining[1] > 0) {
                m_steerFramesRemaining[1]--;
                if (m_steerFramesRemaining[1] == 0) m_steerMarkerRight = 0.0f;
            }
            // Countdown left marker if showing (switched from left to right)
            if (m_steerFramesRemaining[0] > 0) {
                m_steerFramesRemaining[0]--;
                if (m_steerFramesRemaining[0] == 0) m_steerMarkerLeft = 0.0f;
            }
        } else {
            // Near center - countdown both markers if showing
            if (m_steerFramesRemaining[0] > 0) {
                m_steerFramesRemaining[0]--;
                if (m_steerFramesRemaining[0] == 0) m_steerMarkerLeft = 0.0f;
            }
            if (m_steerFramesRemaining[1] > 0) {
                m_steerFramesRemaining[1]--;
                if (m_steerFramesRemaining[1] == 0) m_steerMarkerRight = 0.0f;
            }
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
        if (currentLean < 0) {
            // Leaning left (negative)
            float absLean = std::abs(currentLean);
            // Max marker: show at peak when value starts decreasing, hide when increasing
            if (absLean > m_markerValueLeft + LEAN_THRESHOLD) {
                // Value exceeds marker - update marker position, hide it
                m_markerValueLeft = absLean;
                m_maxFramesRemaining[0] = 0;
            } else if (absLean < m_markerValueLeft - LEAN_THRESHOLD && m_maxFramesRemaining[0] == 0) {
                // Value dropped below marker - start showing marker
                m_maxFramesRemaining[0] = m_maxMarkerLingerFrames;
            } else if (m_maxFramesRemaining[0] > 0) {
                m_maxFramesRemaining[0]--;
                if (m_maxFramesRemaining[0] == 0) m_markerValueLeft = 0.0f;
            }
            // Countdown right marker if showing (switched from right to left)
            if (m_maxFramesRemaining[1] > 0) {
                m_maxFramesRemaining[1]--;
                if (m_maxFramesRemaining[1] == 0) m_markerValueRight = 0.0f;
            }
        } else if (currentLean > 0) {
            // Leaning right (positive)
            // Max marker: show at peak when value starts decreasing, hide when increasing
            if (currentLean > m_markerValueRight + LEAN_THRESHOLD) {
                // Value exceeds marker - update marker position, hide it
                m_markerValueRight = currentLean;
                m_maxFramesRemaining[1] = 0;
            } else if (currentLean < m_markerValueRight - LEAN_THRESHOLD && m_maxFramesRemaining[1] == 0) {
                // Value dropped below marker - start showing marker
                m_maxFramesRemaining[1] = m_maxMarkerLingerFrames;
            } else if (m_maxFramesRemaining[1] > 0) {
                m_maxFramesRemaining[1]--;
                if (m_maxFramesRemaining[1] == 0) m_markerValueRight = 0.0f;
            }
            // Countdown left marker if showing (switched from left to right)
            if (m_maxFramesRemaining[0] > 0) {
                m_maxFramesRemaining[0]--;
                if (m_maxFramesRemaining[0] == 0) m_markerValueLeft = 0.0f;
            }
        } else {
            // Near center - countdown both markers if showing
            if (m_maxFramesRemaining[0] > 0) {
                m_maxFramesRemaining[0]--;
                if (m_maxFramesRemaining[0] == 0) m_markerValueLeft = 0.0f;
            }
            if (m_maxFramesRemaining[1] > 0) {
                m_maxFramesRemaining[1]--;
                if (m_maxFramesRemaining[1] == 0) m_markerValueRight = 0.0f;
            }
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
        m_markerValueLeft = 0.0f;
        m_markerValueRight = 0.0f;
        m_maxFramesRemaining[0] = 0;
        m_maxFramesRemaining[1] = 0;
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

        // Draw background arc (full gauge range, split around center) - same color as BarsWidget backgrounds
        unsigned long arcBgColor = PluginUtils::applyOpacity(this->getColor(ColorSlot::MUTED), m_fBackgroundOpacity * 0.5f);
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

            if (fillAngleRad < -CENTER_GAP_HALF_RAD) {
                addArcSegment(centerX, arcCenterY, innerRadius, outerRadius,
                              fillAngleRad, -CENTER_GAP_HALF_RAD, m_arcFillColor, fillSegments);
            } else if (fillAngleRad > CENTER_GAP_HALF_RAD) {
                addArcSegment(centerX, arcCenterY, innerRadius, outerRadius,
                              CENTER_GAP_HALF_RAD, fillAngleRad, m_arcFillColor, fillSegments);
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
        unsigned long bgColor = PluginUtils::applyOpacity(this->getColor(ColorSlot::MUTED), m_fBackgroundOpacity * 0.5f);

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
                fillQuad.m_ulColor = m_arcFillColor;
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
    setPosition(0.7425f, 0.8769f);
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
