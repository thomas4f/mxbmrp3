// ============================================================================
// core/hotkey_manager.cpp
// Manages customizable hotkey bindings for keyboard and controller
// ============================================================================
#include "hotkey_manager.h"
#include "xinput_reader.h"
#include "../diagnostics/logger.h"
#include <windows.h>

HotkeyManager& HotkeyManager::getInstance() {
    static HotkeyManager instance;
    return instance;
}

HotkeyManager::HotkeyManager()
    : m_captureType(CaptureType::NONE)
    , m_captureAction(HotkeyAction::TOGGLE_STANDINGS)
    , m_captureCompleted(false)
    , m_prevControllerButtons(0)
    , m_bInitialized(false)
{
    m_prevKeyStates.fill(false);
    m_triggeredActions.fill(false);
}

void HotkeyManager::initialize() {
    if (m_bInitialized) return;

    DEBUG_INFO("HotkeyManager initializing");

    resetToDefaults();

    // Initialize previous key states
    for (int i = 0; i < 256; i++) {
        m_prevKeyStates[i] = (GetAsyncKeyState(i) & 0x8000) != 0;
    }
    m_prevControllerButtons = 0;

    m_bInitialized = true;
    DEBUG_INFO("HotkeyManager initialized");
}

void HotkeyManager::shutdown() {
    if (!m_bInitialized) return;

    DEBUG_INFO("HotkeyManager shutting down");
    m_bInitialized = false;
}

void HotkeyManager::resetToDefaults() {
    // Clear all bindings first
    for (auto& binding : m_bindings) {
        binding.clearAll();
    }

    // Set default keyboard bindings - only Settings Menu has defaults
    // VK_OEM_3 is ` on US keyboards, § on some EU layouts
    m_bindings[static_cast<size_t>(HotkeyAction::TOGGLE_SETTINGS)]     = HotkeyBinding(VK_OEM_3);

    DEBUG_INFO("HotkeyManager: Reset to default bindings");
}

void HotkeyManager::update() {
    if (!m_bInitialized) return;

    // Clear triggered actions from last frame
    m_triggeredActions.fill(false);

    // Handle capture mode
    if (m_captureType != CaptureType::NONE) {
        updateCapture();
    } else {
        // Only check for triggered actions when not capturing
        checkTriggeredActions();
    }

    // Update previous key states for next frame
    for (int i = 0; i < 256; i++) {
        m_prevKeyStates[i] = (GetAsyncKeyState(i) & 0x8000) != 0;
    }

    // Update previous controller button state
    const XInputData& xinput = XInputReader::getInstance().getData();
    if (xinput.isConnected) {
        m_prevControllerButtons = 0;
        if (xinput.dpadUp) m_prevControllerButtons |= XINPUT_GAMEPAD_DPAD_UP;
        if (xinput.dpadDown) m_prevControllerButtons |= XINPUT_GAMEPAD_DPAD_DOWN;
        if (xinput.dpadLeft) m_prevControllerButtons |= XINPUT_GAMEPAD_DPAD_LEFT;
        if (xinput.dpadRight) m_prevControllerButtons |= XINPUT_GAMEPAD_DPAD_RIGHT;
        if (xinput.buttonStart) m_prevControllerButtons |= XINPUT_GAMEPAD_START;
        if (xinput.buttonBack) m_prevControllerButtons |= XINPUT_GAMEPAD_BACK;
        if (xinput.leftThumb) m_prevControllerButtons |= XINPUT_GAMEPAD_LEFT_THUMB;
        if (xinput.rightThumb) m_prevControllerButtons |= XINPUT_GAMEPAD_RIGHT_THUMB;
        if (xinput.leftShoulder) m_prevControllerButtons |= XINPUT_GAMEPAD_LEFT_SHOULDER;
        if (xinput.rightShoulder) m_prevControllerButtons |= XINPUT_GAMEPAD_RIGHT_SHOULDER;
        if (xinput.buttonA) m_prevControllerButtons |= XINPUT_GAMEPAD_A;
        if (xinput.buttonB) m_prevControllerButtons |= XINPUT_GAMEPAD_B;
        if (xinput.buttonX) m_prevControllerButtons |= XINPUT_GAMEPAD_X;
        if (xinput.buttonY) m_prevControllerButtons |= XINPUT_GAMEPAD_Y;
    } else {
        m_prevControllerButtons = 0;
    }
}

