// ============================================================================
// core/xinput_reader.h
// XInput controller reader for raw gamepad input access
// ============================================================================
#pragma once

#include <windows.h>
#include <Xinput.h>
#include <deque>

// Motor target selection for rumble effects
// Cycle order: Off -> Light -> Heavy -> Both (light before heavy since less intense)
enum class MotorTarget {
    Off = 0,    // Effect disabled
    Light = 1,  // Right motor only (high-frequency)
    Heavy = 2,  // Left motor only (low-frequency)
    Both = 3    // Both motors
};

// Rumble effect configuration for a single source
struct RumbleEffect {
    MotorTarget motor;  // Which motor(s) to use
    float minInput;     // Input value where rumble starts (e.g., 0.04 = 4% slip)
    float maxInput;     // Input value where rumble reaches max (e.g., 0.15 = 15% slip)
    float minStrength;  // Rumble strength at minInput (0.0 to 1.0)
    float maxStrength;  // Rumble strength at maxInput (0.0 to 1.0)

    RumbleEffect() : motor(MotorTarget::Heavy), minInput(0.0f), maxInput(1.0f),
                     minStrength(0.0f), maxStrength(1.0f) {}

    RumbleEffect(MotorTarget _motor, float _minIn, float _maxIn, float _minStr, float _maxStr)
        : motor(_motor), minInput(_minIn), maxInput(_maxIn),
          minStrength(_minStr), maxStrength(_maxStr) {}

    // Check if effect targets a specific motor
    bool targetsHeavy() const { return motor == MotorTarget::Heavy || motor == MotorTarget::Both; }
    bool targetsLight() const { return motor == MotorTarget::Light || motor == MotorTarget::Both; }
    bool isEnabled() const { return motor != MotorTarget::Off; }

    // Calculate rumble intensity from input value using linear interpolation
    float calculate(float inputValue) const {
        if (motor == MotorTarget::Off || inputValue < minInput) return 0.0f;
        if (inputValue >= maxInput) return maxStrength;
        // Protect against division by zero if minInput equals maxInput
        float range = maxInput - minInput;
        if (range <= 0.0f) return maxStrength;
        // Linear interpolation between min and max
        float t = (inputValue - minInput) / range;
        return minStrength + t * (maxStrength - minStrength);
    }
};

// Controller rumble configuration
struct RumbleConfig {
    bool enabled;           // Master enable/disable
    int controllerIndex;    // Which XInput controller (0-3)
    bool additiveBlend;     // true = add effects (clamped), false = max wins
    bool rumbleWhenCrashed; // false = stop all rumble when player is crashed (default)

    // Bumps: suspension compression on impacts/landings
    RumbleEffect suspensionEffect;

    // Spin: rear wheel overrun (traction feedback)
    RumbleEffect wheelspinEffect;

    // Brake Lockup: wheel underrun
    RumbleEffect brakeLockupEffect;

    // Wheelie: feedback when front wheel lifts off ground
    // Input is pitch angle in degrees (0 = level, 90 = vertical)
    RumbleEffect wheelieEffect;

    // Engine RPM: engine vibration feel
    RumbleEffect rpmEffect;

    // Lateral Slide: slip angle in degrees when bike is sliding sideways
    RumbleEffect slideEffect;

    // Surface: vibration when riding on rough surfaces (material ID > 1)
    // Input is speed (m/s) when on non-track surface (grass, dirt, gravel, etc.)
    RumbleEffect surfaceEffect;

    // Steer Torque: handlebar resistance feedback (ruts, rocks, off-camber)
    // Input is absolute steer torque in Nm
    RumbleEffect steerEffect;

    RumbleConfig() : enabled(false), controllerIndex(0), additiveBlend(false), rumbleWhenCrashed(false),
        // Bumps: Both motors, 50% sens, 50% strength
        suspensionEffect(MotorTarget::Both, 1.5f, 3.0f, 0.0f, 0.5f),
        // Spin: Light motor, 50% sens, 20% strength
        wheelspinEffect(MotorTarget::Light, 0.10f, 0.20f, 0.0f, 0.2f),
        // Lockup: Light motor, 50% sens, 20% strength
        brakeLockupEffect(MotorTarget::Light, 0.20f, 0.40f, 0.0f, 0.2f),
        // Wheelie: Off, 50% sens, 100% strength
        wheelieEffect(MotorTarget::Off, 45.0f, 90.0f, 0.0f, 1.0f),
        // RPM: Off, 50% sens, 10% strength
        rpmEffect(MotorTarget::Off, 6000.0f, 12000.0f, 0.0f, 0.1f),
        // Slide: Light motor, 50% sens, 20% strength
        slideEffect(MotorTarget::Light, 15.0f, 30.0f, 0.0f, 0.2f),
        // Surface: Off, 50% sens, 10% strength
        surfaceEffect(MotorTarget::Off, 15.0f, 30.0f, 0.0f, 0.1f),
        // Steer: Off, 50% sens, 10% strength
        steerEffect(MotorTarget::Off, 10.0f, 20.0f, 0.0f, 0.1f) {}

