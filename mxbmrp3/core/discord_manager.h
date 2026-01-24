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
    void disconnectInternal();  // Called with m_pipeMutex already held
    bool sendHandshake();
    bool sendPresenceUpdate();
    bool readResponse();

    // Frame handling (Discord IPC protocol)
    bool writeFrame(int opcode, const char* data, size_t length);
    bool readFrame(int& opcode, std::string& data);

    // JSON building helper (uses nlohmann::json)
    std::string buildPresenceJson() const;

    // Background connection thread
    void connectionThread();

    // State
    std::atomic<bool> m_enabled;
    std::atomic<State> m_state;
    std::atomic<bool> m_presenceUpdateNeeded;
    std::atomic<bool> m_shutdownRequested;

    // Named pipe handle
    void* m_pipe;  // HANDLE, but we avoid windows.h in header

    // Timing
    std::chrono::steady_clock::time_point m_lastUpdateTime;
    std::chrono::steady_clock::time_point m_lastConnectionAttempt;
    std::chrono::steady_clock::time_point m_lastPresenceRefresh;

    // Update throttling
    static constexpr int MIN_UPDATE_INTERVAL_MS = 5000;   // Minimum 5 seconds between updates
    static constexpr int RECONNECT_INTERVAL_MS = 15000;   // Retry connection every 15 seconds
    static constexpr int PRESENCE_REFRESH_INTERVAL_MS = 30000;  // Force refresh every 30 seconds to detect disconnection

    // Background thread for non-blocking connection
    std::thread m_connectionThread;
    std::mutex m_pipeMutex;

    // Nonce for Discord IPC messages (mutable for use in const buildPresenceJson)
    mutable std::atomic<int> m_nonce;
};
