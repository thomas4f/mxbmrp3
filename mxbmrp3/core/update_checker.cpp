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
    , m_enabled(false)  // Off by default
    , m_shutdownRequested(false)
{
}

UpdateChecker::~UpdateChecker() {
    shutdown();
}

void UpdateChecker::shutdown() {
    m_shutdownRequested = true;
    if (m_workerThread.joinable()) {
        m_workerThread.join();
    }
}

void UpdateChecker::checkForUpdates() {
    // Don't start another check if one is in progress
    if (m_status == Status::CHECKING) {
        return;
    }

    // Wait for any previous thread to complete
    if (m_workerThread.joinable()) {
        m_workerThread.join();
    }

    m_status = Status::CHECKING;
    m_shutdownRequested = false;

    // Start worker thread
    m_workerThread = std::thread(&UpdateChecker::workerThread, this);
}

std::string UpdateChecker::getLatestVersion() const {
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_latestVersion;
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

        // Compare versions
        int cmp = compareVersions(latestVersion, PluginConstants::PLUGIN_VERSION);
        if (cmp > 0) {
            m_status = Status::UPDATE_AVAILABLE;
            DEBUG_INFO_F("UpdateChecker: Update available! Latest: %s, Current: %s",
                        latestVersion.c_str(), PluginConstants::PLUGIN_VERSION);
        } else {
            m_status = Status::UP_TO_DATE;
            DEBUG_INFO_F("UpdateChecker: Up to date (v%s)", PluginConstants::PLUGIN_VERSION);
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

    // Wide string versions of host and path
    std::wstring wHost(GITHUB_API_HOST, GITHUB_API_HOST + strlen(GITHUB_API_HOST));
    std::wstring wPath(GITHUB_RELEASES_PATH, GITHUB_RELEASES_PATH + strlen(GITHUB_RELEASES_PATH));

    // Initialize WinHTTP
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

    // Send request
    BOOL bResults = WinHttpSendRequest(hRequest, WINHTTP_NO_ADDITIONAL_HEADERS,
                                        0, WINHTTP_NO_REQUEST_DATA, 0, 0, 0);
    if (!bResults) {
        WinHttpCloseHandle(hRequest);
        WinHttpCloseHandle(hConnect);
        WinHttpCloseHandle(hSession);
        outError = "Send failed";
        return false;
    }

    // Receive response
    bResults = WinHttpReceiveResponse(hRequest, NULL);
    if (!bResults) {
        WinHttpCloseHandle(hRequest);
        WinHttpCloseHandle(hConnect);
        WinHttpCloseHandle(hSession);
        outError = "No response";
        return false;
    }

    // Check status code
    DWORD statusCode = 0;
    DWORD statusCodeSize = sizeof(statusCode);
    WinHttpQueryHeaders(hRequest, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
                        WINHTTP_HEADER_NAME_BY_INDEX, &statusCode, &statusCodeSize, WINHTTP_NO_HEADER_INDEX);

    if (statusCode != 200) {
        WinHttpCloseHandle(hRequest);
        WinHttpCloseHandle(hConnect);
        WinHttpCloseHandle(hSession);
        outError = "HTTP " + std::to_string(statusCode);
        return false;
    }

    // Read response body
    std::string responseBody;
    DWORD dwSize = 0;
    DWORD dwDownloaded = 0;
    constexpr size_t MAX_RESPONSE_SIZE = 64 * 1024;  // 64KB limit

    do {
        dwSize = 0;
        if (!WinHttpQueryDataAvailable(hRequest, &dwSize)) {
            break;
        }

        if (dwSize == 0) break;

        // Check size limit
        if (responseBody.size() + dwSize > MAX_RESPONSE_SIZE) {
            WinHttpCloseHandle(hRequest);
            WinHttpCloseHandle(hConnect);
            WinHttpCloseHandle(hSession);
            outError = "Response too large";
            return false;
        }

        std::vector<char> buffer(dwSize + 1, 0);

        if (WinHttpReadData(hRequest, buffer.data(), dwSize, &dwDownloaded)) {
            responseBody.append(buffer.data(), dwDownloaded);
        }

    } while (dwSize > 0);

    // Clean up
    WinHttpCloseHandle(hRequest);
    WinHttpCloseHandle(hConnect);
    WinHttpCloseHandle(hSession);

    // Parse JSON response
    try {
        nlohmann::json j = nlohmann::json::parse(responseBody);

        if (j.contains("tag_name") && j["tag_name"].is_string()) {
            outVersion = j["tag_name"].get<std::string>();
            return true;
        } else {
            outError = "No tag_name in response";
            return false;
        }
    } catch (const nlohmann::json::exception& e) {
        outError = std::string("JSON parse error: ") + e.what();
        return false;
    }
}
