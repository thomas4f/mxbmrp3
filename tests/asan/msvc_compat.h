// ============================================================================
// tests/asan/msvc_compat.h
// Minimal MSVC-ism shim so the plugin's PORTABLE headers (plugin_data.h and the
// POD structs it defines) compile with a native Linux g++/clang toolchain under
// AddressSanitizer. The shipping build is MSVC; this is a test-only compat layer,
// force-included via -include, and is NOT part of any shipped translation unit.
//
// Two things block a native include of plugin_data.h:
//   1. `__declspec(dllexport)` in the vendored game API header — neutralized on
//      the command line with -D'__declspec(x)=' (kept there, not here, so this
//      header stays a pure declarations file).
//   2. The Annex-K "safe" string functions (strncpy_s / _TRUNCATE), which glibc
//      does not provide — supplied below with MSVC-equivalent semantics
//      (bounded copy, ALWAYS null-terminates, _TRUNCATE = fill-what-fits).
//
// The semantics matter: the whole point of the ASan harness is to catch a real
// overflow, so this shim must not itself over-copy. strncpy_s never writes more
// than `dsz` bytes and always terminates — same guarantee the production code
// relies on from the MSVC CRT.
// ============================================================================
#pragma once
#include <cstddef>
#include <cstring>

#ifndef _TRUNCATE
#define _TRUNCATE ((size_t)-1)
#endif

// errno_t-style return: 0 on success, non-zero on a truncation/argument error.
// The production code ignores the return and relies on the guaranteed
// null-termination, which we preserve.
static inline int strncpy_s(char* dest, size_t destsz, const char* src, size_t count) {
    if (!dest || destsz == 0) return 22;            // EINVAL-ish
    if (!src) { dest[0] = '\0'; return 22; }
    const size_t maxCopy = destsz - 1;              // reserve the terminator
    const size_t n = (count == _TRUNCATE || count > maxCopy) ? maxCopy : count;
    size_t i = 0;
    for (; i < n && src[i] != '\0'; ++i) dest[i] = src[i];
    dest[i] = '\0';
    return 0;
}

static inline int strcpy_s(char* dest, size_t destsz, const char* src) {
    return strncpy_s(dest, destsz, src, _TRUNCATE);
}
