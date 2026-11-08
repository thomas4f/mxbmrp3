// ============================================================================
// diagnostics/environment_report.cpp
// See environment_report.h for what is logged and when, module_census.h for
// how modules are sorted and why only file names reach the log.
// ============================================================================
#include "environment_report.h"

#include "logger.h"
#include "module_census.h"
#include "system_info.h"
#include "../core/companion_window.h"
#include "../core/plugin_thread.h"
#include "../core/ui_config.h"

#include <windows.h>
#include <psapi.h>   // K32EnumProcessModules

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <exception>
#include <string>
#include <vector>

namespace {

using ModuleCensus::Where;

// A log line holds 1024 bytes; module lists wrap well before that.
constexpr size_t LINE_WRAP = 900;

struct Module {
    std::string name;   // file name only, UTF-8
    Where where;
    bool ours;          // this plugin's own .dlo
};

// Non-Windows module names seen at the first frame, for logLateModules().
// Game thread only (first Draw and Shutdown).
std::vector<std::string> s_firstFrameNames;
bool s_firstFrameLogged = false;

std::string toUtf8(const wchar_t* w) {
    if (!w || !*w) return std::string();
    const int n = WideCharToMultiByte(CP_UTF8, 0, w, -1, nullptr, 0, nullptr, nullptr);
    if (n <= 1) return std::string();
    std::string out(static_cast<size_t>(n - 1), '\0');
    WideCharToMultiByte(CP_UTF8, 0, w, -1, &out[0], n, nullptr, nullptr);
    return out;
}

std::string envFolder(const wchar_t* var) {
    wchar_t buf[MAX_PATH] = {};
    const DWORD n = GetEnvironmentVariableW(var, buf, MAX_PATH);
    return (n > 0 && n < MAX_PATH) ? toUtf8(buf) : std::string();
}

std::string modulePath(HMODULE mod) {
    wchar_t buf[1024] = {};
    const DWORD n = GetModuleFileNameW(mod, buf, static_cast<DWORD>(sizeof(buf) / sizeof(buf[0])));
    return (n > 0 && n < sizeof(buf) / sizeof(buf[0])) ? toUtf8(buf) : std::string();
}

// The process's own module list, sorted by folder kind then name. Only the
// game process: other programs on the PC are never looked at.
std::vector<Module> collectModules() {
    std::vector<Module> out;

    std::string game = modulePath(nullptr);
    game.resize(game.size() - strlen(ModuleCensus::fileName(game.c_str())));
    const std::string plugins = game + "plugins";
    wchar_t winBuf[MAX_PATH] = {};
    const UINT winLen = GetWindowsDirectoryW(winBuf, MAX_PATH);
    const std::string windows = (winLen > 0 && winLen < MAX_PATH) ? toUtf8(winBuf) : std::string();
    const std::string pf = envFolder(L"ProgramFiles");
    const std::string pf86 = envFolder(L"ProgramFiles(x86)");
    const std::string pf64 = envFolder(L"ProgramW6432");
    const std::string user = envFolder(L"USERPROFILE");

    ModuleCensus::Roots roots;
    roots.plugins = plugins.c_str();
    roots.game = game.c_str();
    roots.windows = windows.c_str();
    roots.programs[0] = pf.c_str();
    roots.programs[1] = pf86.c_str();
    roots.programs[2] = pf64.c_str();
    roots.user = user.c_str();

    HMODULE self = nullptr;
    GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                       reinterpret_cast<LPCWSTR>(&collectModules), &self);

    // Modules can load between the two calls, so size from the first answer
    // with headroom and take whatever fits.
    const HANDLE proc = GetCurrentProcess();
    DWORD needed = 0;
    if (!K32EnumProcessModules(proc, nullptr, 0, &needed) || needed == 0) return out;
    std::vector<HMODULE> mods(needed / sizeof(HMODULE) + 32);
    if (!K32EnumProcessModules(proc, mods.data(), static_cast<DWORD>(mods.size() * sizeof(HMODULE)), &needed)) {
        return out;
    }
    const size_t count = std::min(mods.size(), static_cast<size_t>(needed / sizeof(HMODULE)));

