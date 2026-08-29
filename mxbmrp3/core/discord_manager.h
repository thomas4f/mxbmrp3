// ============================================================================
// core/discord_manager.h
// Discord Rich Presence integration using Discord IPC protocol
// ============================================================================
#pragma once

#include <string>
#include <chrono>
#include <atomic>
#include <thread>
#include <mutex>
#include "thread_safety.h"

// Forward declarations
enum class DataChangeType;

class DiscordManager {
public:
    // Connection state
    enum class State {
        DISCONNECTED,   // Not connected to Discord
        CONNECTING,     // Connection attempt in progress
        CONNECTED,      // Connected and ready
        FAILED          // Connection failed (Discord not running, etc.)
    };

    // Singleton access
    static DiscordManager& getInstance();

    // Lifecycle management (called by PluginManager)
    void initialize();
    void shutdown();

    // Update presence based on game state
    // Called periodically from draw loop or on data change
    void update();

    // Notification from PluginData when relevant data changes
    void onDataChanged(DataChangeType changeType);

    // Called when event ends to update presence to "In Menus"
    void onEventEnd();

    // Settings
    void setEnabled(bool enabled);
    bool isEnabled() const { return m_enabled; }

    // Status
    State getState() const { return m_state; }
    bool isConnected() const { return m_state == State::CONNECTED; }
    const char* getStateString() const;

private:
    DiscordManager();
    ~DiscordManager();
    DiscordManager(const DiscordManager&) = delete;
    DiscordManager& operator=(const DiscordManager&) = delete;

    // IPC communication
    bool connect();
    void disconnect();
    // The pipe-I/O helper family below runs under m_pipeMutex — acquired at the
    // three entry points (connectionThread / connect / disconnect) and required
    // by annotation here, so an unlocked caller fails the thread-safety check.
    void disconnectInternal() MXB_REQUIRES(m_pipeMutex);
    bool sendHandshake() MXB_REQUIRES(m_pipeMutex);
    // Outcome of one presence-send attempt. Distinguishes a transient pipe
    // failure (worth dropping the connection over) from a serialization error
    // (same input would just throw again - skip and wait for new data).
    enum class PresenceSendResult {
        Success,
        SerializationError,
        PipeError
    };
    PresenceSendResult sendPresenceUpdate() MXB_REQUIRES(m_pipeMutex);
    bool readResponse() MXB_REQUIRES(m_pipeMutex);

    // Frame handling (Discord IPC protocol)
    bool writeFrame(int opcode, const char* data, size_t length) MXB_REQUIRES(m_pipeMutex);
    bool readFrame(int& opcode, std::string& data) MXB_REQUIRES(m_pipeMutex);

    // JSON building helper (uses nlohmann::json).
    // Reads only from m_snapshot, never PluginData - safe to call from the
    // connection thread because the snapshot is mutex-protected.
    std::string buildPresenceJson() const;

    // Refresh m_snapshot from PluginData. MUST be called on the game thread
    // only (PluginData has no internal thread-safety).
    void updateSnapshot();

    // Background connection thread
    void connectionThread();

    // State
    std::atomic<bool> m_enabled;
    std::atomic<State> m_state;
    std::atomic<bool> m_presenceUpdateNeeded;
    std::atomic<bool> m_shutdownRequested;

    // Named pipe handle
    void* m_pipe MXB_GUARDED_BY(m_pipeMutex);  // HANDLE, but we avoid windows.h in header

    // Timing. The first two are atomic: the game thread resets them
    // (onEventEnd / setEnabled) while the connection thread reads and writes
    // them. m_lastPresenceRefresh is connection-thread only.
    std::atomic<std::chrono::steady_clock::time_point> m_lastUpdateTime;
    std::atomic<std::chrono::steady_clock::time_point> m_lastConnectionAttempt;
    std::chrono::steady_clock::time_point m_lastPresenceRefresh;

    // Update throttling
    static constexpr int MIN_UPDATE_INTERVAL_MS = 5000;   // Minimum 5 seconds between updates
    static constexpr int RECONNECT_INTERVAL_MS = 15000;   // Retry connection every 15 seconds
    static constexpr int PRESENCE_REFRESH_INTERVAL_MS = 30000;  // Force refresh every 30 seconds to detect disconnection

    // Background thread for non-blocking connection
    std::thread m_connectionThread;   // joined-by: shutdown() (PluginManager::shutdown)
    Mutex m_pipeMutex;

    // Nonce for Discord IPC messages (mutable for use in const buildPresenceJson)
    mutable std::atomic<int> m_nonce;

    // Game-thread snapshot of the SessionData fields the connection thread
    // needs. PluginData itself has no thread-safety; reading it directly from
    // the background thread races against game-thread setters that mutate
    // fixed-size char arrays like serverName / trackName. The game thread
    // refreshes this snapshot from onDataChanged(); the connection thread
    // reads it under m_snapshotMutex from buildPresenceJson().
    struct SessionSnapshot {
        char trackName[100]   = {};
        char serverName[100]  = {};
        int  session          = -1;
        int  sessionState     = -1;
        int  sessionLength    = 0;
        int  sessionNumLaps   = 0;
        int  eventType        = 2;   // Default Race (matches SessionData::eventType default)
        int  serverType       = -1;  // Unknown
        int  riderCount       = 0;   // session rider count; >1 => online when serverType is unknown
        int  sessionTimeMs    = 0;
        long long sessionEndUnix = 0;  // absolute countdown end (wall-clock secs); 0 = none
        int  drawState        = 0;   // ON_TRACK
        int  sessionGeneration = 0;  // new-session counter (first-send-per-session Release log)
    };
    mutable Mutex m_snapshotMutex;
    SessionSnapshot m_snapshot MXB_GUARDED_BY(m_snapshotMutex);
    int m_lastLoggedSessionGen = -1;  // connection-thread only: session gen of last logged send
};
