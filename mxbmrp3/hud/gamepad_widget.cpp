// ============================================================================
// hud/gamepad_widget.cpp
// Displays controller button overlay - shows pressed buttons, sticks, triggers
// ============================================================================
#include "gamepad_widget.h"
#include "../core/layout_config.h"
#include "../core/plugin_utils.h"
#include "../core/color_config.h"
#include "../core/asset_manager.h"
#include "../core/xinput_reader.h"
#include "../diagnostics/logger.h"
#include <cmath>
#include <cstring>
#include <algorithm>

using namespace PluginConstants;

GamepadWidget::GamepadWidget() {
    // No caption on this panel -- see BaseHud::m_titleSupported.
    disableTitle();
    // The controller artwork IS this widget -- see BaseHud::m_textureRequired.
    m_textureRequired = true;
    m_packKind = PackKind::Gamepad;

    m_panelKind = PanelKind::Widget;
    m_bContentCard = true;
    DEBUG_INFO("GamepadWidget created");
    setDraggable(true);

    // Zero the rebuild-gate snapshot INCLUDING padding: the update() gate
    // memcmp-compares whole structs, and default-init leaves padding bytes
    // indeterminate — later copy-assigns from XInputReader's object keep ITS
    // padding, so after the first real frame both sides agree, but this makes
    // the comparison well-defined from the very first compare on any toolchain
    // (worst case without it was a redundant rebuild, never a wrong frame).
    // The void* cast is the documented way to tell the compiler this is a
    // deliberate byte-clear (-Wclass-memaccess fires on the typed form because
    // XInputData is not trivially default-constructible). Padding is exactly
    // what we are here to zero, so a typed assignment would not do.
    std::memset(static_cast<void*>(&m_lastRenderedInput), 0, sizeof(m_lastRenderedInput));

    // Pre-allocate render buffers
    m_quads.reserve(50);  // Sticks + triggers + bumpers + face + dpad + menu buttons
    m_strings.reserve(10);

    // No setTextureBaseName here: the background is the selected PACK's art, not a
    // variant of a shared texture, so it is resolved through activePack() in
    // rebuildRenderData rather than through BaseHud's variant machinery.

    // Set all configurable defaults
    resetToDefaults();

    rebuildRenderData();
}

void GamepadWidget::setGamepadPack(const std::string& name) {
    if (m_gamepadPack == name) return;
    m_gamepadPack = name;
    setDataDirty();
}

const GamepadAsset* GamepadWidget::activePack() const {
    const AssetManager& assets = AssetManager::getInstance();
    // Degrade, do not blank: a name this install has no folder for falls back to
    // the shipped pack, and m_gamepadPack is deliberately NOT rewritten -- putting
    // the folder back restores the user's choice without them re-picking it.
    if (const GamepadAsset* named = assets.getGamepadByName(m_gamepadPack)) return named;
    return assets.getDefaultGamepad();
}

int GamepadWidget::packSprite(GamepadSprite::Part part) const {
    const GamepadAsset* pack = activePack();
    return pack ? pack->sprites[part] : 0;
}

void GamepadWidget::update() {
    // OPTIMIZATION: Skip processing when not visible
    if (!isVisibleAnySurface()) {
        clearDataDirty();
        clearLayoutDirty();
        return;
    }

    // Rebuild only when the rendered inputs actually changed. The widget reads
    // XInputReader DIRECTLY (not PluginData), so the InputTelemetry dirty flag
    // alone would freeze it in modes with no telemetry callbacks (replay,
    // spectate) — instead a POD snapshot of the controller state is the change
    // signal. An idle controller at 480fps skips the few-hundred-quad rebuild
    // entirely; a moving one rebuilds at most once per render frame, as before.
    const XInputData& xinput = XInputReader::getInstance().getData();
    const bool inputChanged = !m_hasRenderedInput ||
        std::memcmp(&xinput, &m_lastRenderedInput, sizeof(XInputData)) != 0;
    if (inputChanged || isDataDirty() || isLayoutDirty()) {
        m_lastRenderedInput = xinput;
        m_hasRenderedInput = true;
        rebuildAndRecord();
    }
    clearDataDirty();
    clearLayoutDirty();
}

bool GamepadWidget::handlesDataType(DataChangeType dataType) const {
    return dataType == DataChangeType::InputTelemetry;
}

