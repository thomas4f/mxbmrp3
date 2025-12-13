// ============================================================================
// hud/input_hud.cpp
// Displays analog stick input trails (left stick and right stick)
// ============================================================================
#include "input_hud.h"
#include "../core/plugin_data.h"
#include "../core/plugin_utils.h"
#include "../core/color_config.h"
#include "../diagnostics/logger.h"
#include <cmath>
#include <algorithm>

using namespace PluginConstants;

InputHud::InputHud() {
    DEBUG_INFO("InputHud initialized");
    setScale(1.0f);
    setDraggable(true);

    // Set defaults to match user configuration
    m_bShowTitle = false;
    m_fBackgroundOpacity = SettingsLimits::DEFAULT_OPACITY;
    setPosition(0.6875f, 0.0f);

    // Pre-allocate calculation buffers to avoid per-frame allocation
    const size_t maxHistory = HistoryBuffers::MAX_STICK_HISTORY;
    m_screenX.reserve(maxHistory);
    m_screenY.reserve(maxHistory);
    m_perpX.reserve(maxHistory);
    m_perpY.reserve(maxHistory);
    m_age.reserve(maxHistory);
    m_alpha.reserve(maxHistory);
    m_scale.reserve(maxHistory);

    // Pre-allocate render buffers to avoid reallocations
    m_quads.reserve(110);    // 1 bg + 4 crosshair + 2 sticks × 49 trapezoids + 2 markers = ~107 quads max
    m_strings.reserve(10);   // Title + table (header + 2 rows × 3 cells) = 9 strings

    rebuildRenderData();
}

void InputHud::update() {
    // Always rebuild - updateXInputData() marks this dirty every physics callback (100Hz)
    rebuildRenderData();
    clearDataDirty();
    clearLayoutDirty();
}

bool InputHud::handlesDataType(DataChangeType dataType) const {
    return dataType == DataChangeType::InputTelemetry;
}

