// ============================================================================
// core/update_checker.cpp
// Checks GitHub for plugin updates
// ============================================================================
#include "update_checker.h"
#include "plugin_constants.h"
#include "../diagnostics/logger.h"

// Windows HTTP
#include <windows.h>
#include <winhttp.h>
#pragma comment(lib, "winhttp.lib")

// JSON parsing
#include "../vendor/nlohmann/json.hpp"

#include <sstream>
#include <vector>

UpdateChecker& UpdateChecker::getInstance() {
    static UpdateChecker instance;
    return instance;
}

UpdateChecker::UpdateChecker()
    : m_status(Status::IDLE)
    , m_mode(UpdateMode::OFF)  // Off by default
    , m_channel(UpdateChannel::STABLE)  // Stable by default
    , m_downloadSize(0)
    , m_latestIsPrerelease(false)
    , m_shutdownRequested(false)
    , m_debugMode(false)
    , m_lastCheckTimestamp(0)
{
}

UpdateChecker::~UpdateChecker() {
    shutdown();
}

void UpdateChecker::shutdown() {
    m_shutdownRequested = true;

    // Cancel any in-flight HTTP request by closing all WinHTTP handles.
    // CancelSynchronousIo does NOT work with WinHTTP (it uses internal waits,
    // not kernel I/O). Closing the handles causes pending WinHTTP calls to fail
    // immediately. The mutex in closeHttpHandles() prevents double-close with
    // the worker thread's cleanup.
    closeHttpHandles();

    if (m_workerThread.joinable()) {
        m_workerThread.join();
    }
}

void UpdateChecker::closeHttpHandles() {
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

void UpdateChecker::checkForUpdates() {
    // Cooldown check - prevent spam (silently ignore if on cooldown)
    unsigned long now = GetTickCount();
    if (now - m_lastCheckTimestamp < CHECK_COOLDOWN_MS) {
        return;
    }

    // Don't start another check if one is in progress
    if (m_status == Status::CHECKING) {
        return;
    }

    // Record check start time for cooldown
    m_lastCheckTimestamp = now;

    // Wait for any previous thread to complete
    if (m_workerThread.joinable()) {
        m_workerThread.join();
    }

    m_status = Status::CHECKING;
    m_shutdownRequested = false;

    // Start worker thread
    m_workerThread = std::thread(&UpdateChecker::workerThread, this);
}

bool UpdateChecker::isOnCooldown() const {
    return (GetTickCount() - m_lastCheckTimestamp < CHECK_COOLDOWN_MS);
}

std::string UpdateChecker::getLatestVersion() const {
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_latestVersion;
}

std::string UpdateChecker::getReleaseNotes() const {
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_releaseNotes;
}

std::string UpdateChecker::getDownloadUrl() const {
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_downloadUrl;
}

size_t UpdateChecker::getDownloadSize() const {
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_downloadSize;
}

std::string UpdateChecker::getAssetName() const {
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_assetName;
}

std::string UpdateChecker::getChecksumHash() const {
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_checksumHash;
}

void UpdateChecker::setDismissedVersion(const std::string& version) {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_dismissedVersion = version;
}

std::string UpdateChecker::getDismissedVersion() const {
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_dismissedVersion;
}

void UpdateChecker::setChannel(UpdateChannel channel) {
    std::lock_guard<std::mutex> lock(m_mutex);
    UpdateChannel old = m_channel.exchange(channel);
    if (old != channel) {
        m_dismissedVersion.clear();  // Clear stale dismissal from other channel
    }
}

bool UpdateChecker::isLatestPrerelease() const {
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_latestIsPrerelease;
}

bool UpdateChecker::shouldShowUpdateNotification() const {
    if (m_status != Status::UPDATE_AVAILABLE) {
        return false;
    }
    std::lock_guard<std::mutex> lock(m_mutex);
    // Show notification if no version dismissed, or if latest version differs from dismissed
    return m_dismissedVersion.empty() || m_latestVersion != m_dismissedVersion;
}

void UpdateChecker::setCompletionCallback(std::function<void()> callback) {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_completionCallback = callback;
}

void UpdateChecker::workerThread() {
    std::string latestVersion;
    std::string error;

    if (m_shutdownRequested) {
        return;
    }

    bool success = fetchLatestRelease(latestVersion, error);

    if (m_shutdownRequested) {
        return;
    }

    if (success) {
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            m_latestVersion = latestVersion;
        }

        // Compare versions (debug mode forces update available)
        // Log both versions for easier debugging regardless of result
        int cmp = compareVersions(latestVersion, PluginConstants::PLUGIN_VERSION);
        DEBUG_INFO_F("UpdateChecker: Version comparison - Latest: %s, Current: %s, Result: %d",
                    latestVersion.c_str(), PluginConstants::PLUGIN_VERSION, cmp);

        if (cmp > 0 || m_debugMode) {
            m_status = Status::UPDATE_AVAILABLE;
            if (m_debugMode && cmp <= 0) {
                DEBUG_INFO_F("UpdateChecker: DEBUG MODE - Forcing update available");
            } else {
                DEBUG_INFO_F("UpdateChecker: Update available!");
            }
        } else {
            m_status = Status::UP_TO_DATE;
            DEBUG_INFO_F("UpdateChecker: Up to date");
        }
    } else {
        m_status = Status::CHECK_FAILED;
        DEBUG_WARN_F("UpdateChecker: Check failed - %s", error.c_str());
    }

    // Call completion callback if set
    std::function<void()> callback;
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        callback = m_completionCallback;
    }
    if (callback) {
        callback();
    }
}

