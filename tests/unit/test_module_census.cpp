// ============================================================================
// tests/unit/test_module_census.cpp
// How the log's environment block sorts the game's modules
// (diagnostics/module_census.h): which folder kind a path belongs to, that only
// the file name is ever taken from it, and which names are flagged as an
// overlay, an injector or another plugin. The wiring through the real log is
// tests/integration/tests/environment_report_test.cpp.
// ============================================================================
#include "doctest.h"

#include "../../mxbmrp3/diagnostics/module_census.h"

#include <cstring>

using namespace ModuleCensus;

namespace {
Roots pcRoots() {
    Roots r;
    r.plugins = "C:\\Games\\MX Bikes\\plugins";
    r.game = "C:\\Games\\MX Bikes\\";
    r.windows = "C:\\WINDOWS";
    r.programs[0] = "C:\\Program Files";
    r.programs[1] = "C:\\Program Files (x86)";
    r.programs[2] = nullptr;   // not set on this PC
    r.user = "C:\\Users\\tom";
    return r;
}
}  // namespace

TEST_CASE("module census: a folder matches on a boundary, in any case, with either separator") {
    CHECK(under("C:\\Games\\MX Bikes\\mxbikes.exe", "C:\\Games\\MX Bikes"));
    CHECK(under("c:/games/mx bikes/mxbikes.exe", "C:\\Games\\MX Bikes\\"));
    CHECK_FALSE(under("C:\\Games\\MX Bikes 2\\mxbikes.exe", "C:\\Games\\MX Bikes"));
    CHECK_FALSE(under("C:\\Games\\MX Bikes", "C:\\Games\\MX Bikes"));   // the folder itself is no module
    CHECK_FALSE(under("C:\\Games\\x.dll", nullptr));
    CHECK_FALSE(under("C:\\Games\\x.dll", ""));
    CHECK_FALSE(under("C:\\Games\\x.dll", "C:\\"));   // a bare drive would claim everything
}

TEST_CASE("module census: each folder kind, plugins before the game folder that holds it") {
    const Roots r = pcRoots();
    CHECK(classify("C:\\Games\\MX Bikes\\plugins\\mxbmrp3.dlo", r) == Where::Plugins);
    CHECK(classify("C:\\Games\\MX Bikes\\opengl32.dll", r) == Where::Game);
    CHECK(classify("C:\\Windows\\System32\\opengl32.dll", r) == Where::Windows);
    CHECK(classify("C:\\Windows\\SysWOW64\\kernel32.dll", r) == Where::Windows);
    CHECK(classify("C:\\Program Files (x86)\\Steam\\gameoverlayrenderer64.dll", r) == Where::Programs);
    CHECK(classify("C:\\Program Files\\RivaTuner\\RTSSHooks64.dll", r) == Where::Programs);
    CHECK(classify("C:\\Users\\tom\\AppData\\Local\\Discord\\DiscordHook64.dll", r) == Where::User);
    CHECK(classify("D:\\Tools\\thing.dll", r) == Where::Other);
    CHECK(classify("C:\\Users\\tomas\\x.dll", r) == Where::Other);   // another account, not a prefix match
}

TEST_CASE("module census: only the file name leaves the path") {
    CHECK(strcmp(fileName("C:\\Users\\tom\\AppData\\Local\\Discord\\DiscordHook64.dll"), "DiscordHook64.dll") == 0);
    CHECK(strcmp(fileName("C:/Games/MX Bikes/opengl32.dll"), "opengl32.dll") == 0);
    CHECK(strcmp(fileName("bare.dll"), "bare.dll") == 0);
    CHECK(strcmp(fileName(nullptr), "") == 0);
}

TEST_CASE("module census: a Windows DLL name beside the game is a stand-in, the same name in Windows is not") {
    CHECK(knownTool("opengl32.dll", Where::Game) != nullptr);
    CHECK(knownTool("OPENGL32.DLL", Where::Game) != nullptr);
    CHECK(knownTool("dxgi.dll", Where::Plugins) != nullptr);
    CHECK(knownTool("opengl32.dll", Where::Windows) == nullptr);
    CHECK(knownTool("opengl32.dll", Where::Programs) == nullptr);
    CHECK(knownTool("opengl32.dll.bak", Where::Game) == nullptr);   // the whole name, not a prefix
}

TEST_CASE("module census: overlays are named wherever they load from") {
    CHECK(strcmp(knownTool("ReShade64.dll", Where::Other), "ReShade") == 0);
    CHECK(strcmp(knownTool("RTSSHooks64.dll", Where::Programs), "RivaTuner overlay") == 0);
    CHECK(strcmp(knownTool("gameoverlayrenderer64.dll", Where::Programs), "Steam overlay") == 0);
    CHECK(strcmp(knownTool("DiscordHook64.dll", Where::User), "Discord overlay") == 0);
    CHECK(strcmp(knownTool("nvspcap64.dll", Where::Windows), "NVIDIA overlay") == 0);
    CHECK(strcmp(knownTool("graphics-hook64.dll", Where::Programs), "OBS game capture") == 0);
}

TEST_CASE("module census: other plugins and ASI mods are told apart; ordinary DLLs are not flagged") {
    CHECK(strcmp(knownTool("someone_else.dlo", Where::Plugins), "game plugin") == 0);
    CHECK(strcmp(knownTool("widescreen.asi", Where::Game), "ASI mod") == 0);
    CHECK(knownTool("steam_api64.dll", Where::Game) == nullptr);
    CHECK(knownTool("fmod64.dll", Where::Game) == nullptr);
    CHECK(knownTool("", Where::Game) == nullptr);
    CHECK(knownTool(nullptr, Where::Game) == nullptr);
}
