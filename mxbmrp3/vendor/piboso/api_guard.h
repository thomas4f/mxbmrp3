// ============================================================================
// vendor/piboso/api_guard.h
// Exception barrier for the PiBoSo DLL boundary. The host game does not
// support C++ exceptions across the API — any throw that escapes a DLL
// export terminates the host process. These macros wrap each export's body
// so an unexpected throw is logged and swallowed instead of crashing the
// game.
//
// Usage:
//     __declspec(dllexport) void SomeExport(void* _pData, int _iDataSize) {
//         try {
//             // ... body ...
//         } API_GUARD_CATCH("SomeExport")
//     }
//
// API_GUARD_CATCH expands to "catch (...) { ... } catch (...) { ... }" —
// it is NOT a try-block, so it must follow a try{...} that you write
// yourself. The name passed in appears in the log line if a throw is
// caught.
// ============================================================================
#pragma once

#include "../../diagnostics/logger.h"

// Catch variable is namespaced (_api_guard_ex) rather than a bare `_e` to
// avoid silent shadowing if a wrapped DLL export uses `_e` as a local name.
#define API_GUARD_CATCH(name) \
    catch (const std::exception& _api_guard_ex) { \
        DEBUG_WARN_F("PiBoSo API '%s' threw: %s", name, _api_guard_ex.what()); \
    } catch (...) { \
        DEBUG_WARN_F("PiBoSo API '%s' threw: unknown exception", name); \
    }
