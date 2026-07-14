// ============================================================================
// core/xinput_reader.h
// XInput controller reader for raw gamepad input access
// ============================================================================
#pragma once

#include <windows.h>
#include <Xinput.h>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <deque>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

// Forward declare WinRT types to avoid pulling headers into every translation unit
namespace winrt::Windows::Gaming::Input {
    struct RawGameController;
}

// Rumble effect configuration for a single source
// Each effect has independent strength settings for light and heavy motors
struct RumbleEffect {
    float minInput;       // Input value where rumble starts (e.g., 0.04 = 4% slip)
    float maxInput;       // Input value where rumble reaches max (e.g., 0.15 = 15% slip)
    float lightStrength;  // Peak strength for light motor (0.0 = off, up to 1.0)
    float heavyStrength;  // Peak strength for heavy motor (0.0 = off, up to 1.0)

    RumbleEffect() : minInput(0.0f), maxInput(1.0f),
                     lightStrength(0.0f), heavyStrength(0.0f) {}

    RumbleEffect(float _minIn, float _maxIn, float _lightStr, float _heavyStr)
        : minInput(_minIn), maxInput(_maxIn),
          lightStrength(_lightStr), heavyStrength(_heavyStr) {}

    // Check if effect targets a specific motor
    bool targetsHeavy() const { return heavyStrength > 0.0f; }
    bool targetsLight() const { return lightStrength > 0.0f; }
    bool isEnabled() const { return lightStrength > 0.0f || heavyStrength > 0.0f; }

    // Calculate normalized intensity (0-1) from input value
    // Caller should multiply by lightStrength/heavyStrength for actual motor output
    float calculateNormalized(float inputValue) const {
        if (!isEnabled() || inputValue < minInput) return 0.0f;
        if (inputValue >= maxInput) return 1.0f;
        // Protect against division by zero if minInput equals maxInput
        float range = maxInput - minInput;
        if (range <= 0.0f) return 1.0f;
        // Linear interpolation from 0 to 1
        return (inputValue - minInput) / range;
    }

    // Calculate rumble intensity for light motor
    float calculateLight(float inputValue) const {
        return calculateNormalized(inputValue) * lightStrength;
    }

    // Calculate rumble intensity for heavy motor
    float calculateHeavy(float inputValue) const {
        return calculateNormalized(inputValue) * heavyStrength;
    }
};

// Controller rumble configuration
struct RumbleConfig {
    bool enabled;           // Master enable/disable
    int controllerIndex;    // Which XInput controller (0-3), or -1 for disabled
    bool additiveBlend;     // true = add effects (clamped), false = max wins
    bool rumbleWhenCrashed; // false = stop all rumble when player is crashed (default)
    bool usePerBikeEffects; // true = use per-bike effects from JSON, false = use global from INI

    // Bumps: suspension compression on impacts/landings.
    // The combined/front/rear effects are stored independently: combined is used when
    // linked, front+rear when split. Each persists separately so a split tune survives
    // toggling link<->split.
    RumbleEffect suspensionEffect;       // combined (used when not split)
    RumbleEffect suspensionEffectFront;  // front wheel (used when split)
    RumbleEffect suspensionEffectRear;   // rear wheel (used when split)
    bool suspensionSplit;                // tune front/rear suspension independently
    bool suspensionSplitInitialized;     // front/rear seeded from combined once; never reseeded after

    // Spin: rear wheel overrun (traction feedback)
    RumbleEffect wheelspinEffect;

    // Brake Lockup: wheel underrun (combined/front/rear stored independently, like Bumps)
    RumbleEffect brakeLockupEffect;      // combined (used when not split)
    RumbleEffect brakeLockupEffectFront; // front wheel (used when split)
    RumbleEffect brakeLockupEffectRear;  // rear wheel (used when split)
    bool brakeLockupSplit;               // tune front/rear lockup independently
    bool brakeLockupSplitInitialized;    // front/rear seeded from combined once; never reseeded after

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

    // Rev Limiter: vibration as RPM approaches the bike's limiter.
    // Input is RPM as a percentage of the bike's real limiterRPM (auto per-bike), throttle-gated.
    RumbleEffect revLimiterEffect;

