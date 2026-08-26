// ============================================================================
// hud/gamepad_widget.h
// Displays controller button overlay - shows pressed buttons, sticks, triggers
// ============================================================================
#pragma once

#include "base_hud.h"
#include "gamepad_geometry.h"
#include "../core/plugin_constants.h"
#include "../core/widget_constants.h"
#include "../core/xinput_reader.h"   // XInputData (rebuild-gate snapshot)
#include "../core/asset_manager.h"   // GamepadAsset, GamepadSprite
#include <string>

class GamepadWidget : public BaseHud {
public:
    GamepadWidget();
    virtual ~GamepadWidget() = default;

    void update() override;
    bool handlesDataType(DataChangeType dataType) const override;
    void resetToDefaults();

    // THE SELECTED PAD, BY NAME. Not by index: an index into discovery order
    // reassigns every user's pad the moment a pack is added, removed or renamed,
    // which is the rule icon overrides and themes already follow.
    //
    // An unknown name degrades to the shipped default rather than to "no pad" --
    // a user who removes a pack should see a controller, not an empty panel, and
    // their setting is left untouched so restoring the folder restores the choice.
    const std::string& getGamepadPack() const { return m_gamepadPack; }
    void setGamepadPack(const std::string& name);

    // The pack actually in use once the name has been resolved; nullptr only when
    // no packs are installed at all.
    const GamepadAsset* activePack() const;

    // Trigger display: 0 = fade (texture brightness), 1 = fill (quad from bottom).
    // A viewing preference, not pad art -- it stays a widget setting rather than
    // moving into the pack ini, which describes the controller and nothing else.
    int getTriggerFillMode() const { return m_triggerFillMode; }
    void setTriggerFillMode(int mode) { m_triggerFillMode = mode; }

    // Allow SettingsHud and SettingsManager to access private members
    friend class SettingsHud;
    friend class SettingsManager;

private:
    void rebuildRenderData() override;

    // Sprite index for one part of the ACTIVE pack, or 0 when no pack resolves (in
    // which case every draw helper falls back to its solid-colour shape). Every
    // helper goes through this rather than resolving the pack itself.
    int packSprite(GamepadSprite::Part part) const;

    // Last controller state that was actually rendered — the rebuild gate (see
    // update()). POD snapshot compared bytewise; both copies originate from the
    // same XInputReader object, so padding bytes compare consistently.
    XInputData m_lastRenderedInput{};
    bool m_hasRenderedInput = false;

    // Helper to add a stick with position indicator
    // isPressed = L3/R3 click state for coloring the stick sprite
    void addStick(float centerX, float centerY, float stickX, float stickY,
                  float width, float height, float backgroundWidth,
                  const GamepadLayout::PadGeometry& layout, bool isPressed);

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
                       bool isPressed);

    // Layout constants
    static constexpr float START_X = 0.0f;
    static constexpr float START_Y = 0.0f;
    static constexpr int BACKGROUND_WIDTH_CHARS = GamepadLayout::kFrameChars;

    // Stick area dimensions
    static constexpr float STICK_HEIGHT_LINES = 6.0f;  // Height in text lines
    static constexpr int STICK_SPACING_CHARS = 16;     // Spacing between sticks

    // Button colors
    static constexpr unsigned long COLOR_TRIGGER = PluginUtils::makeColor(180, 180, 180);   // Light gray
    static constexpr unsigned long COLOR_BUMPER = PluginUtils::makeColor(160, 160, 160);    // Gray
    static constexpr unsigned long COLOR_DPAD = PluginUtils::makeColor(200, 200, 200);      // Light gray
    static constexpr unsigned long COLOR_MENUBTN = PluginUtils::makeColor(140, 140, 140);   // Gray
    static constexpr unsigned long COLOR_INACTIVE = PluginUtils::makeColor(60, 60, 60);     // Dark gray

    // Selected pack, by directory name. Resolved through activePack() at every use
    // so a pack removed between sessions degrades rather than dangling.
    std::string m_gamepadPack;

    int m_triggerFillMode = 0;
};
