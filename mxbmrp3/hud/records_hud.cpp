// ============================================================================
// hud/records_hud.cpp
// Displays lap records fetched from external data providers via HTTP
// ============================================================================
#include "records_hud.h"

#include <cstring>
#include <cstdio>
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

// Use WinHTTP for HTTPS support (built into Windows, no external dependencies)
#include <windows.h>
#undef min
#undef max
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
    pos = x;
    if (enabledColumns & COL_POS) x += PluginUtils::calculateMonospaceTextWidth(COL_POS_WIDTH, scaledFontSize);

    rider = x;
    if (enabledColumns & COL_RIDER) x += PluginUtils::calculateMonospaceTextWidth(COL_RIDER_WIDTH, scaledFontSize);

    bike = x;
    if (enabledColumns & COL_BIKE) x += PluginUtils::calculateMonospaceTextWidth(COL_BIKE_WIDTH, scaledFontSize);

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
    , m_fetchButtonHovered(false)
    , m_fetchResultTimestamp(0)
{
    DEBUG_INFO("RecordsHud created");
    setDraggable(true);

    // Set defaults
    m_bShowTitle = true;
    m_fBackgroundOpacity = SettingsLimits::DEFAULT_OPACITY;
    setPosition(-0.2365f, 0.333f);

    // Initialize column positions
    m_columns = ColumnPositions(START_X + Padding::HUD_HORIZONTAL, m_fScale, m_enabledColumns);

    // Pre-allocate vectors
    m_quads.reserve(2);  // Background + fetch button
    m_strings.reserve(60);  // Header rows + 10 record rows * 5 cols + footer
    m_records.reserve(MAX_RECORDS);
    m_clickRegions.reserve(5);

    // Build initial category list
    buildCategoryList();

    // Note: Category auto-detection happens in update() when session data becomes available

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
        default:
            return "";
    }
}