    // Pit Limiter: vibration while the pit-lane speed limiter is active (GP Bikes).
    // Input is binary (1 = active). Light/Heavy set the intensity.
    RumbleEffect pitLimiterEffect;

    // Constructor: RumbleEffect(minInput, maxInput, lightStrength, heavyStrength)
    RumbleConfig() : enabled(false), controllerIndex(0), additiveBlend(true), rumbleWhenCrashed(false), usePerBikeEffects(false),
        // Bumps: Off by default (tuning range: 0-10 m/s). Front/rear default to the
        // same factory values as combined, so a split starts from the defaults.
        suspensionEffect(0.0f, 10.0f, 0.0f, 0.0f),
        suspensionEffectFront(0.0f, 10.0f, 0.0f, 0.0f),
        suspensionEffectRear(0.0f, 10.0f, 0.0f, 0.0f),
        suspensionSplit(false),
        suspensionSplitInitialized(false),
        // Spin: Light motor at 50% strength
        wheelspinEffect(0.0f, 15.0f, 0.5f, 0.0f),
        // Lockup: Light motor at 50% strength (front/rear default to the same)
        brakeLockupEffect(0.2f, 1.0f, 0.5f, 0.0f),
        brakeLockupEffectFront(0.2f, 1.0f, 0.5f, 0.0f),
        brakeLockupEffectRear(0.2f, 1.0f, 0.5f, 0.0f),
        brakeLockupSplit(false),
        brakeLockupSplitInitialized(false),
        // Wheelie: Off by default
        wheelieEffect(0.0f, 90.0f, 0.0f, 0.0f),
        // RPM: Off by default
        rpmEffect(2000.0f, 15000.0f, 0.0f, 0.0f),
        // Slide: Heavy motor at full strength
        slideEffect(10.0f, 30.0f, 0.0f, 1.0f),
        // Surface: Off by default
        surfaceEffect(10.0f, 135.0f, 0.0f, 0.0f),
        // Steer: Off by default
        steerEffect(20.0f, 80.0f, 0.0f, 0.0f),
        // Rev Limiter: Off by default (range 93%-100% of the limiter RPM)
        revLimiterEffect(93.0f, 100.0f, 0.0f, 0.0f),
        // Pit Limiter: Off by default (binary input 0/1)
        pitLimiterEffect(0.0f, 1.0f, 0.0f, 0.0f) {}

