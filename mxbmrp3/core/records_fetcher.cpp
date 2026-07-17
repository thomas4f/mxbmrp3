// ============================================================================
// core/records_fetcher.cpp
// Background HTTP fetch + JSON parse for the external lap-records providers.
// Extracted from hud/records_hud.cpp (which keeps presentation only); the
// transport/parse bodies moved here verbatim — see records_fetcher.h for the
// threading contract and the GAME_HAS_RECORDS_PROVIDER gating note.
// ============================================================================
#include "records_fetcher.h"

#include <cstdio>
#include <cstring>
#include <mutex>

#include "../diagnostics/logger.h"
#include "plugin_constants.h"

// Use WinHTTP for HTTPS support (built into Windows, no external dependencies)
#include <windows.h>
#include <winhttp.h>
#pragma comment(lib, "winhttp.lib")

#include "../vendor/nlohmann/json.hpp"

// ============================================================================
// Lifecycle
// ============================================================================

RecordsFetcher::~RecordsFetcher() {
    // Safety net — the owner (RecordsHud) joins explicitly first, via the
    // HudManager::clear() ordering contract (see records_fetcher.h).
    join();
}

void RecordsFetcher::join() {
    if (m_thread.joinable()) {
        m_thread.join();
    }
}

void RecordsFetcher::start(DataProvider provider, std::string trackName,
                           std::string category, ResultCallback onDone) {
    // Wait for previous thread if any (the caller's FetchState gate guarantees
    // it is no longer running — this just reaps the finished handle).
    join();

    // Snapshot the fetch inputs the caller captured on the game thread; from
    // here on only the worker reads them.
    m_provider = provider;
    m_trackName = std::move(trackName);
    m_category = std::move(category);
    m_onDone = std::move(onDone);

    // Start new fetch thread
    m_thread = std::thread(&RecordsFetcher::performFetch, this);
}

// ============================================================================
// Provider configuration
// ============================================================================

std::string RecordsFetcher::getProviderBaseUrl(DataProvider provider) {
    switch (provider) {
        case DataProvider::CBR:
            return "https://server.cbrservers.com/api/records/top";
        case DataProvider::MXB_RANKED:
            return "https://mxb-ranked.com/pub-api/stats/GetTrackFastestLapsByBikeCategory";
        default:
            return "";
    }
}

const char* RecordsFetcher::getProviderDisplayName(DataProvider provider) {
    switch (provider) {
        case DataProvider::CBR:
            return "CBR";
        case DataProvider::MXB_RANKED:
            return "MXB Ranked";
        default:
            return "Unknown";
    }
}

// ============================================================================
// HTTP Fetch Operations
// ============================================================================

// URL encode a string for use in query parameters
static void appendUrlEncoded(std::string& url, const char* str) {
    for (const char* p = str; *p; ++p) {
        if ((*p >= 'a' && *p <= 'z') || (*p >= 'A' && *p <= 'Z') ||
            (*p >= '0' && *p <= '9') || *p == '-' || *p == '_' || *p == '.') {
            url += *p;
        } else if (*p == ' ') {
            url += "%20";
        } else {
            char hex[4];
            snprintf(hex, sizeof(hex), "%%%02X", static_cast<unsigned char>(*p));
            url += hex;
        }
    }
}

std::string RecordsFetcher::buildRequestUrl() const {
    // Runs on the worker thread: read only the fetch-time snapshot captured by
    // RecordsHud::startFetch() on the game thread and passed into start()
    // (m_provider / m_trackName / m_category), never live PluginData or the
    // HUD's m_provider/m_categoryIndex/m_categoryList.
    std::string baseUrl = getProviderBaseUrl(m_provider);
    if (baseUrl.empty()) return "";

    if (m_provider == DataProvider::MXB_RANKED) {
        // MXB-Ranked uses path-based URL: /trackname or /trackname/category
        // No limit parameter supported
        std::string url = baseUrl;

        // Add track name parameter (required)
        if (!m_trackName.empty()) {
            url += "/";
            appendUrlEncoded(url, m_trackName.c_str());
        } else {
            return "";  // Track name is required for MXB-Ranked
        }

        // Add category parameter (optional - omit for "All")
        if (!m_category.empty()) {
            url += "/";
            appendUrlEncoded(url, m_category.c_str());
        }
        // If "All", omit category to get all categories

        return url;
    } else {
        // CBR uses query parameters
        std::string url = baseUrl + "?limit=" + std::to_string(MAX_RECORDS);

        // Add track parameter
        if (!m_trackName.empty()) {
            url += "&track=";
            appendUrlEncoded(url, m_trackName.c_str());
        }

        // Add category parameter if not "All"
        if (!m_category.empty()) {
            url += "&category=";
            appendUrlEncoded(url, m_category.c_str());
        }

        return url;
    }
}

