// ============================================================================
// core/http_server.h
// Embedded HTTP server for serving race data to external tools (OBS, etc.)
// Runs on a background thread, pushes JSON via Server-Sent Events (SSE)
// ============================================================================
#pragma once

#include <string>
#include <memory>
#include <mutex>
#include <atomic>
#include <thread>
#include <condition_variable>
#include <cstdint>

// Forward declarations
enum class DataChangeType;
namespace httplib { class Server; }

class HttpServer {
public:
    static HttpServer& getInstance();

    // Lifecycle (called by PluginManager)
    void initialize(const char* savePath);
    void shutdown();

    // Start/stop server (called when enabled/disabled)
    void start();
    void stop();

    // Notification from PluginData when relevant data changes.
    // Called on the game thread - builds JSON snapshot here (thread-safe)
    // so server threads only read an immutable cached string.
    void onDataChanged(DataChangeType changeType);

    // Settings
    void setEnabled(bool enabled);
    bool isEnabled() const { return m_enabled; }

    // Port and throttle changes take effect on next server restart.
    void setPort(int port);
    int getPort() const { return m_port; }
    void resetPortToDefault() { m_port = DEFAULT_PORT; }
    void setThrottleMs(int ms);
    int getThrottleMs() const { return m_throttleMs; }
    void resetThrottleToDefault() { setThrottleMs(DEFAULT_THROTTLE_MS); }
    void setBindAddress(const std::string& addr);
    std::string getBindAddress() const;
    void resetBindAddressToDefault() { setBindAddress(DEFAULT_BIND_ADDRESS); }

    // Status
    bool isRunning() const { return m_running; }

private:
    HttpServer();
    ~HttpServer();
    HttpServer(const HttpServer&) = delete;
    HttpServer& operator=(const HttpServer&) = delete;

    // Build JSON snapshot from current PluginData state.
    // Must only be called from the game thread (PluginData is not thread-safe).
    std::string buildJsonSnapshot() const;

    // Server thread entry point
    void serverThread();

    // State
    std::atomic<bool> m_enabled;
    std::atomic<bool> m_initialized;
    std::atomic<bool> m_running;
    std::atomic<bool> m_shutdownRequested;
    std::atomic<int> m_port;
    std::atomic<int> m_throttleMs;
    std::string m_bindAddress;
    mutable std::mutex m_bindMutex;  // Protects m_bindAddress (std::string not atomic)
    std::string m_savePath;
    std::string m_webRoot;  // Path to web/ directory serving static files

    // Server thread and httplib instance
    std::thread m_serverThread;
    std::unique_ptr<httplib::Server> m_server;

    // Cached JSON snapshot (written by game thread, read by server threads)
    std::mutex m_dataMutex;
    std::condition_variable m_dataCondition;
    uint64_t m_sseSequence;                 // Incrementing SSE event ID (per-client tracking)
    std::string m_cachedJson;

    // SSE connection tracking
    std::atomic<int> m_sseConnections{0};
    static constexpr int MAX_SSE_CONNECTIONS = 3;  // Reserve 1 thread for REST

    // Default settings
    static constexpr const char* DEFAULT_BIND_ADDRESS = "127.0.0.1";
    static constexpr int DEFAULT_PORT = 8080;
    static constexpr int DEFAULT_THROTTLE_MS = 250;
    static constexpr int MIN_THROTTLE_MS = 50;
    static constexpr int MAX_THROTTLE_MS = 5000;
    static constexpr int MIN_PORT = 1024;
    static constexpr int MAX_PORT = 65535;
};
