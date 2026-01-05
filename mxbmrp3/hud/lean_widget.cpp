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
           dataType == DataChangeType::SpectateTarget;
}

void LeanWidget::update() {
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
    m_strings.clear();
    m_quads.clear();

    auto dim = getScaledDimensions();

    const PluginData& pluginData = PluginData::getInstance();
    const BikeTelemetryData& bikeData = pluginData.getBikeTelemetry();

    // Steer data is only available when player is on track (not when spectating/replay)
    bool hasSteerData = (pluginData.getDrawState() == ViewState::ON_TRACK);

    float startX = 0.0f;
    float startY = 0.0f;

    // Use same width as SpeedWidget
    float backgroundWidth = calculateBackgroundWidth(WidgetDimensions::SPEED_WIDTH);
    float contentWidth = PluginUtils::calculateMonospaceTextWidth(WidgetDimensions::SPEED_WIDTH, dim.fontSize);

    // Calculate dynamic height based on enabled rows
    // Each row is lineHeightNormal (arc takes 2 rows)
    int rowCount = getRowCount();
    float contentHeight = dim.lineHeightNormal * rowCount;
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

    // Check for spectator switch - reset max values when switching viewed rider
    int currentDisplayRaceNum = pluginData.getDisplayRaceNum();
    if (m_lastDisplayedRaceNum != -1 && currentDisplayRaceNum != m_lastDisplayedRaceNum) {
        // Switched to viewing a different rider - reset all max tracking
        m_maxLeanLeft = 0.0f;
        m_maxLeanRight = 0.0f;
        m_markerValueLeft = 0.0f;
        m_markerValueRight = 0.0f;
        m_prevLeanLeft = 0.0f;
        m_prevLeanRight = 0.0f;
        m_maxFramesRemaining[0] = 0;
        m_maxFramesRemaining[1] = 0;
        m_steerMarkerLeft = 0.0f;
        m_steerMarkerRight = 0.0f;
        m_steerFramesRemaining[0] = 0;
        m_steerFramesRemaining[1] = 0;
        m_smoothedLean = 0.0f;
    }
    m_lastDisplayedRaceNum = currentDisplayRaceNum;

    // Check for crash recovery - reset max lean when recovering from crash
    const TrackPositionData* playerPos = pluginData.getPlayerTrackPosition();
    bool isCrashed = playerPos && playerPos->crashed;
    if (m_wasCrashed && !isCrashed) {
        // Just recovered from crash - reset max lean values and markers
        m_maxLeanLeft = 0.0f;
        m_maxLeanRight = 0.0f;
        m_markerValueLeft = 0.0f;
        m_markerValueRight = 0.0f;
        m_prevLeanLeft = 0.0f;
        m_prevLeanRight = 0.0f;
        m_maxFramesRemaining[0] = 0;
        m_maxFramesRemaining[1] = 0;
        // Reset steer markers too
        m_steerMarkerLeft = 0.0f;
        m_steerMarkerRight = 0.0f;
        m_steerFramesRemaining[0] = 0;
        m_steerFramesRemaining[1] = 0;
    }
    // Capture steer angle before updating crash state (for freezing)
    const HistoryBuffers& history = pluginData.getHistoryBuffers();
    float currentSteer = (!history.steer.empty()) ? history.steer.back() : 0.0f;

    // Freeze steer value when crash starts
    if (isCrashed && !m_wasCrashed) {
        m_frozenSteer = currentSteer;
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
        // Countdown timers when crashed
        if (m_steerFramesRemaining[0] > 0) {
            m_steerFramesRemaining[0]--;
            if (m_steerFramesRemaining[0] == 0) m_steerMarkerLeft = 0.0f;
        }
        if (m_steerFramesRemaining[1] > 0) {
            m_steerFramesRemaining[1]--;
            if (m_steerFramesRemaining[1] == 0) m_steerMarkerRight = 0.0f;
        }
    }

    // Get current lean angle from telemetry
    float currentLean = 0.0f;
    constexpr float LEAN_THRESHOLD = 1.0f;  // 1 degree threshold to avoid jitter

    if (bikeData.isValid && !isCrashed) {
        // Only update when not crashed
        currentLean = bikeData.roll;

        // Update maximum lean tracking
        if (currentLean < 0) {
            // Leaning left (negative)
            float absLean = std::abs(currentLean);
            if (absLean > m_maxLeanLeft) {
                m_maxLeanLeft = absLean;
            }
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
            m_prevLeanLeft = absLean;
            // Countdown right marker if showing (switched from right to left)
            if (m_maxFramesRemaining[1] > 0) {
                m_maxFramesRemaining[1]--;
                if (m_maxFramesRemaining[1] == 0) m_markerValueRight = 0.0f;
            }
        } else if (currentLean > 0) {
            // Leaning right (positive)
            if (currentLean > m_maxLeanRight) {
                m_maxLeanRight = currentLean;
            }
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
            m_prevLeanRight = currentLean;
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
    // When crashed, m_smoothedLean stays frozen at last value
    // Also countdown timers when crashed (so markers eventually disappear)
    else {
        if (m_maxFramesRemaining[0] > 0) {
            m_maxFramesRemaining[0]--;
            if (m_maxFramesRemaining[0] == 0) m_markerValueLeft = 0.0f;
        }
        if (m_maxFramesRemaining[1] > 0) {
            m_maxFramesRemaining[1]--;
            if (m_maxFramesRemaining[1] == 0) m_markerValueRight = 0.0f;
        }
    }

    // Common values used by multiple elements
    float barWidthRef = PluginUtils::calculateMonospaceTextWidth(1, dim.fontSize);
    unsigned long textColor = ColorConfig::getInstance().getPrimary();

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

        // Draw background arc (full gauge range) - same color as BarsWidget backgrounds
        unsigned long arcBgColor = PluginUtils::applyOpacity(ColorConfig::getInstance().getMuted(), m_fBackgroundOpacity * 0.5f);
        addArcSegment(centerX, arcCenterY, innerRadius, outerRadius,
                      arcStartRad, arcEndRad, arcBgColor, ARC_SEGMENTS);

        // Draw filled arc from center (0.0f) to current lean angle
        float displayLean = -m_smoothedLean;
        if (std::abs(displayLean) > 0.5f) {
            float leanRatio = displayLean / MAX_LEAN_ANGLE;
            float fillAngleRad = leanRatio * arcEndRad;

            if (fillAngleRad < arcStartRad) fillAngleRad = arcStartRad;
            if (fillAngleRad > arcEndRad) fillAngleRad = arcEndRad;

            int fillSegments = std::max(3, static_cast<int>(std::abs(fillAngleRad / (arcEndRad - arcStartRad)) * ARC_SEGMENTS));

            if (fillAngleRad < 0) {
                addArcSegment(centerX, arcCenterY, innerRadius, outerRadius,
                              fillAngleRad, 0.0f, m_arcFillColor, fillSegments);
            } else {
                addArcSegment(centerX, arcCenterY, innerRadius, outerRadius,
                              0.0f, fillAngleRad, m_arcFillColor, fillSegments);
            }
        }

        // Draw center marker (extends beyond arc)
        float markerInner = innerRadius - arcThickness * 0.5f;
        float markerOuter = outerRadius + arcThickness * 0.5f;
        float markerWidth = 0.02f;

        addArcSegment(centerX, arcCenterY, markerInner, markerOuter,
                      -markerWidth, markerWidth, ColorConfig::getInstance().getPrimary(), 1);

        // Draw lingering max markers on the arc (if enabled)
        if (m_bShowMaxMarkers) {
            unsigned long maxMarkerColor = ColorConfig::getInstance().getPrimary();

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
                Fonts::getNormal(), textColor, dim.fontSize);
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
            Fonts::getNormal(), textColor, dim.fontSize);
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

        // Draw full-width bar background - same color as BarsWidget backgrounds
        SPluginQuad_t barBgQuad;
        float bgX = contentStartX, bgY = steerBarY;
        applyOffset(bgX, bgY);
        setQuadPositions(barBgQuad, bgX, bgY, contentWidth, steerBarHeight);
        barBgQuad.m_iSprite = PluginConstants::SpriteIndex::SOLID_COLOR;
        barBgQuad.m_ulColor = PluginUtils::applyOpacity(ColorConfig::getInstance().getMuted(), m_fBackgroundOpacity * 0.5f);
        m_quads.push_back(barBgQuad);

        float halfWidth = contentWidth / 2.0f;

        // Draw fill from center outward based on steer direction (only when steer data available)
        if (hasSteerData) {
            float steerRatio = steerAngle / maxSteer;
            steerRatio = std::max(-1.0f, std::min(1.0f, steerRatio));

            if (std::abs(steerRatio) > 0.01f) {
                float fillWidth = std::abs(steerRatio) * halfWidth;
                float fillX = (steerRatio > 0) ? (centerX - fillWidth) : centerX;
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
        if (m_bShowMaxMarkers && hasSteerData) {
            float steerMaxMarkerWidth = contentWidth * 0.02f;
            unsigned long steerMaxMarkerColor = ColorConfig::getInstance().getPrimary();

            if (m_steerFramesRemaining[0] > 0 && m_steerMarkerLeft > 0.5f) {
                float markerRatio = std::min(1.0f, m_steerMarkerLeft / maxSteer);
                float markerX = centerX - markerRatio * halfWidth - steerMaxMarkerWidth / 2.0f;
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
                float markerX = centerX + markerRatio * halfWidth - steerMaxMarkerWidth / 2.0f;
                SPluginQuad_t maxMarkerQuad;
                float mmX = markerX, mmY = steerBarY;
                applyOffset(mmX, mmY);
                setQuadPositions(maxMarkerQuad, mmX, mmY, steerMaxMarkerWidth, steerBarHeight);
                maxMarkerQuad.m_iSprite = PluginConstants::SpriteIndex::SOLID_COLOR;
                maxMarkerQuad.m_ulColor = steerMaxMarkerColor;
                m_quads.push_back(maxMarkerQuad);
            }
        }

        // Draw center marker (thin, extends beyond bar)
        float steerCenterMarkerW = contentWidth * 0.02f;
        float steerCenterMarkerH = steerBarHeight * 1.5f;
        float steerCenterMarkerY = steerBarY - (steerCenterMarkerH - steerBarHeight) / 2.0f;
        SPluginQuad_t centerMarkerQuad;
        float cmX = centerX - steerCenterMarkerW / 2.0f, cmY = steerCenterMarkerY;
        applyOffset(cmX, cmY);
        setQuadPositions(centerMarkerQuad, cmX, cmY, steerCenterMarkerW, steerCenterMarkerH);
        centerMarkerQuad.m_iSprite = PluginConstants::SpriteIndex::SOLID_COLOR;
        centerMarkerQuad.m_ulColor = ColorConfig::getInstance().getPrimary();
        m_quads.push_back(centerMarkerQuad);

        currentY += dim.lineHeightNormal;
    }

    // === Row 4: Steer Value (if enabled) ===
    if (m_enabledRows & ROW_STEER_VALUE) {
        char steerBuffer[16];
        unsigned long steerTextColor = textColor;
        if (!hasSteerData) {
            // Show N/A when spectating/replay (steer data structurally unavailable)
            snprintf(steerBuffer, sizeof(steerBuffer), "%s", Placeholders::NOT_AVAILABLE);
            steerTextColor = ColorConfig::getInstance().getMuted();
        } else if (!bikeData.isValid) {
            snprintf(steerBuffer, sizeof(steerBuffer), "%s", Placeholders::GENERIC);
            steerTextColor = ColorConfig::getInstance().getMuted();
        } else {
            float steerAngle = isCrashed ? m_frozenSteer : currentSteer;
            int displaySteer = static_cast<int>(std::abs(steerAngle) + 0.5f);
            snprintf(steerBuffer, sizeof(steerBuffer), "%d", displaySteer);
        }
        addString(steerBuffer, centerX, currentY, Justify::CENTER,
            Fonts::getNormal(), steerTextColor, dim.fontSize);
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
    setPosition(0.715f, 0.8547f);  // Left of SpeedWidget
    m_smoothedLean = 0.0f;
    m_maxLeanLeft = 0.0f;
    m_maxLeanRight = 0.0f;
    m_markerValueLeft = 0.0f;
    m_markerValueRight = 0.0f;
    m_prevLeanLeft = 0.0f;
    m_prevLeanRight = 0.0f;
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
