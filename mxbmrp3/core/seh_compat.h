// ============================================================================
// core/seh_compat.h
// Portable Structured Exception Handling wrappers.
//
// SEH (__try/__except) is the only construct that can catch *hardware* faults
// such as access violations — things a C++ try/catch fundamentally cannot. The
// plugin uses it to survive over-reads of opaque game blobs (spectate cameras)
// and calls through raw function pointers into steam_api64.dll.
//
// SEH is MSVC-only. The headless cross-platform test build (mingw/GCC — see
// tests/integration/ and MXBMRP3_TEST_BUILD) has no SEH, so there the guarded block
// runs UNGUARDED. That is safe in that build specifically: it never faces the
// faulting conditions (no live game memory, no steam_api64.dll loaded). This is
// a testing-only relaxation, never a shipping configuration.
//
// Usage (identical shape on both compilers):
//     SEH_TRY {
//         ... risky, POD-only work ...
//     } SEH_EXCEPT_ALL {
//         ... fault handler (runs only on MSVC) ...
//     }
//
// As with native SEH, a guarded function must not hold C++ objects that require
// unwinding (MSVC C2712) — keep the body POD.
// ============================================================================
#pragma once

#ifdef _MSC_VER
    #include <excpt.h>   // EXCEPTION_EXECUTE_HANDLER
    #define SEH_TRY        __try
    #define SEH_EXCEPT_ALL __except (EXCEPTION_EXECUTE_HANDLER)
#else
    // No SEH: SEH_TRY is a plain block; the handler becomes dead code (if (0)).
    #define SEH_TRY
    #define SEH_EXCEPT_ALL if (0)
#endif
