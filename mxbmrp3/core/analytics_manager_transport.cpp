// ============================================================================
// core/analytics_manager_transport.cpp
// AnalyticsManager network transport + remote config — the WinHTTP senders
// (postBeacon / postAptabase / postSync / sendGoatCounterHit), handle cleanup,
// and the remote sampling-config fetch/apply. Extracted verbatim from
// analytics_manager.cpp when that file grew past ~1.2k lines; the class,
// members, and public API are unchanged. The dry-run test flag they read
// (s_testCaptureMode) is now a shared static member (see analytics_manager.h).
// ============================================================================
#include "analytics_manager.h"
#include "analytics_remote_config.h"
#include "analytics_endpoint.h"
#include "atomic_file_writer.h"
#include "plugin_constants.h"
#include "settings_manager.h"
#include "hud_manager.h"
#include "xinput_reader.h"
#include "director_manager.h"
#include "update_checker.h"
#include "profile_manager.h"
#include "ui_config.h"
#include "../hud/helmet_overlay_hud.h"
#include "../game/game_config.h"
#include "../diagnostics/logger.h"
#include "../vendor/nlohmann/json.hpp"

#if GAME_HAS_DISCORD
    #include "discord_manager.h"
#endif
#if GAME_HAS_STEAM_FRIENDS
    #include "steam_friends_manager.h"
#endif
#if GAME_HAS_HTTP_SERVER
    #include "http_server.h"
#endif

#include <windows.h>
#include <winhttp.h>
#include <bcrypt.h>
#include <fstream>
#include <vector>
#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <random>

#pragma comment(lib, "winhttp.lib")
#pragma comment(lib, "bcrypt.lib")

void AnalyticsManager::postBeacon(std::wstring host, std::string body) {
    // Thread entry. Exception barrier: an uncaught throw in a std::thread calls
    // std::terminate() and kills the host game. Sends the Aptabase app_started
    // POST (when configured), then the GoatCounter headcount GET — the two are
    // independent so a failure of one never blocks the other.
    try {
        if (m_shutdownRequested) return;
        if (!host.empty() && !body.empty()) {
            // Decide this launch's tier (full vs app_started+crash only) BEFORE the beacon,
            // so a later session_end/custom on the game thread reads a settled m_fullLaunch.
            // app_started itself is sent regardless — it's the always-on minimal tier.
            applyRemoteSampling();
            if (!m_shutdownRequested) postAptabase(host, body);
        }
        if (!m_shutdownRequested) sendGoatCounterHit();
    } catch (const std::exception& e) {
        DEBUG_WARN_F("AnalyticsManager: beacon thread exception: %s", e.what());
    } catch (...) {
        DEBUG_WARN("AnalyticsManager: beacon thread unknown exception");
    }
}