    void resetToDefaults() {
        enabled = false;
        controllerIndex = 0;
        additiveBlend = true;
        rumbleWhenCrashed = false;
        usePerBikeEffects = false;
        suspensionEffect = RumbleEffect(0.0f, 10.0f, 0.0f, 0.0f);
        suspensionEffectFront = RumbleEffect(0.0f, 10.0f, 0.0f, 0.0f);
        suspensionEffectRear = RumbleEffect(0.0f, 10.0f, 0.0f, 0.0f);
        suspensionSplit = false;
        suspensionSplitInitialized = false;
        wheelspinEffect = RumbleEffect(0.0f, 15.0f, 0.5f, 0.0f);
        brakeLockupEffect = RumbleEffect(0.2f, 1.0f, 0.5f, 0.0f);
        brakeLockupEffectFront = RumbleEffect(0.2f, 1.0f, 0.5f, 0.0f);
        brakeLockupEffectRear = RumbleEffect(0.2f, 1.0f, 0.5f, 0.0f);
        brakeLockupSplit = false;
        brakeLockupSplitInitialized = false;
        wheelieEffect = RumbleEffect(0.0f, 90.0f, 0.0f, 0.0f);
        rpmEffect = RumbleEffect(2000.0f, 15000.0f, 0.0f, 0.0f);
        slideEffect = RumbleEffect(10.0f, 30.0f, 0.0f, 1.0f);
        surfaceEffect = RumbleEffect(10.0f, 135.0f, 0.0f, 0.0f);
        steerEffect = RumbleEffect(20.0f, 80.0f, 0.0f, 0.0f);
        revLimiterEffect = RumbleEffect(93.0f, 100.0f, 0.0f, 0.0f);
        pitLimiterEffect = RumbleEffect(0.0f, 1.0f, 0.0f, 0.0f);
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

    // Start / stop the dedicated XInput I/O thread. That thread owns EVERY
    // XInputGetState/XInputSetState on the hot path, so a slow/degraded controller
    // driver (a Bluetooth XInputSetState is a radio transaction; polling a dead slot
    // is device enumeration — both ms-class on a bad stack) can never stall whichever
    // thread drives telemetry/hotkeys (the game thread in legacy mode, the plugin
    // worker in threaded mode). The caller keeps the rumble policy + effect math and
    // only posts target motor values / reads a published input snapshot. Started in
    // PluginManager::initialize (after settings load), stopped in shutdown AFTER the
    // plugin worker is joined. Idempotent. See core/plugin_thread.h for the sibling.
    void startIoThread();
    void stopIoThread();

    // Update controller state (call once per frame). Now cheap: copies the latest
    // snapshot the I/O thread published — issues no XInput call itself.
    void update();

    // Get current controller data
    const XInputData& getData() const {
#ifdef MXBMRP3_TEST_BUILD
        if (m_testForced) return m_testData;   // preview/tests can force a fake controller
#endif
        return m_data;
    }
#ifdef MXBMRP3_TEST_BUILD
    void testForceData(const XInputData& d) { m_testData = d; m_testForced = true; }
    void testClearForcedData() { m_testForced = false; }
    // Drain the rumble command setVibration's policy posted (what the I/O thread would
    // execute), so a headless test can assert the send policy + 8-bit quantization are
    // preserved after moving the actual XInputSetState off-thread. Stop the I/O thread
    // first (testStopIoForTest) so it doesn't drain the command out from under the test.
    bool testConsumePendingRumble(int& left8, int& right8, int& idx) {
        std::lock_guard<std::mutex> lk(m_ioMutex);
        if (!m_pendingRumble) return false;
        left8 = m_pendingLeft8; right8 = m_pendingRight8; idx = m_pendingIndex;
        m_pendingRumble = false;
        return true;
    }
#endif

    // Set which controller to read (0-3)
    void setControllerIndex(int index);
    int getControllerIndex() const { return m_controllerIndex; }

    // Check if a specific controller index is connected (0-3)
    static bool isControllerConnected(int index);

    // Check if any controller connection state changed since last call
    // Returns true once per state change (consumes the flag)
    bool didConnectionStateChange();

    // Get controller name (uses Windows.Gaming.Input)
    // Returns empty string if controller not found
    static std::string getControllerName(int index);

    // Vibration control
    // leftMotor: low-frequency rumble (0.0 to 1.0)
    // rightMotor: high-frequency rumble (0.0 to 1.0)
    void setVibration(float leftMotor, float rightMotor);
    void stopVibration();

    // Rumble configuration
    // Returns active config - either global (from INI) or per-bike (from JSON) based on mode
    // Note: Non-const version auto-creates per-bike profile if missing; const version
    // falls back to global config (can't modify state to create profile)
    RumbleConfig& getRumbleConfig();
    const RumbleConfig& getRumbleConfig() const;

    // Get the global rumble config (always from INI, ignores per-bike mode)
    RumbleConfig& getGlobalRumbleConfig() { return m_rumbleConfig; }
    const RumbleConfig& getGlobalRumbleConfig() const { return m_rumbleConfig; }

    // Rumble send-rate cap (INI-only setting: [Rumble] send_interval_ms).
    // Lower = more responsive feed, higher = less Bluetooth traffic.
    void setRumbleSendIntervalMs(int ms) {
        m_rumbleSendIntervalMs = std::max(MIN_RUMBLE_SEND_INTERVAL_MS,
                                          std::min(ms, MAX_RUMBLE_SEND_INTERVAL_MS));
    }
    int getRumbleSendIntervalMs() const { return m_rumbleSendIntervalMs; }

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
    float getLastRevLimiterRumble() const { return m_lastRevLimiterRumble; }
    float getLastPitLimiterRumble() const { return m_lastPitLimiterRumble; }
    // Rear-wheel contributions for Bumps/Lockup when split (the front/combined value is in
    // getLastSuspensionRumble/getLastLockupRumble); 0 when the effect isn't split.
    float getLastSuspensionRumbleRear() const { return m_lastSuspensionRumbleRear; }
    float getLastLockupRumbleRear() const { return m_lastLockupRumbleRear; }

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
    const std::deque<float>& getRevLimiterHistory() const { return m_revLimiterHistory; }
    const std::deque<float>& getPitLimiterHistory() const { return m_pitLimiterHistory; }
    const std::deque<float>& getSuspensionRearHistory() const { return m_suspensionRearHistory; }
    const std::deque<float>& getLockupRearHistory() const { return m_lockupRearHistory; }

    // Process telemetry and apply rumble effects
    // suspVelFront/suspVelRear: front/rear suspension compression velocity (m/s, positive = compression).
    //   When the Bumps effect isn't split, the engine collapses these to their max (legacy behavior).
    // wheelOverrun: rear wheel overrun ratio (wheelSpeed - vehicleSpeed) / vehicleSpeed (positive = wheelspin)
    // underrunFront/underrunRear: front/rear underrun ratio (vehicleSpeed - wheelSpeed) / vehicleSpeed (positive = lockup).
    //   When the Lockup effect isn't split, the engine collapses these to their max (legacy behavior).
    // rpm: engine RPM (raw value, typically 0-15000)
    // slideAngle: lateral slip angle in degrees (0 = no slip, 90 = full sideways)
    // surfaceSpeed: speed in m/s when on rough surface (0 = on track or not moving)
    // steerTorque: absolute handlebar torque in Nm (higher = more resistance)
    // wheelieIntensity: pitch angle in degrees when doing a wheelie (0 = not doing wheelie, >0 = wheelie angle)
    // revLimiterPct: RPM as a percentage of the bike's limiter RPM, throttle-gated (0 when off-throttle)
    // pitLimiterActive: 1.0 while the pit-lane speed limiter is active, 0.0 otherwise
    // isAirborne: true when both wheels are off the ground (suppresses ground effects)
    // suppressOutput: true to calculate forces for graph but not send to controller (e.g., when crashed)
    void updateRumbleFromTelemetry(float suspVelFront, float suspVelRear, float wheelOverrun, float underrunFront, float underrunRear, float rpm, float slideAngle, float surfaceSpeed, float steerTorque, float wheelieIntensity, float revLimiterPct, float pitLimiterActive, bool isAirborne, bool suppressOutput = false);

private:
    XInputReader();
    ~XInputReader();
    XInputReader(const XInputReader&) = delete;
    XInputReader& operator=(const XInputReader&) = delete;

    // I/O-thread body: owns the selected-slot poll (+ disconnected backoff), the
    // 1 Hz all-slot connection scan, and the rumble send (executing whatever
    // setVibration's policy posted). Never holds m_ioMutex across an XInput call.
    void ioThreadMain();
    // Map a raw XINPUT_STATE into our XInputData (shared by the I/O poll).
    void fillFromState(const XINPUT_STATE& state, XInputData& out) const;

    // Normalize stick values with deadzone
    float normalizeStickValue(SHORT value, SHORT deadzone) const;

    // Normalize trigger value
    float normalizeTriggerValue(BYTE value) const;

    // Sync global settings (enabled, controllerIndex, etc.) to a per-bike config
    // These settings are never stored per-bike, so we copy from m_rumbleConfig
    void syncGlobalSettingsToProfile(RumbleConfig* bikeConfig) const;

    static constexpr SHORT STICK_DEADZONE = XINPUT_GAMEPAD_LEFT_THUMB_DEADZONE;
    static constexpr BYTE TRIGGER_THRESHOLD = XINPUT_GAMEPAD_TRIGGER_THRESHOLD;

    // m_data is the snapshot the caller reads via getData(); update() refreshes it
    // from m_dataPublished (which the I/O thread writes). Touched only on the caller
    // thread, so getData() can keep returning a reference.
    XInputData m_data;
#ifdef MXBMRP3_TEST_BUILD
    bool m_testForced = false;
    XInputData m_testData;
#endif
    // Selected slot (0-3, or -1 = disabled). Atomic: written by the caller
    // (setControllerIndex), read by the I/O thread every loop.
    std::atomic<int> m_controllerIndex{ 0 };

    // ---- XInput I/O thread ----------------------------------------------------
    std::thread m_ioThread;
    std::atomic<bool> m_ioRun{ false };
    // Set true when the I/O thread has left its loop. The DESTRUCTOR spins on this
    // (not join()) then detaches: the destructor runs during static teardown under the
    // Windows loader lock, and a thread's OS-level exit also needs that lock, so a
    // join() there would deadlock. Spinning on an app-level flag needs no loader lock.
    std::atomic<bool> m_ioFinished{ false };
    mutable std::mutex m_ioMutex;                 // guards the two handoff buffers below
    XInputData m_dataPublished;                   // I/O thread writes, update() reads
    // Pending rumble command posted by setVibration (policy runs on the caller; the
    // I/O thread just executes the last posted 8-bit motor pair).
    bool m_pendingRumble = false;
    uint8_t m_pendingLeft8 = 0;
    uint8_t m_pendingRight8 = 0;
    int m_pendingIndex = 0;
    // Set by setControllerIndex so the I/O thread skips the disconnected backoff and
    // polls the freshly-selected slot immediately.
    std::atomic<bool> m_pollImmediately{ false };

    // Connection state tracking for change detection (I/O-thread-owned now). The
    // all-slot scan is throttled to 1 Hz: XInputGetState on a disconnected slot is
    // notoriously slow (device enumeration). The result only feeds the settings UI.
    bool m_lastConnectedState[4];
    std::atomic<bool> m_connectionStateChanged{ false };
    std::chrono::steady_clock::time_point m_lastConnectionScan;

    // Backoff for polling the SELECTED slot while it is disconnected (I/O-thread-owned).
    static constexpr int DISCONNECTED_POLL_INTERVAL_MS = 500;
    // I/O thread loop period. Bounds input freshness + rumble keepalive cadence; the
    // rumble send RATE is still capped by the caller's send-interval policy.
    static constexpr int IO_LOOP_INTERVAL_MS = 5;

    // Vibration state tracking to avoid redundant API calls
    float m_lastLeftMotor;
    float m_lastRightMotor;

    // Motor speeds last sent via XInputSetState (8-bit quantized). Nonzero
    // rumble is a continuous capped-rate feed (controllers decay rumble
    // without refreshes); only the all-zero idle state is deduped so an idle
    // pad generates no Bluetooth traffic. See setVibration().
    uint8_t m_lastSentLeftMotor;
    uint8_t m_lastSentRightMotor;
    bool m_hasSentVibration;
    std::chrono::steady_clock::time_point m_lastVibrationSend;
    int m_rumbleSendIntervalMs = DEFAULT_RUMBLE_SEND_INTERVAL_MS;
    static constexpr int DEFAULT_RUMBLE_SEND_INTERVAL_MS = 10;  // 100Hz live feed (matches the telemetry tick)
    static constexpr int MIN_RUMBLE_SEND_INTERVAL_MS = 4;       // ~250Hz (effectively uncapped)
    static constexpr int MAX_RUMBLE_SEND_INTERVAL_MS = 200;     // 5Hz floor

    // Individual effect values (for visualization)
    float m_lastSuspensionRumble;
    float m_lastWheelspinRumble;
    float m_lastLockupRumble;
    float m_lastWheelieRumble;
    float m_lastRpmRumble;
    float m_lastSlideRumble;
    float m_lastSurfaceRumble;
    float m_lastSteerRumble;
    float m_lastRevLimiterRumble;
    float m_lastPitLimiterRumble;
    float m_lastSuspensionRumbleRear;
    float m_lastLockupRumbleRear;

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
    std::deque<float> m_revLimiterHistory;
    std::deque<float> m_pitLimiterHistory;
    std::deque<float> m_suspensionRearHistory;
    std::deque<float> m_lockupRearHistory;

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
