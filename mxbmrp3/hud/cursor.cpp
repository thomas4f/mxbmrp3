// ============================================================================
// hud/cursor.cpp
// Manages mouse cursor display and interaction state
// ============================================================================
#include "cursor.h"
#include "../core/input_manager.h"
#include "../diagnostics/logger.h"

bool CursorRenderer::addCursorQuad(std::vector<SPluginQuad_t>& quads) {
    const InputManager& input = InputManager::getInstance();

    // Only add cursor if it should be visible and position is valid
    if (!input.shouldShowCursor()) {
        return false;
    }

    const CursorPosition& cursor = input.getCursorPosition();
    if (!cursor.isValid) {
        return false;
    }

    // Create and add the cursor quad
    SPluginQuad_t cursorQuad;
    createCursorQuad(cursorQuad, cursor.x, cursor.y);
    quads.push_back(cursorQuad);
    return true;
}

void CursorRenderer::createCursorQuad(SPluginQuad_t& quad, float x, float y) {
    // Set quad vertices (counter-clockwise from top-left)
    // Top-left
    quad.m_aafPos[0][0] = x;
    quad.m_aafPos[0][1] = y;
    // Bottom-left
    quad.m_aafPos[1][0] = x;
    quad.m_aafPos[1][1] = y + SPRITE_HEIGHT;
    // Bottom-right
    quad.m_aafPos[2][0] = x + SPRITE_WIDTH;
    quad.m_aafPos[2][1] = y + SPRITE_HEIGHT;
    // Top-right
    quad.m_aafPos[3][0] = x + SPRITE_WIDTH;
    quad.m_aafPos[3][1] = y;

    // Set sprite properties
    quad.m_iSprite = POINTER_SPRITE_INDEX;
    quad.m_ulColor = SPRITE_COLOR;
}