void AnalyticsManager::postAptabase(const std::wstring& host, const std::string& body) {
#if defined(MXBMRP3_TEST_BUILD)
    if (s_testCaptureMode) return;   // dry-run: a test build never sends
#endif
    {
        HINTERNET hSession = WinHttpOpen(L"mxbmrp3-analytics",
                                         WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,
                                         WINHTTP_NO_PROXY_NAME,
                                         WINHTTP_NO_PROXY_BYPASS, 0);
        if (!hSession) return;
        WinHttpSetTimeouts(hSession, 8000, 8000, 8000, 8000);

        HINTERNET hConnect = WinHttpConnect(hSession, host.c_str(),
                                            INTERNET_DEFAULT_HTTPS_PORT, 0);
        if (!hConnect) { WinHttpCloseHandle(hSession); return; }

        HINTERNET hRequest = WinHttpOpenRequest(hConnect, L"POST", L"/api/v0/events",
                                                NULL, WINHTTP_NO_REFERER,
                                                WINHTTP_DEFAULT_ACCEPT_TYPES,
                                                WINHTTP_FLAG_SECURE);
        if (!hRequest) {
            WinHttpCloseHandle(hConnect);
            WinHttpCloseHandle(hSession);
            return;
        }

        // Publish handles so shutdown() can cancel the blocking send by closing
        // them (CancelSynchronousIo does not work with WinHTTP).
        {
            std::lock_guard<std::mutex> lock(m_handleMutex);
            m_hSession = hSession;
            m_hConnect = hConnect;
            m_hRequest = hRequest;
        }
        if (m_shutdownRequested) { closeHandles(); return; }

        std::wstring headers = L"Content-Type: application/json\r\nApp-Key: " + m_wAppKey;

        // Log the outgoing request for diagnostics. The payload is anonymous and
        // this is the player's own local log, so logging the full body is safe.
        {
            std::string hostA;
            for (wchar_t c : host) hostA += static_cast<char>(c);  // host is ASCII
            DEBUG_INFO_F("AnalyticsManager: POST https://%s/api/v0/events (%zu bytes): %s",
                         hostA.c_str(), body.size(), body.c_str());
        }

        BOOL ok = WinHttpSendRequest(hRequest, headers.c_str(), (DWORD)-1L,
                                     (LPVOID)body.data(), (DWORD)body.size(),
                                     (DWORD)body.size(), 0);
        if (ok) ok = WinHttpReceiveResponse(hRequest, NULL);

        DWORD status = 0;
        if (ok) {
            DWORD size = sizeof(status);
            WinHttpQueryHeaders(hRequest,
                                WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
                                WINHTTP_HEADER_NAME_BY_INDEX, &status, &size,
                                WINHTTP_NO_HEADER_INDEX);

            // Read the (small) response body so the log shows exactly what
            // Aptabase replied — "{}" means accepted; anything else is a clue.
            std::string response;
            DWORD avail = 0;
            while (response.size() < 4096 &&
                   WinHttpQueryDataAvailable(hRequest, &avail) && avail > 0) {
                std::vector<char> buf(avail + 1, 0);
                DWORD read = 0;
                if (!WinHttpReadData(hRequest, buf.data(), avail, &read) || read == 0) break;
                response.append(buf.data(), read);
            }

            if (status == 200) {
                DEBUG_INFO_F("AnalyticsManager: beacon accepted (HTTP 200, response: %s)",
                             response.c_str());
            } else {
                DEBUG_WARN_F("AnalyticsManager: beacon rejected (HTTP %lu, response: %s)",
                             status, response.c_str());
            }
        } else if (!m_shutdownRequested) {
            DEBUG_WARN_F("AnalyticsManager: beacon send failed (WinHTTP error %lu)", GetLastError());
        }

        closeHandles();
    }
}

void AnalyticsManager::closeHandles() {
    std::lock_guard<std::mutex> lock(m_handleMutex);
    // Close child handles before parent (WinHTTP documented best practice).
    if (m_hRequest) { WinHttpCloseHandle(m_hRequest); m_hRequest = nullptr; }
    if (m_hConnect) { WinHttpCloseHandle(m_hConnect); m_hConnect = nullptr; }
    if (m_hSession) { WinHttpCloseHandle(m_hSession); m_hSession = nullptr; }
}

void AnalyticsManager::sendGoatCounterHit() {
#if defined(MXBMRP3_TEST_BUILD)
    if (s_testCaptureMode) return;   // dry-run: a test build never sends
#endif
    if (m_gcHost.empty() || m_gcBody.empty()) return;

    HINTERNET hSession = WinHttpOpen(L"mxbmrp3-analytics",
                                     WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,
                                     WINHTTP_NO_PROXY_NAME,
                                     WINHTTP_NO_PROXY_BYPASS, 0);
    if (!hSession) return;
    WinHttpSetTimeouts(hSession, 8000, 8000, 8000, 8000);

    HINTERNET hConnect = WinHttpConnect(hSession, m_gcHost.c_str(),
                                        INTERNET_DEFAULT_HTTPS_PORT, 0);
    if (!hConnect) { WinHttpCloseHandle(hSession); return; }

    HINTERNET hRequest = WinHttpOpenRequest(hConnect, L"POST", L"/api/v0/count",
                                            NULL, WINHTTP_NO_REFERER,
                                            WINHTTP_DEFAULT_ACCEPT_TYPES,
                                            WINHTTP_FLAG_SECURE);
    if (!hRequest) {
        WinHttpCloseHandle(hConnect);
        WinHttpCloseHandle(hSession);
        return;
    }

    // Publish handles so shutdown() can cancel the POST. Safe to reuse the slots:
    // the Aptabase POST (if any) has already finished and cleared them.
    {
        std::lock_guard<std::mutex> lock(m_handleMutex);
        m_hSession = hSession;
        m_hConnect = hConnect;
        m_hRequest = hRequest;
    }
    if (m_shutdownRequested) { closeHandles(); return; }

    std::wstring tokenW;
    {
        const char* t = PluginConstants::GOATCOUNTER_TOKEN;
        tokenW.assign(t, t + std::strlen(t));  // ASCII
    }
    std::wstring headers = L"Content-Type: application/json\r\nAuthorization: Bearer " + tokenW;

    BOOL ok = WinHttpSendRequest(hRequest, headers.c_str(), (DWORD)-1L,
                                 (LPVOID)m_gcBody.data(), (DWORD)m_gcBody.size(),
                                 (DWORD)m_gcBody.size(), 0);
    if (ok) ok = WinHttpReceiveResponse(hRequest, NULL);

    DWORD status = 0;
    if (ok) {
        DWORD size = sizeof(status);
        WinHttpQueryHeaders(hRequest, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
                            WINHTTP_HEADER_NAME_BY_INDEX, &status, &size, WINHTTP_NO_HEADER_INDEX);
        // 202 Accepted is the documented success for /api/v0/count.
        DEBUG_INFO_F("AnalyticsManager: GoatCounter hit (HTTP %lu)", status);
    } else if (!m_shutdownRequested) {
        DEBUG_INFO_F("AnalyticsManager: GoatCounter hit failed (WinHTTP error %lu)", GetLastError());
    }

    closeHandles();
}

