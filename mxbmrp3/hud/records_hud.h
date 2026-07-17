// ============================================================================
// hud/records_hud.h
// Displays lap records fetched from external data providers via HTTP
// ============================================================================
#pragma once

#include "base_hud.h"
#include "../core/plugin_constants.h"
#include "../core/widget_constants.h"
#include "../core/records_fetcher.h"
#include <vector>
#include <string>
#include <atomic>
#include <mutex>

class RecordsHud : public BaseHud {
public:
    // Column visibility flags (bitfield)
    enum ColumnFlags : uint32_t {
        COL_POS     = 1 << 0,  // Position (P1, P2, etc.) - always shown
        COL_RIDER   = 1 << 1,  // Rider name - always shown
        COL_BIKE    = 1 << 2,  // Bike name - always shown
        COL_LAPTIME = 1 << 3,  // Lap time - always shown
        COL_DATE    = 1 << 4,  // Date recorded (optional)
        COL_SECTORS = 1 << 5,  // Sector times S1/S2/S3/S4 (optional, MXB-Ranked only)
        COL_CORE    = COL_POS | COL_RIDER | COL_BIKE | COL_LAPTIME,  // Always-on columns
        COL_DEFAULT = COL_CORE  // Sectors and date disabled by default
    };

    bool isColumnEnabled(uint32_t col) const {
        return (m_enabledColumns & col) != 0;
    }

    // Data providers (hardcoded endpoints). The type lives in RecordsFetcher
    // (core/records_fetcher.h) with the transport/parse code; aliased so
    // existing consumers (settings serde/tabs) keep spelling RecordsHud::DataProvider.
    using DataProvider = RecordsFetcher::DataProvider;

    // Check if current provider supports sector times
    bool providerHasSectors() const {
        return m_provider == DataProvider::MXB_RANKED;
    }

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

    // Record entry from API response (defined next to the parse code that
    // fills it — core/records_fetcher.h)
    using RecordEntry = RecordsFetcher::RecordEntry;

    // Column positions helper struct
    struct ColumnPositions {
        float pos;
        float rider;
        float bike;
        float laptime;
        float sector1;
        float sector2;
        float sector3;
        float sector4;
        float date;

        ColumnPositions() : pos(0), rider(0), bike(0), laptime(0), sector1(0), sector2(0), sector3(0), sector4(0), date(0) {}
        ColumnPositions(float contentStartX, float scale, uint32_t enabledColumns);
    };

public:
    RecordsHud();
    virtual ~RecordsHud();

    void update() override;
    bool handlesDataType(DataChangeType dataType) const override;
    const char* getIconName() const override { return "hud-records"; }
    void resetToDefaults();

    // Join the background fetch thread if running. Must be called before
    // HudManager nulls its cached HUD pointers (the worker touches TimingHud
    // on completion). Safe to call multiple times.
    void joinFetchThread();

    // Get the fastest record lap time (for TimingHud gap comparison)
    // Returns -1 if no records available
    int getFastestRecordLapTime() const;

    // Get sector times for the fastest record (for TimingHud gap comparison)
    // Returns false if sectors not available
    bool getFastestRecordSectors(int& sector1, int& sector2, int& sector3, int& sector4) const;

    // Allow SettingsHud and SettingsManager to access private members
    friend class SettingsHud;
    friend class SettingsManager;

#if defined(MXBMRP3_TEST_BUILD)
    // --- Test seams (tests/integration/tests/records_parse_test.cpp) --------
    // Never compiled into a shipping (MSVC) build. See core/test_hooks.cpp for
    // the exported MXBMRP3_Test_Records* wrappers.
    //
    // Run a canned HTTP response body through the REAL parse path, exactly as
    // the fetch worker would for that provider (0=CBR, 1=MXB_RANKED). Returns
    // false on a parse error (same signal the worker turns into FETCH_ERROR).
    bool testParseResponse(int provider, const std::string& response);
    // Read back the parsed records (copied under m_recordsMutex).
    int  testRecordCount() const;
    bool testGetRecord(int index, RecordEntry& out) const;
    // Start a real fetch through the same cooldown/state gate as the Compare
    // button, and read the fetch state (FetchState as int).
    void testStartFetch() { startFetch(); }
    int  testFetchState() const { return static_cast<int>(m_fetchState.load()); }
    // Arm the fetch-worker stub: the worker sleeps delayMs, then completes with
    // `response` through the normal parse/notify path instead of touching the
    // network — keeps the worker's lifetime (and its cross-HUD TimingHud
    // completion) real while a test stays offline and bounded. delayMs < 0
    // disarms. Used by the shutdown-during-fetch join-contract test.
    static void testSetFetchStub(int delayMs, const char* response);
#endif

protected:
    void rebuildLayout() override;

private:
    void rebuildRenderData() override;

