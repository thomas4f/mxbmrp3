// ============================================================================
// core/test_hooks_perf.cpp
// The MXBMRP3_Test_* exports behind the perf ledger (run_perf_ledger.sh): a
// game-thread heap-allocation counter, the per-HUD rebuild and primitive rows
// the BenchmarkWidget already collects, and a stand-in for an open web overlay.
// Everything here is a COUNT, so the ledger can gate it exactly where timings
// can only be reported.
//
// THE ALLOCATION COUNTER REPLACES GLOBAL operator new/delete FOR THE WHOLE TEST
// DLL. The cross-build links libstdc++ statically (mxbmrp3/CMakeLists.txt), so
// these definitions also catch the library's own allocations - std::string,
// std::vector growth, std::function - which is the point: "no allocation per
// frame" is a rule about what the code ends up doing, not what it spells.
// Counted only on the thread that armed it (the game thread in the driver), so
// a worker allocating in the background cannot leak into a frame's figure.
// Not counted: plain malloc/strdup and Win32 heap calls, which the plugin's own
// code does not use on its hot paths, and the aligned operator new (for types
// over-aligned past 16 bytes), which keeps the library's own allocator.
//
// Same rules as core/test_hooks.cpp: the whole file is gated on
// MXBMRP3_TEST_BUILD and mxbmrp3/CMakeLists.txt removes it from every shipping
// target's source list, so neither the exports nor the replaced operators can
// exist in a released DLL.
// ============================================================================
#include "../game/game_config.h"

#if defined(MXBMRP3_TEST_BUILD)

#include <windows.h>
#include <malloc.h>
#include <atomic>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cmath>
#include <cstring>
#include <new>
#include "http_server.h"
#include "hud_manager.h"
#include "../hud/map_hud.h"
#include "../hud/performance_hud.h"
#include "../hud/telemetry_hud.h"
#include "plugin_data.h"

namespace {

// 0 = counting off. Otherwise the id of the one thread whose allocations count.
std::atomic<DWORD> g_allocThread{0};
std::atomic<unsigned long long> g_allocCount{0};
std::atomic<unsigned long long> g_allocBytes{0};
std::atomic<long long> g_liveBytes{0};

inline bool countingHere() {
    const DWORD t = g_allocThread.load(std::memory_order_relaxed);
    return t != 0 && t == GetCurrentThreadId();
}

// Allocation SITES (MXBMRP3_Test_AllocSites): while on, each counted
// allocation's call stack is binned with the caller's tag (which callback or
// Draw the driver is in), in a fixed open-addressed table so recording never
// allocates. The driver dumps it as module offsets for addr2line.
constexpr int kSiteDepth = 10;
constexpr int kSiteSlots = 8192;
struct Site { int tag; unsigned short depth; void* pc[kSiteDepth]; unsigned long long count, bytes; };
Site g_sites[kSiteSlots];
bool g_sitesOn = false;
int g_siteTag = 0;

void recordSite(std::size_t n) {
    void* pc[kSiteDepth + 1];
    const USHORT got = RtlCaptureStackBackTrace(1, kSiteDepth, pc, nullptr);
    unsigned long long h = static_cast<unsigned long long>(g_siteTag) * 1099511628211ULL;
    for (USHORT i = 0; i < got; ++i) h = (h ^ reinterpret_cast<uintptr_t>(pc[i])) * 1099511628211ULL;
    for (int probe = 0; probe < kSiteSlots; ++probe) {
        Site& st = g_sites[(h + probe) & (kSiteSlots - 1)];
        if (st.count == 0) {
            st.tag = g_siteTag; st.depth = got;
            std::memcpy(st.pc, pc, got * sizeof(void*));
        } else if (st.tag != g_siteTag || st.depth != got ||
                   std::memcmp(st.pc, pc, got * sizeof(void*)) != 0) {
            continue;
        }
        ++st.count; st.bytes += n;
        return;
    }
}

}  // namespace

void* operator new(std::size_t n) {
    void* p = std::malloc(n ? n : 1);
    if (!p) std::abort();   // test build only: an OOM here is a harness failure
    if (countingHere()) {
        g_allocCount.fetch_add(1, std::memory_order_relaxed);
        g_allocBytes.fetch_add(n, std::memory_order_relaxed);
        g_liveBytes.fetch_add(static_cast<long long>(_msize(p)), std::memory_order_relaxed);
        if (g_sitesOn) recordSite(n);
    }
    return p;
}

void operator delete(void* p) noexcept {
    if (!p) return;
    if (countingHere()) {
        g_liveBytes.fetch_sub(static_cast<long long>(_msize(p)), std::memory_order_relaxed);
    }
    std::free(p);
}

void operator delete(void* p, std::size_t) noexcept {
    ::operator delete(p);
}

extern bool g_mapStraightRunsOff;   // hud/map_hud_track.cpp

