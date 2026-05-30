// ============================================================================
// core/discord_manager.cpp
// Discord Rich Presence integration using Discord IPC protocol
// ============================================================================
#include "discord_manager.h"
#include "plugin_data.h"
#include "plugin_utils.h"
#include "../game/game_config.h"
#include "../diagnostics/logger.h"
#include "../vendor/nlohmann/json.hpp"
#include <windows.h>
#include <cstdio>
#include <cstring>

// Discord Application IDs per game.
// TODO: Register separate Discord applications for GP Bikes, WRS, and KRP so each
// game gets its own icon/name in Rich Presence. Until then, all non-MXB builds
// reuse the MXB application ID intentionally — it still works, the icon is just
// branded as MX Bikes.
#if defined(GAME_MXBIKES) || defined(GAME_GPBIKES) || defined(GAME_WRS) || defined(GAME_KRP)
    static constexpr const char* DISCORD_APPLICATION_ID = "1124352181441679441";
#endif

// Discord IPC opcodes
namespace DiscordOpcode {
    constexpr int HANDSHAKE = 0;
    constexpr int FRAME = 1;
    constexpr int CLOSE = 2;
    constexpr int PING = 3;
    constexpr int PONG = 4;
}

DiscordManager::DiscordManager()
    : m_enabled(false)
    , m_state(State::DISCONNECTED)
    , m_presenceUpdateNeeded(false)
    , m_shutdownRequested(false)
    , m_pipe(INVALID_HANDLE_VALUE)
    , m_nonce(0)
{
}

DiscordManager::~DiscordManager() {
    shutdown();
}

DiscordManager& DiscordManager::getInstance() {
    static DiscordManager instance;
    return instance;
}

void DiscordManager::initialize() {
    if (!m_enabled) {
        DEBUG_INFO("DiscordManager: Disabled by settings, not initializing");
        return;
    }

    // Thread may have been started by setEnabled() during settings load
    if (m_connectionThread.joinable()) {
        DEBUG_INFO("DiscordManager: Connection thread already running");
        return;
    }

    DEBUG_INFO("DiscordManager: Initializing");
    m_shutdownRequested = false;

    // Prime the snapshot from the game thread before the connection thread
    // starts. Without this, if Discord finishes its handshake before any
    // onDataChanged fires, the first presence frame would render the default
    // (empty) snapshot as "In Menus" even when a session is already active.
    updateSnapshot();

    // Start background connection thread
    m_connectionThread = std::thread(&DiscordManager::connectionThread, this);
}

void DiscordManager::shutdown() {
    DEBUG_INFO("DiscordManager: Shutting down");

    m_shutdownRequested = true;

    // Cancel any blocking pipe I/O (ReadFile/WriteFile/CreateFileA) on the
    // connection thread so it can check m_shutdownRequested and exit promptly.
    // Without this, the thread may block indefinitely on ReadFile while holding
    // m_pipeMutex, preventing disconnect() from closing the pipe handle.
    if (m_connectionThread.joinable()) {
        HANDLE hThread = static_cast<HANDLE>(m_connectionThread.native_handle());
        if (hThread) {
            CancelSynchronousIo(hThread);
        }
        m_connectionThread.join();
    }

    disconnect();
}

