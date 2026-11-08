// ============================================================================
// tests/integration/tests/environment_report_test.cpp
// The log's environment block (diagnostics/environment_report.h), through the
// real plugin and the real mxbmrp3_log.txt:
//
//   1. Startup logs the PC: OS, CPU, RAM, monitors, and the compatibility
//      layers Windows applied to the game, cleaned of the loader's "~" marker
//      (set here the way Windows sets it for a game in compatibility mode).
//   2. The first Draw logs the drawing settings and the game's modules, ONCE:
//      later frames add nothing, so it costs nothing per frame.
//   3. Modules are named by file name and folder kind only. No module line
//      carries a path, and the plugin's own file is in the list.
//   4. Shutdown names a module that loaded after the first frame, flagged by
//      what it is (a copy loaded under an OBS capture hook's name).
//   5. With the game's GL context current (Xvfb, as gl_render_test), the first
//      frame names the GPU, the GL version and the viewport, read from it.
//
// WHY. A player's graphics report (ghost images pinned to the screen) could not
// be told apart from a shader injector or an overlay, because the log named
// neither the GPU nor anything else loaded in the game. The sorting rules are
// pinned in tests/unit/test_module_census.cpp; this is the wiring.
// ============================================================================
#include "doctest.h"
#include "integration_main.h"
#include "plugin_host.h"
#include "ini.h"

#include <windows.h>

#include <sstream>
#include <string>

namespace {
size_t count(const std::string& hay, const std::string& needle) {
    size_t n = 0;
    for (size_t at = hay.find(needle); at != std::string::npos; at = hay.find(needle, at + 1)) ++n;
    return n;
}
}  // namespace

TEST_CASE("environment report: the PC at startup, the game's modules once at the first frame") {
    char tmp[MAX_PATH] = { 0 };
    REQUIRE(GetTempPathA(MAX_PATH, tmp) > 0);
    const std::string save = std::string(tmp) + "mxbmrp3-envreport\\";
    CreateDirectoryA(save.c_str(), nullptr);
    DeleteFileA((save + "mxbmrp3\\mxbmrp3_log.txt").c_str());

    // A module to load between the first frame and Shutdown: any DLL will do,
    // copied under a name the census recognises.
    char sys[MAX_PATH] = { 0 };
    REQUIRE(GetSystemDirectoryA(sys, MAX_PATH) > 0);
    const std::string late = save + "graphics-hook64.dll";
    REQUIRE(CopyFileA((std::string(sys) + "\\version.dll").c_str(), late.c_str(), FALSE));

    SetEnvironmentVariableA("__COMPAT_LAYER", "~ WIN7RTM DISABLEDXMAXIMIZEDWINDOWEDMODE");
    HMODULE lateMod = nullptr;
    {
        PluginHost host(dllPath());
        REQUIRE(host.loaded());
        host.startup(save.c_str());
        host.draw();
        host.draw();
        host.draw();
        lateMod = LoadLibraryA(late.c_str());
        host.shutdown();   // flushes the log
    }
    if (lateMod) FreeLibrary(lateMod);
    SetEnvironmentVariableA("__COMPAT_LAYER", nullptr);

    const std::string log = ini::readFile(save + "mxbmrp3\\mxbmrp3_log.txt");
    REQUIRE_FALSE(log.empty());

    // 1. The PC.
    CHECK(log.find("OS: ") != std::string::npos);
    CHECK(log.find("CPU: ") != std::string::npos);
    CHECK(log.find("RAM: ") != std::string::npos);
    CHECK(log.find("Monitors: ") != std::string::npos);
    CHECK(log.find("Compatibility mode: WIN7RTM DISABLEDXMAXIMIZEDWINDOWEDMODE") != std::string::npos);

    // 2. Once, however many frames.
    CHECK(count(log, "Environment at the first frame:") == 1);
    CHECK(count(log, "Drawing: Direct GL off, plugin thread off, display In-game") == 1);
    CHECK(count(log, "Modules, Windows: ") == 1);
    CHECK(log.find("Modules, Windows: 0 ") == std::string::npos);
    CHECK(count(log, "Overlays and injectors: ") == 1);
    CHECK(count(log, "GPU: ") == 1);

    // 3. File names only, our own among them.
    bool sawSelf = false;
    std::istringstream lines(log);
    for (std::string line; std::getline(lines, line);) {
        if (line.find("Modules, ") == std::string::npos &&
            line.find("Modules loaded after") == std::string::npos &&
            line.find("Overlays and injectors") == std::string::npos &&
            line.find("Other plugins") == std::string::npos) {
            continue;
        }
        INFO(line);
        CHECK(line.find('\\') == std::string::npos);
        CHECK(line.find(":/") == std::string::npos);
        if (line.find("mxbmrp3_test.dlo") != std::string::npos) sawSelf = true;
    }
    CHECK(sawSelf);

    // 4. The late arrival, named for what it is.
    REQUIRE(lateMod != nullptr);
    CHECK(log.find("Modules loaded after the first frame: graphics-hook64.dll (") != std::string::npos);
    CHECK(log.find("OBS game capture)") != std::string::npos);

    DeleteFileA(late.c_str());
}

TEST_CASE("environment report: the GPU is read from the game's GL context at the first frame") {
    const std::string save = "Z:\\tmp\\mxbmrp3-tests\\envreport_gl\\";
    CreateDirectoryA("Z:\\tmp\\mxbmrp3-tests", nullptr);
    CreateDirectoryA(save.c_str(), nullptr);
    DeleteFileA((save + "mxbmrp3\\mxbmrp3_log.txt").c_str());
    {
        PluginHost host(dllPath());
        REQUIRE(host.loaded());
        host.startup(save.c_str());
        if (!host.glMakeContext(true)) {
            MESSAGE("no GL context in this environment - the GPU lines cannot be exercised");
            return;
        }
        host.draw();
        host.shutdown();
    }
    const std::string log = ini::readFile(save + "mxbmrp3\\mxbmrp3_log.txt");
    REQUIRE_FALSE(log.empty());
    CHECK(count(log, "GPU: ") == 1);
    CHECK(log.find("GPU: opengl32.dll is not loaded") == std::string::npos);
    CHECK(log.find("GPU: no OpenGL context") == std::string::npos);
    CHECK(count(log, "OpenGL: ") == 1);
    CHECK(log.find("Compatibility mode: none") != std::string::npos);
    CHECK(log.find("Game viewport: ") != std::string::npos);
    CHECK(log.find("Game viewport: 0x0") == std::string::npos);
}