#if defined(MXBMRP3_TEST_BUILD)
// Test-only fetch-worker stub state (see MXBMRP3_Test_RecordsSetFetchStub):
// armed on the test/game thread, read by the fetch worker. Guarded so the
// worker never reads a half-written response string.
static std::mutex s_testStubMutex;
static int s_testStubDelayMs = -1;
static std::string s_testStubResponse;

void RecordsFetcher::testSetFetchStub(int delayMs, const char* response) {
    std::lock_guard<std::mutex> lock(s_testStubMutex);
    s_testStubDelayMs = delayMs;
    s_testStubResponse = response ? response : "";
}

static bool testReadFetchStub(int& delayMs, std::string& response) {
    std::lock_guard<std::mutex> lock(s_testStubMutex);
    if (s_testStubDelayMs < 0) return false;
    delayMs = s_testStubDelayMs;
    response = s_testStubResponse;
    return true;
}
#endif

void RecordsFetcher::completeError(const char* what) noexcept {
    // The error-string/callback path can itself throw bad_alloc — if the
    // original failure was bad_alloc, allocating a new error string would
    // defeat the barrier. Build what we can and never let a throw escape the
    // worker thread (std::terminate).
    try {
        Result r;
        r.provider = m_provider;
        try {
            r.error = what ? what : "";
        } catch (...) {
            // keep the error empty (the owner clears its message)  // noexcept
        }
        if (m_onDone) m_onDone(std::move(r));
    } catch (...) {
        // The owner's callback is written not to throw (atomics + a guarded
        // string copy under its lock); swallow anyway — see above.
    }
}

