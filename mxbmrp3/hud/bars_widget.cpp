// ============================================================================
// hud/bars_widget.cpp
// Bars Widget - displays up to 8 vertical bars (left to right):
//   - T: Throttle (green)
//   - B: Brakes (split: red front | dark red rear)
//   - C: Clutch (blue)
//   - R: RPM (gray)
//   - S: Suspension (split: purple front | dark purple rear)
//   - F: Fuel (yellow)
//   - E: Engine temperature (gradient: blue/green/red based on optimal range)
//   - W: Water temperature (gradient: blue/green/red based on optimal range)
// ============================================================================
#include "bars_widget.h"
#include "../core/plugin_utils.h"
#include "../core/color_config.h"
#include "../diagnostics/logger.h"
#include <algorithm>
#include <cstdio>

using namespace PluginConstants;

namespace {
    // Padding (in temperature units) added below alarmLow and above alarmHigh
    // so the bar shows some headroom on both ends of the normalized range.
    constexpr float TEMP_BAR_PADDING = 20.0f;

    // Threshold for hiding a max/threshold marker — values below this are effectively zero
    // (avoids rendering a sliver at the bottom of an empty bar).
    constexpr float MARKER_RENDER_GATE = 0.01f;

    // Marker line height as a fraction of bar height (thin horizontal line).
    constexpr float MARKER_HEIGHT_RATIO = 0.02f;
}

BarsWidget::BarsWidget() {
    m_panelKind = PanelKind::Widget;
    m_bContentCard = true;
    // One-time setup
    DEBUG_INFO("BarsWidget created");
    setDraggable(true);
    m_quads.reserve(48);   // 1 background + bar quads (single/split × empty+filled) + gap-split temp bars + max markers
    m_strings.reserve(8);  // 8 labels: T, B, C, R, S, F, E, W

    // Set texture base name for dynamic texture discovery
    setTextureBaseName("bars_widget");

    // Set all configurable defaults
    resetToDefaults();

    rebuildRenderData();
}

void BarsWidget::update() {
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

    // Always rebuild - telemetry updates at physics rate (100Hz)
    rebuildAndRecord();
    clearDataDirty();
    clearLayoutDirty();
}

bool BarsWidget::handlesDataType(DataChangeType dataType) const {
    return dataType == DataChangeType::InputTelemetry;
}

void BarsWidget::rebuildLayout() {
    // Widget always rebuilds every frame (see update()), so rebuildLayout just delegates to full rebuild
    // This is acceptable because the widget is lightweight and the split bar logic is complex
    rebuildRenderData();
}

