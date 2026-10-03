// ============================================================================
// core/hotkey_manager.cpp
// Manages customizable hotkey bindings for keyboard and controller
// ============================================================================
#include "hotkey_manager.h"
#include "input_manager.h"
#include "xinput_reader.h"
#include "ui_config.h"
#include "hold_repeat.h"
#include "../diagnostics/logger.h"
#include <windows.h>

namespace {
// Pack the pressed buttons of an XInputData into an XINPUT_GAMEPAD button mask.
// Returns 0 when disconnected. Shared by the prev-state tracking, the binding
// baseline, and the click test so the three stay in lockstep.
uint16_t controllerButtonMask(const XInputData& x) {
    if (!x.isConnected) return 0;
    uint16_t m = 0;
    if (x.dpadUp)        m |= XINPUT_GAMEPAD_DPAD_UP;
    if (x.dpadDown)      m |= XINPUT_GAMEPAD_DPAD_DOWN;
    if (x.dpadLeft)      m |= XINPUT_GAMEPAD_DPAD_LEFT;
    if (x.dpadRight)     m |= XINPUT_GAMEPAD_DPAD_RIGHT;
    if (x.buttonStart)   m |= XINPUT_GAMEPAD_START;
    if (x.buttonBack)    m |= XINPUT_GAMEPAD_BACK;
    if (x.leftThumb)     m |= XINPUT_GAMEPAD_LEFT_THUMB;
    if (x.rightThumb)    m |= XINPUT_GAMEPAD_RIGHT_THUMB;
    if (x.leftShoulder)  m |= XINPUT_GAMEPAD_LEFT_SHOULDER;
    if (x.rightShoulder) m |= XINPUT_GAMEPAD_RIGHT_SHOULDER;
    if (x.buttonA)       m |= XINPUT_GAMEPAD_A;
    if (x.buttonB)       m |= XINPUT_GAMEPAD_B;
    if (x.buttonX)       m |= XINPUT_GAMEPAD_X;
    if (x.buttonY)       m |= XINPUT_GAMEPAD_Y;
    return m;
}
}  // namespace

HotkeyManager& HotkeyManager::getInstance() {
    static HotkeyManager instance;
    return instance;
}

HotkeyManager::HotkeyManager()
    : m_prevControllerButtons(0)
    , m_captureType(CaptureType::NONE)
    , m_captureAction(HotkeyAction::TOGGLE_STANDINGS)
    , m_captureCompleted(false)
    , m_bInitialized(false)
{
    m_prevKeyStates.fill(false);
    m_triggeredActions.fill(false);
}

void HotkeyManager::initialize() {
    if (m_bInitialized) return;

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

    // Only detect hotkey actions when game window is focused
    if (InputManager::getInstance().isCursorEnabled()) {
        // The controller cache (XInputReader::m_data) is otherwise refreshed only
        // by RunTelemetry, which ticks while riding but NOT while spectating or in
        // menus. Hotkeys are processed here on the Draw path, which runs in all
        // those contexts, so refresh the controller here too whenever controller
        // input matters - a capture in progress, or any controller binding to
        // test. Without it, controller binding AND triggering only worked on
        // track. Cheap: update() no longer issues any XInput call - it just copies
        // the snapshot the XInput I/O thread already published (a short mutexed
        // struct copy) - and it's skipped entirely when no controller binding exists.
        if (m_captureType == CaptureType::CONTROLLER || hasAnyControllerBinding()) {
            XInputReader::getInstance().update();
        }

        if (m_captureType != CaptureType::NONE) {
            updateCapture();
        } else {
            checkTriggeredActions();
        }
    }

    // Always update previous input states (even when unfocused) to prevent
    // false edge triggers when focus returns.
    // Full 256-key refresh only while capturing a new binding (any key can
    // be captured); otherwise only the bound keys need edge tracking -
    // 256 GetAsyncKeyState syscalls per frame are measurable at 480fps.
    if (m_captureType != CaptureType::NONE) {
        for (int i = 0; i < 256; i++) {
            m_prevKeyStates[i] = (GetAsyncKeyState(i) & 0x8000) != 0;
        }
    } else {
        for (const auto& binding : m_bindings) {
            if (binding.hasKeyboard()) {
                uint8_t vk = binding.keyboard.keyCode;
                m_prevKeyStates[vk] = (GetAsyncKeyState(vk) & 0x8000) != 0;
            }
        }
    }

    // Update previous controller button state (for next frame's edge detection)
    m_prevControllerButtons = controllerButtonMask(XInputReader::getInstance().getData());
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

    // Refresh ALL key states: outside capture mode only bound keys are
    // tracked, so unbound entries are stale and a currently-held key would
    // otherwise register as a false immediate "click"
    for (int i = 0; i < 256; i++) {
        m_prevKeyStates[i] = (GetAsyncKeyState(i) & 0x8000) != 0;
    }

    // Same baseline for the controller: poll once now (menus don't tick
    // RunTelemetry) and seed the prev-button mask so a button already held when
    // capture starts isn't taken as an immediate press.
    if (type == CaptureType::CONTROLLER) {
        XInputReader::getInstance().update();
        m_prevControllerButtons = controllerButtonMask(XInputReader::getInstance().getData());
    }
    DEBUG_INFO_F("HotkeyManager: Started %s capture for action %d",
                 type == CaptureType::KEYBOARD ? "keyboard" : "controller",
                 static_cast<int>(action));
}

