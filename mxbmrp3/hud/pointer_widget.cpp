// ============================================================================
// hud/pointer_widget.cpp
// Pointer widget - customizable mouse pointer rendered with quads
// ============================================================================
#include "pointer_widget.h"
#include "../core/input_manager.h"
#include "../core/color_config.h"
#include "../core/asset_manager.h"
#include "../diagnostics/logger.h"

using namespace PluginConstants;

PointerWidget::PointerWidget() {
    DEBUG_INFO("PointerWidget created");

    // Pointer is not draggable (it follows mouse position)
    setDraggable(false);

    // Set texture base name for dynamic texture discovery
    setTextureBaseName("pointer_widget");

    // Set defaults
    m_fScale = 1.0f;
    m_fBackgroundOpacity = 1.0f;  // 100% opacity for sprite-based rendering

    // Pre-allocate vectors (4 quads: 2 shadow + 2 foreground)
    m_quads.reserve(4);

    rebuildRenderData();
}

bool PointerWidget::handlesDataType(DataChangeType dataType) const {
    // Pointer doesn't depend on any game data
    return false;
}

void PointerWidget::update() {
    // Always rebuild - pointer position changes every frame
    // Pointer is lightweight (4 quads) so rebuilding every frame is fine
    setDataDirty();

    // Handle dirty flags using base class helper
    processDirtyFlags();
}

void PointerWidget::setScale(float scale) {
    // Clamp pointer scale to reasonable range (0.5x to 3.0x)
    if (scale < 0.5f) scale = 0.5f;
    if (scale > 3.0f) scale = 3.0f;

    if (m_fScale != scale) {
        m_fScale = scale;
        setDataDirty();
    }
}

void PointerWidget::rebuildLayout() {
    // Pointer doesn't need layout rebuild - it's positioned by mouse
    rebuildRenderData();
}

void PointerWidget::rebuildRenderData() {
    m_quads.clear();

    // Don't render if widget is hidden
    if (!m_bVisible) {
        return;
    }

    const InputManager& input = InputManager::getInstance();

    // Only render pointer if cursor should be visible
    if (!input.shouldShowCursor()) {
        return;
    }

    const CursorPosition& pos = input.getCursorPosition();
    if (!pos.isValid) {
        return;
    }

    // Build pointer at mouse position
    if (m_bShowBackgroundTexture) {
        // Sprite-based rendering (TGA texture)
        createPointerSprite(pos.x, pos.y);
    } else {
        // Quad-based rendering (accent color shapes)
        createPointerQuads(pos.x, pos.y);
    }
}

void PointerWidget::createPointerSprite(float x, float y) {
    // Sprite-based pointer using TGA texture
    // Apply scale to base size
    const float scaledHeight = BASE_SIZE * m_fScale;
    const float scaledWidth = scaledHeight / UI_ASPECT_RATIO;

    // Create sprite quad at mouse position
    SPluginQuad_t sprite;
    sprite.m_aafPos[0][0] = x;                    // Top-left
    sprite.m_aafPos[0][1] = y;
    sprite.m_aafPos[1][0] = x;                    // Bottom-left
    sprite.m_aafPos[1][1] = y + scaledHeight;
    sprite.m_aafPos[2][0] = x + scaledWidth;      // Bottom-right
    sprite.m_aafPos[2][1] = y + scaledHeight;
    sprite.m_aafPos[3][0] = x + scaledWidth;      // Top-right
    sprite.m_aafPos[3][1] = y;
    sprite.m_iSprite = getBackgroundTextureIndex();

    // Apply background opacity (alpha channel)
    const unsigned char alpha = static_cast<unsigned char>(m_fBackgroundOpacity * 255.0f);
    sprite.m_ulColor = PluginUtils::makeColor(255, 255, 255, alpha);

    m_quads.push_back(sprite);
}