void BarsWidget::rebuildRenderData() {
    m_quads.clear();
    clearStrings();

    const auto dims = getScaledDimensions();
    const PluginData& pluginData = PluginData::getInstance();
    const BikeTelemetryData& bikeTelemetry = pluginData.getBikeTelemetry();
    const InputTelemetryData& inputTelemetry = pluginData.getInputTelemetry();
    const SessionData& sessionData = pluginData.getSessionData();

    // Full telemetry data (rear brake, clutch, suspension, fuel) is ONLY available when ON_TRACK
    // because RunTelemetry() callback only fires when player is on track.
    // During SPECTATE/REPLAY, only limited RaceVehicleData is available (throttle, front brake, RPM, gear).
    bool hasFullTelemetry = (pluginData.getDrawState() == PluginConstants::ViewState::ON_TRACK);

    // When crashed, peak markers freeze (see updateMaxTracking) so the rider can read
    // the pre-crash peaks after the tumble settles. Markers also render in NEGATIVE
    // (red) during crash so they pop visually against the bar fills, and on the rising
    // edge of the crash state any markers that weren't already visible snap to the
    // current bar value so impact-moment telemetry is captured.
    const RiderTrackState* playerPos = pluginData.getPlayerTrackPosition();
    bool isCrashed = playerPos && playerPos->crashed;
    bool crashJustStarted = isCrashed && !m_wasCrashed;
    m_wasCrashed = isCrashed;
    unsigned long maxMarkerColor = isCrashed
        ? this->getColor(ColorSlot::NEGATIVE)
        : this->getColor(ColorSlot::PRIMARY);

    // Calculate bar dimensions
    float barWidth = PluginUtils::calculateMonospaceTextWidth(BAR_WIDTH_CHARS, dims.fontSize);
    float halfBarWidth = barWidth * 0.5f;  // For split bars (FBR/RBR, FSU/RSU)
    float barSpacing = PluginUtils::calculateMonospaceTextWidth(1, dims.fontSize) * BAR_SPACING_CHARS;  // 0.5 char spacing
    // THREE LINES, like every gauge beside it, with the letters on the same line
    // as a neighbouring compass's cardinals.
    //
    // The label INK is pinned to the bottom of the content box, and the compass
    // insets its cardinal ring by half a cap's ink so its letters reach the rim
    // of an identical three-line box. Both therefore land on `section -
    // capInk/2` without either widget knowing the other's numbers -- they agree
    // because they measure from the same edge with the same constant.
    //
    // THE GAP IS FREE. Pinning the ink to the bottom edge fixes its centre, so
    // the breathing room under the bars comes out of BAR HEIGHT and moves
    // nothing else -- which is why it can be tuned by eye without breaking the
    // alignment. The row reserves ink + gap and no ascender/descender slack:
    // these are fixed capitals, and the slack a text row normally keeps is what
    // made this widget stand taller than its neighbours.
    const float sectionHeight = CONTENT_LINES * dims.lineHeightNormal;
    const float labelInk = dims.fontSizeSmall * WidgetDimensions::CAP_INK_RATIO;
    const float labelGap = labelInk * 0.35f;
    const float labelRowHeight = m_bShowLabels ? labelInk + labelGap : 0.0f;
    const float barHeight = sectionHeight - labelRowHeight;

    // Count enabled bars to calculate width dynamically
    int enabledBarCount = 0;
    if (m_enabledColumns & COL_THROTTLE) enabledBarCount++;
    if (m_enabledColumns & COL_BRAKE) enabledBarCount++;
    if (m_enabledColumns & COL_CLUTCH) enabledBarCount++;
    if (m_enabledColumns & COL_RPM) enabledBarCount++;
    if (m_enabledColumns & COL_SUSPENSION) enabledBarCount++;
    if (m_enabledColumns & COL_FUEL) enabledBarCount++;
    if (m_enabledColumns & COL_ENGINE_TEMP) enabledBarCount++;
    if (m_enabledColumns & COL_WATER_TEMP) enabledBarCount++;

    // Calculate dynamic width based on enabled bars
    float barsWidth = enabledBarCount > 0
        ? (enabledBarCount * barWidth) + ((enabledBarCount - 1) * barSpacing)
        : 0.0f;
    // BOX-MODEL: the plan owns padding, chrome, the title band and the body card
    // (it pads BOTH ends and ceils the height itself, so the hand-rolled height
    // this replaced -- and the top-only-padding bug it carried -- is gone).
    // The label row is part of sectionHeight, so the box measures it like any
    // other content -- no drawing outside what the plan was told about.
    BaseHud::PanelWant want;
    want.contentW = barsWidth;
    want.sectionH = { sectionHeight };
    want.captionW = planTitleWidth(dims, "Bars");
    PanelPlan& p = planPanel(dims, want);
    const float backgroundWidth = p.width();
    const float backgroundHeight = p.height();

    setBounds(START_X, START_Y, START_X + backgroundWidth, START_Y + backgroundHeight);

    // Background, title band and body card at the plan's coordinates
    addPlanBackground(p, START_X, START_Y);
    addPlanTitle(p, "Bars", this->getFont(FontCategory::TITLE),
        this->getColor(ColorSlot::PRIMARY));

    // CENTRED ON THE CARD, like the Lean and G-force gauges beside it
    // (PanelPlan::sectionBoxCenterX). The panel's centre is the same place only
    // while the [content] terms are left/right symmetric -- `margin = 4 6 8 0`
    // moved it outside the card and the bars followed it off.
    //
    // barsWidth, not contentW(): they are the same number (the bars ARE this panel's
    // content ask) but the block being centred is the bars, and saying so is what keeps
    // this true if the panel ever gains a wider row.
    float contentStartX = p.sectionBoxCenterX() - barsWidth / 2.0f;
    float contentStartY = p.contentY();
    // The letters, INK-CENTRED IN THEIR OWN ROW at the bottom of the content --
    // a row the bars gave up, so it is inside the card and cannot collide with a
    // theme's frame. SMALL font, because the row is one small line: a normal-size
    // cap inks 0.63 of its em and would fill it edge to edge.
    const float labelFontSize = dims.fontSizeSmall;
    // Ink-centred in the INK BAND ALONE, below the gap -- so the ink fills
    // [barsBottom + gap, contentBottom] exactly and its centre is the mark the
    // compass's cardinals land on.
    const float labelBandTop = contentStartY + barHeight + labelGap;
    const float labelTextY = inkCenteredY(labelBandTop, labelInk, labelFontSize);

    // Get current values - throttle and front brake always available from inputTelemetry
    // (history buffers are only populated when TelemetryHud is visible)
    float throttleValue = inputTelemetry.throttle;
    float frontBrakeValue = inputTelemetry.frontBrake;

    // Rear brake (only available when ON_TRACK - show 0 when spectating/replay)
    float rearBrakeValue = hasFullTelemetry ? inputTelemetry.rearBrake : 0.0f;

    // Clutch (only available when ON_TRACK - show 0 when spectating/replay)
    float clutchValue = hasFullTelemetry ? inputTelemetry.clutch : 0.0f;

    // RPM normalized to 0-1 range (always available)
    float rpmValue = 0.0f;
    int rpm = std::max(0, bikeTelemetry.rpm);
    int limiterRPM = sessionData.limiterRPM;
    rpmValue = (limiterRPM > 0) ? static_cast<float>(rpm) / limiterRPM : 0.0f;

    // Fuel normalized to 0-1 range (only available when ON_TRACK)
    float fuelValue = 0.0f;
    if (hasFullTelemetry && bikeTelemetry.maxFuel > 0) {
        fuelValue = bikeTelemetry.fuel / bikeTelemetry.maxFuel;
    }

    // Suspension compression normalized to 0-1 range (only available when ON_TRACK)
    float frontSuspValue = 0.0f;
    float rearSuspValue = 0.0f;
    if (hasFullTelemetry) {
        if (bikeTelemetry.frontSuspMaxTravel > 0.0f) {
            frontSuspValue = 1.0f - (bikeTelemetry.frontSuspLength / bikeTelemetry.frontSuspMaxTravel);
        }
        if (bikeTelemetry.rearSuspMaxTravel > 0.0f) {
            rearSuspValue = 1.0f - (bikeTelemetry.rearSuspLength / bikeTelemetry.rearSuspMaxTravel);
        }
    }

    // Bar colors - use muted gray when data unavailable
    unsigned long mutedColor = this->getColor(ColorSlot::MUTED);
    unsigned long throttleColor = SemanticColors::THROTTLE;       // Green (always available)
    unsigned long frontBrakeColor = SemanticColors::FRONT_BRAKE;  // Red (always available)
    unsigned long rearBrakeColor = hasFullTelemetry ? SemanticColors::REAR_BRAKE : mutedColor;
    unsigned long clutchColor = hasFullTelemetry ? SemanticColors::CLUTCH : mutedColor;
    unsigned long rpmColor = ColorPalette::GRAY;                  // Gray (always available)
    unsigned long fuelColor = hasFullTelemetry ? ColorPalette::BRIGHT_YELLOW : mutedColor;
    unsigned long frontSuspColor = hasFullTelemetry ? SemanticColors::FRONT_SUSP : mutedColor;
    unsigned long rearSuspColor = hasFullTelemetry ? SemanticColors::REAR_SUSP : mutedColor;

    // Render enabled bars (dynamically positioned)
    float currentX = contentStartX;

    // Bar 0: Throttle (T) - single bar
    if (m_enabledColumns & COL_THROTTLE) {
        updateMaxTracking(0, throttleValue, isCrashed, crashJustStarted);
        addVerticalBar(currentX, contentStartY, barWidth, barHeight, throttleValue, throttleColor);
        if ((m_bShowMaxMarkers || isCrashed) && m_maxFramesRemaining[0] > 0) {
            addMaxMarker(currentX, contentStartY, barWidth, barHeight, m_markerValues[0], maxMarkerColor);
        }
        if (m_bShowLabels) {
            addString("T", currentX + barWidth / 2.0f, labelTextY, Justify::CENTER,
                      this->getFont(FontCategory::STRONG), this->getColor(ColorSlot::TERTIARY), labelFontSize);
        }
        currentX += barWidth + barSpacing;
    }

    // Bar 1: Brake (B) - split into FBR | RBR when both available, full width FBR when rear unavailable
    if (m_enabledColumns & COL_BRAKE) {
        // Track max of both brakes (use highest value)
        float maxBrakeValue = std::max(frontBrakeValue, rearBrakeValue);
        updateMaxTracking(1, maxBrakeValue, isCrashed, crashJustStarted);
        if (hasFullTelemetry) {
            // Split bar: front brake (left) | rear brake (right)
            addVerticalBar(currentX, contentStartY, halfBarWidth, barHeight, frontBrakeValue, frontBrakeColor);
            addVerticalBar(currentX + halfBarWidth, contentStartY, halfBarWidth, barHeight, rearBrakeValue, rearBrakeColor);
        } else {
            // Full width: only front brake available
            addVerticalBar(currentX, contentStartY, barWidth, barHeight, frontBrakeValue, frontBrakeColor);
        }
        if ((m_bShowMaxMarkers || isCrashed) && m_maxFramesRemaining[1] > 0) {
            addMaxMarker(currentX, contentStartY, barWidth, barHeight, m_markerValues[1], maxMarkerColor);
        }
        if (m_bShowLabels) {
            addString("B", currentX + barWidth / 2.0f, labelTextY, Justify::CENTER,
                      this->getFont(FontCategory::STRONG), this->getColor(ColorSlot::TERTIARY), labelFontSize);
        }
        currentX += barWidth + barSpacing;
    }

    // Bar 2: Clutch (C) - single bar (muted when unavailable)
    if (m_enabledColumns & COL_CLUTCH) {
        updateMaxTracking(2, clutchValue, isCrashed, crashJustStarted);
        addVerticalBar(currentX, contentStartY, barWidth, barHeight, clutchValue, clutchColor);
        if ((m_bShowMaxMarkers || isCrashed) && m_maxFramesRemaining[2] > 0) {
            addMaxMarker(currentX, contentStartY, barWidth, barHeight, m_markerValues[2], maxMarkerColor);
        }
        if (m_bShowLabels) {
            addString("C", currentX + barWidth / 2.0f, labelTextY, Justify::CENTER,
                      this->getFont(FontCategory::STRONG), hasFullTelemetry ? this->getColor(ColorSlot::TERTIARY) : mutedColor, labelFontSize);
        }
        currentX += barWidth + barSpacing;
    }

    // Bar 3: RPM (R) - single bar
    if (m_enabledColumns & COL_RPM) {
        updateMaxTracking(3, rpmValue, isCrashed, crashJustStarted);
        addVerticalBar(currentX, contentStartY, barWidth, barHeight, rpmValue, rpmColor);
        if ((m_bShowMaxMarkers || isCrashed) && m_maxFramesRemaining[3] > 0) {
            addMaxMarker(currentX, contentStartY, barWidth, barHeight, m_markerValues[3], maxMarkerColor);
        }
        if (m_bShowLabels) {
            addString("R", currentX + barWidth / 2.0f, labelTextY, Justify::CENTER,
                      this->getFont(FontCategory::STRONG), this->getColor(ColorSlot::TERTIARY), labelFontSize);
        }
        currentX += barWidth + barSpacing;
    }

    // Bar 4: Suspension (S) - split into FSU | RSU (muted when unavailable)
    if (m_enabledColumns & COL_SUSPENSION) {
        // Track max of both suspension values (use highest)
        float maxSuspValue = std::max(frontSuspValue, rearSuspValue);
        updateMaxTracking(4, maxSuspValue, isCrashed, crashJustStarted);
        addVerticalBar(currentX, contentStartY, halfBarWidth, barHeight, frontSuspValue, frontSuspColor);
        addVerticalBar(currentX + halfBarWidth, contentStartY, halfBarWidth, barHeight, rearSuspValue, rearSuspColor);
        if ((m_bShowMaxMarkers || isCrashed) && m_maxFramesRemaining[4] > 0) {
            addMaxMarker(currentX, contentStartY, barWidth, barHeight, m_markerValues[4], maxMarkerColor);
        }
        if (m_bShowLabels) {
            addString("S", currentX + barWidth / 2.0f, labelTextY, Justify::CENTER,
                      this->getFont(FontCategory::STRONG), hasFullTelemetry ? this->getColor(ColorSlot::TERTIARY) : mutedColor, labelFontSize);
        }
        currentX += barWidth + barSpacing;
    }

    // Bar 5: Fuel (F) - single bar (muted when unavailable)
    if (m_enabledColumns & COL_FUEL) {
        updateMaxTracking(5, fuelValue, isCrashed, crashJustStarted);
        addVerticalBar(currentX, contentStartY, barWidth, barHeight, fuelValue, fuelColor);
        if ((m_bShowMaxMarkers || isCrashed) && m_maxFramesRemaining[5] > 0) {
            addMaxMarker(currentX, contentStartY, barWidth, barHeight, m_markerValues[5], maxMarkerColor);
        }
        if (m_bShowLabels) {
            addString("F", currentX + barWidth / 2.0f, labelTextY, Justify::CENTER,
                      this->getFont(FontCategory::STRONG), hasFullTelemetry ? this->getColor(ColorSlot::TERTIARY) : mutedColor, labelFontSize);
        }
        currentX += barWidth + barSpacing;
    }

    // Engine and water temp bars share thresholds (API doesn't expose separate water alarm/optimal values).
    const float tempOpt = sessionData.engineOptTemperature;
    const float tempAlarmLow = sessionData.engineTempAlarmLow;
    const float tempAlarmHigh = sessionData.engineTempAlarmHigh;
    const float labelY = labelTextY;

    // Bar 6: Engine Temperature (E)
    if (m_enabledColumns & COL_ENGINE_TEMP) {
        renderTemperatureBar(6, currentX, contentStartY, labelY, barWidth, barHeight,
                             bikeTelemetry.engineTemperature, tempOpt, tempAlarmLow, tempAlarmHigh,
                             hasFullTelemetry, isCrashed, crashJustStarted,
                             maxMarkerColor, mutedColor, "E", labelFontSize);
        currentX += barWidth + barSpacing;
    }

    // Bar 7: Water Temperature (W)
    if (m_enabledColumns & COL_WATER_TEMP) {
        renderTemperatureBar(7, currentX, contentStartY, labelY, barWidth, barHeight,
                             bikeTelemetry.waterTemperature, tempOpt, tempAlarmLow, tempAlarmHigh,
                             hasFullTelemetry, isCrashed, crashJustStarted,
                             maxMarkerColor, mutedColor, "W", labelFontSize);
        currentX += barWidth + barSpacing;
    }
}