void DiscordManager::connectionThread() {
    // Exception barrier: an uncaught throw in a std::thread calls
    // std::terminate() and kills the host game process. sendPresenceUpdate()
    // already catches JSON serialization errors internally, but other code
    // here (mutex / chrono / future Discord protocol changes) shouldn't be
    // able to crash the game either.
    try {

        while (!m_shutdownRequested) {
            if (m_enabled && m_state != State::CONNECTED) {
                // Attempt connection
                auto now = std::chrono::steady_clock::now();
                auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
                    now - m_lastConnectionAttempt
                ).count();

                if (elapsed >= RECONNECT_INTERVAL_MS || m_state == State::DISCONNECTED) {
                    m_lastConnectionAttempt = now;
                    m_state = State::CONNECTING;

                    if (connect()) {
                        DEBUG_INFO("DiscordManager: Connected to Discord");
                        m_state = State::CONNECTED;
                        m_presenceUpdateNeeded = true;  // Send initial presence
                        m_lastPresenceRefresh = std::chrono::steady_clock::now();
                    } else {
                        m_state = State::FAILED;
                    }
                }
            } else if (!m_enabled && m_state != State::DISCONNECTED) {
                // User disabled - disconnect
                disconnect();
            }

            // Periodic presence refresh to detect disconnection and keep presence alive
            if (m_state == State::CONNECTED) {
                auto now = std::chrono::steady_clock::now();
                auto refreshElapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
                    now - m_lastPresenceRefresh
                ).count();

                if (refreshElapsed >= PRESENCE_REFRESH_INTERVAL_MS) {
                    m_presenceUpdateNeeded = true;
                    m_lastPresenceRefresh = now;
                }
            }

            // Check for presence update
            if (m_state == State::CONNECTED && m_presenceUpdateNeeded) {
                auto now = std::chrono::steady_clock::now();
                auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
                    now - m_lastUpdateTime
                ).count();

                if (elapsed >= MIN_UPDATE_INTERVAL_MS) {
                    std::lock_guard<std::mutex> lock(m_pipeMutex);
                    PresenceSendResult result = sendPresenceUpdate();
                    if (result == PresenceSendResult::Success) {
                        m_lastUpdateTime = now;
                        m_presenceUpdateNeeded = false;
                    } else if (result == PresenceSendResult::PipeError) {
                        // Connection lost - use internal version since we already hold the mutex
                        DEBUG_WARN("DiscordManager: Failed to send presence, disconnecting");
                        disconnectInternal();
                    } else {
                        // SerializationError: same input would just throw again, so don't
                        // hammer Discord with retries and don't drop a healthy connection.
                        // Wait for the next data change to dirty the snapshot before retrying.
                        m_lastUpdateTime = now;
                        m_presenceUpdateNeeded = false;
                    }
                }
            }

            // Sleep to avoid busy-waiting
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }

    } catch (const std::exception& e) {
        DEBUG_WARN_F("DiscordManager thread terminated by exception: %s", e.what());
        // No thread is alive to send presence anymore — reflect that in
        // state so onDataChanged stops queuing snapshot updates.
        m_state = State::DISCONNECTED;
    } catch (...) {
        DEBUG_WARN("DiscordManager thread terminated by unknown exception");
        m_state = State::DISCONNECTED;
    }
}

bool DiscordManager::connect() {
    std::lock_guard<std::mutex> lock(m_pipeMutex);

    // Try connecting to discord-ipc-0 through discord-ipc-9
    char pipeName[64];
    for (int i = 0; i < 10; i++) {
        snprintf(pipeName, sizeof(pipeName), "\\\\.\\pipe\\discord-ipc-%d", i);

        HANDLE pipe = CreateFileA(
            pipeName,
            GENERIC_READ | GENERIC_WRITE,
            0,
            nullptr,
            OPEN_EXISTING,
            0,
            nullptr
        );

        if (pipe != INVALID_HANDLE_VALUE) {
            m_pipe = pipe;

            // Set pipe to message mode
            DWORD mode = PIPE_READMODE_BYTE;
            SetNamedPipeHandleState(pipe, &mode, nullptr, nullptr);

            // Send handshake
            if (sendHandshake()) {
                // Read response
                if (readResponse()) {
                    DEBUG_INFO_F("DiscordManager: Connected on pipe %d", i);
                    return true;
                }
            }

            // Handshake failed, close and try next pipe
            CloseHandle(pipe);
            m_pipe = INVALID_HANDLE_VALUE;
        }
    }

    DEBUG_INFO("DiscordManager: Failed to connect - Discord may not be running");
    return false;
}

void DiscordManager::disconnect() {
    std::lock_guard<std::mutex> lock(m_pipeMutex);
    disconnectInternal();
}

void DiscordManager::disconnectInternal() {
    // Called with m_pipeMutex already held
    if (m_pipe != INVALID_HANDLE_VALUE) {
        CloseHandle((HANDLE)m_pipe);
        m_pipe = INVALID_HANDLE_VALUE;
    }
    m_state = State::DISCONNECTED;
}

bool DiscordManager::sendHandshake() {
    // Handshake payload: {"v":1,"client_id":"..."}
    char payload[256];
    snprintf(payload, sizeof(payload),
        R"({"v":1,"client_id":"%s"})",
        DISCORD_APPLICATION_ID
    );

    return writeFrame(DiscordOpcode::HANDSHAKE, payload, strlen(payload));
}