    void resetToDefaults() {
        enabled = false;
        controllerIndex = 0;
        additiveBlend = false;
        rumbleWhenCrashed = false;
        suspensionEffect = RumbleEffect(MotorTarget::Both, 1.5f, 3.0f, 0.0f, 0.5f);
        wheelspinEffect = RumbleEffect(MotorTarget::Light, 0.10f, 0.20f, 0.0f, 0.2f);
        brakeLockupEffect = RumbleEffect(MotorTarget::Light, 0.20f, 0.40f, 0.0f, 0.2f);
        wheelieEffect = RumbleEffect(MotorTarget::Off, 45.0f, 90.0f, 0.0f, 1.0f);
        rpmEffect = RumbleEffect(MotorTarget::Off, 6000.0f, 12000.0f, 0.0f, 0.1f);
        slideEffect = RumbleEffect(MotorTarget::Light, 15.0f, 30.0f, 0.0f, 0.2f);
        surfaceEffect = RumbleEffect(MotorTarget::Off, 15.0f, 30.0f, 0.0f, 0.1f);
        steerEffect = RumbleEffect(MotorTarget::Off, 10.0f, 20.0f, 0.0f, 0.1f);
    }
};

// XInput controller state data
struct XInputData {
    // Left stick (typically steering/throttle)
    float leftStickX;       // -1.0 to 1.0 (left to right)
    float leftStickY;       // -1.0 to 1.0 (down to up)

    // Right stick (rider body lean)
    float rightStickX;      // -1.0 to 1.0 (lean left to right)
    float rightStickY;      // -1.0 to 1.0 (lean back to forward)

    // Triggers
    float leftTrigger;      // 0.0 to 1.0 (rear brake typically)
    float rightTrigger;     // 0.0 to 1.0 (front brake typically)

    // D-Pad
    bool dpadUp;
    bool dpadDown;
    bool dpadLeft;
    bool dpadRight;

    // Buttons
    bool buttonA;
    bool buttonB;
    bool buttonX;
    bool buttonY;
    bool leftShoulder;
    bool rightShoulder;
    bool leftThumb;         // Left stick click
    bool rightThumb;        // Right stick click
    bool buttonStart;
    bool buttonBack;

    // Connection state
    bool isConnected;

    XInputData() : leftStickX(0.0f), leftStickY(0.0f),
                   rightStickX(0.0f), rightStickY(0.0f),
                   leftTrigger(0.0f), rightTrigger(0.0f),
                   dpadUp(false), dpadDown(false), dpadLeft(false), dpadRight(false),
                   buttonA(false), buttonB(false), buttonX(false), buttonY(false),
                   leftShoulder(false), rightShoulder(false),
                   leftThumb(false), rightThumb(false),
                   buttonStart(false), buttonBack(false),
                   isConnected(false) {}
};

class XInputReader {
public:
    static XInputReader& getInstance();

    // Update controller state (call once per frame)
    void update();

    // Get current controller data
    const XInputData& getData() const { return m_data; }

    // Set which controller to read (0-3)
    void setControllerIndex(int index);
    int getControllerIndex() const { return m_controllerIndex; }

    // Check if a specific controller index is connected (0-3)
    static bool isControllerConnected(int index);

    // Vibration control
    // leftMotor: low-frequency rumble (0.0 to 1.0)
    // rightMotor: high-frequency rumble (0.0 to 1.0)
    void setVibration(float leftMotor, float rightMotor);
    void stopVibration();

    // Rumble configuration
    RumbleConfig& getRumbleConfig() { return m_rumbleConfig; }
    const RumbleConfig& getRumbleConfig() const { return m_rumbleConfig; }

    // Get current motor output values (for visualization)
    float getLastHeavyMotor() const { return m_lastLeftMotor; }   // Left = heavy/low-freq
    float getLastLightMotor() const { return m_lastRightMotor; }  // Right = light/high-freq

    // Get individual effect contributions (for visualization)
    float getLastSuspensionRumble() const { return m_lastSuspensionRumble; }
    float getLastWheelspinRumble() const { return m_lastWheelspinRumble; }
    float getLastLockupRumble() const { return m_lastLockupRumble; }
    float getLastWheelieRumble() const { return m_lastWheelieRumble; }
    float getLastRpmRumble() const { return m_lastRpmRumble; }
    float getLastSlideRumble() const { return m_lastSlideRumble; }
    float getLastSurfaceRumble() const { return m_lastSurfaceRumble; }
    float getLastSteerRumble() const { return m_lastSteerRumble; }

