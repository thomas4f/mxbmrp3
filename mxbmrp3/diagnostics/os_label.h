// ============================================================================
// diagnostics/os_label.h
// Names the Windows release from a build number, for the log's OS line and the
// usage survey's osVersion (diagnostics/system_info.cpp collects the numbers).
//
// WHY THE BUILD IS A MAXIMUM. Compatibility mode ("Run this program in
// compatibility mode for: Windows 7" on mxbikes.exe) makes Windows lie to the
// game about its version: GetVersionEx, RtlGetVersion and even the registry's
// CurrentBuildNumber answer 7600 (Windows 7 RTM). A real 1.35.0 log from a
// Windows 10 19045 PC read "Windows 10 (7600)": the registry's
// CurrentMajorVersionNumber, which Windows 7 never had, came through honest.
// No single source is safe, but every lie points DOWN to an older Windows, so
// system_info reads several and keeps the highest. One of them is kernel32's
// own file version, which no compatibility layer rewrites; it carries the base
// build of a release family (19041 for 10 22H2's 19045), so it is the floor,
// not the answer, when an honest registry value is there.
//
// Pure (no Windows headers) so the unit suite compiles it as is. Pinned by
// tests/unit/test_os_label.cpp.
// ============================================================================
#pragma once

#include <cstdio>

namespace OsLabel {

// "Windows 11 (22631)" into `buf`; "" when there is no build at all.
// `major10` is the registry's CurrentMajorVersionNumber == 10, which only
// Windows 10 and 11 have. 11 shares major 10 and is told apart by build alone.
inline const char* windows(unsigned long build, bool major10, char* buf, size_t size) {
    if (!buf || size == 0) return "";
    buf[0] = '\0';
    if (build == 0) return buf;   // nothing read: no guess from the major alone
    const char* name = nullptr;
    if (build >= 22000)                    name = "Windows 11";
    else if (major10 || build >= 10240)    name = "Windows 10";
    else if (build >= 9600)                name = "Windows 8.1";
    else if (build >= 9200)                name = "Windows 8";
    else if (build >= 7600)                name = "Windows 7";
    else                                   name = "Windows";
    snprintf(buf, size, "%s (%lu)", name, build);
    return buf;
}

// The compatibility layers Windows applied to the game, from the process's
// __COMPAT_LAYER variable ("~ WIN7RTM DISABLEDXMAXIMIZEDWINDOWEDMODE"). The
// leading "~" (and the "!" / "#" flag characters some layers carry) is how the
// loader marks them, not a layer name; dropped, along with spacing. "" for none.
inline const char* layers(const char* raw, char* buf, size_t size) {
    if (!buf || size == 0) return "";
    size_t n = 0;
    bool needSpace = false;
    for (const char* p = raw ? raw : ""; *p; ++p) {
        const char c = *p;
        if (c == ' ' || c == '\t' || c == '~' || c == '!' || c == '#') {
            if (c == ' ' || c == '\t') needSpace = (n > 0);
            continue;
        }
        if (needSpace) {
            if (n + 1 >= size) break;
            buf[n++] = ' ';
            needSpace = false;
        }
        if (n + 1 >= size) break;
        buf[n++] = c;
    }
    buf[n] = '\0';
    return buf;
}

}  // namespace OsLabel