    out.reserve(count);
    for (size_t i = 0; i < count; ++i) {
        const std::string path = modulePath(mods[i]);
        if (path.empty()) continue;
        out.push_back({ ModuleCensus::fileName(path.c_str()),
                        ModuleCensus::classify(path.c_str(), roots),
                        mods[i] == self });
    }
    std::sort(out.begin(), out.end(), [](const Module& a, const Module& b) {
        if (a.where != b.where) return a.where < b.where;
        return _stricmp(a.name.c_str(), b.name.c_str()) < 0;
    });
    return out;
}

// "<header>: a, b, c", wrapped onto "<header> (cont.): ..." lines.
void logList(const char* header, const std::vector<std::string>& items) {
    if (items.empty()) return;
    std::string line = header;
    line += ": ";
    bool first = true;
    for (const std::string& item : items) {
        if (!first && line.size() + item.size() + 2 > LINE_WRAP) {
            DEBUG_INFO_F("%s", line.c_str());
            line = header;
            line += " (cont.): ";
            first = true;
        }
        if (!first) line += ", ";
        line += item;
        first = false;
    }
    DEBUG_INFO_F("%s", line.c_str());
}

void logModules(const std::vector<Module>& mods) {
    size_t windowsCount = 0;
    std::vector<std::string> tools;
    std::vector<std::string> plugins;
    std::vector<std::string> group;
    for (size_t i = 0; i < mods.size(); ++i) {
        const Module& m = mods[i];
        if (m.where == Where::Windows) {
            ++windowsCount;
        } else {
            group.push_back(m.name);
            if (const char* tool = ModuleCensus::knownTool(m.name.c_str(), m.where)) {
                const bool isPlugin = strcmp(tool, "game plugin") == 0;
                if (isPlugin && !m.ours) {
                    plugins.push_back(m.name);
                } else if (!isPlugin) {
                    tools.push_back(m.name + " (" + tool + ", " + ModuleCensus::whereLabel(m.where) + ")");
                }
            }
        }
        const bool groupEnds = (i + 1 == mods.size()) || mods[i + 1].where != m.where;
        if (groupEnds && !group.empty()) {
            char header[64];
            snprintf(header, sizeof(header), "Modules, %s", ModuleCensus::whereLabel(m.where));
            logList(header, group);
            group.clear();
        }
    }
    DEBUG_INFO_F("Modules, Windows: %zu (not listed)", windowsCount);
    if (tools.empty()) {
        DEBUG_INFO("Overlays and injectors: none recognised");
    } else {
        logList("Overlays and injectors", tools);
    }
    if (plugins.empty()) {
        DEBUG_INFO("Other plugins: none");
    } else {
        logList("Other plugins", plugins);
    }
}

// The GPU the game actually renders on (on a laptop, which of its two), read
// from the game's own context. glGetString and glGetIntegerv only read.
void logGraphics() {
    HMODULE gl = GetModuleHandleA("opengl32.dll");
    if (!gl) {
        DEBUG_INFO("GPU: opengl32.dll is not loaded");
        return;
    }
    using GetCurrentContextFn = HGLRC(WINAPI*)();
    using GetStringFn = const unsigned char*(WINAPI*)(unsigned);
    using GetIntegervFn = void(WINAPI*)(unsigned, int*);
    const auto getContext = reinterpret_cast<GetCurrentContextFn>(
        reinterpret_cast<void*>(GetProcAddress(gl, "wglGetCurrentContext")));
    const auto getString = reinterpret_cast<GetStringFn>(
        reinterpret_cast<void*>(GetProcAddress(gl, "glGetString")));
    const auto getIntegerv = reinterpret_cast<GetIntegervFn>(
        reinterpret_cast<void*>(GetProcAddress(gl, "glGetIntegerv")));
    if (!getContext || !getString || !getContext()) {
        DEBUG_INFO("GPU: no OpenGL context at the first frame");
        return;
    }
    auto str = [&](unsigned name) {
        const unsigned char* p = getString(name);
        return p ? reinterpret_cast<const char*>(p) : "(none)";
    };
    DEBUG_INFO_F("GPU: %s (%s)", str(0x1F01 /*GL_RENDERER*/), str(0x1F00 /*GL_VENDOR*/));
    DEBUG_INFO_F("OpenGL: %s", str(0x1F02 /*GL_VERSION*/));
    if (getIntegerv) {
        int vp[4] = { 0, 0, 0, 0 };
        getIntegerv(0x0BA2 /*GL_VIEWPORT*/, vp);
        DEBUG_INFO_F("Game viewport: %dx%d", vp[2], vp[3]);
    }
}