void HotkeyManager::startTextCapture(const std::string& initial, size_t maxLen, bool handlePunctuation) {
    m_captureType = CaptureType::TEXT;
    m_captureHandlePunctuation = handlePunctuation;
    m_captureCompleted = false;
    m_textCommitted = false;
    m_captureEdit.reset(initial, maxLen);
    m_repeatKey = 0;
    // Same baseline as startCapture(): the click that opened the field must not
    // register as a keypress, and keys already held are not typed.
    for (int i = 0; i < 256; i++) {
        m_prevKeyStates[i] = (GetAsyncKeyState(i) & 0x8000) != 0;
    }
    DEBUG_INFO("HotkeyManager: Started text capture");
}

void HotkeyManager::commitTextCapture() {
    if (m_captureType != CaptureType::TEXT) return;
    m_captureType = CaptureType::NONE;
    m_captureCompleted = true;
    m_textCommitted = true;
}

bool HotkeyManager::consumeTextCommit(std::string& out) {
    if (!m_textCommitted) return false;
    m_textCommitted = false;
    out = m_captureEdit.text;
    return true;
}

void HotkeyManager::cancelCapture() {
    // Callers cancel unconditionally - closing the settings panel does it
    // whether or not a binding was being captured - so there is nothing to say
    // unless a capture was actually running. Said every time, the line just
    // buried the ones that mean something.
    const bool wasCapturing = m_captureType != CaptureType::NONE;
    m_captureType = CaptureType::NONE;
    m_captureCompleted = false;
    m_textCommitted = false;
    if (wasCapturing) DEBUG_INFO("HotkeyManager: Capture cancelled");
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
    if (m_captureType == CaptureType::TEXT) {
        updateTextCapture();
        return;
    }
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

void HotkeyManager::updateTextCapture() {
    const bool ctrl = (GetAsyncKeyState(VK_CONTROL) & 0x8000) != 0;
    const bool shift = (GetAsyncKeyState(VK_SHIFT) & 0x8000) != 0;
    const bool alt = (GetAsyncKeyState(VK_MENU) & 0x8000) != 0;

    if (isKeyClicked(VK_RETURN)) {
        commitTextCapture();
        return;
    }
    // The editing keys repeat while held, on the settings arrows' hold curve
    // (core/hold_repeat.h), so clearing a pasted URL or walking the cursor
    // across a name is not one press per character. Only a press made INSIDE
    // the field repeats: a key already held when it opened never registers as a
    // click, so it never arms.
    static constexpr uint8_t REPEAT_KEYS[] = {VK_BACK, VK_DELETE, VK_LEFT, VK_RIGHT};
    for (uint8_t vk : REPEAT_KEYS) {
        if (isKeyClicked(vk)) {
            applyTextEditKey(vk, ctrl);
            m_repeatKey = vk;
            m_repeatHeldSince = m_repeatLast = std::chrono::steady_clock::now();
            m_repeats = 0;
        }
    }
    if (m_repeatKey != 0 && !isKeyPressed(m_repeatKey)) {
        m_repeatKey = 0;
    } else if (m_repeatKey != 0) {
        const auto now = std::chrono::steady_clock::now();
        const auto held = std::chrono::duration_cast<std::chrono::milliseconds>(now - m_repeatHeldSince).count();
        if (held >= HoldRepeat::INITIAL_DELAY_MS) {
            const long long interval = HoldRepeat::intervalMs(
                m_repeats, UiConfig::getInstance().getHoldRepeatFastMs());
            if (std::chrono::duration_cast<std::chrono::milliseconds>(now - m_repeatLast).count() >= interval) {
                applyTextEditKey(m_repeatKey, ctrl);
                m_repeatLast = now;
                ++m_repeats;
            }
        }
    }
    if (isKeyClicked(VK_HOME)) m_captureEdit.home();
    if (isKeyClicked(VK_END)) m_captureEdit.end();
    if (ctrl) {
        if (isKeyClicked('V')) pasteClipboardText();
        return;  // Ctrl+letter is a shortcut, never a character
    }
    if (alt) return;

    auto append = [&](char c) { m_captureEdit.insert(c); };
    // Virtual-key codes, not the layout's characters: letters and digits sit on
    // the same VK codes on every layout, which is all a channel name needs.
    // Anything else can be pasted.
    for (int vk = 'A'; vk <= 'Z'; ++vk) {
        if (isKeyClicked(static_cast<uint8_t>(vk))) {
            append(shift ? static_cast<char>(vk) : static_cast<char>(vk - 'A' + 'a'));
        }
    }
    for (int vk = '0'; vk <= '9'; ++vk) {
        if (!isKeyClicked(static_cast<uint8_t>(vk))) continue;
        // Shift+digit is a symbol wherever the key's own character is the
        // digit (US: Shift+2 is '@', which would otherwise type "2"). On layouts
        // whose unshifted row is symbols (AZERTY), Shift is how a digit is typed.
        if (shift && (MapVirtualKeyA(static_cast<UINT>(vk), MAPVK_VK_TO_CHAR) & 0x7FFF) == static_cast<UINT>(vk)) {
            continue;
        }
        append(static_cast<char>(vk));
    }
    for (int vk = VK_NUMPAD0; vk <= VK_NUMPAD9; ++vk) {
        if (isKeyClicked(static_cast<uint8_t>(vk))) append(static_cast<char>('0' + (vk - VK_NUMPAD0)));
    }
    // Shift+minus is '_' on most layouts. A bare '-' (and '.') is typed only
    // for a YouTube handle: a Twitch name cannot contain one, and its
    // normalizeChannel would silently cut "abc-def" to "abc" at it.
    if (shift && isKeyClicked(VK_OEM_MINUS)) append('_');
    if (m_captureHandlePunctuation && !shift) {
        if (isKeyClicked(VK_OEM_MINUS) || isKeyClicked(VK_SUBTRACT)) append('-');
        if (isKeyClicked(VK_OEM_PERIOD) || isKeyClicked(VK_DECIMAL)) append('.');
    }
}

void HotkeyManager::pasteClipboardText() {
    if (!OpenClipboard(nullptr)) return;
    HANDLE data = GetClipboardData(CF_TEXT);
    if (data) {
        const char* text = static_cast<const char*>(GlobalLock(data));
        if (text) {
            // Paste replaces the buffer, wherever the cursor is: what gets
            // pasted is a whole name or URL, and one spliced into the old name
            // would normalize to neither. Raw (the caller normalizes), capped
            // generously so a URL fits.
            std::string pasted;
            constexpr size_t PASTE_MAX = 256;
            for (size_t i = 0; text[i] != '\0' && i < PASTE_MAX; ++i) {
                const unsigned char c = static_cast<unsigned char>(text[i]);
                if (c >= 0x20 && c < 0x7F) pasted += static_cast<char>(c);
            }
            m_captureEdit.replaceAll(pasted);
            GlobalUnlock(data);
        }
    }
    CloseClipboard();
}

void HotkeyManager::applyTextEditKey(uint8_t vk, bool ctrl) {
    switch (vk) {
    case VK_BACK:   ctrl ? m_captureEdit.deleteToStart() : m_captureEdit.backspace(); break;
    case VK_DELETE: ctrl ? m_captureEdit.deleteToEnd() : m_captureEdit.deleteForward(); break;
    case VK_LEFT:   ctrl ? m_captureEdit.home() : m_captureEdit.left(); break;
    case VK_RIGHT:  ctrl ? m_captureEdit.end() : m_captureEdit.right(); break;
    default: break;
    }
}

bool HotkeyManager::isKeyClicked(uint8_t vkCode) const {
    bool isPressed = (GetAsyncKeyState(vkCode) & 0x8000) != 0;
    bool wasPressed = m_prevKeyStates[vkCode];
    return isPressed && !wasPressed;
}

bool HotkeyManager::hasAnyControllerBinding() const {
    for (const auto& b : m_bindings) {
        if (b.hasController()) return true;
    }
    return false;
}

bool HotkeyManager::isControllerButtonClicked(ControllerButton button) const {
    const XInputData& xinput = XInputReader::getInstance().getData();
    if (!xinput.isConnected) return false;

    uint16_t buttonMask = static_cast<uint16_t>(button);
    if (buttonMask == 0) return false;

    uint16_t currentButtons = controllerButtonMask(xinput);

    bool isPressed = (currentButtons & buttonMask) != 0;
    bool wasPressed = (m_prevControllerButtons & buttonMask) != 0;
    return isPressed && !wasPressed;
}
