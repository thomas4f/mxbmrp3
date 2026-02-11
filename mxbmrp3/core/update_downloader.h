// ============================================================================
// core/update_downloader.h
// Downloads and installs plugin updates
// ============================================================================
#pragma once

#include <string>
#include <thread>
#include <mutex>
#include <atomic>
#include <functional>
#include <vector>

class UpdateDownloader {
public:
    enum class State {
        IDLE,           // Not downloading
        DOWNLOADING,    // Downloading ZIP file
        VERIFYING,      // Checking file integrity
        EXTRACTING,     // Extracting ZIP contents
        READY,          // Update staged, restart needed
        FAILED          // Error occurred
    };

    // Installation steps for progress display
    enum class Step {
        DOWNLOAD,
        VERIFY,
        BACKUP,
        EXTRACT,
        INSTALL,
        STEP_COUNT  // Must be last
    };

    enum class StepStatus {
        PENDING,
        IN_PROGRESS,
        COMPLETE,
        SKIPPED
    };

    struct StepInfo {
        const char* label;
        StepStatus status;
    };

    // Singleton access
    static UpdateDownloader& getInstance();

    // Start download from URL (runs asynchronously)
    // expectedSize is used for progress calculation and verification
    // checksumHash is optional - if provided, SHA256 will be verified
    void startDownload(const std::string& url, size_t expectedSize,
                       const std::string& checksumHash = "");

    // Cancel in-progress download
    void cancelDownload();

    // Get current state
    State getState() const { return m_state; }

    // Get download progress (0.0 - 1.0)
    float getProgress() const;

    // Get human-readable status text
    std::string getStatusText() const;

    // Get step-by-step progress info
    std::vector<StepInfo> getSteps() const;

    // Get error message (only valid when FAILED)
    std::string getErrorMessage() const;

    // Check if restart is pending
    bool isRestartPending() const { return m_state == State::READY; }

    // Set callback for state changes (called from worker thread!)
    void setStateChangeCallback(std::function<void()> callback);

    // Cleanup (call before shutdown)
    void shutdown();

    // Reset to IDLE state (e.g., after user dismisses error)
    void reset();

    // Debug mode: extracts to mxbmrp3_update_test/ subdirectory (for testing)
    void setDebugMode(bool enabled) { m_debugMode = enabled; }
    bool isDebugMode() const { return m_debugMode; }

    // Cleanup old .dlo files from previous updates (call on startup)
    void cleanupOldFiles();

private:
    UpdateDownloader();
    ~UpdateDownloader();
    UpdateDownloader(const UpdateDownloader&) = delete;
    UpdateDownloader& operator=(const UpdateDownloader&) = delete;

    // Worker thread function
    void workerThread();

    // Download file from URL to memory (redirectDepth limits recursive redirects to prevent infinite loops)
    bool downloadFile(std::vector<char>& outData, std::string& outError, int redirectDepth = 0);
    static constexpr int MAX_REDIRECTS = 5;  // Limit redirect depth to prevent infinite loops

    // Extract ZIP from memory to plugin directory
    bool extractAndInstall(const std::vector<char>& zipData, std::string& outError);

    // Get plugin directory path
    std::string getPluginDirectory() const;

    // Backup/restore helpers for atomic updates
    bool createBackupDirectory(const std::string& backupDir);
    bool backupExistingFiles(const std::string& pluginDir, const std::string& backupDir);
    bool restoreFromBackup(const std::string& pluginDir, const std::string& backupDir,
                           const std::vector<std::string>& extractedFiles);
    void cleanupBackup(const std::string& backupDir);
    void cleanupExtractedFiles(const std::string& pluginDir, const std::vector<std::string>& files);
    bool verifyExtractedFiles(const std::string& pluginDir,
                              const std::vector<std::pair<std::string, size_t>>& expectedFiles);

    // Map ZIP filename to install path (handles .dlo vs data files)
    std::string mapToInstallPath(const std::string& zipFilename) const;

    // Notify state change
    void notifyStateChange();

    // Calculate SHA256 hash of data
    std::string calculateSHA256(const std::vector<char>& data);

    // Step tracking helpers
    void resetSteps();
    void setStepStatus(Step step, StepStatus status);

    // Safely close all active HTTP handles (called from both shutdown and worker thread).
    // The mutex ensures exactly one caller closes the handles; the other gets nulls.
    void closeHttpHandles();

    std::atomic<State> m_state;
    std::atomic<bool> m_cancelRequested;
    std::atomic<bool> m_shutdownRequested;

    // Download parameters
    std::string m_downloadUrl;
    std::string m_checksumHash;
    size_t m_expectedSize;

    // Progress tracking
    std::atomic<size_t> m_bytesDownloaded;
    std::atomic<size_t> m_totalBytes;

    // Step-by-step progress tracking
    StepStatus m_stepStatus[static_cast<int>(Step::STEP_COUNT)];

    // Error message
    std::string m_errorMessage;

    // Threading
    mutable std::mutex m_mutex;
    std::thread m_workerThread;
    std::function<void()> m_stateChangeCallback;

    // HTTP handle tracking for cross-thread cancellation via WinHttpCloseHandle.
    // All three handles are stored so shutdown can close them explicitly rather than
    // relying on ambiguous cascade-close behavior when only the session is closed.
    std::mutex m_httpHandleMutex;
    void* m_hHttpSession{nullptr};
    void* m_hHttpConnect{nullptr};
    void* m_hHttpRequest{nullptr};

    // Plugin path (cached)
    std::string m_pluginPath;

    // Debug mode: extract to test subdirectory
    std::atomic<bool> m_debugMode;
};
