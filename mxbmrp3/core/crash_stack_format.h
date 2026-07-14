// ============================================================================
// core/crash_stack_format.h
// Pure (no Win32) formatting for the crash handler's faulting-stack backtrace.
//
// The stack WALK itself (RtlVirtualUnwind over the faulting CONTEXT) is
// Windows/MSVC-only and lives in crash_handler.cpp. This header is just the
// string plumbing — turning already-resolved (module basename, offset) frames
// into a compact backtrace string like
//   "mxbmrp3.dlo+0x378d8 mxbmrp3.dlo+0x1234 mxbikes.exe+0x5678"
// so it compiles natively and is unit-tested on Linux (tests/unit/), where the
// walk can't run. Everything here uses only fixed caller-owned buffers and
// snprintf: the crash filter runs on a possibly-corrupt heap, so NO allocation.
//
// Why a plain space-delimited string, not a JSON array: the backtrace is sent as
// one Aptabase string PROP, and the ingest sink (a) caps string prop values at
// ~180 chars, truncating anything longer *mid-frame*, and (b) parses and
// re-serializes a JSON-looking value (turning ["a","b"] into single-quoted
// ['a','b']). A plain space-delimited list dodges both: nothing to re-parse, and
// formatFrameList bounds the output to MAX_STACK_CHARS emitting only WHOLE frames,
// so the sink never truncates it. Downstream splits on spaces.
// ============================================================================
#pragma once

#include <cstdio>
#include <cstddef>

namespace CrashStack {

// Max frames the walk captures. The formatter then emits as many WHOLE frames as
// fit in MAX_STACK_CHARS (typically ~8), which for our crashes covers the plugin
// call chain — those frames cluster at the top of the stack, and the recursive
// std::_Tree::_Erase_tree teardown faults at the ROOT (shallow), not deep in the
// recursion. The full stack is always in the paired .dmp.
constexpr int MAX_FRAMES = 16;

// Ceiling on the emitted backtrace string, kept under the analytics sink's ~180-
// char string-prop cap so the value is never truncated mid-frame at ingest.
constexpr int MAX_STACK_CHARS = 176;

// Module basename buffer size (matches the crash handler's leaf resolver).
constexpr int MODULE_NAME_SIZE = 64;

struct Frame {
    char module[MODULE_NAME_SIZE];   // basename, e.g. "mxbmrp3.dlo" (or "unknown")
    unsigned long long offset;       // address - module base
};

// Format one frame as "<module>+0x<offset>" into `out`. An empty/null module
// renders as "unknown". Returns chars written (excluding the NUL), or 0 if it
// didn't fit or args were bad (in which case out is left as "").
inline int formatFrame(char* out, size_t outSize,
                       const char* module, unsigned long long offset) {
    if (!out || outSize == 0) return 0;
    const char* m = (module && module[0]) ? module : "unknown";
    int n = std::snprintf(out, outSize, "%s+0x%llx", m, offset);
    if (n < 0 || static_cast<size_t>(n) >= outSize) { out[0] = '\0'; return 0; }
    return n;
}

// Build a space-delimited backtrace of the first `count` frames into `out`:
//   "mod0+0x.. mod1+0x.. mod2+0x.."
// bounded to `maxChars` (default MAX_STACK_CHARS). Emits only WHOLE frames — if
// the next frame won't fit the budget, it stops rather than writing a partial one
// (so the analytics sink's length cap can't cut a frame in half). A frame that
// can't be formatted is skipped. Module basenames are plain [A-Za-z0-9._-] with
// no spaces/quotes, so the value needs no escaping. Returns chars written
// (excluding NUL); `out` is always NUL-terminated (empty on bad/empty input).
// Map an access-violation type code (EXCEPTION_RECORD::ExceptionInformation[0]
// for EXCEPTION_ACCESS_VIOLATION) to a label: 0 -> "read", 1 -> "write",
// 8 -> "execute". Anything else (or a non-AV exception) -> "" so the caller omits
// the field. A fixed Windows ABI, so this needs no maintenance. Pure — no Win32,
// unit-tested on Linux. `execute` is the tell for the injector-at-launch cluster.
inline const char* avTypeName(unsigned long long info0) {
    switch (info0) {
        case 0: return "read";
        case 1: return "write";
        case 8: return "execute";
        default: return "";
    }
}

inline int formatFrameList(char* out, size_t outSize, const Frame* frames, int count,
                           size_t maxChars = MAX_STACK_CHARS) {
    if (!out || outSize == 0) return 0;
    out[0] = '\0';
    if (!frames || count <= 0) return 0;

    // Never write past the caller's buffer, and never exceed the sink budget.
    size_t budget = (maxChars < outSize) ? maxChars : (outSize - 1);

    char frameBuf[MODULE_NAME_SIZE + 24];   // "<mod>+0x<16 hex>" + NUL
    size_t pos = 0;
    int emitted = 0;
    for (int i = 0; i < count; ++i) {
        int fn = formatFrame(frameBuf, sizeof(frameBuf),
                             frames[i].module, frames[i].offset);
        if (fn <= 0) continue;   // unformattable frame — skip, keep the rest
        size_t need = (emitted > 0 ? 1u : 0u) + static_cast<size_t>(fn);  // sep + frame
        if (pos + need > budget) break;      // whole frames only — stop, don't truncate
        if (emitted > 0) out[pos++] = ' ';
        for (int k = 0; k < fn; ++k) out[pos++] = frameBuf[k];
        out[pos] = '\0';
        ++emitted;
    }
    return static_cast<int>(pos);
}

}  // namespace CrashStack
