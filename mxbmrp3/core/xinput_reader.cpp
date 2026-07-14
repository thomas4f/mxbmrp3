// ============================================================================
// core/xinput_reader.cpp
// XInput controller reader for raw gamepad input access
// ============================================================================
#include "xinput_reader.h"
#include "rumble_profile_manager.h"
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

// C++/WinRT for Windows.Gaming.Input (Windows 10+). MSVC-only: used solely to
// resolve friendly controller hardware names; mingw ships neither the WinRT
// headers nor windowsapp.lib. On non-MSVC the name cache stays empty and the
// code already renders "(unknown name)" — real XInput state (via <xinput.h>,
// which mingw does ship) is unaffected. See MXBMRP3_TEST_BUILD in game_config.h.
#ifdef _MSC_VER
#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.Foundation.Collections.h>
#include <winrt/Windows.Gaming.Input.h>

#pragma comment(lib, "windowsapp.lib")
#endif

using namespace PluginConstants;

// OS-call wrappers. In the headless TEST build there is never a real controller, and
// Wine's XInput can block/hang under sustained load from the I/O thread's tight poll
// loop — which would wedge the test harness on shutdown (join). So the test build makes
// these no-ops: the I/O thread still exercises its full structure + lifecycle (loop,
// drain, publish, scan cadence, start/stop/join) and the rumble send POLICY is still
// verified (via testConsumePendingRumble), but no real XInput syscall runs. The
// shipping (MSVC) build always issues the real calls; controller behavior + rumble feel
// are validated in-game on Windows, not headlessly.
namespace {
    inline DWORD osGetState(int index, XINPUT_STATE* state) {
#ifdef MXBMRP3_TEST_BUILD
        (void)index; if (state) ZeroMemory(state, sizeof(XINPUT_STATE));
        return ERROR_DEVICE_NOT_CONNECTED;
#else
        return XInputGetState(index, state);
#endif
    }
    inline void osSetState(int index, XINPUT_VIBRATION* vib) {
#ifdef MXBMRP3_TEST_BUILD
        (void)index; (void)vib;
#else
        XInputSetState(index, vib);
#endif
    }
}

XInputReader& XInputReader::getInstance() {
    static XInputReader instance;
    return instance;
}

XInputReader::XInputReader()
    : m_controllerIndex(0)
    , m_lastConnectedState{false, false, false, false}
    , m_connectionStateChanged(false)
    , m_lastConnectionScan(std::chrono::steady_clock::now())
    , m_lastLeftMotor(0.0f)
    , m_lastRightMotor(0.0f)
    , m_lastSentLeftMotor(0)
    , m_lastSentRightMotor(0)
    , m_hasSentVibration(false)
    , m_lastVibrationSend(std::chrono::steady_clock::now())
    , m_lastSuspensionRumble(0.0f)
    , m_lastWheelspinRumble(0.0f)
    , m_lastLockupRumble(0.0f)
    , m_lastRpmRumble(0.0f)
    , m_lastSlideRumble(0.0f)
    , m_lastSurfaceRumble(0.0f)
    , m_lastSteerRumble(0.0f)
    , m_lastRevLimiterRumble(0.0f)
    , m_lastPitLimiterRumble(0.0f)
    , m_lastSuspensionRumbleRear(0.0f)
    , m_lastLockupRumbleRear(0.0f)
    , m_lastWheelieRumble(0.0f)
{
    // Initialize connection state
    for (int i = 0; i < 4; i++) {
        m_lastConnectedState[i] = isControllerConnected(i);
    }
    DEBUG_INFO("XInputReader initialized");
}

