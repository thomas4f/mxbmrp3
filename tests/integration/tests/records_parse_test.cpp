// ============================================================================
// tests/integration/tests/records_parse_test.cpp
// Records provider: response parsing + the fetch worker's shutdown contract.
//
// The records fetch (RecordsHud → RecordsFetcher) mixes user/auto-triggered
// network I/O with JSON parsing whose only previous exercise was a live in-game
// fetch. These tests pin the PARSE half headless by feeding canned response
// bodies through the REAL parse path (MXBMRP3_Test_RecordsParse — the same code
// the worker runs on a 200 response) and reading the parsed records back:
//   - a valid CBR / MXB-Ranked response maps every field (incl. the
//     seconds→milliseconds conversion and the date truncation),
//   - malformed / truncated / empty JSON is rejected without crashing and
//     yields zero records,
//   - absurd field values (multi-KB names, negative times, wrong-typed fields,
//     more rows than MAX_RECORDS) are handled sanely.
//
// The WORKER half is pinned with the fetch stub (MXBMRP3_Test_RecordsSetFetchStub):
// the real fetch thread sleeps, then completes through the normal parse/notify
// path (including the cross-HUD TimingHud touch) with a canned response instead
// of network I/O. That lets the last test hold a fetch in flight and shut the
// plugin down mid-fetch — pinning the documented join contract:
// HudManager::clear() joins the fetch thread BEFORE nulling the cached HUD
// pointers the worker dereferences on completion. Completing without a
// crash/hang IS the assertion.
//
// GAME_HAS_RECORDS_PROVIDER is MXB-only; on a build without the hooks the tests
// no-op (hasRecords() false).
// ============================================================================
#define DOCTEST_CONFIG_IMPLEMENT
#include "doctest.h"
#include "integration_main.h"
#include "plugin_host.h"

#include <string>

// Provider ids (RecordsHud::DataProvider / RecordsFetcher::DataProvider).
static constexpr int PROV_CBR = 0;
static constexpr int PROV_MXB_RANKED = 1;

// Fetch states (RecordsHud::FetchState).
static constexpr int FETCH_IDLE = 0;
static constexpr int FETCH_FETCHING = 1;
static constexpr int FETCH_SUCCESS = 2;
static constexpr int FETCH_ERROR = 3;

// Wine's GetTickCount() counts from wineserver start, and run_tests.sh restarts
// the server per test binary — so the plugin's 5 s fetch cooldown (measured
// against a zero-initialized start timestamp) could swallow the very first
// startFetch() of a fresh prefix. Wait past it once before driving a fetch.
static void waitPastFetchCooldown() {
    while (GetTickCount() < 6000) Sleep(100);
}

TEST_CASE("records parse: valid CBR and MXB-Ranked responses map every field") {
    PluginHost host(dllPath());
    REQUIRE(host.loaded());
    host.startup("Z:\\tmp\\mxbmrp3-tests\\records_parse\\");
    if (!host.hasRecords()) { MESSAGE("records hooks not exported (non-MXB build); skipping"); host.shutdown(); return; }

    // CBR schema: { notice, records:[{player, bike, laptime(ms), timestamp}] }.
    const char* cbr = R"({"notice":"Data by CBR","records":[
        {"player":"Alice","bike":"KTM 450 SX-F","laptime":95123,"timestamp":"2024-05-01T12:34:56Z"},
        {"player":"Bob","bike":"Honda CRF450R","laptime":96500,"timestamp":"2024-05-02T08:00:00Z"},
        {"player":"Cara","bike":"Yamaha YZ450F","laptime":97001,"timestamp":"2024-05-03T18:30:00Z"}]})";
    CHECK(host.recordsParse(PROV_CBR, cbr) == 3);
    CHECK(host.recordsCount() == 3);
    auto r0 = host.recordsGet(0);
    REQUIRE(r0.ok);
    CHECK(r0.rider == "Alice");
    CHECK(r0.bike == "KTM 450 SX-F");
    CHECK(r0.laptime == 95123);
    CHECK(r0.date == "2024-05-01");        // timestamp truncated to YYYY-MM-DD
    CHECK(r0.s1 == -1);                    // CBR carries no sector times
    CHECK(r0.s2 == -1);
    CHECK(r0.s3 == -1);
    auto r2 = host.recordsGet(2);
    REQUIRE(r2.ok);
    CHECK(r2.rider == "Cara");
    CHECK(r2.laptime == 97001);
    CHECK_FALSE(host.recordsGet(3).ok);    // out of range

    // MXB-Ranked schema: a bare array with times in SECONDS (floats) — parsed
    // to milliseconds — plus per-sector times and createDateTimeUtc.
    // 95.5 / 30.25 / 31.25 / 34.0 are exactly representable, so the ms values
    // are exact.
    const char* ranked = R"([
        {"name":"Dave","bike":"Kawasaki KX450","lapTime":95.5,
         "sector1":30.25,"sector2":31.25,"sector3":34.0,
         "createDateTimeUtc":"2024-06-01T10:00:00Z"},
        {"name":"Eve","bike":"GasGas MC450F","lapTime":96.0,
         "sector1":31.0,"sector2":32.0,"sector3":33.0,
         "createDateTimeUtc":"2024-06-02T11:00:00Z"}])";
    CHECK(host.recordsParse(PROV_MXB_RANKED, ranked) == 2);
    auto d = host.recordsGet(0);
    REQUIRE(d.ok);
    CHECK(d.rider == "Dave");
    CHECK(d.bike == "Kawasaki KX450");
    CHECK(d.laptime == 95500);
    CHECK(d.s1 == 30250);
    CHECK(d.s2 == 31250);
    CHECK(d.s3 == 34000);
    CHECK(d.date == "2024-06-01");
    auto e = host.recordsGet(1);
    REQUIRE(e.ok);
    CHECK(e.rider == "Eve");
    CHECK(e.laptime == 96000);

    host.shutdown();
}

