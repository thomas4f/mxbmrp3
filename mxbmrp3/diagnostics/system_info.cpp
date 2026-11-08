// ============================================================================
// diagnostics/system_info.cpp
// See system_info.h. Moved here from the usage survey (osVersion) and the
// Benchmark report (CPU, memory) so the log's environment block reads the same
// values through the same code.
// ============================================================================
#include "system_info.h"

#include "os_label.h"

#include "../core/plugin_utils.h"   // trimInPlace

#include <windows.h>

#include <algorithm>
#include <cstdio>
#include <cstdlib>

namespace {

// RtlGetVersion's build, which compatibility mode can lower but never raise.
unsigned long rtlBuild() {
    HMODULE ntdll = GetModuleHandleA("ntdll.dll");
    if (!ntdll) return 0;
    using RtlGetVersionFn = LONG(WINAPI*)(OSVERSIONINFOW*);
    const auto fn = reinterpret_cast<RtlGetVersionFn>(
        reinterpret_cast<void*>(GetProcAddress(ntdll, "RtlGetVersion")));
    if (!fn) return 0;
    OSVERSIONINFOW v{};
    v.dwOSVersionInfoSize = sizeof(v);
    return fn(&v) == 0 ? v.dwBuildNumber : 0;
}

// The build in kernel32.dll's own version resource, read from the copy already
// loaded in this process (no file is opened, nothing new is loaded). A file
// version is not something a compatibility layer rewrites. It is the base build
// of the release family, so it is a floor under the registry's figure.
unsigned long kernel32Build() {
    HMODULE k32 = GetModuleHandleA("kernel32.dll");
    if (!k32) return 0;
    HRSRC res = FindResourceW(k32, MAKEINTRESOURCEW(VS_VERSION_INFO), MAKEINTRESOURCEW(16) /*RT_VERSION*/);
    if (!res) return 0;
    HGLOBAL h = LoadResource(k32, res);
    const DWORD size = SizeofResource(k32, res);
    const BYTE* data = h ? static_cast<const BYTE*>(LockResource(h)) : nullptr;
    if (!data || size < sizeof(VS_FIXEDFILEINFO)) return 0;
    // VS_FIXEDFILEINFO follows the "VS_VERSION_INFO" header, DWORD-aligned;
    // find it by its signature rather than re-deriving the header layout.
    for (DWORD off = 0; off + sizeof(VS_FIXEDFILEINFO) <= size; off += 4) {
        const auto* ffi = reinterpret_cast<const VS_FIXEDFILEINFO*>(data + off);
        if (ffi->dwSignature == 0xFEEF04BD) return HIWORD(ffi->dwFileVersionLS);
    }
    return 0;
}

}  // namespace

