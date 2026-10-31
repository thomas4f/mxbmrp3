// ============================================================================
// tests/integration/perf_sampler.h
// WHERE the plugin's own CPU time goes: a sampling profiler for the perf
// ledger driver. A second thread suspends the replay thread every
// millisecond, reads its instruction pointer and unwinds a few frames with the
// x64 unwind tables (RtlVirtualUnwind), and counts each stack. The driver
// writes the stacks as module-relative offsets ("count off0 off1 ..."), which
// perf_alloc_sites.py --cpu symbolizes against the DLL the same way it does
// allocation sites, giving self and inclusive time per plugin function.
//
// Run with the real clock (the `hudtime` mode): under the virtual clock the
// replay still takes real time, but the sample interval is the only clock this
// reads, so either works; hudtime just keeps the counters out of it.
//
// Evaluated first: Linux perf (absent here, and it cannot symbolize a PE DLL
// under Wine), valgrind/callgrind (does not follow a Wine process), gprof
// (needs -pg in the plugin build, which changes the code being measured).
// ============================================================================
#pragma once

#include <windows.h>
#include <atomic>
#include <cstdio>
#include <map>
#include <vector>

namespace perf_sampler {

constexpr int kMaxFrames = 24;

struct State {
    HANDLE target = nullptr;
    HANDLE thread = nullptr;
    DWORD64 base = 0, end = 0;
    std::atomic<bool> stop{false};
    std::map<std::vector<DWORD64>, long> stacks;
    long total = 0;
};

inline State& state() { static State s; return s; }

inline bool inModule(DWORD64 a) { return a >= state().base && a < state().end; }

inline DWORD WINAPI samplerMain(LPVOID) {
    State& s = state();
    std::vector<DWORD64> stack;
    stack.reserve(kMaxFrames);
    while (!s.stop.load()) {
        Sleep(1);
        if (SuspendThread(s.target) == static_cast<DWORD>(-1)) continue;
        CONTEXT ctx{};
        ctx.ContextFlags = CONTEXT_FULL;
        stack.clear();  // within its reserve: no allocation while the target is suspended
        if (GetThreadContext(s.target, &ctx)) {
            // Walk out of non-plugin frames (CRT, ntdll) too, so time spent in
            // malloc/memcpy is charged to the plugin function that called it.
            for (int f = 0; f < kMaxFrames && ctx.Rip; ++f) {
                stack.push_back(ctx.Rip);
                DWORD64 imageBase = 0;
                PRUNTIME_FUNCTION fn = RtlLookupFunctionEntry(ctx.Rip, &imageBase, nullptr);
                if (!fn) {  // leaf function: return address is at [rsp]
                    ctx.Rip = *reinterpret_cast<DWORD64*>(ctx.Rsp);
                    ctx.Rsp += 8;
                    continue;
                }
                PVOID handlerData = nullptr;
                DWORD64 establisher = 0;
                RtlVirtualUnwind(UNW_FLAG_NHANDLER, imageBase, ctx.Rip, fn, &ctx, &handlerData,
                                 &establisher, nullptr);
            }
        }
        // Resume BEFORE touching the map: the target may be suspended inside
        // the heap lock, and an allocation here would then wait on it forever.
        ResumeThread(s.target);
        if (stack.empty()) continue;
        ++s.total;
        bool any = false;
        for (DWORD64 a : stack) any = any || inModule(a);
        if (any) {
            ++s.stacks[stack];  // absolute; made relative (or named) at write time
        }
    }
    return 0;
}

// Start sampling the calling thread; `module` is the plugin DLL.
inline bool start(HMODULE module) {
    State& s = state();
    auto* dos = reinterpret_cast<IMAGE_DOS_HEADER*>(module);
    auto* nt = reinterpret_cast<IMAGE_NT_HEADERS*>(reinterpret_cast<char*>(module) + dos->e_lfanew);
    s.base = reinterpret_cast<DWORD64>(module);
    s.end = s.base + nt->OptionalHeader.SizeOfImage;
    if (!DuplicateHandle(GetCurrentProcess(), GetCurrentThread(), GetCurrentProcess(), &s.target,
                         THREAD_ALL_ACCESS, FALSE, 0)) return false;
    s.thread = CreateThread(nullptr, 0, samplerMain, nullptr, 0, nullptr);
    return s.thread != nullptr;
}

// The module a non-plugin frame is in ("user32.dll"), so a stack that ends in
// the OS says where: under Wine a user32 call is a wineserver round trip, tens
// of microseconds that cost well under one on Windows.
inline const char* moduleOf(DWORD64 a, char* buf, DWORD size) {
    HMODULE m = nullptr;
    if (!GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                            reinterpret_cast<LPCSTR>(a), &m) || !GetModuleFileNameA(m, buf, size))
        return "?";
    const char* b = buf;
    for (const char* c = buf; *c; ++c) if (*c == '\\' || *c == '/') b = c + 1;
    return b;
}

// Stop and write "count frame0 frame1 ..." per stack: a hex offset into the
// DLL, or "@module" for a frame outside it.
inline void stopAndWrite(const char* path) {
    State& s = state();
    if (!s.thread) return;
    s.stop.store(true);
    WaitForSingleObject(s.thread, INFINITE);
    CloseHandle(s.thread);
    CloseHandle(s.target);
    s.thread = nullptr;
    FILE* f = fopen(path, "w");
    if (!f) return;
    fprintf(f, "# samples %ld\n", s.total);
    for (const auto& kv : s.stacks) {
        fprintf(f, "%ld", kv.second);
        for (DWORD64 a : kv.first) {
            char buf[MAX_PATH];
            if (inModule(a)) fprintf(f, " %llx", static_cast<unsigned long long>(a - s.base));
            else fprintf(f, " @%s", moduleOf(a, buf, sizeof(buf)));
        }
        fputc('\n', f);
    }
    fclose(f);
}

}  // namespace perf_sampler