TEST_CASE("records parse: malformed / truncated / empty / mis-shaped JSON is safe") {
    PluginHost host(dllPath());
    REQUIRE(host.loaded());
    host.startup("Z:\\tmp\\mxbmrp3-tests\\records_parse\\");
    if (!host.hasRecords()) { MESSAGE("records hooks not exported (non-MXB build); skipping"); host.shutdown(); return; }

    // Outright parse failures: rejected (-1), no records, no crash.
    CHECK(host.recordsParse(PROV_CBR, "") == -1);                              // empty body
    CHECK(host.recordsCount() == 0);
    CHECK(host.recordsParse(PROV_CBR, "this is not json") == -1);              // garbage
    CHECK(host.recordsParse(PROV_CBR, R"({"records":[{"player":"a")") == -1);  // truncated mid-object
    CHECK(host.recordsParse(PROV_MXB_RANKED, R"([{"name":)") == -1);           // truncated mid-array
    CHECK(host.recordsCount() == 0);

    // Valid JSON with the WRONG shape parses "successfully" to zero records
    // (the provider found nothing to show — not an error).
    CHECK(host.recordsParse(PROV_CBR, "{}") == 0);                 // no records key
    CHECK(host.recordsParse(PROV_CBR, R"({"records":42})") == 0);  // records not an array
    CHECK(host.recordsParse(PROV_CBR, "null") == 0);               // null document
    CHECK(host.recordsParse(PROV_CBR, "[]") == 0);                 // CBR expects an object
    CHECK(host.recordsParse(PROV_MXB_RANKED, R"({"records":[]})") == 0);  // ranked expects an array
    CHECK(host.recordsCount() == 0);

    host.shutdown();
}