void AnalyticsManager::applyRemoteSampling() {
    // Developer cost lever (Aptabase bills per event). Fetch the public config file and
    // read aptabase_full_sample ∈ [0,1]: the fraction of launches that send the FULL set
    // (session_end + custom) on top of the always-sent app_started (+ crash). Roll once to
    // decide THIS launch. Fail-open: any fetch/parse failure leaves sample at 1.0 (full),
    // so a GitHub outage or a typo can never silently blind analytics.
    std::string body;
    double sample = 1.0;
    if (fetchRemoteConfig(body)) {
        sample = AnalyticsRemoteConfig::parseFullSample(body);
    }
    double roll = 0.0;
    if (sample > 0.0 && sample < 1.0) {
        // Only the strictly-between case needs randomness; the 0.0/1.0 endpoints are
        // deterministic (see shouldSendFull), so the binary switch is exact.
        std::random_device rd;
        std::mt19937 gen(rd());
        std::uniform_real_distribution<double> dist(0.0, 1.0);
        roll = dist(gen);
    }
    const bool full = AnalyticsRemoteConfig::shouldSendFull(sample, roll);
    m_fullLaunch.store(full);
    DEBUG_INFO_F("AnalyticsManager: remote aptabase_full_sample=%.3f -> this launch is %s",
                 sample, full ? "FULL" : "minimal (app_started + crash only)");
}

