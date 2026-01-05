// ============================================================================
// hud/gamepad_widget.cpp
// Displays controller button overlay - shows pressed buttons, sticks, triggers
// ============================================================================
#include "gamepad_widget.h"
#include "../core/plugin_utils.h"
#include "../core/color_config.h"
#include "../core/asset_manager.h"
#include "../core/xinput_reader.h"
#include "../diagnostics/logger.h"
#include <cmath>
#include <algorithm>

using namespace PluginConstants;

GamepadWidget::GamepadWidget() {
    DEBUG_INFO("GamepadWidget created");
    setDraggable(true);

    // Pre-allocate render buffers
    m_quads.reserve(50);  // Sticks + triggers + bumpers + face + dpad + menu buttons
    m_strings.reserve(10);

    // Set texture base name for dynamic texture discovery
    setTextureBaseName("gamepad_widget");

    // Set all configurable defaults
    resetToDefaults();

    rebuildRenderData();
}

void GamepadWidget::update() {
    // Always rebuild - XInput data updates every physics callback
    rebuildRenderData();
    clearDataDirty();
    clearLayoutDirty();
}

bool GamepadWidget::handlesDataType(DataChangeType dataType) const {
    return dataType == DataChangeType::InputTelemetry;
}

void GamepadWidget::rebuildRenderData() {
    m_quads.clear();
    m_strings.clear();

    const auto dims = getScaledDimensions();
    const XInputData& xinput = XInputReader::getInstance().getData();

    // Calculate dimensions
    float backgroundWidth = PluginUtils::calculateMonospaceTextWidth(BACKGROUND_WIDTH_CHARS, dims.fontSize)
        + dims.paddingH + dims.paddingH;
    float stickHeight = STICK_HEIGHT_LINES * dims.lineHeightNormal;

    // Layout: triggers/bumpers row + sticks row + buttons row (face/dpad/menu)
    // Proportions tuned to match 750:422 gamepad texture aspect ratio
    float triggersHeight = dims.lineHeightNormal * 1.2f;
    float buttonsHeight = dims.lineHeightNormal * 2.45f;
    float backgroundHeight = dims.paddingV + triggersHeight + stickHeight + buttonsHeight + dims.paddingV;

    setBounds(START_X, START_Y, START_X + backgroundWidth, START_Y + backgroundHeight);

    // Add background quad
    addBackgroundQuad(START_X, START_Y, backgroundWidth, backgroundHeight);

    float contentStartX = START_X + dims.paddingH;
    float contentStartY = START_Y + dims.paddingV;
    float currentY = contentStartY;
    float contentWidth = backgroundWidth - dims.paddingH * 2;

    const auto& layout = getCurrentLayout();
    float scale = getScale();

    // ========================================================================
    // ROW 1: Triggers and Bumpers
    // ========================================================================
    float triggerRowY = currentY;

    // Trigger size from layout
    float triggerWidth = backgroundWidth * (layout.triggerWidth / layout.backgroundWidth);
    float triggerHeight = triggerWidth * (layout.triggerHeight / layout.triggerWidth) * UI_ASPECT_RATIO;
    float triggerCenterY = triggerRowY + triggerHeight / 2;

    // Bumper size from layout
    float bumperWidth = backgroundWidth * (layout.bumperWidth / layout.backgroundWidth);
    float bumperHeight = bumperWidth * (layout.bumperHeight / layout.bumperWidth) * UI_ASPECT_RATIO;

    // Left trigger (LT) - with offset
    float ltOffsetX = layout.leftTriggerX * scale;
    float ltOffsetY = layout.leftTriggerY * scale;
    float ltCenterX = contentStartX + triggerWidth / 2 + ltOffsetX;
    float ltCenterY = triggerCenterY + ltOffsetY;
    addTriggerButton(ltCenterX, ltCenterY, triggerWidth, triggerHeight,
                     xinput.leftTrigger, true);

    // Left bumper (LB) - with offset
    float lbOffsetX = layout.leftBumperX * scale;
    float lbOffsetY = layout.leftBumperY * scale;
    float lbCenterX = contentStartX + triggerWidth + dims.gridH(1) + bumperWidth / 2 + lbOffsetX;
    float lbCenterY = triggerCenterY + lbOffsetY;
    addBumperButton(lbCenterX, lbCenterY, bumperWidth, bumperHeight,
                    xinput.leftShoulder, true);

    // Right bumper (RB) - with offset
    float rbOffsetX = layout.rightBumperX * scale;
    float rbOffsetY = layout.rightBumperY * scale;
    float rbCenterX = contentStartX + contentWidth - triggerWidth - dims.gridH(1) - bumperWidth / 2 + rbOffsetX;
    float rbCenterY = triggerCenterY + rbOffsetY;
    addBumperButton(rbCenterX, rbCenterY, bumperWidth, bumperHeight,
                    xinput.rightShoulder, false);

    // Right trigger (RT) - with offset
    float rtOffsetX = layout.rightTriggerX * scale;
    float rtOffsetY = layout.rightTriggerY * scale;
    float rtCenterX = contentStartX + contentWidth - triggerWidth / 2 + rtOffsetX;
    float rtCenterY = triggerCenterY + rtOffsetY;
    addTriggerButton(rtCenterX, rtCenterY, triggerWidth, triggerHeight,
                     xinput.rightTrigger, false);

    currentY += triggersHeight;

    // ========================================================================
    // ROW 2: Analog Sticks
    // ========================================================================
    float stickWidth = stickHeight / UI_ASPECT_RATIO;
    float stickSpacing = PluginUtils::calculateMonospaceTextWidth(STICK_SPACING_CHARS, dims.fontSize);

    // Left stick - with offset
    float lsOffsetX = layout.leftStickX * scale;
    float lsOffsetY = layout.leftStickY * scale;
    float leftStickCenterX = contentStartX + stickWidth / 2 + lsOffsetX;
    float leftStickCenterY = currentY + stickHeight / 2 + lsOffsetY;
    addStick(leftStickCenterX, leftStickCenterY, xinput.leftStickX, xinput.leftStickY,
             stickWidth, stickHeight, backgroundWidth, layout, xinput.leftThumb);

    // Right stick - with offset
    float rsOffsetX = layout.rightStickX * scale;
    float rsOffsetY = layout.rightStickY * scale;
    float rightStickCenterX = contentStartX + stickWidth + stickSpacing + stickWidth / 2 + rsOffsetX;
    float rightStickCenterY = currentY + stickHeight / 2 + rsOffsetY;
    addStick(rightStickCenterX, rightStickCenterY, xinput.rightStickX, xinput.rightStickY,
             stickWidth, stickHeight, backgroundWidth, layout, xinput.rightThumb);

    currentY += stickHeight;

    // ========================================================================
    // ROW 3: D-Pad, Menu Buttons, Face Buttons
    // ========================================================================
    if (xinput.isConnected) {
        float buttonRowY = currentY + dims.lineHeightNormal * 0.15f;

        // D-Pad (left side, aligned with left stick) - with offset
        float dpadOffsetX = layout.dpadX * scale;
        float dpadOffsetY = layout.dpadY * scale;
        float dpadCenterX = contentStartX + stickWidth / 2 + dpadOffsetX;
        float dpadCenterY = buttonRowY + dims.lineHeightNormal * 0.9f + dpadOffsetY;

        float dpadBtnWidth = backgroundWidth * (layout.dpadWidth / layout.backgroundWidth);
        float dpadBtnHeight = dpadBtnWidth * (layout.dpadHeight / layout.dpadWidth) * UI_ASPECT_RATIO;
        float dpadBtnSpacing = dpadBtnHeight * 0.55f * layout.dpadSpacing;

        // Up (direction 0)
        addDpadButton(dpadCenterX, dpadCenterY - dpadBtnSpacing, dpadBtnWidth, dpadBtnHeight,
                      xinput.dpadUp, 0);
        // Down (direction 2)
        addDpadButton(dpadCenterX, dpadCenterY + dpadBtnSpacing, dpadBtnWidth, dpadBtnHeight,
                      xinput.dpadDown, 2);
        // Left (direction 3)
        addDpadButton(dpadCenterX - dpadBtnSpacing / UI_ASPECT_RATIO, dpadCenterY, dpadBtnWidth, dpadBtnHeight,
                      xinput.dpadLeft, 3);
        // Right (direction 1)
        addDpadButton(dpadCenterX + dpadBtnSpacing / UI_ASPECT_RATIO, dpadCenterY, dpadBtnWidth, dpadBtnHeight,
                      xinput.dpadRight, 1);

        // Menu buttons (center - Back and Start) - with offset
        float menuOffsetX = layout.menuButtonsX * scale;
        float menuOffsetY = layout.menuButtonsY * scale;
        float menuBtnWidth = backgroundWidth * (layout.menuButtonWidth / layout.backgroundWidth);
        float menuBtnHeight = menuBtnWidth * (layout.menuButtonHeight / layout.menuButtonWidth) * UI_ASPECT_RATIO;
        float menuCenterX = contentStartX + contentWidth / 2 + menuOffsetX;
        float menuCenterY = buttonRowY + dims.lineHeightNormal * 0.7f + menuBtnHeight / 2 + menuOffsetY;
        float menuSpacing = menuBtnWidth * layout.menuButtonSpacing;

        // Back (select)
        addMenuButton(menuCenterX - menuSpacing - menuBtnWidth / 2, menuCenterY,
                      menuBtnWidth, menuBtnHeight, xinput.buttonBack, nullptr);

        // Start
        addMenuButton(menuCenterX + menuSpacing + menuBtnWidth / 2, menuCenterY,
                      menuBtnWidth, menuBtnHeight, xinput.buttonStart, nullptr);

        // Face buttons (right side, aligned with right stick) - diamond layout - with offset
        float faceOffsetX = layout.faceButtonsX * scale;
        float faceOffsetY = layout.faceButtonsY * scale;
        float faceButtonSize = backgroundWidth * (layout.faceButtonSize / layout.backgroundWidth) * UI_ASPECT_RATIO;
        float faceCenterX = contentStartX + stickWidth + stickSpacing + stickWidth / 2 + faceOffsetX;
        float faceCenterY = buttonRowY + dims.lineHeightNormal * 0.9f + faceOffsetY;
        float faceSpacing = faceButtonSize * layout.faceButtonSpacing;

        // Y (top) - Yellow
        addFaceButton(faceCenterX, faceCenterY - faceSpacing, faceButtonSize,
                      xinput.buttonY, COLOR_BUTTON_Y, "Y");
        // A (bottom) - Green
        addFaceButton(faceCenterX, faceCenterY + faceSpacing, faceButtonSize,
                      xinput.buttonA, COLOR_BUTTON_A, "A");
        // X (left) - Blue
        addFaceButton(faceCenterX - faceSpacing / UI_ASPECT_RATIO, faceCenterY, faceButtonSize,
                      xinput.buttonX, COLOR_BUTTON_X, "X");
        // B (right) - Red
        addFaceButton(faceCenterX + faceSpacing / UI_ASPECT_RATIO, faceCenterY, faceButtonSize,
                      xinput.buttonB, COLOR_BUTTON_B, "B");
    }
}

