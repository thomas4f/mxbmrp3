// ============================================================================
// tests/unit/shim/logger_stub.cpp
// Link-time stand-in for Logger in the native unit build.
//
// WHY A STUB AND NOT THE REAL logger.cpp. The real one is genuinely
// Windows-bound — CreateDirectoryA to make the log folder, GetModuleFileNameA
// and GetModuleHandleW to name the host process. Those are CALLS, not just
// types, and tests/unit/shim/windows.h is deliberately types-only: the moment a
// shim starts implementing Win32 behaviour it becomes a second, unverified
// implementation of the thing under test. So the unit layer links this instead.
//
// WHAT THIS COSTS, stated plainly: nothing in the unit layer exercises log
// formatting or file output. That is fine here because no unit test asserts on
// log content — the headers under test (settings_serde.h and anything else that
// calls DEBUG_*) just need the symbols to resolve. Logger's own behaviour,
// including the mutex that serialises its writers, is covered where it can be:
// through the real DLL in the integration layer.
//
// Deliberately silent rather than printing: settings_serde.h emits a
// DEBUG_WARN_F on every unrecognised enum string, and test_settings_serde.cpp
// feeds it hundreds of deliberate junk inputs. Echoing those would bury the
// doctest output in expected warnings.
// ============================================================================
#include "diagnostics/logger.h"

Logger& Logger::getInstance() {
    static Logger instance;
    return instance;
}

void Logger::log(const char* /*level*/, const char* /*message*/) {
    // Intentionally does nothing — see the header comment above.
}

// ~Logger (inline in the header) calls this, so the stub must supply it too.
void Logger::shutdown() {}
