// ============================================================================
// tests/unit/test_pb_trace_store.cpp
// The persisted all-time PB gap tables (core/pb_trace_store.h). Pins:
//   - encode/decode round-trip a table exactly, holes included (-1), and
//     decode refuses a short, long or non-numeric string rather than half-fill
//   - store() keeps the FASTEST lap per track+bike: a slower lap is refused,
//     a faster one replaces; save() writes the file, the way the stats flush
//   - the file round-trips through load(): entries, lap times and every slot
//   - a malformed entry is skipped on its own -- a wrong JSON type included,
//     which read through value() would throw past the per-entry skip and
//     drop every other trace -- and so is a table with a sample past its
//     lap time (read as a gap, corruption would be a huge one); a malformed
//     FILE loads empty; clear() empties the store and the file
//   - store() drops a sample past the lap time rather than keep what load()
//     refuses: the tracker's tail is wall-clock after the S2 resync and can
//     overrun the official time by a few ms at the line. Kept as-is, the
//     entry worked in-session, was skipped at the next launch, erased by the
//     next save and replaced by the next (slower) lap on that key
// The file lives under <savePath>/mxbmrp3/ like the stats file; the test
// creates that folder in a temp dir, which is the only place it touches.
// ============================================================================
#include "doctest.h"
#include "core/pb_trace_store.h"

#include <cstdio>
#include <filesystem>
#include <fstream>
#include <string>

namespace {

PbGapTracker::Table linearTable(int lapTimeMs, int holeFrom = -1, int holeTo = -1) {
    PbGapTracker::Table t{};
    for (int i = 0; i < PbGapTracker::NUM_POINTS; ++i) {
        if (i >= holeFrom && i < holeTo) continue;
        t[i] = PbGapTracker::Point{ i * lapTimeMs / PbGapTracker::NUM_POINTS, true };
    }
    return t;
}

bool same(const PbGapTracker::Table& a, const PbGapTracker::Table& b) {
    for (int i = 0; i < PbGapTracker::NUM_POINTS; ++i) {
        if (a[i].valid != b[i].valid) return false;
        if (a[i].valid && a[i].elapsedMs != b[i].elapsedMs) return false;
    }
    return true;
}

// A fresh save path per test: <tmp>/mxbmrp3_pb_trace_test_<n>/ with the
// mxbmrp3/ folder the plugin's startup would have created.
std::string freshSavePath() {
    static int n = 0;
    const std::filesystem::path root =
        std::filesystem::temp_directory_path() / ("mxbmrp3_pb_trace_test_" + std::to_string(++n));
    std::filesystem::remove_all(root);
    std::filesystem::create_directories(root / "mxbmrp3");
    return root.string();
}

}  // namespace

TEST_CASE("samplesFit refuses a sample past the lap time") {
    CHECK(PbTraceStore::samplesFit(linearTable(60000), 60000));
    CHECK(PbTraceStore::samplesFit(linearTable(60000, 984, 1000), 59000));   // the slots past 59 s are holes
    CHECK_FALSE(PbTraceStore::samplesFit(linearTable(60000), 59000));
}

TEST_CASE("encode/decode round-trip a table, holes included, and refuse a malformed string") {
    const PbGapTracker::Table t = linearTable(60000, 400, 410);
    const std::string text = PbTraceStore::encode(t);
    PbGapTracker::Table back{};
    REQUIRE(PbTraceStore::decode(text, back));
    CHECK(same(t, back));
    CHECK_FALSE(back[405].valid);
    CHECK(back[399].valid);

    PbGapTracker::Table untouched = linearTable(1000);
    CHECK_FALSE(PbTraceStore::decode("1,2,3", untouched));               // short
    CHECK_FALSE(PbTraceStore::decode(text + ",7", untouched));           // long
    CHECK_FALSE(PbTraceStore::decode("1,x,3", untouched));               // junk
    CHECK_FALSE(PbTraceStore::decode(text + "junk", untouched));         // trailing
    CHECK(same(untouched, linearTable(1000)));                           // a refusal leaves the table alone
}