void GamepadWidget::addStick(float centerX, float centerY, float stickX, float stickY,
                              float width, float height, float backgroundWidth,
                              const LayoutConfig& layout, bool isPressed) {
    float ox = centerX, oy = centerY;
    applyOffset(ox, oy);

    // Try to use stick sprite texture
    int spriteIndex = (m_textureVariant > 0)
        ? AssetManager::getInstance().getSpriteIndex("gamepad_stick", m_textureVariant)
        : 0;

    // Calculate stick position - reduced movement range (30% of area)
    float moveRange = 0.3f;
    float currentX = ox + (stickX * width / 2 * moveRange);
    float currentY = oy - (stickY * height / 2 * moveRange);

    SPluginQuad_t markerQuad;
    if (spriteIndex > 0) {
        // Stick sprite size from layout
        float markerWidth = backgroundWidth * (layout.stickSize / layout.backgroundWidth);
        float markerHeight = markerWidth * UI_ASPECT_RATIO;

        markerQuad.m_iSprite = spriteIndex;
        // Dark when not pressed (L3/R3), white when pressed
        if (isPressed) {
            markerQuad.m_ulColor = ColorPalette::WHITE;
        } else {
            markerQuad.m_ulColor = PluginUtils::makeColor(80, 80, 80);
        }
        setQuadPositions(markerQuad, currentX - markerWidth / 2, currentY - markerHeight / 2,
                       markerWidth, markerHeight);
    } else {
        // Fallback to solid color dot
        float baseThickness = height * 0.02f;
        float markerHeight = baseThickness * 4.0f;
        float markerWidth = markerHeight / UI_ASPECT_RATIO;

        markerQuad.m_iSprite = SpriteIndex::SOLID_COLOR;
        markerQuad.m_ulColor = isPressed ? ColorPalette::WHITE : COLOR_INACTIVE;
        setQuadPositions(markerQuad, currentX - markerWidth / 2, currentY - markerHeight / 2,
                       markerWidth, markerHeight);
    }
    m_quads.push_back(markerQuad);
}

