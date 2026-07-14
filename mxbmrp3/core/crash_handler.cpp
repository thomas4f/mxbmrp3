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
#include "plugin_constants.h"   // PLUGIN_VERSION (constant-initialized extern into .rdata, crash-safe)
#include "crash_stack_format.h" // pure backtrace string formatting (no Win32)

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

// Resolve a code address to its owning module's basename + offset. Fills `mod`
// (>= 1 byte) with the basename, or "unknown" if the address isn't in a loaded
// module, and *offset with (addr - moduleBase). GetModuleHandleExA with
// FROM_ADDRESS|UNCHANGED_REFCOUNT resolves without loading anything or bumping a
// refcount — safe from inside the filter (no heap, no disk). Shared by the leaf
// fault resolver and the per-frame backtrace walk.
//
// When the address is in NO loaded module, `mod` stays "unknown" and `*offset`
// carries the RAW address (not 0). That case is diagnostically important — an
// execute access violation whose faulting IP is in no module means control flow
// jumped through a null/corrupt function pointer or into freed/JIT memory (the
// injector-at-launch fingerprint behind the "unknown+0x0" cluster). Reporting the
// real address distinguishes a literal null call (0x0) from a wild jump, and gives
// unresolved backtrace frames (e.g. an injected thunk) their address instead of 0.
void resolveModuleOffset(void* addr, char* mod, size_t modSize,
                         unsigned long long* offset) {
    if (modSize) strncpy_s(mod, modSize, "unknown", _TRUNCATE);
    *offset = reinterpret_cast<unsigned long long>(addr);  // raw addr unless resolved below
    HMODULE hMod = nullptr;
    if (GetModuleHandleExA(
            GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
            GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
            static_cast<LPCSTR>(addr), &hMod) && hMod) {
        char modPath[MAX_PATH];
        DWORD n = GetModuleFileNameA(hMod, modPath, sizeof(modPath));
        if (n > 0 && n < sizeof(modPath)) {
            const char* base = modPath;
            for (const char* p = modPath; *p; ++p)
                if (*p == '\\' || *p == '/') base = p + 1;
            strncpy_s(mod, modSize, base, _TRUNCATE);
        }
        *offset = static_cast<unsigned long long>(
            reinterpret_cast<ULONG_PTR>(addr) - reinterpret_cast<ULONG_PTR>(hMod));
    }
}

