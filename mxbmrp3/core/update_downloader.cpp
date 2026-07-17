// ============================================================================
// core/update_downloader.cpp
// Downloads and installs plugin updates
// ============================================================================
#include "update_downloader.h"
#include "atomic_file_writer.h"
#include "plugin_constants.h"
#include "plugin_manager.h"
#include "update_checker.h"
#include "../game/game_config.h"
#include "../diagnostics/logger.h"

// Windows HTTP and file operations
#include <windows.h>
#include <winhttp.h>
#include <bcrypt.h>
#pragma comment(lib, "winhttp.lib")
#pragma comment(lib, "bcrypt.lib")

// ZIP extraction
#include "../vendor/miniz/miniz.h"

#include <cstring>
#include <fstream>
#include <sstream>
#include <algorithm>
#include <iomanip>
#include <cctype>

UpdateDownloader& UpdateDownloader::getInstance() {
    static UpdateDownloader instance;
    return instance;
}

UpdateDownloader::UpdateDownloader()
    : m_state(State::IDLE)
    , m_cancelRequested(false)
    , m_shutdownRequested(false)
    , m_expectedSize(0)
    , m_bytesDownloaded(0)
    , m_totalBytes(0)
    , m_debugMode(false)
{
    // Initialize step status
    for (int i = 0; i < static_cast<int>(Step::STEP_COUNT); i++) {
        m_stepStatus[i] = StepStatus::PENDING;
    }
}

UpdateDownloader::~UpdateDownloader() {
    shutdown();
}

void UpdateDownloader::shutdown() {
    m_shutdownRequested = true;
    m_cancelRequested = true;

    // Cancel any in-flight HTTP request by closing all WinHTTP handles.
    // CancelSynchronousIo does NOT work with WinHTTP (it uses internal waits,
    // not kernel I/O). Closing the handles causes pending WinHTTP calls to fail
    // immediately. The mutex in closeHttpHandles() prevents double-close with
    // the worker thread's cleanup.
    closeHttpHandles();

    if (m_workerThread.joinable()) {
        m_workerThread.join();
    }

    // Drop the state-change callback: it captures a SettingsHud pointer that
    // dangles once HudManager shuts down after us. The worker is joined, so
    // nothing can fire it now - and a future late download must not call
    // into destroyed HUDs.
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_stateChangeCallback = nullptr;
    }
}

void UpdateDownloader::closeHttpHandles() {
    HINTERNET r;
    HINTERNET c;
    HINTERNET s;
    {
        std::lock_guard<std::mutex> lock(m_httpHandleMutex);
        r = static_cast<HINTERNET>(m_hHttpRequest);  m_hHttpRequest = nullptr;
        c = static_cast<HINTERNET>(m_hHttpConnect);   m_hHttpConnect = nullptr;
        s = static_cast<HINTERNET>(m_hHttpSession);   m_hHttpSession = nullptr;
    }
    // Close child handles before parent (WinHTTP documented best practice)
    if (r) WinHttpCloseHandle(r);
    if (c) WinHttpCloseHandle(c);
    if (s) WinHttpCloseHandle(s);
}

void UpdateDownloader::reset() {
    if (m_state == State::DOWNLOADING || m_state == State::VERIFYING || m_state == State::EXTRACTING) {
        return;  // Don't reset while in progress
    }
    m_state = State::IDLE;
    m_bytesDownloaded = 0;
    m_totalBytes = 0;
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_errorMessage.clear();
        for (int i = 0; i < static_cast<int>(Step::STEP_COUNT); i++) {
            m_stepStatus[i] = StepStatus::PENDING;
        }
    }
}

