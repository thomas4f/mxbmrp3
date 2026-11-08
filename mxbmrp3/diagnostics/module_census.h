// ============================================================================
// diagnostics/module_census.h
// Sorts the DLLs loaded in the game process for the log's environment block
// (diagnostics/environment_report.cpp), so a graphics report can be read from
// the log alone: a ReShade-style opengl32.dll beside the game's .exe, an
// overlay hooked into the game's frame, another .dlo plugin drawing too.
//
// Privacy, which is why this file exists rather than a list of full paths:
// mxbmrp3_log.txt is posted with bug reports and stays anonymous. A module is
// logged as its FILE NAME and the KIND of folder it came from (game folder,
// Program Files, user folder, ...), never its path, which can name the Windows
// account or a private folder layout. Windows' own DLLs are only counted.
//
// Pure (no Windows headers) so the unit suite compiles it as is; the Windows
// side collects the paths and folders. Pinned by tests/unit/test_module_census.cpp
// and, through the real log, tests/integration/tests/environment_report_test.cpp.
// ============================================================================
#pragma once

#include <cstddef>
#include <cstdint>
#include <cstring>

namespace ModuleCensus {

// The kind of folder a module was loaded from. Plugins is checked before Game
// because the plugins folder sits inside the game folder.
enum class Where : uint8_t { Plugins, Game, Windows, Programs, User, Other, COUNT };

inline const char* whereLabel(Where w) {
    switch (w) {
        case Where::Plugins:  return "plugins folder";
        case Where::Game:     return "game folder";
        case Where::Windows:  return "Windows";
        case Where::Programs: return "Program Files";
        case Where::User:     return "user folder";
        default:              return "elsewhere";
    }
}

// The folders a path is sorted against, as UTF-8. Any may be null or empty
// (not resolved on this PC), which matches nothing.
struct Roots {
    const char* plugins = nullptr;
    const char* game = nullptr;
    const char* windows = nullptr;
    const char* programs[3] = { nullptr, nullptr, nullptr };
    const char* user = nullptr;
};

inline char foldChar(char c) {
    if (c == '/') return '\\';
    if (c >= 'A' && c <= 'Z') return static_cast<char>(c - 'A' + 'a');
    return c;
}

// Windows paths compare case-insensitively with either separator, and the
// folder must end on a boundary: C:\Games\MX Bikes is not under C:\Games\MX.
inline bool under(const char* path, const char* folder) {
    if (!path || !folder) return false;
    size_t n = strlen(folder);
    while (n > 0 && (folder[n - 1] == '\\' || folder[n - 1] == '/')) --n;
    if (n < 3) return false;   // "C:" or empty would claim the whole drive
    for (size_t i = 0; i < n; ++i) {
        if (path[i] == '\0' || foldChar(path[i]) != foldChar(folder[i])) return false;
    }
    return path[n] == '\\' || path[n] == '/';
}

inline Where classify(const char* path, const Roots& r) {
    if (under(path, r.plugins)) return Where::Plugins;
    if (under(path, r.game)) return Where::Game;
    if (under(path, r.windows)) return Where::Windows;
    for (const char* p : r.programs) {
        if (under(path, p)) return Where::Programs;
    }
    if (under(path, r.user)) return Where::User;
    return Where::Other;
}

// The part after the last separator: the only part of a path that is logged.
inline const char* fileName(const char* path) {
    if (!path) return "";
    const char* name = path;
    for (const char* p = path; *p; ++p) {
        if (*p == '\\' || *p == '/') name = p + 1;
    }
    return name;
}

inline bool startsWithI(const char* s, const char* prefix) {
    for (; *prefix; ++s, ++prefix) {
        if (*s == '\0' || foldChar(*s) != foldChar(*prefix)) return false;
    }
    return true;
}

inline bool endsWithI(const char* s, const char* suffix) {
    const size_t n = strlen(s), m = strlen(suffix);
    return n >= m && startsWithI(s + n - m, suffix);
}

// What a module tells us about the game's frame, or nullptr when it is not one
// we recognise. Matched by FILE NAME, which is all these tools have in common
// across versions and install folders.
//
// The proxy case is the one this exists for: ReShade and the injectors built
// like it (GameFX, ENB) are installed as a copy of a Windows graphics DLL
// beside the game's .exe, which Windows loads in place of its own. The same
// name loaded from Windows is the normal case and is not flagged.
inline const char* knownTool(const char* name, Where where) {
    if (!name || !*name) return nullptr;
    struct Tool { const char* prefix; const char* label; };
    static const Tool TOOLS[] = {
        { "reshade",             "ReShade" },
        { "rtsshooks",           "RivaTuner overlay" },
        { "gameoverlayrenderer", "Steam overlay" },
        { "discordhook",         "Discord overlay" },
        { "nvspcap",             "NVIDIA overlay" },
        { "graphics-hook",       "OBS game capture" },
        { "specialk",            "Special K" },
        { "fraps",               "Fraps" },
    };
    for (const Tool& t : TOOLS) {
        if (startsWithI(name, t.prefix)) return t.label;
    }
    if (where == Where::Game || where == Where::Plugins) {
        static const char* const PROXIES[] = {
            "opengl32.dll", "dxgi.dll", "d3d9.dll", "d3d11.dll", "d3d12.dll",
            "dinput8.dll", "version.dll", "winmm.dll", "dsound.dll",
        };
        for (const char* p : PROXIES) {
            if (startsWithI(name, p) && name[strlen(p)] == '\0') return "stands in for a Windows DLL";
        }
    }
    if (endsWithI(name, ".dlo")) return "game plugin";
    if (endsWithI(name, ".asi")) return "ASI mod";
    return nullptr;
}

}  // namespace ModuleCensus
