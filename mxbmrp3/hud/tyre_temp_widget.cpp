// ============================================================================
// hud/tyre_temp_widget.cpp
// Tyre Temperature Widget - displays tyre temperatures for front and rear wheels
// GP Bikes only - shows left/middle/right tread temperatures as colored blocks
// ============================================================================
#include "tyre_temp_widget.h"

#if GAME_HAS_TYRE_TEMP

#include <cstdio>
#include <algorithm>

#include "../diagnostics/logger.h"
#include "../core/plugin_utils.h"
#include "../core/color_config.h"

using namespace PluginConstants;

TyreTempWidget::TyreTempWidget() {
    DEBUG_INFO("TyreTempWidget created");
    setDraggable(true);

    // Reserve space for render data:
    // - 1 background quad
    // - 6 temp bars (3 per wheel x 2 wheels)
    // - 6 temp bar backgrounds
    m_quads.reserve(13);

    // Reserve space for strings:
    // - 3 L/M/R section labels (shared across both wheels)
    // - 2 F/R wheel labels (one per row, in the left padding)
    // - 6 temperature values
    m_strings.reserve(11);

    // Set texture base name for dynamic texture discovery
    setTextureBaseName("tyre_temp_widget");

    // Set all configurable defaults
    resetToDefaults();

    rebuildRenderData();
}

bool TyreTempWidget::handlesDataType(DataChangeType dataType) const {
    // Update on telemetry changes (tyre data)
    return dataType == DataChangeType::InputTelemetry ||
           dataType == DataChangeType::SpectateTarget;
}

void TyreTempWidget::update() {
    // OPTIMIZATION: Skip processing when not visible
    if (!isVisibleAnySurface()) {
        clearDataDirty();
        clearLayoutDirty();
        return;
    }

    // Always rebuild - tyre temps update at telemetry rate
    rebuildRenderData();
    clearDataDirty();
    clearLayoutDirty();
}

void TyreTempWidget::rebuildLayout() {
    // Fast path - for this widget, full rebuild is still cheap
    rebuildRenderData();
}

