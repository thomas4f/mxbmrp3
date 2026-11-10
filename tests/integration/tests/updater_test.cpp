// ============================================================================
// tests/integration/tests/updater_test.cpp
// Update install pipeline under a locked/in-use target — the "files being locked
// during updates" concern. Drives the real backup → extract → verify → rollback
// path (UpdateDownloader::extractAndInstall) against a temp directory with an
// in-memory ZIP, via the MXBMRP3_Test_ExtractAndInstall hook (no network).
//
// Three cases:
//   A. happy path — a valid release installs; the .dlo and a data file land with
//      the new content.
//   B. target locked for the whole attempt — the backup move can't proceed, so
//      the install aborts and the ORIGINAL .dlo is left intact (no half-install).
//   C. transient lock — the target is locked, then released ~300ms in; the move's
//      retry/backoff catches the release and the install SUCCEEDS. This is the
//      red→green for the retry: a single-shot MoveFile would have failed at B's
//      first attempt and lost the user their install.
//
// Wine honors an exclusive (FILE_SHARE_NONE) handle against MoveFile — a locked
// source yields ERROR_SHARING_VIOLATION — so the lock is real under the harness.
// A second case pins the Updates tab's step percentages: Extract and Install
// count 0 → 100% and only then read OK (the rows used to jump straight from
// nothing to OK), Install too under the Updates tab's debug mode, which skips
// the verify that counts it, while the near-instant Backup shows no percentage. Download's
// counter goes through the same setStepProgress() but needs WinHTTP, so no
// headless test reaches it.
//
// Self-contained doctest; see run_tests.sh.
// ============================================================================
#include "doctest.h"
#include "integration_main.h"
#include "plugin_host.h"
#include "ini.h"        // readFile / writeFile (binary)
#include "zipwrite.h"   // build a stored ZIP in memory
#include <thread>
#include <string>

// The cross-build is MX Bikes, so the release .dlo is named this.
static const char* DLO = "mxbmrp3.dlo";

namespace {

std::string join(const std::string& dir, const char* leaf) { return dir + leaf; }

void mkdirW(const std::string& winPath) { CreateDirectoryA(winPath.c_str(), nullptr); }

bool exists(const std::string& winPath) {
    return GetFileAttributesA(winPath.c_str()) != INVALID_FILE_ATTRIBUTES;
}

// Open an exclusive (no-share) handle on a file → blocks MoveFile on it.
HANDLE lockExclusive(const std::string& winPath) {
    return CreateFileA(winPath.c_str(), GENERIC_READ, 0 /*no sharing*/, nullptr,
                       OPEN_EXISTING, 0, nullptr);
}

// A two-file release: the plugin .dlo (goes to root) + one data file (goes under
// mxbmrp3_data/). "NEWDLO-v2" is the marker we assert got installed.
std::string releaseZip() {
    return zipw::build({
        { DLO,              "NEWDLO-v2" },
        { "assets/data.bin", "DATA123" },
    });
}

}  // namespace

