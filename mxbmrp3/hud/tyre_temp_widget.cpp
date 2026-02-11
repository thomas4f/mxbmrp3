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
    // - 6 L/M/R labels (3 per wheel x 2 wheels) - actually just 3, shared between both
    // - 6 temperature values
    m_strings.reserve(9);

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
    if (!isVisible()) {
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
    bool hasData = (pluginData.getDrawState() == ViewState::ON_TRACK) && bikeData.isValid;

    float startX = 0.0f;
    float startY = 0.0f;

    // Widget width: match BarsWidget with 7 bars (7*1 + 6*0.4 = 9.4 chars)
    // Use 9 chars base + extra via padding adjustment
    constexpr int WIDGET_WIDTH_CHARS = 9;
    // Add 0.4 char extra width to match BarsWidget's 9.4 char content
    float extraWidth = PluginUtils::calculateMonospaceTextWidth(1, dim.fontSize) * 0.4f;
    float backgroundWidth = calculateBackgroundWidth(WIDGET_WIDTH_CHARS) + extraWidth;
    float contentWidth = PluginUtils::calculateMonospaceTextWidth(WIDGET_WIDTH_CHARS, dim.fontSize) + extraWidth;

    // Calculate height (mirroring BarsWidget pattern):
    // - labelHeight at top (L/M/R labels in top padding area)
    // - content rows based on enabled rows (bars and/or values for each wheel)
    // - paddingV at bottom
    float labelHeight = LABEL_HEIGHT_LINES * dim.lineHeightNormal;

    // Count content rows based on enabled row types
    // Each wheel (front/rear) can have bars and/or values
    int rowsPerWheel = 0;
    if (m_enabledRows & ROW_BARS) rowsPerWheel++;
    if (m_enabledRows & ROW_VALUES) rowsPerWheel++;
    int numContentRows = rowsPerWheel * NUM_WHEELS;  // 2 wheels

    float contentHeight = dim.lineHeightNormal * numContentRows;
    float backgroundHeight = labelHeight + contentHeight + dim.paddingV;

    // Add background quad
    addBackgroundQuad(startX, startY, backgroundWidth, backgroundHeight);

    // Set bounds for drag detection
    setBounds(startX, startY, startX + backgroundWidth, startY + backgroundHeight);

    float contentStartX = startX + dim.paddingH;

    // Calculate bar dimensions
    // Each section takes 1/3 of the width, with small gaps
    float sectionWidth = contentWidth / 3.0f;
    float barWidth = sectionWidth * 0.8f;  // 80% of section for bar, 20% for spacing
    float barSpacing = (sectionWidth - barWidth) / 2.0f;
    float barHeight = dim.lineHeightNormal * 0.8f;  // 80% of line height

    // Colors
    unsigned long textColor = this->getColor(ColorSlot::PRIMARY);
    unsigned long mutedColor = this->getColor(ColorSlot::MUTED);
    unsigned long barBgColor = PluginUtils::applyOpacity(mutedColor, m_fBackgroundOpacity * 0.5f);

    // Section labels: L, M, R - placed in top label area (like BarsWidget bottom labels)
    const char* labels[NUM_SECTIONS] = {"L", "M", "R"};
    for (int s = 0; s < NUM_SECTIONS; ++s) {
        float labelX = contentStartX + sectionWidth * s + sectionWidth / 2.0f;
        addString(labels[s], labelX, startY, Justify::CENTER,
            this->getFont(FontCategory::NORMAL), this->getColor(ColorSlot::TERTIARY), dim.fontSize);
    }

    // Content starts after label area
    float currentY = startY + labelHeight;

    // Draw both wheels (0 = front, 1 = rear)
    for (int wheel = 0; wheel < NUM_WHEELS; ++wheel) {
        // Draw bars for each section (if enabled)
        if (m_enabledRows & ROW_BARS) {
            // Center bars vertically in their row
            float barY = currentY + (dim.lineHeightNormal - barHeight) / 2.0f;

            for (int section = 0; section < NUM_SECTIONS; ++section) {
                float barX = contentStartX + sectionWidth * section + barSpacing;

                // Background bar (always visible)
                SPluginQuad_t bgQuad;
                float bgX = barX, bgY = barY;
                applyOffset(bgX, bgY);
                setQuadPositions(bgQuad, bgX, bgY, barWidth, barHeight);
                bgQuad.m_iSprite = SpriteIndex::SOLID_COLOR;
                bgQuad.m_ulColor = barBgColor;
                m_quads.push_back(bgQuad);

                // Colored bar (only when data available)
                if (hasData) {
                    float temp = bikeData.treadTemperature[wheel][section];
                    unsigned long barColor = calculateTyreTemperatureColor(temp);

                    SPluginQuad_t fillQuad;
                    float fillX = barX, fillY = barY;
                    applyOffset(fillX, fillY);
                    setQuadPositions(fillQuad, fillX, fillY, barWidth, barHeight);
                    fillQuad.m_iSprite = SpriteIndex::SOLID_COLOR;
                    fillQuad.m_ulColor = barColor;
                    m_quads.push_back(fillQuad);
                }
            }

            currentY += dim.lineHeightNormal;
        }

        // Temperature values row (if enabled)
        if (m_enabledRows & ROW_VALUES) {
            for (int section = 0; section < NUM_SECTIONS; ++section) {
                float tempX = contentStartX + sectionWidth * section + sectionWidth / 2.0f;

                char tempBuffer[8];
                unsigned long tempColor = textColor;

                if (!hasData) {
                    snprintf(tempBuffer, sizeof(tempBuffer), "%s", Placeholders::GENERIC);
                    tempColor = mutedColor;
                } else {
                    float temp = bikeData.treadTemperature[wheel][section];
                    int displayTemp = static_cast<int>(temp + 0.5f);
                    snprintf(tempBuffer, sizeof(tempBuffer), "%d", displayTemp);
                }

                addString(tempBuffer, tempX, currentY, Justify::CENTER,
                    this->getFont(FontCategory::DIGITS), tempColor, dim.fontSize);
            }

            currentY += dim.lineHeightNormal;
        }
    }
}

