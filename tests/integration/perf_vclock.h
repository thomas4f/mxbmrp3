// ============================================================================
// tests/integration/perf_vclock.h
// A virtual clock for the perf ledger: patches the loaded plugin DLL's clock
// IMPORTS (its IAT) so every time read inside the plugin returns simulated
// time that the driver advances, while the driver's own timers stay real.
//
// WHY AT THE IMPORT TABLE, not a seam in the plugin: dozens of plugin files read
// a clock (steady_clock, GetTickCount, QPC) for throttles, coalescing windows,
// hold timers and 1 s scans. A replay runs many times faster than the recording,
// so with the real clock that work fires a different number of times on every
// host and every run, and no counter built on it can be exact. Routing all of
// them through one plugin-side seam would touch every one of those files; the
// cross-build links libstdc++ and winpthreads statically, so steady_clock and
// friends all bottom out in the few kernel32/msvcrt imports patched here. No
// plugin source changes, and the MSVC build is untouched by construction.
//
// Patched: QueryPerformanceCounter / QueryPerformanceFrequency, GetTickCount /
// GetTickCount64, GetSystemTimeAsFileTime, GetSystemTime / GetLocalTime, msvcrt
// _time64, and GetProcAddress for the clocks looked up by name. NOT patched: waits
// and sleeps (WaitForSingleObject, Sleep) - a worker's timed wait still elapses
// in real time, which is harmless here because simulated time only moves
// forward while the driver pumps frames. Wall-clock dates are pinned to one
// fixed instant (plus simulated time) so anything date-stamped is identical
// run to run. Call release() before Shutdown() so teardown sees a live clock.
//
// Evaluated first: Wine's own clock, and an LD_PRELOAD faketime - both shift
// the DRIVER's timers too, and the ledger's timing column needs those real.
// ============================================================================
#pragma once
#include <windows.h>
#include <atomic>
#include <cstdint>
#include <cstring>
#include <ctime>

