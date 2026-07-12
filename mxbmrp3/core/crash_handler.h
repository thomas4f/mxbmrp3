// ============================================================================
// core/crash_handler.h
// Top-level SEH filter that writes a minidump on unhandled hardware faults
// (access violation, stack overflow, divide-by-zero, etc.) before the host
// game dies. Does NOT prevent the crash — it just turns "game crashed
// sometime in qualifying" into "user can send a .dmp the dev can open in
// WinDbg / Visual Studio".
//
// The C++ exception barrier at the DLL boundary already catches throw-based
// failures; this complements it for the hardware-fault class of bugs that
// C++ try/catch cannot intercept.
//
// Alongside the dump, the filter writes a tiny savePath\\mxbmrp3\\pending_crash.json
// marker (faulting module+offset, exception code, plugin version + game build at
// crash time, and time) that AnalyticsManager reads on the NEXT launch to report the
// crash (opt-in analytics only). The versions are pinned at crash time because the
// report is sent on a later launch that may be a different build. This is written
// AFTER the dump with stack buffers + Win32 file I/O only - no network from the
// filter - so it never jeopardizes the load-bearing .dmp.
// ============================================================================
#pragma once

namespace CrashHandler {

// Install the process-wide unhandled exception filter. Idempotent —
// subsequent calls are no-ops. Dumps are written under
// savePath\\mxbmrp3\\crashes\\ (same umbrella as logs / settings / stats).
// If savePath is null or empty, the current working directory is used.
void install(const char* savePath);

// Restore the previous filter (if any). Safe to call even if install() was
// never called.
void uninstall();

}  // namespace CrashHandler