void XInputReader::fillFromState(const XINPUT_STATE& state, XInputData& out) const {
    out.isConnected = true;
    const XINPUT_GAMEPAD& pad = state.Gamepad;
    // Sticks/triggers: raw input, no deadzone (for visualization)
    out.leftStickX = normalizeStickValue(pad.sThumbLX, 0);
    out.leftStickY = normalizeStickValue(pad.sThumbLY, 0);
    out.rightStickX = normalizeStickValue(pad.sThumbRX, 0);
    out.rightStickY = normalizeStickValue(pad.sThumbRY, 0);
    out.leftTrigger = normalizeTriggerValue(pad.bLeftTrigger);
    out.rightTrigger = normalizeTriggerValue(pad.bRightTrigger);
    out.dpadUp = (pad.wButtons & XINPUT_GAMEPAD_DPAD_UP) != 0;
    out.dpadDown = (pad.wButtons & XINPUT_GAMEPAD_DPAD_DOWN) != 0;
    out.dpadLeft = (pad.wButtons & XINPUT_GAMEPAD_DPAD_LEFT) != 0;
    out.dpadRight = (pad.wButtons & XINPUT_GAMEPAD_DPAD_RIGHT) != 0;
    out.buttonA = (pad.wButtons & XINPUT_GAMEPAD_A) != 0;
    out.buttonB = (pad.wButtons & XINPUT_GAMEPAD_B) != 0;
    out.buttonX = (pad.wButtons & XINPUT_GAMEPAD_X) != 0;
    out.buttonY = (pad.wButtons & XINPUT_GAMEPAD_Y) != 0;
    out.leftShoulder = (pad.wButtons & XINPUT_GAMEPAD_LEFT_SHOULDER) != 0;
    out.rightShoulder = (pad.wButtons & XINPUT_GAMEPAD_RIGHT_SHOULDER) != 0;
    out.leftThumb = (pad.wButtons & XINPUT_GAMEPAD_LEFT_THUMB) != 0;
    out.rightThumb = (pad.wButtons & XINPUT_GAMEPAD_RIGHT_THUMB) != 0;
    out.buttonStart = (pad.wButtons & XINPUT_GAMEPAD_START) != 0;
    out.buttonBack = (pad.wButtons & XINPUT_GAMEPAD_BACK) != 0;
}

void XInputReader::update() {
    // Cheap now: the I/O thread does the actual XInputGetState off-thread and
    // publishes the latest snapshot; here we just copy it into m_data (which getData()
    // returns). No XInput call, so a degraded controller driver can't stall the caller.
    std::lock_guard<std::mutex> lk(m_ioMutex);
    m_data = m_dataPublished;
}

void XInputReader::setControllerIndex(int index) {
    // XInput supports controllers 0-3, or -1 for disabled.
    int newIndex = std::max(-1, std::min(3, index));
    m_controllerIndex.store(newIndex, std::memory_order_relaxed);

    // Force the next setVibration() to actually send - the new controller's
    // motor state is unknown, so the dedup cache must not suppress it.
    m_hasSentVibration = false;

    // Tell the I/O thread to poll the freshly-selected slot immediately (skip the
    // disconnected backoff). The I/O thread also detects the index change itself and
    // stops rumble on the old slot (and on ALL slots when switching to disabled), so
    // no XInputSetState runs on the caller thread here anymore.
    m_pollImmediately.store(true, std::memory_order_relaxed);

    if (newIndex < 0) {
        m_lastLeftMotor = 0.0f;
        m_lastRightMotor = 0.0f;
    }
}

bool XInputReader::didConnectionStateChange() {
    return m_connectionStateChanged.exchange(false, std::memory_order_relaxed);
}

XInputReader::~XInputReader() {
    // Static-teardown backstop: only fires if the orchestrated shutdown (stopIoThread)
    // was skipped — e.g. the DLL is unloaded WITHOUT the Shutdown() export being
    // called. That path runs under the Windows loader lock (FreeLibrary -> static
    // dtors), and a std::thread::join() waits for the thread's OS-level exit, which
    // ALSO needs the loader lock -> deadlock. So DON'T join: signal stop, spin until
    // the thread has left our loop (an app-level flag, no loader lock involved), then
    // detach so its CRT/OS teardown finishes without us blocking on it. By the time
    // FreeLibrary unmaps our code, the thread is long past running any of it.
    if (m_ioThread.joinable()) {
        m_ioRun.store(false, std::memory_order_release);
        while (!m_ioFinished.load(std::memory_order_acquire)) {
            std::this_thread::yield();
        }
        m_ioThread.detach();
    }
}

