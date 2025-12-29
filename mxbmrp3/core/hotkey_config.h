// ============================================================================
// core/hotkey_config.h
// Hotkey configuration types and constants for customizable keyboard/controller bindings
// ============================================================================
#pragma once

#include <windows.h>
#include <Xinput.h>
#include <cstdint>
#include <string>

// ============================================================================
// Hotkey Actions - All bindable actions in the plugin
// ============================================================================
enum class HotkeyAction : uint8_t {
    TOGGLE_STANDINGS = 0,
    TOGGLE_MAP,
    TOGGLE_RADAR,
    TOGGLE_LAP_LOG,
    TOGGLE_IDEAL_LAP,
    TOGGLE_TELEMETRY,
    TOGGLE_INPUT,
    TOGGLE_RECORDS,
    TOGGLE_WIDGETS,          // Toggle all widgets
    TOGGLE_PITBOARD,
    TOGGLE_TIMING,
    TOGGLE_GAP_BAR,
    TOGGLE_PERFORMANCE,
    TOGGLE_RUMBLE,
    TOGGLE_ALL_HUDS,         // Hide/show all HUDs
    TOGGLE_SETTINGS,         // Default: ` (tilde)
    RELOAD_CONFIG,           // Reload settings from .ini file

    COUNT  // Must be last
};

// Get display name for an action
inline const char* getActionDisplayName(HotkeyAction action) {
    switch (action) {
        case HotkeyAction::TOGGLE_STANDINGS:    return "Standings";
        case HotkeyAction::TOGGLE_MAP:          return "Map";
        case HotkeyAction::TOGGLE_RADAR:        return "Radar";
        case HotkeyAction::TOGGLE_LAP_LOG:      return "Lap Log";
        case HotkeyAction::TOGGLE_IDEAL_LAP:    return "Ideal Lap";
        case HotkeyAction::TOGGLE_TELEMETRY:    return "Telemetry";
        case HotkeyAction::TOGGLE_INPUT:        return "Input";
        case HotkeyAction::TOGGLE_RECORDS:      return "Records";
        case HotkeyAction::TOGGLE_WIDGETS:      return "All Widgets";
        case HotkeyAction::TOGGLE_PITBOARD:     return "Pitboard";
        case HotkeyAction::TOGGLE_TIMING:       return "Timing";
        case HotkeyAction::TOGGLE_GAP_BAR:      return "Gap Bar";
        case HotkeyAction::TOGGLE_PERFORMANCE:  return "Performance";
        case HotkeyAction::TOGGLE_RUMBLE:       return "Rumble";
        case HotkeyAction::TOGGLE_ALL_HUDS:     return "All Elements";
        case HotkeyAction::TOGGLE_SETTINGS:     return "Settings Menu";
        case HotkeyAction::RELOAD_CONFIG:       return "Reload Config";
        default: return "Unknown";
    }
}

// ============================================================================
// Modifier Keys - Can be combined with main keys
// ============================================================================
enum class ModifierFlags : uint8_t {
    NONE  = 0,
    CTRL  = 1 << 0,
    SHIFT = 1 << 1,
    ALT   = 1 << 2
};

inline ModifierFlags operator|(ModifierFlags a, ModifierFlags b) {
    return static_cast<ModifierFlags>(static_cast<uint8_t>(a) | static_cast<uint8_t>(b));
}

inline ModifierFlags operator&(ModifierFlags a, ModifierFlags b) {
    return static_cast<ModifierFlags>(static_cast<uint8_t>(a) & static_cast<uint8_t>(b));
}

inline bool hasModifier(ModifierFlags flags, ModifierFlags mod) {
    return (static_cast<uint8_t>(flags) & static_cast<uint8_t>(mod)) != 0;
}

// ============================================================================
// Controller Buttons - XInput button identifiers for binding
// ============================================================================
enum class ControllerButton : uint16_t {
    NONE = 0,
    DPAD_UP      = XINPUT_GAMEPAD_DPAD_UP,
    DPAD_DOWN    = XINPUT_GAMEPAD_DPAD_DOWN,
    DPAD_LEFT    = XINPUT_GAMEPAD_DPAD_LEFT,
    DPAD_RIGHT   = XINPUT_GAMEPAD_DPAD_RIGHT,
    START        = XINPUT_GAMEPAD_START,
    BACK         = XINPUT_GAMEPAD_BACK,
    LEFT_THUMB   = XINPUT_GAMEPAD_LEFT_THUMB,
    RIGHT_THUMB  = XINPUT_GAMEPAD_RIGHT_THUMB,
    LEFT_SHOULDER  = XINPUT_GAMEPAD_LEFT_SHOULDER,
    RIGHT_SHOULDER = XINPUT_GAMEPAD_RIGHT_SHOULDER,
    BUTTON_A     = XINPUT_GAMEPAD_A,
    BUTTON_B     = XINPUT_GAMEPAD_B,
    BUTTON_X     = XINPUT_GAMEPAD_X,
    BUTTON_Y     = XINPUT_GAMEPAD_Y
};