void BarsWidget::renderTemperatureBar(int barIndex,
                                      float x, float y, float labelY,
                                      float barWidth, float barHeight,
                                      float temp, float optTemp, float alarmLow, float alarmHigh,
                                      bool hasFullTelemetry, bool isCrashed, bool crashJustStarted,
                                      unsigned long maxMarkerColor, unsigned long mutedColor,
                                      const char* label, float fontSize) {
    // Normalize temperature to 0-1 using alarm range with padding on both ends.
    const float tempMin = alarmLow - TEMP_BAR_PADDING;
    const float tempMax = alarmHigh + TEMP_BAR_PADDING;
    const float tempRange = tempMax - tempMin;
    float tempNorm = 0.0f;
    if (tempRange > 0.0f) {
        tempNorm = std::max(0.0f, std::min(1.0f, (temp - tempMin) / tempRange));
    }

    const unsigned long tempColor = hasFullTelemetry
        ? calculateTemperatureColor(temp, optTemp, alarmLow, alarmHigh)
        : mutedColor;

    updateMaxTracking(barIndex, tempNorm, isCrashed, crashJustStarted);

    // Thresholds (alarm low, optimal, alarm high) are shown as gaps cut out of the bar
    // — revealing the panel background — rather than black lines drawn over it. Matches
    // the gap-based divider style used by the LeanWidget arc/steer gauges.
    if (tempRange > 0.0f) {
        const float gaps[3] = {
            (alarmLow - tempMin) / tempRange,
            (optTemp - tempMin) / tempRange,
            (alarmHigh - tempMin) / tempRange
        };
        addVerticalBarWithGaps(x, y, barWidth, barHeight, tempNorm, tempColor, gaps, 3);
    } else {
        addVerticalBar(x, y, barWidth, barHeight, tempNorm, tempColor);
    }

    if ((m_bShowMaxMarkers || isCrashed) && m_maxFramesRemaining[barIndex] > 0) {
        addMaxMarker(x, y, barWidth, barHeight, m_markerValues[barIndex], maxMarkerColor);
    }

    if (m_bShowLabels) {
        addString(label, x + barWidth / 2.0f, labelY, Justify::CENTER,
                  this->getFont(FontCategory::STRONG),
                  hasFullTelemetry ? this->getColor(ColorSlot::TERTIARY) : mutedColor, fontSize);
    }
}

