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
// Self-contained doctest; see run_tests.sh.
// ============================================================================
#define DOCTEST_CONFIG_IMPLEMENT
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
