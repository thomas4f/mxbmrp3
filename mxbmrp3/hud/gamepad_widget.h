// ============================================================================
// hud/gamepad_widget.h
// Displays controller button overlay - shows pressed buttons, sticks, triggers
// ============================================================================
#pragma once

#include "base_hud.h"
#include "../core/plugin_constants.h"
#include "../core/widget_constants.h"
#include <map>

class GamepadWidget : public BaseHud {
public:
    GamepadWidget();
    virtual ~GamepadWidget() = default;

    void update() override;
    bool handlesDataType(DataChangeType dataType) const override;
    void resetToDefaults();

    // Per-variant layout configuration
    // Stored in .ini as [GamepadWidget_Layout_N] sections
    struct LayoutConfig {
        // Reference background dimensions (texture aspect ratio)
        // Defaults match variant 1 (Xbox); initDefaultLayouts() sets per-variant values
        float backgroundWidth = 750.0f;
        float backgroundHeight = 630.0f;

        // Texture dimensions (on backgroundWidth reference)
        float triggerWidth = 89.0f, triggerHeight = 61.0f;
        float bumperWidth = 171.0f, bumperHeight = 63.0f;
        float dpadWidth = 34.0f, dpadHeight = 56.0f;
        float faceButtonSize = 53.0f;  // Square buttons
        float menuButtonWidth = 33.0f, menuButtonHeight = 33.0f;  // Can be non-square
        float stickSize = 83.0f;

        // Position offsets (applied after base layout calculation)
        float leftTriggerX = 0.0f, leftTriggerY = 0.0f;
        float rightTriggerX = 0.0f, rightTriggerY = 0.0f;
        float leftBumperX = 0.0f, leftBumperY = 0.0f;
        float rightBumperX = 0.0f, rightBumperY = 0.0f;
        float leftStickX = 0.0f, leftStickY = 0.0f;
        float rightStickX = 0.0f, rightStickY = 0.0f;
        float dpadX = 0.0f, dpadY = 0.0f;
        float faceButtonsX = 0.0f, faceButtonsY = 0.0f;
        float menuButtonsX = 0.0f, menuButtonsY = 0.0f;

        // Spacing multipliers (1.0 = neutral default)
        float dpadSpacing = 1.0f;
        float faceButtonSpacing = 1.0f;
        float menuButtonSpacing = 1.0f;

        // Trigger display mode: 0=fade (texture brightness), 1=fill (quad from bottom)
        int triggerFillMode = 0;
    };

    // Get layout for a specific variant (creates default if not exists)
    LayoutConfig& getLayout(int variant);
    const LayoutConfig& getCurrentLayout() const;

    // Get layout for a specific variant if it exists (const-safe, returns nullptr if not found)
    const LayoutConfig* getLayoutIfExists(int variant) const {
        auto it = m_layouts.find(variant);
        return (it != m_layouts.end()) ? &it->second : nullptr;
    }

    // Check if layout exists (for save optimization)
    bool hasLayout(int variant) const { return m_layouts.find(variant) != m_layouts.end(); }

    // Allow SettingsHud and SettingsManager to access private members
    friend class SettingsHud;
    friend class SettingsManager;

private:
    void rebuildRenderData() override;

    // Helper to add a stick with position indicator
    // isPressed = L3/R3 click state for coloring the stick sprite
    void addStick(float centerX, float centerY, float stickX, float stickY,
                  float width, float height, float backgroundWidth,
                  const LayoutConfig& layout, bool isPressed);

    // Helper to add a face button with sprite texture (A/B/X/Y)
    void addFaceButton(float centerX, float centerY, float size, bool isPressed, const char* label);

    // Helper to add a D-pad button with sprite texture
    // Direction: 0=up, 1=right, 2=down, 3=left
    void addDpadButton(float centerX, float centerY, float width, float height,
                       bool isPressed, int direction);

    // Helper to add a trigger button with sprite texture
    void addTriggerButton(float centerX, float centerY, float width, float height,
                          float value, bool isLeft);

    // Helper to add a bumper button with sprite texture
    void addBumperButton(float centerX, float centerY, float width, float height,
                         bool isPressed, bool isLeft);

    // Helper to add a menu button with sprite texture (Back/Start)
    void addMenuButton(float centerX, float centerY, float width, float height,
                       bool isPressed, const char* label);

    // Layout constants
    static constexpr float START_X = 0.0f;
    static constexpr float START_Y = 0.0f;
    static constexpr int BACKGROUND_WIDTH_CHARS = 43;

    // Stick area dimensions
    static constexpr float STICK_HEIGHT_LINES = 6.0f;  // Height in text lines
    static constexpr int STICK_SPACING_CHARS = 16;     // Spacing between sticks

    // Button colors
    static constexpr unsigned long COLOR_TRIGGER = PluginUtils::makeColor(180, 180, 180);   // Light gray
    static constexpr unsigned long COLOR_BUMPER = PluginUtils::makeColor(160, 160, 160);    // Gray
    static constexpr unsigned long COLOR_DPAD = PluginUtils::makeColor(200, 200, 200);      // Light gray
    static constexpr unsigned long COLOR_MENUBTN = PluginUtils::makeColor(140, 140, 140);   // Gray
    static constexpr unsigned long COLOR_INACTIVE = PluginUtils::makeColor(60, 60, 60);     // Dark gray

    // Per-variant layouts (indexed by texture variant number)
    // Variant 0 means "no texture", variants 1+ correspond to gamepad_widget_N.tga
    std::map<int, LayoutConfig> m_layouts;

    // Initialize default layouts for known variants
    void initDefaultLayouts();
};