void BarsWidget::updateMaxTracking(int barIndex, float currentValue, bool isCrashed, bool justEnteredCrash) {
    if (barIndex < 0 || barIndex >= NUM_BARS) return;

    // On the first frame of the crash, force the marker to render at the current
    // value if no marker is currently being drawn (linger inactive). Captures impact-
    // moment telemetry even when the rider was holding a steady value pre-crash
    // (which keeps m_markerValues at the held value but never arms the linger; the
    // render gate is m_maxFramesRemaining > 0). Doesn't touch a still-rendering
    // higher peak.
    if (justEnteredCrash && m_maxFramesRemaining[barIndex] == 0 && currentValue >= 0.01f) {
        m_markerValues[barIndex] = currentValue;
        m_maxFramesRemaining[barIndex] = m_maxMarkerLingerFrames;
    }

    // While crashed, freeze marker + linger so the rider can read the pre-crash peak
    // after the tumble settles.
    if (isCrashed) {
        return;
    }

    // Max marker: show at peak when value starts decreasing, hide when increasing
    // Use small threshold to avoid jitter from noise
    constexpr float THRESHOLD = 0.02f;

    PeakMarker::advanceActive(m_markerValues[barIndex], m_maxFramesRemaining[barIndex],
                              currentValue, THRESHOLD, m_maxMarkerLingerFrames);
}

