// ============================================================================
// hud/cursor.h
// Manages mouse cursor display and interaction state
// ============================================================================
#pragma once

#include <vector>
#include "../vendor/piboso/mxb_api.h"
#include "../core/plugin_constants.h"
#include "../core/plugin_utils.h"

class CursorRenderer {
public:
    // Static method to add cursor quad directly to the provided quad vector
    // Returns true if a quad was added, false if cursor is disabled or invalid
    static bool addCursorQuad(std::vector<SPluginQuad_t>& quads);

private:
    // Create a quad entry for sprite rendering
    static void createCursorQuad(SPluginQuad_t& quad, float x, float y);

    // Display constants
    static constexpr float BASE_SIZE = 0.04f;  // ~43 pixels at 1920x1080
    static constexpr float SPRITE_WIDTH = BASE_SIZE / PluginConstants::UI_ASPECT_RATIO;
    static constexpr float SPRITE_HEIGHT = BASE_SIZE;
    static constexpr int POINTER_SPRITE_INDEX = 1;
    static constexpr unsigned long SPRITE_COLOR = PluginUtils::makeColor(255, 255, 255, 255);  // White
};
