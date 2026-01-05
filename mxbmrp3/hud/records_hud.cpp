// ============================================================================
// hud/records_hud.cpp
// Displays lap records fetched from external data providers via HTTP
// ============================================================================
#include "records_hud.h"
#include "timing_hud.h"

#include <cstring>
#include <cstdio>
#include <ctime>
#include <algorithm>

#include "../diagnostics/logger.h"
#include "../diagnostics/timer.h"
#include "../core/plugin_utils.h"
#include "../core/plugin_constants.h"
#include "../core/plugin_data.h"
#include "../core/plugin_manager.h"
#include "../core/hud_manager.h"
#include "../core/settings_manager.h"
#include "../core/input_manager.h"
#include "../core/color_config.h"
#include "../core/personal_best_manager.h"

// Use WinHTTP for HTTPS support (built into Windows, no external dependencies)
#include <windows.h>
#include <winhttp.h>
#pragma comment(lib, "winhttp.lib")

#include "../vendor/nlohmann/json.hpp"

using namespace PluginConstants;

// ============================================================================
// Column Positions
// ============================================================================

RecordsHud::ColumnPositions::ColumnPositions(float contentStartX, float scale, uint32_t enabledColumns) {
    float scaledFontSize = FontSizes::NORMAL * scale;
    float x = contentStartX;

    // Position each column based on what's enabled before it
    // Order: POS, RIDER, BIKE, SECTOR1, SECTOR2, SECTOR3, LAPTIME, DATE
    pos = x;
    if (enabledColumns & COL_POS) x += PluginUtils::calculateMonospaceTextWidth(COL_POS_WIDTH, scaledFontSize);

    rider = x;
    if (enabledColumns & COL_RIDER) x += PluginUtils::calculateMonospaceTextWidth(COL_RIDER_WIDTH, scaledFontSize);

    bike = x;
    if (enabledColumns & COL_BIKE) x += PluginUtils::calculateMonospaceTextWidth(COL_BIKE_WIDTH, scaledFontSize);

    sector1 = x;
    if (enabledColumns & COL_SECTOR1) x += PluginUtils::calculateMonospaceTextWidth(COL_SECTOR_WIDTH, scaledFontSize);

    sector2 = x;
    if (enabledColumns & COL_SECTOR2) x += PluginUtils::calculateMonospaceTextWidth(COL_SECTOR_WIDTH, scaledFontSize);

    sector3 = x;
    if (enabledColumns & COL_SECTOR3) x += PluginUtils::calculateMonospaceTextWidth(COL_SECTOR_WIDTH, scaledFontSize);

    laptime = x;
    if (enabledColumns & COL_LAPTIME) x += PluginUtils::calculateMonospaceTextWidth(COL_LAPTIME_WIDTH, scaledFontSize);

    date = x;
    // DATE is last column, no need to advance x
}

// ============================================================================
// Constructor / Destructor
// ============================================================================

RecordsHud::RecordsHud()
    : m_columns()
    , m_provider(DataProvider::CBR)
    , m_categoryIndex(0)
    , m_fetchState(FetchState::IDLE)
    , m_recordsProvider(DataProvider::CBR)
    , m_fetchButtonHovered(false)
    , m_fetchResultTimestamp(0)
    , m_fetchStartTimestamp(0)
    , m_wasOnCooldown(false)
{
    // One-time setup
    DEBUG_INFO("RecordsHud created");
    setDraggable(true);
    m_quads.reserve(2);  // Background + fetch button
    m_strings.reserve(60);  // Header rows + 10 record rows * 5 cols + footer
    m_records.reserve(MAX_RECORDS);
    m_clickRegions.reserve(5);

    // Set texture base name for dynamic texture discovery
    setTextureBaseName("records_hud");

    // Set all configurable defaults
    resetToDefaults();

    // Initialize column positions (after resetToDefaults sets m_enabledColumns)
    m_columns = ColumnPositions(START_X + Padding::HUD_HORIZONTAL, m_fScale, m_enabledColumns);

    // Build initial category list (after resetToDefaults sets m_provider)
    buildCategoryList();

    rebuildRenderData();
}

RecordsHud::~RecordsHud() {
    // Wait for any ongoing fetch to complete
    if (m_fetchThread.joinable()) {
        m_fetchThread.join();
    }
}

// ============================================================================
// Data Provider Configuration
// ============================================================================

std::string RecordsHud::getProviderBaseUrl(DataProvider provider) {
    switch (provider) {
        case DataProvider::CBR:
            return "https://server.cbrservers.com/api/records/top";
        case DataProvider::MXB_RANKED:
            return "https://mxb-ranked.com/pub-api/stats/GetTrackFastestLapsByBikeCategory";
        default:
            return "";
    }
}

