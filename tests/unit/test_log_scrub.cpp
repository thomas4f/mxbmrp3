// ============================================================================
// tests/unit/test_log_scrub.cpp
// Unit tests for diagnostics/log_scrub.h — keeping the Windows user folder out
// of the player's log file.
//
// The bug this pins is a privacy one: mxbmrp3_log.txt is posted with bug
// reports, and through v1.31's pre-release every path it logged (the log file,
// crash dumps, settings, stats, PB traces, assets) read
// C:\Users\<account>\Documents\..., naming the player's Windows account. The
// paths still get logged, they are how a missing file is diagnosed, with the
// account folder replaced by a placeholder. The player's in-game name is not
// the account and is left alone.
//
// Header-only, no game engine. See tests/unit/README.md.
// ============================================================================
// The doctest implementation + main() live in test_plugin_utils.cpp
// (DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN); this TU only registers more tests.
#include "doctest.h"

#include "diagnostics/log_scrub.h"

#include <string>

using LogScrub::Prefix;

namespace {
struct Table {
    Prefix p[2];
    Table(const char* docs, const char* profile) {
        LogScrub::setPrefix(p[0], docs, "<Documents>");
        LogScrub::setPrefix(p[1], profile, "%USERPROFILE%");
    }
    std::string run(const char* in, size_t outSize = 1024) const {
        char out[1024];
        LogScrub::scrub(in, out, outSize, p, 2);
        return out;
    }
};
const Table kUsual("C:\\Users\\tom\\Documents", "C:\\Users\\tom");
}  // namespace

TEST_CASE("log scrub: Documents and the profile are replaced, the rest kept") {
    CHECK(kUsual.run("Log file: C:\\Users\\tom\\Documents\\PiBoSo\\MX Bikes\\mxbmrp3\\mxbmrp3_log.txt") ==
          "Log file: <Documents>\\PiBoSo\\MX Bikes\\mxbmrp3\\mxbmrp3_log.txt");
    CHECK(kUsual.run("Loaded C:\\Users\\tom\\AppData\\Local\\x.ini") ==
          "Loaded %USERPROFILE%\\AppData\\Local\\x.ini");
    CHECK(kUsual.run("C:\\Users\\tom") == "%USERPROFILE%");
    // Twice in one line, as a copy/move log does.
    CHECK(kUsual.run("a C:\\Users\\tom\\Documents\\x -> C:\\Users\\tom\\y") ==
          "a <Documents>\\x -> %USERPROFILE%\\y");
}

TEST_CASE("log scrub: case and separator style do not let a path through") {
    CHECK(kUsual.run("c:\\users\\TOM\\documents\\a") == "<Documents>\\a");
    CHECK(kUsual.run("C:/Users/tom/Documents/a") == "<Documents>/a");
}

TEST_CASE("log scrub: the player name and other folders are left alone") {
    // The in-game name can equal the account name; only the PATH is scrubbed.
    CHECK(kUsual.run("Player name: tom") == "Player name: tom");
    // Another account sharing the prefix is not this one.
    CHECK(kUsual.run("C:\\Users\\tomas\\x") == "C:\\Users\\tomas\\x");
    CHECK(kUsual.run("D:\\Games\\MX Bikes\\mxbikes.exe") == "D:\\Games\\MX Bikes\\mxbikes.exe");
}

TEST_CASE("log scrub: Documents outside the profile is still caught") {
    const Table moved("D:\\Docs\\tom", "C:\\Users\\tom\\");
    CHECK(moved.run("D:\\Docs\\tom\\PiBoSo") == "<Documents>\\PiBoSo");
    CHECK(moved.run("C:\\Users\\tom\\OneDrive") == "%USERPROFILE%\\OneDrive");
}

TEST_CASE("log scrub: an unresolved or implausible folder scrubs nothing") {
    Prefix p;
    CHECK_FALSE(LogScrub::setPrefix(p, nullptr, "x"));
    CHECK_FALSE(LogScrub::setPrefix(p, "", "x"));
    CHECK_FALSE(LogScrub::setPrefix(p, "C:\\", "x"));
    const Table none("", "");
    CHECK(none.run("C:\\Users\\tom\\Documents") == "C:\\Users\\tom\\Documents");
}

TEST_CASE("log scrub: the output is always terminated and never overrun") {
    CHECK(kUsual.run("C:\\Users\\tom\\Documents\\abc", 6) == "<Docu");
    CHECK(kUsual.run("abcdef", 4) == "abc");
    char out[1] = { 'x' };
    LogScrub::scrub("abc", out, 1, nullptr, 0);
    CHECK(out[0] == '\0');
}
