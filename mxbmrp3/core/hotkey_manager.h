// ============================================================================
// core/hotkey_manager.h
// Manages customizable hotkey bindings for keyboard and controller
// ============================================================================
#pragma once

#include "hotkey_config.h"
#include "text_edit.h"
#include <array>
#include <chrono>
#include <functional>
#include <string>

// Capture mode types
enum class CaptureType {
    NONE,
    KEYBOARD,
    CONTROLLER,
    // Free-text entry for a settings field (the Twitch channel name). Rides the
    // capture machinery so everything that already keeps a capture safe applies
    // unchanged: hotkeys are suppressed while it runs (processKeyboardInput), ESC
    // cancels it (SettingsHud::update), and closing the menu disarms it.
    TEXT
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
    // "Press F1 to open settings": how the settings menu opens, as bound. The
    // shipped ` has no glyph in the game font, so it is named by place; with
    // no keyboard binding, the menu button.
    void formatOpenSettingsHint(char* out, size_t cap) const;
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

    // Text capture (CaptureType::TEXT). Keys typed while it runs edit the buffer
    // at its cursor (core/text_edit.h): letters, digits, '_' (Shift+minus),
    // Backspace/Delete (Ctrl: everything before/after the cursor), Left/Right
    // (Ctrl: to either end), Home/End, Ctrl+V (paste replaces all, raw -- the
    // caller normalizes); Enter commits. The committed text is handed over once
    // through consumeTextCommit(); wasCaptureCompleted() fires on the same frame
    // like any other capture.
    // `handlePunctuation` also types a bare '-' and '.', which YouTube handles
    // can contain and Twitch names cannot (see updateTextCapture).
    void startTextCapture(const std::string& initial, size_t maxLen, bool handlePunctuation);
    const std::string& getCaptureText() const { return m_captureEdit.text; }
    size_t getCaptureCursor() const { return m_captureEdit.cursor; }
    bool consumeTextCommit(std::string& out);
    // Commit whatever is in the buffer now (clicking the field again).
    void commitTextCapture();

    // Check if a specific action was triggered this frame
    bool wasActionTriggered(HotkeyAction action) const;

#if defined(MXBMRP3_TEST_BUILD)
    // Fire `action` on the next update(), as if its binding was pressed: the
    // headless tests have no keyboard to press it with.
    void testInject(HotkeyAction action) { m_injected[static_cast<size_t>(action)] = true; }
#endif

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
    void updateTextCapture();
    void pasteClipboardText();
    void applyTextEditKey(uint8_t vk, bool ctrl);
    void checkTriggeredActions();
    bool isKeyPressed(uint8_t vkCode) const;
    bool isKeyClicked(uint8_t vkCode) const;
    bool isControllerButtonClicked(ControllerButton button) const;
    bool hasAnyControllerBinding() const;  // gates the Draw-path controller poll

    // Bindings for all actions
    std::array<HotkeyBinding, static_cast<size_t>(HotkeyAction::COUNT)> m_bindings;

    // Previous frame key states for click detection
    std::array<bool, 256> m_prevKeyStates;
    uint16_t m_prevControllerButtons;

    // Actions triggered this frame
    std::array<bool, static_cast<size_t>(HotkeyAction::COUNT)> m_triggeredActions;
#if defined(MXBMRP3_TEST_BUILD)
    std::array<bool, static_cast<size_t>(HotkeyAction::COUNT)> m_injected{};
#endif

    // Capture state
    CaptureType m_captureType;
    HotkeyAction m_captureAction;
    bool m_captureCompleted;
    TextEdit::Buffer m_captureEdit;  // TEXT capture buffer + cursor
    bool m_captureHandlePunctuation = false;
    bool m_textCommitted = false;    // a TEXT capture ended with Enter/commit, not consumed yet
    // Hold-to-repeat for the editing keys (Backspace, Delete, Left, Right): the
    // one last pressed inside the field, 0 when none is held.
    uint8_t m_repeatKey = 0;
    std::chrono::steady_clock::time_point m_repeatHeldSince{};
    std::chrono::steady_clock::time_point m_repeatLast{};
    int m_repeats = 0;

    bool m_bInitialized;
};
