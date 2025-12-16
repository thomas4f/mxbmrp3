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

XInputReader::XInputReader()
    : m_controllerIndex(0)
    , m_lastLeftMotor(0.0f)
    , m_lastRightMotor(0.0f)
    , m_lastSuspensionRumble(0.0f)
    , m_lastWheelspinRumble(0.0f)
    , m_lastLockupRumble(0.0f)
    , m_lastRpmRumble(0.0f)
    , m_lastSlideRumble(0.0f)
    , m_lastSurfaceRumble(0.0f)
    , m_lastSteerRumble(0.0f)
    , m_lastWheelieRumble(0.0f)
{
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

bool XInputReader::isControllerConnected(int index) {
    if (index < 0 || index > 3) return false;
    XINPUT_STATE state;
    ZeroMemory(&state, sizeof(XINPUT_STATE));
    return XInputGetState(index, &state) == ERROR_SUCCESS;
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

void XInputReader::setVibration(float leftMotor, float rightMotor) {
    // Clamp values to valid range
    leftMotor = std::max(0.0f, std::min(1.0f, leftMotor));
    rightMotor = std::max(0.0f, std::min(1.0f, rightMotor));

    // Track values for visualization
    m_lastLeftMotor = leftMotor;
    m_lastRightMotor = rightMotor;

    XINPUT_VIBRATION vibration = {};
    vibration.wLeftMotorSpeed = static_cast<WORD>(leftMotor * 65535.0f);
    vibration.wRightMotorSpeed = static_cast<WORD>(rightMotor * 65535.0f);

    XInputSetState(m_controllerIndex, &vibration);
}

void XInputReader::stopVibration() {
    setVibration(0.0f, 0.0f);
}

void XInputReader::updateRumbleFromTelemetry(float suspensionVelocity, float wheelOverrun, float wheelUnderrun, float rpm, float slideAngle, float surfaceSpeed, float steerTorque, float wheelieIntensity, bool isAirborne, bool suppressOutput) {
    // Always calculate forces for graph visualization, even when rumble is disabled
    // Calculate each effect's intensity
    // Ground-based effects are suppressed when airborne
    if (isAirborne) {
        m_lastSuspensionRumble = 0.0f;
        m_lastWheelspinRumble = 0.0f;
        m_lastLockupRumble = 0.0f;
        m_lastSlideRumble = 0.0f;
        m_lastSurfaceRumble = 0.0f;
        m_lastSteerRumble = 0.0f;
        m_lastWheelieRumble = 0.0f;
        // RPM still active mid-air but reduced (engine under less load)
        m_lastRpmRumble = m_rumbleConfig.rpmEffect.calculate(rpm) * 0.5f;
    } else {
        m_lastSuspensionRumble = m_rumbleConfig.suspensionEffect.calculate(std::abs(suspensionVelocity));
        m_lastWheelspinRumble = m_rumbleConfig.wheelspinEffect.calculate(std::max(0.0f, wheelOverrun));
        m_lastLockupRumble = m_rumbleConfig.brakeLockupEffect.calculate(std::max(0.0f, wheelUnderrun));
        m_lastWheelieRumble = m_rumbleConfig.wheelieEffect.calculate(wheelieIntensity);
        m_lastRpmRumble = m_rumbleConfig.rpmEffect.calculate(rpm);
        m_lastSlideRumble = m_rumbleConfig.slideEffect.calculate(slideAngle);
        m_lastSurfaceRumble = m_rumbleConfig.surfaceEffect.calculate(surfaceSpeed);
        m_lastSteerRumble = m_rumbleConfig.steerEffect.calculate(std::abs(steerTorque));
    }

    // Combine effects based on motor targeting
    // Each effect can target Heavy, Light, Both, or Off
    float heavyMotor = 0.0f;
    float lightMotor = 0.0f;

    // Helper lambda for blending based on mode
    const bool additive = m_rumbleConfig.additiveBlend;
    auto blend = [additive](float& motor, float value) {
        if (additive) {
            motor += value;  // Additive (clamped later)
        } else {
            motor = std::max(motor, value);  // Max wins
        }
    };

    // Suspension effect
    if (m_rumbleConfig.suspensionEffect.targetsHeavy()) {
        blend(heavyMotor, m_lastSuspensionRumble);
    }
    if (m_rumbleConfig.suspensionEffect.targetsLight()) {
        blend(lightMotor, m_lastSuspensionRumble);
    }

    // Wheelspin effect
    if (m_rumbleConfig.wheelspinEffect.targetsHeavy()) {
        blend(heavyMotor, m_lastWheelspinRumble);
    }
    if (m_rumbleConfig.wheelspinEffect.targetsLight()) {
        blend(lightMotor, m_lastWheelspinRumble);
    }

    // Brake lockup effect
    if (m_rumbleConfig.brakeLockupEffect.targetsHeavy()) {
        blend(heavyMotor, m_lastLockupRumble);
    }
    if (m_rumbleConfig.brakeLockupEffect.targetsLight()) {
        blend(lightMotor, m_lastLockupRumble);
    }

    // Wheelie effect
    if (m_rumbleConfig.wheelieEffect.targetsHeavy()) {
        blend(heavyMotor, m_lastWheelieRumble);
    }
    if (m_rumbleConfig.wheelieEffect.targetsLight()) {
        blend(lightMotor, m_lastWheelieRumble);
    }

    // RPM effect
    if (m_rumbleConfig.rpmEffect.targetsHeavy()) {
        blend(heavyMotor, m_lastRpmRumble);
    }
    if (m_rumbleConfig.rpmEffect.targetsLight()) {
        blend(lightMotor, m_lastRpmRumble);
    }

    // Slide effect
    if (m_rumbleConfig.slideEffect.targetsHeavy()) {
        blend(heavyMotor, m_lastSlideRumble);
    }
    if (m_rumbleConfig.slideEffect.targetsLight()) {
        blend(lightMotor, m_lastSlideRumble);
    }

    // Surface effect
    if (m_rumbleConfig.surfaceEffect.targetsHeavy()) {
        blend(heavyMotor, m_lastSurfaceRumble);
    }
    if (m_rumbleConfig.surfaceEffect.targetsLight()) {
        blend(lightMotor, m_lastSurfaceRumble);
    }

    // Steer effect
    if (m_rumbleConfig.steerEffect.targetsHeavy()) {
        blend(heavyMotor, m_lastSteerRumble);
    }
    if (m_rumbleConfig.steerEffect.targetsLight()) {
        blend(lightMotor, m_lastSteerRumble);
    }

    // Clamp to valid range (important for additive mode)
    heavyMotor = std::min(1.0f, heavyMotor);
    lightMotor = std::min(1.0f, lightMotor);

    // Record history for graph visualization
    pushToHistory(m_heavyMotorHistory, heavyMotor);
    pushToHistory(m_lightMotorHistory, lightMotor);
    pushToHistory(m_suspensionHistory, m_lastSuspensionRumble);
    pushToHistory(m_wheelspinHistory, m_lastWheelspinRumble);
    pushToHistory(m_lockupHistory, m_lastLockupRumble);
    pushToHistory(m_wheelieHistory, m_lastWheelieRumble);
    pushToHistory(m_rpmHistory, m_lastRpmRumble);
    pushToHistory(m_slideHistory, m_lastSlideRumble);
    pushToHistory(m_surfaceHistory, m_lastSurfaceRumble);
    pushToHistory(m_steerHistory, m_lastSteerRumble);

    // Send to controller (unless suppressed or disabled)
    // Graph still updates even when output is suppressed
    if (suppressOutput || !m_rumbleConfig.enabled) {
        setVibration(0.0f, 0.0f);
    } else {
        setVibration(heavyMotor, lightMotor);
    }
}