void XInputReader::startIoThread() {
    if (m_ioRun.load(std::memory_order_acquire)) return;   // already running
    m_ioFinished.store(false, std::memory_order_release);
    m_ioRun.store(true, std::memory_order_release);
    m_ioThread = std::thread([this]() {
        // Top-level guard: an uncaught throw in a std::thread body calls
        // std::terminate() and takes the host game down with it.
        try {
            ioThreadMain();
        } catch (...) {
            DEBUG_ERROR("XInputReader: I/O thread terminated by exception");
        }
        // LAST: signal the destructor's spin-wait that we've left our code. Keep this
        // the final statement so no more of our (potentially-unmapped-soon) code runs.
        m_ioFinished.store(true, std::memory_order_release);
    });
    DEBUG_INFO("XInputReader: I/O thread started");
}

void XInputReader::stopIoThread() {
    if (!m_ioThread.joinable()) return;
    m_ioRun.store(false, std::memory_order_release);
    try { m_ioThread.join(); } catch (...) {}

    // Motors off on the way out so the pad doesn't keep buzzing after the plugin
    // unloads. One synchronous XInput call at shutdown (thread joined) is fine.
    int idx = m_controllerIndex.load(std::memory_order_relaxed);
    XINPUT_VIBRATION off = {};
    if (idx >= 0) {
        osSetState(idx, &off);
    } else {
        for (int i = 0; i < 4; i++) osSetState(i, &off);
    }
    DEBUG_INFO("XInputReader: I/O thread stopped");
}

void XInputReader::ioThreadMain() {
    using clock = std::chrono::steady_clock;
    int lastIdx = m_controllerIndex.load(std::memory_order_relaxed);
    bool wasConnected = false;                 // selected-slot connection state
    auto lastFailedPoll = clock::time_point{}; // disconnected-slot backoff anchor

    while (m_ioRun.load(std::memory_order_acquire)) {
        int idx = m_controllerIndex.load(std::memory_order_relaxed);

        // Selected slot changed (or disabled): stop rumble on the old slot (all slots
        // when disabling), and drop any pending command aimed at the old slot. This
        // replaces the XInputSetState that setControllerIndex used to do inline.
        if (idx != lastIdx) {
            { std::lock_guard<std::mutex> lk(m_ioMutex); m_pendingRumble = false; }
            XINPUT_VIBRATION off = {};
            if (lastIdx >= 0) osSetState(lastIdx, &off);
            if (idx < 0) for (int i = 0; i < 4; i++) osSetState(i, &off);
            lastIdx = idx;
            wasConnected = false;
        }

        // Execute the latest rumble command the caller's policy posted (if any).
        bool hasCmd = false;
        uint8_t left8 = 0, right8 = 0;
        int cmdIdx = idx;
        {
            std::lock_guard<std::mutex> lk(m_ioMutex);
            if (m_pendingRumble) {
                hasCmd = true;
                left8 = m_pendingLeft8;
                right8 = m_pendingRight8;
                cmdIdx = m_pendingIndex;
                m_pendingRumble = false;
            }
        }
        if (hasCmd && cmdIdx >= 0) {
            XINPUT_VIBRATION vibration = {};
            // 257 maps the 8-bit range back onto the full WORD range (255 -> 65535)
            vibration.wLeftMotorSpeed = static_cast<WORD>(left8 * 257);
            vibration.wRightMotorSpeed = static_cast<WORD>(right8 * 257);
            osSetState(cmdIdx, &vibration);
        }

        // Poll the selected slot into a local, then publish. Mirrors the old update()
        // logic: disabled -> default; disconnected -> back off (poll at most every
        // DISCONNECTED_POLL_INTERVAL_MS) to avoid hammering the slow empty-slot path.
        XInputData local;   // default = disconnected
        if (idx >= 0) {
            bool forced = m_pollImmediately.exchange(false, std::memory_order_relaxed);
            bool skip = !wasConnected && !forced &&
                        (clock::now() - lastFailedPoll < std::chrono::milliseconds(DISCONNECTED_POLL_INTERVAL_MS));
            if (!skip) {
                XINPUT_STATE state;
                ZeroMemory(&state, sizeof(XINPUT_STATE));
                if (osGetState(idx, &state) == ERROR_SUCCESS) {
                    fillFromState(state, local);
                    wasConnected = true;
                } else {
                    wasConnected = false;
                    lastFailedPoll = clock::now();
                }
            }
            // skip -> keep default (disconnected); wasConnected stays false
        }
        {
            std::lock_guard<std::mutex> lk(m_ioMutex);
            m_dataPublished = local;
        }

        // 1 Hz all-slot connection scan (feeds the settings UI controller list).
        auto now = clock::now();
        if (now - m_lastConnectionScan >= std::chrono::seconds(1)) {
            m_lastConnectionScan = now;
            for (int i = 0; i < 4; i++) {
                bool connected = isControllerConnected(i);
                if (connected != m_lastConnectedState[i]) {
                    m_connectionStateChanged.store(true, std::memory_order_relaxed);
                    m_lastConnectedState[i] = connected;
                    DEBUG_INFO_F("XInputReader: Controller %d %s", i + 1, connected ? "connected" : "disconnected");
                }
            }
        }

        std::this_thread::sleep_for(std::chrono::milliseconds(IO_LOOP_INTERVAL_MS));
    }
}