void UpdateDownloader::startDownload(const std::string& url, size_t expectedSize,
                                     const std::string& checksumHash) {
    // Don't start if already in progress
    if (m_state == State::DOWNLOADING || m_state == State::VERIFYING || m_state == State::EXTRACTING) {
        return;
    }

    // Wait for any previous thread to complete
    if (m_workerThread.joinable()) {
        m_workerThread.join();
    }

    // Store parameters and reset step tracking
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_downloadUrl = url;
        m_checksumHash = checksumHash;
        m_expectedSize = expectedSize;
        m_errorMessage.clear();
        m_pluginPath = getPluginDirectory();

        // Reset all steps to pending
        for (int i = 0; i < static_cast<int>(Step::STEP_COUNT); i++) {
            m_stepStatus[i] = StepStatus::PENDING;
        }
        // Mark download as in progress
        m_stepStatus[static_cast<int>(Step::DOWNLOAD)] = StepStatus::IN_PROGRESS;
    }

    m_bytesDownloaded = 0;
    m_totalBytes = expectedSize;
    m_cancelRequested = false;
    m_shutdownRequested = false;
    m_state = State::DOWNLOADING;

    // Start worker thread
    m_workerThread = std::thread(&UpdateDownloader::workerThread, this);
}

void UpdateDownloader::cancelDownload() {
    m_cancelRequested = true;
}

float UpdateDownloader::getProgress() const {
    size_t total = m_totalBytes.load();
    if (total == 0) return 0.0f;
    size_t downloaded = m_bytesDownloaded.load();
    return static_cast<float>(downloaded) / static_cast<float>(total);
}

std::string UpdateDownloader::getStatusText() const {
    switch (m_state.load()) {
        case State::IDLE:
            return "";
        case State::DOWNLOADING: {
            size_t downloaded = m_bytesDownloaded.load();
            size_t total = m_totalBytes.load();
            float progress = total > 0 ? (100.0f * downloaded / total) : 0.0f;
            char buf[64];
            if (total > 1024 * 1024) {
                snprintf(buf, sizeof(buf), "Downloading (%.0f%%) %.1f/%.1f MB",
                        progress, downloaded / (1024.0f * 1024.0f), total / (1024.0f * 1024.0f));
            } else {
                snprintf(buf, sizeof(buf), "Downloading (%.0f%%) %.0f/%.0f KB",
                        progress, downloaded / 1024.0f, total / 1024.0f);
            }
            return buf;
        }
        case State::VERIFYING:
            return "Verifying integrity...";
        case State::EXTRACTING:
            return "Extracting files...";
        case State::READY:
            return "Update installed! Restart " GAME_NAME " to apply.";
        case State::FAILED: {
            std::lock_guard<std::mutex> lock(m_mutex);
            return "Failed: " + m_errorMessage;
        }
    }
    return "";
}

std::string UpdateDownloader::getErrorMessage() const {
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_errorMessage;
}

void UpdateDownloader::setStateChangeCallback(std::function<void()> callback) {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_stateChangeCallback = callback;
}

void UpdateDownloader::notifyStateChange() {
    // Thread-safety note: We copy the callback under lock, then invoke outside the lock.
    // This prevents deadlock if the callback tries to acquire our mutex (e.g., to read state).
    // The callback copy is safe because std::function handles reference counting internally.
    // Current callbacks only trigger HUD redraws via markDataDirty(), which is thread-safe.
    std::function<void()> callback;
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        callback = m_stateChangeCallback;
    }
    if (callback) {
        callback();
    }
}

void UpdateDownloader::resetSteps() {
    std::lock_guard<std::mutex> lock(m_mutex);
    for (int i = 0; i < static_cast<int>(Step::STEP_COUNT); i++) {
        m_stepStatus[i] = StepStatus::PENDING;
    }
}

void UpdateDownloader::setStepStatus(Step step, StepStatus status) {
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_stepStatus[static_cast<int>(step)] = status;
    }
    notifyStateChange();  // Trigger UI refresh
}

std::vector<UpdateDownloader::StepInfo> UpdateDownloader::getSteps() const {
    std::lock_guard<std::mutex> lock(m_mutex);

    // Step labels
    static const char* labels[] = {
        "Download",
        "Verify",
        "Backup",
        "Extract",
        "Install"
    };

    std::vector<StepInfo> steps;
    for (int i = 0; i < static_cast<int>(Step::STEP_COUNT); i++) {
        steps.push_back({labels[i], m_stepStatus[i]});
    }
    return steps;
}