// Get display name for controller button
inline const char* getControllerButtonName(ControllerButton button) {
    switch (button) {
        case ControllerButton::NONE:           return "None";
        case ControllerButton::DPAD_UP:        return "D-Pad Up";
        case ControllerButton::DPAD_DOWN:      return "D-Pad Down";
        case ControllerButton::DPAD_LEFT:      return "D-Pad Left";
        case ControllerButton::DPAD_RIGHT:     return "D-Pad Right";
        case ControllerButton::START:          return "Start";
        case ControllerButton::BACK:           return "Back";
        case ControllerButton::LEFT_THUMB:     return "L3";
        case ControllerButton::RIGHT_THUMB:    return "R3";
        case ControllerButton::LEFT_SHOULDER:  return "LB";
        case ControllerButton::RIGHT_SHOULDER: return "RB";
        case ControllerButton::BUTTON_A:       return "A";
        case ControllerButton::BUTTON_B:       return "B";
        case ControllerButton::BUTTON_X:       return "X";
        case ControllerButton::BUTTON_Y:       return "Y";
        default: return "Unknown";
    }
}

// ============================================================================
// Keyboard Key Binding
// ============================================================================
struct KeyBinding {
    uint8_t keyCode;          // Windows VK code (0 = unbound)
    ModifierFlags modifiers;  // Ctrl/Shift/Alt flags

    KeyBinding() : keyCode(0), modifiers(ModifierFlags::NONE) {}
    KeyBinding(uint8_t vk, ModifierFlags mods = ModifierFlags::NONE)
        : keyCode(vk), modifiers(mods) {}

    bool isSet() const { return keyCode != 0; }
    void clear() { keyCode = 0; modifiers = ModifierFlags::NONE; }

    bool operator==(const KeyBinding& other) const {
        return keyCode == other.keyCode && modifiers == other.modifiers;
    }
};

// ============================================================================
// Complete Hotkey Binding - Keyboard + Controller
// ============================================================================
struct HotkeyBinding {
    KeyBinding keyboard;
    ControllerButton controller;

    HotkeyBinding() : controller(ControllerButton::NONE) {}
    HotkeyBinding(uint8_t vk, ModifierFlags mods = ModifierFlags::NONE)
        : keyboard(vk, mods), controller(ControllerButton::NONE) {}
    HotkeyBinding(ControllerButton btn)
        : controller(btn) {}
    HotkeyBinding(uint8_t vk, ModifierFlags mods, ControllerButton btn)
        : keyboard(vk, mods), controller(btn) {}

    bool hasKeyboard() const { return keyboard.isSet(); }
    bool hasController() const { return controller != ControllerButton::NONE; }
    bool isSet() const { return hasKeyboard() || hasController(); }

    void clearKeyboard() { keyboard.clear(); }
    void clearController() { controller = ControllerButton::NONE; }
    void clearAll() { clearKeyboard(); clearController(); }
};

// ============================================================================
// Key Name Utilities
// ============================================================================