const char* RecordsHud::getProviderDisplayName(DataProvider provider) {
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
// Category Management
// ============================================================================

void RecordsHud::buildCategoryList() {
    m_categoryList.clear();
    m_categoryList.push_back("All");
    m_categoryList.push_back("MX-E OEM");
    m_categoryList.push_back("MX1 OEM");
    m_categoryList.push_back("MX2 OEM");
    m_categoryList.push_back("MX1-2T OEM");
    m_categoryList.push_back("MX2-2T OEM");
    m_categoryList.push_back("MX3 OEM");
}

int RecordsHud::findCategoryIndex(const char* category) const {
    // Skip "All" (index 0)
    for (size_t i = 1; i < m_categoryList.size(); ++i) {
        if (m_categoryList[i] == category) {
            return static_cast<int>(i);
        }
    }
    return -1;  // Not found
}

const char* RecordsHud::getCurrentCategoryDisplay() const {
    if (m_categoryIndex >= 0 && m_categoryIndex < static_cast<int>(m_categoryList.size())) {
        return m_categoryList[m_categoryIndex].c_str();
    }
    return "All";
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

std::string RecordsHud::buildRequestUrl() const {
    std::string baseUrl = getProviderBaseUrl(m_provider);
    if (baseUrl.empty()) return "";

    const SessionData& session = PluginData::getInstance().getSessionData();

    if (m_provider == DataProvider::MXB_RANKED) {
        // MXB-Ranked uses path-based URL: /trackname or /trackname/category
        // No limit parameter supported
        std::string url = baseUrl;

        // Add track name parameter (required)
        if (session.trackName[0] != '\0') {
            url += "/";
            appendUrlEncoded(url, session.trackName);
        } else {
            return "";  // Track name is required for MXB-Ranked
        }

        // Add category parameter (optional - omit for "All")
        if (m_categoryIndex > 0 && m_categoryIndex < static_cast<int>(m_categoryList.size())) {
            url += "/";
            appendUrlEncoded(url, m_categoryList[m_categoryIndex].c_str());
        }
        // If "All" (index 0), omit category to get all categories

        return url;
    } else {
        // CBR uses query parameters
        std::string url = baseUrl + "?limit=" + std::to_string(MAX_RECORDS);

        // Add track parameter
        if (session.trackId[0] != '\0') {
            url += "&track=";
            appendUrlEncoded(url, session.trackId);
        }

        // Add category parameter if not "All" (index 0)
        if (m_categoryIndex > 0 && m_categoryIndex < static_cast<int>(m_categoryList.size())) {
            url += "&category=";
            appendUrlEncoded(url, m_categoryList[m_categoryIndex].c_str());
        }

        return url;
    }
}

void RecordsHud::startFetch() {
    // Cooldown check - prevent spam (silently ignore if on cooldown)
    unsigned long now = GetTickCount();
    if (now - m_fetchStartTimestamp < FETCH_COOLDOWN_MS) {
        return;
    }

    // Don't start if already fetching
    FetchState expected = FetchState::IDLE;
    if (!m_fetchState.compare_exchange_strong(expected, FetchState::FETCHING)) {
        // Also allow re-fetch from SUCCESS/ERROR states
        expected = FetchState::SUCCESS;
        if (!m_fetchState.compare_exchange_strong(expected, FetchState::FETCHING)) {
            expected = FetchState::FETCH_ERROR;
            if (!m_fetchState.compare_exchange_strong(expected, FetchState::FETCHING)) {
                return;  // Already fetching
            }
        }
    }

    // Record fetch start time for cooldown
    m_fetchStartTimestamp = now;

    // Wait for previous thread if any
    if (m_fetchThread.joinable()) {
        m_fetchThread.join();
    }

    // Start new fetch thread
    m_fetchThread = std::thread(&RecordsHud::performFetch, this);
}

void RecordsHud::performFetch() {
    DEBUG_INFO("RecordsHud: Starting HTTP fetch");

    std::string url = buildRequestUrl();
    if (url.empty()) {
        {
            std::lock_guard<std::mutex> lock(m_recordsMutex);
            m_lastError = "Invalid URL";
        }
        m_fetchState = FetchState::FETCH_ERROR;
        m_fetchResultTimestamp = GetTickCount();
        setDataDirty();
        return;
    }

    DEBUG_INFO_F("RecordsHud: Fetching URL: %s", url.c_str());

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
        {
            std::lock_guard<std::mutex> lock(m_recordsMutex);
            m_lastError = "WinHttpOpen failed";
        }
        m_fetchState = FetchState::FETCH_ERROR;
        m_fetchResultTimestamp = GetTickCount();
        setDataDirty();
        DEBUG_WARN("RecordsHud: WinHttpOpen failed");
        return;
    }

    // Set timeouts (10 seconds for each operation)
    WinHttpSetTimeouts(hSession, 10000, 10000, 10000, 10000);

    // Connect to server
    HINTERNET hConnect = WinHttpConnect(hSession, wHost.c_str(), port, 0);
    if (!hConnect) {
        WinHttpCloseHandle(hSession);
        {
            std::lock_guard<std::mutex> lock(m_recordsMutex);
            m_lastError = "Connection failed";
        }
        m_fetchState = FetchState::FETCH_ERROR;
        m_fetchResultTimestamp = GetTickCount();
        setDataDirty();
        DEBUG_WARN("RecordsHud: WinHttpConnect failed");
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
        {
            std::lock_guard<std::mutex> lock(m_recordsMutex);
            m_lastError = "Request failed";
        }
        m_fetchState = FetchState::FETCH_ERROR;
        m_fetchResultTimestamp = GetTickCount();
        setDataDirty();
        DEBUG_WARN("RecordsHud: WinHttpOpenRequest failed");
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
        {
            std::lock_guard<std::mutex> lock(m_recordsMutex);
            m_lastError = "Send failed";
        }
        m_fetchState = FetchState::FETCH_ERROR;
        m_fetchResultTimestamp = GetTickCount();
        setDataDirty();
        DEBUG_WARN("RecordsHud: WinHttpSendRequest failed");
        return;
    }

    // Receive response
    bResults = WinHttpReceiveResponse(hRequest, NULL);
    if (!bResults) {
        WinHttpCloseHandle(hRequest);
        WinHttpCloseHandle(hConnect);
        WinHttpCloseHandle(hSession);
        {
            std::lock_guard<std::mutex> lock(m_recordsMutex);
            m_lastError = "No response";
        }
        m_fetchState = FetchState::FETCH_ERROR;
        m_fetchResultTimestamp = GetTickCount();
        setDataDirty();
        DEBUG_WARN("RecordsHud: WinHttpReceiveResponse failed");
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
        {
            std::lock_guard<std::mutex> lock(m_recordsMutex);
            m_lastError = "HTTP " + std::to_string(statusCode);
        }
        m_fetchState = FetchState::FETCH_ERROR;
        m_fetchResultTimestamp = GetTickCount();
        setDataDirty();
        DEBUG_WARN_F("RecordsHud: HTTP error %d", statusCode);
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
            DEBUG_WARN_F("RecordsHud: Response size limit exceeded (current=%zu, chunk=%lu, limit=%zu)",
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
        {
            std::lock_guard<std::mutex> lock(m_recordsMutex);
            m_lastError = "Response too large";
        }
        m_fetchState = FetchState::FETCH_ERROR;
        m_fetchResultTimestamp = GetTickCount();
        setDataDirty();
        return;
    }

    // Clean up
    WinHttpCloseHandle(hRequest);
    WinHttpCloseHandle(hConnect);
    WinHttpCloseHandle(hSession);

    // Process response
    if (!responseBody.empty()) {
        if (processFetchResult(responseBody)) {
            m_fetchState = FetchState::SUCCESS;
            DEBUG_INFO("RecordsHud: Fetch successful");
            // Notify TimingHud so it can update RC reference time immediately
            HudManager::getInstance().getTimingHud().setDataDirty();
        } else {
            m_fetchState = FetchState::FETCH_ERROR;
            DEBUG_WARN("RecordsHud: Failed to parse response");
        }
    } else {
        {
            std::lock_guard<std::mutex> lock(m_recordsMutex);
            m_lastError = "Empty response";
        }
        m_fetchState = FetchState::FETCH_ERROR;
        DEBUG_WARN("RecordsHud: Empty response");
    }

    m_fetchResultTimestamp = GetTickCount();
    setDataDirty();
}

bool RecordsHud::processFetchResult(const std::string& response) {
    try {
        nlohmann::json j = nlohmann::json::parse(response);

        // Lock for all member variable writes
        std::lock_guard<std::mutex> lock(m_recordsMutex);
        m_records.clear();
        m_recordsProvider = m_provider;  // Track which provider these records came from

        // Determine which format we're parsing based on provider
        // CBR: { "notice": "...", "records": [...] }
        // MXB-Ranked: [...]
        const nlohmann::json* recordsArray = nullptr;

        if (m_provider == DataProvider::MXB_RANKED) {
            // MXB-Ranked returns a direct array
            if (j.is_array()) {
                recordsArray = &j;
            }
        } else {
            // CBR wraps records in an object
            if (j.contains("notice") && j["notice"].is_string()) {
                m_apiNotice = j["notice"].get<std::string>();
            }
            if (j.contains("records") && j["records"].is_array()) {
                recordsArray = &j["records"];
            }
        }

        if (recordsArray) {
            int position = 1;
            for (const auto& record : *recordsArray) {
                if (m_records.size() >= MAX_RECORDS) break;

                RecordEntry entry;
                entry.position = position++;

                if (m_provider == DataProvider::MXB_RANKED) {
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

                m_records.push_back(entry);
            }

            DEBUG_INFO_F("RecordsHud: Parsed %zu records from %s",
                         m_records.size(), getProviderDisplayName(m_provider));
        }

        return true;
    } catch (const std::exception& e) {
        std::lock_guard<std::mutex> lock(m_recordsMutex);
        m_lastError = "Parse error";
        DEBUG_WARN_F("RecordsHud: JSON parse error: %s", e.what());
        return false;
    }
}

// ============================================================================
// Click Handling
// ============================================================================

void RecordsHud::handleClick(float mouseX, float mouseY) {
    // Click regions store base positions, add offset for hit testing
    for (const auto& region : m_clickRegions) {
        if (isPointInRect(mouseX, mouseY,
                region.x + m_fOffsetX, region.y + m_fOffsetY, region.width, region.height)) {
            bool saveSettings = false;
            switch (region.type) {
                case ClickRegionType::PROVIDER_LEFT:
                    cycleProvider(-1);
                    saveSettings = true;
                    break;
                case ClickRegionType::PROVIDER_RIGHT:
                    cycleProvider(1);
                    saveSettings = true;
                    break;
                case ClickRegionType::CATEGORY_LEFT:
                    cycleCategory(-1);
                    saveSettings = true;
                    break;
                case ClickRegionType::CATEGORY_RIGHT:
                    cycleCategory(1);
                    saveSettings = true;
                    break;
                case ClickRegionType::FETCH_BUTTON:
                    startFetch();
                    break;
            }
            setDataDirty();
            if (saveSettings) {
                SettingsManager::getInstance().saveSettings(HudManager::getInstance(),
                    PluginManager::getInstance().getSavePath());
            }
            break;
        }
    }
}

void RecordsHud::cycleProvider(int direction) {
    int count = static_cast<int>(DataProvider::COUNT);
    int current = static_cast<int>(m_provider);
    current = (current + direction + count) % count;
    m_provider = static_cast<DataProvider>(current);
}

void RecordsHud::cycleCategory(int direction) {
    int count = static_cast<int>(m_categoryList.size());
    if (count == 0) return;

    // Cycle through all categories including "All" (index 0)
    m_categoryIndex = (m_categoryIndex + direction + count) % count;
}

int RecordsHud::findPlayerPositionInRecords(int playerPBTime) const {
    if (playerPBTime <= 0) return -1;  // No valid PB
    if (m_records.empty()) return 0;   // Faster than all (no records to compare)

    // Find where player's PB would rank
    for (size_t i = 0; i < m_records.size(); i++) {
        if (playerPBTime < m_records[i].laptime) {
            return static_cast<int>(i);  // Player is faster, would be at this position
        }
    }

    // Player is slower than all records
    return static_cast<int>(m_records.size());
}

// ============================================================================
// Update and Rendering
// ============================================================================

bool RecordsHud::handlesDataType(DataChangeType dataType) const {
    return (dataType == DataChangeType::SessionData);  // Rebuild when track/category changes
}

void RecordsHud::update() {
    const SessionData& session = PluginData::getInstance().getSessionData();
    bool shouldAutoFetch = false;

    // Clear records when event ends (track becomes empty)
    if (session.trackId[0] == '\0') {
        if (!m_records.empty() || m_lastSessionTrackId[0] != '\0') {
            std::lock_guard<std::mutex> lock(m_recordsMutex);
            m_records.clear();
            m_fetchState = FetchState::IDLE;
            m_lastSessionTrackId[0] = '\0';  // Reset tracked track
            m_lastSessionCategory[0] = '\0';  // Reset tracked category
            setDataDirty();
        }
    } else {
        // Track changed (entered new event) - auto-fetch if enabled
        if (strcmp(session.trackId, m_lastSessionTrackId) != 0) {
            strncpy_s(m_lastSessionTrackId, sizeof(m_lastSessionTrackId), session.trackId, sizeof(m_lastSessionTrackId) - 1);
            shouldAutoFetch = true;
        }
    }

    // Auto-update category selection when session category changes (event start)
    // Note: Game doesn't allow changing category mid-event, so this only triggers on join
    if (strcmp(session.category, m_lastSessionCategory) != 0) {
        strncpy_s(m_lastSessionCategory, sizeof(m_lastSessionCategory), session.category, sizeof(m_lastSessionCategory) - 1);
        if (session.category[0] != '\0') {
            int idx = findCategoryIndex(session.category);
            if (idx >= 0) {
                m_categoryIndex = idx;
                setDataDirty();
            }
        }
    }

    // Perform auto-fetch if enabled and conditions met
    if (m_bAutoFetch && shouldAutoFetch && m_fetchState != FetchState::FETCHING) {
        startFetch();
    }

    // Handle mouse input for click regions
    const auto& inputManager = InputManager::getInstance();
    const auto& cursor = inputManager.getCursorPosition();
    float mouseX = cursor.x;
    float mouseY = cursor.y;

    // Check for fetch button hover (click regions store base positions, add offset for hit testing)
    bool wasHovered = m_fetchButtonHovered;
    m_fetchButtonHovered = false;
    for (const auto& region : m_clickRegions) {
        if (region.type == ClickRegionType::FETCH_BUTTON) {
            m_fetchButtonHovered = isPointInRect(mouseX, mouseY,
                region.x + m_fOffsetX, region.y + m_fOffsetY, region.width, region.height);
            break;
        }
    }
    if (wasHovered != m_fetchButtonHovered) {
        setDataDirty();
    }

    // Handle clicks
    if (inputManager.getLeftButton().isClicked() && isPointInBounds(mouseX, mouseY)) {
        handleClick(mouseX, mouseY);
    }

    // Check if fetch result display should timeout
    if (m_fetchState == FetchState::SUCCESS || m_fetchState == FetchState::FETCH_ERROR) {
        unsigned long elapsed = GetTickCount() - m_fetchResultTimestamp;
        if (elapsed > FETCH_RESULT_DISPLAY_MS) {
            // Keep showing the data, just reset state to IDLE
            m_fetchState = FetchState::IDLE;
            setDataDirty();
        }
    }

    // Check if cooldown just expired (need to re-enable button)
    {
        bool wasOnCooldown = m_wasOnCooldown;
        bool isOnCooldown = (GetTickCount() - m_fetchStartTimestamp < FETCH_COOLDOWN_MS);
        m_wasOnCooldown = isOnCooldown;
        if (wasOnCooldown && !isOnCooldown) {
            setDataDirty();
        }
    }

    // Handle dirty flags using base class helper
    processDirtyFlags();
}

void RecordsHud::rebuildLayout() {
    // Rebuild everything for layout changes (dragging, scale, etc.)
    // Strings have offset baked in when created, so we need full rebuild
    rebuildRenderData();
}

void RecordsHud::rebuildRenderData() {
    m_strings.clear();
    m_quads.clear();
    m_clickRegions.clear();

    // Get scaled dimensions
    auto dim = getScaledDimensions();
    float titleHeight = m_bShowTitle ? dim.lineHeightLarge : 0.0f;

    // Copy ALL records for pagination (minimize mutex hold time)
    std::vector<RecordEntry> allRecords;
    std::string lastError;
    {
        std::lock_guard<std::mutex> lock(m_recordsMutex);
        allRecords = m_records;  // Copy all for proper pagination
        lastError = m_lastError;
    }
    int totalRecords = static_cast<int>(allRecords.size());
    int footerRows = m_bShowFooter ? FOOTER_ROWS : 0;
    int totalRows = HEADER_ROWS + m_recordsToShow + footerRows;

    // Calculate background width based on enabled columns
    // Note: padding is added by calculateBackgroundWidth(), don't double-count
    // Order: POS, RIDER, BIKE, SECTOR1, SECTOR2, SECTOR3, LAPTIME, DATE
    int bgWidthChars = 0;
    if (m_enabledColumns & COL_POS) bgWidthChars += COL_POS_WIDTH;
    if (m_enabledColumns & COL_RIDER) bgWidthChars += COL_RIDER_WIDTH;
    if (m_enabledColumns & COL_BIKE) bgWidthChars += COL_BIKE_WIDTH;
    if (m_enabledColumns & COL_SECTOR1) bgWidthChars += COL_SECTOR_WIDTH;
    if (m_enabledColumns & COL_SECTOR2) bgWidthChars += COL_SECTOR_WIDTH;
    if (m_enabledColumns & COL_SECTOR3) bgWidthChars += COL_SECTOR_WIDTH;
    if (m_enabledColumns & COL_LAPTIME) bgWidthChars += COL_LAPTIME_WIDTH;
    if (m_enabledColumns & COL_DATE) bgWidthChars += COL_DATE_WIDTH;
    // Remove trailing gap from last visible column (gap not needed after last column)
    // Find the last enabled column and subtract 1 for its gap
    uint32_t lastCol = 0;
    if (m_enabledColumns & COL_DATE) lastCol = COL_DATE;
    else if (m_enabledColumns & COL_LAPTIME) lastCol = COL_LAPTIME;
    else if (m_enabledColumns & COL_SECTOR3) lastCol = COL_SECTOR3;
    else if (m_enabledColumns & COL_SECTOR2) lastCol = COL_SECTOR2;
    else if (m_enabledColumns & COL_SECTOR1) lastCol = COL_SECTOR1;
    else if (m_enabledColumns & COL_BIKE) lastCol = COL_BIKE;
    else if (m_enabledColumns & COL_RIDER) lastCol = COL_RIDER;
    if (lastCol != 0 && lastCol != COL_DATE) {
        bgWidthChars -= 1;  // Remove gap from last column (unless it's DATE which has no gap anyway)
    }
    bgWidthChars = std::max(bgWidthChars, 34);  // Minimum width for controls row

    float backgroundWidth = calculateBackgroundWidth(bgWidthChars);
    float backgroundHeight = calculateBackgroundHeight(totalRows - 1);

    // Adjust for footer using smaller line height (2 text rows are small, not normal)
    if (m_bShowFooter) {
        backgroundHeight -= 2 * (dim.lineHeightNormal - dim.lineHeightSmall);
    }

    setBounds(START_X, START_Y, START_X + backgroundWidth, START_Y + backgroundHeight);
    addBackgroundQuad(START_X, START_Y, backgroundWidth, backgroundHeight);

    float contentStartX = START_X + dim.paddingH;
    float contentStartY = START_Y + dim.paddingV;
    float currentY = contentStartY;

    // Recalculate column positions based on enabled columns
    m_columns = ColumnPositions(contentStartX, m_fScale, m_enabledColumns);

    // === Title Row ===
    addTitleString("Records", contentStartX, currentY, Justify::LEFT,
                   Fonts::getTitle(), ColorConfig::getInstance().getPrimary(), dim.fontSizeLarge);
    currentY += titleHeight;

    // === Provider / Category / Fetch Row ===
    // Note: Click regions store positions WITHOUT offset - offset is added during hit testing
    float rowX = contentStartX;
    float charWidth = PluginUtils::calculateMonospaceTextWidth(1, dim.fontSize);

    // Provider selector: "< MXB Ranked >" - use fixed width so arrows don't jump
    static constexpr int PROVIDER_WIDTH_CHARS = 10;  // Longest: "MXB Ranked"
    float providerFixedWidth = PluginUtils::calculateMonospaceTextWidth(PROVIDER_WIDTH_CHARS, dim.fontSize);

    addString("<", rowX, currentY, Justify::LEFT, Fonts::getNormal(), ColorConfig::getInstance().getAccent(), dim.fontSize);
    m_clickRegions.push_back({rowX, currentY, charWidth * 2, dim.lineHeightNormal, ClickRegionType::PROVIDER_LEFT});
    rowX += charWidth * 2;  // "< "

    addString(getProviderDisplayName(m_provider), rowX, currentY, Justify::LEFT,
              Fonts::getNormal(), ColorConfig::getInstance().getPrimary(), dim.fontSize);
    rowX += providerFixedWidth;  // Fixed width regardless of actual name length

    addString(" >", rowX, currentY, Justify::LEFT, Fonts::getNormal(), ColorConfig::getInstance().getAccent(), dim.fontSize);
    m_clickRegions.push_back({rowX, currentY, charWidth * 2, dim.lineHeightNormal, ClickRegionType::PROVIDER_RIGHT});
    rowX += charWidth * 4;  // " > " + gap

    // Category selector: "< MX1-2T OEM >" - use fixed width so arrows don't jump
    static constexpr int CATEGORY_WIDTH_CHARS = 10;  // Longest: "MX1-2T OEM"
    float categoryFixedWidth = PluginUtils::calculateMonospaceTextWidth(CATEGORY_WIDTH_CHARS, dim.fontSize);

    addString("<", rowX, currentY, Justify::LEFT, Fonts::getNormal(), ColorConfig::getInstance().getAccent(), dim.fontSize);
    m_clickRegions.push_back({rowX, currentY, charWidth * 2, dim.lineHeightNormal, ClickRegionType::CATEGORY_LEFT});
    rowX += charWidth * 2;  // "< "

    const char* catName = getCurrentCategoryDisplay();
    addString(catName, rowX, currentY, Justify::LEFT,
              Fonts::getNormal(), ColorConfig::getInstance().getPrimary(), dim.fontSize);
    rowX += categoryFixedWidth;  // Fixed width regardless of actual name length

    addString(" >", rowX, currentY, Justify::LEFT, Fonts::getNormal(), ColorConfig::getInstance().getAccent(), dim.fontSize);
    m_clickRegions.push_back({rowX, currentY, charWidth * 2, dim.lineHeightNormal, ClickRegionType::CATEGORY_RIGHT});
    rowX += charWidth * 4;  // " > " + gap

    // Compare button - all labels same width as [Compare] (9 chars)
    // Button is disabled when trackId is unavailable (spectating mode) or on cooldown
    const SessionData& sessionForButton = PluginData::getInstance().getSessionData();
    bool trackIdAvailable = sessionForButton.trackId[0] != '\0';
    bool isOnCooldown = (GetTickCount() - m_fetchStartTimestamp < FETCH_COOLDOWN_MS);
    FetchState state = m_fetchState.load();
    bool isButtonDisabled = !trackIdAvailable || (isOnCooldown && state != FetchState::FETCHING);

    const char* compareLabel = "[Compare]";
    if (state == FetchState::FETCHING) {
        compareLabel = "[  ...  ]";
    } else if (state == FetchState::SUCCESS) {
        compareLabel = "[   OK  ]";
    } else if (state == FetchState::FETCH_ERROR) {
        compareLabel = "[ Error ]";
    }

    unsigned long compareColor;
    if (isButtonDisabled) {
        compareColor = ColorConfig::getInstance().getMuted();  // Muted when disabled/cooldown
    } else if (state == FetchState::SUCCESS) {
        compareColor = ColorConfig::getInstance().getPositive();
    } else if (state == FetchState::FETCH_ERROR) {
        compareColor = ColorConfig::getInstance().getNegative();
    } else if (state == FetchState::FETCHING) {
        // Always accent during fetch (no hover feedback since button is busy)
        compareColor = ColorConfig::getInstance().getAccent();
    } else {
        // Use PRIMARY when hovered, ACCENT when not (purple on purple)
        compareColor = m_fetchButtonHovered ? ColorConfig::getInstance().getPrimary() : ColorConfig::getInstance().getAccent();
    }

    float compareWidth = PluginUtils::calculateMonospaceTextWidth(static_cast<int>(strlen("[Compare]")), dim.fontSize);

    // Only add click region if button is enabled
    if (!isButtonDisabled) {
        m_clickRegions.push_back({rowX, currentY, compareWidth, dim.lineHeightNormal, ClickRegionType::FETCH_BUTTON});
    }

    // Button background - muted when disabled, accent when enabled
    {
        SPluginQuad_t bgQuad;
        float bgX = rowX;
        float bgY = currentY;
        applyOffset(bgX, bgY);
        setQuadPositions(bgQuad, bgX, bgY, compareWidth, dim.lineHeightNormal);
        bgQuad.m_iSprite = SpriteIndex::SOLID_COLOR;
        if (isButtonDisabled) {
            bgQuad.m_ulColor = PluginUtils::applyOpacity(ColorConfig::getInstance().getMuted(), 64.0f / 255.0f);
        } else {
            bgQuad.m_ulColor = m_fetchButtonHovered && state != FetchState::FETCHING
                ? ColorConfig::getInstance().getAccent()
                : PluginUtils::applyOpacity(ColorConfig::getInstance().getAccent(), 128.0f / 255.0f);
        }
        m_quads.push_back(bgQuad);
    }

    addString(compareLabel, rowX, currentY, Justify::LEFT, Fonts::getNormal(), compareColor, dim.fontSize);

    currentY += dim.lineHeightNormal;

    // === Empty row (no column headers) ===
    currentY += dim.lineHeightNormal;

    // === Record Rows (with Personal Best integration) ===
    // Get player's all-time PB for this track+bike
    const SessionData& session = PluginData::getInstance().getSessionData();
    const PersonalBestEntry* playerPB = nullptr;
    int playerPosition = -1;  // -1 = no PB, 0+ = position in records

    if (session.trackId[0] != '\0' && session.bikeName[0] != '\0') {
        playerPB = PersonalBestManager::getInstance().getPersonalBest(session.trackId, session.bikeName);
        if (playerPB && playerPB->isValid()) {
            playerPosition = findPlayerPositionInRecords(playerPB->lapTime);
        }
    }

    // Lambda to render a single record row
    // isPlayerRow: add highlight background, skip position column
    // sector1/2/3: -1 if not available (CBR or player PB)
    auto renderRecordRow = [&](int position, const char* rider, const char* bike, int laptime,
                               int sector1, int sector2, int sector3, const char* date, bool isPlayerRow) {
        // Add highlight background quad for player row
        if (isPlayerRow) {
            SPluginQuad_t highlight;
            float highlightX = START_X;
            float highlightY = currentY;
            applyOffset(highlightX, highlightY);
            setQuadPositions(highlight, highlightX, highlightY, backgroundWidth, dim.lineHeightNormal);
            highlight.m_iSprite = PluginConstants::SpriteIndex::SOLID_COLOR;
            highlight.m_ulColor = PluginUtils::applyOpacity(ColorConfig::getInstance().getAccent(), 80.0f / 255.0f);
            m_quads.push_back(highlight);
        }

        // Position (P1, P2, etc.) - skip for player row
        if (isColumnEnabled(COL_POS) && !isPlayerRow) {
            char posStr[8];
            snprintf(posStr, sizeof(posStr), "P%d", position);
            unsigned long posColor;
            if (position == 1) posColor = PodiumColors::GOLD;
            else if (position == 2) posColor = PodiumColors::SILVER;
            else if (position == 3) posColor = PodiumColors::BRONZE;
            else posColor = ColorConfig::getInstance().getPrimary();
            addString(posStr, m_columns.pos, currentY, Justify::LEFT, Fonts::getNormal(), posColor, dim.fontSize);
        }

        // Rider (truncate if too long)
        if (isColumnEnabled(COL_RIDER)) {
            char riderStr[16];
            size_t maxLen = COL_RIDER_WIDTH - 1;
            strncpy_s(riderStr, sizeof(riderStr), rider, maxLen);
            riderStr[maxLen] = '\0';
            // Player row keeps same column alignment (skip position but stay in rider column)
            addString(riderStr, m_columns.rider, currentY, Justify::LEFT, Fonts::getNormal(),
                      ColorConfig::getInstance().getPrimary(), dim.fontSize);
        }

        // Bike (truncate if too long)
        if (isColumnEnabled(COL_BIKE)) {
            char bikeStr[20];
            size_t maxLen = COL_BIKE_WIDTH - 1;
            strncpy_s(bikeStr, sizeof(bikeStr), bike, maxLen);
            bikeStr[maxLen] = '\0';
            addString(bikeStr, m_columns.bike, currentY, Justify::LEFT, Fonts::getNormal(),
                      ColorConfig::getInstance().getSecondary(), dim.fontSize);
        }

        // Sector times (displayed before lap time)
        if (isColumnEnabled(COL_SECTOR1)) {
            char sectorStr[12];
            if (sector1 > 0) {
                PluginUtils::formatSectorTime(sector1, sectorStr, sizeof(sectorStr));
                addString(sectorStr, m_columns.sector1, currentY, Justify::LEFT,
                          Fonts::getNormal(), ColorConfig::getInstance().getSecondary(), dim.fontSize);
            } else {
                addString("---.---", m_columns.sector1, currentY, Justify::LEFT,
                          Fonts::getNormal(), ColorConfig::getInstance().getMuted(), dim.fontSize);
            }
        }

        if (isColumnEnabled(COL_SECTOR2)) {
            char sectorStr[12];
            if (sector2 > 0) {
                PluginUtils::formatSectorTime(sector2, sectorStr, sizeof(sectorStr));
                addString(sectorStr, m_columns.sector2, currentY, Justify::LEFT,
                          Fonts::getNormal(), ColorConfig::getInstance().getSecondary(), dim.fontSize);
            } else {
                addString("---.---", m_columns.sector2, currentY, Justify::LEFT,
                          Fonts::getNormal(), ColorConfig::getInstance().getMuted(), dim.fontSize);
            }
        }

        if (isColumnEnabled(COL_SECTOR3)) {
            char sectorStr[12];
            if (sector3 > 0) {
                PluginUtils::formatSectorTime(sector3, sectorStr, sizeof(sectorStr));
                addString(sectorStr, m_columns.sector3, currentY, Justify::LEFT,
                          Fonts::getNormal(), ColorConfig::getInstance().getSecondary(), dim.fontSize);
            } else {
                addString("---.---", m_columns.sector3, currentY, Justify::LEFT,
                          Fonts::getNormal(), ColorConfig::getInstance().getMuted(), dim.fontSize);
            }
        }

        // Laptime
        if (isColumnEnabled(COL_LAPTIME)) {
            char laptimeStr[16];
            if (laptime > 0) {
                PluginUtils::formatLapTime(laptime, laptimeStr, sizeof(laptimeStr));
                addString(laptimeStr, m_columns.laptime, currentY, Justify::LEFT,
                          Fonts::getStrong(), ColorConfig::getInstance().getPrimary(), dim.fontSize);
            } else {
                addString(Placeholders::LAP_TIME, m_columns.laptime, currentY, Justify::LEFT,
                          Fonts::getStrong(), ColorConfig::getInstance().getMuted(), dim.fontSize);
            }
        }

        // Date
        if (isColumnEnabled(COL_DATE)) {
            addString(date && date[0] != '\0' ? date : "---", m_columns.date, currentY,
                      Justify::LEFT, Fonts::getNormal(), ColorConfig::getInstance().getTertiary(), dim.fontSize);
        }

        currentY += dim.lineHeightNormal;
    };

    FetchState currentState = m_fetchState.load();
    bool hasFetched = (currentState == FetchState::SUCCESS || !allRecords.empty());

    if (!hasFetched) {
        // Before fetch or error: show player's PB and/or status message
        bool hasPlayerRow = (playerPB && playerPB->isValid());
        if (hasPlayerRow) {
            const char* playerName = session.riderName[0] != '\0' ? session.riderName : "You";
            renderRecordRow(1, playerName, playerPB->bikeName.c_str(), playerPB->lapTime,
                            playerPB->sector1, playerPB->sector2, playerPB->sector3, "", true);
        }
        // Show status message (error or prompt to compare)
        const char* statusMessage = nullptr;
        char errorMessage[64];
        if (currentState == FetchState::FETCH_ERROR) {
            if (!lastError.empty()) {
                snprintf(errorMessage, sizeof(errorMessage), "Compare failed: %s", lastError.c_str());
            } else {
                strncpy_s(errorMessage, sizeof(errorMessage), "Compare failed. Try again.", sizeof(errorMessage) - 1);
            }
            statusMessage = errorMessage;
        } else if (!hasPlayerRow) {
            statusMessage = "Click Compare to load records.";
        }
        if (statusMessage) {
            if (hasPlayerRow) currentY += dim.lineHeightNormal * 0.5f;  // Gap after player row
            addString(statusMessage, contentStartX, currentY,
                      Justify::LEFT, Fonts::getNormal(), ColorConfig::getInstance().getMuted(), dim.fontSize);
            currentY += dim.lineHeightNormal;
        }
    } else if (allRecords.empty()) {
        // Fetched but no records found - show player's PB and/or message
        bool hasPlayerRow = (playerPB && playerPB->isValid());
        if (hasPlayerRow) {
            const char* playerName = session.riderName[0] != '\0' ? session.riderName : "You";
            renderRecordRow(1, playerName, playerPB->bikeName.c_str(), playerPB->lapTime,
                            playerPB->sector1, playerPB->sector2, playerPB->sector3, "", true);
            currentY += dim.lineHeightNormal * 0.5f;  // Gap after player row
        }
        // Always show "no records" message so user knows the fetch worked
        addString("No records found for this track/category.", contentStartX, currentY,
                  Justify::LEFT, Fonts::getNormal(), ColorConfig::getInstance().getMuted(), dim.fontSize);
        currentY += dim.lineHeightNormal;
    } else {
        // Has records - show with StandingsHud-style pagination
        // Strategy (like StandingsHud):
        // - If player is in top 3 (or no PB): show first N records with player inserted
        // - If player is beyond top 3: show top 3, then context around player position

        static constexpr int TOP_POSITIONS = 3;
        const char* playerName = session.riderName[0] != '\0' ? session.riderName : "You";
        bool hasPlayerPB = (playerPB && playerPB->isValid());

        // Format player's PB date from timestamp
        char playerDateStr[16] = "";
        if (hasPlayerPB && playerPB->timestamp > 0) {
            std::tm tm;
            if (localtime_s(&tm, &playerPB->timestamp) == 0) {
                snprintf(playerDateStr, sizeof(playerDateStr), "%04d-%02d-%02d",
                         tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday);
            }
        }

        // Helper lambda to render a range of server records, optionally inserting player
        auto renderRecordRange = [&](int startIdx, int endIdx, bool insertPlayer) {
            for (int i = startIdx; i <= endIdx && i < totalRecords; i++) {
                // Insert player row before this record if player position matches
                if (insertPlayer && hasPlayerPB && playerPosition == i) {
                    renderRecordRow(0, playerName, playerPB->bikeName.c_str(), playerPB->lapTime,
                                    playerPB->sector1, playerPB->sector2, playerPB->sector3, playerDateStr, true);
                }
                // Render the server record
                const auto& record = allRecords[i];
                renderRecordRow(record.position, record.rider, record.bike, record.laptime,
                                record.sector1, record.sector2, record.sector3, record.date, false);
            }
            // Insert player at end if they're after the last record in range
            if (insertPlayer && hasPlayerPB && playerPosition > endIdx && playerPosition <= endIdx + 1) {
                renderRecordRow(0, playerName, playerPB->bikeName.c_str(), playerPB->lapTime,
                                playerPB->sector1, playerPB->sector2, playerPB->sector3, playerDateStr, true);
            }
        };

        if (!hasPlayerPB || playerPosition < TOP_POSITIONS) {
            // Player is in top 3 (or no PB) - show first N records with player inserted
            int recordsToShow = std::min(totalRecords, m_recordsToShow - (hasPlayerPB ? 1 : 0));
            renderRecordRange(0, recordsToShow - 1, true);
        } else {
            // Player is beyond top 3 - show top 3, then context around player
            // 1. Show top 3 records
            int topToShow = std::min(totalRecords, TOP_POSITIONS);
            renderRecordRange(0, topToShow - 1, false);

            // 2. Calculate context window around player
            // Available rows = total - top3 - 1 (for player row)
            int availableRows = m_recordsToShow - TOP_POSITIONS - 1;
            int contextStart, contextEnd;

            if (playerPosition >= totalRecords) {
                // Player is slower than all fetched records - show bottom N records before them
                contextEnd = totalRecords - 1;
                contextStart = std::max(TOP_POSITIONS, totalRecords - availableRows);
            } else {
                // Player is within the records - show context around them
                int contextBefore = availableRows / 2;
                int contextAfter = availableRows - contextBefore - 1;

                contextStart = std::max(TOP_POSITIONS, playerPosition - contextBefore);
                contextEnd = std::min(totalRecords - 1, playerPosition + contextAfter);

                // Adjust if we hit boundaries - shift the window to use all available rows
                if (contextEnd == totalRecords - 1 && contextStart > TOP_POSITIONS) {
                    // Hit bottom - extend start upward to fill rows
                    contextStart = std::max(TOP_POSITIONS, contextEnd - availableRows + 1);
                } else if (contextStart == TOP_POSITIONS && contextEnd < totalRecords - 1) {
                    // Hit top (right after top 3) - extend end downward to fill rows
                    contextEnd = std::min(totalRecords - 1, contextStart + availableRows - 1);
                }
            }

            // 3. Render context around player
            for (int i = contextStart; i <= contextEnd && i < totalRecords; i++) {
                // Insert player row at correct position
                if (hasPlayerPB && playerPosition == i) {
                    renderRecordRow(0, playerName, playerPB->bikeName.c_str(), playerPB->lapTime,
                                    playerPB->sector1, playerPB->sector2, playerPB->sector3, playerDateStr, true);
                }
                const auto& record = allRecords[i];
                renderRecordRow(record.position, record.rider, record.bike, record.laptime,
                                record.sector1, record.sector2, record.sector3, record.date, false);
            }
            // Insert player at end if they're after the last context record
            if (hasPlayerPB && playerPosition > contextEnd) {
                renderRecordRow(0, playerName, playerPB->bikeName.c_str(), playerPB->lapTime,
                                playerPB->sector1, playerPB->sector2, playerPB->sector3, playerDateStr, true);
            }
        }
    }

    // === Footer Note ===
    if (m_bShowFooter) {
        // Skip to footer position (fixed row count ensures consistent height)
        // +1 for blank row gap before footer
        currentY = contentStartY + titleHeight + ((HEADER_ROWS - 1 + m_recordsToShow + 1) * dim.lineHeightNormal);

        // Line 1: "Records provided by <provider>" - use the provider records were fetched from
        const char* prefix = "Records provided by ";
        addString(prefix, contentStartX, currentY,
                  Justify::LEFT, Fonts::getNormal(), ColorConfig::getInstance().getMuted(), dim.fontSizeSmall);

        float prefixWidth = PluginUtils::calculateMonospaceTextWidth(static_cast<int>(strlen(prefix)), dim.fontSizeSmall);
        const char* providerName = getProviderDisplayName(m_recordsProvider);
        addString(providerName, contentStartX + prefixWidth, currentY,
                  Justify::LEFT, Fonts::getNormal(), ColorConfig::getInstance().getSecondary(), dim.fontSizeSmall);

        currentY += dim.lineHeightSmall;

        // Line 2: "Submit by playing on their servers"
        addString("Submit by playing on their servers", contentStartX, currentY,
                  Justify::LEFT, Fonts::getNormal(), ColorConfig::getInstance().getMuted(), dim.fontSizeSmall);
    }
}

// ============================================================================
// Public API for TimingHud Integration
// ============================================================================

int RecordsHud::getFastestRecordLapTime() const {
    std::lock_guard<std::mutex> lock(m_recordsMutex);
    if (m_records.empty()) return -1;
    // Records are sorted by lap time, first entry is fastest
    return m_records[0].laptime;
}

bool RecordsHud::getFastestRecordSectors(int& sector1, int& sector2, int& sector3) const {
    std::lock_guard<std::mutex> lock(m_recordsMutex);
    if (m_records.empty()) return false;

    const auto& fastest = m_records[0];
    if (!fastest.hasSectors()) return false;

    sector1 = fastest.sector1;
    sector2 = fastest.sector2;
    sector3 = fastest.sector3;
    return true;
}

void RecordsHud::resetToDefaults() {
    m_bVisible = false;
    m_bShowTitle = true;
    setTextureVariant(0);  // No texture by default
    m_fBackgroundOpacity = SettingsLimits::DEFAULT_OPACITY;
    m_fScale = 1.0f;
    setPosition(0.0055f, 0.4773f);
    m_provider = DataProvider::CBR;
    m_recordsProvider = DataProvider::CBR;
    m_categoryIndex = 0;
    m_lastSessionTrackId[0] = '\0';  // Reset so update() will pick up current session track
    m_lastSessionCategory[0] = '\0';  // Reset so update() will pick up current session category
    m_bAutoFetch = false;  // Auto-fetch disabled by default
    m_enabledColumns = COL_DEFAULT;
    m_recordsToShow = 4;  // Default to 4 rows
    m_bShowFooter = true;
    {
        std::lock_guard<std::mutex> lock(m_recordsMutex);
        m_records.clear();
    }
    m_fetchState = FetchState::IDLE;
    setDataDirty();
}