DiscordManager::PresenceSendResult DiscordManager::sendPresenceUpdate() {
    // nlohmann::json::dump() validates UTF-8 by default and throws
    // json::type_error if it sees invalid bytes. An uncaught throw here would
    // escape the connection thread and trigger CRT terminate() (the
    // "Runtime requested termination" dialog). Catch defensively so a single
    // bad serialization can't kill the whole game process.
    std::string json;
    try {
        json = buildPresenceJson();
    } catch (const std::exception& e) {
        DEBUG_WARN_F("DiscordManager: buildPresenceJson failed: %s", e.what());
        return PresenceSendResult::SerializationError;
    } catch (...) {
        DEBUG_WARN("DiscordManager: buildPresenceJson failed (unknown exception)");
        return PresenceSendResult::SerializationError;
    }
    DEBUG_INFO_F("DiscordManager: Sending presence: %.500s%s",
                 json.c_str(), json.length() > 500 ? "..." : "");

    if (!writeFrame(DiscordOpcode::FRAME, json.c_str(), json.length())) {
        DEBUG_WARN("DiscordManager: Failed to write presence frame");
        return PresenceSendResult::PipeError;
    }

#ifdef _DEBUG
    // Read response to check for errors (debug builds only to avoid latency)
    int opcode;
    std::string response;
    if (readFrame(opcode, response)) {
        DEBUG_INFO_F("DiscordManager: Response (opcode=%d): %.200s%s",
                     opcode, response.c_str(), response.length() > 200 ? "..." : "");
    }
#endif

    return PresenceSendResult::Success;
}

bool DiscordManager::readResponse() {
    int opcode;
    std::string data;

    if (!readFrame(opcode, data)) {
        return false;
    }

    // Check for READY response or error
    // For simplicity, we just check that we got a valid frame back
    return opcode == DiscordOpcode::FRAME;
}

bool DiscordManager::writeFrame(int opcode, const char* data, size_t length) {
    if (m_pipe == INVALID_HANDLE_VALUE) {
        return false;
    }

    // Frame format: opcode (4 bytes LE) + length (4 bytes LE) + data
    uint32_t header[2] = {
        static_cast<uint32_t>(opcode),
        static_cast<uint32_t>(length)
    };

    DWORD bytesWritten;

    // Write header
    if (!WriteFile((HANDLE)m_pipe, header, sizeof(header), &bytesWritten, nullptr) ||
        bytesWritten != sizeof(header)) {
        return false;
    }

    // Write data
    if (length > 0) {
        if (!WriteFile((HANDLE)m_pipe, data, static_cast<DWORD>(length), &bytesWritten, nullptr) ||
            bytesWritten != length) {
            return false;
        }
    }

    return true;
}

bool DiscordManager::readFrame(int& opcode, std::string& data) {
    if (m_pipe == INVALID_HANDLE_VALUE) {
        return false;
    }

    // Read header
    uint32_t header[2];
    DWORD bytesRead;

    if (!ReadFile((HANDLE)m_pipe, header, sizeof(header), &bytesRead, nullptr) ||
        bytesRead != sizeof(header)) {
        return false;
    }

    opcode = static_cast<int>(header[0]);
    uint32_t length = header[1];

    // Sanity check length
    if (length > 65536) {
        return false;
    }

    // Read data
    if (length > 0) {
        data.resize(length);
        if (!ReadFile((HANDLE)m_pipe, &data[0], length, &bytesRead, nullptr) ||
            bytesRead != length) {
            return false;
        }
    } else {
        data.clear();
    }

    return true;
}

