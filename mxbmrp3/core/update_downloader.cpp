// ============================================================================
// core/update_downloader.cpp
// Downloads and installs plugin updates
// ============================================================================
#include "update_downloader.h"
#include "plugin_constants.h"
#include "plugin_manager.h"
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
    if (m_workerThread.joinable()) {
        m_workerThread.join();
    }
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

// Forward declaration for recursive delete (defined later in file)
static bool deleteDirectoryRecursive(const std::string& dir);

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

std::string UpdateDownloader::getPluginDirectory() const {
    // Get save path from PluginManager and construct plugin directory
    const char* savePath = PluginManager::getInstance().getSavePath();
    if (!savePath || strlen(savePath) == 0) {
        return "";
    }

    // Plugin directory is typically: {game_path}/plugins/
    // But we need to find where mxbmrp3.dlo is located
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
            DEBUG_WARN("UpdateDownloader: SHA256 calculation failed");
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
    notifyStateChange();
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
    std::string host;
    std::string path;
    bool isHttps = true;

    if (url.substr(0, 8) == "https://") {
        url = url.substr(8);
    } else if (url.substr(0, 7) == "http://") {
        url = url.substr(7);
        isHttps = false;
    }

    size_t pathStart = url.find('/');
    if (pathStart == std::string::npos) {
        outError = "Invalid URL format";
        return false;
    }

    host = url.substr(0, pathStart);
    path = url.substr(pathStart);

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

    // Initialize WinHTTP
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

    // Connect
    INTERNET_PORT port = isHttps ? INTERNET_DEFAULT_HTTPS_PORT : INTERNET_DEFAULT_HTTP_PORT;
    HINTERNET hConnect = WinHttpConnect(hSession, wHost.c_str(), port, 0);
    if (!hConnect) {
        DWORD err = GetLastError();
        WinHttpCloseHandle(hSession);
        outError = "Connection failed (error " + std::to_string(err) + ")";
        return false;
    }

    // Create request
    DWORD flags = isHttps ? WINHTTP_FLAG_SECURE : 0;
    HINTERNET hRequest = WinHttpOpenRequest(hConnect, L"GET", wPath.c_str(),
                                            NULL, WINHTTP_NO_REFERER,
                                            WINHTTP_DEFAULT_ACCEPT_TYPES, flags);
    if (!hRequest) {
        DWORD err = GetLastError();
        WinHttpCloseHandle(hConnect);
        WinHttpCloseHandle(hSession);
        outError = "Request creation failed (error " + std::to_string(err) + ")";
        return false;
    }

    // Send request
    BOOL bResults = WinHttpSendRequest(hRequest, WINHTTP_NO_ADDITIONAL_HEADERS,
                                        0, WINHTTP_NO_REQUEST_DATA, 0, 0, 0);
    if (!bResults) {
        DWORD err = GetLastError();
        WinHttpCloseHandle(hRequest);
        WinHttpCloseHandle(hConnect);
        WinHttpCloseHandle(hSession);
        outError = "Send failed (error " + std::to_string(err) + ")";
        return false;
    }

    // Receive response
    bResults = WinHttpReceiveResponse(hRequest, NULL);
    if (!bResults) {
        DWORD err = GetLastError();
        WinHttpCloseHandle(hRequest);
        WinHttpCloseHandle(hConnect);
        WinHttpCloseHandle(hSession);
        outError = "No response (error " + std::to_string(err) + ")";
        return false;
    }

    // Check for redirect (GitHub releases often redirect)
    DWORD statusCode = 0;
    DWORD statusCodeSize = sizeof(statusCode);
    WinHttpQueryHeaders(hRequest, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
                        WINHTTP_HEADER_NAME_BY_INDEX, &statusCode, &statusCodeSize, WINHTTP_NO_HEADER_INDEX);

    // Handle redirects (302, 301)
    if (statusCode == 302 || statusCode == 301) {
        // Get redirect location
        DWORD locationSize = 0;
        WinHttpQueryHeaders(hRequest, WINHTTP_QUERY_LOCATION, WINHTTP_HEADER_NAME_BY_INDEX,
                           NULL, &locationSize, WINHTTP_NO_HEADER_INDEX);

        if (GetLastError() == ERROR_INSUFFICIENT_BUFFER && locationSize > 0) {
            std::vector<wchar_t> locationBuf(locationSize / sizeof(wchar_t) + 1);
            if (WinHttpQueryHeaders(hRequest, WINHTTP_QUERY_LOCATION, WINHTTP_HEADER_NAME_BY_INDEX,
                                   locationBuf.data(), &locationSize, WINHTTP_NO_HEADER_INDEX)) {
                // Close current handles
                WinHttpCloseHandle(hRequest);
                WinHttpCloseHandle(hConnect);
                WinHttpCloseHandle(hSession);

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

        WinHttpCloseHandle(hRequest);
        WinHttpCloseHandle(hConnect);
        WinHttpCloseHandle(hSession);
        outError = "Failed to follow redirect";
        return false;
    }

    if (statusCode != 200) {
        WinHttpCloseHandle(hRequest);
        WinHttpCloseHandle(hConnect);
        WinHttpCloseHandle(hSession);
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

    // Read data
    outData.clear();
    outData.reserve(m_totalBytes > 0 ? m_totalBytes.load() : 1024 * 1024);  // Pre-allocate

    DWORD dwSize = 0;
    DWORD dwDownloaded = 0;
    constexpr size_t MAX_DOWNLOAD_SIZE = 50 * 1024 * 1024;  // 50MB limit

    do {
        if (m_cancelRequested || m_shutdownRequested) {
            WinHttpCloseHandle(hRequest);
            WinHttpCloseHandle(hConnect);
            WinHttpCloseHandle(hSession);
            outError = "Cancelled";
            return false;
        }

        dwSize = 0;
        if (!WinHttpQueryDataAvailable(hRequest, &dwSize)) {
            break;
        }

        if (dwSize == 0) break;

        // Check size limit
        if (outData.size() + dwSize > MAX_DOWNLOAD_SIZE) {
            WinHttpCloseHandle(hRequest);
            WinHttpCloseHandle(hConnect);
            WinHttpCloseHandle(hSession);
            outError = "Download too large";
            return false;
        }

        std::vector<char> buffer(dwSize);
        if (WinHttpReadData(hRequest, buffer.data(), dwSize, &dwDownloaded)) {
            outData.insert(outData.end(), buffer.begin(), buffer.begin() + dwDownloaded);
            m_bytesDownloaded = outData.size();
        }

    } while (dwSize > 0);

    WinHttpCloseHandle(hRequest);
    WinHttpCloseHandle(hConnect);
    WinHttpCloseHandle(hSession);

    if (outData.empty()) {
        outError = "No data received";
        return false;
    }

    DEBUG_INFO_F("UpdateDownloader: Downloaded %zu bytes", outData.size());
    return true;
}

bool UpdateDownloader::createBackupDirectory(const std::string& backupDir) {
    // Remove existing backup directory if present (recursively)
    deleteDirectoryRecursive(backupDir);

    // Create fresh backup directory
    if (!CreateDirectoryA(backupDir.c_str(), NULL)) {
        DWORD err = GetLastError();
        if (err != ERROR_ALREADY_EXISTS) {
            DEBUG_WARN_F("UpdateDownloader: Failed to create backup directory (error %lu)", err);
            return false;
        }
    }
    DEBUG_INFO_F("UpdateDownloader: Created backup directory: %s", backupDir.c_str());
    return true;
}

// Helper to create directories recursively (like mkdir -p)
static bool createDirectoriesRecursive(const std::string& path) {
    // Try to create the directory
    if (CreateDirectoryA(path.c_str(), NULL)) {
        return true;
    }

    DWORD err = GetLastError();
    if (err == ERROR_ALREADY_EXISTS) {
        return true;
    }

    if (err == ERROR_PATH_NOT_FOUND) {
        // Parent doesn't exist, create it first
        size_t lastSlash = path.find_last_of('\\');
        if (lastSlash != std::string::npos && lastSlash > 0) {
            std::string parent = path.substr(0, lastSlash);
            if (createDirectoriesRecursive(parent)) {
                // Now try again
                return CreateDirectoryA(path.c_str(), NULL) || GetLastError() == ERROR_ALREADY_EXISTS;
            }
        }
    }

    return false;
}

// Recursively copy a directory
static bool copyDirectoryRecursive(const std::string& srcDir, const std::string& dstDir) {
    // Create destination directory
    if (!createDirectoriesRecursive(dstDir)) {
        return false;
    }

    WIN32_FIND_DATAA findData;
    std::string searchPath = srcDir + "*";
    HANDLE hFind = FindFirstFileA(searchPath.c_str(), &findData);

    if (hFind == INVALID_HANDLE_VALUE) {
        return true;  // Empty or non-existent directory is ok
    }

    bool success = true;
    do {
        if (strcmp(findData.cFileName, ".") == 0 || strcmp(findData.cFileName, "..") == 0) {
            continue;
        }

        std::string srcPath = srcDir + findData.cFileName;
        std::string dstPath = dstDir + findData.cFileName;

        if (findData.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
            // Recurse into subdirectory
            if (!copyDirectoryRecursive(srcPath + "\\", dstPath + "\\")) {
                success = false;
                break;
            }
        } else {
            // Copy file
            if (!CopyFileA(srcPath.c_str(), dstPath.c_str(), FALSE)) {
                DEBUG_WARN_F("UpdateDownloader: Failed to copy %s", srcPath.c_str());
                success = false;
                break;
            }
        }
    } while (FindNextFileA(hFind, &findData));

    FindClose(hFind);
    return success;
}

// Recursively delete a directory and its contents
static bool deleteDirectoryRecursive(const std::string& dir) {
    WIN32_FIND_DATAA findData;
    std::string searchPath = dir + "*";
    HANDLE hFind = FindFirstFileA(searchPath.c_str(), &findData);

    if (hFind == INVALID_HANDLE_VALUE) {
        return true;  // Already gone
    }

    do {
        if (strcmp(findData.cFileName, ".") == 0 || strcmp(findData.cFileName, "..") == 0) {
            continue;
        }

        std::string path = dir + findData.cFileName;

        if (findData.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
            deleteDirectoryRecursive(path + "\\");
        } else {
            DeleteFileA(path.c_str());
        }
    } while (FindNextFileA(hFind, &findData));

    FindClose(hFind);
    RemoveDirectoryA(dir.c_str());
    return true;
}

bool UpdateDownloader::backupExistingFiles(const std::string& pluginDir, const std::string& backupDir) {
    // Move the entire plugin installation to backup:
    // Windows allows moving loaded DLLs, so we can move even the running plugin!
    // This is faster and more atomic than copying.

    // Move the .dlo file (works even while loaded!)
    std::string dloSrc = pluginDir + GAME_DLO_NAME;
    std::string dloDst = backupDir + GAME_DLO_NAME;
    DWORD attrs = GetFileAttributesA(dloSrc.c_str());
    if (attrs != INVALID_FILE_ATTRIBUTES) {
        if (!MoveFileA(dloSrc.c_str(), dloDst.c_str())) {
            DEBUG_WARN_F("UpdateDownloader: Failed to move %s to backup (error %lu)", GAME_DLO_NAME, GetLastError());
            return false;
        }
        DEBUG_INFO_F("UpdateDownloader: Moved %s to backup", GAME_DLO_NAME);
    }

    // Move the data directory
    std::string dataSrc = pluginDir + "mxbmrp3_data";  // No trailing backslash for MoveFile
    std::string dataDst = backupDir + "mxbmrp3_data";
    attrs = GetFileAttributesA(dataSrc.c_str());
    if (attrs != INVALID_FILE_ATTRIBUTES && (attrs & FILE_ATTRIBUTE_DIRECTORY)) {
        if (!MoveFileA(dataSrc.c_str(), dataDst.c_str())) {
            DEBUG_WARN_F("UpdateDownloader: Failed to move mxbmrp3_data/ to backup (error %lu)", GetLastError());
            // Try to restore the .dlo we already moved
            if (MoveFileA(dloDst.c_str(), dloSrc.c_str())) {
                DEBUG_INFO_F("UpdateDownloader: Restored %s after failed data backup", GAME_DLO_NAME);
            } else {
                DEBUG_WARN_F("UpdateDownloader: CRITICAL - Failed to restore %s (error %lu) - DO NOT delete backup!",
                    GAME_DLO_NAME, GetLastError());
            }
            return false;
        }
        DEBUG_INFO("UpdateDownloader: Moved mxbmrp3_data/ to backup");
    }

    return true;
}

bool UpdateDownloader::restoreFromBackup(const std::string& pluginDir, const std::string& backupDir,
                                         const std::vector<std::string>& extractedFiles) {
    DEBUG_WARN("UpdateDownloader: Restoring from backup...");

    // First, delete any files we extracted
    for (const auto& relativePath : extractedFiles) {
        std::string filePath = pluginDir + relativePath;
        DeleteFileA(filePath.c_str());
    }

    // Also remove any new mxbmrp3_data directory that might have been created
    std::string dataDir = pluginDir + "mxbmrp3_data";
    deleteDirectoryRecursive(dataDir + "\\");

    // Move the .dlo file back from backup
    std::string dloSrc = backupDir + GAME_DLO_NAME;
    std::string dloDst = pluginDir + GAME_DLO_NAME;
    DWORD attrs = GetFileAttributesA(dloSrc.c_str());
    if (attrs != INVALID_FILE_ATTRIBUTES) {
        // Delete any partial .dlo that might have been extracted
        DeleteFileA(dloDst.c_str());
        if (MoveFileA(dloSrc.c_str(), dloDst.c_str())) {
            DEBUG_INFO_F("UpdateDownloader: Restored %s", GAME_DLO_NAME);
        } else {
            DEBUG_WARN_F("UpdateDownloader: Failed to restore %s (error %lu)", GAME_DLO_NAME, GetLastError());
        }
    }

    // Move the data directory back from backup
    std::string dataSrc = backupDir + "mxbmrp3_data";
    std::string dataDst = pluginDir + "mxbmrp3_data";
    attrs = GetFileAttributesA(dataSrc.c_str());
    if (attrs != INVALID_FILE_ATTRIBUTES && (attrs & FILE_ATTRIBUTE_DIRECTORY)) {
        if (MoveFileA(dataSrc.c_str(), dataDst.c_str())) {
            DEBUG_INFO("UpdateDownloader: Restored mxbmrp3_data/ directory");
        } else {
            DEBUG_WARN_F("UpdateDownloader: Failed to restore mxbmrp3_data/ (error %lu)", GetLastError());
        }
    }

    DEBUG_INFO("UpdateDownloader: Restore complete");
    return true;
}

void UpdateDownloader::cleanupBackup(const std::string& backupDir) {
    deleteDirectoryRecursive(backupDir);
    DEBUG_INFO("UpdateDownloader: Cleaned up backup directory");
}

void UpdateDownloader::cleanupExtractedFiles(const std::string& pluginDir, const std::vector<std::string>& files) {
    for (const auto& relativePath : files) {
        std::string filePath = pluginDir + relativePath;
        DeleteFileA(filePath.c_str());
    }
}

bool UpdateDownloader::verifyExtractedFiles(const std::string& pluginDir,
                                            const std::vector<std::pair<std::string, size_t>>& expectedFiles) {
    for (const auto& [relativePath, expectedSize] : expectedFiles) {
        std::string filePath = pluginDir + relativePath;

        // Check file exists
        HANDLE hFile = CreateFileA(filePath.c_str(), GENERIC_READ, FILE_SHARE_READ,
                                   NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
        if (hFile == INVALID_HANDLE_VALUE) {
            DEBUG_WARN_F("UpdateDownloader: Verify failed - file missing: %s", relativePath.c_str());
            return false;
        }

        // Check file size
        LARGE_INTEGER fileSize;
        if (!GetFileSizeEx(hFile, &fileSize)) {
            CloseHandle(hFile);
            DEBUG_WARN_F("UpdateDownloader: Verify failed - can't get size: %s", relativePath.c_str());
            return false;
        }
        CloseHandle(hFile);

        if (static_cast<size_t>(fileSize.QuadPart) != expectedSize) {
            DEBUG_WARN_F("UpdateDownloader: Verify failed - size mismatch for %s: expected %zu, got %lld",
                        relativePath.c_str(), expectedSize, fileSize.QuadPart);
            return false;
        }
    }

    DEBUG_INFO_F("UpdateDownloader: Verified %zu files successfully", expectedFiles.size());
    return true;
}

// Check if a file should be skipped during extraction
static bool shouldSkipFile(const std::string& filename) {
    // Skip documentation files - not needed for runtime
    if (filename == "LICENSE" ||
        filename == "README.md" ||
        filename == "README.txt" ||
        filename == "THIRD_PARTY_LICENSES.md") {
        return true;
    }

    // Skip DLO files that don't match the current game
    // The ZIP may contain multiple game DLOs (mxbmrp3.dlo, mxbmrp3_gpb.dlo, etc.)
    // We only extract the one that matches GAME_DLO_NAME
    if (filename.size() >= 4 &&
        filename.substr(filename.size() - 4) == ".dlo") {
        if (filename != GAME_DLO_NAME) {
            DEBUG_INFO_F("UpdateDownloader: Skipping %s (not for this game)", filename.c_str());
            return true;
        }
    }

    return false;
}

std::string UpdateDownloader::mapToInstallPath(const std::string& zipFilename) const {
    // .dlo files go directly to plugin directory
    // Everything else goes under mxbmrp3_data/ subdirectory
    if (zipFilename.size() >= 4 &&
        zipFilename.substr(zipFilename.size() - 4) == ".dlo") {
        return zipFilename;
    }

    // All other files go under mxbmrp3_data/
    return "mxbmrp3_data\\" + zipFilename;
}

bool UpdateDownloader::extractAndInstall(const std::vector<char>& zipData, std::string& outError) {
    std::string pluginDir;
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        pluginDir = m_pluginPath;
    }

    if (pluginDir.empty()) {
        outError = "Cannot determine plugin directory";
        return false;
    }

    DEBUG_INFO_F("UpdateDownloader: Target directory: %s", pluginDir.c_str());
    DEBUG_INFO("UpdateDownloader: Scanning release...");

    // Initialize ZIP reader
    mz_zip_archive zip;
    memset(&zip, 0, sizeof(zip));

    if (!mz_zip_reader_init_mem(&zip, zipData.data(), zipData.size(), 0)) {
        outError = "Failed to open ZIP";
        return false;
    }

    int numFiles = static_cast<int>(mz_zip_reader_get_num_files(&zip));
    DEBUG_INFO_F("UpdateDownloader: ZIP contains %d files", numFiles);

    // First pass: collect all files we'll extract and their expected sizes
    std::vector<std::string> filesToBackup;
    std::vector<std::pair<std::string, size_t>> expectedFiles;  // relative path -> expected size

    for (int i = 0; i < numFiles; i++) {
        mz_zip_archive_file_stat fileStat;
        if (!mz_zip_reader_file_stat(&zip, i, &fileStat)) {
            continue;
        }

        if (mz_zip_reader_is_file_a_directory(&zip, i)) {
            continue;
        }

        std::string filename = fileStat.m_filename;

        if (filename.find("..") != std::string::npos) {
            continue;
        }

        // Strip top-level directory if present
        size_t firstSlash = filename.find('/');
        if (firstSlash != std::string::npos && firstSlash < filename.size() - 1) {
            std::string topDir = filename.substr(0, firstSlash);
            if (topDir.find("mxbmrp3") != std::string::npos) {
                filename = filename.substr(firstSlash + 1);
            }
        }

        if (filename.empty()) {
            continue;
        }

        // Skip documentation files (not needed at runtime)
        if (shouldSkipFile(filename)) {
            continue;
        }

        // Convert to Windows path separators
        std::replace(filename.begin(), filename.end(), '/', '\\');

        // Map to actual install path (.dlo to root, others to mxbmrp3_data/)
        std::string installPath = mapToInstallPath(filename);

        filesToBackup.push_back(installPath);
        expectedFiles.push_back({installPath, static_cast<size_t>(fileStat.m_uncomp_size)});
    }

    if (filesToBackup.empty()) {
        mz_zip_reader_end(&zip);
        outError = "ZIP contains no valid files";
        return false;
    }

    DEBUG_INFO_F("UpdateDownloader: Will extract %zu files", filesToBackup.size());

    // Early check: verify ZIP contains this game's DLO before doing any backup/extraction
    // This prevents unnecessary backup operations if the ZIP is for a different game
    bool hasGameDlo = false;
    for (const auto& file : filesToBackup) {
        if (file == GAME_DLO_NAME) {
            hasGameDlo = true;
            break;
        }
    }
    if (!hasGameDlo) {
        mz_zip_reader_end(&zip);
        DEBUG_WARN_F("UpdateDownloader: ZIP does not contain %s - invalid release for this game!", GAME_DLO_NAME);
        outError = "Release not for " GAME_NAME;
        return false;
    }
    DEBUG_INFO_F("UpdateDownloader: Found %s in ZIP", GAME_DLO_NAME);

    // In debug mode, skip backup since we're extracting to an empty test directory
    std::string backupDir;
    if (!m_debugMode) {
        // Backup step
        setStepStatus(Step::BACKUP, StepStatus::IN_PROGRESS);

        // Create backup directory
        backupDir = pluginDir + "mxbmrp3_update_backup\\";
        if (!createBackupDirectory(backupDir)) {
            mz_zip_reader_end(&zip);
            outError = "Failed to create backup directory";
            return false;
        }

        // Backup existing files (moves entire mxbmrp3.dlo and mxbmrp3_data/ to backup)
        if (!backupExistingFiles(pluginDir, backupDir)) {
            mz_zip_reader_end(&zip);
            // DO NOT cleanupBackup here - the DLO might still be in backup if restore failed!
            // The backup dir will be cleaned up on next successful update attempt.
            outError = "Backup failed - try manual update";
            return false;
        }

        setStepStatus(Step::BACKUP, StepStatus::COMPLETE);
    } else {
        DEBUG_INFO("UpdateDownloader: DEBUG MODE - Skipping backup (test directory)");
        setStepStatus(Step::BACKUP, StepStatus::SKIPPED);
    }

    // Extract step
    setStepStatus(Step::EXTRACT, StepStatus::IN_PROGRESS);
    DEBUG_INFO("UpdateDownloader: Extracting files...");

    // Track what we've extracted for potential rollback
    std::vector<std::string> extractedFiles;
    bool extractionFailed = false;
    std::string extractError;

    // Second pass: extract files
    for (int i = 0; i < numFiles && !extractionFailed; i++) {
        if (m_cancelRequested || m_shutdownRequested) {
            extractionFailed = true;
            extractError = "Cancelled";
            break;
        }

        mz_zip_archive_file_stat fileStat;
        if (!mz_zip_reader_file_stat(&zip, i, &fileStat)) {
            continue;
        }

        if (mz_zip_reader_is_file_a_directory(&zip, i)) {
            continue;
        }

        std::string filename = fileStat.m_filename;

        if (filename.find("..") != std::string::npos) {
            continue;
        }

        size_t firstSlash = filename.find('/');
        if (firstSlash != std::string::npos && firstSlash < filename.size() - 1) {
            std::string topDir = filename.substr(0, firstSlash);
            if (topDir.find("mxbmrp3") != std::string::npos) {
                filename = filename.substr(firstSlash + 1);
            }
        }

        if (filename.empty()) {
            continue;
        }

        // Skip documentation files (not needed at runtime)
        if (shouldSkipFile(filename)) {
            continue;
        }

        // Convert to Windows path separators
        std::replace(filename.begin(), filename.end(), '/', '\\');

        // Map to actual install path (.dlo to root, others to mxbmrp3_data/)
        std::string installPath = mapToInstallPath(filename);
        std::string outputPath = pluginDir + installPath;

        // Create subdirectories if needed (recursive)
        size_t lastSlash = outputPath.find_last_of('\\');
        if (lastSlash != std::string::npos) {
            std::string dir = outputPath.substr(0, lastSlash);
            createDirectoriesRecursive(dir);
        }

        // Extract file (existing files have been moved to backup, so path should be clear)
        if (!mz_zip_reader_extract_to_file(&zip, i, outputPath.c_str(), 0)) {
            extractionFailed = true;
            extractError = "Failed to extract: " + installPath;
            break;
        }

        extractedFiles.push_back(installPath);
        DEBUG_INFO_F("UpdateDownloader: Extracted %s", installPath.c_str());
    }

    mz_zip_reader_end(&zip);

    // Handle extraction failure
    if (extractionFailed) {
        DEBUG_WARN_F("UpdateDownloader: Extraction failed: %s", extractError.c_str());
        if (!m_debugMode) {
            restoreFromBackup(pluginDir, backupDir, extractedFiles);
        }
        // In debug mode, leave files in test dir - cleanup causes crashes
        outError = extractError;
        return false;
    }

    // Extract complete (DLO presence was verified before backup started)
    setStepStatus(Step::EXTRACT, StepStatus::COMPLETE);

    // Install step (verification)
    setStepStatus(Step::INSTALL, StepStatus::IN_PROGRESS);

    // Verify all extracted files (skip in debug mode)
    if (!m_debugMode && !verifyExtractedFiles(pluginDir, expectedFiles)) {
        DEBUG_WARN("UpdateDownloader: Verification failed, restoring backup");
        restoreFromBackup(pluginDir, backupDir, extractedFiles);
        outError = "File verification failed";
        return false;
    }

    // Install complete!
    setStepStatus(Step::INSTALL, StepStatus::COMPLETE);

    // Success!
    if (!m_debugMode) {
        // Intentionally keep backup until the next update for manual recovery if needed.
        // The backup directory is cleaned up by createBackupDirectory() when a new update starts,
        // allowing users to manually recover files even after multiple game restarts.
        DEBUG_INFO_F("UpdateDownloader: Extraction complete. Backup kept at: %s", backupDir.c_str());
    } else {
        DEBUG_INFO_F("UpdateDownloader: DEBUG MODE - Extraction complete at: %s", pluginDir.c_str());
    }
    return true;
}

std::string UpdateDownloader::calculateSHA256(const std::vector<char>& data) {
    BCRYPT_ALG_HANDLE hAlg = NULL;
    BCRYPT_HASH_HANDLE hHash = NULL;
    std::string result;

    // Open algorithm provider
    NTSTATUS status = BCryptOpenAlgorithmProvider(&hAlg, BCRYPT_SHA256_ALGORITHM, NULL, 0);
    if (!BCRYPT_SUCCESS(status)) {
        return result;
    }

    // Get hash object size
    DWORD hashObjSize = 0;
    DWORD cbData = 0;
    status = BCryptGetProperty(hAlg, BCRYPT_OBJECT_LENGTH, (PUCHAR)&hashObjSize, sizeof(DWORD), &cbData, 0);
    if (!BCRYPT_SUCCESS(status)) {
        BCryptCloseAlgorithmProvider(hAlg, 0);
        return result;
    }

    // Get hash size (should be 32 for SHA256)
    DWORD hashSize = 0;
    status = BCryptGetProperty(hAlg, BCRYPT_HASH_LENGTH, (PUCHAR)&hashSize, sizeof(DWORD), &cbData, 0);
    if (!BCRYPT_SUCCESS(status)) {
        BCryptCloseAlgorithmProvider(hAlg, 0);
        return result;
    }

    // Allocate hash object
    std::vector<UCHAR> hashObj(hashObjSize);
    std::vector<UCHAR> hash(hashSize);

    // Create hash object
    status = BCryptCreateHash(hAlg, &hHash, hashObj.data(), hashObjSize, NULL, 0, 0);
    if (!BCRYPT_SUCCESS(status)) {
        BCryptCloseAlgorithmProvider(hAlg, 0);
        return result;
    }

    // Hash the data
    status = BCryptHashData(hHash, (PUCHAR)data.data(), static_cast<ULONG>(data.size()), 0);
    if (!BCRYPT_SUCCESS(status)) {
        BCryptDestroyHash(hHash);
        BCryptCloseAlgorithmProvider(hAlg, 0);
        return result;
    }

    // Finish hash
    status = BCryptFinishHash(hHash, hash.data(), hashSize, 0);
    if (!BCRYPT_SUCCESS(status)) {
        BCryptDestroyHash(hHash);
        BCryptCloseAlgorithmProvider(hAlg, 0);
        return result;
    }

    // Convert to hex string (lowercase)
    std::ostringstream oss;
    oss << std::hex << std::setfill('0');
    for (DWORD i = 0; i < hashSize; i++) {
        oss << std::setw(2) << static_cast<int>(hash[i]);
    }
    result = oss.str();

    // Cleanup
    BCryptDestroyHash(hHash);
    BCryptCloseAlgorithmProvider(hAlg, 0);

    return result;
}