void GamepadWidget::addFaceButton(float centerX, float centerY, float size, bool isPressed,
                                   unsigned long labelColor, const char* label) {
    const auto dims = getScaledDimensions();

    float ox = centerX, oy = centerY;
    applyOffset(ox, oy);

    float buttonWidth = size / UI_ASPECT_RATIO;
    float buttonHeight = size;

    // Get sprite index for gamepad_face_button texture
    int spriteIndex = (m_textureVariant > 0)
        ? AssetManager::getInstance().getSpriteIndex("gamepad_face_button", m_textureVariant)
        : 0;

    SPluginQuad_t buttonQuad;
    if (spriteIndex > 0) {
        buttonQuad.m_iSprite = spriteIndex;
        if (isPressed) {
            buttonQuad.m_ulColor = ColorPalette::WHITE;
        } else {
            buttonQuad.m_ulColor = PluginUtils::makeColor(40, 40, 40);
        }
    } else {
        buttonQuad.m_iSprite = SpriteIndex::SOLID_COLOR;
        buttonQuad.m_ulColor = isPressed ? labelColor : COLOR_INACTIVE;
    }
    setQuadPositions(buttonQuad, ox - buttonWidth / 2, oy - buttonHeight / 2, buttonWidth, buttonHeight);
    m_quads.push_back(buttonQuad);

    // Add label text centered on button
    if (label) {
        float labelFontSize = dims.fontSize * 0.75f;
        addString(label, centerX, centerY - labelFontSize * 0.4f, Justify::CENTER,
            Fonts::getStrong(), labelColor, labelFontSize);
    }
}

