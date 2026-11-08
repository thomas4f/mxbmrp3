// ============================================================================
// tests/unit/test_os_label.cpp
// The OS line's naming rules (diagnostics/os_label.h): which Windows a build
// is, and the compatibility layers cleaned for the log.
//
// The case that made this a test: a Windows 10 19045 PC with mxbikes.exe in
// "Windows 7" compatibility mode logged "Windows 10 (7600)" in 1.35.0's
// first log, because the registry's build number was lied to while its major
// version was not. system_info now keeps the HIGHEST build from several
// sources; the floor it can fall back to is kernel32's base build, pinned
// here as still naming Windows 10.
// ============================================================================
#include "doctest.h"

#include "../../mxbmrp3/diagnostics/os_label.h"

#include <cstring>
#include <string>

namespace {
std::string win(unsigned long build, bool major10) {
    char buf[64];
    return OsLabel::windows(build, major10, buf, sizeof(buf));
}
std::string layers(const char* raw) {
    char buf[128];
    return OsLabel::layers(raw, buf, sizeof(buf));
}
}  // namespace

TEST_CASE("os label: each release by build") {
    CHECK(win(26100, true) == "Windows 11 (26100)");
    CHECK(win(22000, true) == "Windows 11 (22000)");
    CHECK(win(19045, true) == "Windows 10 (19045)");
    CHECK(win(10240, false) == "Windows 10 (10240)");
    CHECK(win(9600, false) == "Windows 8.1 (9600)");
    CHECK(win(9200, false) == "Windows 8 (9200)");
    CHECK(win(7601, false) == "Windows 7 (7601)");
    CHECK(win(6002, false) == "Windows (6002)");
    CHECK(win(0, true) == "");
}

TEST_CASE("os label: compatibility mode's floor still names the real release") {
    // kernel32's file version on 10 22H2 is the 19041 base build; the lied-to
    // registry says 7600. The maximum is what system_info passes in.
    CHECK(win(19041, true) == "Windows 10 (19041)");
    // Windows 11 23H2 ships 22621-based binaries.
    CHECK(win(22621, true) == "Windows 11 (22621)");
}

TEST_CASE("os label: compatibility layers lose the loader's markers, keep their names") {
    CHECK(layers("~ WIN7RTM DISABLEDXMAXIMIZEDWINDOWEDMODE") == "WIN7RTM DISABLEDXMAXIMIZEDWINDOWEDMODE");
    CHECK(layers("~ HIGHDPIAWARE") == "HIGHDPIAWARE");
    CHECK(layers("RunAsInvoker") == "RunAsInvoker");
    CHECK(layers("  ~  WINXPSP3   256COLOR ") == "WINXPSP3 256COLOR");
    CHECK(layers("") == "");
    CHECK(layers(nullptr) == "");
    CHECK(layers("~") == "");
}

TEST_CASE("os label: a small buffer truncates instead of overflowing") {
    char buf[8];
    CHECK(std::strlen(OsLabel::layers("~ WIN7RTM DISABLEDXMAXIMIZEDWINDOWEDMODE", buf, sizeof(buf))) == 7);
}