bool UpdateChecker::parseVersion(const std::string& version, int& major, int& minor, int& patch, int& build) {
    // Handle versions with or without 'v' prefix
    std::string ver = version;
    if (!ver.empty() && (ver[0] == 'v' || ver[0] == 'V')) {
        ver = ver.substr(1);
    }

    // Strip suffix after hyphen (e.g., "1.11.0.0-beta1" -> "1.11.0.0")
    size_t hyphen = ver.find('-');
    if (hyphen != std::string::npos) {
        ver = ver.substr(0, hyphen);
    }

    major = minor = patch = build = 0;

    std::istringstream iss(ver);
    char dot;

    if (!(iss >> major)) return false;
    if (iss.peek() == '.') {
        iss >> dot;
        if (!(iss >> minor)) return false;
    }
    if (iss.peek() == '.') {
        iss >> dot;
        if (!(iss >> patch)) return false;
    }
    if (iss.peek() == '.') {
        iss >> dot;
        if (!(iss >> build)) return false;
    }

    return true;
}

int UpdateChecker::compareVersions(const std::string& a, const std::string& b) {
    int aMajor, aMinor, aPatch, aBuild;
    int bMajor, bMinor, bPatch, bBuild;

    if (!parseVersion(a, aMajor, aMinor, aPatch, aBuild)) return 0;
    if (!parseVersion(b, bMajor, bMinor, bPatch, bBuild)) return 0;

    if (aMajor != bMajor) return aMajor > bMajor ? 1 : -1;
    if (aMinor != bMinor) return aMinor > bMinor ? 1 : -1;
    if (aPatch != bPatch) return aPatch > bPatch ? 1 : -1;
    if (aBuild != bBuild) return aBuild > bBuild ? 1 : -1;

    return 0;
}