void GamepadWidget::addDpadButton(float centerX, float centerY, float width, float height,
                                   bool isPressed, int direction) {
    float ox = centerX, oy = centerY;
    applyOffset(ox, oy);

    // Get sprite index for gamepad_dpad_button texture
    int spriteIndex = (m_textureVariant > 0)
        ? AssetManager::getInstance().getSpriteIndex("gamepad_dpad_button", m_textureVariant)
        : 0;

    SPluginQuad_t buttonQuad;
    if (spriteIndex > 0) {
        buttonQuad.m_iSprite = spriteIndex;
        if (isPressed) {
            buttonQuad.m_ulColor = ColorPalette::WHITE;
        } else {
            buttonQuad.m_ulColor = PluginUtils::makeColor(40, 40, 40);
        }
    } else {
        buttonQuad.m_iSprite = SpriteIndex::SOLID_COLOR;
        buttonQuad.m_ulColor = isPressed ? COLOR_DPAD : COLOR_INACTIVE;
    }

    // Calculate half dimensions
    float hw = width / 2.0f;
    float hh = height / 2.0f;

    // For 90/270 rotation, convert dimensions between X/Y coordinate systems
    float hw_rotated = hh / UI_ASPECT_RATIO;
    float hh_rotated = hw * UI_ASPECT_RATIO;

    float corners[4][2];

    // Rotate texture by remapping which texture corner goes to which screen position
    switch (direction) {
        case 0: // Up (original orientation)
            corners[0][0] = ox - hw; corners[0][1] = oy - hh;
            corners[1][0] = ox - hw; corners[1][1] = oy + hh;
            corners[2][0] = ox + hw; corners[2][1] = oy + hh;
            corners[3][0] = ox + hw; corners[3][1] = oy - hh;
            break;
        case 1: // Right (90 clockwise)
            corners[0][0] = ox + hw_rotated; corners[0][1] = oy - hh_rotated;
            corners[1][0] = ox - hw_rotated; corners[1][1] = oy - hh_rotated;
            corners[2][0] = ox - hw_rotated; corners[2][1] = oy + hh_rotated;
            corners[3][0] = ox + hw_rotated; corners[3][1] = oy + hh_rotated;
            break;
        case 2: // Down (180)
            corners[0][0] = ox + hw; corners[0][1] = oy + hh;
            corners[1][0] = ox + hw; corners[1][1] = oy - hh;
            corners[2][0] = ox - hw; corners[2][1] = oy - hh;
            corners[3][0] = ox - hw; corners[3][1] = oy + hh;
            break;
        case 3: // Left (270 clockwise)
            corners[0][0] = ox - hw_rotated; corners[0][1] = oy + hh_rotated;
            corners[1][0] = ox + hw_rotated; corners[1][1] = oy + hh_rotated;
            corners[2][0] = ox + hw_rotated; corners[2][1] = oy - hh_rotated;
            corners[3][0] = ox - hw_rotated; corners[3][1] = oy - hh_rotated;
            break;
        default:
            corners[0][0] = ox - hw; corners[0][1] = oy - hh;
            corners[1][0] = ox - hw; corners[1][1] = oy + hh;
            corners[2][0] = ox + hw; corners[2][1] = oy + hh;
            corners[3][0] = ox + hw; corners[3][1] = oy - hh;
            break;
    }

    for (int i = 0; i < 4; i++) {
        buttonQuad.m_aafPos[i][0] = corners[i][0];
        buttonQuad.m_aafPos[i][1] = corners[i][1];
    }

    m_quads.push_back(buttonQuad);
}