void GamepadWidget::rebuildRenderData() {
    m_quads.clear();
    clearStrings();

    const auto dims = getScaledDimensions();
    const XInputData& xinput = XInputReader::getInstance().getData();

    // THE FRAME IS THE UNIT: size the frame from the type, then read back the em
    // that produced it and spend every interior distance in that em. See
    // hud/gamepad_geometry.h for why the interior cannot be measured against the
    // global grid, and for the two bugs that came of trying.
    float backgroundWidth = PluginUtils::calculateMonospaceTextWidth(BACKGROUND_WIDTH_CHARS, dims.fontSize)
        + dims.paddingH + dims.paddingH;

    const float fontSize = GamepadLayout::interiorEm(backgroundWidth);
    const float u = GamepadLayout::unitScale(fontSize);          // authored units -> screen
    const float charW = fontSize * GamepadLayout::kCharRatio;    // one char == one cell here
    const float lineH = fontSize * GamepadLayout::kLineRatio;
    const float padH  = charW * GamepadLayout::kPadChars;
    const float padV  = lineH;                    // vertical padding (HUD_VERTICAL == NORMAL when authored)

    float stickHeight = STICK_HEIGHT_LINES * lineH;

    // The pack supplies BOTH the artwork and the numbers that place buttons on it;
    // they are one unit, which is why a pad is a pack rather than a texture variant.
    const GamepadAsset* pack = activePack();
    static const GamepadLayout::PadGeometry kNoPackGeometry;
    const GamepadLayout::PadGeometry& layout = pack ? pack->geometry : kNoPackGeometry;

    // Keep BaseHud's background sprite pointing at the active pack. Assigned only on
    // change: setBackgroundTextureIndex invalidates the theme memo, and this runs on
    // every rebuild.
    const int packBackground = pack ? pack->sprites[GamepadSprite::BACKGROUND] : 0;
    if (getBackgroundTextureIndex() != packBackground) setBackgroundTextureIndex(packBackground);

    // Layout: triggers/bumpers row + sticks row + buttons row (face/dpad/menu)
    // Content proportions unchanged - extra texture padding handled via background aspect ratio
    float triggersHeight = lineH * 1.2f;
    // Background height from current layout's texture aspect ratio
    // Multiply by UI_ASPECT_RATIO to convert from texture pixels to screen coordinates
    float backgroundHeight = backgroundWidth * (layout.backgroundHeight / layout.backgroundWidth) * UI_ASPECT_RATIO;
    float stickWidthForLayout = STICK_HEIGHT_LINES * lineH / UI_ASPECT_RATIO;

    // The BOX lands on the lattice. Only the HEIGHT is off here -- it is derived from
    // the controller texture's aspect through UI_ASPECT_RATIO, so it lands wherever the
    // art does -- and the art keeps its size: ORIGIN_* re-centres the content in the
    // snapped box rather than stretching the controller. See fitPanelToGrid.
    const GridFit fit = fitPanelToGrid(backgroundWidth, backgroundHeight);
    setBounds(START_X, START_Y, START_X + fit.w, START_Y + fit.h);
    const float ORIGIN_X = START_X + fit.padX;
    const float ORIGIN_Y = START_Y + fit.padY;

    // When not connected, tint the background texture dark instead of full brightness
    // bg-quad-exempt: draws the texture a SECOND time in a custom dark tint on top of
    // the normal background; addBackgroundQuad has no tint parameter and this is an
    // extra layer, not a replacement for it.
    if (!xinput.isConnected && m_bShowBackgroundTexture && m_iBackgroundTextureIndex > 0) {
        SPluginQuad_t quadEntry;
        float bgX = ORIGIN_X, bgY = ORIGIN_Y, bgW = backgroundWidth, bgH = backgroundHeight;
        applyOffset(bgX, bgY);
        applyTextureAspectCorrection(bgX, bgY, bgW, bgH);
        setQuadPositions(quadEntry, bgX, bgY, bgW, bgH);
        // bg-quad-exempt: same disconnected-tint layer as the condition above.
        quadEntry.m_iSprite = m_iBackgroundTextureIndex;
        // Dark tint instead of white - texture shape preserved but appears blacked out
        unsigned long darkTint = PluginUtils::makeColor(15, 15, 15);
        quadEntry.m_ulColor = PluginUtils::applyOpacity(darkTint, m_fBackgroundOpacity);
        m_quads.push_back(quadEntry);

        // Centered disconnect message
        float centerX = ORIGIN_X + backgroundWidth / 2;
        float centerY = ORIGIN_Y + backgroundHeight / 2;
        int ctrlNum = XInputReader::getInstance().getControllerIndex() + 1; // 0-based to 1-based
        char titleBuf[64];
        snprintf(titleBuf, sizeof(titleBuf), "Controller %d Not Connected", ctrlNum);
        // Minus addString's own row centring, which compounds with this explicit one.
        addString(titleBuf, centerX, centerY - lineH * 0.5f - rowCenterOffset(fontSize),
                  Justify::CENTER, this->getFont(FontCategory::NORMAL), this->getColor(ColorSlot::NEGATIVE), fontSize);
        addString("Check MXBMRP3 Settings > General", centerX,
                  centerY + lineH * 0.5f - rowCenterOffset(fontSize * 0.8f),
                  Justify::CENTER, this->getFont(FontCategory::NORMAL), this->getColor(ColorSlot::MUTED), fontSize * 0.8f);
        return;
    }

    // Add background quad (normal brightness)
    addBackgroundQuad(ORIGIN_X, ORIGIN_Y, backgroundWidth, backgroundHeight);
    // No caption at all here -- not a title toggle switched off, NO title -- so
    // nothing reaches the caption path, and that is what emits the body card
    // the constructor asked for with m_bContentCard. Without this the flag drew
    // nothing while still reserving the card's clearance (contentPaddingX reads the
    // same flag), so a themed panel was a frame around bare content with an
    // unexplained cell of padding inside it. Same call the captioned widgets make
    // from their `else` branch.
    //
    // A sprite background opts a panel out of theming entirely, upstream of this
    // (resolveActiveTheme), so on the widgets that ship with one this is inert until
    // the sprite is switched off -- at which point they get the same treatment as
    // every other widget instead of the one they had.
    // check_hud_helpers.sh rule 10 fails the build if a new one forgets the call.
    emitContentCard(0.0f);

    float contentStartX = ORIGIN_X + padH;
    float contentStartY = ORIGIN_Y + padV;
    float currentY = contentStartY;
    float contentWidth = backgroundWidth - padH * 2;
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
    float ltOffsetX = layout.leftTriggerX * u;
    float ltOffsetY = layout.leftTriggerY * u;
    float ltCenterX = contentStartX + triggerWidth / 2 + ltOffsetX;
    float ltCenterY = triggerCenterY + ltOffsetY;
    addTriggerButton(ltCenterX, ltCenterY, triggerWidth, triggerHeight,
                     xinput.leftTrigger, true);

    // Left bumper (LB) - with offset
    float lbOffsetX = layout.leftBumperX * u;
    float lbOffsetY = layout.leftBumperY * u;
    float lbCenterX = contentStartX + triggerWidth + charW + bumperWidth / 2 + lbOffsetX;
    float lbCenterY = triggerCenterY + lbOffsetY;
    addBumperButton(lbCenterX, lbCenterY, bumperWidth, bumperHeight,
                    xinput.leftShoulder, true);

    // Right bumper (RB) - with offset
    float rbOffsetX = layout.rightBumperX * u;
    float rbOffsetY = layout.rightBumperY * u;
    float rbCenterX = contentStartX + contentWidth - triggerWidth - charW - bumperWidth / 2 + rbOffsetX;
    float rbCenterY = triggerCenterY + rbOffsetY;
    addBumperButton(rbCenterX, rbCenterY, bumperWidth, bumperHeight,
                    xinput.rightShoulder, false);

    // Right trigger (RT) - with offset
    float rtOffsetX = layout.rightTriggerX * u;
    float rtOffsetY = layout.rightTriggerY * u;
    float rtCenterX = contentStartX + contentWidth - triggerWidth / 2 + rtOffsetX;
    float rtCenterY = triggerCenterY + rtOffsetY;
    addTriggerButton(rtCenterX, rtCenterY, triggerWidth, triggerHeight,
                     xinput.rightTrigger, false);

    currentY += triggersHeight;

    // ========================================================================
    // ROW 2: Analog Sticks
    // ========================================================================
    // Use stickWidthForLayout for X positioning (preserves original positions)
    float stickSpacing = STICK_SPACING_CHARS * charW;

    // Left stick - with offset
    float lsOffsetX = layout.leftStickX * u;
    float lsOffsetY = layout.leftStickY * u;
    float leftStickCenterX = contentStartX + stickWidthForLayout / 2 + lsOffsetX;
    float leftStickCenterY = currentY + stickHeight / 2 + lsOffsetY;
    addStick(leftStickCenterX, leftStickCenterY, xinput.leftStickX, xinput.leftStickY,
             stickWidthForLayout, stickHeight, backgroundWidth, layout, xinput.leftThumb);

    // Right stick - with offset
    float rsOffsetX = layout.rightStickX * u;
    float rsOffsetY = layout.rightStickY * u;
    float rightStickCenterX = contentStartX + stickWidthForLayout + stickSpacing + stickWidthForLayout / 2 + rsOffsetX;
    float rightStickCenterY = currentY + stickHeight / 2 + rsOffsetY;
    addStick(rightStickCenterX, rightStickCenterY, xinput.rightStickX, xinput.rightStickY,
             stickWidthForLayout, stickHeight, backgroundWidth, layout, xinput.rightThumb);

    currentY += stickHeight;

    // ========================================================================
    // ROW 3: D-Pad, Menu Buttons, Face Buttons
    // ========================================================================
    float buttonRowY = currentY + lineH * 0.15f;

    // D-Pad (left side, aligned with left stick) - with offset
    float dpadOffsetX = layout.dpadX * u;
    float dpadOffsetY = layout.dpadY * u;
    float dpadCenterX = contentStartX + stickWidthForLayout / 2 + dpadOffsetX;
    float dpadCenterY = buttonRowY + lineH * 0.9f + dpadOffsetY;

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
    float menuOffsetX = layout.menuButtonsX * u;
    float menuOffsetY = layout.menuButtonsY * u;
    float menuBtnWidth = backgroundWidth * (layout.menuButtonWidth / layout.backgroundWidth);
    float menuBtnHeight = menuBtnWidth * (layout.menuButtonHeight / layout.menuButtonWidth) * UI_ASPECT_RATIO;
    float menuCenterX = contentStartX + contentWidth / 2 + menuOffsetX;
    float menuCenterY = buttonRowY + lineH * 0.7f + menuBtnHeight / 2 + menuOffsetY;
    float menuSpacing = menuBtnWidth * layout.menuButtonSpacing;

    // Back (select)
    addMenuButton(menuCenterX - menuSpacing - menuBtnWidth / 2, menuCenterY,
                  menuBtnWidth, menuBtnHeight, xinput.buttonBack);

    // Start
    addMenuButton(menuCenterX + menuSpacing + menuBtnWidth / 2, menuCenterY,
                  menuBtnWidth, menuBtnHeight, xinput.buttonStart);

    // Face buttons (right side, aligned with right stick) - diamond layout - with offset
    float faceOffsetX = layout.faceButtonsX * u;
    float faceOffsetY = layout.faceButtonsY * u;
    float faceButtonSize = backgroundWidth * (layout.faceButtonSize / layout.backgroundWidth) * UI_ASPECT_RATIO;
    float faceCenterX = contentStartX + stickWidthForLayout + stickSpacing + stickWidthForLayout / 2 + faceOffsetX;
    float faceCenterY = buttonRowY + lineH * 0.9f + faceOffsetY;
    float faceSpacing = faceButtonSize * layout.faceButtonSpacing;

    // Top (Y / Triangle)
    addFaceButton(faceCenterX, faceCenterY - faceSpacing, faceButtonSize, xinput.buttonY, "Y");
    // Bottom (A / Cross)
    addFaceButton(faceCenterX, faceCenterY + faceSpacing, faceButtonSize, xinput.buttonA, "A");
    // Left (X / Square)
    addFaceButton(faceCenterX - faceSpacing / UI_ASPECT_RATIO, faceCenterY, faceButtonSize, xinput.buttonX, "X");
    // Right (B / Circle)
    addFaceButton(faceCenterX + faceSpacing / UI_ASPECT_RATIO, faceCenterY, faceButtonSize, xinput.buttonB, "B");
}

