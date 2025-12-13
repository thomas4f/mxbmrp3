// ============================================================================
// core/update_checker.h
// Checks GitHub for plugin updates
// ============================================================================
#pragma once

#include <string>
#include <thread>
#include <mutex>
#include <atomic>
#include <functional>

class UpdateChecker {
public:
    enum class Status {
        IDLE,             // Not checked yet
        CHECKING,         // Currently checking
        UP_TO_DATE,       // Current version is latest
        UPDATE_AVAILABLE, // Newer version available
        CHECK_FAILED      // Network/parse error
    };

    // Singleton access
    static UpdateChecker& getInstance();

    // Trigger an update check (runs asynchronously)
    void checkForUpdates();

    // Enable/disable update checking (persisted setting)
    bool isEnabled() const { return m_enabled; }
    void setEnabled(bool enabled) { m_enabled = enabled; }

    // Get current status
    Status getStatus() const { return m_status; }

    // Get latest version string (only valid when UPDATE_AVAILABLE)
    std::string getLatestVersion() const;

    // Check if currently checking
    bool isChecking() const { return m_status == Status::CHECKING; }

    // Set callback for when check completes (called from worker thread!)
    void setCompletionCallback(std::function<void()> callback);

    // Cleanup (call before shutdown)
    void shutdown();

private:
    UpdateChecker();
    ~UpdateChecker();
    UpdateChecker(const UpdateChecker&) = delete;
    UpdateChecker& operator=(const UpdateChecker&) = delete;

    // Worker thread function
    void workerThread();

    // Parse version string to comparable integers (e.g., "1.6.6.0" -> {1,6,6,0})
    static bool parseVersion(const std::string& version, int& major, int& minor, int& patch, int& build);

    // Compare two versions, returns: -1 (a < b), 0 (a == b), 1 (a > b)
    static int compareVersions(const std::string& a, const std::string& b);

    // HTTP fetch (blocking)
    bool fetchLatestRelease(std::string& outVersion, std::string& outError);

    std::atomic<Status> m_status;
    std::atomic<bool> m_enabled;  // Persisted setting: check for updates on load
    std::string m_latestVersion;
    mutable std::mutex m_mutex;
    std::thread m_workerThread;
    std::atomic<bool> m_shutdownRequested;
    std::function<void()> m_completionCallback;

    // GitHub API endpoint
    static constexpr const char* GITHUB_API_HOST = "api.github.com";
    static constexpr const char* GITHUB_RELEASES_PATH = "/repos/thomas4f/mxbmrp3/releases/latest";
};