void BarsWidget::addMaxMarker(float x, float y, float barWidth, float barHeight, float maxValue, unsigned long color) {
    // Draw a thin horizontal line at the max value position
    maxValue = std::max(0.0f, std::min(1.0f, maxValue));
    if (maxValue < MARKER_RENDER_GATE) return;  // Don't draw if max is essentially zero

    float markerHeight = barHeight * MARKER_HEIGHT_RATIO;
    float markerY = y + barHeight * (1.0f - maxValue) - markerHeight * 0.5f;

    SPluginQuad_t markerQuad;
    float markerX = x;
    applyOffset(markerX, markerY);
    setQuadPositions(markerQuad, markerX, markerY, barWidth, markerHeight);
    markerQuad.m_iSprite = PluginConstants::SpriteIndex::SOLID_COLOR;
    markerQuad.m_ulColor = color;
    m_quads.push_back(markerQuad);
}

void BarsWidget::addVerticalBarWithGaps(float x, float y, float barWidth, float barHeight,
                                        float value, unsigned long color,
                                        const float* gaps, int gapCount) {
    value = std::max(0.0f, std::min(1.0f, value));

    // Gap half-height in normalized (0=bottom, 1=top) units; matches the old threshold
    // marker thickness so the divider stays the same visual size.
    const float gapHalf = MARKER_HEIGHT_RATIO * 0.5f;

    // Bar background is part of the gauge readout, not the panel backdrop — keep it at a
    // fixed 50% so it stays legible regardless of the widget's background-opacity slider
    // (only addBackgroundQuad responds to that). Matches the GForceWidget ring.
    const unsigned long emptyColor = PluginUtils::applyOpacity(this->getColor(ColorSlot::MUTED), 0.5f);
    const unsigned long filledColor = PluginUtils::applyOpacity(color, 1.0f);

    // Emit one quad spanning the normalized vertical range [v0, v1] (v0 < v1).
    auto pushQuad = [&](float v0, float v1, unsigned long col) {
        if (v1 - v0 <= 0.001f) return;
        SPluginQuad_t quad;
        float qx = x, qy = y + barHeight * (1.0f - v1);
        applyOffset(qx, qy);
        setQuadPositions(quad, qx, qy, barWidth, barHeight * (v1 - v0));
        quad.m_iSprite = PluginConstants::SpriteIndex::SOLID_COLOR;
        quad.m_ulColor = col;
        m_quads.push_back(quad);
    };

    // Draw a solid (non-gap) band [v0, v1], split at the fill boundary into filled/empty.
    auto drawBand = [&](float v0, float v1) {
        const float fillTop = std::min(v1, value);
        pushQuad(v0, fillTop, filledColor);
        const float emptyBot = std::max(v0, value);
        pushQuad(emptyBot, v1, emptyColor);
    };

    // Sort gap positions ascending so the bottom-to-top walk skips them in order.
    float sortedGaps[8];
    int n = std::min(gapCount, 8);
    for (int i = 0; i < n; ++i) sortedGaps[i] = std::max(0.0f, std::min(1.0f, gaps[i]));
    std::sort(sortedGaps, sortedGaps + n);

    // Walk bottom (0) to top (1), drawing solid bands between the gap cut-outs.
    float cursor = 0.0f;
    for (int i = 0; i < n; ++i) {
        const float gLow = std::max(0.0f, sortedGaps[i] - gapHalf);
        const float gHigh = std::min(1.0f, sortedGaps[i] + gapHalf);
        if (gLow > cursor) drawBand(cursor, gLow);
        cursor = std::max(cursor, gHigh);
    }
    if (cursor < 1.0f) drawBand(cursor, 1.0f);
}

