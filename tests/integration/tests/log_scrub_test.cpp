// ============================================================================
// tests/integration/tests/log_scrub_test.cpp
// The log never names the player's Windows account. Pins, through the real
// plugin and the real mxbmrp3_log.txt:
//
//   1. A save path under the user profile (where the games keep it:
//      C:\Users\<account>\Documents\PiBoSo\...) is logged with the profile
//      replaced by a placeholder, in every line that names it: the log file,
//      the crash-dump folder, settings, stats, PB traces, assets.
//   2. The lines are still there: the scrub keeps the path below the account
//      folder, which is what a missing-file report is diagnosed from.
//
// Through v1.31's pre-release every one of those lines carried the account
// name, and logs are posted with bug reports. The pure matching rules (case,
// separators, C:\Users\tom vs C:\Users\tomas, the player name left alone) are
// pinned in tests/unit/test_log_scrub.cpp; this is the wiring.
// ============================================================================
#include "doctest.h"
#include "integration_main.h"
#include "plugin_host.h"
#include "ini.h"

#include <windows.h>

#include <algorithm>
#include <cctype>
#include <string>

namespace {
std::string lower(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return s;
}
}  // namespace

TEST_CASE("log scrub: a save path under the user profile never reaches the log") {
    char profile[MAX_PATH] = { 0 };
    const DWORD n = GetEnvironmentVariableA("USERPROFILE", profile, MAX_PATH);
    REQUIRE(n > 0);
    REQUIRE(n < MAX_PATH);

    const std::string save = std::string(profile) + "\\mxbmrp3-log-scrub\\";
    CreateDirectoryA(save.c_str(), nullptr);
    DeleteFileA((save + "mxbmrp3\\mxbmrp3_log.txt").c_str());

    {
        PluginHost host(dllPath());
        REQUIRE(host.loaded());
        host.startup(save.c_str());
        host.shutdown();   // flushes the log
    }

    const std::string log = ini::readFile(save + "mxbmrp3\\mxbmrp3_log.txt");
    REQUIRE_FALSE(log.empty());
    const std::string low = lower(log);
    CHECK_MESSAGE(low.find(lower(profile)) == std::string::npos, "the user profile reached the log");
    // Wine's Documents is a folder of the profile, so the save path sits under
    // one placeholder or the other depending on the prefix; either is the scrub.
    CHECK(low.find("%userprofile%\\mxbmrp3-log-scrub\\mxbmrp3\\mxbmrp3_log.txt") != std::string::npos);
    CHECK(low.find("crashhandler installed, dumps -> %userprofile%\\mxbmrp3-log-scrub") != std::string::npos);
}