namespace SystemInfo {

// Wine/Proton first, because many PiBoSo players run on Linux via Steam Play.
// Then Windows, from the highest build any source reports: compatibility mode
// lowers some of them (see os_label.h).
std::string osVersion() {
    // --- Wine / Proton / Steam Deck? ntdll exports wine_get_version only under
    // Wine, which Proton (Steam Play) and the Steam Deck are built on. Refine the
    // label from Steam's environment when present, and fall back to plain "Wine"
    // if those hints are missing (graceful downgrade). Examples:
    //   "Steam Deck [Wine 8.0] (Linux)", "Proton [Wine 8.0] (Linux)", "Wine 9.0 (Linux)"
    if (HMODULE ntdll = GetModuleHandleA("ntdll.dll")) {
        typedef const char* (__cdecl *wine_get_version_t)(void);
        auto pWineVer = reinterpret_cast<wine_get_version_t>(
            reinterpret_cast<void*>(GetProcAddress(ntdll, "wine_get_version")));
        if (pWineVer) {
            const char* wv = pWineVer();   // e.g. "8.0"; guarded below in case it's null

            // Steam sets SteamDeck=1 on the Deck and STEAM_COMPAT_DATA_PATH for any
            // Proton launch; Proton passes these into the Windows env. Best-effort.
            const char* label = "Wine";
            bool refined = false;
            char envBuf[8] = {};
            if (GetEnvironmentVariableA("SteamDeck", envBuf, sizeof(envBuf)) > 0 && envBuf[0] == '1') {
                label = "Steam Deck"; refined = true;
            } else if (GetEnvironmentVariableA("STEAM_COMPAT_DATA_PATH", nullptr, 0) > 0) {
                label = "Proton"; refined = true;
            }

            std::string out;
            if (!refined) {
                out = (wv && *wv) ? std::string("Wine ") + wv : "Wine";
            } else {
                out = label;
                if (wv && *wv) { out += " [Wine "; out += wv; out += "]"; }
            }

            // wine_get_host_version reports the real host OS (e.g. "Linux", "Darwin").
            typedef void (__cdecl *wine_get_host_version_t)(const char**, const char**);
            auto pHost = reinterpret_cast<wine_get_host_version_t>(
                reinterpret_cast<void*>(GetProcAddress(ntdll, "wine_get_host_version")));
            if (pHost) {
                const char* sysname = nullptr;
                const char* release = nullptr;
                pHost(&sysname, &release);
                if (sysname && *sysname) { out += " ("; out += sysname; out += ")"; }
            }
            return out;
        }
    }

    // --- Native Windows. Every source below can be lied to by compatibility
    // mode except kernel32's file version, and every lie is an OLDER Windows,
    // so the highest build wins (diagnostics/os_label.h has the full story).
    unsigned long build = 0;
    bool major10 = false;
    HKEY hKey;
    if (RegOpenKeyExA(HKEY_LOCAL_MACHINE,
            "SOFTWARE\\Microsoft\\Windows NT\\CurrentVersion", 0,
            KEY_READ | KEY_WOW64_64KEY, &hKey) == ERROR_SUCCESS) {
        for (const char* name : { "CurrentBuildNumber", "CurrentBuild" }) {
            char buildStr[32] = {};
            DWORD sz = sizeof(buildStr) - 1, type = 0;
            if (RegQueryValueExA(hKey, name, nullptr, &type,
                                 reinterpret_cast<LPBYTE>(buildStr), &sz) == ERROR_SUCCESS &&
                type == REG_SZ) {
                build = (std::max)(build, strtoul(buildStr, nullptr, 10));
            }
        }
        DWORD major = 0, sz = sizeof(major), type = 0;
        if (RegQueryValueExA(hKey, "CurrentMajorVersionNumber", nullptr, &type,
                             reinterpret_cast<LPBYTE>(&major), &sz) == ERROR_SUCCESS &&
            type == REG_DWORD) {
            major10 = (major == 10);   // absent before Windows 10
        }
        RegCloseKey(hKey);
    }
    build = (std::max)(build, rtlBuild());
    build = (std::max)(build, kernel32Build());

    char label[64];
    return OsLabel::windows(build, major10, label, sizeof(label));
}

std::string compatibilityLayers() {
    char raw[512] = {};
    const DWORD n = GetEnvironmentVariableA("__COMPAT_LAYER", raw, sizeof(raw));
    if (n == 0 || n >= sizeof(raw)) return std::string();
    char clean[512];
    return OsLabel::layers(raw, clean, sizeof(clean));
}

const char* cpuName(char* buf, size_t size) {
    if (!buf || size == 0) return "";
    DWORD bytes = static_cast<DWORD>(size);
    if (RegGetValueA(HKEY_LOCAL_MACHINE, "HARDWARE\\DESCRIPTION\\System\\CentralProcessor\\0",
                     "ProcessorNameString", RRF_RT_REG_SZ, nullptr, buf, &bytes) != ERROR_SUCCESS) {
        snprintf(buf, size, "unknown");
        return buf;
    }
    // Trim the padding some vendors put around the name (AMD pads the end).
    return PluginUtils::trimInPlace(buf);
}

unsigned logicalCores() {
    return static_cast<unsigned>(GetActiveProcessorCount(ALL_PROCESSOR_GROUPS));
}

Memory physicalMemory() {
    Memory m;
    MEMORYSTATUSEX mem{};
    mem.dwLength = sizeof(mem);
    if (GlobalMemoryStatusEx(&mem)) {
        constexpr double GB = 1024.0 * 1024.0 * 1024.0;
        m.ok = true;
        m.totalGB = static_cast<double>(mem.ullTotalPhys) / GB;
        m.freeGB = static_cast<double>(mem.ullAvailPhys) / GB;
    }
    return m;
}

}  // namespace SystemInfo