void GamepadWidget::addTriggerButton(float centerX, float centerY, float width, float height,
                                      float value, bool isLeft) {
    float ox = centerX, oy = centerY;
    applyOffset(ox, oy);

    float hw = width / 2.0f;
    float hh = height / 2.0f;

    // Fill mode: draw trigger shape with quads that fill from bottom to top
    if (getCurrentLayout().triggerFillMode == 1) {
        // SVG-accurate trigger shape using multiple segments
        // Based on the SVG path: 89x61 viewBox with curved outer edge
        // The outer edge (left for LT, right for RT) curves inward at the top
        // The inner edge is more vertical
        //
        // Define outline points as (outerX, innerX) at different Y levels (0=top, 1=bottom)
        // Normalized to width, where 0=left edge, 1=right edge
        // These approximate the SVG Bezier curves
        // SVG path analysis: viewBox 89x61, transform translate(68.02, -0.45)
        // After transform, shape spans roughly x=1 to x=90, y=0 to y=62
        // Normalized to 0-1 range based on the 89x61 dimensions:
        // - Inner edge (right): nearly vertical at x≈89 (normalized ~1.0)
        // - Outer edge (left): curves from x≈68 at top to x≈0 at bottom
        // - Top width: ~22 pixels = 22/89 ≈ 0.247 (from x=68 to x=90)
        // - The outer edge curve follows the SVG bezier path
        struct OutlinePoint { float y; float outer; float inner; };
        constexpr int NUM_POINTS = 9;
        const OutlinePoint outline[NUM_POINTS] = {
            { 0.00f, 0.85f, 0.98f },  // Top: very narrow, curves to top-right
            { 0.04f, 0.50f, 0.98f },  // Outer curves out quickly
            { 0.10f, 0.30f, 0.99f },  // Continuing curve
            { 0.20f, 0.15f, 0.99f },  // Upper: outer still curving
            { 0.35f, 0.04f, 1.00f },  // Mid-upper: outer nearly at edge
            { 0.55f, 0.00f, 1.00f },  // Mid: outer at full width
            { 0.80f, 0.00f, 1.00f },  // Lower: stays at full width
            { 0.92f, 0.00f, 1.00f },  // Bottom-right corner (inner edge ends here)
            { 1.00f, 0.00f, 0.00f },  // Bottom-left corner (tapers to point)
        };

        float baseX = ox - hw;  // Left edge of bounding box
        float topY = oy - hh;

        // Helper lambda to get X positions at a given Y ratio
        auto getEdgeX = [&](float yRatio, bool getOuter) -> float {
            // Find the two points to interpolate between
            int i = 0;
            for (; i < NUM_POINTS - 1 && outline[i + 1].y < yRatio; ++i) {}
            if (i >= NUM_POINTS - 1) i = NUM_POINTS - 2;

            float t = (yRatio - outline[i].y) / (outline[i + 1].y - outline[i].y);
            float val = getOuter
                ? outline[i].outer + t * (outline[i + 1].outer - outline[i].outer)
                : outline[i].inner + t * (outline[i + 1].inner - outline[i].inner);

            // Convert normalized to actual X, accounting for left/right trigger mirroring
            if (isLeft) {
                return baseX + val * width;  // outer=left edge, inner=right edge
            } else {
                return baseX + (1.0f - val) * width;  // mirror: outer=right, inner=left
            }
        };

        // Draw background segments
        for (int i = 0; i < NUM_POINTS - 1; ++i) {
            float y0 = topY + outline[i].y * height;
            float y1 = topY + outline[i + 1].y * height;

            float outerX0, innerX0, outerX1, innerX1;
            if (isLeft) {
                outerX0 = baseX + outline[i].outer * width;
                innerX0 = baseX + outline[i].inner * width;
                outerX1 = baseX + outline[i + 1].outer * width;
                innerX1 = baseX + outline[i + 1].inner * width;
            } else {
                // Mirror for right trigger
                outerX0 = baseX + (1.0f - outline[i].outer) * width;
                innerX0 = baseX + (1.0f - outline[i].inner) * width;
                outerX1 = baseX + (1.0f - outline[i + 1].outer) * width;
                innerX1 = baseX + (1.0f - outline[i + 1].inner) * width;
            }

            SPluginQuad_t bgQuad;
            bgQuad.m_iSprite = SpriteIndex::SOLID_COLOR;
            bgQuad.m_ulColor = COLOR_INACTIVE;
            if (isLeft) {
                bgQuad.m_aafPos[0][0] = outerX0; bgQuad.m_aafPos[0][1] = y0;
                bgQuad.m_aafPos[1][0] = outerX1; bgQuad.m_aafPos[1][1] = y1;
                bgQuad.m_aafPos[2][0] = innerX1; bgQuad.m_aafPos[2][1] = y1;
                bgQuad.m_aafPos[3][0] = innerX0; bgQuad.m_aafPos[3][1] = y0;
            } else {
                bgQuad.m_aafPos[0][0] = innerX0; bgQuad.m_aafPos[0][1] = y0;
                bgQuad.m_aafPos[1][0] = innerX1; bgQuad.m_aafPos[1][1] = y1;
                bgQuad.m_aafPos[2][0] = outerX1; bgQuad.m_aafPos[2][1] = y1;
                bgQuad.m_aafPos[3][0] = outerX0; bgQuad.m_aafPos[3][1] = y0;
            }
            m_quads.push_back(bgQuad);
        }

        // Draw fill segments (from bottom up based on value)
        if (value > 0.01f) {
            float fillStartY = 1.0f - value;  // Y ratio where fill starts (0=top, 1=bottom)

            for (int i = 0; i < NUM_POINTS - 1; ++i) {
                float segTopY = outline[i].y;
                float segBotY = outline[i + 1].y;

                // Skip segments entirely above the fill level
                if (segBotY <= fillStartY) continue;

                // Clip segment to fill level
                float clippedTopY = std::max(segTopY, fillStartY);
                float clippedBotY = segBotY;

                float y0 = topY + clippedTopY * height;
                float y1 = topY + clippedBotY * height;

                float outerX0 = getEdgeX(clippedTopY, true);
                float innerX0 = getEdgeX(clippedTopY, false);
                float outerX1 = getEdgeX(clippedBotY, true);
                float innerX1 = getEdgeX(clippedBotY, false);

                SPluginQuad_t fillQuad;
                fillQuad.m_iSprite = SpriteIndex::SOLID_COLOR;
                fillQuad.m_ulColor = ColorConfig::getInstance().getPrimary();
                if (isLeft) {
                    fillQuad.m_aafPos[0][0] = outerX0; fillQuad.m_aafPos[0][1] = y0;
                    fillQuad.m_aafPos[1][0] = outerX1; fillQuad.m_aafPos[1][1] = y1;
                    fillQuad.m_aafPos[2][0] = innerX1; fillQuad.m_aafPos[2][1] = y1;
                    fillQuad.m_aafPos[3][0] = innerX0; fillQuad.m_aafPos[3][1] = y0;
                } else {
                    fillQuad.m_aafPos[0][0] = innerX0; fillQuad.m_aafPos[0][1] = y0;
                    fillQuad.m_aafPos[1][0] = innerX1; fillQuad.m_aafPos[1][1] = y1;
                    fillQuad.m_aafPos[2][0] = outerX1; fillQuad.m_aafPos[2][1] = y1;
                    fillQuad.m_aafPos[3][0] = outerX0; fillQuad.m_aafPos[3][1] = y0;
                }
                m_quads.push_back(fillQuad);
            }
        }
    } else {
        // Fade mode (default): use texture with brightness interpolation
        int spriteIndex = 0;
        if (m_textureVariant > 0) {
            const char* textureName = isLeft ? "gamepad_trigger_button_l" : "gamepad_trigger_button_r";
            spriteIndex = AssetManager::getInstance().getSpriteIndex(textureName, m_textureVariant);
        }

        SPluginQuad_t buttonQuad;
        if (spriteIndex > 0) {
            buttonQuad.m_iSprite = spriteIndex;
            // Interpolate color from dark to white based on trigger value
            int brightness = 40 + static_cast<int>(value * 215);  // 40 to 255
            buttonQuad.m_ulColor = PluginUtils::makeColor(brightness, brightness, brightness);
        } else {
            buttonQuad.m_iSprite = SpriteIndex::SOLID_COLOR;
            buttonQuad.m_ulColor = value > 0.5f ? COLOR_TRIGGER : COLOR_INACTIVE;
        }

        setQuadPositions(buttonQuad, ox - hw, oy - hh, width, height);
        m_quads.push_back(buttonQuad);
    }
}