std::string DiscordManager::buildPresenceJson() const {
    using json = nlohmann::json;

    // Snapshot all fields under the lock, then build JSON without holding it.
    // The snapshot is the authoritative source - never read PluginData directly
    // from this thread (it has no thread-safety, and the game thread can mutate
    // session.serverName / session.trackName etc. mid-read).
    SessionSnapshot session;
    {
        std::lock_guard<std::mutex> lock(m_snapshotMutex);
        session = m_snapshot;
    }

    // Build activity details based on session state
    std::string details;
    std::string state;
    std::string largeImageKey = "mxbikes_logo";  // Default, can be customized in Discord app
    std::string largeImageText = GAME_NAME;

    // Determine session type
    bool hasTrack = session.trackName[0] != '\0';
    int drawState = session.drawState;  // 0=ON_TRACK, 1=SPECTATE, 2=REPLAY

    // Check if we're in menus (no event loaded)
    // Layout: Details (line 1) = track + session info, State (line 2) = server name
    if (!hasTrack) {
        details = "In Menus";
        // No state line needed for menus
    } else if (drawState == 2) {
        // Replay mode - just show track and replay indicator
        details = session.trackName;
        state = "Watching Replay";
    } else {
        // In event (riding or spectating) - show full session info
        details = session.trackName;

        // Build session string (e.g., "Race 1", "Qualify", "Practice")
        // Check for valid session data (session=-1 and sessionState=-1 are uninitialized defaults)
        bool hasValidSession = (session.session >= 0);
        bool hasValidState = (session.sessionState >= 0);

        const char* sessionStr = hasValidSession ?
            PluginUtils::getSessionString(session.eventType, session.session) : nullptr;
        const char* stateStr = hasValidState ?
            PluginUtils::getSessionStateString(session.sessionState) : nullptr;

        // Build session format string (time/laps)
        bool hasTime = (session.sessionLength > 0);
        bool hasLaps = (session.sessionNumLaps > 0);
        std::string formatStr;

        if (hasTime || hasLaps) {
            char formatBuf[32];
            if (hasTime && hasLaps) {
                int mins = session.sessionLength / 60000;
                int secs = (session.sessionLength / 1000) % 60;
                snprintf(formatBuf, sizeof(formatBuf), "%d:%02d + %dL", mins, secs, session.sessionNumLaps);
            } else if (hasTime) {
                int mins = session.sessionLength / 60000;
                int secs = (session.sessionLength / 1000) % 60;
                snprintf(formatBuf, sizeof(formatBuf), "%d:%02d", mins, secs);
            } else {
                snprintf(formatBuf, sizeof(formatBuf), "%d Laps", session.sessionNumLaps);
            }
            formatStr = formatBuf;
        }

        // Build details line: "Track · Session (Format, State)"
        // Format: session type first, then format and state in parentheses
        if (sessionStr) {
            details += " \xC2\xB7 ";
            details += sessionStr;

            // Build parenthetical info: (Format, State)
            std::string parenInfo;
            if (!formatStr.empty()) {
                parenInfo = formatStr;
            }
            // Add state if different from session name (avoid "Waiting (Waiting)")
            if (stateStr && strcmp(sessionStr, stateStr) != 0) {
                if (!parenInfo.empty()) {
                    parenInfo += ", ";
                }
                parenInfo += stateStr;
            }
            if (!parenInfo.empty()) {
                details += " (" + parenInfo + ")";
            }
        }

        bool hasServerName = session.serverName[0] != '\0';

        // Mirrors SessionData::isOnline() / isOffline() helpers (which we can't
        // call here because the snapshot stores a plain int instead of a
        // SessionData reference).
        bool isOnline  = (session.serverType > 0);
        bool isOffline = (session.serverType == 0);

        if (isOnline && hasServerName) {
            // Online: state line shows server name (truncated to fit display)
            // Discord displays ~40 chars before truncating visually
            constexpr size_t MAX_SERVER_NAME_DISPLAY = 40;
            state = session.serverName;
            if (state.length() > MAX_SERVER_NAME_DISPLAY) {
                state = state.substr(0, MAX_SERVER_NAME_DISPLAY - 3) + "...";
            }
        } else if (isOffline && session.eventType == 1) {
            // Offline Testing: show "Testing" on state line (no party info)
            state = "Testing";
        }
    }

    // Get current Unix timestamp
    long long nowUnix = std::chrono::duration_cast<std::chrono::seconds>(
        std::chrono::system_clock::now().time_since_epoch()
    ).count();

    // Determine timestamp mode based on session type
    // - Timed sessions (sessionLength > 0): countdown using "end" timestamp
    // - Lap-based sessions: countup using "start" timestamp
    bool usesCountdown = (session.sessionLength > 0);
    int sessionTimeMs = session.sessionTimeMs;

    // Build the SET_ACTIVITY command using nlohmann::json
    json activity;

    // Details (main text line)
    if (!details.empty()) {
        activity["details"] = details;
    }

    // State (secondary text line)
    if (!state.empty()) {
        activity["state"] = state;
    }

    // Timestamps - only when in an active session (hasTrack)
    // Use game's session time for accurate countdown/countup
    if (hasTrack) {
        if (usesCountdown && sessionTimeMs > 0) {
            // Timed session with time remaining: show countdown
            activity["timestamps"]["end"] = nowUnix + (sessionTimeMs / 1000);
        } else if (!usesCountdown && sessionTimeMs >= 0) {
            // Lap-based session: show elapsed time (countup)
            activity["timestamps"]["start"] = nowUnix - (sessionTimeMs / 1000);
        }
        // If timed session with sessionTimeMs <= 0, we're in overtime - no timer shown
    }

    // Assets (images)
    activity["assets"]["large_image"] = largeImageKey;
    activity["assets"]["large_text"] = largeImageText;

    // Build the full command
    json command;
    command["cmd"] = "SET_ACTIVITY";
    command["args"]["pid"] = static_cast<int>(GetCurrentProcessId());
    command["args"]["activity"] = activity;
    command["nonce"] = std::to_string(++m_nonce);

    return command.dump();
}

