// ============================================================================
// hud/records_hud.h
// Displays lap records fetched from external data providers via HTTP
// ============================================================================
#pragma once

#include "base_hud.h"
#include "../core/plugin_constants.h"
#include "../core/widget_constants.h"
#include <vector>
#include <string>
#include <thread>
#include <atomic>
#include <mutex>

class RecordsHud : public BaseHud {
public:
    // Column visibility flags (bitfield)
    static constexpr uint32_t COL_POS     = 1 << 0;  // Position (P1, P2, etc.)
    static constexpr uint32_t COL_RIDER   = 1 << 1;  // Rider name
    static constexpr uint32_t COL_BIKE    = 1 << 2;  // Bike name
    static constexpr uint32_t COL_LAPTIME = 1 << 3;  // Lap time
    static constexpr uint32_t COL_DATE    = 1 << 4;  // Date recorded
    static constexpr uint32_t COL_DEFAULT = COL_POS | COL_RIDER | COL_BIKE | COL_LAPTIME;  // Date disabled by default

    bool isColumnEnabled(uint32_t col) const {
        return (m_enabledColumns & col) != 0;
    }

    // Data providers (hardcoded endpoints)
    enum class DataProvider : uint8_t {
        CBR = 0,
        // Future providers can be added here
        COUNT
    };

    // Fetch state for UI feedback
    // Note: Using FETCH_ERROR instead of ERROR to avoid conflict with Windows macro
    enum class FetchState : uint8_t {
        IDLE,
        FETCHING,
        SUCCESS,
        FETCH_ERROR
    };

    // Click region types for interactive elements
    enum class ClickRegionType : uint8_t {
        PROVIDER_LEFT,
        PROVIDER_RIGHT,
        CATEGORY_LEFT,
        CATEGORY_RIGHT,
        FETCH_BUTTON
    };

    struct ClickRegion {
        float x, y, width, height;
        ClickRegionType type;
    };

    // Record entry from API response
    struct RecordEntry {
        int position;
        char rider[64];
        char bike[64];
        int laptime;          // milliseconds
        char date[32];        // Formatted date string

        RecordEntry() : position(0), laptime(-1) {
            rider[0] = '\0';
            bike[0] = '\0';
            date[0] = '\0';
        }
    };

    // Column positions helper struct
    struct ColumnPositions {
        float pos;
        float rider;
        float bike;
        float laptime;
        float date;

        ColumnPositions() : pos(0), rider(0), bike(0), laptime(0), date(0) {}
        ColumnPositions(float contentStartX, float scale, uint32_t enabledColumns);
    };

public:
    RecordsHud();
    virtual ~RecordsHud();

    void update() override;
    bool handlesDataType(DataChangeType dataType) const override;
    void resetToDefaults();

    // Allow SettingsHud and SettingsManager to access private members
    friend class SettingsHud;
    friend class SettingsManager;

protected:
    void rebuildLayout() override;

private:
    void rebuildRenderData() override;

    // HTTP fetch operations
    void startFetch();
    void performFetch();  // Runs in background thread
    void processFetchResult(const std::string& response);
    std::string buildRequestUrl() const;
    static std::string getProviderBaseUrl(DataProvider provider);
    static const char* getProviderDisplayName(DataProvider provider);

    // Category management
    void buildCategoryList();
    int findCategoryIndex(const char* category) const;
    const char* getCurrentCategoryDisplay() const;

    // Click handling
    void handleClick(float mouseX, float mouseY);
    bool isPointInRect(float x, float y, float rectX, float rectY, float width, float height) const;
    void cycleProvider(int direction);
    void cycleCategory(int direction);

    // HUD positioning constants
    static constexpr float START_X = HudPositions::CENTER_LEFT_X;
    static constexpr float START_Y = HudPositions::UPPER_Y;
    static constexpr int MAX_RECORDS = 10;
    static constexpr size_t MAX_RESPONSE_SIZE = 64 * 1024;  // 64KB max response to prevent memory exhaustion
    static constexpr int HEADER_ROWS = 3;  // Title + Provider/Category/Fetch + empty row (no column headers)
    static constexpr int FOOTER_ROWS = 3;  // Empty row + two lines of note

    // Column width constants (in character counts)
    // Each width = content chars + 1 gap (matches pattern used by other HUDs)
    // Total default (POS+RIDER+BIKE+LAPTIME): 4+13+18+8 = 43 chars (last col has no gap)
    static constexpr int COL_POS_WIDTH = 4;       // "P99" = 3 chars + 1 gap
    static constexpr int COL_RIDER_WIDTH = 13;    // Up to 12 chars displayed + 1 gap
    static constexpr int COL_BIKE_WIDTH = 18;     // Up to 17 chars displayed + 1 gap
    static constexpr int COL_LAPTIME_WIDTH = 9;   // M:SS.mmm = 8 chars + 1 gap
    static constexpr int COL_DATE_WIDTH = 11;     // YYYY-MM-DD = 10 chars + 1 gap

    // Data
    ColumnPositions m_columns;
    std::vector<RecordEntry> m_records;
    std::vector<ClickRegion> m_clickRegions;
    std::vector<std::string> m_categoryList;  // Available categories

    // Settings
    DataProvider m_provider;
    int m_categoryIndex;  // Index into m_categoryList (0 = "All")
    uint32_t m_enabledColumns = COL_DEFAULT;  // Bitfield of enabled columns
    int m_recordsToShow = 1;  // Number of records to display (1-10, default 1)
    char m_lastSessionCategory[64] = {0};  // Track session category to auto-update on bike change

    // Fetch state
    std::atomic<FetchState> m_fetchState;
    std::thread m_fetchThread;
    std::mutex m_recordsMutex;
    std::string m_lastError;
    std::string m_apiNotice;  // Notice from API response

    // UI state
    bool m_fetchButtonHovered;
    static constexpr unsigned long FETCH_RESULT_DISPLAY_MS = 3000;  // Show success/error for 3 seconds
    unsigned long m_fetchResultTimestamp;  // When fetch completed (for display timeout, from GetTickCount)
};
