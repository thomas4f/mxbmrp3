// ============================================================================
// tests/integration/tests/atomic_writer_test.cpp
// The shared atomic file writer (core/atomic_file_writer.cpp): a write lands
// whole, leaves no temp behind, and cleans up after a writer that never got
// to its rename.
//
// THE BUG THIS PINS. Two instances of the plugin in one game (a duplicate .dlo
// in plugins\ - a real support case) write the same files at launch, and a
// shared temp name let one instance's truncate-and-write race the other's
// rename. The first fix suffixed the temp with the process id -- but one game
// is one process, so both instances still computed the same name and the race
// was unchanged; and once temp names vary at all, a temp a killed writer left
// behind is never overwritten by the next write, so every crash mid-write
// added a file to Documents for good. The name now carries the module's own
// identity and a sequence, and a successful write sweeps the stale temps next
// to its target (ten minutes old or more; a younger one may be in flight).
//
// The writer is Win32 (MoveFileExA, FindFirstFileA), so it runs inside the
// DLL under Wine through MXBMRP3_Test_WriteFileAtomic; the test plants the
// orphans and reads the results back through the same Z:\ paths.
//
// THE WORKER (second case). Saves are handed to the writer's thread so the game
// thread never waits on the disk; what must hold is that nothing is lost or
// reordered on the way: a later write to a file still queued replaces it, files
// land in the order given, and Shutdown() drains the queue -- a clean exit
// (menu quit or Alt-F4) has every file on disk. The log is flushed by the same
// thread, so its last lines must be there after Shutdown() as well. The harness
// runs the writer inline by default; this case switches the worker on.
//
// A FAILED WRITE IS RETRIED (third case). A manager's dirty flag clears when its
// bytes are handed to the writer, so without AtomicFileWriter::needsRetry a
// write that then failed was never attempted again unless something else
// changed. Driven through the stats file, whose target is made a directory so
// the replace fails, then made writable again with nothing new to save.
// ============================================================================
#include "doctest.h"
#include "integration_main.h"
#include "plugin_host.h"
#include <chrono>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

namespace fs = std::filesystem;