void UpdateDownloader::cleanupOldFiles() {
    // Delete any .dlo.old files left over from previous updates
    // This should be called on startup when the new DLL is loaded

    // Get plugin directory
    char modulePath[MAX_PATH];
    HMODULE hModule = NULL;

    if (GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                           (LPCSTR)&UpdateDownloader::getInstance, &hModule)) {
        if (GetModuleFileNameA(hModule, modulePath, MAX_PATH) > 0) {
            std::string path(modulePath);
            size_t lastSlash = path.find_last_of("\\/");
            if (lastSlash != std::string::npos) {
                std::string pluginDir = path.substr(0, lastSlash + 1);

                // Look for .dlo.old files
                WIN32_FIND_DATAA findData;
                std::string searchPath = pluginDir + "*.dlo.old";
                HANDLE hFind = FindFirstFileA(searchPath.c_str(), &findData);

                if (hFind != INVALID_HANDLE_VALUE) {
                    do {
                        std::string oldFile = pluginDir + findData.cFileName;
                        if (DeleteFileA(oldFile.c_str())) {
                            DEBUG_INFO_F("UpdateDownloader: Cleaned up %s", findData.cFileName);
                        } else {
                            DEBUG_WARN_F("UpdateDownloader: Failed to delete %s", findData.cFileName);
                        }
                    } while (FindNextFileA(hFind, &findData));
                    FindClose(hFind);
                }

                // Note: Backup directory (mxbmrp3_update_backup/) is intentionally NOT deleted here.
                // It's kept until the next update starts (cleaned in createBackupDirectory()),
                // allowing users to manually recover files even after multiple game restarts.
            }
        }
    }
}

bool UpdateDownloader::checkAndClearDonationNudge() {
    try {
        const char* savePath = PluginManager::getInstance().getSavePath();
        if (!savePath || strlen(savePath) == 0) return false;
        std::string nudgePath = std::string(savePath) + "\\mxbmrp3\\donation_nudge_pending";

        // Read the version the installer wrote, then delete the sentinel regardless.
        std::string installedVersion;
        HANDLE h = CreateFileA(nudgePath.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr,
                               OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (h == INVALID_HANDLE_VALUE) return false;
        char buf[64] = {};
        DWORD read = 0;
        ReadFile(h, buf, sizeof(buf) - 1, &read, nullptr);
        CloseHandle(h);
        installedVersion = std::string(buf, read);
        DeleteFileA(nudgePath.c_str());

        // Only show the nudge if the running DLL matches the installed version.
        // Use compareVersions (strips "v" prefix, tolerates 3-vs-4 components) rather
        // than == because getLatestVersion() returns the raw tag ("v1.25.0.0") while
        // PLUGIN_VERSION has no prefix ("1.25.0.0").
        // isValidVersion guards the corrupt-sentinel case: compareVersions returns 0
        // for "equal" AND for "unparseable", so a garbled (non-empty) sentinel would
        // otherwise alias to a match and fire a spurious nudge.
        if (UpdateChecker::isValidVersion(installedVersion) &&
            UpdateChecker::compareVersions(installedVersion, PluginConstants::PLUGIN_VERSION) == 0) {
            DEBUG_INFO_F("UpdateDownloader: Donation nudge confirmed for v%s", PluginConstants::PLUGIN_VERSION);
            return true;
        }
        DEBUG_INFO_F("UpdateDownloader: Donation nudge skipped (sentinel v%s, running v%s)",
                     installedVersion.c_str(), PluginConstants::PLUGIN_VERSION);
    } catch (...) {}
    return false;
}

std::string UpdateDownloader::getPluginDirectory() const {
    // Get save path from PluginManager and construct plugin directory
    const char* savePath = PluginManager::getInstance().getSavePath();
    if (!savePath || strlen(savePath) == 0) {
        return "";
    }

    // Plugin directory is typically: {game_path}/plugins/
    // But we need to find where our DLO is located
    // The save path is usually: {game_path}/Documents/
    // So plugin path would be: {game_path}/../plugins/ or we use GetModuleFileName

    // Use GetModuleFileName to find where our DLL is loaded from
    char modulePath[MAX_PATH];
    HMODULE hModule = NULL;

    // Get handle to our own DLL
    if (GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                           (LPCSTR)&UpdateDownloader::getInstance, &hModule)) {
        if (GetModuleFileNameA(hModule, modulePath, MAX_PATH) > 0) {
            std::string path(modulePath);
            // Remove filename to get directory
            size_t lastSlash = path.find_last_of("\\/");
            if (lastSlash != std::string::npos) {
                std::string baseDir = path.substr(0, lastSlash + 1);

                // Debug mode: use mxbmrp3_update_test/ subdirectory
                if (m_debugMode) {
                    std::string testDir = baseDir + "mxbmrp3_update_test\\";
                    CreateDirectoryA(testDir.c_str(), NULL);
                    DEBUG_INFO_F("UpdateDownloader: DEBUG MODE - Extracting to %s", testDir.c_str());
                    return testDir;
                }

                return baseDir;
            }
        }
    }

    return "";
}