unsigned long TyreTempWidget::calculateTyreTemperatureColor(float temp) const {
    // Temperature color gradient:
    // - Below coldThreshold: Blue (too cold, no grip)
    // - coldThreshold to midpoint: Blue -> Green gradient (warming up)
    // - midpoint to hotThreshold: Green -> Yellow -> Red gradient (getting hot)
    // - Above hotThreshold: Red (overheating, degradation)

    // Color constants (RGB values)
    constexpr unsigned char BLUE_R = 0x40, BLUE_G = 0x80, BLUE_B = 0xFF;   // Cold blue
    constexpr unsigned char GREEN_R = 0x40, GREEN_G = 0xFF, GREEN_B = 0x40; // Optimal green
    constexpr unsigned char YELLOW_R = 0xFF, YELLOW_G = 0xD0, YELLOW_B = 0x40; // Warning yellow
    constexpr unsigned char RED_R = 0xFF, RED_G = 0x40, RED_B = 0x40;      // Hot red

    // Calculate midpoint (optimal temperature range)
    float midpoint = (m_coldThreshold + m_hotThreshold) / 2.0f;

    unsigned char r, g, b;

    if (temp <= m_coldThreshold) {
        // Below cold threshold - solid blue (too cold)
        r = BLUE_R;
        g = BLUE_G;
        b = BLUE_B;
    } else if (temp < midpoint) {
        // Between coldThreshold and midpoint - blue to green gradient
        float range = midpoint - m_coldThreshold;
        float t = (range > 0.0f) ? (temp - m_coldThreshold) / range : 1.0f;
        r = static_cast<unsigned char>(BLUE_R + t * (GREEN_R - BLUE_R));
        g = static_cast<unsigned char>(BLUE_G + t * (GREEN_G - BLUE_G));
        b = static_cast<unsigned char>(BLUE_B + t * (GREEN_B - BLUE_B));
    } else if (temp <= m_hotThreshold) {
        // Between midpoint and hotThreshold - green to yellow to red gradient
        float range = m_hotThreshold - midpoint;
        float normalized = (range > 0.0f) ? (temp - midpoint) / range : 0.0f;

        if (normalized < 0.5f) {
            // Green to yellow (first half)
            float t = normalized * 2.0f;
            r = static_cast<unsigned char>(GREEN_R + t * (YELLOW_R - GREEN_R));
            g = static_cast<unsigned char>(GREEN_G + t * (YELLOW_G - GREEN_G));
            b = static_cast<unsigned char>(GREEN_B + t * (YELLOW_B - GREEN_B));
        } else {
            // Yellow to red (second half)
            float t = (normalized - 0.5f) * 2.0f;
            r = static_cast<unsigned char>(YELLOW_R + t * (RED_R - YELLOW_R));
            g = static_cast<unsigned char>(YELLOW_G + t * (RED_G - YELLOW_G));
            b = static_cast<unsigned char>(YELLOW_B + t * (RED_B - YELLOW_B));
        }
    } else {
        // Above hot threshold - solid red (overheating)
        r = RED_R;
        g = RED_G;
        b = RED_B;
    }

    return PluginUtils::makeColor(r, g, b);
}

void TyreTempWidget::resetToDefaults() {
    m_bVisible = true;  // Visible by default in GP Bikes
    m_bShowTitle = false;  // No title for gauge widgets
    setTextureVariant(0);  // No texture by default
    m_fBackgroundOpacity = 1.0f;  // Full opacity (100%)
    m_fScale = 1.0f;
    setPosition(0.65f, 0.85f);  // Default position (can be adjusted)
    m_coldThreshold = DEFAULT_COLD_THRESHOLD;
    m_hotThreshold = DEFAULT_HOT_THRESHOLD;
    m_enabledRows = ROW_DEFAULT;  // Show both bars and values by default
    setDataDirty();
}

#endif // GAME_HAS_TYRE_TEMP