void RecordsFetcher::performFetch() {
    // Exception barrier: an uncaught throw in a std::thread calls
    // std::terminate() and kills the host game process. The body below does
    // std::string / std::wstring / std::vector allocations and WinHTTP calls
    // — any of those can throw under memory pressure or unexpected response
    // shapes. parseResponse is internally guarded, but the outer thread
    // body wasn't.
    try {

#if defined(MXBMRP3_TEST_BUILD)
        // Test-only stubbed fetch: sleep, then complete through the normal
        // parse/notify path (including the owner's cross-HUD TimingHud touch
        // that the join-before-null contract in HudManager::clear() protects)
        // with a canned response instead of any network I/O.
        {
            int stubDelayMs = -1;
            std::string stubResponse;
            if (testReadFetchStub(stubDelayMs, stubResponse)) {
                ::Sleep(static_cast<DWORD>(stubDelayMs));
                Result r;
                r.provider = m_provider;
                if (parseResponse(m_provider, stubResponse, r)) {
                    r.parsed = true;
                } else {
                    r.error = "Parse error";
                }
                if (m_onDone) m_onDone(std::move(r));
                return;
            }
        }
#endif

        DEBUG_INFO("RecordsFetcher: Starting HTTP fetch");

        std::string url = buildRequestUrl();
        if (url.empty()) {
            completeError("Invalid URL");
            return;
        }

        DEBUG_INFO_F("RecordsFetcher: Fetching URL: %s", url.c_str());

        // Parse URL to extract host and path
        std::wstring wHost;
        std::wstring wPath;
        INTERNET_PORT port = INTERNET_DEFAULT_HTTPS_PORT;
        bool useHttps = true;

        // Skip protocol
        size_t hostStart = url.find("://");
        if (hostStart != std::string::npos) {
            std::string protocol = url.substr(0, hostStart);
            if (protocol == "http") {
                useHttps = false;
                port = INTERNET_DEFAULT_HTTP_PORT;
            }
            hostStart += 3;
        } else {
            hostStart = 0;
        }

        // Find end of host
        size_t pathStart = url.find('/', hostStart);
        std::string host;
        std::string path;
        if (pathStart != std::string::npos) {
            host = url.substr(hostStart, pathStart - hostStart);
            path = url.substr(pathStart);
        } else {
            host = url.substr(hostStart);
            path = "/";
        }

        // Convert to wide strings for WinHTTP
        wHost.assign(host.begin(), host.end());
        wPath.assign(path.begin(), path.end());

        // Create user agent string
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
            DEBUG_WARN("RecordsFetcher: WinHttpOpen failed");
            completeError("WinHttpOpen failed");
            return;
        }

        // Set timeouts (10 seconds for each operation)
        WinHttpSetTimeouts(hSession, 10000, 10000, 10000, 10000);

        // Connect to server
        HINTERNET hConnect = WinHttpConnect(hSession, wHost.c_str(), port, 0);
        if (!hConnect) {
            WinHttpCloseHandle(hSession);
            DEBUG_WARN("RecordsFetcher: WinHttpConnect failed");
            completeError("Connection failed");
            return;
        }

        // Create request
        DWORD flags = useHttps ? WINHTTP_FLAG_SECURE : 0;
        HINTERNET hRequest = WinHttpOpenRequest(hConnect, L"GET", wPath.c_str(),
                                                NULL, WINHTTP_NO_REFERER,
                                                WINHTTP_DEFAULT_ACCEPT_TYPES, flags);
        if (!hRequest) {
            WinHttpCloseHandle(hConnect);
            WinHttpCloseHandle(hSession);
            DEBUG_WARN("RecordsFetcher: WinHttpOpenRequest failed");
            completeError("Request failed");
            return;
        }

        // Add Accept header
        WinHttpAddRequestHeaders(hRequest, L"Accept: application/json",
                                 (DWORD)-1, WINHTTP_ADDREQ_FLAG_ADD);

        // Send request
        BOOL bResults = WinHttpSendRequest(hRequest, WINHTTP_NO_ADDITIONAL_HEADERS,
                                            0, WINHTTP_NO_REQUEST_DATA, 0, 0, 0);
        if (!bResults) {
            WinHttpCloseHandle(hRequest);
            WinHttpCloseHandle(hConnect);
            WinHttpCloseHandle(hSession);
            DEBUG_WARN("RecordsFetcher: WinHttpSendRequest failed");
            completeError("Send failed");
            return;
        }

        // Receive response
        bResults = WinHttpReceiveResponse(hRequest, NULL);
        if (!bResults) {
            WinHttpCloseHandle(hRequest);
            WinHttpCloseHandle(hConnect);
            WinHttpCloseHandle(hSession);
            DEBUG_WARN("RecordsFetcher: WinHttpReceiveResponse failed");
            completeError("No response");
            return;
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
            DEBUG_WARN_F("RecordsFetcher: HTTP error %d", statusCode);
            completeError(("HTTP " + std::to_string(statusCode)).c_str());
            return;
        }

        // Read response body with size limit
        std::string responseBody;
        DWORD dwSize = 0;
        DWORD dwDownloaded = 0;
        bool sizeLimitExceeded = false;

        do {
            dwSize = 0;
            if (!WinHttpQueryDataAvailable(hRequest, &dwSize)) {
                break;
            }

            if (dwSize == 0) break;

            // Check size limit to prevent memory exhaustion
            if (responseBody.size() + dwSize > MAX_RESPONSE_SIZE) {
                sizeLimitExceeded = true;
                DEBUG_WARN_F("RecordsFetcher: Response size limit exceeded (current=%zu, chunk=%lu, limit=%zu)",
                             responseBody.size(), dwSize, MAX_RESPONSE_SIZE);
                break;
            }

            std::vector<char> buffer(dwSize + 1, 0);

            if (WinHttpReadData(hRequest, buffer.data(), dwSize, &dwDownloaded)) {
                responseBody.append(buffer.data(), dwDownloaded);
            }

        } while (dwSize > 0);

        if (sizeLimitExceeded) {
            WinHttpCloseHandle(hRequest);
            WinHttpCloseHandle(hConnect);
            WinHttpCloseHandle(hSession);
            completeError("Response too large");
            return;
        }

        // Clean up
        WinHttpCloseHandle(hRequest);
        WinHttpCloseHandle(hConnect);
        WinHttpCloseHandle(hSession);

        // Process response
        if (!responseBody.empty()) {
            Result r;
            r.provider = m_provider;
            if (parseResponse(m_provider, responseBody, r)) {
                r.parsed = true;
                DEBUG_INFO("RecordsFetcher: Fetch successful");
            } else {
                r.error = "Parse error";
                DEBUG_WARN("RecordsFetcher: Failed to parse response");
            }
            if (m_onDone) m_onDone(std::move(r));
        } else {
            DEBUG_WARN("RecordsFetcher: Empty response");
            completeError("Empty response");
        }

    } catch (const std::exception& e) {
        DEBUG_WARN_F("Records fetch thread terminated by exception: %s", e.what());
        // completeError is the noexcept-shaped error path — see its comment
        // for the bad_alloc / nested-lock discipline this preserves.
        completeError(e.what());
    } catch (...) {
        DEBUG_WARN("Records fetch thread terminated by unknown exception");
        completeError("Unknown error");
    }
}