void UpdateDownloader::workerThread() {
    // Exception barrier: an uncaught throw in a std::thread calls
    // std::terminate() and kills the host game process. File I/O, miniz
    // extraction, and std::string ops below can all throw under disk-full /
    // access-denied / corrupt-archive conditions.
    try {

        std::vector<char> zipData;
        std::string error;

        DEBUG_INFO("UpdateDownloader: Starting download...");
        // Step::DOWNLOAD already set to IN_PROGRESS in startDownload()
        notifyStateChange();

        // Download
        if (!downloadFile(zipData, error)) {
            if (m_cancelRequested || m_shutdownRequested) {
                m_state = State::IDLE;
                DEBUG_INFO("UpdateDownloader: Cancelled");
            } else {
                std::lock_guard<std::mutex> lock(m_mutex);
                m_errorMessage = error;
                m_state = State::FAILED;
                DEBUG_WARN_F("UpdateDownloader: Download failed - %s", error.c_str());
            }
            notifyStateChange();
            return;
        }

        // Download complete
        setStepStatus(Step::DOWNLOAD, StepStatus::COMPLETE);

        if (m_cancelRequested || m_shutdownRequested) {
            m_state = State::IDLE;
            notifyStateChange();
            return;
        }

        // Verify
        setStepStatus(Step::VERIFY, StepStatus::IN_PROGRESS);
        m_state = State::VERIFYING;
        notifyStateChange();
        DEBUG_INFO("UpdateDownloader: Verifying...");

        // Check file size
        if (m_expectedSize > 0 && zipData.size() != m_expectedSize) {
            std::string errorMsg = "Size mismatch: expected " + std::to_string(m_expectedSize) +
                                   ", got " + std::to_string(zipData.size());
            {
                std::lock_guard<std::mutex> lock(m_mutex);
                m_errorMessage = errorMsg;
                m_state = State::FAILED;
            }
            DEBUG_WARN_F("UpdateDownloader: %s", errorMsg.c_str());
            notifyStateChange();
            return;
        }

        if (m_cancelRequested || m_shutdownRequested) {
            m_state = State::IDLE;
            notifyStateChange();
            return;
        }

        // Verify SHA256 checksum if available
        std::string expectedHash;
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            expectedHash = m_checksumHash;
        }

        if (!expectedHash.empty()) {
            DEBUG_INFO("UpdateDownloader: Verifying SHA256 checksum...");

            std::string actualHash = calculateSHA256(zipData);
            if (actualHash.empty()) {
                // We HAVE an expected digest but couldn't verify against it —
                // installing anyway would silently skip the integrity check, so
                // treat it exactly like a mismatch.
                {
                    std::lock_guard<std::mutex> lock(m_mutex);
                    m_errorMessage = "SHA256 verification failed (could not compute hash)";
                    m_state = State::FAILED;
                }
                DEBUG_WARN("UpdateDownloader: SHA256 calculation failed - refusing to install unverified download");
                notifyStateChange();
                return;
            } else if (actualHash != expectedHash) {
                {
                    std::lock_guard<std::mutex> lock(m_mutex);
                    m_errorMessage = "SHA256 checksum mismatch";
                    m_state = State::FAILED;
                }
                DEBUG_WARN_F("UpdateDownloader: SHA256 mismatch! Expected: %s, Got: %s",
                            expectedHash.c_str(), actualHash.c_str());
                notifyStateChange();
                return;
            } else {
                DEBUG_INFO_F("UpdateDownloader: SHA256 verified: %s", actualHash.c_str());
            }
        } else {
            // GitHub's release API emits a digest for every asset nowadays, so an
            // absent one is unusual — proceed (the chain is still TLS-anchored to
            // an allowlisted GitHub host) but say so loudly in the log.
            DEBUG_WARN("UpdateDownloader: release API provided no SHA256 digest - installing on TLS trust alone");
        }

        // Verify complete
        setStepStatus(Step::VERIFY, StepStatus::COMPLETE);

        if (m_cancelRequested || m_shutdownRequested) {
            m_state = State::IDLE;
            notifyStateChange();
            return;
        }

        // Extract and install (backup/extract/install steps are handled in extractAndInstall)
        m_state = State::EXTRACTING;
        notifyStateChange();
        DEBUG_INFO("UpdateDownloader: Processing update...");

        if (!extractAndInstall(zipData, error)) {
            {
                std::lock_guard<std::mutex> lock(m_mutex);
                m_errorMessage = error;
                m_state = State::FAILED;
            }
            // notifyStateChange acquires mutex, so must be called outside the lock
            DEBUG_WARN_F("UpdateDownloader: Extract failed - %s", error.c_str());
            notifyStateChange();
            return;
        }

        // Success!
        m_state = State::READY;
        DEBUG_INFO("UpdateDownloader: Update ready, restart required");

        // Write sentinel containing the installed version so the next launch
        // can confirm the new DLL is actually running before showing the nudge.
        try {
            const char* savePath = PluginManager::getInstance().getSavePath();
            std::string version = UpdateChecker::getInstance().getLatestVersion();
            if (savePath && strlen(savePath) > 0 && !version.empty()) {
                std::string nudgePath = std::string(savePath) + "\\mxbmrp3\\donation_nudge_pending";
                // Route through the shared atomic writer, like every other persisted file.
                AtomicFileWriter::writeFileAtomic(nudgePath, version);
            }
        } catch (...) {}

        notifyStateChange();

    } catch (const std::exception& e) {
        DEBUG_WARN_F("UpdateDownloader thread terminated by exception: %s", e.what());
        // Set the atomic state first (noexcept). The string assignment below
        // can itself throw bad_alloc — if the original exception WAS
        // bad_alloc, allocating a new error string would defeat the catch
        // and terminate the thread. Wrap the assignment so we degrade to an
        // empty message rather than crash.
        //
        // Acquire the lock once outside the inner try: a second lock_guard in
        // a nested catch could itself throw std::system_error, which would
        // propagate out of the thread and call std::terminate.
        m_state = State::FAILED;
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            try {
                m_errorMessage = e.what();
            } catch (...) {
                m_errorMessage.clear();  // noexcept
            }
        }
        // notifyStateChange acquires m_mutex internally, so call it outside the lock
        notifyStateChange();
    } catch (...) {
        DEBUG_WARN("UpdateDownloader thread terminated by unknown exception");
        // Same restructure as the std::exception& branch above: acquire the
        // lock once outside the inner try so a second lock_guard in a nested
        // catch can't throw std::system_error and propagate out of the thread.
        m_state = State::FAILED;
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            try {
                m_errorMessage = "Unknown error";
            } catch (...) {
                m_errorMessage.clear();  // noexcept
            }
        }
        notifyStateChange();
    }
}