bool UpdateChecker::fetchLatestRelease(std::string& outVersion, std::string& outError) {
    // Create user agent string
    char userAgentA[128];
    snprintf(userAgentA, sizeof(userAgentA), "%s/%s",
             PluginConstants::PLUGIN_DISPLAY_NAME,
             PluginConstants::PLUGIN_VERSION);
    std::wstring userAgent(userAgentA, userAgentA + strlen(userAgentA));

    // Construct API path from centralized repo constants (supports repo moves/renames)
    // Use /releases endpoint to get array of releases (supports prerelease filtering)
    std::string apiPath = "/repos/";
    apiPath += PluginConstants::GITHUB_REPO_OWNER;
    apiPath += "/";
    apiPath += PluginConstants::GITHUB_REPO_NAME;
    apiPath += "/releases?per_page=15";

    // Wide string versions of host and path
    std::wstring wHost(GITHUB_API_HOST, GITHUB_API_HOST + strlen(GITHUB_API_HOST));
    std::wstring wPath(apiPath.begin(), apiPath.end());

    // Initialize WinHTTP - create all handles (non-blocking operations)
    HINTERNET hSession = WinHttpOpen(userAgent.c_str(),
                                     WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,
                                     WINHTTP_NO_PROXY_NAME,
                                     WINHTTP_NO_PROXY_BYPASS, 0);
    if (!hSession) {
        outError = "WinHttpOpen failed";
        return false;
    }

    // Set timeouts (10 seconds)
    WinHttpSetTimeouts(hSession, 10000, 10000, 10000, 10000);

    // Connect to server (HTTPS port 443)
    HINTERNET hConnect = WinHttpConnect(hSession, wHost.c_str(), INTERNET_DEFAULT_HTTPS_PORT, 0);
    if (!hConnect) {
        WinHttpCloseHandle(hSession);
        outError = "Connection failed";
        return false;
    }

    // Create request (HTTPS)
    HINTERNET hRequest = WinHttpOpenRequest(hConnect, L"GET", wPath.c_str(),
                                            NULL, WINHTTP_NO_REFERER,
                                            WINHTTP_DEFAULT_ACCEPT_TYPES,
                                            WINHTTP_FLAG_SECURE);
    if (!hRequest) {
        WinHttpCloseHandle(hConnect);
        WinHttpCloseHandle(hSession);
        outError = "Request creation failed";
        return false;
    }

    // GitHub API requires User-Agent and Accept headers
    WinHttpAddRequestHeaders(hRequest, L"Accept: application/vnd.github+json",
                             (DWORD)-1, WINHTTP_ADDREQ_FLAG_ADD);

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
        closeHttpHandles();
        outError = "Send failed";
        return false;
    }

    bResults = WinHttpReceiveResponse(hRequest, NULL);
    if (!bResults) {
        closeHttpHandles();
        outError = "No response";
        return false;
    }

    // Check status code
    DWORD statusCode = 0;
    DWORD statusCodeSize = sizeof(statusCode);
    WinHttpQueryHeaders(hRequest, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
                        WINHTTP_HEADER_NAME_BY_INDEX, &statusCode, &statusCodeSize, WINHTTP_NO_HEADER_INDEX);

    if (statusCode != 200) {
        closeHttpHandles();
        outError = "HTTP " + std::to_string(statusCode);
        return false;
    }

    // Read response body
    std::string responseBody;
    DWORD dwSize = 0;
    DWORD dwDownloaded = 0;
    constexpr size_t MAX_RESPONSE_SIZE = 256 * 1024;  // 256KB limit (array of releases)

    do {
        if (m_shutdownRequested) break;

        dwSize = 0;
        if (!WinHttpQueryDataAvailable(hRequest, &dwSize)) {
            break;
        }

        if (dwSize == 0) break;

        // Check size limit
        if (responseBody.size() + dwSize > MAX_RESPONSE_SIZE) {
            closeHttpHandles();
            outError = "Response too large";
            return false;
        }

        std::vector<char> buffer(dwSize + 1, 0);

        if (WinHttpReadData(hRequest, buffer.data(), dwSize, &dwDownloaded)) {
            responseBody.append(buffer.data(), dwDownloaded);
        }

    } while (dwSize > 0);

    closeHttpHandles();

    // Parse JSON response (array of releases)
    try {
        nlohmann::json releases = nlohmann::json::parse(responseBody);

        if (!releases.is_array() || releases.empty()) {
            outError = "No releases found";
            return false;
        }

        // Find best matching release based on channel
        UpdateChannel channel = m_channel.load();
        nlohmann::json* selectedRelease = nullptr;

        for (auto& release : releases) {
            // Skip releases without tag_name
            if (!release.contains("tag_name") || !release["tag_name"].is_string()) {
                continue;
            }
            // Skip drafts
            if (release.contains("draft") && release["draft"].get<bool>()) {
                continue;
            }

            bool isPrerelease = release.contains("prerelease") && release["prerelease"].get<bool>();

            // Stable channel skips prereleases
            if (channel == UpdateChannel::STABLE && isPrerelease) {
                continue;
            }

            // First matching release is the newest (GitHub returns newest first)
            selectedRelease = &release;
            break;
        }

        if (!selectedRelease) {
            outError = "No suitable release found";
            return false;
        }

        // Extract data from selected release
        nlohmann::json& j = *selectedRelease;
        outVersion = j["tag_name"].get<std::string>();
        bool isPrerelease = j.contains("prerelease") && j["prerelease"].get<bool>();

        // Extract release notes (body field)
        std::string releaseNotes;
        if (j.contains("body") && j["body"].is_string()) {
            releaseNotes = j["body"].get<std::string>();
        }

        // Extract asset info (find first .zip file)
        std::string downloadUrl;
        std::string assetName;
        size_t downloadSize = 0;
        std::string checksumHash;

        if (j.contains("assets") && j["assets"].is_array()) {
            for (const auto& asset : j["assets"]) {
                if (asset.contains("name") && asset["name"].is_string()) {
                    std::string name = asset["name"].get<std::string>();
                    // Look for .zip file
                    if (name.size() >= 4 && name.substr(name.size() - 4) == ".zip") {
                        assetName = name;
                        if (asset.contains("browser_download_url") && asset["browser_download_url"].is_string()) {
                            downloadUrl = asset["browser_download_url"].get<std::string>();
                        }
                        if (asset.contains("size") && asset["size"].is_number()) {
                            downloadSize = asset["size"].get<size_t>();
                        }
                        // Extract SHA256 from digest field (format: "sha256:abc123...")
                        if (asset.contains("digest") && asset["digest"].is_string()) {
                            std::string digest = asset["digest"].get<std::string>();
                            if (digest.substr(0, 7) == "sha256:") {
                                checksumHash = digest.substr(7);
                            }
                        }
                        break;  // Use first .zip found
                    }
                }
            }
        }

        // Store the additional data under mutex
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            m_releaseNotes = releaseNotes;
            m_downloadUrl = downloadUrl;
            m_assetName = assetName;
            m_downloadSize = downloadSize;
            m_checksumHash = checksumHash;
            m_latestIsPrerelease = isPrerelease;
        }

        DEBUG_INFO_F("UpdateChecker: Parsed release %s%s, notes: %zu chars, asset: %s (%zu bytes), checksum: %s",
                    outVersion.c_str(), isPrerelease ? " (prerelease)" : "",
                    releaseNotes.size(), assetName.c_str(), downloadSize,
                    checksumHash.empty() ? "none" : checksumHash.substr(0, 16).c_str());

        return true;
    } catch (const nlohmann::json::exception& e) {
        outError = std::string("JSON parse error: ") + e.what();
        return false;
    }
}