void GamepadWidget::addBumperButton(float centerX, float centerY, float width, float height,
                                     bool isPressed, bool isLeft) {
    float ox = centerX, oy = centerY;
    applyOffset(ox, oy);

    // Get sprite index for bumper texture
    int spriteIndex = 0;
    if (m_textureVariant > 0) {
        const char* textureName = isLeft ? "gamepad_bumper_button_l" : "gamepad_bumper_button_r";
        spriteIndex = AssetManager::getInstance().getSpriteIndex(textureName, m_textureVariant);
    }

    SPluginQuad_t buttonQuad;
    if (spriteIndex > 0) {
        buttonQuad.m_iSprite = spriteIndex;
        if (isPressed) {
            buttonQuad.m_ulColor = ColorPalette::WHITE;
        } else {
            buttonQuad.m_ulColor = PluginUtils::makeColor(40, 40, 40);
        }
    } else {
        buttonQuad.m_iSprite = SpriteIndex::SOLID_COLOR;
        buttonQuad.m_ulColor = isPressed ? COLOR_BUMPER : COLOR_INACTIVE;
    }

    float hw = width / 2.0f;
    float hh = height / 2.0f;
    setQuadPositions(buttonQuad, ox - hw, oy - hh, width, height);

    m_quads.push_back(buttonQuad);
}