const HotkeyBinding& HotkeyManager::getBinding(HotkeyAction action) const {
    return m_bindings[static_cast<size_t>(action)];
}

void HotkeyManager::setBinding(HotkeyAction action, const HotkeyBinding& binding) {
    m_bindings[static_cast<size_t>(action)] = binding;
}

void HotkeyManager::setKeyboardBinding(HotkeyAction action, const KeyBinding& binding) {
    m_bindings[static_cast<size_t>(action)].keyboard = binding;
}

void HotkeyManager::setControllerBinding(HotkeyAction action, ControllerButton button) {
    m_bindings[static_cast<size_t>(action)].controller = button;
}

void HotkeyManager::clearBinding(HotkeyAction action) {
    m_bindings[static_cast<size_t>(action)].clearAll();
}

void HotkeyManager::clearKeyboardBinding(HotkeyAction action) {
    m_bindings[static_cast<size_t>(action)].clearKeyboard();
}

void HotkeyManager::clearControllerBinding(HotkeyAction action) {
    m_bindings[static_cast<size_t>(action)].clearController();
}

void HotkeyManager::startCapture(HotkeyAction action, CaptureType type) {
    m_captureAction = action;
    m_captureType = type;
    m_captureCompleted = false;
    DEBUG_INFO_F("HotkeyManager: Started %s capture for action %d",
                 type == CaptureType::KEYBOARD ? "keyboard" : "controller",
                 static_cast<int>(action));
}

void HotkeyManager::cancelCapture() {
    m_captureType = CaptureType::NONE;
    m_captureCompleted = false;
    DEBUG_INFO("HotkeyManager: Capture cancelled");
}

bool HotkeyManager::wasCaptureCompleted() {
    if (m_captureCompleted) {
        m_captureCompleted = false;
        return true;
    }
    return false;
}

bool HotkeyManager::wasActionTriggered(HotkeyAction action) const {
    return m_triggeredActions[static_cast<size_t>(action)];
}

bool HotkeyManager::hasKeyboardConflict(HotkeyAction action, const KeyBinding& binding) const {
    if (!binding.isSet()) return false;

    for (size_t i = 0; i < static_cast<size_t>(HotkeyAction::COUNT); i++) {
        if (i == static_cast<size_t>(action)) continue;
        if (m_bindings[i].keyboard == binding) {
            return true;
        }
    }
    return false;
}

bool HotkeyManager::hasControllerConflict(HotkeyAction action, ControllerButton button) const {
    if (button == ControllerButton::NONE) return false;

    for (size_t i = 0; i < static_cast<size_t>(HotkeyAction::COUNT); i++) {
        if (i == static_cast<size_t>(action)) continue;
        if (m_bindings[i].controller == button) {
            return true;
        }
    }
    return false;
}

ModifierFlags HotkeyManager::getCurrentModifiers() const {
    ModifierFlags mods = ModifierFlags::NONE;

    if ((GetAsyncKeyState(VK_CONTROL) & 0x8000) != 0) {
        mods = mods | ModifierFlags::CTRL;
    }
    if ((GetAsyncKeyState(VK_SHIFT) & 0x8000) != 0) {
        mods = mods | ModifierFlags::SHIFT;
    }
    if ((GetAsyncKeyState(VK_MENU) & 0x8000) != 0) {
        mods = mods | ModifierFlags::ALT;
    }

    return mods;
}

