// ============================================================================
// core/crash_handler.cpp
// SEH filter implementation. See header for rationale.
//
// All work inside the filter uses only stack memory and Win32 calls — the
// heap may be corrupt at the point a hardware fault fires, so we avoid
// std::string, new, and other allocating C++ machinery.
// ============================================================================
#include "crash_handler.h"
#include "../diagnostics/logger.h"

#include <windows.h>
#include <dbghelp.h>
#include <cstdio>
#include <cstring>

// dbghelp.dll ships with Windows — link implicitly so it's loaded at
// process start (when the heap is healthy) rather than lazily inside the
// crash filter (when it may not be).
#pragma comment(lib, "dbghelp.lib")

namespace {

// All static state. Set once during install() (on the game thread), then
// only read from inside the filter — no synchronization needed.
char s_dumpDir[MAX_PATH] = {};
LPTOP_LEVEL_EXCEPTION_FILTER s_previousFilter = nullptr;
bool s_installed = false;

// Re-entry guard: if MiniDumpWriteDump itself faults we don't want infinite
// recursion. InterlockedExchange gives us an atomic test-and-set.
volatile LONG s_dumping = 0;

LONG WINAPI crashFilter(EXCEPTION_POINTERS* info) {
    // Prevent re-entry. If we're already mid-dump (e.g., MiniDumpWriteDump
    // faulted), bail to the next filter.
    if (InterlockedExchange(&s_dumping, 1) != 0) {
        if (s_previousFilter) return s_previousFilter(info);
        return EXCEPTION_CONTINUE_SEARCH;
    }

    // Build the dump path on the stack. Format:
    //   <savePath>\mxbmrp3\crashes\mxbmrp3_crash_YYYYMMDD_HHMMSS_<pid>.dmp
    // (The mxbmrp3 subfolder is the shared umbrella the logger / settings /
    // stats all live under.)
    SYSTEMTIME t;
    GetLocalTime(&t);
    const char* root = s_dumpDir[0] ? s_dumpDir : ".";

    // CreateDirectoryA doesn't create parents. The logger normally creates
    // <root>\mxbmrp3 at startup, but if that init failed we still want a
    // best-effort attempt, so create both levels.
    char path[MAX_PATH];
    int written = snprintf(path, sizeof(path), "%s\\mxbmrp3", root);
    if (written < 0 || written >= static_cast<int>(sizeof(path))) {
        // Path overflow — fall through to chain without writing a dump.
        if (s_previousFilter) return s_previousFilter(info);
        return EXCEPTION_CONTINUE_SEARCH;
    }
    CreateDirectoryA(path, nullptr);  // ERROR_ALREADY_EXISTS is fine

    written = snprintf(path, sizeof(path), "%s\\mxbmrp3\\crashes", root);
    if (written < 0 || written >= static_cast<int>(sizeof(path))) {
        if (s_previousFilter) return s_previousFilter(info);
        return EXCEPTION_CONTINUE_SEARCH;
    }
    CreateDirectoryA(path, nullptr);

    written = snprintf(path, sizeof(path),
        "%s\\mxbmrp3\\crashes\\mxbmrp3_crash_%04d%02d%02d_%02d%02d%02d_%lu.dmp",
        root,
        t.wYear, t.wMonth, t.wDay,
        t.wHour, t.wMinute, t.wSecond,
        GetCurrentProcessId());
    if (written < 0 || written >= static_cast<int>(sizeof(path))) {
        if (s_previousFilter) return s_previousFilter(info);
        return EXCEPTION_CONTINUE_SEARCH;
    }

    HANDLE hFile = CreateFileA(path, GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS,
                                FILE_ATTRIBUTE_NORMAL, nullptr);
    if (hFile != INVALID_HANDLE_VALUE) {
        MINIDUMP_EXCEPTION_INFORMATION exInfo = {};
        exInfo.ThreadId = GetCurrentThreadId();
        exInfo.ExceptionPointers = info;
        exInfo.ClientPointers = FALSE;

        // MiniDumpWithThreadInfo gives stack traces for all threads, which
        // is what you want for "where was the SSE thread when this hit?"
        // MiniDumpWithIndirectlyReferencedMemory grabs heap pages that
        // local variables point into, which helps reconstruct state.
        // MiniDumpWithProcessThreadData captures TEB/PEB — cheap and
        // useful for thread-context inspection. Skip MiniDumpWithFullMemory
        // (multi-GB dumps) and MiniDumpWithDataSegs (would add every loaded
        // module's globals; only useful with matching PDBs, which means
        // little value to game devs triaging host-side crashes).
        MINIDUMP_TYPE dumpType = static_cast<MINIDUMP_TYPE>(
            MiniDumpNormal |
            MiniDumpWithThreadInfo |
            MiniDumpWithIndirectlyReferencedMemory |
            MiniDumpWithUnloadedModules |
            MiniDumpWithProcessThreadData);

        BOOL dumpOk = MiniDumpWriteDump(GetCurrentProcess(), GetCurrentProcessId(),
                                        hFile, dumpType, info ? &exInfo : nullptr,
                                        nullptr, nullptr);
        CloseHandle(hFile);

        // If the dump itself failed (e.g. out of disk, or MiniDumpWriteDump
        // hit an internal error walking a stack), surface the GetLastError
        // value via OutputDebugString. We deliberately don't use Logger here
        // — see comment block below for the mutex-deadlock hazard. The
        // debug stream is lock-free and visible in DebugView/attached
        // debuggers, which is the right tool for diagnosing why a dump
        // wasn't produced.
        if (!dumpOk) {
            char msg[128];
            _snprintf_s(msg, sizeof(msg), _TRUNCATE,
                        "MXBMRP3 CrashHandler: MiniDumpWriteDump failed, GetLastError=%lu\n",
                        GetLastError());
            OutputDebugStringA(msg);
        }
    }

    // Best-effort: copy the current log file alongside the dump, so users
    // (and devs) browsing the crashes folder can see what the plugin was
    // doing in the moments before the fault. CopyFileA is safe to call
    // from this filter:
    // - We don't touch the Logger mutex, so no deadlock risk if another
    //   thread was mid-log or the faulting thread itself held the lock.
    // - Logger's std::ofstream opens with _SH_DENYNO (MSVC default), so
    //   our read handle doesn't conflict with its write handle.
    // - Logger flushes after every line, so the snapshot is current.
    // - Failure is silent and harmless — the .dmp is the load-bearing
    //   artifact, the .log copy is a convenience.
    char logSrc[MAX_PATH];
    char logDst[MAX_PATH];
    int wSrc = snprintf(logSrc, sizeof(logSrc), "%s\\mxbmrp3\\mxbmrp3_log.txt", root);
    int wDst = snprintf(logDst, sizeof(logDst),
        "%s\\mxbmrp3\\crashes\\mxbmrp3_crash_%04d%02d%02d_%02d%02d%02d_%lu.log",
        root, t.wYear, t.wMonth, t.wDay,
        t.wHour, t.wMinute, t.wSecond,
        GetCurrentProcessId());
    if (wSrc > 0 && wSrc < static_cast<int>(sizeof(logSrc)) &&
        wDst > 0 && wDst < static_cast<int>(sizeof(logDst))) {
        CopyFileA(logSrc, logDst, FALSE);
    }

    // Deliberately don't log here: Logger::log() now takes a mutex, and
    // MiniDumpWriteDump suspends other threads while it walks their
    // stacks. If the faulting thread happened to be mid-log when the
    // fault hit (or another thread is holding the lock right now),
    // acquiring it here would either deadlock or wedge for an unbounded
    // time. The .dmp contains the exception record (code, address,
    // context) plus full stack traces, so this log line was never
    // load-bearing — the dump itself is the diagnostic artifact.

    // Chain to the previous filter (typically the host's own handler, or
    // the OS default that shows the Windows Error Reporting dialog). We
    // explicitly DON'T return EXCEPTION_EXECUTE_HANDLER — that would
    // silently terminate the process and skip the host's own diagnostics.
    if (s_previousFilter) {
        return s_previousFilter(info);
    }
    return EXCEPTION_CONTINUE_SEARCH;
}

}  // namespace

namespace CrashHandler {

void install(const char* savePath) {
    if (s_installed) return;

    if (savePath && savePath[0] != '\0') {
        strncpy_s(s_dumpDir, sizeof(s_dumpDir), savePath, _TRUNCATE);
        // Trim trailing separators so we can append "\mxbmrp3_crashes"
        // without double-slashes.
        size_t len = strlen(s_dumpDir);
        while (len > 0 && (s_dumpDir[len - 1] == '\\' || s_dumpDir[len - 1] == '/')) {
            s_dumpDir[--len] = '\0';
        }
    } else {
        s_dumpDir[0] = '.';
        s_dumpDir[1] = '\0';
    }

    s_previousFilter = SetUnhandledExceptionFilter(&crashFilter);
    s_installed = true;
    DEBUG_INFO_F("CrashHandler installed, dumps -> %s\\mxbmrp3\\crashes\\", s_dumpDir);
}

void uninstall() {
    if (!s_installed) return;
    SetUnhandledExceptionFilter(s_previousFilter);
    s_previousFilter = nullptr;
    s_installed = false;
}

}  // namespace CrashHandler