const char* RecordsHud::getProviderDisplayName(DataProvider provider) {
    switch (provider) {
        case DataProvider::CBR:
            return "CBR";
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

    // Build query parameters
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

void RecordsHud::startFetch() {
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
    char* pszOutBuffer;
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
            DEBUG_WARN_F("RecordsHud: Response size limit exceeded (%zu bytes)", responseBody.size() + dwSize);
            break;
        }

        pszOutBuffer = new char[dwSize + 1];
        if (!pszOutBuffer) break;

        ZeroMemory(pszOutBuffer, dwSize + 1);

        if (WinHttpReadData(hRequest, pszOutBuffer, dwSize, &dwDownloaded)) {
            responseBody.append(pszOutBuffer, dwDownloaded);
        }

        delete[] pszOutBuffer;

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
        processFetchResult(responseBody);
        m_fetchState = FetchState::SUCCESS;
        DEBUG_INFO("RecordsHud: Fetch successful");
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

void RecordsHud::processFetchResult(const std::string& response) {
    try {
        nlohmann::json j = nlohmann::json::parse(response);

        // Lock for all member variable writes
        std::lock_guard<std::mutex> lock(m_recordsMutex);

        // Extract notice if present
        if (j.contains("notice") && j["notice"].is_string()) {
            m_apiNotice = j["notice"].get<std::string>();
        }

        // Parse records array
        if (j.contains("records") && j["records"].is_array()) {
            m_records.clear();

            int position = 1;
            for (const auto& record : j["records"]) {
                if (m_records.size() >= MAX_RECORDS) break;

                RecordEntry entry;
                entry.position = position++;

                // Parse rider/player name
                if (record.contains("player") && record["player"].is_string()) {
                    std::string player = record["player"].get<std::string>();
                    strncpy_s(entry.rider, sizeof(entry.rider), player.c_str(), sizeof(entry.rider) - 1);
                }

                // Parse bike name
                if (record.contains("bike") && record["bike"].is_string()) {
                    std::string bike = record["bike"].get<std::string>();
                    strncpy_s(entry.bike, sizeof(entry.bike), bike.c_str(), sizeof(entry.bike) - 1);
                }

                // Parse laptime (milliseconds)
                if (record.contains("laptime") && record["laptime"].is_number()) {
                    entry.laptime = record["laptime"].get<int>();
                }

                // Parse timestamp and format as date
                if (record.contains("timestamp") && record["timestamp"].is_string()) {
                    std::string timestamp = record["timestamp"].get<std::string>();
                    // Extract YYYY-MM-DD from ISO 8601 format
                    if (timestamp.length() >= 10) {
                        strncpy_s(entry.date, sizeof(entry.date), timestamp.substr(0, 10).c_str(), sizeof(entry.date) - 1);
                    }
                }

                m_records.push_back(entry);
            }

            DEBUG_INFO_F("RecordsHud: Parsed %zu records", m_records.size());
        }
    } catch (const std::exception& e) {
        std::lock_guard<std::mutex> lock(m_recordsMutex);
        m_lastError = "JSON parse error";
        DEBUG_WARN_F("RecordsHud: JSON parse error: %s", e.what());
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

bool RecordsHud::isPointInRect(float x, float y, float rectX, float rectY, float width, float height) const {
    return x >= rectX && x <= rectX + width && y >= rectY && y <= rectY + height;
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
    m_categoryIndex = (m_categoryIndex + direction + count) % count;
}

// ============================================================================
// Update and Rendering
// ============================================================================

bool RecordsHud::handlesDataType(DataChangeType dataType) const {
    return (dataType == DataChangeType::SessionData);  // Rebuild when track/category changes
}

void RecordsHud::update() {
    // Clear records when event ends (track becomes empty)
    const SessionData& session = PluginData::getInstance().getSessionData();
    if (session.trackId[0] == '\0' && !m_records.empty()) {
        std::lock_guard<std::mutex> lock(m_recordsMutex);
        m_records.clear();
        m_fetchState = FetchState::IDLE;
        m_lastSessionCategory[0] = '\0';  // Reset tracked category
        setDataDirty();
    }

    // Auto-update category when session category changes (e.g., bike swap or event start)
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

    // Standard dirty flag handling
    if (isDataDirty()) {
        rebuildRenderData();
        clearDataDirty();
        clearLayoutDirty();
    } else if (isLayoutDirty()) {
        rebuildLayout();
        clearLayoutDirty();
    }
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

    // Copy records for display (minimize mutex hold time)
    std::vector<RecordEntry> displayRecords;
    {
        std::lock_guard<std::mutex> lock(m_recordsMutex);
        int copyCount = std::min(static_cast<int>(m_records.size()), m_recordsToShow);
        if (copyCount > 0) {
            displayRecords.assign(m_records.begin(), m_records.begin() + copyCount);
        }
    }
    int displayCount = static_cast<int>(displayRecords.size());
    int totalRows = HEADER_ROWS + m_recordsToShow + FOOTER_ROWS;  // Fixed height based on setting

    // Calculate background width based on enabled columns
    // Note: padding is added by calculateBackgroundWidth(), don't double-count
    int bgWidthChars = 0;
    if (m_enabledColumns & COL_POS) bgWidthChars += COL_POS_WIDTH;
    if (m_enabledColumns & COL_RIDER) bgWidthChars += COL_RIDER_WIDTH;
    if (m_enabledColumns & COL_BIKE) bgWidthChars += COL_BIKE_WIDTH;
    if (m_enabledColumns & COL_LAPTIME) bgWidthChars += COL_LAPTIME_WIDTH;
    if (m_enabledColumns & COL_DATE) bgWidthChars += COL_DATE_WIDTH;
    // Remove trailing gap from last visible column (gap not needed after last column)
    if (!(m_enabledColumns & COL_DATE) && (m_enabledColumns & COL_LAPTIME)) {
        bgWidthChars -= 1;  // LAPTIME is last, remove its gap
    }
    bgWidthChars = std::max(bgWidthChars, 34);  // Minimum width for controls row

    float backgroundWidth = calculateBackgroundWidth(bgWidthChars);
    float backgroundHeight = calculateBackgroundHeight(totalRows - 1);

    setBounds(START_X, START_Y, START_X + backgroundWidth, START_Y + backgroundHeight);
    addBackgroundQuad(START_X, START_Y, backgroundWidth, backgroundHeight);

    float contentStartX = START_X + dim.paddingH;
    float contentStartY = START_Y + dim.paddingV;
    float currentY = contentStartY;

    // Recalculate column positions based on enabled columns
    m_columns = ColumnPositions(contentStartX, m_fScale, m_enabledColumns);

    // === Title Row ===
    addTitleString("Records", contentStartX, currentY, Justify::LEFT,
                   Fonts::ENTER_SANSMAN, ColorConfig::getInstance().getPrimary(), dim.fontSizeLarge);
    currentY += titleHeight;

    // === Provider / Category / Fetch Row ===
    // Note: Click regions store positions WITHOUT offset - offset is added during hit testing
    float rowX = contentStartX;
    float charWidth = PluginUtils::calculateMonospaceTextWidth(1, dim.fontSize);

    // Provider selector: "< CBR >"
    addString("<", rowX, currentY, Justify::LEFT, Fonts::ROBOTO_MONO, ColorConfig::getInstance().getSecondary(), dim.fontSize);
    m_clickRegions.push_back({rowX, currentY, charWidth * 2, dim.lineHeightNormal, ClickRegionType::PROVIDER_LEFT});
    rowX += charWidth * 2;  // "< "

    addString(getProviderDisplayName(m_provider), rowX, currentY, Justify::LEFT,
              Fonts::ROBOTO_MONO, ColorConfig::getInstance().getPrimary(), dim.fontSize);
    rowX += PluginUtils::calculateMonospaceTextWidth(static_cast<int>(strlen(getProviderDisplayName(m_provider))), dim.fontSize);

    addString(" >", rowX, currentY, Justify::LEFT, Fonts::ROBOTO_MONO, ColorConfig::getInstance().getSecondary(), dim.fontSize);
    m_clickRegions.push_back({rowX, currentY, charWidth * 2, dim.lineHeightNormal, ClickRegionType::PROVIDER_RIGHT});
    rowX += charWidth * 4;  // " > " + gap

    // Category selector: "< MX1-2T OEM >" - use fixed width so arrows don't jump
    static constexpr int CATEGORY_WIDTH_CHARS = 10;  // Longest: "MX1-2T OEM"
    float categoryFixedWidth = PluginUtils::calculateMonospaceTextWidth(CATEGORY_WIDTH_CHARS, dim.fontSize);

    addString("<", rowX, currentY, Justify::LEFT, Fonts::ROBOTO_MONO, ColorConfig::getInstance().getSecondary(), dim.fontSize);
    m_clickRegions.push_back({rowX, currentY, charWidth * 2, dim.lineHeightNormal, ClickRegionType::CATEGORY_LEFT});
    rowX += charWidth * 2;  // "< "

    const char* catName = getCurrentCategoryDisplay();
    addString(catName, rowX, currentY, Justify::LEFT,
              Fonts::ROBOTO_MONO, ColorConfig::getInstance().getPrimary(), dim.fontSize);
    rowX += categoryFixedWidth;  // Fixed width regardless of actual name length

    addString(" >", rowX, currentY, Justify::LEFT, Fonts::ROBOTO_MONO, ColorConfig::getInstance().getSecondary(), dim.fontSize);
    m_clickRegions.push_back({rowX, currentY, charWidth * 2, dim.lineHeightNormal, ClickRegionType::CATEGORY_RIGHT});
    rowX += charWidth * 4;  // " > " + gap

    // Fetch button
    const char* fetchLabel = "[ Fetch ]";
    FetchState state = m_fetchState.load();
    if (state == FetchState::FETCHING) {
        fetchLabel = "[ ... ]";
    } else if (state == FetchState::SUCCESS) {
        fetchLabel = "[ OK ]";
    } else if (state == FetchState::FETCH_ERROR) {
        fetchLabel = "[ ERR ]";
    }

    unsigned long fetchColor = ColorConfig::getInstance().getSecondary();
    if (m_fetchButtonHovered && state != FetchState::FETCHING) {
        fetchColor = ColorConfig::getInstance().getPositive();  // Highlight on hover
    } else if (state == FetchState::SUCCESS) {
        fetchColor = ColorConfig::getInstance().getPositive();
    } else if (state == FetchState::FETCH_ERROR) {
        fetchColor = ColorConfig::getInstance().getNegative();
    }

    float fetchWidth = PluginUtils::calculateMonospaceTextWidth(static_cast<int>(strlen("[ Fetch ]")), dim.fontSize);
    m_clickRegions.push_back({rowX, currentY, fetchWidth, dim.lineHeightNormal, ClickRegionType::FETCH_BUTTON});
    addString(fetchLabel, rowX, currentY, Justify::LEFT, Fonts::ROBOTO_MONO, fetchColor, dim.fontSize);

    currentY += dim.lineHeightNormal;

    // === Empty row (no column headers) ===
    currentY += dim.lineHeightNormal;

    // === Record Rows ===
    // Note: Using local displayRecords copy (mutex already released above)
    if (displayRecords.empty()) {
        // Show appropriate message based on fetch state
        const char* emptyMessage = "No records loaded. Click Fetch to load.";
        FetchState currentState = m_fetchState.load();
        if (currentState == FetchState::SUCCESS) {
            emptyMessage = "No records found for this track/category.";
        } else if (currentState == FetchState::FETCH_ERROR) {
            emptyMessage = "Fetch failed. Try again.";
        }
        addString(emptyMessage, contentStartX, currentY,
                  Justify::LEFT, Fonts::ROBOTO_MONO, ColorConfig::getInstance().getMuted(), dim.fontSize);
        currentY += dim.lineHeightNormal;
    } else {
        for (const auto& record : displayRecords) {
            // Position (P1, P2, etc.)
            if (isColumnEnabled(COL_POS)) {
                char posStr[8];
                snprintf(posStr, sizeof(posStr), "P%d", record.position);
                unsigned long posColor = ColorConfig::getInstance().getPrimary();
                if (record.position == 1) posColor = PodiumColors::GOLD;
                else if (record.position == 2) posColor = PodiumColors::SILVER;
                else if (record.position == 3) posColor = PodiumColors::BRONZE;
                addString(posStr, m_columns.pos, currentY, Justify::LEFT, Fonts::ROBOTO_MONO, posColor, dim.fontSize);
            }

            // Rider (truncate with ... if too long)
            if (isColumnEnabled(COL_RIDER)) {
                char riderStr[16];
                size_t maxLen = COL_RIDER_WIDTH - 1;  // 1 char gap (matches other HUDs)
                if (strlen(record.rider) > maxLen) {
                    strncpy_s(riderStr, sizeof(riderStr), record.rider, maxLen - 3);
                    riderStr[maxLen - 3] = '\0';
                    strcat_s(riderStr, sizeof(riderStr), "...");
                } else {
                    strncpy_s(riderStr, sizeof(riderStr), record.rider, sizeof(riderStr) - 1);
                }
                addString(riderStr, m_columns.rider, currentY, Justify::LEFT, Fonts::ROBOTO_MONO, ColorConfig::getInstance().getPrimary(), dim.fontSize);
            }

            // Bike (truncate with ... if too long)
            if (isColumnEnabled(COL_BIKE)) {
                char bikeStr[20];
                size_t maxLen = COL_BIKE_WIDTH - 1;  // 1 char gap (matches other HUDs)
                if (strlen(record.bike) > maxLen) {
                    strncpy_s(bikeStr, sizeof(bikeStr), record.bike, maxLen - 3);
                    bikeStr[maxLen - 3] = '\0';
                    strcat_s(bikeStr, sizeof(bikeStr), "...");
                } else {
                    strncpy_s(bikeStr, sizeof(bikeStr), record.bike, sizeof(bikeStr) - 1);
                }
                addString(bikeStr, m_columns.bike, currentY, Justify::LEFT, Fonts::ROBOTO_MONO, ColorConfig::getInstance().getSecondary(), dim.fontSize);
            }

            // Laptime
            if (isColumnEnabled(COL_LAPTIME)) {
                char laptimeStr[16];
                if (record.laptime > 0) {
                    PluginUtils::formatLapTime(record.laptime, laptimeStr, sizeof(laptimeStr));
                    addString(laptimeStr, m_columns.laptime, currentY, Justify::LEFT,
                              Fonts::ROBOTO_MONO_BOLD, ColorConfig::getInstance().getPrimary(), dim.fontSize);
                } else {
                    addString(Placeholders::LAP_TIME, m_columns.laptime, currentY, Justify::LEFT,
                              Fonts::ROBOTO_MONO_BOLD, ColorConfig::getInstance().getMuted(), dim.fontSize);
                }
            }

            // Date
            if (isColumnEnabled(COL_DATE)) {
                addString(record.date[0] != '\0' ? record.date : "---", m_columns.date, currentY,
                          Justify::LEFT, Fonts::ROBOTO_MONO, ColorConfig::getInstance().getTertiary(), dim.fontSize);
            }

            currentY += dim.lineHeightNormal;
        }
    }

    // === Empty Row + Footer Notes ===
    currentY += dim.lineHeightNormal;  // Empty row for spacing

    // Line 1: Data provider (normal font)
    char providerText[64];
    snprintf(providerText, sizeof(providerText), "Data provided by %s", getProviderDisplayName(m_provider));
    addString(providerText, contentStartX, currentY,
              Justify::LEFT, Fonts::ROBOTO_MONO, ColorConfig::getInstance().getMuted(), dim.fontSize);
    currentY += dim.lineHeightNormal;

    // Line 2: How to submit (small font)
    addString("Submit records by playing on their servers", contentStartX, currentY,
              Justify::LEFT, Fonts::ROBOTO_MONO, ColorConfig::getInstance().getMuted(), dim.fontSizeSmall);
}

void RecordsHud::resetToDefaults() {
    m_bVisible = true;
    m_bShowTitle = true;
    m_bShowBackgroundTexture = false;
    m_fBackgroundOpacity = SettingsLimits::DEFAULT_OPACITY;
    m_fScale = 1.0f;
    setPosition(-0.2365f, 0.333f);
    m_provider = DataProvider::CBR;
    m_categoryIndex = 0;
    m_lastSessionCategory[0] = '\0';  // Reset so update() will pick up current session category
    m_enabledColumns = COL_DEFAULT;
    m_recordsToShow = 3;  // Default to 3 rows
    {
        std::lock_guard<std::mutex> lock(m_recordsMutex);
        m_records.clear();
    }
    m_fetchState = FetchState::IDLE;
    setDataDirty();
}