void GamepadWidget::addStick(float centerX, float centerY, float stickX, float stickY,
                              float width, float height, float backgroundWidth,
                              const GamepadLayout::PadGeometry& layout, bool isPressed) {
    float ox = centerX, oy = centerY;
    applyOffset(ox, oy);

    // Try to use stick sprite texture
    int spriteIndex = packSprite(GamepadSprite::STICK);

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
    // Fade with the background-opacity slider so the inputs dim together with the texture,
    // applied after the press-state color so L3/R3 still reads through the fade.
    markerQuad.m_ulColor = PluginUtils::applyOpacity(markerQuad.m_ulColor, m_fBackgroundOpacity);
    m_quads.push_back(markerQuad);
}

void GamepadWidget::addFaceButton(float centerX, float centerY, float size, bool isPressed, const char* label) {
    float ox = centerX, oy = centerY;
    applyOffset(ox, oy);

    float buttonWidth = size / UI_ASPECT_RATIO;
    float buttonHeight = size;

    // Map label to button index: 1=A, 2=B, 3=X, 4=Y
    int buttonIndex = 0;
    if (label) {
        if (strcmp(label, "A") == 0) buttonIndex = 1;
        else if (strcmp(label, "B") == 0) buttonIndex = 2;
        else if (strcmp(label, "X") == 0) buttonIndex = 3;
        else if (strcmp(label, "Y") == 0) buttonIndex = 4;
    }

    // Per-button art from the pack: face_button_<n>.tga and face_button_<n>_pressed.tga.
    // The two runs are contiguous in GamepadSprite::Part (static_assert'd there), so
    // the button number indexes straight into each.
    int unpressedSprite = 0;
    int pressedSprite = 0;
    if (buttonIndex > 0) {
        const int n = buttonIndex - 1;
        unpressedSprite = packSprite(static_cast<GamepadSprite::Part>(GamepadSprite::FACE_1 + n));
        pressedSprite = packSprite(static_cast<GamepadSprite::Part>(GamepadSprite::FACE_1_PRESSED + n));
    }

    SPluginQuad_t buttonQuad;
    if (unpressedSprite > 0 || pressedSprite > 0) {
        if (isPressed && pressedSprite > 0) {
            buttonQuad.m_iSprite = pressedSprite;
        } else if (unpressedSprite > 0) {
            buttonQuad.m_iSprite = unpressedSprite;
        } else {
            buttonQuad.m_iSprite = pressedSprite;
        }
        buttonQuad.m_ulColor = ColorPalette::WHITE;
    } else {
        // Fallback to solid color
        buttonQuad.m_iSprite = SpriteIndex::SOLID_COLOR;
        buttonQuad.m_ulColor = isPressed ? COLOR_DPAD : COLOR_INACTIVE;
    }
    // Fade with the background-opacity slider (after press-state color so the press still reads).
    buttonQuad.m_ulColor = PluginUtils::applyOpacity(buttonQuad.m_ulColor, m_fBackgroundOpacity);
    setQuadPositions(buttonQuad, ox - buttonWidth / 2, oy - buttonHeight / 2, buttonWidth, buttonHeight);
    m_quads.push_back(buttonQuad);
}