TEST_CASE("records parse: absurd field values are handled sanely") {
    PluginHost host(dllPath());
    REQUIRE(host.loaded());
    host.startup("Z:\\tmp\\mxbmrp3-tests\\records_parse\\");
    if (!host.hasRecords()) { MESSAGE("records hooks not exported (non-MXB build); skipping"); host.shutdown(); return; }

    // Huge strings: an 8 KB rider name and bike name must be truncated to the
    // fixed 64-byte entry fields (63 chars + NUL), not overflow or throw.
    std::string hugeName(8192, 'A');
    std::string hugeBike(8192, 'B');
    std::string body = std::string(R"({"records":[{"player":")") + hugeName +
                       R"(","bike":")" + hugeBike + R"(","laptime":90000,)" +
                       R"("timestamp":"2024-01-01T00:00:00Z"}]})";
    CHECK(host.recordsParse(PROV_CBR, body) == 1);
    auto huge = host.recordsGet(0);
    REQUIRE(huge.ok);
    CHECK(huge.laptime == 90000);
    CHECK(huge.rider.size() == 63);                       // truncated, NUL-terminated
    CHECK(huge.rider == std::string(63, 'A'));
    CHECK(huge.bike == std::string(63, 'B'));

    // Negative + wrong-typed + missing fields: stored/skipped without crashing.
    // A negative laptime is stored as-is (the renderer treats <= 0 as "no time");
    // a string laptime is ignored (stays -1); missing fields keep entry defaults.
    const char* weird = R"({"records":[
        {"player":"Neg","bike":"B","laptime":-5000,"timestamp":"2024-01-01T00:00:00Z"},
        {"player":"Str","bike":"B","laptime":"fast","timestamp":"x"},
        {}]})";
    CHECK(host.recordsParse(PROV_CBR, weird) == 3);
    auto neg = host.recordsGet(0);
    REQUIRE(neg.ok);
    CHECK(neg.laptime == -5000);
    auto str = host.recordsGet(1);
    REQUIRE(str.ok);
    CHECK(str.laptime == -1);                              // non-number ignored
    CHECK(str.date.empty());                               // "x" too short for a date
    auto empty = host.recordsGet(2);
    REQUIRE(empty.ok);
    CHECK(empty.rider.empty());
    CHECK(empty.laptime == -1);

    // More rows than MAX_RECORDS (50): the parse caps, it doesn't grow unbounded.
    std::string many = R"({"records":[)";
    for (int i = 0; i < 60; ++i) {
        if (i) many += ',';
        many += R"({"player":"P)" + std::to_string(i) + R"(","bike":"B","laptime":)" +
                std::to_string(90000 + i) + "}";
    }
    many += "]}";
    CHECK(host.recordsParse(PROV_CBR, many) == 50);
    CHECK(host.recordsCount() == 50);
    CHECK(host.recordsGet(49).ok);
    CHECK_FALSE(host.recordsGet(50).ok);

    host.shutdown();
}

TEST_CASE("records fetch: stubbed worker completes through the real thread path") {
    PluginHost host(dllPath());
    REQUIRE(host.loaded());
    host.startup("Z:\\tmp\\mxbmrp3-tests\\records_parse\\");
    if (!host.hasRecords()) { MESSAGE("records hooks not exported (non-MXB build); skipping"); host.shutdown(); return; }

    // Arm the stub, start a REAL fetch (thread + state gate + completion), and
    // wait for the worker to land the canned records.
    waitPastFetchCooldown();
    host.recordsSetFetchStub(50, R"({"records":[
        {"player":"Worker","bike":"Thread 450","laptime":91234,
         "timestamp":"2024-07-01T00:00:00Z"}]})");
    REQUIRE(host.recordsStartFetch());
    int state = FETCH_FETCHING;
    for (int i = 0; i < 200 && state == FETCH_FETCHING; ++i) {   // <= ~4 s
        Sleep(20);
        state = host.recordsFetchState();
    }
    CHECK(state == FETCH_SUCCESS);
    CHECK(host.recordsCount() == 1);
    auto rec = host.recordsGet(0);
    REQUIRE(rec.ok);
    CHECK(rec.rider == "Worker");
    CHECK(rec.laptime == 91234);

    host.shutdown();
    (void)FETCH_IDLE; (void)FETCH_ERROR;
}

TEST_CASE("records fetch: shutdown during an in-flight fetch joins the worker (no crash, no hang)") {
    PluginHost host(dllPath());
    REQUIRE(host.loaded());
    host.startup("Z:\\tmp\\mxbmrp3-tests\\records_parse\\");
    if (!host.hasRecords()) { MESSAGE("records hooks not exported (non-MXB build); skipping"); host.shutdown(); return; }

    // Hold the worker in flight for ~200 ms, then shut down IMMEDIATELY. The
    // worker's completion dereferences the cached TimingHud pointer, so
    // HudManager::clear() must join the fetch thread BEFORE nulling the cached
    // HUD pointers (the documented ordering in hud_manager.cpp). If that order
    // ever regresses, this crashes or hangs; completing IS the assertion.
    waitPastFetchCooldown();
    host.recordsSetFetchStub(200, R"({"records":[
        {"player":"Racer","bike":"Bike","laptime":90000,
         "timestamp":"2024-07-01T00:00:00Z"}]})");
    REQUIRE(host.recordsStartFetch());
    CHECK(host.recordsFetchState() == FETCH_FETCHING);

    host.shutdown();   // must block on the join, then tear down cleanly

    CHECK(true);       // reached = the wine process survived the mid-fetch teardown
}