bool XInputReader::isControllerConnected(int index) {
    if (index < 0 || index > 3) return false;
    XINPUT_STATE state;
    ZeroMemory(&state, sizeof(XINPUT_STATE));
    return osGetState(index, &state) == ERROR_SUCCESS;
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

#ifdef _MSC_VER
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
#endif  // _MSC_VER (WinRT controller-name lookup; no-op on mingw)
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

    // Track values for visualization (full precision - the rumble graph
    // should show the computed signal, not the quantized output)
    m_lastLeftMotor = leftMotor;
    m_lastRightMotor = rightMotor;

    // Quantize to 8-bit steps - the motors can't render finer differences
    // (the Xbox BT rumble protocol is 8-bit per motor anyway), and it gives
    // a crisp definition of "motors off" for the idle check below
    uint8_t left8 = static_cast<uint8_t>(leftMotor * 255.0f + 0.5f);
    uint8_t right8 = static_cast<uint8_t>(rightMotor * 255.0f + 0.5f);

    // Send policy, shaped by two constraints:
    //  - Bluetooth: XInputSetState is a radio transaction; sustained 100Hz
    //    traffic can back up the BT stack until every call blocks
    //    (progressive FPS collapse). So: cap the send rate, and go fully
    //    silent while the motors are off.
    //  - Keepalive: controllers decay rumble without a continuous feed (a
    //    constant nonzero value sent once stops rumbling after a moment), so
    //    nonzero values are RE-SENT every interval even when unchanged -
    //    never deduped.
    // Transitions to (0,0) bypass the rate cap: a stop that lands inside the
    // cap window could otherwise be swallowed, and if telemetry then halts
    // (run deinit) the motors would be left running.
    bool isZero = (left8 == 0 && right8 == 0);
    bool lastWasZero = (m_lastSentLeftMotor == 0 && m_lastSentRightMotor == 0);
    auto now = std::chrono::steady_clock::now();
    if (m_hasSentVibration) {
        if (isZero && lastWasZero) {
            return;  // Idle: stay silent, "off" needs no keepalive
        }
        if (!isZero &&
            now - m_lastVibrationSend < std::chrono::milliseconds(m_rumbleSendIntervalMs)) {
            return;  // Live feed: capped cadence
        }
    }

    m_hasSentVibration = true;
    m_lastSentLeftMotor = left8;
    m_lastSentRightMotor = right8;
    m_lastVibrationSend = now;

    // The policy above (idle-silence, send cap, keepalive, transition-to-zero bypass)
    // is UNCHANGED and still runs on the caller thread at telemetry rate — only the
    // actual XInputSetState is deferred to the I/O thread. Post the latest 8-bit motor
    // pair; the I/O thread sends it (and coalesces if it hasn't drained the previous
    // one yet — a skipped keepalive is harmless, the next one lands within a cap window).
    {
        std::lock_guard<std::mutex> lk(m_ioMutex);
        m_pendingRumble = true;
        m_pendingLeft8 = left8;
        m_pendingRight8 = right8;
        m_pendingIndex = m_controllerIndex.load(std::memory_order_relaxed);
    }
}

