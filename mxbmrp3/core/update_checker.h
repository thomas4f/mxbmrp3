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

    enum class UpdateMode {
        OFF,      // Don't check for updates
        NOTIFY    // Check and notify user (manual install via Install button)
    };

    enum class UpdateChannel {
        STABLE,      // Only stable releases (prerelease=false)
        PRERELEASE   // All releases including prereleases
    };

    // Singleton access
    static UpdateChecker& getInstance();

    // Trigger an update check (runs asynchronously)
    void checkForUpdates();

    // Update mode setting (persisted)
    UpdateMode getMode() const { return m_mode; }
    void setMode(UpdateMode mode) { m_mode = mode; }

    // Legacy compatibility - maps to mode != OFF
    bool isEnabled() const { return m_mode != UpdateMode::OFF; }
    void setEnabled(bool enabled) { m_mode = enabled ? UpdateMode::NOTIFY : UpdateMode::OFF; }

    // Update channel setting (persisted) - determines whether prereleases are shown
    UpdateChannel getChannel() const { return m_channel.load(); }
    void setChannel(UpdateChannel channel);  // Clears dismissed version on change
    bool isPrereleaseChannel() const { return m_channel.load() == UpdateChannel::PRERELEASE; }

    // Check if the latest available version is a prerelease
    bool isLatestPrerelease() const;

    // Get current status
    Status getStatus() const { return m_status; }

    // Get latest version string (only valid when UPDATE_AVAILABLE)
    std::string getLatestVersion() const;

    // Get release notes (markdown, only valid when UPDATE_AVAILABLE)
    std::string getReleaseNotes() const;

    // Get download URL for the release asset (only valid when UPDATE_AVAILABLE)
    std::string getDownloadUrl() const;

    // Get expected download size in bytes (only valid when UPDATE_AVAILABLE)
    size_t getDownloadSize() const;

    // Get asset filename (only valid when UPDATE_AVAILABLE)
    std::string getAssetName() const;

    // Get SHA256 checksum hash (only valid when UPDATE_AVAILABLE, may be empty)
    std::string getChecksumHash() const;

    // Dismissed version tracking (user chose to skip this version)
    void setDismissedVersion(const std::string& version);
    std::string getDismissedVersion() const;

    // Check if update notification should be shown (update available AND not dismissed)
    bool shouldShowUpdateNotification() const;

    // Check if currently checking
    bool isChecking() const { return m_status == Status::CHECKING; }

    // Check if on cooldown (prevent spam)
    bool isOnCooldown() const;

    // Set callback for when check completes (called from worker thread!)
    void setCompletionCallback(std::function<void()> callback);

    // Cleanup (call before shutdown)
    void shutdown();

    // Debug mode: forces update to appear available (for testing)
    void setDebugMode(bool enabled) { m_debugMode = enabled; }
    bool isDebugMode() const { return m_debugMode; }

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
    std::atomic<UpdateMode> m_mode;  // Persisted setting: Off or Notify
    std::atomic<UpdateChannel> m_channel;  // Persisted setting: Stable or Prerelease
    std::string m_latestVersion;
    std::string m_releaseNotes;   // GitHub release body (markdown)
    std::string m_downloadUrl;    // Asset browser_download_url
    std::string m_assetName;      // Asset filename (e.g., "mxbmrp3-v1.10.3.0.zip")
    size_t m_downloadSize;        // Asset size in bytes
    std::string m_checksumHash;   // SHA256 hash from GitHub digest field
    bool m_latestIsPrerelease;    // Whether the latest available version is a prerelease
    std::string m_dismissedVersion;  // Version user chose to skip (persisted)
    mutable std::mutex m_mutex;
    std::thread m_workerThread;
    std::atomic<bool> m_shutdownRequested;
    std::function<void()> m_completionCallback;
    std::atomic<bool> m_debugMode;  // Forces update available for testing
    unsigned long m_lastCheckTimestamp;  // When last check started (for cooldown)

    // GitHub API endpoint (host is always api.github.com, path uses constants from plugin_constants.h)
    static constexpr const char* GITHUB_API_HOST = "api.github.com";
    static constexpr unsigned long CHECK_COOLDOWN_MS = 5000;  // Minimum time between checks (prevent spam)
    // Note: GITHUB_RELEASES_PATH is constructed at runtime using PluginConstants::GITHUB_REPO_OWNER/NAME
};