void TyreTempWidget::rebuildRenderData() {
    clearStrings();
    m_quads.clear();

    auto dim = getScaledDimensions();

    const PluginData& pluginData = PluginData::getInstance();
    const BikeTelemetryData& bikeData = pluginData.getBikeTelemetry();

    // Tyre temp data is only available when player is on track
    bool onTrack = (pluginData.getDrawState() == ViewState::ON_TRACK);
    bool hasData = onTrack && bikeData.isValid;

    float startX = 0.0f;
    float startY = 0.0f;

    // Widget width: match BarsWidget's 6-bar GP Bikes default (6*1 + 5*0.4 = 8 chars)
    constexpr int WIDGET_WIDTH_CHARS = 8;
    float backgroundWidth = calculateBackgroundWidth(WIDGET_WIDTH_CHARS);
    float contentWidth = PluginUtils::calculateMonospaceTextWidth(WIDGET_WIDTH_CHARS, dim.fontSize);

    bool showBars = (m_enabledRows & ROW_BARS) != 0;
    bool showValues = (m_enabledRows & ROW_VALUES) != 0;

    // Height layout (matches BarsWidget):
    // - paddingV at top
    // - one row per wheel; when bars are shown the row is BAR_ROW_LINES tall so
    //   the temperature value sits centered inside the taller colored bar
    // - labelHeight at bottom (L/M/R column labels); collapses when labels are hidden
    float labelHeight = m_bShowLabels ? (LABEL_HEIGHT_LINES * dim.lineHeightNormal) : 0.0f;
    float wheelRowLines = showBars ? BAR_ROW_LINES : (showValues ? 1.0f : 0.0f);
    float wheelRowHeight = wheelRowLines * dim.lineHeightNormal;
    float contentHeight = wheelRowHeight * NUM_WHEELS;
    float backgroundHeight = dim.paddingV + contentHeight + labelHeight;

    // Add background quad
    addBackgroundQuad(startX, startY, backgroundWidth, backgroundHeight);

    // Set bounds for drag detection
    setBounds(startX, startY, startX + backgroundWidth, startY + backgroundHeight);

    float contentStartX = startX + dim.paddingH;

    // Bar geometry: the three section bars hug each other (no horizontal gap)
    // and split the content width evenly, so each wheel reads as one continuous
    // L/M/R tread strip. ~2.67-char bars still hold a centered 3-digit value at
    // the small font.
    float barSpacing = 0.0f;
    float barWidth = contentWidth / NUM_SECTIONS;
    float barsStartX = contentStartX;
    float barVMargin = BAR_VMARGIN_LINES * dim.lineHeightNormal;
    float barHeight = wheelRowHeight - 2.0f * barVMargin;

    // Per-bar center X (shared by the bar quad, the L/M/R label, and the value)
    float barCenterX[NUM_SECTIONS];
    for (int s = 0; s < NUM_SECTIONS; ++s) {
        barCenterX[s] = barsStartX + s * (barWidth + barSpacing) + barWidth * 0.5f;
    }

    // Colors
    unsigned long textColor = this->getColor(ColorSlot::PRIMARY);
    unsigned long mutedColor = this->getColor(ColorSlot::MUTED);
    // Fixed 50% — the bar background is part of the gauge readout, not the panel backdrop,
    // so it stays legible regardless of the background-opacity slider (only addBackgroundQuad
    // follows that). Matches BarsWidget / GForceWidget.
    unsigned long barBgColor = PluginUtils::applyOpacity(mutedColor, 0.5f);

    // Wheel labels: F, R - embedded in the left padding, one per wheel row
    const char* wheelLabels[NUM_WHEELS] = {"F", "R"};

    // Content starts after the top padding; L/M/R labels go at the bottom (BarsWidget-style)
    float currentY = startY + dim.paddingV;

    // Draw both wheels (0 = front, 1 = rear). Each wheel is one row; the value
    // is drawn centered inside the colored bar rather than on a separate row.
    for (int wheel = 0; wheel < NUM_WHEELS; ++wheel) {
        float barY = currentY + barVMargin;
        // Use the small font (same as Map HUD labels): at 8-char content width a
        // section column is only ~2.67 chars, so a 3-digit value at the normal
        // font would overrun the bar and collide with the adjacent column. The
        // small font keeps 3 digits within the column whether bars are shown or
        // not. Vertically centered within the row/bar.
        float valueFontSize = dim.fontSizeSmall;
        float textY = currentY + (wheelRowHeight - valueFontSize) * 0.5f;

        // Wheel label (F/R) centered in the left padding and in the row
        if (m_bShowLabels && (showBars || showValues)) {
            float wheelLabelX = startX + dim.paddingH * 0.5f;
            float wheelLabelY = currentY + (wheelRowHeight - dim.fontSize) * 0.5f;
            addString(wheelLabels[wheel], wheelLabelX, wheelLabelY, Justify::CENTER,
                this->getFont(FontCategory::STRONG), this->getColor(ColorSlot::TERTIARY), dim.fontSize);
        }

        for (int section = 0; section < NUM_SECTIONS; ++section) {
            float barX = barCenterX[section] - barWidth * 0.5f;

            // Colored bar (if enabled)
            if (showBars) {
                // Background bar (always visible)
                SPluginQuad_t bgQuad;
                float bgX = barX, bgY = barY;
                applyOffset(bgX, bgY);
                setQuadPositions(bgQuad, bgX, bgY, barWidth, barHeight);
                bgQuad.m_iSprite = SpriteIndex::SOLID_COLOR;
                bgQuad.m_ulColor = barBgColor;
                m_quads.push_back(bgQuad);

                // Colored fill (only when data available)
                if (hasData) {
                    float temp = bikeData.treadTemperature[wheel][section];
                    float midpoint = (m_coldThreshold + m_hotThreshold) / 2.0f;
                    unsigned long barColor = calculateTemperatureColor(temp, midpoint, m_coldThreshold, m_hotThreshold);

                    SPluginQuad_t fillQuad;
                    float fillX = barX, fillY = barY;
                    applyOffset(fillX, fillY);
                    setQuadPositions(fillQuad, fillX, fillY, barWidth, barHeight);
                    fillQuad.m_iSprite = SpriteIndex::SOLID_COLOR;
                    fillQuad.m_ulColor = barColor;
                    m_quads.push_back(fillQuad);
                }
            }

            // Temperature value (if enabled) - centered in the bar
            if (showValues) {
                float tempX = barCenterX[section];

                char tempBuffer[8];
                unsigned long tempColor = textColor;

                if (!hasData) {
                    // Spectating/replay: data is structurally unavailable (N/A).
                    // On track but invalid: simple missing data (-). Matches FuelWidget.
                    const char* placeholder = onTrack ? Placeholders::GENERIC : Placeholders::NOT_AVAILABLE;
                    snprintf(tempBuffer, sizeof(tempBuffer), "%s", placeholder);
                    tempColor = mutedColor;
                } else {
                    float temp = bikeData.treadTemperature[wheel][section];
                    int displayTemp = static_cast<int>(temp + 0.5f);
                    snprintf(tempBuffer, sizeof(tempBuffer), "%d", displayTemp);
                }

                addString(tempBuffer, tempX, textY, Justify::CENTER,
                    this->getFont(FontCategory::DIGITS), tempColor, valueFontSize);
            }
        }

        currentY += wheelRowHeight;
    }

    // Section labels: L, M, R - centered under each column in the bottom label
    // strip, matching BarsWidget's bottom-aligned bar labels.
    if (m_bShowLabels) {
        const char* labels[NUM_SECTIONS] = {"L", "M", "R"};
        float labelY = startY + dim.paddingV + contentHeight;
        for (int s = 0; s < NUM_SECTIONS; ++s) {
            addString(labels[s], barCenterX[s], labelY, Justify::CENTER,
                this->getFont(FontCategory::STRONG), this->getColor(ColorSlot::TERTIARY), dim.fontSize);
        }
    }
}

void TyreTempWidget::resetToDefaults() {
    m_bVisible = false;  // Hidden by default; opt-in via the Widgets tab
    m_bShowTitle = false;  // No title for gauge widgets
    setTextureVariant(0);  // No texture by default
    m_fBackgroundOpacity = 1.0f;  // Full opacity (100%)
    m_fScale = 1.0f;
    // GP-only widget: sits left of the Compass in the bottom gauge row (pitch 0.0715).
    setPosition(0.4565f, 0.86828f);
    m_coldThreshold = DEFAULT_COLD_THRESHOLD;
    m_hotThreshold = DEFAULT_HOT_THRESHOLD;
    m_enabledRows = ROW_DEFAULT;  // Show both bars and values by default
    m_bShowLabels = true;  // Labels ON by default (INI-only toggle)
    setDataDirty();
}

#endif // GAME_HAS_TYRE_TEMP