bool RecordsFetcher::parseResponse(DataProvider provider, const std::string& response, Result& out) {
    try {
        nlohmann::json j = nlohmann::json::parse(response);

        // Use the provider the response was REQUESTED with (snapshotted on the
        // game thread in RecordsHud::startFetch and passed through start()),
        // never the HUD's live m_provider - the user can cycle providers
        // mid-fetch and the response must be parsed with the schema it was
        // requested with.
        out.records.clear();
        out.provider = provider;

        // Determine which format we're parsing based on provider
        // CBR: { "notice": "...", "records": [...] }
        // MXB-Ranked: [...]
        const nlohmann::json* recordsArray = nullptr;

        if (provider == DataProvider::MXB_RANKED) {
            // MXB-Ranked returns a direct array
            if (j.is_array()) {
                recordsArray = &j;
            }
        } else {
            // CBR wraps records in an object
            if (j.contains("notice") && j["notice"].is_string()) {
                out.apiNotice = j["notice"].get<std::string>();
            }
            if (j.contains("records") && j["records"].is_array()) {
                recordsArray = &j["records"];
            }
        }

        if (recordsArray) {
            int position = 1;
            for (const auto& record : *recordsArray) {
                if (out.records.size() >= MAX_RECORDS) break;

                RecordEntry entry;
                entry.position = position++;

                if (provider == DataProvider::MXB_RANKED) {
                    // MXB-Ranked format: name, bike, lapTime (seconds), sector1-3 (seconds), createDateTimeUtc
                    if (record.contains("name") && record["name"].is_string()) {
                        std::string name = record["name"].get<std::string>();
                        strncpy_s(entry.rider, sizeof(entry.rider), name.c_str(), sizeof(entry.rider) - 1);
                    }

                    if (record.contains("bike") && record["bike"].is_string()) {
                        std::string bike = record["bike"].get<std::string>();
                        strncpy_s(entry.bike, sizeof(entry.bike), bike.c_str(), sizeof(entry.bike) - 1);
                    }

                    // MXB-Ranked returns times in seconds (float), convert to milliseconds
                    if (record.contains("lapTime") && record["lapTime"].is_number()) {
                        entry.laptime = static_cast<int>(record["lapTime"].get<double>() * 1000.0);
                    }

                    // Parse sector times (seconds to milliseconds)
                    if (record.contains("sector1") && record["sector1"].is_number()) {
                        entry.sector1 = static_cast<int>(record["sector1"].get<double>() * 1000.0);
                    }
                    if (record.contains("sector2") && record["sector2"].is_number()) {
                        entry.sector2 = static_cast<int>(record["sector2"].get<double>() * 1000.0);
                    }
                    if (record.contains("sector3") && record["sector3"].is_number()) {
                        entry.sector3 = static_cast<int>(record["sector3"].get<double>() * 1000.0);
                    }

                    // Parse date from createDateTimeUtc
                    if (record.contains("createDateTimeUtc") && record["createDateTimeUtc"].is_string()) {
                        std::string timestamp = record["createDateTimeUtc"].get<std::string>();
                        if (timestamp.length() >= 10) {
                            strncpy_s(entry.date, sizeof(entry.date), timestamp.substr(0, 10).c_str(), sizeof(entry.date) - 1);
                        }
                    }
                } else {
                    // CBR format: player, bike, laptime (milliseconds), timestamp
                    if (record.contains("player") && record["player"].is_string()) {
                        std::string player = record["player"].get<std::string>();
                        strncpy_s(entry.rider, sizeof(entry.rider), player.c_str(), sizeof(entry.rider) - 1);
                    }

                    if (record.contains("bike") && record["bike"].is_string()) {
                        std::string bike = record["bike"].get<std::string>();
                        strncpy_s(entry.bike, sizeof(entry.bike), bike.c_str(), sizeof(entry.bike) - 1);
                    }

                    // CBR returns laptime in milliseconds
                    if (record.contains("laptime") && record["laptime"].is_number()) {
                        entry.laptime = record["laptime"].get<int>();
                    }

                    // Parse timestamp and format as date
                    if (record.contains("timestamp") && record["timestamp"].is_string()) {
                        std::string timestamp = record["timestamp"].get<std::string>();
                        if (timestamp.length() >= 10) {
                            strncpy_s(entry.date, sizeof(entry.date), timestamp.substr(0, 10).c_str(), sizeof(entry.date) - 1);
                        }
                    }
                    // CBR doesn't provide sector times - leave as -1 (default)
                }

                out.records.push_back(entry);
            }

            DEBUG_INFO_F("RecordsFetcher: Parsed %zu records from %s",
                         out.records.size(), getProviderDisplayName(provider));
        }

        return true;
    } catch (const std::exception& e) {
        DEBUG_WARN_F("RecordsFetcher: JSON parse error: %s", e.what());
        return false;
    }
}