void DiscordManager::update() {
    // This is called from the main thread periodically
    // The actual work is done in the background thread
    // We just mark that an update is needed
    if (m_enabled && m_state == State::CONNECTED) {
        m_presenceUpdateNeeded = true;
    }
}

void DiscordManager::onDataChanged(DataChangeType changeType) {
    // Queue presence update for relevant data changes
    switch (changeType) {
        case DataChangeType::SessionData:
        case DataChangeType::Standings:
        case DataChangeType::SpectateTarget:
            // Refresh the snapshot on the game thread so the connection
            // thread can build presence JSON without racing against
            // PluginData mutations (no internal locking there).
            updateSnapshot();
            m_presenceUpdateNeeded = true;
            break;
        default:
            // Ignore high-frequency updates like telemetry
            break;
    }
}

void DiscordManager::updateSnapshot() {
    // Read PluginData on the calling (game) thread, then publish a copy
    // under m_snapshotMutex. The connection thread reads only the snapshot.
    const PluginData& pd = PluginData::getInstance();
    const SessionData& session = pd.getSessionData();

    SessionSnapshot next;
    // strncpy_s with explicit null termination, mirrors PluginData::setStringValue.
    strncpy_s(next.trackName,  sizeof(next.trackName),  session.trackName,  sizeof(next.trackName)  - 1);
    next.trackName[sizeof(next.trackName) - 1]   = '\0';
    strncpy_s(next.serverName, sizeof(next.serverName), session.serverName, sizeof(next.serverName) - 1);
    next.serverName[sizeof(next.serverName) - 1] = '\0';
    next.session        = session.session;
    next.sessionState   = session.sessionState;
    next.sessionLength  = session.sessionLength;
    next.sessionNumLaps = session.sessionNumLaps;
    next.eventType      = session.eventType;
    next.serverType     = session.serverType;
    next.sessionTimeMs  = pd.getSessionTime();
    next.drawState      = pd.getDrawState();

    std::lock_guard<std::mutex> lock(m_snapshotMutex);
    m_snapshot = next;
}

void DiscordManager::onEventEnd() {
    // Force immediate update to show "In Menus"
    m_lastUpdateTime = std::chrono::steady_clock::time_point{};  // Allow immediate update
    m_presenceUpdateNeeded = true;
}

void DiscordManager::setEnabled(bool enabled) {
    bool wasEnabled = m_enabled.exchange(enabled);

    if (enabled && !wasEnabled) {
        // Force reconnection attempt
        m_lastConnectionAttempt = std::chrono::steady_clock::time_point{};

        // Start connection thread if not already running
        // (happens when Discord was disabled at startup)
        if (!m_connectionThread.joinable()) {
            m_shutdownRequested = false;
            // Prime snapshot here too (game thread) for the same reason as
            // initialize(): the first presence frame after enabling needs a
            // current snapshot, not the default one.
            updateSnapshot();
            m_connectionThread = std::thread(&DiscordManager::connectionThread, this);
            DEBUG_INFO("DiscordManager: Started connection thread (was disabled at startup)");
        }
    }
    // Note: disconnect is handled by connection thread when it sees m_enabled=false
}

const char* DiscordManager::getStateString() const {
    switch (m_state.load()) {
        case State::DISCONNECTED: return "Disconnected";
        case State::CONNECTING:   return "Connecting...";
        case State::CONNECTED:    return "Connected";
        case State::FAILED:       return "Not Available";
        default:                  return "Unknown";
    }
}