    // HTTP fetch operations. The transport + parse live in RecordsFetcher
    // (core/records_fetcher.h); the HUD snapshots the fetch inputs on the game
    // thread (startFetch), and stores the fetcher's result (onFetchComplete —
    // runs ON THE WORKER THREAD, so it only touches m_recordsMutex-guarded
    // members, atomics, and setDataDirty()).
    void startFetch();
    void onFetchComplete(RecordsFetcher::Result&& result);
    // Store a successfully parsed result under m_recordsMutex (shared by the
    // worker completion and the canned-response test seam).
    void storeParsedRecords(RecordsFetcher::Result&& result);

    // Category management
    void buildCategoryList();
    int findCategoryIndex(const char* category) const;
    const char* getCurrentCategoryDisplay() const;

    // Click handling
    void handleClick(float mouseX, float mouseY);
    void cycleProvider(int direction);
    void cycleCategory(int direction);

    // Personal best integration
    // Returns the position the player's PB would have in the records list (1-based)
    // Returns -1 if no PB exists, 0 if faster than all records
    // Takes a snapshot of the records (copied under m_recordsMutex by the caller)
    // because the live m_records may be mutated by the fetch thread.
    int findPlayerPositionInRecords(const std::vector<RecordEntry>& records, int playerPBTime) const;

    // Base position (0,0) - actual position comes from m_fOffsetX/m_fOffsetY
    static constexpr float START_X = 0.0f;
    static constexpr float START_Y = 0.0f;
    static constexpr int MAX_RECORDS = RecordsFetcher::MAX_RECORDS;  // API only returns 50 results
    static constexpr int HEADER_ROWS = 3;  // Title + Provider/Category/Compare + empty row (no column headers)
    static constexpr int FOOTER_ROWS = 1;  // Gap row only (footer text renders in bottom padding)

    // Column width constants (in character counts)
    // Each width = content chars + 1 gap (matches pattern used by other HUDs)
    // Total default (POS+RIDER+BIKE+LAPTIME): 4+13+18+8 = 43 chars (last col has no gap)
    static constexpr int COL_POS_WIDTH = 4;       // "P99" = 3 chars + 1 gap
    static constexpr int COL_RIDER_WIDTH = 13;    // Up to 12 chars displayed + 1 gap
    static constexpr int COL_BIKE_WIDTH = 18;     // Up to 17 chars displayed + 1 gap
    static constexpr int COL_LAPTIME_WIDTH = 9;   // M:SS.mmm = 8 chars + 1 gap
    static constexpr int COL_SECTOR_WIDTH = 9;    // M:SS.mmm = 8 chars + 1 gap
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
    int m_recordsToShow = 8;  // Number of records to display (3-30, default 8 -> 15-row HUD)
    char m_lastSessionTrackName[256] = {0};  // Track session trackName to auto-fetch on event start
    char m_lastSessionCategory[64] = {0};  // Track session category to auto-fetch on bike change
    bool m_bAutoFetch = false;  // Auto-fetch records when entering event (default off)
    bool m_bShowHeaders = false;  // Show a column-header row (fills the row HEADER_ROWS already reserves)
    bool m_bShowFooter = true;  // Show provider attribution at bottom (configurable via INI)

    // Fetch state. The worker thread + fetch-input snapshot live in
    // RecordsFetcher; startFetch() (game thread) snapshots the inputs
    // (m_provider / session trackName / resolved category) and passes them in,
    // so the worker never touches PluginData or game-thread-mutated state.
    // m_fetchState stays here: the CAS single-flight gate runs on the game
    // thread in startFetch(), the worker's completion (onFetchComplete) writes
    // SUCCESS/FETCH_ERROR, and the settings tab reads it for the button label.
    std::atomic<FetchState> m_fetchState;
    mutable std::mutex m_recordsMutex;
    std::string m_lastError;
    std::string m_apiNotice;  // Notice from API response
    DataProvider m_recordsProvider;  // Provider that current records were fetched from
    // Declared LAST among these members deliberately: members destroy in reverse
    // declaration order, and the fetcher's destructor joins a worker whose
    // completion callback touches the mutex-guarded members above — so the
    // fetcher must be destroyed FIRST, while the mutex and result strings are
    // still alive. (The orchestrated path joins long before any destructor runs;
    // this ordering is the backstop's backstop.)
    RecordsFetcher m_fetcher;

    // UI state
    bool m_fetchButtonHovered;
    static constexpr unsigned long FETCH_RESULT_DISPLAY_MS = 3000;  // Show success/error for 3 seconds
    static constexpr unsigned long FETCH_COOLDOWN_MS = 5000;  // Minimum time between fetches (prevent spam)
    // Atomic: written by the fetch worker, read by the game thread (display
    // timeout / cooldown checks). Plain loads/stores only.
    std::atomic<unsigned long> m_fetchResultTimestamp;  // When fetch completed (for display timeout, from GetTickCount)
    std::atomic<unsigned long> m_fetchStartTimestamp;   // When fetch started (for cooldown)
    bool m_wasOnCooldown;                  // Track cooldown state for refresh trigger
};