void GamepadWidget::addDpadButton(float centerX, float centerY, float width, float height,
                                   bool isPressed, int direction) {
    float ox = centerX, oy = centerY;
    applyOffset(ox, oy);

    int spriteIndex = packSprite(GamepadSprite::DPAD_BUTTON);

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
    // Fade with the background-opacity slider (after press-state color so the press still reads).
    buttonQuad.m_ulColor = PluginUtils::applyOpacity(buttonQuad.m_ulColor, m_fBackgroundOpacity);

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
    if (m_triggerFillMode == 1) {
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
            bgQuad.m_ulColor = PluginUtils::applyOpacity(COLOR_INACTIVE, m_fBackgroundOpacity);
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
                // Fill height already encodes the trigger % (press amount); fade its alpha only.
                fillQuad.m_ulColor = PluginUtils::applyOpacity(this->getColor(ColorSlot::PRIMARY), m_fBackgroundOpacity);
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
        int spriteIndex = packSprite(isLeft ? GamepadSprite::TRIGGER_L : GamepadSprite::TRIGGER_R);

        SPluginQuad_t buttonQuad;
        if (spriteIndex > 0) {
            buttonQuad.m_iSprite = spriteIndex;
            // Interpolate color from dark to white based on trigger value.
            //
            // Clamped rather than cast, but DEFENSIVELY — not fixing a live bug.
            // Today's only caller passes XInputData::leftTrigger/rightTrigger,
            // which normalizeTriggerValue() bounds to exactly 0..1, so this maxes
            // at 255 and never wraps. The clamp is here because makeColor takes
            // uint8_t while the arithmetic is int: an unnormalised value from a
            // future caller would wrap to a DARK button instead of saturating
            // white, which is a silent-wrong-pixel failure rather than a loud one.
            // MSVC /W4 flagged the implicit narrowing (C4244); GCC's
            // -Wall -Wextra does not cover that family at all.
            const int brightnessI = 40 + static_cast<int>(value * 215);  // 40 to 255
            const uint8_t brightness =
                static_cast<uint8_t>(brightnessI < 0 ? 0 : (brightnessI > 255 ? 255 : brightnessI));
            buttonQuad.m_ulColor = PluginUtils::makeColor(brightness, brightness, brightness);
        } else {
            buttonQuad.m_iSprite = SpriteIndex::SOLID_COLOR;
            buttonQuad.m_ulColor = value > 0.5f ? COLOR_TRIGGER : COLOR_INACTIVE;
        }

        // Fade with the background-opacity slider. Applied after the brightness ramp /
        // press color so the trigger % (encoded as brightness) still reads through the fade.
        buttonQuad.m_ulColor = PluginUtils::applyOpacity(buttonQuad.m_ulColor, m_fBackgroundOpacity);
        setQuadPositions(buttonQuad, ox - hw, oy - hh, width, height);
        m_quads.push_back(buttonQuad);
    }
}