void HotkeyManager::updateCapture() {
    if (m_captureType == CaptureType::KEYBOARD) {
        // Look for any key press (excluding modifiers and blacklisted keys)
        for (int vk = 1; vk < 256; vk++) {
            if (isKeyBlacklisted(static_cast<uint8_t>(vk))) continue;

            if (isKeyClicked(static_cast<uint8_t>(vk))) {
                // Capture this key with current modifiers
                KeyBinding newBinding;
                newBinding.keyCode = static_cast<uint8_t>(vk);
                newBinding.modifiers = getCurrentModifiers();

                setKeyboardBinding(m_captureAction, newBinding);

                char bindStr[32];
                formatKeyBinding(newBinding, bindStr, sizeof(bindStr));
                DEBUG_INFO_F("HotkeyManager: Captured keyboard binding: %s for action %d",
                             bindStr, static_cast<int>(m_captureAction));

                m_captureType = CaptureType::NONE;
                m_captureCompleted = true;
                return;
            }
        }
    }
    else if (m_captureType == CaptureType::CONTROLLER) {
        // Look for any controller button press
        const ControllerButton buttons[] = {
            ControllerButton::DPAD_UP, ControllerButton::DPAD_DOWN,
            ControllerButton::DPAD_LEFT, ControllerButton::DPAD_RIGHT,
            ControllerButton::START, ControllerButton::BACK,
            ControllerButton::LEFT_THUMB, ControllerButton::RIGHT_THUMB,
            ControllerButton::LEFT_SHOULDER, ControllerButton::RIGHT_SHOULDER,
            ControllerButton::BUTTON_A, ControllerButton::BUTTON_B,
            ControllerButton::BUTTON_X, ControllerButton::BUTTON_Y
        };

        for (ControllerButton btn : buttons) {
            if (isControllerButtonClicked(btn)) {
                setControllerBinding(m_captureAction, btn);

                DEBUG_INFO_F("HotkeyManager: Captured controller binding: %s for action %d",
                             getControllerButtonName(btn), static_cast<int>(m_captureAction));

                m_captureType = CaptureType::NONE;
                m_captureCompleted = true;
                return;
            }
        }
    }
}

void HotkeyManager::checkTriggeredActions() {
    ModifierFlags currentMods = getCurrentModifiers();

    for (size_t i = 0; i < static_cast<size_t>(HotkeyAction::COUNT); i++) {
        const HotkeyBinding& binding = m_bindings[i];

        // Check keyboard binding
        if (binding.hasKeyboard()) {
            const KeyBinding& kb = binding.keyboard;
            // Check if modifiers match AND key was clicked
            if (kb.modifiers == currentMods && isKeyClicked(kb.keyCode)) {
                m_triggeredActions[i] = true;
                continue;
            }
        }

        // Check controller binding
        if (binding.hasController()) {
            if (isControllerButtonClicked(binding.controller)) {
                m_triggeredActions[i] = true;
            }
        }
    }
}

bool HotkeyManager::isKeyPressed(uint8_t vkCode) const {
    return (GetAsyncKeyState(vkCode) & 0x8000) != 0;
}

bool HotkeyManager::isKeyClicked(uint8_t vkCode) const {
    bool isPressed = (GetAsyncKeyState(vkCode) & 0x8000) != 0;
    bool wasPressed = m_prevKeyStates[vkCode];
    return isPressed && !wasPressed;
}

bool HotkeyManager::isControllerButtonClicked(ControllerButton button) const {
    const XInputData& xinput = XInputReader::getInstance().getData();
    if (!xinput.isConnected) return false;

    uint16_t buttonMask = static_cast<uint16_t>(button);
    if (buttonMask == 0) return false;

    // Build current button state
    uint16_t currentButtons = 0;
    if (xinput.dpadUp) currentButtons |= XINPUT_GAMEPAD_DPAD_UP;
    if (xinput.dpadDown) currentButtons |= XINPUT_GAMEPAD_DPAD_DOWN;
    if (xinput.dpadLeft) currentButtons |= XINPUT_GAMEPAD_DPAD_LEFT;
    if (xinput.dpadRight) currentButtons |= XINPUT_GAMEPAD_DPAD_RIGHT;
    if (xinput.buttonStart) currentButtons |= XINPUT_GAMEPAD_START;
    if (xinput.buttonBack) currentButtons |= XINPUT_GAMEPAD_BACK;
    if (xinput.leftThumb) currentButtons |= XINPUT_GAMEPAD_LEFT_THUMB;
    if (xinput.rightThumb) currentButtons |= XINPUT_GAMEPAD_RIGHT_THUMB;
    if (xinput.leftShoulder) currentButtons |= XINPUT_GAMEPAD_LEFT_SHOULDER;
    if (xinput.rightShoulder) currentButtons |= XINPUT_GAMEPAD_RIGHT_SHOULDER;
    if (xinput.buttonA) currentButtons |= XINPUT_GAMEPAD_A;
    if (xinput.buttonB) currentButtons |= XINPUT_GAMEPAD_B;
    if (xinput.buttonX) currentButtons |= XINPUT_GAMEPAD_X;
    if (xinput.buttonY) currentButtons |= XINPUT_GAMEPAD_Y;

    bool isPressed = (currentButtons & buttonMask) != 0;
    bool wasPressed = (m_prevControllerButtons & buttonMask) != 0;
    return isPressed && !wasPressed;
}