void GamepadWidget::addMenuButton(float centerX, float centerY, float width, float height,
                                   bool isPressed, const char* label) {
    const auto dims = getScaledDimensions();

    float ox = centerX, oy = centerY;
    applyOffset(ox, oy);

    // Get sprite index for menu button texture, fall back to face_button if not available
    int spriteIndex = 0;
    if (m_textureVariant > 0) {
        spriteIndex = AssetManager::getInstance().getSpriteIndex("gamepad_menu_button", m_textureVariant);
        if (spriteIndex == 0) {
            spriteIndex = AssetManager::getInstance().getSpriteIndex("gamepad_face_button", m_textureVariant);
        }
    }

    SPluginQuad_t buttonQuad;
    if (spriteIndex > 0) {
        buttonQuad.m_iSprite = spriteIndex;
        if (isPressed) {
            buttonQuad.m_ulColor = ColorPalette::WHITE;
        } else {
            buttonQuad.m_ulColor = PluginUtils::makeColor(40, 40, 40);
        }
    } else {
        buttonQuad.m_iSprite = SpriteIndex::SOLID_COLOR;
        buttonQuad.m_ulColor = isPressed ? COLOR_MENUBTN : COLOR_INACTIVE;
    }
    setQuadPositions(buttonQuad, ox - width / 2, oy - height / 2, width, height);
    m_quads.push_back(buttonQuad);

    // Add label text
    if (label) {
        float labelFontSize = dims.fontSize * 0.5f;
        addString(label, centerX, centerY - labelFontSize * 0.4f, Justify::CENTER,
            Fonts::getSmall(), COLOR_MENUBTN, labelFontSize);
    }
}