void InputHud::rebuildRenderData() {
    m_quads.clear();
    m_strings.clear();

    // PERFORMANCE TEST: Skip all calculations when disabled
    if (!ENABLED) return;

    const auto dims = getScaledDimensions();
    const HistoryBuffers& history = PluginData::getInstance().getHistoryBuffers();
    const InputTelemetryData& inputData = PluginData::getInstance().getInputTelemetry();

    // Calculate dimensions
    float backgroundWidth = PluginUtils::calculateMonospaceTextWidth(BACKGROUND_WIDTH_CHARS, dims.fontSize)
        + dims.paddingH + dims.paddingH;
    float stickHeight = STICK_HEIGHT_LINES * dims.lineHeightNormal;

    // Height: top pad + title (if shown) + stick trails + bottom pad (table overlaps bottom of crosshairs)
    float titleHeight = m_bShowTitle ? dims.lineHeightLarge : 0.0f;
    float backgroundHeight = dims.paddingV + titleHeight + stickHeight + dims.paddingV;

    setBounds(START_X, START_Y, START_X + backgroundWidth, START_Y + backgroundHeight);

    // Add background quad
    addBackgroundQuad(START_X, START_Y, backgroundWidth, backgroundHeight);

    float contentStartX = START_X + dims.paddingH;
    float contentStartY = START_Y + dims.paddingV;
    float currentY = contentStartY;

    // Title
    addTitleString("Input", contentStartX, currentY, PluginConstants::Justify::LEFT,
        PluginConstants::Fonts::ENTER_SANSMAN, ColorConfig::getInstance().getPrimary(), dims.fontSizeLarge);
    currentY += titleHeight;

    // Calculate stick dimensions - make them square (7 lines high)
    // Square means width in screen space = height in screen space
    float stickWidth = stickHeight / PluginConstants::UI_ASPECT_RATIO;  // Square in pixel terms
    float stickSpacing = PluginUtils::calculateMonospaceTextWidth(STICK_SPACING_CHARS, dims.fontSize);

    // Left stick trail (blue)
    addStickTrail("LEFT STICK", history.leftStick, contentStartX, currentY,
                  stickWidth, stickHeight, PluginConstants::SemanticColors::STICK_L, inputData.xinputConnected);

    // Right stick trail (green) - rider lean
    float rightStickX = contentStartX + stickWidth + stickSpacing;
    addStickTrail("RIGHT STICK", history.rightStick, rightStickX, currentY,
                  stickWidth, stickHeight, PluginConstants::SemanticColors::STICK_R, inputData.xinputConnected);

    // Stick position table in bottom portion of crosshairs (centered, transposed)
    // Only render if ELEM_VALUES is enabled
    if (m_enabledElements & ELEM_VALUES) {
        // Table format (transposed): "  LS    RS" header, then "X +x.xx -x.xx" and "Y +x.xx +x.xx"
        // Table width: "X " (2) + LS value (5) + "  " (2) + RS value (5) = 14 chars
        float tableWidth = PluginUtils::calculateMonospaceTextWidth(14, dims.fontSize);
        float tableStartX = contentStartX + (PluginUtils::calculateMonospaceTextWidth(BACKGROUND_WIDTH_CHARS, dims.fontSize) - tableWidth) / 2;

        // Position table so bottom row aligns with bottom of crosshairs
        float tableHeight = 3 * dims.lineHeightNormal;  // Header + X + Y rows
        float tableY = currentY + stickHeight - tableHeight;

        // Column positions
        float labelColX = tableStartX;
        float lsValueX = labelColX + PluginUtils::calculateMonospaceTextWidth(2, dims.fontSize);   // After "X "
        float rsValueX = lsValueX + PluginUtils::calculateMonospaceTextWidth(7, dims.fontSize);    // After LS value + "  "

        // Header row - LS and RS column headers (offset by 1 char to align with digit, not sign)
        float headerOffsetX = PluginUtils::calculateMonospaceTextWidth(1, dims.fontSize);
        addString("LS", lsValueX + headerOffsetX, tableY, PluginConstants::Justify::LEFT,
            PluginConstants::Fonts::ROBOTO_MONO, ColorConfig::getInstance().getTertiary(), dims.fontSize);
        addString("RS", rsValueX + headerOffsetX, tableY, PluginConstants::Justify::LEFT,
            PluginConstants::Fonts::ROBOTO_MONO, ColorConfig::getInstance().getTertiary(), dims.fontSize);

        // X row
        float xRowY = tableY + dims.lineHeightNormal;
        addString("X", labelColX, xRowY, PluginConstants::Justify::LEFT,
            PluginConstants::Fonts::ROBOTO_MONO, ColorConfig::getInstance().getTertiary(), dims.fontSize);
        char lsXValue[8];
        char rsXValue[8];
        snprintf(lsXValue, sizeof(lsXValue), "%+.2f", inputData.leftStickX);
        snprintf(rsXValue, sizeof(rsXValue), "%+.2f", inputData.rightStickX);
        addString(lsXValue, lsValueX, xRowY, PluginConstants::Justify::LEFT,
            PluginConstants::Fonts::ROBOTO_MONO, ColorConfig::getInstance().getSecondary(), dims.fontSize);
        addString(rsXValue, rsValueX, xRowY, PluginConstants::Justify::LEFT,
            PluginConstants::Fonts::ROBOTO_MONO, ColorConfig::getInstance().getSecondary(), dims.fontSize);

        // Y row
        float yRowY = xRowY + dims.lineHeightNormal;
        addString("Y", labelColX, yRowY, PluginConstants::Justify::LEFT,
            PluginConstants::Fonts::ROBOTO_MONO, ColorConfig::getInstance().getTertiary(), dims.fontSize);
        char lsYValue[8];
        char rsYValue[8];
        snprintf(lsYValue, sizeof(lsYValue), "%+.2f", inputData.leftStickY);
        snprintf(rsYValue, sizeof(rsYValue), "%+.2f", inputData.rightStickY);
        addString(lsYValue, lsValueX, yRowY, PluginConstants::Justify::LEFT,
            PluginConstants::Fonts::ROBOTO_MONO, ColorConfig::getInstance().getSecondary(), dims.fontSize);
        addString(rsYValue, rsValueX, yRowY, PluginConstants::Justify::LEFT,
            PluginConstants::Fonts::ROBOTO_MONO, ColorConfig::getInstance().getSecondary(), dims.fontSize);
    }
}