// The Performance HUD's FPS graph over a filled history: every sample `fps`,
// every other one `swing` higher. Returns its quads in the graph's good colour,
// the line's pieces. A held (or ceiling-clamped) frame rate is a level line.
int MXBMRP3_Test_PerformanceFpsQuadsImpl(float fps, float swing) {
    PerformanceHud& hud = HudManager::getInstance().getPerformanceHud();
    hud.m_enabledElements = PerformanceHud::ELEM_FPS;
    hud.m_displayMode = PerformanceHud::DISPLAY_GRAPHS;
    for (int i = 0; i < PerformanceHud::GRAPH_HISTORY_SIZE; ++i)
        hud.m_fpsHistory[i] = fps + ((i % 2) ? swing : 0.0f);
    hud.rebuildRenderData();
    const unsigned long color = hud.getColor(ColorSlot::POSITIVE) | 0xFF000000;
    int n = 0;
    for (const SPluginQuad_t& q : hud.getQuads())
        if (q.m_ulColor == color) ++n;
    return n;
}

extern "C" {

// Arm the counter on the CALLING thread and zero it. on=0 disarms and keeps
// the totals readable.
__declspec(dllexport) void MXBMRP3_Test_AllocCount(int on) {
    if (on) {
        g_allocCount.store(0);
        g_allocBytes.store(0);
        g_liveBytes.store(0);
    }
    g_allocThread.store(on ? GetCurrentThreadId() : 0);
}

// Running totals since the last arm. liveBytes is net (allocated minus freed,
// by usable block size) and can go negative when a block from before the arm
// is freed after it. Any out-pointer may be null.
__declspec(dllexport) void MXBMRP3_Test_AllocStats(unsigned long long* count,
                                                   unsigned long long* bytes,
                                                   long long* liveBytes) {
    if (count) *count = g_allocCount.load();
    if (bytes) *bytes = g_allocBytes.load();
    if (liveBytes) *liveBytes = g_liveBytes.load();
}

// One per-HUD row of the BenchmarkWidget's metrics (collected only while the
// widget is shown - see MXBMRP3_Test_BenchmarkWidget): stint rebuild count and
// the primitives the HUD emitted into the last frame. Returns 0 past the end.
__declspec(dllexport) int MXBMRP3_Test_BenchmarkHudRow(int index, char* name, int nameCap,
                                                       int* rebuilds, int* quads, int* strings) {
    const auto& bm = PluginData::getInstance().getBenchmarkMetrics();
    if (index < 0 || index >= bm.hudCount) return 0;
    const auto& h = bm.huds[index];
    if (name && nameCap > 0) {
        std::strncpy(name, h.name, static_cast<size_t>(nameCap) - 1);
        name[nameCap - 1] = '\0';
    }
    if (rebuilds) *rebuilds = h.stintRebuildCount;
    if (quads) *quads = h.quadCount;
    if (strings) *strings = h.stringCount;
    return 1;
}

// Allocation-site profiling for the counted thread. mode 1 = start (clears the
// table), 0 = stop, 2 = set the tag that following allocations are binned
// under (tag = the driver's event type, or its own id for Draw), 3 = write the
// table to `path` as "tag count bytes off0 off1 ..." with offsets from this
// DLL's base. Returns the number of distinct sites for mode 3.
__declspec(dllexport) int MXBMRP3_Test_AllocSites(int mode, int tag, const char* path) {
    switch (mode) {
        case 1: std::memset(g_sites, 0, sizeof(g_sites)); g_sitesOn = true; return 0;
        case 0: g_sitesOn = false; return 0;
        case 2: g_siteTag = tag; return 0;
        default: break;
    }
    HMODULE self = nullptr;
    GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                       reinterpret_cast<LPCSTR>(&MXBMRP3_Test_AllocSites), &self);
    const uintptr_t base = reinterpret_cast<uintptr_t>(self);
    FILE* f = path ? std::fopen(path, "w") : nullptr;
    if (!f) return -1;
    int n = 0;
    for (const Site& st : g_sites) {
        if (!st.count) continue;
        ++n;
        std::fprintf(f, "%d %llu %llu", st.tag, st.count, st.bytes);
        for (int i = 0; i < st.depth; ++i)
            std::fprintf(f, " %llx", static_cast<unsigned long long>(reinterpret_cast<uintptr_t>(st.pc[i]) - base));
        std::fputc('\n', f);
    }
    std::fclose(f);
    return n;
}

// The same row's TIMES (stint totals, microseconds): every rebuild, and the
// whole update() call including them. Real time, so reported, never gated.
__declspec(dllexport) int MXBMRP3_Test_BenchmarkHudTime(int index, long long* rebuildUs, long long* updateUs) {
    const auto& bm = PluginData::getInstance().getBenchmarkMetrics();
    if (index < 0 || index >= bm.hudCount) return 0;
    if (rebuildUs) *rebuildUs = bm.huds[index].stintTotalTimeUs;
    if (updateUs) *updateUs = bm.huds[index].stintUpdateTimeUs;
    return 1;
}