void GamepadWidget::resetToDefaults() {
    m_bVisible = false;  // Hidden by default
    m_bShowTitle = false;  // No title (overlays gamepad texture)
    setTextureVariant(1);  // Default to texture variant 1
    m_fBackgroundOpacity = 1.0f;  // 100% opacity
    m_fScale = 1.0f;
    setPosition(0.374f, 0.74137f);
    // Reset layouts to defaults
    initDefaultLayouts();
    setDataDirty();
}

GamepadWidget::LayoutConfig& GamepadWidget::getLayout(int variant) {
    // If layout doesn't exist, create with defaults
    if (m_layouts.find(variant) == m_layouts.end()) {
        m_layouts[variant] = LayoutConfig{};
    }
    return m_layouts[variant];
}

const GamepadWidget::LayoutConfig& GamepadWidget::getCurrentLayout() const {
    auto it = m_layouts.find(m_textureVariant);
    if (it != m_layouts.end()) {
        return it->second;
    }
    // Fallback to default layout
    static LayoutConfig defaultLayout;
    return defaultLayout;
}

void GamepadWidget::initDefaultLayouts() {
    m_layouts.clear();

    // Layout for variant 1 (tuned offsets, original textures)
    LayoutConfig& layout1 = m_layouts[1];
    layout1.backgroundWidth = 750.0f;
    layout1.triggerWidth = 89.0f;
    layout1.triggerHeight = 61.0f;
    layout1.bumperWidth = 171.0f;
    layout1.bumperHeight = 63.0f;
    layout1.dpadWidth = 32.0f;
    layout1.dpadHeight = 53.0f;
    layout1.faceButtonSize = 50.0f;
    layout1.menuButtonWidth = 33.0f;
    layout1.menuButtonHeight = 33.0f;
    layout1.stickSize = 83.0f;
    layout1.leftTriggerX = 0.041f;
    layout1.leftTriggerY = -0.022f;
    layout1.rightTriggerX = -0.041f;
    layout1.rightTriggerY = -0.022f;
    layout1.leftBumperX = -0.01f;
    layout1.leftBumperY = 0.021f;
    layout1.rightBumperX = 0.01f;
    layout1.rightBumperY = 0.021f;
    layout1.leftStickX = 0.015f;
    layout1.leftStickY = 0.02f;
    layout1.rightStickX = -0.049f;
    layout1.rightStickY = 0.09f;
    layout1.dpadX = 0.0473f;
    layout1.dpadY = 0.0045f;
    layout1.faceButtonsX = -0.0162f;
    layout1.faceButtonsY = -0.0706f;
    layout1.menuButtonsX = 0.0004f;
    layout1.menuButtonsY = -0.0756f;
    layout1.dpadSpacing = 0.95f;
    layout1.faceButtonSpacing = 1.0f;
    layout1.menuButtonSpacing = 1.14f;

    // Layout for variant 2 (different texture dimensions, 806x453 background)
    LayoutConfig& layout2 = m_layouts[2];
    layout2.backgroundWidth = 806.0f;
    layout2.triggerWidth = 99.0f;
    layout2.triggerHeight = 91.0f;
    layout2.bumperWidth = 99.0f;
    layout2.bumperHeight = 22.0f;
    layout2.dpadWidth = 32.0f;
    layout2.dpadHeight = 45.0f;
    layout2.faceButtonSize = 52.0f;
    layout2.menuButtonWidth = 27.0f;
    layout2.menuButtonHeight = 45.0f;
    layout2.stickSize = 94.0f;
    layout2.leftTriggerX = 0.0238f;
    layout2.leftTriggerY = -0.0221f;
    layout2.rightTriggerX = -0.0238f;
    layout2.rightTriggerY = -0.0221f;
    layout2.leftBumperX = -0.0133f;
    layout2.leftBumperY = 0.012f;
    layout2.rightBumperX = 0.0133f;
    layout2.rightBumperY = 0.012f;
    layout2.leftStickX = 0.0398f;
    layout2.leftStickY = 0.0873f;
    layout2.rightStickX = -0.041f;
    layout2.rightStickY = 0.0873f;
    layout2.dpadX = 0.001f;
    layout2.dpadY = -0.066f;
    layout2.faceButtonsX = -0.0023f;
    layout2.faceButtonsY = -0.066f;
    layout2.menuButtonsX = 0.0001f;
    layout2.menuButtonsY = -0.1195f;
    layout2.dpadSpacing = 1.55f;
    layout2.faceButtonSpacing = 1.1f;
    layout2.menuButtonSpacing = 5.51f;
}
