// ============================================================================
// tests/integration/tests/source_notice_test.cpp
// Someone who hands the bare .dlo to `strings` or an AI tool should find, first,
// that the plugin is open source and where its tagged source is. Pins, through
// the real DLL and the real mxbmrp3_log.txt:
//
//   1. The notice is in the binary as one plain-ASCII run (strings and
//      disassembler string views skip UTF-16 by default), with the tag URL.
//   2. The log repeats it. That logging call is also what keeps the string
//      referenced; without it /OPT:REF + /LTCG strip it. This mingw build links
//      differently, so release.yml's smoke-load step checks the MSVC .dlo too.
// ============================================================================
#include "doctest.h"
#include "integration_main.h"
#include "plugin_host.h"
#include "ini.h"

#include <windows.h>

#include <string>

namespace {
const char* const kNoticeStart =
    "MXBMRP3 is open source (MIT). Source for this build: "
    "https://github.com/thomas4f/mxbmrp3/tree/v";
}  // namespace

TEST_CASE("source notice: the .dlo carries it in ASCII and the log repeats it") {
    const std::string dll = ini::readFile(dllPath());
    REQUIRE_FALSE(dll.empty());
    const size_t at = dll.find(kNoticeStart);
    REQUIRE_MESSAGE(at != std::string::npos, "no ASCII source notice in the .dlo");
    const size_t end = dll.find('\0', at);
    REQUIRE(end != std::string::npos);
    const std::string notice = dll.substr(at, end - at);
    // The project's rules for changing or forking it, on the same tag.
    CHECK(notice.find("/CONTRIBUTING.md#building-on-mxbmrp3") != std::string::npos);

    const std::string save = "Z:\\tmp\\mxbmrp3-tests\\source-notice\\";
    CreateDirectoryA("Z:\\tmp\\mxbmrp3-tests", nullptr);
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
    CHECK_MESSAGE(log.find(notice) != std::string::npos, "the log does not repeat the notice");
}
