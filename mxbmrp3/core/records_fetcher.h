// ============================================================================
// core/records_fetcher.h
// Background HTTP fetch + JSON parse for the external lap-records providers
// (CBR, MXB-Ranked). Extracted from RecordsHud so the HUD keeps presentation
// only; the fetcher owns the worker thread and the transport/parse code.
//
// Threading contract (moved verbatim from RecordsHud — see also the join
// ordering comment in HudManager::clear()):
//  - start() runs on the GAME thread with inputs the caller snapshotted there
//    (provider / track name / category — RecordsHud::startFetch()). The worker
//    never reads PluginData or game-thread-mutated state; everything it needs
//    is captured into the fetcher before the thread spawns.
//  - The result callback runs ON THE WORKER THREAD. The owner stores the
//    result under its own lock (RecordsHud::m_recordsMutex) and may notify
//    other HUDs (TimingHud) — which is why the owner's join entry point
//    (RecordsHud::joinFetchThread) must run before HudManager nulls its cached
//    HUD pointers.
//  - join() blocks until the worker is done. Safe to call multiple times.
//  - The worker body carries the top-level exception barrier: an uncaught
//    throw in a std::thread calls std::terminate() and kills the host game.
//
// Gating: GAME_HAS_RECORDS_PROVIDER (MX Bikes only) is a REGISTRATION/USE gate,
// exactly like RecordsHud itself — this TU compiles for every game (the vcxproj
// and the cross-build include it unconditionally, and RecordsHud, which is also
// compiled everywhere, links against it), but only RecordsHud ever instantiates
// it, and RecordsHud is only registered in HudManager under the flag.
// ============================================================================
#pragma once

#include <atomic>
#include <cstdint>
#include <functional>
#include <string>
#include <thread>
#include <vector>

class RecordsFetcher {
public:
    // Data providers (hardcoded endpoints). RecordsHud aliases this as
    // RecordsHud::DataProvider for the settings serde/tab code.
    enum class DataProvider : uint8_t {
        CBR = 0,
        MXB_RANKED = 1,
        COUNT
    };

    static constexpr int MAX_RECORDS = 50;  // API only returns 50 results
    static constexpr size_t MAX_RESPONSE_SIZE = 256 * 1024;  // 256KB max response to prevent memory exhaustion

    // Record entry from API response (aliased as RecordsHud::RecordEntry)
    struct RecordEntry {
        int position;
        char rider[64];
        char bike[64];
        int laptime;          // milliseconds
        int sector1;          // milliseconds (MXB-Ranked only, -1 if not available)
        int sector2;          // milliseconds (MXB-Ranked only, -1 if not available)
        int sector3;          // milliseconds (MXB-Ranked only, -1 if not available)
        int sector4;          // milliseconds (4-sector games only, -1 if not available)
        char date[32];        // Formatted date string

        RecordEntry() : position(0), laptime(-1), sector1(-1), sector2(-1), sector3(-1), sector4(-1) {
            rider[0] = '\0';
            bike[0] = '\0';
            date[0] = '\0';
        }

        bool hasSectors() const {
            return sector1 > 0 && sector2 > 0 && sector3 > 0;
        }
    };

    // One fetch's outcome, built entirely on the worker thread and handed to
    // the owner's callback (still on the worker thread).
    struct Result {
        bool parsed = false;             // transport OK + response parsed with the requested schema
        std::vector<RecordEntry> records;
        std::string apiNotice;           // CBR "notice" field (empty = none in this response)
        std::string error;               // set when !parsed ("Connection failed", "HTTP 500", "Parse error", ...)
        DataProvider provider = DataProvider::CBR;  // schema the response was requested/parsed with
    };
    using ResultCallback = std::function<void(Result&&)>;

    RecordsFetcher() = default;
    ~RecordsFetcher();

    RecordsFetcher(const RecordsFetcher&) = delete;
    RecordsFetcher& operator=(const RecordsFetcher&) = delete;

    // Start a fetch. The caller (RecordsHud::startFetch, game thread) is
    // responsible for single-flight gating (its FetchState CAS) and passes the
    // inputs it snapshotted on the game thread; any previous worker is joined
    // before the new one spawns.
    void start(DataProvider provider, std::string trackName, std::string category,
               ResultCallback onDone);

    // Join the worker if running. RecordsHud::joinFetchThread delegates here —
    // see the HudManager::clear() ordering contract above.
    void join();

    // The REAL parse path (response body -> records/notice), also used by the
    // canned-response test hook. Returns false on a JSON/parse error (out's
    // records/notice are then meaningless and must be ignored).
    static bool parseResponse(DataProvider provider, const std::string& response, Result& out);

    static std::string getProviderBaseUrl(DataProvider provider);
    static const char* getProviderDisplayName(DataProvider provider);

#if defined(MXBMRP3_TEST_BUILD)
    // Arm the fetch-worker stub (see MXBMRP3_Test_RecordsSetFetchStub): the
    // worker sleeps delayMs, then completes with `response` through the normal
    // parse/notify path instead of touching the network. delayMs < 0 disarms.
    static void testSetFetchStub(int delayMs, const char* response);
#endif

private:
    void performFetch();  // Runs in the worker thread
    std::string buildRequestUrl() const;
    // Deliver a transport/exception failure to the owner. noexcept-shaped: it
    // swallows anything thrown while building/delivering the error result so a
    // throw can never escape the worker thread (std::terminate).
    void completeError(const char* what) noexcept;

    std::thread m_thread;
    ResultCallback m_onDone;

    // Fetch inputs snapshotted on the game thread by the caller (RecordsHud::
    // startFetch) and passed into start(); read by buildRequestUrl()/
    // performFetch() on the worker thread. The worker must not touch PluginData
    // or game-thread-mutated state (the HUD's m_provider / m_categoryIndex /
    // m_categoryList) directly — everything it needs is captured here first,
    // while still on the game thread.
    DataProvider m_provider = DataProvider::CBR;  // Provider captured at fetch start
    std::string m_trackName;                      // Track name captured at fetch start
    std::string m_category;                       // Resolved category (empty => "All" / no filter)
};