// Get display name for a Windows virtual key code
inline const char* getKeyName(uint8_t vkCode) {
    switch (vkCode) {
        case 0:             return "None";
        // Function keys
        case VK_F1:         return "F1";
        case VK_F2:         return "F2";
        case VK_F3:         return "F3";
        case VK_F4:         return "F4";
        case VK_F5:         return "F5";
        case VK_F6:         return "F6";
        case VK_F7:         return "F7";
        case VK_F8:         return "F8";
        case VK_F9:         return "F9";
        case VK_F10:        return "F10";
        case VK_F11:        return "F11";
        case VK_F12:        return "F12";
        // Letters A-Z (0x41-0x5A)
        case 'A':           return "A";
        case 'B':           return "B";
        case 'C':           return "C";
        case 'D':           return "D";
        case 'E':           return "E";
        case 'F':           return "F";
        case 'G':           return "G";
        case 'H':           return "H";
        case 'I':           return "I";
        case 'J':           return "J";
        case 'K':           return "K";
        case 'L':           return "L";
        case 'M':           return "M";
        case 'N':           return "N";
        case 'O':           return "O";
        case 'P':           return "P";
        case 'Q':           return "Q";
        case 'R':           return "R";
        case 'S':           return "S";
        case 'T':           return "T";
        case 'U':           return "U";
        case 'V':           return "V";
        case 'W':           return "W";
        case 'X':           return "X";
        case 'Y':           return "Y";
        case 'Z':           return "Z";
        // Numbers 0-9 (0x30-0x39)
        case '0':           return "0";
        case '1':           return "1";
        case '2':           return "2";
        case '3':           return "3";
        case '4':           return "4";
        case '5':           return "5";
        case '6':           return "6";
        case '7':           return "7";
        case '8':           return "8";
        case '9':           return "9";
        // Numpad
        case VK_NUMPAD0:    return "Num 0";
        case VK_NUMPAD1:    return "Num 1";
        case VK_NUMPAD2:    return "Num 2";
        case VK_NUMPAD3:    return "Num 3";
        case VK_NUMPAD4:    return "Num 4";
        case VK_NUMPAD5:    return "Num 5";
        case VK_NUMPAD6:    return "Num 6";
        case VK_NUMPAD7:    return "Num 7";
        case VK_NUMPAD8:    return "Num 8";
        case VK_NUMPAD9:    return "Num 9";
        case VK_MULTIPLY:   return "Num *";
        case VK_ADD:        return "Num +";
        case VK_SUBTRACT:   return "Num -";
        case VK_DECIMAL:    return "Num .";
        case VK_DIVIDE:     return "Num /";
        // Special keys
        case VK_TAB:        return "Tab";
        case VK_SPACE:      return "Space";
        case VK_BACK:       return "Backspace";
        case VK_DELETE:     return "Delete";
        case VK_INSERT:     return "Insert";
        case VK_HOME:       return "Home";
        case VK_END:        return "End";
        case VK_PRIOR:      return "Page Up";
        case VK_NEXT:       return "Page Down";
        case VK_UP:         return "Up";
        case VK_DOWN:       return "Down";
        case VK_LEFT:       return "Left";
        case VK_RIGHT:      return "Right";
        // OEM keys (layout-dependent)
        case VK_OEM_1:      return ";";
        case VK_OEM_2:      return "/";
        case VK_OEM_3:      return "`";
        case VK_OEM_4:      return "[";
        case VK_OEM_5:      return "\\";
        case VK_OEM_6:      return "]";
        case VK_OEM_7:      return "'";
        case VK_OEM_PLUS:   return "=";
        case VK_OEM_COMMA:  return ",";
        case VK_OEM_MINUS:  return "-";
        case VK_OEM_PERIOD: return ".";
        // Misc
        case VK_PAUSE:      return "Pause";
        case VK_SCROLL:     return "Scroll Lock";
        case VK_NUMLOCK:    return "Num Lock";
        case VK_CAPITAL:    return "Caps Lock";
        default:            return "?";
    }
}

// Check if a key is blacklisted (should not be bindable)
inline bool isKeyBlacklisted(uint8_t vkCode) {
    switch (vkCode) {
        // System keys
        case VK_ESCAPE:     // System menu
        case VK_RETURN:     // Chat/confirm
        case VK_LBUTTON:    // Mouse buttons reserved for settings interaction
        case VK_RBUTTON:
        case VK_MBUTTON:
        case VK_XBUTTON1:
        case VK_XBUTTON2:
        // Modifier keys alone
        case VK_SHIFT:
        case VK_CONTROL:
        case VK_MENU:       // Alt
        case VK_LSHIFT:
        case VK_RSHIFT:
        case VK_LCONTROL:
        case VK_RCONTROL:
        case VK_LMENU:
        case VK_RMENU:
        // Windows keys
        case VK_LWIN:
        case VK_RWIN:
        case VK_APPS:       // Context menu key
        // Lock keys (Caps Lock allowed - convenient for gaming)
        case VK_NUMLOCK:
        case VK_SCROLL:
        // Print Screen (system screenshot)
        case VK_SNAPSHOT:
            return true;
        default:
            return false;
    }
}

// Format a complete key binding as a display string
// Output buffer should be at least 32 characters
inline void formatKeyBinding(const KeyBinding& binding, char* buffer, size_t bufferSize) {
    if (!binding.isSet()) {
        snprintf(buffer, bufferSize, "None");
        return;
    }

    std::string result;

    // Add modifiers
    if (hasModifier(binding.modifiers, ModifierFlags::CTRL)) {
        result += "Ctrl+";
    }
    if (hasModifier(binding.modifiers, ModifierFlags::SHIFT)) {
        result += "Shift+";
    }
    if (hasModifier(binding.modifiers, ModifierFlags::ALT)) {
        result += "Alt+";
    }

    // Add key name
    result += getKeyName(binding.keyCode);

    snprintf(buffer, bufferSize, "%s", result.c_str());
}