void PointerWidget::createPointerQuads(float x, float y) {
    // Pointer shape from SVG (64x64 viewBox):
    // - Triangle: M 0 1 L 2 51 L 43 27 Z
    // - Square: centered at (28,47), rotated -28 deg, 18x18 rect at (-9,-5)

    // Apply scale to base size
    const float scaledHeight = BASE_SIZE * m_fScale;
    const float scaledWidth = scaledHeight / UI_ASPECT_RATIO;

    // Scale factors: convert 64x64 SVG coords to normalized screen coords
    const float scaleX = scaledWidth / 64.0f;
    const float scaleY = scaledHeight / 64.0f;

    // Shadow offset to bottom-right (scales with pointer size)
    const float shadowOffsetX = 3.0f * scaleX;
    const float shadowOffsetY = 3.0f * scaleY;

    // Colors - accent color foreground at 100% opacity, semi-transparent black for shadow
    const unsigned long accentColor = ColorConfig::getInstance().getAccent();
    const unsigned long fgColor = (accentColor & 0x00FFFFFF) | 0xFF000000;  // Force 100% alpha
    const unsigned long shadowColor = PluginUtils::makeColor(0, 0, 0, 204);  // Black with 80% opacity

    // === Triangle vertices (from SVG path: M 0 1 L 2 51 L 43 27 Z) ===
    const float triTipX = x + 0.0f * scaleX;
    const float triTipY = y + 1.0f * scaleY;
    const float triBottomLeftX = x + 2.0f * scaleX;
    const float triBottomLeftY = y + 51.0f * scaleY;
    const float triRightX = x + 43.0f * scaleX;
    const float triRightY = y + 27.0f * scaleY;

    // === Square vertices (rotated -28 deg around center at 28,47) ===
    // Pre-calculated rotated corners of 18x18 rect at (-9,-5):
    // cos(-28 deg) = 0.8829, sin(-28 deg) = -0.4695
    const float sqTopLeftX = x + 17.71f * scaleX;
    const float sqTopLeftY = y + 46.81f * scaleY;
    const float sqTopRightX = x + 33.60f * scaleX;
    const float sqTopRightY = y + 38.36f * scaleY;
    const float sqBottomRightX = x + 42.05f * scaleX;
    const float sqBottomRightY = y + 54.25f * scaleY;
    const float sqBottomLeftX = x + 26.16f * scaleX;
    const float sqBottomLeftY = y + 62.70f * scaleY;

    // === Shadow quads (rendered first, behind - offset to bottom-right) ===

    // Shadow triangle
    SPluginQuad_t shadowTri;
    createTriangleQuad(shadowTri,
                       triTipX + shadowOffsetX, triTipY + shadowOffsetY,
                       triBottomLeftX + shadowOffsetX, triBottomLeftY + shadowOffsetY,
                       triRightX + shadowOffsetX, triRightY + shadowOffsetY,
                       shadowColor);
    m_quads.push_back(shadowTri);

    // Shadow square (rotated rect)
    SPluginQuad_t shadowSquare;
    shadowSquare.m_aafPos[0][0] = sqTopLeftX + shadowOffsetX;
    shadowSquare.m_aafPos[0][1] = sqTopLeftY + shadowOffsetY;
    shadowSquare.m_aafPos[1][0] = sqBottomLeftX + shadowOffsetX;
    shadowSquare.m_aafPos[1][1] = sqBottomLeftY + shadowOffsetY;
    shadowSquare.m_aafPos[2][0] = sqBottomRightX + shadowOffsetX;
    shadowSquare.m_aafPos[2][1] = sqBottomRightY + shadowOffsetY;
    shadowSquare.m_aafPos[3][0] = sqTopRightX + shadowOffsetX;
    shadowSquare.m_aafPos[3][1] = sqTopRightY + shadowOffsetY;
    shadowSquare.m_iSprite = SpriteIndex::SOLID_COLOR;
    shadowSquare.m_ulColor = shadowColor;
    m_quads.push_back(shadowSquare);

    // === Foreground quads (accent color) ===

    // Foreground triangle
    SPluginQuad_t fgTri;
    createTriangleQuad(fgTri,
                       triTipX, triTipY,
                       triBottomLeftX, triBottomLeftY,
                       triRightX, triRightY,
                       fgColor);
    m_quads.push_back(fgTri);

    // Foreground square (rotated rect)
    SPluginQuad_t fgSquare;
    fgSquare.m_aafPos[0][0] = sqTopLeftX;
    fgSquare.m_aafPos[0][1] = sqTopLeftY;
    fgSquare.m_aafPos[1][0] = sqBottomLeftX;
    fgSquare.m_aafPos[1][1] = sqBottomLeftY;
    fgSquare.m_aafPos[2][0] = sqBottomRightX;
    fgSquare.m_aafPos[2][1] = sqBottomRightY;
    fgSquare.m_aafPos[3][0] = sqTopRightX;
    fgSquare.m_aafPos[3][1] = sqTopRightY;
    fgSquare.m_iSprite = SpriteIndex::SOLID_COLOR;
    fgSquare.m_ulColor = fgColor;
    m_quads.push_back(fgSquare);
}

void PointerWidget::createTriangleQuad(SPluginQuad_t& quad,
                                      float x0, float y0,
                                      float x1, float y1,
                                      float x2, float y2,
                                      unsigned long color) {
    // Create a degenerate quad that forms a triangle
    // by placing vertex 3 at the same position as vertex 2
    // Vertices are counter-clockwise: 0 -> 1 -> 2 -> 3

    quad.m_aafPos[0][0] = x0;  // First vertex (tip)
    quad.m_aafPos[0][1] = y0;
    quad.m_aafPos[1][0] = x1;  // Second vertex (base left)
    quad.m_aafPos[1][1] = y1;
    quad.m_aafPos[2][0] = x2;  // Third vertex (base right)
    quad.m_aafPos[2][1] = y2;
    quad.m_aafPos[3][0] = x2;  // Fourth vertex = same as third (degenerate)
    quad.m_aafPos[3][1] = y2;

    quad.m_iSprite = SpriteIndex::SOLID_COLOR;
    quad.m_ulColor = color;
}

void PointerWidget::resetToDefaults() {
    m_bVisible = true;
    m_fScale = 1.0f;
    m_fBackgroundOpacity = 1.0f;  // 100% for sprite-based rendering
    setTextureVariant(0);  // Quad-based by default (variant 0 = Off)
    setDataDirty();
}