#ifdef _MSC_VER
// Walk the FAULTING thread's stack (from the exception CONTEXT) and record the
// top N frames as (module, offset). Uses RtlVirtualUnwind + RtlLookupFunctionEntry
// — NOT StackWalk64/dbghelp: dbghelp needs SymInitialize (which allocates and
// takes a global lock) and could hang or fault the filter on the corrupt heap
// that is the whole premise here, losing the analytics beacon. RtlVirtualUnwind
// unwinds x64 straight from each module's .pdata/.xdata: no heap, no lock, no
// symbols. The walk reads stack memory that may be corrupt, so the whole loop is
// wrapped in __try/__except — a fault just ends the walk with what we have.
// x64/MSVC only; the mingw test build gets the stub below.
//
// Optimization is disabled for this one function: mixing SEH (__try/__except)
// with whole-program optimization (/GL + /LTCG) can trip an MSVC internal
// compiler error (C1001) during code generation. This runs only on the crash
// path (once per crash), so there is nothing to gain from optimizing it and no
// reason to risk the ICE. noinline keeps the SEH frame self-contained.
#pragma optimize("", off)
__declspec(noinline)
int captureBacktrace(CONTEXT* ctxIn, CrashStack::Frame* frames, int maxFrames) {
    if (!ctxIn || !frames || maxFrames <= 0) return 0;
    CONTEXT ctx = *ctxIn;   // full copy — RtlVirtualUnwind mutates nonvolatile regs
    int count = 0;
    __try {
        for (int i = 0; i < maxFrames && ctx.Rip; ++i) {
            resolveModuleOffset(reinterpret_cast<void*>(ctx.Rip),
                                frames[count].module, sizeof(frames[count].module),
                                &frames[count].offset);
            ++count;

            DWORD64 imageBase = 0;
            PRUNTIME_FUNCTION fn = RtlLookupFunctionEntry(ctx.Rip, &imageBase, nullptr);
            ULONG64 rspBefore = ctx.Rsp;
            if (fn) {
                PVOID handlerData = nullptr;
                ULONG64 establisherFrame = 0;
                RtlVirtualUnwind(UNW_FLAG_NHANDLER, imageBase, ctx.Rip, fn,
                                 &ctx, &handlerData, &establisherFrame, nullptr);
            } else {
                // Leaf function (no unwind data): return address sits at [Rsp].
                if (ctx.Rsp == 0) break;
                ctx.Rip = *reinterpret_cast<ULONG64*>(ctx.Rsp);
                ctx.Rsp += sizeof(ULONG64);
            }
            // The stack must unwind toward higher addresses. If it doesn't move
            // up, the chain is corrupt or looping — stop rather than spin.
            if (ctx.Rsp <= rspBefore) break;
        }
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        // A frame walk faulted (corrupt stack) — return what we captured so far.
    }
    return count;
}
#pragma optimize("", on)
#else
// mingw cross-build (tests/integration): no MSVC SEH __try, so no in-filter walk.
// The pure formatter is still exercised by the Linux unit tests; the real walk is
// confirmed by the first MSVC-built crash, like the ASan job.
int captureBacktrace(CONTEXT*, CrashStack::Frame*, int) { return 0; }
#endif

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

    // Analytics breadcrumb (crash-safe): resolve the faulting module + offset and
    // write a tiny marker file that the plugin reads on its NEXT launch to report
    // the crash (fault location + when) to analytics. Written AFTER the dump so
    // nothing here can jeopardize the load-bearing .dmp, and — like the CopyFileA
    // above — it uses only stack buffers and Win32 calls (no heap, no Logger, no
    // network). Best-effort: any failure is silent. The plugin deletes the marker
    // once it has reported it.
    if (info && info->ExceptionRecord) {
        void* faultAddr = info->ExceptionRecord->ExceptionAddress;
        DWORD code = info->ExceptionRecord->ExceptionCode;

        // Module basename + offset of the faulting instruction (the leaf). Shared
        // resolver — same logic the per-frame backtrace below uses.
        char modName[CrashStack::MODULE_NAME_SIZE] = "unknown";
        unsigned long long offset = 0;
        resolveModuleOffset(faultAddr, modName, sizeof(modName), &offset);

        // Access-violation sub-type (read/write/execute) from ExceptionInformation[0].
        // Meaningful only for access violations; "" otherwise (field omitted). Fixed
        // Windows ABI, no maintenance. "execute" fingerprints a jump into non-code —
        // the tell for the "unknown+..." injector-at-launch cluster.
        const char* avType = "";
        if (code == EXCEPTION_ACCESS_VIOLATION &&
            info->ExceptionRecord->NumberParameters >= 1) {
            avType = CrashStack::avTypeName(
                static_cast<unsigned long long>(info->ExceptionRecord->ExceptionInformation[0]));
        }

        // MX Bikes build fingerprint: the host executable's PE link timestamp. An
        // mxbikes.exe+offset fault is only interpretable against a specific game build
        // (offsets shift between betas), so capture which build crashed. Reading the
        // in-memory PE header (module base -> e_lfanew -> NT headers) needs no disk I/O
        // and is safe here. 0 if it can't be read.
        unsigned long gameBuild = 0;
        if (HMODULE exe = GetModuleHandleA(nullptr)) {
            PIMAGE_DOS_HEADER dos = reinterpret_cast<PIMAGE_DOS_HEADER>(exe);
            if (dos->e_magic == IMAGE_DOS_SIGNATURE) {
                PIMAGE_NT_HEADERS nt = reinterpret_cast<PIMAGE_NT_HEADERS>(
                    reinterpret_cast<BYTE*>(exe) + dos->e_lfanew);
                if (nt->Signature == IMAGE_NT_SIGNATURE) {
                    gameBuild = nt->FileHeader.TimeDateStamp;
                }
            }
        }

        // Host process basename. The beacon otherwise only carries the game_build
        // timestamp, so a crash in a dev-tool host (e.g. mxbmrp3_replay.exe running the
        // DLL) is indistinguishable from a real in-game crash on the dashboard.
        // Stack buffers + a cheap module query only; safe from inside the filter.
        char hostExe[64] = "";
        if (HMODULE exe = GetModuleHandleA(nullptr)) {
            char exePath[MAX_PATH];
            DWORD n = GetModuleFileNameA(exe, exePath, sizeof(exePath));
            if (n > 0 && n < sizeof(exePath)) {
                const char* base = exePath;
                for (const char* p = exePath; *p; ++p)
                    if (*p == '\\' || *p == '/') base = p + 1;
                strncpy_s(hostExe, sizeof(hostExe), base, _TRUNCATE);
            }
        }

        // Crash time in Unix seconds (matches the analytics epoch clock).
        FILETIME ft;
        GetSystemTimeAsFileTime(&ft);
        ULARGE_INTEGER u;
        u.LowPart = ft.dwLowDateTime;
        u.HighPart = ft.dwHighDateTime;
        unsigned long long epoch = (u.QuadPart - 116444736000000000ULL) / 10000000ULL;

        // Faulting-stack backtrace (top frames as module+0xoffset). A crash in our
        // container-walk code (StatsManager::save, std::_Tree::_Erase_tree) reports
        // only its leaf offset today, so we can't tell a same-stack culprit
        // (re-entrancy / a bad pointer we passed in) from a bystander walking memory
        // some already-returned code corrupted. The backtrace settles that: it names
        // the callers between the leaf and our code (e.g. which teardown path called
        // clear()). Plain space-delimited and bounded to MAX_STACK_CHARS (whole frames
        // only) so the analytics sink stores it verbatim without truncating mid-frame;
        // "" if the walk found nothing.
        char stackStr[CrashStack::MAX_STACK_CHARS + 1];
        stackStr[0] = '\0';
        if (info->ContextRecord) {
            CrashStack::Frame frames[CrashStack::MAX_FRAMES];
            int nf = captureBacktrace(info->ContextRecord, frames, CrashStack::MAX_FRAMES);
            CrashStack::formatFrameList(stackStr, sizeof(stackStr), frames, nf);
        }

        char marker[MAX_PATH];
        int wM = snprintf(marker, sizeof(marker), "%s\\mxbmrp3\\pending_crash.json", root);
        if (wM > 0 && wM < static_cast<int>(sizeof(marker))) {
            // Also pin the plugin version and game build AT CRASH TIME - the report is
            // sent on the next launch, which may be a different (upgraded) version.
            // The optional av_type / stack fields are pre-rendered as JSON fragments
            // (",key":"value", or empty) so the body is ONE snprintf regardless of which
            // are present. Both hold only safe chars (literals / module basenames + hex +
            // spaces, no quotes or backslashes), so they embed inside JSON verbatim.
            char avFrag[32];
            avFrag[0] = '\0';
            if (avType[0])
                snprintf(avFrag, sizeof(avFrag), ",\"av_type\":\"%s\"", avType);
            char stackFrag[CrashStack::MAX_STACK_CHARS + 16];
            stackFrag[0] = '\0';
            if (stackStr[0])
                snprintf(stackFrag, sizeof(stackFrag), ",\"stack\":\"%s\"", stackStr);

            char bodyBuf[768];
            int wB = snprintf(bodyBuf, sizeof(bodyBuf),
                "{\"fault\":\"%s+0x%llx\",\"code\":\"0x%08lX\",\"plugin\":\"%s\","
                "\"game_build\":\"0x%08lX\",\"host\":\"%s\",\"time\":%llu%s%s}",
                modName, offset, static_cast<unsigned long>(code),
                PluginConstants::PLUGIN_VERSION, gameBuild, hostExe, epoch, avFrag, stackFrag);
            if (wB > 0 && wB < static_cast<int>(sizeof(bodyBuf))) {
                HANDLE hMarker = CreateFileA(marker, GENERIC_WRITE, 0, nullptr,
                                             CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
                if (hMarker != INVALID_HANDLE_VALUE) {
                    DWORD wrote = 0;
                    WriteFile(hMarker, bodyBuf, static_cast<DWORD>(wB), &wrote, nullptr);
                    CloseHandle(hMarker);
                }
            }
        }
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