namespace vclock {

constexpr long long kFreq = 10000000;                    // 100 ns ticks, Wine's own QPF
// 2026-01-05 12:00:00 UTC: as FILETIME (100 ns since 1601) and as Unix seconds.
constexpr unsigned long long kEpochFileTime = 134120880000000000ULL;
constexpr long long kEpochUnix = 1767614400LL;

inline std::atomic<long long>& ticks() { static std::atomic<long long> t{0}; return t; }
// After release(): simulated time keeps running at the REAL rate from where the
// replay left it. See release() for why the patch is never simply undone.
inline std::atomic<bool>& released() { static std::atomic<bool> r{false}; return r; }
inline long long& releaseRealQpc() { static long long q = 0; return q; }
inline long long& releaseTicks() { static long long t = 0; return t; }
inline long long& qpcBase()  { static long long b = 0; return b; }
inline unsigned long long& tickBase() { static unsigned long long b = 0; return b; }

// Advance simulated time to `us` microseconds since the start of the replay.
// Never moves backwards: a tape's timestamps can tie or (across a session
// boundary) step back, and a clock that rewinds breaks every elapsed-time test.
inline void setUs(unsigned long long us) {
    const long long t = static_cast<long long>(us) * 10;
    if (t > ticks().load(std::memory_order_relaxed)) ticks().store(t, std::memory_order_relaxed);
}
inline unsigned long long nowUs() { return static_cast<unsigned long long>(ticks().load() / 10); }

// While STEPPING (Startup and set-up, before frames are pumped), each clock
// read on the stepping thread moves time on by one microsecond. Startup waits
// for its workers in deadline loops that never end if time stands still; a
// fixed step per read ends them, and keeps every timestamp Startup takes on
// the simulated timeline (a periodic task anchored at Startup otherwise fires
// a frame early or late depending on how long the real Startup took).
inline std::atomic<DWORD>& steppingThread() { static std::atomic<DWORD> t{0}; return t; }
inline void stepOnThisThread(bool on) { steppingThread().store(on ? GetCurrentThreadId() : 0); }

inline long long realTicks();
inline long long current() {
    if (!released().load(std::memory_order_acquire)) {
        const DWORD st = steppingThread().load(std::memory_order_relaxed);
        if (st != 0 && st == GetCurrentThreadId()) return ticks().fetch_add(10, std::memory_order_relaxed) + 10;
        return ticks().load(std::memory_order_relaxed);
    }
    return releaseTicks() + (realTicks() - releaseRealQpc());
}

inline BOOL WINAPI vQpc(LARGE_INTEGER* p) {
    if (!p) return FALSE;
    p->QuadPart = qpcBase() + current();
    return TRUE;
}
inline BOOL WINAPI vQpf(LARGE_INTEGER* p) {
    if (!p) return FALSE;
    p->QuadPart = kFreq;
    return TRUE;
}
inline ULONGLONG WINAPI vTick64() {
    return tickBase() + static_cast<unsigned long long>(current() / 10000);
}
inline DWORD WINAPI vTick() { return static_cast<DWORD>(vTick64()); }
inline VOID WINAPI vFileTime(LPFILETIME ft) {
    if (!ft) return;
    const unsigned long long v = kEpochFileTime + static_cast<unsigned long long>(current());
    ft->dwLowDateTime = static_cast<DWORD>(v);
    ft->dwHighDateTime = static_cast<DWORD>(v >> 32);
}
// Local time IS UTC here: the zone is not something a run should depend on.
inline VOID WINAPI vSystemTime(LPSYSTEMTIME st) {
    if (!st) return;
    FILETIME ft; vFileTime(&ft);
    FileTimeToSystemTime(&ft, st);
}
inline FARPROC WINAPI vGetProcAddress(HMODULE m, LPCSTR name);
inline long long __cdecl vTime64(long long* out) {
    const long long v = kEpochUnix + current() / kFreq;
    if (out) *out = v;
    return v;
}

// Real time in the same 100 ns units, read through the DRIVER's own import
// (never patched), for release().
inline long long realTicks() {
    LARGE_INTEGER q, f;
    QueryPerformanceCounter(&q); QueryPerformanceFrequency(&f);
    return (f.QuadPart == kFreq) ? q.QuadPart : static_cast<long long>(q.QuadPart * (double)kFreq / f.QuadPart);
}

inline int& patchCount() { static int n = 0; return n; }

// A clock looked up at run time instead of imported (winpthreads resolves the
// precise system time this way) gets the virtual one too.
inline FARPROC WINAPI vGetProcAddress(HMODULE m, LPCSTR name) {
    if (reinterpret_cast<ULONG_PTR>(name) > 0xFFFF) {
        if (std::strcmp(name, "GetSystemTimePreciseAsFileTime") == 0 ||
            std::strcmp(name, "GetSystemTimeAsFileTime") == 0) return reinterpret_cast<FARPROC>(&vFileTime);
        if (std::strcmp(name, "QueryPerformanceCounter") == 0) return reinterpret_cast<FARPROC>(&vQpc);
        if (std::strcmp(name, "GetTickCount64") == 0) return reinterpret_cast<FARPROC>(&vTick64);
    }
    return GetProcAddress(m, name);   // the driver's own import: the real one
}

// Rewrite the module's import slots for the clock functions. Returns how many
// slots were patched (0 = nothing found, or a host up longer than the fixed
// start below; the caller must treat either as an error: an unpatched clock
// silently makes every counter host-dependent).
inline int install(HMODULE mod) {
    if (!mod) return 0;
    {   // Start simulated time at a FIXED instant, never the real "now": some
        // plugin code takes a phase from a clock's absolute value (the RPM
        // limiter flash is `now / halfPeriod % 2`, the crash vignette picks its
        // first picture from the clock), so any real-time start gives those a
        // different phase on every run. 116 days of uptime is past any test
        // host's (the driver checks), so nothing the DLL stamped during its
        // static init, on the real clock, appears to lie in the future.
        qpcBase() = 100000000000000LL;              // 1e14 x 100 ns = 116 days
        tickBase() = 10000000000ULL;                // 1e10 ms, the same instant
        ticks().store(0);
        if (realTicks() >= qpcBase() || GetTickCount64() >= tickBase()) return 0;
    }
    struct Repl { const char* dll; const char* fn; void* to; };
    const Repl repl[] = {
        { "kernel32.dll", "QueryPerformanceCounter",   reinterpret_cast<void*>(&vQpc) },
        { "kernel32.dll", "QueryPerformanceFrequency", reinterpret_cast<void*>(&vQpf) },
        { "kernel32.dll", "GetTickCount",              reinterpret_cast<void*>(&vTick) },
        { "kernel32.dll", "GetTickCount64",            reinterpret_cast<void*>(&vTick64) },
        { "kernel32.dll", "GetSystemTimeAsFileTime",   reinterpret_cast<void*>(&vFileTime) },
        { "kernel32.dll", "GetSystemTime",             reinterpret_cast<void*>(&vSystemTime) },
        { "kernel32.dll", "GetLocalTime",              reinterpret_cast<void*>(&vSystemTime) },
        { "kernel32.dll", "GetProcAddress",            reinterpret_cast<void*>(&vGetProcAddress) },
        { "msvcrt.dll",   "_time64",                   reinterpret_cast<void*>(&vTime64) },
    };
    auto* base = reinterpret_cast<unsigned char*>(mod);
    auto* dos = reinterpret_cast<IMAGE_DOS_HEADER*>(base);
    auto* nt = reinterpret_cast<IMAGE_NT_HEADERS*>(base + dos->e_lfanew);
    const IMAGE_DATA_DIRECTORY& dir = nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT];
    if (!dir.VirtualAddress) return 0;
    for (auto* imp = reinterpret_cast<IMAGE_IMPORT_DESCRIPTOR*>(base + dir.VirtualAddress); imp->Name; ++imp) {
        const char* dllName = reinterpret_cast<const char*>(base + imp->Name);
        if (!imp->OriginalFirstThunk) continue;     // mingw always emits the name table
        auto* names = reinterpret_cast<IMAGE_THUNK_DATA*>(base + imp->OriginalFirstThunk);
        auto* slots = reinterpret_cast<IMAGE_THUNK_DATA*>(base + imp->FirstThunk);
        for (; names->u1.AddressOfData; ++names, ++slots) {
            if (IMAGE_SNAP_BY_ORDINAL(names->u1.Ordinal)) continue;
            const char* fn = reinterpret_cast<const char*>(
                reinterpret_cast<IMAGE_IMPORT_BY_NAME*>(base + names->u1.AddressOfData)->Name);
            for (const Repl& r : repl) {
                if (_stricmp(dllName, r.dll) != 0 || std::strcmp(fn, r.fn) != 0) continue;
                void** slot = reinterpret_cast<void**>(&slots->u1.Function);
                DWORD old;
                VirtualProtect(slot, sizeof(void*), PAGE_READWRITE, &old);
                ++patchCount();
                *slot = r.to;
                VirtualProtect(slot, sizeof(void*), old, &old);
            }
        }
    }
    return patchCount();
}

// Hand the clock back to real time WITHOUT a jump. The replay runs many times
// faster than real time, so by its end simulated time is minutes ahead of the
// real clock; restoring the original imports would step every clock the plugin
// reads back by those minutes, and a worker that computed a deadline in
// simulated time then waits that long before Shutdown can join it. So the patch
// stays, and simulated time carries on at the real rate from where it stopped.
inline void release() {
    if (released().load()) return;
    releaseTicks() = ticks().load();
    releaseRealQpc() = realTicks();
    released().store(true, std::memory_order_release);
}

}  // namespace vclock