namespace {
constexpr const char* SAVE = "Z:\\tmp\\mxbmrp3-tests\\atomic_writer\\";
const fs::path DIR = "Z:\\tmp\\mxbmrp3-tests\\atomic_writer\\mxbmrp3";
const std::string TARGET = (DIR / "target.json").string();

std::string readFile(const fs::path& p) {
    std::ifstream f(p, std::ios::binary);
    return std::string((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
}

void plant(const fs::path& p, std::chrono::minutes age) {
    { std::ofstream f(p, std::ios::binary | std::ios::trunc); f << "half-written"; }
    fs::last_write_time(p, fs::file_time_type::clock::now() - age);
}

// Every "<target>.*.tmp" sibling, by name.
std::vector<std::string> temps() {
    std::vector<std::string> out;
    for (const auto& e : fs::directory_iterator(DIR)) {
        const std::string n = e.path().filename().string();
        if (n.rfind("target.json.", 0) == 0 && n.size() > 4 && n.compare(n.size() - 4, 4, ".tmp") == 0) {
            out.push_back(n);
        }
    }
    return out;
}
}  // namespace

TEST_CASE("a write lands whole and leaves no temp; stale temps next to it are swept, a fresh one is not") {
    PluginHost host(dllPath());
    REQUIRE(host.loaded());
    host.startup(SAVE);   // creates <save>\mxbmrp3\
    REQUIRE(fs::is_directory(DIR));
    for (const auto& n : temps()) fs::remove(DIR / n);
    fs::remove(fs::path(TARGET));

    REQUIRE(host.writeFileAtomic(TARGET, "one"));
    CHECK(readFile(TARGET) == "one");
    REQUIRE(host.writeFileAtomic(TARGET, "two"));
    CHECK(readFile(TARGET) == "two");
    CHECK(temps().empty());   // the temp was moved into place, not left beside it

    // What a killed writer leaves: a unique-named temp an hour old, the
    // pre-unique-name "<target>.tmp" an hour old, and one from a minute ago
    // that may be another instance's write in flight.
    plant(DIR / "target.json.4242-7f00-3.tmp", std::chrono::minutes(60));
    plant(DIR / "target.json.tmp", std::chrono::minutes(60));
    plant(DIR / "target.json.4243-7f11-1.tmp", std::chrono::minutes(1));
    REQUIRE(temps().size() == 3);

    REQUIRE(host.writeFileAtomic(TARGET, "three"));
    CHECK(readFile(TARGET) == "three");
    const auto left = temps();
    CHECK_MESSAGE(left.size() == 1, "expected only the fresh temp to survive, " << left.size() << " left");
    CHECK(fs::exists(DIR / "target.json.4243-7f11-1.tmp"));
    CHECK_FALSE(fs::exists(DIR / "target.json.4242-7f00-3.tmp"));
    CHECK_FALSE(fs::exists(DIR / "target.json.tmp"));

    // And a later write from this instance still leaves nothing behind.
    fs::remove(DIR / "target.json.4243-7f11-1.tmp");
    REQUIRE(host.writeFileAtomic(TARGET, "four"));
    CHECK(readFile(TARGET) == "four");
    CHECK(temps().empty());
    host.shutdown();
}

TEST_CASE("the worker: a queued write is replaced by a later one, files land in order, Shutdown() drains the queue and the log") {
    const std::string other = (DIR / "other.json").string();
    const fs::path log = DIR / "mxbmrp3_log.txt";
    PluginHost host(dllPath());
    REQUIRE(host.loaded());
    host.setAsyncWrites(true);
    host.startup(SAVE);
    REQUIRE(host.writerRunning());
    fs::remove(fs::path(TARGET));
    fs::remove(fs::path(other));

    // Round trip through the queue.
    REQUIRE(host.writeFileAtomic(TARGET, "queued"));
    CHECK(readFile(TARGET) == "queued");

    // Three submits, one flush: the target's second state took the first one's
    // slot, and the other file is written too.
    host.submitWrite(TARGET, "a1");
    host.submitWrite(other, "b1");
    host.submitWrite(TARGET, "a2");
    host.flushWrites();
    CHECK(readFile(TARGET) == "a2");
    CHECK(readFile(other) == "b1");
    CHECK(temps().empty());

    // Queued and not waited for: Shutdown() must write it before it returns.
    host.submitWrite(TARGET, "final");
    host.shutdown();
    CHECK(readFile(TARGET) == "final");

    // The buffered log reached the file: the writer's own start line, and the
    // Logger's last line, written after the worker was joined.
    const std::string text = readFile(log);
    CHECK(text.find("disk writer thread started") != std::string::npos);
    CHECK(text.find("disk writer thread stopped") != std::string::npos);
    CHECK(text.find("Logger shutting down") != std::string::npos);
    CHECK(text.find("\r\n") != std::string::npos);   // CRLF, as the ofstream wrote it
    fs::remove(fs::path(other));
}

TEST_CASE("a failed write is retried at the next save even with nothing new to save") {
    const fs::path stats = DIR / "mxbmrp3_stats.json";
    fs::remove_all(stats);
    PluginHost host(dllPath());
    REQUIRE(host.loaded());
    host.startup(SAVE);
    REQUIRE(host.hasStatsOdometer());

    // Something to save: a PB lap.
    host.eventInit("TestTrack", "Alice");
    host.raceEvent("TestTrack");
    host.session(/*RACE1=*/6, /*numLaps=*/10, /*lengthMs=*/0);
    host.addEntry(10, "Alice");
    host.runInit(6);
    host.raceLap(6, 10, 1, 90000, 2);

    // The target is a directory, so the atomic replace fails.
    fs::remove_all(stats);
    fs::create_directory(stats);
    host.statsSave();
    REQUIRE(host.writeNeedsRetry(stats.string()));
    CHECK(fs::is_directory(stats));

    // Writable again; nothing has changed since, and the save still writes.
    fs::remove_all(stats);
    host.statsSave();
    CHECK_FALSE(host.writeNeedsRetry(stats.string()));
    CHECK(readFile(stats).find("90000") != std::string::npos);
    host.shutdown();
}
