// ============================================================================
// core/xinput_reader.cpp
// XInput controller reader for raw gamepad input access
// ============================================================================
#include "xinput_reader.h"
#include "../diagnostics/logger.h"
#include "plugin_constants.h"
#include <algorithm>

using namespace PluginConstants;

XInputReader& XInputReader::getInstance() {
    static XInputReader instance;
    return instance;
}

XInputReader::XInputReader() : m_controllerIndex(0) {
    DEBUG_INFO("XInputReader initialized");
}

void XInputReader::update() {
    XINPUT_STATE state;
    ZeroMemory(&state, sizeof(XINPUT_STATE));

    // Get the state of the controller
    DWORD result = XInputGetState(m_controllerIndex, &state);

    if (result == ERROR_SUCCESS) {
        // Controller is connected
        m_data.isConnected = true;

        // Process gamepad data
        const XINPUT_GAMEPAD& pad = state.Gamepad;

        // Left stick (raw input, no deadzone for visualization)
        m_data.leftStickX = normalizeStickValue(pad.sThumbLX, 0);
        m_data.leftStickY = normalizeStickValue(pad.sThumbLY, 0);

        // Right stick (raw input, no deadzone for visualization)
        m_data.rightStickX = normalizeStickValue(pad.sThumbRX, 0);
        m_data.rightStickY = normalizeStickValue(pad.sThumbRY, 0);

        // Triggers (normalize 0-255 to 0.0-1.0)
        m_data.leftTrigger = normalizeTriggerValue(pad.bLeftTrigger);
        m_data.rightTrigger = normalizeTriggerValue(pad.bRightTrigger);

        // D-Pad
        m_data.dpadUp = (pad.wButtons & XINPUT_GAMEPAD_DPAD_UP) != 0;
        m_data.dpadDown = (pad.wButtons & XINPUT_GAMEPAD_DPAD_DOWN) != 0;
        m_data.dpadLeft = (pad.wButtons & XINPUT_GAMEPAD_DPAD_LEFT) != 0;
        m_data.dpadRight = (pad.wButtons & XINPUT_GAMEPAD_DPAD_RIGHT) != 0;

        // Buttons
        m_data.buttonA = (pad.wButtons & XINPUT_GAMEPAD_A) != 0;
        m_data.buttonB = (pad.wButtons & XINPUT_GAMEPAD_B) != 0;
        m_data.buttonX = (pad.wButtons & XINPUT_GAMEPAD_X) != 0;
        m_data.buttonY = (pad.wButtons & XINPUT_GAMEPAD_Y) != 0;
        m_data.leftShoulder = (pad.wButtons & XINPUT_GAMEPAD_LEFT_SHOULDER) != 0;
        m_data.rightShoulder = (pad.wButtons & XINPUT_GAMEPAD_RIGHT_SHOULDER) != 0;
        m_data.leftThumb = (pad.wButtons & XINPUT_GAMEPAD_LEFT_THUMB) != 0;
        m_data.rightThumb = (pad.wButtons & XINPUT_GAMEPAD_RIGHT_THUMB) != 0;
        m_data.buttonStart = (pad.wButtons & XINPUT_GAMEPAD_START) != 0;
        m_data.buttonBack = (pad.wButtons & XINPUT_GAMEPAD_BACK) != 0;
    }
    else {
        // Controller is not connected
        m_data.isConnected = false;

        // Reset all values to default
        m_data = XInputData();
    }
}

void XInputReader::setControllerIndex(int index) {
    // XInput supports controllers 0-3
    m_controllerIndex = std::max(0, std::min(3, index));
}

float XInputReader::normalizeStickValue(SHORT value, SHORT deadzone) const {
    // Apply deadzone
    if (value > -deadzone && value < deadzone) {
        return 0.0f;
    }

    // Safety: Clamp deadzone to prevent division by zero
    // Max stick range is -32768 to 32767, so clamp deadzone to reasonable values
    if (deadzone >= 32767) {
        return 0.0f;  // Deadzone too large, no input possible
    }

    // Normalize to -1.0 to 1.0
    float normalized;
    if (value < 0) {
        // Negative range: -32768 to -deadzone -> -1.0 to 0.0
        normalized = static_cast<float>(value + deadzone) / (XInputLimits::STICK_NEGATIVE_MAX - deadzone);
    }
    else {
        // Positive range: deadzone to 32767 -> 0.0 to 1.0
        normalized = static_cast<float>(value - deadzone) / (XInputLimits::STICK_POSITIVE_MAX - deadzone);
    }

    // Clamp to valid range
    return std::max(-1.0f, std::min(1.0f, normalized));
}

float XInputReader::normalizeTriggerValue(BYTE value) const {
    // Apply threshold
    if (value < TRIGGER_THRESHOLD) {
        return 0.0f;
    }

    // Safety: Prevent division by zero (TRIGGER_THRESHOLD should be < TRIGGER_MAX, but be defensive)
    if (TRIGGER_THRESHOLD >= XInputLimits::TRIGGER_MAX) {
        return 0.0f;
    }

    // Normalize 0-255 to 0.0-1.0
    return static_cast<float>(value - TRIGGER_THRESHOLD) / (XInputLimits::TRIGGER_MAX - TRIGGER_THRESHOLD);
}