// Update artifacts only ever live on GitHub or its asset CDN. Applied to the
// initial URL (from the GitHub API response) AND to every redirect hop, so a
// spoofed/compromised redirect can't point the download elsewhere.
static bool isAllowedUpdateHost(const std::string& host) {
    auto endsWith = [](const std::string& s, const char* suffix) {
        size_t n = std::strlen(suffix);
        return s.size() >= n && s.compare(s.size() - n, n, suffix) == 0;
    };
    return host == "github.com" ||
           endsWith(host, ".github.com") ||
           endsWith(host, ".githubusercontent.com");
}

bool UpdateDownloader::downloadFile(std::vector<char>& outData, std::string& outError, int redirectDepth) {
    // Check redirect depth to prevent infinite redirect loops
    if (redirectDepth > MAX_REDIRECTS) {
        outError = "Too many redirects";
        return false;
    }

    std::string url;
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        url = m_downloadUrl;
    }

    if (url.empty()) {
        outError = "No download URL";
        return false;
    }

    // Parse URL to extract host and path
    // URL format: https://github.com/...
    //
    // HTTPS only: the whole update chain is anchored on TLS to GitHub (there is
    // no code signing), so a plain-http URL — even one that "came from" the API
    // response — must never be fetched.
    std::string host;
    std::string path;

    if (url.substr(0, 8) == "https://") {
        url = url.substr(8);
    } else {
        outError = "Refusing non-HTTPS download URL";
        return false;
    }

    size_t pathStart = url.find('/');
    if (pathStart == std::string::npos) {
        outError = "Invalid URL format";
        return false;
    }

    host = url.substr(0, pathStart);
    path = url.substr(pathStart);

    // Host allowlist (also applied to every redirect hop, which re-enters this
    // function): update artifacts only ever live on GitHub or its asset CDN. A
    // compromised or spoofed redirect must not be able to point the download
    // anywhere else.
    if (!isAllowedUpdateHost(host)) {
        outError = "Refusing download from unexpected host: " + host;
        return false;
    }

    // Convert to wide strings using proper UTF-8 conversion (handles non-ASCII URLs)
    std::wstring wHost, wPath;
    int hostLen = MultiByteToWideChar(CP_UTF8, 0, host.c_str(), -1, NULL, 0);
    if (hostLen > 0) {
        wHost.resize(hostLen - 1);
        MultiByteToWideChar(CP_UTF8, 0, host.c_str(), -1, &wHost[0], hostLen);
    }
    int pathLen = MultiByteToWideChar(CP_UTF8, 0, path.c_str(), -1, NULL, 0);
    if (pathLen > 0) {
        wPath.resize(pathLen - 1);
        MultiByteToWideChar(CP_UTF8, 0, path.c_str(), -1, &wPath[0], pathLen);
    }

    // Create user agent
    char userAgentA[128];
    snprintf(userAgentA, sizeof(userAgentA), "%s/%s",
             PluginConstants::PLUGIN_DISPLAY_NAME,
             PluginConstants::PLUGIN_VERSION);
    std::wstring userAgent(userAgentA, userAgentA + strlen(userAgentA));

    // Initialize WinHTTP - create all handles (non-blocking operations)
    HINTERNET hSession = WinHttpOpen(userAgent.c_str(),
                                     WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,
                                     WINHTTP_NO_PROXY_NAME,
                                     WINHTTP_NO_PROXY_BYPASS, 0);
    if (!hSession) {
        outError = "WinHttpOpen failed (error " + std::to_string(GetLastError()) + ")";
        return false;
    }

    // Set timeouts (30 seconds for download)
    WinHttpSetTimeouts(hSession, 30000, 30000, 30000, 30000);

    // Connect (HTTPS only — non-HTTPS URLs were rejected during URL parsing)
    HINTERNET hConnect = WinHttpConnect(hSession, wHost.c_str(), INTERNET_DEFAULT_HTTPS_PORT, 0);
    if (!hConnect) {
        DWORD err = GetLastError();
        WinHttpCloseHandle(hSession);
        outError = "Connection failed (error " + std::to_string(err) + ")";
        return false;
    }

    // Create request
    HINTERNET hRequest = WinHttpOpenRequest(hConnect, L"GET", wPath.c_str(),
                                            NULL, WINHTTP_NO_REFERER,
                                            WINHTTP_DEFAULT_ACCEPT_TYPES, WINHTTP_FLAG_SECURE);
    if (!hRequest) {
        DWORD err = GetLastError();
        WinHttpCloseHandle(hConnect);
        WinHttpCloseHandle(hSession);
        outError = "Request creation failed (error " + std::to_string(err) + ")";
        return false;
    }

    // Disable WinHTTP's automatic redirect following: its internal hops would
    // bypass the manual per-hop host allowlist below entirely (the manual
    // 301/302/303/307/308 handling re-runs isAllowedUpdateHost on every hop —
    // that path must be the ONLY redirect path). Best-effort: if the option
    // isn't supported, the manual handler still sees whatever status arrives.
    {
        DWORD redirectPolicy = WINHTTP_OPTION_REDIRECT_POLICY_NEVER;
        WinHttpSetOption(hRequest, WINHTTP_OPTION_REDIRECT_POLICY,
                         &redirectPolicy, sizeof(redirectPolicy));
    }

    // Publish all handles for cross-thread cancellation by shutdown().
    // Handle creation above is non-blocking, so publishing after all three
    // are ready means shutdown can always close the complete set.
    {
        std::lock_guard<std::mutex> lock(m_httpHandleMutex);
        m_hHttpSession = hSession;
        m_hHttpConnect = hConnect;
        m_hHttpRequest = hRequest;
    }

    // Re-check shutdown to close the timing window where shutdown() runs
    // between handle creation and the store above (it would miss them otherwise).
    if (m_shutdownRequested) {
        closeHttpHandles();
        outError = "Shutdown requested";
        return false;
    }

    // --- Blocking calls begin (cancellable by shutdown via closeHttpHandles) ---

    BOOL bResults = WinHttpSendRequest(hRequest, WINHTTP_NO_ADDITIONAL_HEADERS,
                                        0, WINHTTP_NO_REQUEST_DATA, 0, 0, 0);
    if (!bResults) {
        DWORD err = GetLastError();
        closeHttpHandles();
        outError = "Send failed (error " + std::to_string(err) + ")";
        return false;
    }

    bResults = WinHttpReceiveResponse(hRequest, NULL);
    if (!bResults) {
        DWORD err = GetLastError();
        closeHttpHandles();
        outError = "No response (error " + std::to_string(err) + ")";
        return false;
    }

    // Check for redirect (GitHub releases often redirect)
    DWORD statusCode = 0;
    DWORD statusCodeSize = sizeof(statusCode);
    WinHttpQueryHeaders(hRequest, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
                        WINHTTP_HEADER_NAME_BY_INDEX, &statusCode, &statusCodeSize, WINHTTP_NO_HEADER_INDEX);

    // Handle redirects (all standard redirect codes: GitHub's CDN normally sends
    // 302, but 301/303/307/308 are equally valid for a GET and must not surface
    // as an opaque "HTTP 307" failure). Every hop re-runs the host allowlist.
    if (statusCode == 301 || statusCode == 302 || statusCode == 303 ||
        statusCode == 307 || statusCode == 308) {
        // Get redirect location
        DWORD locationSize = 0;
        WinHttpQueryHeaders(hRequest, WINHTTP_QUERY_LOCATION, WINHTTP_HEADER_NAME_BY_INDEX,
                           NULL, &locationSize, WINHTTP_NO_HEADER_INDEX);

        if (GetLastError() == ERROR_INSUFFICIENT_BUFFER && locationSize > 0) {
            std::vector<wchar_t> locationBuf(locationSize / sizeof(wchar_t) + 1);
            if (WinHttpQueryHeaders(hRequest, WINHTTP_QUERY_LOCATION, WINHTTP_HEADER_NAME_BY_INDEX,
                                   locationBuf.data(), &locationSize, WINHTTP_NO_HEADER_INDEX)) {
                // Close current handles so recursive call can store new ones
                closeHttpHandles();

                // Convert wide string to narrow using proper Windows API
                std::wstring wLocation(locationBuf.data());
                int requiredSize = WideCharToMultiByte(CP_UTF8, 0, wLocation.c_str(), -1, NULL, 0, NULL, NULL);
                std::string newUrl;
                if (requiredSize > 0) {
                    newUrl.resize(requiredSize - 1);  // -1 to exclude null terminator
                    WideCharToMultiByte(CP_UTF8, 0, wLocation.c_str(), -1, &newUrl[0], requiredSize, NULL, NULL);
                }

                if (newUrl.empty()) {
                    outError = "Failed to parse redirect URL";
                    return false;
                }

                DEBUG_INFO_F("UpdateDownloader: Following redirect (%d/%d) to %s",
                            redirectDepth + 1, MAX_REDIRECTS, newUrl.c_str());

                // Update URL and retry with incremented depth
                {
                    std::lock_guard<std::mutex> lock(m_mutex);
                    m_downloadUrl = newUrl;
                }
                return downloadFile(outData, outError, redirectDepth + 1);
            }
        }

        closeHttpHandles();
        outError = "Failed to follow redirect";
        return false;
    }

    if (statusCode != 200) {
        closeHttpHandles();
        outError = "HTTP " + std::to_string(statusCode);
        return false;
    }

    // Get content length for progress
    DWORD contentLength = 0;
    DWORD contentLengthSize = sizeof(contentLength);
    if (WinHttpQueryHeaders(hRequest, WINHTTP_QUERY_CONTENT_LENGTH | WINHTTP_QUERY_FLAG_NUMBER,
                           WINHTTP_HEADER_NAME_BY_INDEX, &contentLength, &contentLengthSize, WINHTTP_NO_HEADER_INDEX)) {
        m_totalBytes = contentLength;
    }

    // Read data. Bound the pre-allocation by the download cap: Content-Length is
    // an attacker-influenced header, and reserve(~4GB) would throw bad_alloc here
    // (caught by the worker's top-level handler, but a hostile header shouldn't
    // get to allocate anything the size check below would reject anyway).
    constexpr size_t MAX_DOWNLOAD_SIZE = 50 * 1024 * 1024;  // 50MB limit
    outData.clear();
    const size_t reserveHint = (m_totalBytes > 0)
        ? static_cast<size_t>(m_totalBytes.load()) : 1024 * 1024;
    outData.reserve((std::min)(reserveHint, MAX_DOWNLOAD_SIZE));

    DWORD dwSize = 0;
    DWORD dwDownloaded = 0;

    bool readError = false;
    do {
        if (m_cancelRequested || m_shutdownRequested) {
            closeHttpHandles();
            outError = "Cancelled";
            return false;
        }

        dwSize = 0;
        if (!WinHttpQueryDataAvailable(hRequest, &dwSize)) {
            readError = true;   // mid-transfer network error, NOT end-of-stream
            break;
        }

        if (dwSize == 0) break;

        // Check size limit
        if (outData.size() + dwSize > MAX_DOWNLOAD_SIZE) {
            closeHttpHandles();
            outError = "Download too large";
            return false;
        }

        std::vector<char> buffer(dwSize);
        if (WinHttpReadData(hRequest, buffer.data(), dwSize, &dwDownloaded)) {
            outData.insert(outData.end(), buffer.begin(), buffer.begin() + dwDownloaded);
            m_bytesDownloaded = outData.size();
        } else {
            readError = true;
            break;
        }

    } while (dwSize > 0);

    closeHttpHandles();

    // A connection lost mid-transfer used to fall through as success with partial
    // data — the SHA256/size checks still caught it downstream, but the user saw a
    // misleading "Size mismatch" instead of a network error. Report it as what it is.
    if (readError || (contentLength > 0 && outData.size() < contentLength)) {
        outError = "Connection lost mid-download (" + std::to_string(outData.size()) +
                   " of " + std::to_string(contentLength) + " bytes)";
        return false;
    }

    if (outData.empty()) {
        outError = "No data received";
        return false;
    }

    DEBUG_INFO_F("UpdateDownloader: Downloaded %zu bytes", outData.size());
    return true;
}