void XInputReader::stopVibration() {
    setVibration(0.0f, 0.0f);
}

void XInputReader::syncGlobalSettingsToProfile(RumbleConfig* bikeConfig) const {
    // These settings are always global (stored in INI, never per-bike JSON)
    bikeConfig->enabled = m_rumbleConfig.enabled;
    bikeConfig->controllerIndex = m_rumbleConfig.controllerIndex;
    bikeConfig->additiveBlend = m_rumbleConfig.additiveBlend;
    bikeConfig->rumbleWhenCrashed = m_rumbleConfig.rumbleWhenCrashed;
    bikeConfig->usePerBikeEffects = m_rumbleConfig.usePerBikeEffects;
}

RumbleConfig& XInputReader::getRumbleConfig() {
    if (m_rumbleConfig.usePerBikeEffects) {
        auto& profileMgr = RumbleProfileManager::getInstance();
        RumbleConfig* bikeConfig = profileMgr.getProfileForCurrentBike();
        if (!bikeConfig) {
            // No profile yet - auto-create from global
            profileMgr.createProfileForCurrentBike(m_rumbleConfig);
            bikeConfig = profileMgr.getProfileForCurrentBike();
        }
        if (bikeConfig) {
            syncGlobalSettingsToProfile(bikeConfig);
            return *bikeConfig;
        }
    }
    return m_rumbleConfig;  // Global config from INI
}

const RumbleConfig& XInputReader::getRumbleConfig() const {
    if (m_rumbleConfig.usePerBikeEffects) {
        auto& profileMgr = RumbleProfileManager::getInstance();
        RumbleConfig* bikeConfig = profileMgr.getProfileForCurrentBike();
        if (bikeConfig) {
            // Safe: only syncing transient global settings, not persisted per-bike data
            syncGlobalSettingsToProfile(bikeConfig);
            return *bikeConfig;
        }
    }
    return m_rumbleConfig;  // Global config from INI
}

