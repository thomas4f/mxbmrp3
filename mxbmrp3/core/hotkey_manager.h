// ============================================================================
// core/hotkey_manager.h
// Manages customizable hotkey bindings for keyboard and controller
// ============================================================================
#pragma once

#include "hotkey_config.h"
#include <array>
#include <functional>

// Capture mode types
enum class CaptureType {
    NONE,
    KEYBOARD,
    CONTROLLER
};

// Callback type for when a hotkey action is triggered
using HotkeyCallback = std::function<void(HotkeyAction)>;

class HotkeyManager {
public:
    static HotkeyManager& getInstance();

    void initialize();
    void shutdown();

    // Update input state and check for triggered hotkeys
    // Call once per frame after InputManager and XInputReader update
    void update();

    // Get/set bindings
    const HotkeyBinding& getBinding(HotkeyAction action) const;
    void setBinding(HotkeyAction action, const HotkeyBinding& binding);
    void setKeyboardBinding(HotkeyAction action, const KeyBinding& binding);
    void setControllerBinding(HotkeyAction action, ControllerButton button);
    void clearBinding(HotkeyAction action);
    void clearKeyboardBinding(HotkeyAction action);
    void clearControllerBinding(HotkeyAction action);

    // Reset to default bindings
    void resetToDefaults();

    // Capture mode - for settings UI to capture new bindings
    void startCapture(HotkeyAction action, CaptureType type);
    void cancelCapture();
    bool isCapturing() const { return m_captureType != CaptureType::NONE; }
    CaptureType getCaptureType() const { return m_captureType; }
    HotkeyAction getCaptureAction() const { return m_captureAction; }

    // Check if capture completed this frame (does NOT clear the flag)
    bool didCaptureCompleteThisFrame() const { return m_captureCompleted; }

    // Check if capture completed this frame (returns true once, clears flag)
    bool wasCaptureCompleted();

    // Check if a specific action was triggered this frame
    bool wasActionTriggered(HotkeyAction action) const;

    // Check for binding conflicts
    bool hasKeyboardConflict(HotkeyAction action, const KeyBinding& binding) const;
    bool hasControllerConflict(HotkeyAction action, ControllerButton button) const;

    // Get current modifier state
    ModifierFlags getCurrentModifiers() const;

private:
    HotkeyManager();
    ~HotkeyManager() = default;
    HotkeyManager(const HotkeyManager&) = delete;
    HotkeyManager& operator=(const HotkeyManager&) = delete;

    // Internal helpers
    void updateCapture();
    void checkTriggeredActions();
    bool isKeyPressed(uint8_t vkCode) const;
    bool isKeyClicked(uint8_t vkCode) const;
    bool isControllerButtonClicked(ControllerButton button) const;

    // Bindings for all actions
    std::array<HotkeyBinding, static_cast<size_t>(HotkeyAction::COUNT)> m_bindings;

    // Previous frame key states for click detection
    std::array<bool, 256> m_prevKeyStates;
    uint16_t m_prevControllerButtons;

    // Actions triggered this frame
    std::array<bool, static_cast<size_t>(HotkeyAction::COUNT)> m_triggeredActions;

    // Capture state
    CaptureType m_captureType;
    HotkeyAction m_captureAction;
    bool m_captureCompleted;

    bool m_bInitialized;
};