TEST_CASE("updater: install survives a locked/in-use target with rollback + retry") {
    PluginHost host(dllPath());
    REQUIRE(host.loaded());
    host.startup("Z:\\tmp\\mxbmrp3-tests\\updater\\");

    const std::string root = "Z:\\tmp\\mxbmrp3-tests\\updater\\";
    const std::string zip = releaseZip();
    std::string err;

    // --- A: happy path — a valid release installs -----------------------------
    {
        const std::string dir = root + "a\\";
        mkdirW(dir);
        ini::writeFile(join(dir, DLO), "ORIG-DLO");        // an existing install to back up
        mkdirW(dir + "mxbmrp3_data");
        ini::writeFile(dir + "mxbmrp3_data\\old.txt", "old");

        const int r = host.extractAndInstall(dir.c_str(), zip, err);
        REQUIRE_MESSAGE(r != -1, "MXBMRP3_Test_ExtractAndInstall not exported");
        INFO("err: " << err);
        CHECK(r == 1);
        CHECK(ini::readFile(join(dir, DLO)) == "NEWDLO-v2");                 // new .dlo installed
        CHECK(ini::readFile(dir + "mxbmrp3_data\\assets\\data.bin") == "DATA123");
    }

    // --- B: locked for the whole attempt — abort, original intact ------------
    {
        const std::string dir = root + "b\\";
        mkdirW(dir);
        ini::writeFile(join(dir, DLO), "ORIG-DLO");

        HANDLE lock = lockExclusive(join(dir, DLO));
        REQUIRE(lock != INVALID_HANDLE_VALUE);

        const int r = host.extractAndInstall(dir.c_str(), zip, err);
        CloseHandle(lock);

        CHECK(r == 0);                                                       // install aborted
        CHECK(ini::readFile(join(dir, DLO)) == "ORIG-DLO");                  // original untouched
        CHECK_FALSE(exists(dir + "mxbmrp3_data\\assets\\data.bin"));         // nothing half-extracted
    }

    // --- C: transient lock released mid-attempt — retry succeeds -------------
    {
        const std::string dir = root + "c\\";
        mkdirW(dir);
        ini::writeFile(join(dir, DLO), "ORIG-DLO");

        HANDLE lock = lockExclusive(join(dir, DLO));
        REQUIRE(lock != INVALID_HANDLE_VALUE);
        // Release the lock ~300ms in; the move's backoff window (~1.5s) catches it.
        std::thread releaser([lock] { Sleep(300); CloseHandle(lock); });

        const int r = host.extractAndInstall(dir.c_str(), zip, err);
        releaser.join();

        INFO("err: " << err);
        CHECK(r == 1);                                                       // retry caught the release
        CHECK(ini::readFile(join(dir, DLO)) == "NEWDLO-v2");                 // installed after retry
    }

    host.shutdown();
}

TEST_CASE("updater: slow steps count 0 to 100 percent before OK") {
    PluginHost host(dllPath());
    REQUIRE(host.loaded());
    host.startup("Z:\\tmp\\mxbmrp3-tests\\updater\\");

    // Five files of different sizes, so Extract (by bytes) and Install (by file)
    // both pass through values between 0 and 100.
    const std::string zip = zipw::build({
        { DLO,               std::string(4000, 'D') },
        { "assets/a.bin",    std::string(1000, 'a') },
        { "assets/b.bin",    std::string(2000, 'b') },
        { "assets/c.bin",    std::string(3000, 'c') },
        { "assets/d.bin",    std::string(500,  'd') },
    });
    const std::string root = "Z:\\tmp\\mxbmrp3-tests\\updater\\";
    enum { BACKUP = 2, EXTRACT = 3, INSTALL = 4 };  // UpdateDownloader::Step

    auto run = [&](const char* leaf, int step, bool debugMode = false) {
        const std::string dir = root + leaf;
        mkdirW(dir);
        ini::writeFile(join(dir, DLO), "ORIG-DLO");
        int ok = 0;
        std::vector<int> trace = host.updaterStepTrace(dir.c_str(), zip, step, ok, debugMode);
        REQUIRE_MESSAGE(ok != -1, "MXBMRP3_Test_UpdaterStepTrace not exported");
        CHECK(ok == 1);
        return trace;
    };

    for (int step : { EXTRACT, INSTALL }) {
        const std::vector<int> t = run(step == EXTRACT ? "pe\\" : "pi\\", step);
        INFO("step " << step << ", " << t.size() << " samples");
        REQUIRE(t.size() >= 4);                  // 0, something in between, 100, OK
        CHECK(t.front() == 0);                   // starts at 0%, not blank
        CHECK(t[t.size() - 2] == 100);           // reaches 100% ...
        CHECK(t.back() == 101);                  // ... and only then reads OK
        bool between = false;
        for (size_t i = 1; i + 1 < t.size(); ++i) {
            CHECK(t[i] > t[i - 1]);              // only counts up
            if (t[i] > 0 && t[i] < 100) between = true;
        }
        CHECK(between);
    }

    // Backup is two renames: no percentage, straight to OK.
    const std::vector<int> b = run("pb\\", BACKUP);
    REQUIRE(b.size() == 2);
    CHECK(b[0] == -1);
    CHECK(b[1] == 101);

    // Debug mode skips the verify that counts Install, and the row read 0% then
    // OK. It still lands on 100% first.
    const std::vector<int> d = run("pd\\", INSTALL, true);
    REQUIRE(d.size() == 3);
    CHECK(d[0] == 0);
    CHECK(d[1] == 100);
    CHECK(d[2] == 101);

    host.shutdown();
}