    // History buffer size for graph visualization (matches MAX_TELEMETRY_HISTORY)
    static constexpr size_t MAX_RUMBLE_HISTORY = 200;

    // Get history buffers for graph visualization
    const std::deque<float>& getHeavyMotorHistory() const { return m_heavyMotorHistory; }
    const std::deque<float>& getLightMotorHistory() const { return m_lightMotorHistory; }
    const std::deque<float>& getSuspensionHistory() const { return m_suspensionHistory; }
    const std::deque<float>& getWheelspinHistory() const { return m_wheelspinHistory; }
    const std::deque<float>& getLockupHistory() const { return m_lockupHistory; }
    const std::deque<float>& getWheelieHistory() const { return m_wheelieHistory; }
    const std::deque<float>& getRpmHistory() const { return m_rpmHistory; }
    const std::deque<float>& getSlideHistory() const { return m_slideHistory; }
    const std::deque<float>& getSurfaceHistory() const { return m_surfaceHistory; }
    const std::deque<float>& getSteerHistory() const { return m_steerHistory; }

    // Process telemetry and apply rumble effects
    // suspensionVelocity: max of front/rear compression velocity (m/s, positive = compression)
    // wheelOverrun: rear wheel overrun ratio (wheelSpeed - vehicleSpeed) / vehicleSpeed (positive = wheelspin)
    // wheelUnderrun: max of front/rear underrun ratio (vehicleSpeed - wheelSpeed) / vehicleSpeed (positive = lockup)
    // rpm: engine RPM (raw value, typically 0-15000)
    // slideAngle: lateral slip angle in degrees (0 = no slip, 90 = full sideways)
    // surfaceSpeed: speed in m/s when on rough surface (0 = on track or not moving)
    // steerTorque: absolute handlebar torque in Nm (higher = more resistance)
    // wheelieIntensity: pitch angle in degrees when doing a wheelie (0 = not doing wheelie, >0 = wheelie angle)
    // isAirborne: true when both wheels are off the ground (suppresses ground effects)
    // suppressOutput: true to calculate forces for graph but not send to controller (e.g., when crashed)
    void updateRumbleFromTelemetry(float suspensionVelocity, float wheelOverrun, float wheelUnderrun, float rpm, float slideAngle, float surfaceSpeed, float steerTorque, float wheelieIntensity, bool isAirborne, bool suppressOutput = false);

private:
    XInputReader();
    ~XInputReader() = default;
    XInputReader(const XInputReader&) = delete;
    XInputReader& operator=(const XInputReader&) = delete;

    // Normalize stick values with deadzone
    float normalizeStickValue(SHORT value, SHORT deadzone) const;

    // Normalize trigger value
    float normalizeTriggerValue(BYTE value) const;

    static constexpr SHORT STICK_DEADZONE = XINPUT_GAMEPAD_LEFT_THUMB_DEADZONE;
    static constexpr BYTE TRIGGER_THRESHOLD = XINPUT_GAMEPAD_TRIGGER_THRESHOLD;

    XInputData m_data;
    int m_controllerIndex;

    // Vibration state tracking to avoid redundant API calls
    float m_lastLeftMotor;
    float m_lastRightMotor;

    // Individual effect values (for visualization)
    float m_lastSuspensionRumble;
    float m_lastWheelspinRumble;
    float m_lastLockupRumble;
    float m_lastWheelieRumble;
    float m_lastRpmRumble;
    float m_lastSlideRumble;
    float m_lastSurfaceRumble;
    float m_lastSteerRumble;

    // History buffers for graph visualization
    std::deque<float> m_heavyMotorHistory;
    std::deque<float> m_lightMotorHistory;
    std::deque<float> m_suspensionHistory;
    std::deque<float> m_wheelspinHistory;
    std::deque<float> m_lockupHistory;
    std::deque<float> m_wheelieHistory;
    std::deque<float> m_rpmHistory;
    std::deque<float> m_slideHistory;
    std::deque<float> m_surfaceHistory;
    std::deque<float> m_steerHistory;

    // Helper to push value to history buffer with size limit
    void pushToHistory(std::deque<float>& buffer, float value) {
        buffer.push_back(value);
        if (buffer.size() > MAX_RUMBLE_HISTORY) {
            buffer.pop_front();
        }
    }

    // Rumble configuration
    RumbleConfig m_rumbleConfig;
};