void logDrawSettings() {
    const UiConfig& ui = UiConfig::getInstance();
    const char* target = "In-game";
    if (ui.getDisplayTarget() == DisplayTarget::COMPANION) target = "Companion";
    else if (ui.getDisplayTarget() == DisplayTarget::BOTH) target = "Both";
    const std::string& theme = ui.getThemeName();
    DEBUG_INFO_F("Drawing: Direct GL %s, plugin thread %s, display %s, companion GPU %s, theme %s",
                 ui.getGlInGame() ? "on" : "off",
                 PluginThread::getInstance().enabled() ? "on" : "off",
                 target,
                 CompanionWindow::getInstance().getHwAccel() ? "on" : "off",
                 theme.empty() ? "none" : theme.c_str());
}

}  // namespace

namespace EnvironmentReport {

void logStartup() {
    try {
        const std::string os = SystemInfo::osVersion();
        DEBUG_INFO_F("OS: %s", os.empty() ? "unknown" : os.c_str());
        // Set on mxbikes.exe's Compatibility tab (or by Windows): changes what
        // the game is told about Windows and how its window is presented.
        const std::string layers = SystemInfo::compatibilityLayers();
        DEBUG_INFO_F("Compatibility mode: %s", layers.empty() ? "none" : layers.c_str());
        char cpu[128];
        DEBUG_INFO_F("CPU: %s, %u logical cores", SystemInfo::cpuName(cpu, sizeof(cpu)),
                     SystemInfo::logicalCores());
        const SystemInfo::Memory mem = SystemInfo::physicalMemory();
        if (mem.ok) DEBUG_INFO_F("RAM: %.1f GB, %.1f GB free", mem.totalGB, mem.freeGB);

        DEVMODEW dm{};
        dm.dmSize = sizeof(dm);
        if (EnumDisplaySettingsW(nullptr, ENUM_CURRENT_SETTINGS, &dm)) {
            // 0 and 1 mean "the hardware's default" rather than a rate.
            char hz[24] = "";
            if (dm.dmDisplayFrequency > 1) {
                snprintf(hz, sizeof(hz), " at %lu Hz", static_cast<unsigned long>(dm.dmDisplayFrequency));
            }
            DEBUG_INFO_F("Monitors: %d, main one %lux%lu%s", GetSystemMetrics(SM_CMONITORS),
                         static_cast<unsigned long>(dm.dmPelsWidth), static_cast<unsigned long>(dm.dmPelsHeight), hz);
        } else {
            DEBUG_INFO_F("Monitors: %d", GetSystemMetrics(SM_CMONITORS));
        }
    } catch (const std::exception& e) {
        DEBUG_WARN_F("Environment report (startup) failed: %s", e.what());
    }
}

void logFirstFrame() {
    if (s_firstFrameLogged) return;
    s_firstFrameLogged = true;
    try {
        DEBUG_INFO("Environment at the first frame:");
        logGraphics();
        logDrawSettings();
        const std::vector<Module> mods = collectModules();
        logModules(mods);
        s_firstFrameNames.clear();
        for (const Module& m : mods) {
            if (m.where != Where::Windows) s_firstFrameNames.push_back(m.name);
        }
    } catch (const std::exception& e) {
        DEBUG_WARN_F("Environment report (first frame) failed: %s", e.what());
    }
}

void logLateModules() {
    if (!s_firstFrameLogged) return;
    try {
        std::vector<std::string> late;
        for (const Module& m : collectModules()) {
            if (m.where == Where::Windows) continue;
            if (std::find(s_firstFrameNames.begin(), s_firstFrameNames.end(), m.name) != s_firstFrameNames.end()) {
                continue;
            }
            std::string item = m.name + " (" + ModuleCensus::whereLabel(m.where);
            if (const char* tool = ModuleCensus::knownTool(m.name.c_str(), m.where)) {
                item += ", ";
                item += tool;
            }
            item += ")";
            late.push_back(item);
        }
        if (late.empty()) {
            DEBUG_INFO("Modules loaded after the first frame: none");
        } else {
            logList("Modules loaded after the first frame", late);
        }
    } catch (const std::exception& e) {
        DEBUG_WARN_F("Environment report (late modules) failed: %s", e.what());
    }
}

}  // namespace EnvironmentReport
