// ============================================================================
// core/xinput_reader.cpp
// XInput controller reader for raw gamepad input access
// ============================================================================
#include "xinput_reader.h"
#include "../diagnostics/logger.h"
#include "plugin_constants.h"
#include <algorithm>
#include <chrono>

// ============================================================================
// DEBUG: Uncomment ONE of these to log raw input values for that effect
// Logs min/max values every 2 seconds to help tune minInput/maxInput ranges
// ============================================================================
// #define DEBUG_RUMBLE_INPUT_SUSPENSION
// #define DEBUG_RUMBLE_INPUT_WHEELSPIN
// #define DEBUG_RUMBLE_INPUT_LOCKUP
// #define DEBUG_RUMBLE_INPUT_WHEELIE
// #define DEBUG_RUMBLE_INPUT_RPM
// #define DEBUG_RUMBLE_INPUT_SLIDE
// #define DEBUG_RUMBLE_INPUT_SURFACE
// #define DEBUG_RUMBLE_INPUT_STEER

// C++/WinRT for Windows.Gaming.Input (Windows 10+)
#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.Foundation.Collections.h>
#include <winrt/Windows.Gaming.Input.h>

#pragma comment(lib, "windowsapp.lib")

using namespace PluginConstants;

XInputReader& XInputReader::getInstance() {
    static XInputReader instance;
    return instance;
}

XInputReader::XInputReader()
    : m_controllerIndex(0)
    , m_lastConnectedState{false, false, false, false}
    , m_connectionStateChanged(false)
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
    // Initialize connection state
    for (int i = 0; i < 4; i++) {
        m_lastConnectedState[i] = isControllerConnected(i);
    }
    DEBUG_INFO("XInputReader initialized");
}