void InputHud::addStickTrail(const char* label, const std::deque<HistoryBuffers::StickSample>& stickHistory,
                                        float x, float y, float width, float height, unsigned long color, bool xinputConnected) {
    const auto dims = getScaledDimensions();

    // Apply offset to all positions first (for dragging support)
    float ox = x, oy = y;
    applyOffset(ox, oy);

    // Calculate center positions (with offset already applied)
    float centerX = ox + width / 2;
    float centerY = oy + height / 2;
    float crosshairThickness = 0.001f * getScale();  // Match grid line thickness

    // Crosshair lines (always render since they're required)
    if (m_enabledElements & ELEM_CROSSHAIRS) {
        // Horizontal center line (use grid line styling for consistency)
        SPluginQuad_t hLineQuad;
        hLineQuad.m_iSprite = PluginConstants::SpriteIndex::SOLID_COLOR;
        hLineQuad.m_ulColor = ColorConfig::getInstance().getMuted();  // Match grid line color
        setQuadPositions(hLineQuad, ox, centerY - crosshairThickness / 2,
                        width, crosshairThickness);
        m_quads.push_back(hLineQuad);

        // Vertical center line (use grid line styling for consistency)
        // Apply aspect ratio correction to thickness (used as width for vertical line)
        SPluginQuad_t vLineQuad;
        vLineQuad.m_iSprite = PluginConstants::SpriteIndex::SOLID_COLOR;
        vLineQuad.m_ulColor = ColorConfig::getInstance().getMuted();  // Match grid line color
        setQuadPositions(vLineQuad, centerX - (crosshairThickness / 2) / PluginConstants::UI_ASPECT_RATIO,
                        oy, crosshairThickness / PluginConstants::UI_ASPECT_RATIO, height);
        m_quads.push_back(vLineQuad);
    }

    // Draw stick trail as tapered trapezoids (only if enabled, controller connected, and history available)
    if ((m_enabledElements & ELEM_TRAILS) && xinputConnected && !stickHistory.empty()) {
        size_t historySize = stickHistory.size();
        float baseThickness = height * 0.02f;

        // Resize cached buffers to match history size
        m_screenX.resize(historySize);
        m_screenY.resize(historySize);
        m_perpX.resize(historySize);
        m_perpY.resize(historySize);
        m_age.resize(historySize);
        m_alpha.resize(historySize);
        m_scale.resize(historySize);

        // Pre-calculate all values once
        float invHistorySize = 1.0f / historySize;
        for (size_t i = 0; i < historySize; i++) {
            // Screen positions
            m_screenX[i] = centerX + (stickHistory[i].x * width / 2);
            m_screenY[i] = centerY - (stickHistory[i].y * height / 2);

            // Age-based gradient values
            m_age[i] = (float)i * invHistorySize;
            m_alpha[i] = 0.2f + (m_age[i] * 0.8f);
            m_scale[i] = 0.5f + (m_age[i] * 3.5f);

            // Initialize perpendiculars
            m_perpX[i] = 0.0f;
            m_perpY[i] = 0.0f;
        }

        // Calculate mitered perpendiculars
        for (size_t i = 0; i < historySize; i++) {
            float px = 0.0f, py = 0.0f;
            int count = 0;

            if (i > 0) {
                float dx = m_screenX[i] - m_screenX[i - 1];
                float dy = m_screenY[i] - m_screenY[i - 1];
                float len = std::sqrt(dx * dx + dy * dy);
                if (len > 0.0001f) {
                    float invLen = 1.0f / len;
                    px += dy * invLen;
                    py += -dx * invLen;
                    count++;
                }
            }

            if (i < historySize - 1) {
                float dx = m_screenX[i + 1] - m_screenX[i];
                float dy = m_screenY[i + 1] - m_screenY[i];
                float len = std::sqrt(dx * dx + dy * dy);
                if (len > 0.0001f) {
                    float invLen = 1.0f / len;
                    px += dy * invLen;
                    py += -dx * invLen;
                    count++;
                }
            }

            if (count > 0) {
                float len = std::sqrt(px * px + py * py);
                if (len > 0.0001f) {
                    float invLen = 1.0f / len;
                    m_perpX[i] = px * invLen;
                    m_perpY[i] = py * invLen;
                }
            }
        }

        // Draw tapered trapezoids
        for (size_t i = 0; i < historySize - 1; i++) {
            float ax = m_screenX[i];
            float ay = m_screenY[i];
            float bx = m_screenX[i + 1];
            float by = m_screenY[i + 1];

            // Skip coincident points
            float dx = bx - ax;
            float dy = by - ay;
            if (dx * dx + dy * dy < 0.00000001f) continue;

            // Use cached gradient values
            float avgAlpha = (m_alpha[i] + m_alpha[i + 1]) * 0.5f;
            unsigned long fadedColor = (color & 0x00FFFFFF) | (static_cast<unsigned long>(avgAlpha * 255) << 24);

            float thicknessA = baseThickness * m_scale[i];
            float thicknessB = baseThickness * m_scale[i + 1];

            // Use cached mitered perpendiculars
            float hxA = (m_perpX[i] * thicknessA * 0.5f) / PluginConstants::UI_ASPECT_RATIO;
            float hyA = m_perpY[i] * thicknessA * 0.5f;
            float hxB = (m_perpX[i + 1] * thicknessB * 0.5f) / PluginConstants::UI_ASPECT_RATIO;
            float hyB = m_perpY[i + 1] * thicknessB * 0.5f;

            // Create trapezoid quad (counter-clockwise)
            SPluginQuad_t trapezoid;
            trapezoid.m_iSprite = PluginConstants::SpriteIndex::SOLID_COLOR;
            trapezoid.m_ulColor = fadedColor;

            // Vertices counter-clockwise: A+perp, A-perp, B-perp, B+perp
            trapezoid.m_aafPos[0][0] = ax + hxA;
            trapezoid.m_aafPos[0][1] = ay + hyA;
            trapezoid.m_aafPos[1][0] = ax - hxA;
            trapezoid.m_aafPos[1][1] = ay - hyA;
            trapezoid.m_aafPos[2][0] = bx - hxB;
            trapezoid.m_aafPos[2][1] = by - hyB;
            trapezoid.m_aafPos[3][0] = bx + hxB;
            trapezoid.m_aafPos[3][1] = by + hyB;

            m_quads.push_back(trapezoid);
        }
    }

    // Draw current position marker (always show if controller connected, even if trails disabled)
    if (xinputConnected && !stickHistory.empty()) {
        const auto& currentSample = stickHistory.back();
        float currentX = centerX + (currentSample.x * width / 2);
        float currentY = centerY - (currentSample.y * height / 2);  // Inverted Y

        // Make marker square and 4x base size
        float baseThickness = height * 0.02f;
        float markerHeight = baseThickness * 4.0f;  // 4x base size
        float markerWidth = markerHeight / PluginConstants::UI_ASPECT_RATIO;  // Square in pixels

        // Draw current position marker with full opacity
        SPluginQuad_t markerQuad;
        markerQuad.m_iSprite = PluginConstants::SpriteIndex::SOLID_COLOR;
        markerQuad.m_ulColor = color;  // Full opacity (no fading)
        setQuadPositions(markerQuad, currentX - markerWidth / 2, currentY - markerHeight / 2,
                       markerWidth, markerHeight);
        m_quads.push_back(markerQuad);
    }
}

void InputHud::resetToDefaults() {
    m_bVisible = true;
    m_bShowTitle = false;
    m_bShowBackgroundTexture = false;  // No texture by default
    m_fBackgroundOpacity = SettingsLimits::DEFAULT_OPACITY;
    m_fScale = 1.0f;
    setPosition(0.6875f, 0.0f);
    m_enabledElements = ELEM_DEFAULT;
    setDataDirty();
}