void GamepadWidget::addBumperButton(float centerX, float centerY, float width, float height,
                                     bool isPressed, bool isLeft) {
    float ox = centerX, oy = centerY;
    applyOffset(ox, oy);

    int spriteIndex = packSprite(isLeft ? GamepadSprite::BUMPER_L : GamepadSprite::BUMPER_R);

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
    // Fade with the background-opacity slider (after press-state color so the press still reads).
    buttonQuad.m_ulColor = PluginUtils::applyOpacity(buttonQuad.m_ulColor, m_fBackgroundOpacity);

    float hw = width / 2.0f;
    float hh = height / 2.0f;
    setQuadPositions(buttonQuad, ox - hw, oy - hh, width, height);

    m_quads.push_back(buttonQuad);
}

void GamepadWidget::addMenuButton(float centerX, float centerY, float width, float height,
                                   bool isPressed) {
    float ox = centerX, oy = centerY;
    applyOffset(ox, oy);

    // Menu button art, falling back to the pack's generic face button. A pack is only
    // accepted with its whole sprite set present, so this fallback is now vestigial --
    // kept because it costs nothing and is the correct behaviour if a future pack
    // format ever makes menu_button optional.
    int spriteIndex = packSprite(GamepadSprite::MENU_BUTTON);
    if (spriteIndex == 0) spriteIndex = packSprite(GamepadSprite::FACE);

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
    // Fade with the background-opacity slider (after press-state color so the press still reads).
    buttonQuad.m_ulColor = PluginUtils::applyOpacity(buttonQuad.m_ulColor, m_fBackgroundOpacity);
    setQuadPositions(buttonQuad, ox - width / 2, oy - height / 2, width, height);
    m_quads.push_back(buttonQuad);
}

void GamepadWidget::resetToDefaults() {
    // NEVER THEMED, by default -- same reasoning as RadarHud: this widget is a pad
    // TEXTURE with the buttons lit on top of it, so the art is the panel and a themed
    // frame around it is decoration around decoration. See the note there for why this
    // is the override rather than deleted code paths.
    setThemeOverride(THEME_NONE);
    m_bVisible = false;  // Hidden by default
    m_bShowTitle = false;  // No title (overlays gamepad texture)
    // The pad artwork IS this widget. Through the setter, not the member: it owns the
    // theme-memo invalidation a background texture needs (and check_hud_helpers.sh
    // fails the build on a HUD touching the member directly).
    setShowBackgroundTexture(true);
    m_gamepadPack = AssetManager::DEFAULT_GAMEPAD;
    m_triggerFillMode = 0;
    m_fBackgroundOpacity = 1.0f;  // 100% opacity
    m_fScale = 1.0f;
    setPosition(cellsX(68), cellsY(62));
    setDataDirty();
}