void XInputReader::update() {
    // When disabled (-1), don't poll XInput at all
    if (m_controllerIndex < 0) {
        m_data = XInputData();  // Reset to default (disconnected) state
    } else {
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

    // Check if any controller connection state changed
    for (int i = 0; i < 4; i++) {
        bool connected = isControllerConnected(i);
        if (connected != m_lastConnectedState[i]) {
            m_connectionStateChanged = true;
            m_lastConnectedState[i] = connected;
            DEBUG_INFO_F("XInputReader: Controller %d %s", i + 1, connected ? "connected" : "disconnected");
        }
    }
}

void XInputReader::setControllerIndex(int index) {
    int oldIndex = m_controllerIndex;
    // XInput supports controllers 0-3, or -1 for disabled
    m_controllerIndex = std::max(-1, std::min(3, index));

    // When disabling or switching controllers, stop vibration on old controller
    if (oldIndex >= 0 && oldIndex != m_controllerIndex) {
        XINPUT_VIBRATION vibration = {};
        XInputSetState(oldIndex, &vibration);
    }

    // When switching to disabled, stop vibration on ALL controllers
    // This clears any lingering vibration state from any source
    if (m_controllerIndex < 0) {
        XINPUT_VIBRATION vibration = {};
        for (int i = 0; i < 4; i++) {
            XInputSetState(i, &vibration);
        }
        m_lastLeftMotor = 0.0f;
        m_lastRightMotor = 0.0f;
    }
}

bool XInputReader::didConnectionStateChange() {
    bool changed = m_connectionStateChanged;
    m_connectionStateChanged = false;
    return changed;
}

bool XInputReader::isControllerConnected(int index) {
    if (index < 0 || index > 3) return false;
    XINPUT_STATE state;
    ZeroMemory(&state, sizeof(XINPUT_STATE));
    return XInputGetState(index, &state) == ERROR_SUCCESS;
}

std::string XInputReader::getControllerName(int index) {
    if (index < 0 || index > 3) return "";
    if (!isControllerConnected(index)) return "";

    // Cache controller names to avoid querying WinRT every frame
    // Cache is invalidated when controller connection state changes
    static std::string s_cachedNames[4] = {"", "", "", ""};
    static bool s_cachedConnected[4] = {false, false, false, false};
    static bool s_cacheInitialized = false;

    // Check if we need to refresh the cache
    bool needsRefresh = !s_cacheInitialized;
    for (int i = 0; i < 4 && !needsRefresh; i++) {
        bool connected = isControllerConnected(i);
        if (connected != s_cachedConnected[i]) {
            needsRefresh = true;
        }
    }

    if (needsRefresh) {
        // Clear cache
        for (int i = 0; i < 4; i++) {
            s_cachedNames[i] = "";
            s_cachedConnected[i] = isControllerConnected(i);
        }
        s_cacheInitialized = true;

        try {
            // Use RawGameController to get hardware names (Windows 10+)
            // Note: Xbox One controllers report as "Xbox 360 Controller for Windows"
            // due to the XInput compatibility driver - this is a Windows limitation
            auto controllers = winrt::Windows::Gaming::Input::RawGameController::RawGameControllers();

            // Find controllers that are also XInput gamepads
            int gamepadIndex = 0;
            for (uint32_t i = 0; i < controllers.Size() && gamepadIndex < 4; i++) {
                auto rawController = controllers.GetAt(i);

                // Check if this raw controller is also a Gamepad (XInput compatible)
                auto gamepad = winrt::Windows::Gaming::Input::Gamepad::FromGameController(rawController);
                if (gamepad != nullptr) {
                    // Convert wide string name to narrow
                    std::wstring wideName = rawController.DisplayName().c_str();
                    if (!wideName.empty()) {
                        int size = WideCharToMultiByte(CP_UTF8, 0, wideName.c_str(), -1, nullptr, 0, nullptr, nullptr);
                        if (size > 0) {
                            s_cachedNames[gamepadIndex].resize(size - 1);
                            WideCharToMultiByte(CP_UTF8, 0, wideName.c_str(), -1, &s_cachedNames[gamepadIndex][0], size, nullptr, nullptr);
                        }
                    }
                    gamepadIndex++;
                }
            }

            // Log discovered controllers
            for (int i = 0; i < 4; i++) {
                if (s_cachedConnected[i]) {
                    if (!s_cachedNames[i].empty()) {
                        DEBUG_INFO_F("XInputReader: Slot %d: %s", i + 1, s_cachedNames[i].c_str());
                    } else {
                        DEBUG_INFO_F("XInputReader: Slot %d: (unknown name)", i + 1);
                    }
                }
            }
        } catch (...) {
            // WinRT not available or error - cache remains empty
        }
    }

    return s_cachedNames[index];
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
    // Don't send vibration when controller is disabled
    if (m_controllerIndex < 0) {
        m_lastLeftMotor = 0.0f;
        m_lastRightMotor = 0.0f;
        return;
    }

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
    // If controller is disabled, don't process rumble at all
    if (m_controllerIndex < 0) {
        return;
    }

    // Always calculate forces for graph visualization, even when rumble is disabled
    // m_last*Rumble stores the max motor contribution for visualization

    // Get input values (used for both normalized calculation and motor-specific)
    float suspInput = std::abs(suspensionVelocity);
    float spinInput = std::max(0.0f, wheelOverrun);
    float lockInput = std::max(0.0f, wheelUnderrun);
    float wheelieInput = wheelieIntensity;
    float rpmInput = rpm;
    float slideInput = slideAngle;
    float surfaceInput = surfaceSpeed;
    float steerInput = std::abs(steerTorque);

    // Debug logging for raw input values (uncomment ONE #define at top of file)
#if defined(DEBUG_RUMBLE_INPUT_SUSPENSION) || defined(DEBUG_RUMBLE_INPUT_WHEELSPIN) || \
    defined(DEBUG_RUMBLE_INPUT_LOCKUP) || defined(DEBUG_RUMBLE_INPUT_WHEELIE) || \
    defined(DEBUG_RUMBLE_INPUT_RPM) || defined(DEBUG_RUMBLE_INPUT_SLIDE) || \
    defined(DEBUG_RUMBLE_INPUT_SURFACE) || defined(DEBUG_RUMBLE_INPUT_STEER)
    {
        static float s_minVal = 999999.0f;
        static float s_maxVal = -999999.0f;
        static auto s_lastLog = std::chrono::steady_clock::now();

        // Select which input to track based on the #define
#if defined(DEBUG_RUMBLE_INPUT_SUSPENSION)
        float trackVal = suspInput;
        const char* trackName = "SUSPENSION (m/s)";
#elif defined(DEBUG_RUMBLE_INPUT_WHEELSPIN)
        float trackVal = spinInput;
        const char* trackName = "WHEELSPIN (ratio)";
#elif defined(DEBUG_RUMBLE_INPUT_LOCKUP)
        float trackVal = lockInput;
        const char* trackName = "LOCKUP (ratio)";
#elif defined(DEBUG_RUMBLE_INPUT_WHEELIE)
        float trackVal = wheelieInput;
        const char* trackName = "WHEELIE (degrees)";
#elif defined(DEBUG_RUMBLE_INPUT_RPM)
        float trackVal = rpmInput;
        const char* trackName = "RPM";
#elif defined(DEBUG_RUMBLE_INPUT_SLIDE)
        float trackVal = slideInput;
        const char* trackName = "SLIDE (degrees)";
#elif defined(DEBUG_RUMBLE_INPUT_SURFACE)
        float trackVal = surfaceInput;
        const char* trackName = "SURFACE (m/s)";
#elif defined(DEBUG_RUMBLE_INPUT_STEER)
        float trackVal = steerInput;
        const char* trackName = "STEER (Nm)";
#endif

        // Update min/max (only track non-zero values for min)
        if (trackVal > 0.0f && trackVal < s_minVal) s_minVal = trackVal;
        if (trackVal > s_maxVal) s_maxVal = trackVal;

        // Log every 2 seconds
        auto now = std::chrono::steady_clock::now();
        if (std::chrono::duration_cast<std::chrono::milliseconds>(now - s_lastLog).count() >= 2000) {
            DEBUG_INFO_F("[RUMBLE DEBUG] %s: current=%.3f, min=%.3f, max=%.3f",
                trackName, trackVal, s_minVal == 999999.0f ? 0.0f : s_minVal, s_maxVal);
            s_lastLog = now;
        }
    }
#endif

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
        float rpmNorm = m_rumbleConfig.rpmEffect.calculateNormalized(rpmInput) * 0.5f;
        m_lastRpmRumble = rpmNorm * std::max(m_rumbleConfig.rpmEffect.lightStrength,
                                              m_rumbleConfig.rpmEffect.heavyStrength);
    } else {
        // Store max motor contribution for visualization (max of light/heavy)
        auto maxContrib = [](const RumbleEffect& eff, float input) {
            float norm = eff.calculateNormalized(input);
            return norm * std::max(eff.lightStrength, eff.heavyStrength);
        };
        m_lastSuspensionRumble = maxContrib(m_rumbleConfig.suspensionEffect, suspInput);
        m_lastWheelspinRumble = maxContrib(m_rumbleConfig.wheelspinEffect, spinInput);
        m_lastLockupRumble = maxContrib(m_rumbleConfig.brakeLockupEffect, lockInput);
        m_lastWheelieRumble = maxContrib(m_rumbleConfig.wheelieEffect, wheelieInput);
        m_lastRpmRumble = maxContrib(m_rumbleConfig.rpmEffect, rpmInput);
        m_lastSlideRumble = maxContrib(m_rumbleConfig.slideEffect, slideInput);
        m_lastSurfaceRumble = maxContrib(m_rumbleConfig.surfaceEffect, surfaceInput);
        m_lastSteerRumble = maxContrib(m_rumbleConfig.steerEffect, steerInput);
    }

    // Combine effects - each effect contributes independently to each motor
    float heavyMotor = 0.0f;
    float lightMotor = 0.0f;

    // Helper lambda for blending based on mode
    const bool additive = m_rumbleConfig.additiveBlend;
    auto blend = [additive](float& motor, float value) {
        if (value <= 0.0f) return;
        if (additive) {
            motor += value;  // Additive (clamped later)
        } else {
            motor = std::max(motor, value);  // Max wins
        }
    };

    // Calculate actual motor contributions (0 when airborne for ground effects)
    if (!isAirborne) {
        // Suspension effect
        blend(heavyMotor, m_rumbleConfig.suspensionEffect.calculateHeavy(suspInput));
        blend(lightMotor, m_rumbleConfig.suspensionEffect.calculateLight(suspInput));

        // Wheelspin effect
        blend(heavyMotor, m_rumbleConfig.wheelspinEffect.calculateHeavy(spinInput));
        blend(lightMotor, m_rumbleConfig.wheelspinEffect.calculateLight(spinInput));

        // Brake lockup effect
        blend(heavyMotor, m_rumbleConfig.brakeLockupEffect.calculateHeavy(lockInput));
        blend(lightMotor, m_rumbleConfig.brakeLockupEffect.calculateLight(lockInput));

        // Wheelie effect
        blend(heavyMotor, m_rumbleConfig.wheelieEffect.calculateHeavy(wheelieInput));
        blend(lightMotor, m_rumbleConfig.wheelieEffect.calculateLight(wheelieInput));

        // Slide effect
        blend(heavyMotor, m_rumbleConfig.slideEffect.calculateHeavy(slideInput));
        blend(lightMotor, m_rumbleConfig.slideEffect.calculateLight(slideInput));

        // Surface effect
        blend(heavyMotor, m_rumbleConfig.surfaceEffect.calculateHeavy(surfaceInput));
        blend(lightMotor, m_rumbleConfig.surfaceEffect.calculateLight(surfaceInput));

        // Steer effect
        blend(heavyMotor, m_rumbleConfig.steerEffect.calculateHeavy(steerInput));
        blend(lightMotor, m_rumbleConfig.steerEffect.calculateLight(steerInput));

        // RPM effect (full strength on ground)
        blend(heavyMotor, m_rumbleConfig.rpmEffect.calculateHeavy(rpmInput));
        blend(lightMotor, m_rumbleConfig.rpmEffect.calculateLight(rpmInput));
    } else {
        // RPM still active mid-air but reduced (engine under less load)
        float rpmNorm = m_rumbleConfig.rpmEffect.calculateNormalized(rpmInput) * 0.5f;
        blend(heavyMotor, rpmNorm * m_rumbleConfig.rpmEffect.heavyStrength);
        blend(lightMotor, rpmNorm * m_rumbleConfig.rpmEffect.lightStrength);
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