void XInputReader::updateRumbleFromTelemetry(float suspVelFront, float suspVelRear, float wheelOverrun, float underrunFront, float underrunRear, float rpm, float slideAngle, float surfaceSpeed, float steerTorque, float wheelieIntensity, float revLimiterPct, float pitLimiterActive, bool isAirborne, bool suppressOutput) {
    // If controller is disabled, don't process rumble at all
    if (m_controllerIndex < 0) {
        return;
    }

    // Get active config (global or per-bike based on mode)
    const RumbleConfig& config = getRumbleConfig();

    // Always calculate forces for graph visualization, even when rumble is disabled
    // m_last*Rumble stores the max motor contribution for visualization

    // Get input values (used for both normalized calculation and motor-specific)
    // Bumps/Lockup carry separate front/rear inputs. When their effect isn't split
    // we collapse to the max (matching the legacy single-input behavior); when split
    // each wheel drives its own effect, so keep the per-wheel inputs too.
    float suspFrontInput = std::abs(suspVelFront);
    float suspRearInput = std::abs(suspVelRear);
    float suspInput = std::abs(std::max(suspVelFront, suspVelRear));
    float spinInput = std::max(0.0f, wheelOverrun);
    float lockFrontInput = std::max(0.0f, underrunFront);
    float lockRearInput = std::max(0.0f, underrunRear);
    float lockInput = std::max(lockFrontInput, lockRearInput);
    float wheelieInput = wheelieIntensity;
    float rpmInput = rpm;
    float slideInput = slideAngle;
    float surfaceInput = surfaceSpeed;
    float steerInput = std::abs(steerTorque);
    // Rev/pit limiter are engine/state effects (not ground-dependent), active even airborne.
    float revLimiterInput = std::max(0.0f, revLimiterPct);
    float pitLimiterInput = std::max(0.0f, pitLimiterActive);

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
        m_lastSuspensionRumbleRear = 0.0f;
        m_lastWheelspinRumble = 0.0f;
        m_lastLockupRumble = 0.0f;
        m_lastLockupRumbleRear = 0.0f;
        m_lastSlideRumble = 0.0f;
        m_lastSurfaceRumble = 0.0f;
        m_lastSteerRumble = 0.0f;
        m_lastWheelieRumble = 0.0f;
        // RPM still active mid-air but reduced (engine under less load)
        float rpmNorm = config.rpmEffect.calculateNormalized(rpmInput) * 0.5f;
        m_lastRpmRumble = rpmNorm * std::max(config.rpmEffect.lightStrength,
                                              config.rpmEffect.heavyStrength);
    } else {
        // Store max motor contribution for visualization (max of light/heavy)
        auto maxContrib = [](const RumbleEffect& eff, float input) {
            float norm = eff.calculateNormalized(input);
            return norm * std::max(eff.lightStrength, eff.heavyStrength);
        };
        // Bumps/Lockup: when split, keep front and rear separate so the graph can draw two
        // traces (front in m_last*Rumble, rear in m_last*RumbleRear); when linked, the single
        // value is the combined contribution and the rear trace is unused.
        if (config.suspensionSplit) {
            m_lastSuspensionRumble = maxContrib(config.suspensionEffectFront, suspFrontInput);
            m_lastSuspensionRumbleRear = maxContrib(config.suspensionEffectRear, suspRearInput);
        } else {
            m_lastSuspensionRumble = maxContrib(config.suspensionEffect, suspInput);
            m_lastSuspensionRumbleRear = 0.0f;
        }
        m_lastWheelspinRumble = maxContrib(config.wheelspinEffect, spinInput);
        if (config.brakeLockupSplit) {
            m_lastLockupRumble = maxContrib(config.brakeLockupEffectFront, lockFrontInput);
            m_lastLockupRumbleRear = maxContrib(config.brakeLockupEffectRear, lockRearInput);
        } else {
            m_lastLockupRumble = maxContrib(config.brakeLockupEffect, lockInput);
            m_lastLockupRumbleRear = 0.0f;
        }
        m_lastWheelieRumble = maxContrib(config.wheelieEffect, wheelieInput);
        m_lastRpmRumble = maxContrib(config.rpmEffect, rpmInput);
        m_lastSlideRumble = maxContrib(config.slideEffect, slideInput);
        m_lastSurfaceRumble = maxContrib(config.surfaceEffect, surfaceInput);
        m_lastSteerRumble = maxContrib(config.steerEffect, steerInput);
    }

    // Rev/pit limiter visualization - engine/state effects, active regardless of airborne
    {
        auto maxContrib = [](const RumbleEffect& eff, float input) {
            float norm = eff.calculateNormalized(input);
            return norm * std::max(eff.lightStrength, eff.heavyStrength);
        };
        m_lastRevLimiterRumble = maxContrib(config.revLimiterEffect, revLimiterInput);
        m_lastPitLimiterRumble = maxContrib(config.pitLimiterEffect, pitLimiterInput);
    }

    // Combine effects - each effect contributes independently to each motor
    float heavyMotor = 0.0f;
    float lightMotor = 0.0f;

    // Helper lambda for blending based on mode
    // Note: additiveBlend comes from global config, not per-bike profile
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
        // Suspension effect (front/rear blended independently when split)
        if (config.suspensionSplit) {
            blend(heavyMotor, config.suspensionEffectFront.calculateHeavy(suspFrontInput));
            blend(lightMotor, config.suspensionEffectFront.calculateLight(suspFrontInput));
            blend(heavyMotor, config.suspensionEffectRear.calculateHeavy(suspRearInput));
            blend(lightMotor, config.suspensionEffectRear.calculateLight(suspRearInput));
        } else {
            blend(heavyMotor, config.suspensionEffect.calculateHeavy(suspInput));
            blend(lightMotor, config.suspensionEffect.calculateLight(suspInput));
        }

        // Wheelspin effect
        blend(heavyMotor, config.wheelspinEffect.calculateHeavy(spinInput));
        blend(lightMotor, config.wheelspinEffect.calculateLight(spinInput));

        // Brake lockup effect (front/rear blended independently when split)
        if (config.brakeLockupSplit) {
            blend(heavyMotor, config.brakeLockupEffectFront.calculateHeavy(lockFrontInput));
            blend(lightMotor, config.brakeLockupEffectFront.calculateLight(lockFrontInput));
            blend(heavyMotor, config.brakeLockupEffectRear.calculateHeavy(lockRearInput));
            blend(lightMotor, config.brakeLockupEffectRear.calculateLight(lockRearInput));
        } else {
            blend(heavyMotor, config.brakeLockupEffect.calculateHeavy(lockInput));
            blend(lightMotor, config.brakeLockupEffect.calculateLight(lockInput));
        }

        // Wheelie effect
        blend(heavyMotor, config.wheelieEffect.calculateHeavy(wheelieInput));
        blend(lightMotor, config.wheelieEffect.calculateLight(wheelieInput));

        // Slide effect
        blend(heavyMotor, config.slideEffect.calculateHeavy(slideInput));
        blend(lightMotor, config.slideEffect.calculateLight(slideInput));

        // Surface effect
        blend(heavyMotor, config.surfaceEffect.calculateHeavy(surfaceInput));
        blend(lightMotor, config.surfaceEffect.calculateLight(surfaceInput));

        // Steer effect
        blend(heavyMotor, config.steerEffect.calculateHeavy(steerInput));
        blend(lightMotor, config.steerEffect.calculateLight(steerInput));

        // RPM effect (full strength on ground)
        blend(heavyMotor, config.rpmEffect.calculateHeavy(rpmInput));
        blend(lightMotor, config.rpmEffect.calculateLight(rpmInput));
    } else {
        // RPM still active mid-air but reduced (engine under less load)
        float rpmNorm = config.rpmEffect.calculateNormalized(rpmInput) * 0.5f;
        blend(heavyMotor, rpmNorm * config.rpmEffect.heavyStrength);
        blend(lightMotor, rpmNorm * config.rpmEffect.lightStrength);
    }

    // Rev/pit limiter - engine/state effects, applied regardless of airborne
    blend(heavyMotor, config.revLimiterEffect.calculateHeavy(revLimiterInput));
    blend(lightMotor, config.revLimiterEffect.calculateLight(revLimiterInput));
    blend(heavyMotor, config.pitLimiterEffect.calculateHeavy(pitLimiterInput));
    blend(lightMotor, config.pitLimiterEffect.calculateLight(pitLimiterInput));

    // Clamp to valid range (important for additive mode)
    heavyMotor = std::min(1.0f, heavyMotor);
    lightMotor = std::min(1.0f, lightMotor);

    // Record history for graph visualization
    pushToHistory(m_heavyMotorHistory, heavyMotor);
    pushToHistory(m_lightMotorHistory, lightMotor);
    pushToHistory(m_suspensionHistory, m_lastSuspensionRumble);
    pushToHistory(m_suspensionRearHistory, m_lastSuspensionRumbleRear);
    pushToHistory(m_wheelspinHistory, m_lastWheelspinRumble);
    pushToHistory(m_lockupHistory, m_lastLockupRumble);
    pushToHistory(m_lockupRearHistory, m_lastLockupRumbleRear);
    pushToHistory(m_wheelieHistory, m_lastWheelieRumble);
    pushToHistory(m_rpmHistory, m_lastRpmRumble);
    pushToHistory(m_slideHistory, m_lastSlideRumble);
    pushToHistory(m_surfaceHistory, m_lastSurfaceRumble);
    pushToHistory(m_steerHistory, m_lastSteerRumble);
    pushToHistory(m_revLimiterHistory, m_lastRevLimiterRumble);
    pushToHistory(m_pitLimiterHistory, m_lastPitLimiterRumble);

    // Send to controller (unless suppressed or disabled)
    // Graph still updates even when output is suppressed
    // Note: enabled comes from global config, not per-bike profile
    if (suppressOutput || !m_rumbleConfig.enabled) {
        setVibration(0.0f, 0.0f);
    } else {
        setVibration(heavyMotor, lightMotor);
    }
}