bool AnalyticsManager::fetchRemoteConfig(std::string& out) {
    out.clear();
#if defined(MXBMRP3_TEST_BUILD)
    if (s_testCaptureMode) return false;   // dry-run: no fetch (caller fail-opens to full)
#endif

    std::wstring host;
    { const char* h = PluginConstants::ANALYTICS_CONFIG_HOST; host.assign(h, h + std::strlen(h)); }
    // Path: /<owner>/<repo>/<branch>/<file> on raw.githubusercontent.com (public repo).
    std::string pathA = std::string("/") + PluginConstants::GITHUB_REPO_OWNER + "/" +
                        PluginConstants::GITHUB_REPO_NAME + "/" +
                        PluginConstants::ANALYTICS_CONFIG_BRANCH + "/" +
                        PluginConstants::ANALYTICS_CONFIG_FILE;
    std::wstring path(pathA.begin(), pathA.end());   // ASCII path

    HINTERNET hSession = WinHttpOpen(L"mxbmrp3-analytics",
                                     WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,
                                     WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
    if (!hSession) return false;
    WinHttpSetTimeouts(hSession, 5000, 5000, 5000, 5000);   // short: never stall startup

    HINTERNET hConnect = WinHttpConnect(hSession, host.c_str(), INTERNET_DEFAULT_HTTPS_PORT, 0);
    if (!hConnect) { WinHttpCloseHandle(hSession); return false; }

    HINTERNET hRequest = WinHttpOpenRequest(hConnect, L"GET", path.c_str(),
                                            NULL, WINHTTP_NO_REFERER,
                                            WINHTTP_DEFAULT_ACCEPT_TYPES, WINHTTP_FLAG_SECURE);
    if (!hRequest) { WinHttpCloseHandle(hConnect); WinHttpCloseHandle(hSession); return false; }

    // Publish handles so shutdown() can cancel this GET (reused by the beacon after).
    {
        std::lock_guard<std::mutex> lock(m_handleMutex);
        m_hSession = hSession; m_hConnect = hConnect; m_hRequest = hRequest;
    }
    if (m_shutdownRequested) { closeHandles(); return false; }

    BOOL ok = WinHttpSendRequest(hRequest, WINHTTP_NO_ADDITIONAL_HEADERS, 0,
                                 WINHTTP_NO_REQUEST_DATA, 0, 0, 0);
    if (ok) ok = WinHttpReceiveResponse(hRequest, NULL);

    DWORD status = 0;
    if (ok) {
        DWORD size = sizeof(status);
        WinHttpQueryHeaders(hRequest, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
                            WINHTTP_HEADER_NAME_BY_INDEX, &status, &size, WINHTTP_NO_HEADER_INDEX);
        DWORD avail = 0;
        while (out.size() < 8192 &&
               WinHttpQueryDataAvailable(hRequest, &avail) && avail > 0) {
            std::vector<char> buf(avail + 1, 0);
            DWORD read = 0;
            if (!WinHttpReadData(hRequest, buf.data(), avail, &read) || read == 0) break;
            out.append(buf.data(), read);
        }
    } else if (!m_shutdownRequested) {
        DEBUG_INFO_F("AnalyticsManager: remote config fetch failed (WinHTTP error %lu)", GetLastError());
    }

    closeHandles();
    // A non-200 (e.g. 404 while the file doesn't exist yet) is a miss -> caller fail-opens.
    if (status != 200) { out.clear(); return false; }
    return !out.empty();
}

void AnalyticsManager::postSync(const std::wstring& host, const std::string& body,
                                unsigned long timeoutMs) {
#if defined(MXBMRP3_TEST_BUILD)
    if (s_testCaptureMode) return;   // dry-run: a test build never sends
#endif
    // Self-contained synchronous POST with local handles, used by the custom-
    // event worker. Short timeout so a slow send can't stall game shutdown for
    // long; not tied to the cancellation handles.
    HINTERNET hSession = WinHttpOpen(L"mxbmrp3-analytics",
                                     WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,
                                     WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
    if (!hSession) return;
    WinHttpSetTimeouts(hSession, timeoutMs, timeoutMs, timeoutMs, timeoutMs);

    HINTERNET hConnect = WinHttpConnect(hSession, host.c_str(),
                                        INTERNET_DEFAULT_HTTPS_PORT, 0);
    if (!hConnect) { WinHttpCloseHandle(hSession); return; }

    HINTERNET hRequest = WinHttpOpenRequest(hConnect, L"POST", L"/api/v0/events",
                                            NULL, WINHTTP_NO_REFERER,
                                            WINHTTP_DEFAULT_ACCEPT_TYPES, WINHTTP_FLAG_SECURE);
    if (!hRequest) { WinHttpCloseHandle(hConnect); WinHttpCloseHandle(hSession); return; }

    std::wstring headers = L"Content-Type: application/json\r\nApp-Key: " + m_wAppKey;
    BOOL ok = WinHttpSendRequest(hRequest, headers.c_str(), (DWORD)-1L,
                                 (LPVOID)body.data(), (DWORD)body.size(),
                                 (DWORD)body.size(), 0);
    if (ok) ok = WinHttpReceiveResponse(hRequest, NULL);

    DWORD status = 0;
    if (ok) {
        DWORD size = sizeof(status);
        WinHttpQueryHeaders(hRequest, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
                            WINHTTP_HEADER_NAME_BY_INDEX, &status, &size, WINHTTP_NO_HEADER_INDEX);
        DEBUG_INFO_F("AnalyticsManager: event POST (HTTP %lu)", status);
    } else {
        DEBUG_INFO_F("AnalyticsManager: event POST failed (WinHTTP error %lu)", GetLastError());
    }

    WinHttpCloseHandle(hRequest);
    WinHttpCloseHandle(hConnect);
    WinHttpCloseHandle(hSession);
}