// Invisible primitives in one HUD's own output (by registration index): quads
// with alpha 0, quads whose four corners coincide, and strings with no text.
// The engine pays for each of them all the same. Returns 0 past the end.
__declspec(dllexport) int MXBMRP3_Test_HudPrimitiveAudit(int index, char* name, int nameCap, int* visible,
                                                        int* quads, int* alphaZero, int* degenerate,
                                                        int* strings, int* emptyStrings) {
    const auto& huds = HudManager::getInstance().getHuds();
    if (index < 0 || index >= static_cast<int>(huds.size()) || !huds[index]) return 0;
    const BaseHud& h = *huds[index];
    if (name && nameCap > 0) {
        std::strncpy(name, h.getHarnessId(), static_cast<size_t>(nameCap) - 1);
        name[nameCap - 1] = '\0';
    }
    int a0 = 0, deg = 0, empty = 0;
    for (const SPluginQuad_t& q : h.getQuads()) {
        if ((q.m_ulColor >> 24) == 0) ++a0;
        bool same = true;
        for (int c = 1; c < 4; ++c)
            same = same && q.m_aafPos[c][0] == q.m_aafPos[0][0] && q.m_aafPos[c][1] == q.m_aafPos[0][1];
        if (same) ++deg;
    }
    for (const SPluginString_t& st : h.getStrings())   // what the engine cull drops: empty or alpha 0
        if (st.m_szString[0] == '\0' || (st.m_ulColor >> 24) == 0) ++empty;
    if (visible) *visible = h.isVisibleAnySurface() ? 1 : 0;
    if (quads) *quads = static_cast<int>(h.getQuads().size());
    if (alphaZero) *alphaZero = a0;
    if (degenerate) *degenerate = deg;
    if (strings) *strings = static_cast<int>(h.getStrings().size());
    if (emptyStrings) *emptyStrings = empty;
    return 1;
}

// The map's straight runs (renderTrack, map_hud_track.cpp) on or off, and the
// area its quads cover - in total, and weighted by colour so that area moving
// from one colour to another shows - so a test can draw one frame both ways and
// see only the quad count move.
__declspec(dllexport) void MXBMRP3_Test_MapStraightRuns(int on) { g_mapStraightRunsOff = on == 0; }
__declspec(dllexport) int MXBMRP3_Test_MapQuadArea(double* area, double* byColor) {
    double total = 0.0, weighted = 0.0;
    const auto& quads = HudManager::getInstance().getMapHud().getQuads();
    for (const SPluginQuad_t& q : quads) {
        double twice = 0.0;   // shoelace
        for (int c = 0; c < 4; ++c) {
            const int n = (c + 1) % 4;
            twice += static_cast<double>(q.m_aafPos[c][0]) * q.m_aafPos[n][1] -
                     static_cast<double>(q.m_aafPos[n][0]) * q.m_aafPos[c][1];
        }
        total += std::fabs(twice) * 0.5;
        weighted += std::fabs(twice) * 0.5 * static_cast<double>(q.m_ulColor % 1000003ul + 1ul);
    }
    if (area) *area = total;
    if (byColor) *byColor = weighted;
    return static_cast<int>(quads.size());
}

// Heap allocations in one map rebuild with the rider shape set to `shape` (0 =
// Off), after a warm-up rebuild: the map rebuilds every frame, so this is 0.
__declspec(dllexport) unsigned long long MXBMRP3_Test_MapRebuildAllocs(int shape) {
    MapHud& map = HudManager::getInstance().getMapHud();
    map.setRiderShape(shape);
    map.setDataDirty(); map.update();   // warm: capacities settle
    MXBMRP3_Test_AllocCount(1);
    map.setDataDirty(); map.update();
    MXBMRP3_Test_AllocCount(0);
    return g_allocCount.load();
}

// Quads in the Telemetry HUD's frame drawn in the throttle line's colour. A
// held throttle is one quad (BaseHud::addChartSegment).
__declspec(dllexport) int MXBMRP3_Test_PerformanceFpsQuads(float fps, float swing) {
    return MXBMRP3_Test_PerformanceFpsQuadsImpl(fps, swing);
}

__declspec(dllexport) int MXBMRP3_Test_TelemetryThrottleQuads() {
    const unsigned long color = PluginConstants::SemanticColors::THROTTLE | 0xFF000000;
    int n = 0;
    for (const SPluginQuad_t& q : HudManager::getInstance().getTelemetryHud().getQuads())
        if (q.m_ulColor == color) ++n;
    return n;
}

// Count as an /api/state poll now - see HttpServer::testMarkClientActive.
__declspec(dllexport) void MXBMRP3_Test_HttpMarkClientActive() {
    HttpServer::getInstance().testMarkClientActive();
}

}  // extern "C"

#endif  // MXBMRP3_TEST_BUILD