TEST_CASE("store keeps the last PB lap per track+bike and the file round-trips") {
    PbTraceStore& store = PbTraceStore::getInstance();
    const std::string save = freshSavePath();
    store.load(save);
    REQUIRE(store.size() == 0);

    // The caller hands over only the stats file's new PB, so the store follows
    // it either way: a slower one is a PB after the stats were cleared or restored.
    CHECK(store.store("southwick", "Test 450", linearTable(61000), 61000));
    CHECK(store.store("southwick", "Test 450", linearTable(60000), 60000));         // faster: replaces
    CHECK(store.store("southwick", "Test 450", linearTable(62000), 62000));         // slower: replaces too
    CHECK(store.store("southwick", "Test 450", linearTable(59000, 10, 20), 59000));
    CHECK(store.store("southwick", "Other 450", linearTable(64000), 64000));
    CHECK_FALSE(store.store("southwick", "Test 450", linearTable(58000), 0));       // no lap time: refused
    CHECK_FALSE(store.store("", "Test 450", linearTable(58000), 58000));            // no key: refused
    CHECK(store.size() == 2);

    int ms = 0;
    const PbGapTracker::Table* found = store.find("southwick", "Test 450", &ms);
    REQUIRE(found);
    CHECK(ms == 59000);
    CHECK(same(*found, linearTable(59000, 10, 20)));
    CHECK(store.find("southwick", "Nope", &ms) == nullptr);
    CHECK(store.find("elsewhere", "Test 450", &ms) == nullptr);

    // Nothing is on disk until the flush the stats file shares; then it round-trips.
    store.save();
    store.load(save);
    REQUIRE(store.size() == 2);
    found = store.find("southwick", "Test 450", &ms);
    REQUIRE(found);
    CHECK(ms == 59000);
    CHECK(same(*found, linearTable(59000, 10, 20)));
    found = store.find("southwick", "Other 450", &ms);
    REQUIRE(found);
    CHECK(ms == 64000);

    store.clear();
    CHECK(store.size() == 0);
    store.load(save);
    CHECK(store.size() == 0);   // the file was rewritten empty, not left behind
}

TEST_CASE("a malformed entry is skipped alone; a malformed file loads empty") {
    PbTraceStore& store = PbTraceStore::getInstance();
    const std::string save = freshSavePath();
    const std::string path = PbTraceStore::filePathFor(save);
    {
        std::ofstream f(path);
        f << "{\"version\":1,\"traces\":{"
             "\"southwick|Test 450\":{\"lapTimeMs\":60000,\"table\":\"" << PbTraceStore::encode(linearTable(60000)) << "\"},"
             "\"southwick|Broken\":{\"lapTimeMs\":60000,\"table\":\"1,2,3\"},"
             "\"southwick|NoTime\":{\"lapTimeMs\":0,\"table\":\"" << PbTraceStore::encode(linearTable(60000)) << "\"},"
             "\"southwick|WrongType\":{\"lapTimeMs\":\"60000\",\"table\":\"" << PbTraceStore::encode(linearTable(60000)) << "\"},"
             "\"southwick|TableNotString\":{\"lapTimeMs\":60000,\"table\":[1,2]},"
             "\"southwick|PastTheLap\":{\"lapTimeMs\":50000,\"table\":\"" << PbTraceStore::encode(linearTable(60000)) << "\"},"
             "\"southwick|NotAnObject\":5"
             "}}\n";
    }
    store.load(save);
    CHECK(store.size() == 1);   // the wrong-typed, out-of-range and shapeless entries each fail alone
    int ms = 0;
    CHECK(store.find("southwick", "Test 450", &ms) != nullptr);

    {
        std::ofstream f(path);
        f << "{ this is not json";
    }
    store.load(save);
    CHECK(store.size() == 0);
}

TEST_CASE("store drops a sample past the lap time, so the entry survives its own load") {
    PbTraceStore& store = PbTraceStore::getInstance();
    const std::string save = freshSavePath();
    store.load(save);
    REQUIRE(store.size() == 0);

    // A 60 s lap whose last two slots overran the official time by a few ms.
    PbGapTracker::Table t = linearTable(60000);
    t[998] = PbGapTracker::Point{ 60002, true };
    t[999] = PbGapTracker::Point{ 60003, true };
    REQUIRE_FALSE(PbTraceStore::samplesFit(t, 60000));
    REQUIRE(store.store("southwick", "Test 450", t, 60000));

    int ms = 0;
    const PbGapTracker::Table* kept = store.find("southwick", "Test 450", &ms);
    REQUIRE(kept);
    CHECK(ms == 60000);
    CHECK(PbTraceStore::samplesFit(*kept, 60000));
    CHECK_FALSE((*kept)[998].valid);
    CHECK_FALSE((*kept)[999].valid);
    CHECK((*kept)[997].valid);                       // the rest of the lap is untouched
    CHECK((*kept)[997].elapsedMs == t[997].elapsedMs);

    // The round-trip keeps it: a second launch still has the ghost.
    store.save();
    store.load(save);
    REQUIRE(store.size() == 1);
    REQUIRE(store.find("southwick", "Test 450", &ms));
    CHECK(ms == 60000);
    store.clear();
}