void BarsWidget::addVerticalBar(float x, float y, float barWidth, float barHeight,
                                          float value, unsigned long color) {
    // Clamp value to 0-1 range
    value = std::max(0.0f, std::min(1.0f, value));

    // Calculate filled and empty heights
    float filledHeight = barHeight * value;
    float emptyHeight = barHeight - filledHeight;

    // Empty portion (top) - darker gray
    if (emptyHeight > 0.001f) {
        SPluginQuad_t emptyQuad;
        float emptyX = x, emptyY = y;
        applyOffset(emptyX, emptyY);
        setQuadPositions(emptyQuad, emptyX, emptyY, barWidth, emptyHeight);
        emptyQuad.m_iSprite = PluginConstants::SpriteIndex::SOLID_COLOR;

        // Fixed 50% — the empty bar is part of the gauge, not the panel backdrop, so it
        // doesn't follow the background-opacity slider (see addVerticalBarWithGaps).
        emptyQuad.m_ulColor = PluginUtils::applyOpacity(this->getColor(ColorSlot::MUTED), 0.5f);

        m_quads.push_back(emptyQuad);
    }

    // Filled portion (bottom) - colored
    if (filledHeight > 0.001f) {
        SPluginQuad_t filledQuad;
        float filledX = x, filledY = y + emptyHeight;
        applyOffset(filledX, filledY);
        setQuadPositions(filledQuad, filledX, filledY, barWidth, filledHeight);
        filledQuad.m_iSprite = PluginConstants::SpriteIndex::SOLID_COLOR;

        // Apply full opacity to filled portion
        filledQuad.m_ulColor = PluginUtils::applyOpacity(color, 1.0f);

        m_quads.push_back(filledQuad);
    }
}

void BarsWidget::resetToDefaults() {
    m_bVisible = false;
    m_bShowTitle = false;  // No title by default
    setTextureVariant(0);  // No texture by default
    m_fBackgroundOpacity = 1.0f;  // Full opacity
    m_fScale = 1.0f;
    setPosition(cellsX(109), cellsY(74));
#if GAME_HAS_TYRE_TEMP
    // GP Bikes: include engine temp by default (has reliable temp data)
    m_enabledColumns = COL_DEFAULT | COL_ENGINE_TEMP;
#else
    m_enabledColumns = COL_DEFAULT;
#endif
    m_bShowLabels = true;  // Labels ON by default
    m_bShowMaxMarkers = false;  // Max markers OFF by default
    m_maxMarkerLingerFrames = 60;  // ~1 second at 60fps

    // Reset max tracking state
    for (int i = 0; i < NUM_BARS; ++i) {
        m_markerValues[i] = 0.0f;
        m_maxFramesRemaining[i] = 0;
    }
    m_wasCrashed = false;

    setDataDirty();
}
