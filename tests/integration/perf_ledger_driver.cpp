// ============================================================================
// tests/integration/perf_ledger_driver.cpp
// The perf LEDGER: replay a busy tape through the real test DLL on a virtual
// clock at a fixed simulated frame rate, and print what every frame cost -
// exact COUNTERS (primitives handed to the engine, heap allocations, snapshot
// builds, a hash of the frames) and noisy TIMINGS, kept apart because only the
// first kind can gate. run_perf_ledger.sh runs it, repeats it, checks the
// counters agree across repeats and against the committed baseline.
//
//   wine perf_ledger_driver.exe mxbmrp3_test.dlo Z:\tmp\busy.tape all|defaults [hud|hudtime]
//
// Profiles: `defaults` is the shipped look (no theme; drop shadow + glow on,
// which the test build otherwise turns off). `all` is every function on, the
// worst case: every HUD visible at its maximum settings, a theme, and the web
// overlay counted as open (so the JSON snapshot builds as it would for a
// streamer). `hud` adds the BenchmarkWidget pass for per-HUD rebuild counts -
// a separate run, because the widget draws itself and times every HUD, which
// would move both the primitive counts and the timings of the main pass.
// `hudtime` is that pass on the real clock, for the widget's per-HUD TIMES
// (every interval the plugin measures is 0 on the virtual clock); its counters
// depend on the host, so only the times are read.
//
// MXB_LEDGER_PROFILE=<file> also samples where the replay spends CPU
// (perf_sampler.h); perf_alloc_sites.py --cpu reads the file.
//
// DETERMINISM. Frames are pumped at a fixed SIMULATED cadence between tape
// events (recorded Draw events are ignored), so the frame count is a property
// of the tape, not of the recording machine's FPS or this host's speed; and the
// plugin's clock reads are patched to that simulated time (perf_vclock.h).
// Warm-up frames (before kWarmupS of simulated time) are excluded, and so is
// everything the startup itself allocates.
//
// THE ENGINE COST IS AN ESTIMATE, priced with tools/probetheme's measurements
// on the owner's PC (v1.29, README "What it found"): 1.00 us per quad, +0.62 us
// per sprite change between consecutive quads, 0.177 us per glyph (2.66 us per
// 15-character string). It is the biggest cost the plugin causes - roughly 40x
// its own Draw - and it is exact up to those three prices, which is why it is a
// counter here and not a timing.
// ============================================================================
#include "plugin_host.h"
#include "perf_sampler.h"
#include "perf_vclock.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <map>
#include <string>
#include <vector>

namespace {

// The two Draw output arrays, mirrored from mxb_api.h (the harness's
// plugin_api.h carries only the callback payloads). Read-only here.
struct SPluginQuad_t { float m_aafPos[4][2]; int m_iSprite; unsigned long m_ulColor; };
struct SPluginString_t {
    char m_szString[100]; float m_afPos[2]; int m_iFont; float m_fSize; int m_iJustify; unsigned long m_ulColor;
};
static_assert(sizeof(SPluginQuad_t) == 40 && sizeof(SPluginString_t) == 124, "Draw array layout differs from mxb_api.h");

constexpr double kFrameHz   = 120.0;   // simulated Draw cadence
constexpr double kWarmupS   = 5.0;     // excluded from every figure
constexpr double kIdleTailS = 10.0;    // frames with no events after the tape
constexpr int    kDrawTag   = 1000;    // allocation-site tag for Draw (callbacks use their event type)
constexpr int    kHashEvery = 60;      // frames between frame-hash folds

// Engine prices (us), see the header.
constexpr double kQuadUs = 1.00, kSwitchUs = 0.62, kGlyphUs = 0.177;

typedef void (*PFN_AllocCount)(int);
typedef int (*PFN_AllocSites)(int, int, const char*);
typedef int (*PFN_Audit)(int, char*, int, int*, int*, int*, int*, int*, int*);
typedef void (*PFN_AllocStats)(unsigned long long*, unsigned long long*, long long*);
typedef int  (*PFN_HudRow)(int, char*, int, int*, int*, int*);

struct Event { tape::EventType type; uint64_t us; std::vector<uint8_t> data; };

LARGE_INTEGER g_freq;
inline double realUs() {
    LARGE_INTEGER t; QueryPerformanceCounter(&t);   // the DRIVER's import: never patched
    return t.QuadPart * 1e6 / static_cast<double>(g_freq.QuadPart);
}

struct Timing {
    std::vector<double> us;
    void add(double v) { us.push_back(v); }
    void print(const char* name) {
        if (us.empty()) return;
        std::sort(us.begin(), us.end());
        double sum = 0; for (double v : us) sum += v;
        auto pct = [&](double p) { return us[std::min(us.size() - 1, static_cast<size_t>(p * (us.size() - 1)))]; };
        printf("TIME %s calls=%zu avg=%.2f p50=%.2f p99=%.2f max=%.2f\n",
               name, us.size(), sum / us.size(), pct(0.50), pct(0.99), us.back());
    }
};

// Everything one group of frames (the replay, or the idle tail) adds up.
struct FrameTotals {
    unsigned long long frames = 0, quads = 0, strings = 0, glyphs = 0, switches = 0;
    unsigned long long allocs = 0, allocBytes = 0, allocFrames = 0, allocMax = 0;
    unsigned long long quadsMax = 0, stringsMax = 0;
    double engineUs = 0;
    Timing draw;
    void print(const char* g) {
        printf("COUNT %s.frames %llu\n", g, frames);
        if (!frames) return;
        printf("COUNT %s.quads %llu\n", g, quads);
        printf("COUNT %s.quads_max %llu\n", g, quadsMax);
        printf("COUNT %s.strings %llu\n", g, strings);
        printf("COUNT %s.strings_max %llu\n", g, stringsMax);
        printf("COUNT %s.glyphs %llu\n", g, glyphs);
        printf("COUNT %s.sprite_switches %llu\n", g, switches);
        printf("COUNT %s.engine_us_per_frame %.1f\n", g, engineUs / frames);
        printf("COUNT %s.draw_allocs %llu\n", g, allocs);
        printf("COUNT %s.draw_alloc_bytes %llu\n", g, allocBytes);
        printf("COUNT %s.draw_alloc_frames %llu\n", g, allocFrames);
        printf("COUNT %s.draw_allocs_max %llu\n", g, allocMax);
    }
};

const char* typeName(tape::EventType t) {
    switch (t) {
        case tape::EventType::EventInit: return "EventInit";
        case tape::EventType::RunInit: return "RunInit";
        case tape::EventType::RunDeinit: return "RunDeinit";
        case tape::EventType::RunStart: return "RunStart";
        case tape::EventType::RunStop: return "RunStop";
        case tape::EventType::RunLap: return "RunLap";
        case tape::EventType::RunSplit: return "RunSplit";
        case tape::EventType::RunTelemetry: return "RunTelemetry";
        case tape::EventType::TrackCenterline: return "TrackCenterline";
        case tape::EventType::RaceEvent: return "RaceEvent";
        case tape::EventType::RaceSession: return "RaceSession";
        case tape::EventType::RaceSessionState: return "RaceSessionState";
        case tape::EventType::RaceAddEntry: return "RaceAddEntry";
        case tape::EventType::RaceRemoveEntry: return "RaceRemoveEntry";
        case tape::EventType::RaceLap: return "RaceLap";
        case tape::EventType::RaceSplit: return "RaceSplit";
        case tape::EventType::RaceHoleshot: return "RaceHoleshot";
        case tape::EventType::RaceClassification: return "RaceClassification";
        case tape::EventType::RaceTrackPosition: return "RaceTrackPosition";
        case tape::EventType::RaceCommunication: return "RaceCommunication";
        case tape::EventType::RaceVehicleData: return "RaceVehicleData";
        default: return nullptr;
    }
}

class Ledger {
public:
    explicit Ledger(PluginHost& host) : m_host(host) {
        m_draw = host.sym<PFN_Draw>("Draw");
        m_allocCount = host.sym<PFN_AllocCount>("MXBMRP3_Test_AllocCount");
        m_allocStats = host.sym<PFN_AllocStats>("MXBMRP3_Test_AllocStats");
        m_frameHash = host.sym<unsigned long long(*)()>("MXBMRP3_Test_GameFrameHash");
        m_markClient = host.sym<void(*)()>("MXBMRP3_Test_HttpMarkClientActive");
        m_sites = host.sym<PFN_AllocSites>("MXBMRP3_Test_AllocSites");
        m_audit = host.sym<PFN_Audit>("MXBMRP3_Test_HudPrimitiveAudit");
        m_ds[tape::EventType::EventInit]       = host.sym<PFN_Void_DS>("EventInit");
        m_ds[tape::EventType::RunInit]         = host.sym<PFN_Void_DS>("RunInit");
        m_ds[tape::EventType::RunLap]          = host.sym<PFN_Void_DS>("RunLap");
        m_ds[tape::EventType::RunSplit]        = host.sym<PFN_Void_DS>("RunSplit");
        m_ds[tape::EventType::RaceEvent]       = host.sym<PFN_Void_DS>("RaceEvent");
        m_ds[tape::EventType::RaceSession]     = host.sym<PFN_Void_DS>("RaceSession");
        m_ds[tape::EventType::RaceSessionState]= host.sym<PFN_Void_DS>("RaceSessionState");
        m_ds[tape::EventType::RaceAddEntry]    = host.sym<PFN_Void_DS>("RaceAddEntry");
        m_ds[tape::EventType::RaceRemoveEntry] = host.sym<PFN_Void_DS>("RaceRemoveEntry");
        m_ds[tape::EventType::RaceLap]         = host.sym<PFN_Void_DS>("RaceLap");
        m_ds[tape::EventType::RaceSplit]       = host.sym<PFN_Void_DS>("RaceSplit");
        m_ds[tape::EventType::RaceHoleshot]    = host.sym<PFN_Void_DS>("RaceHoleshot");
        m_ds[tape::EventType::RaceCommunication]= host.sym<PFN_Void_DS>("RaceCommunication");
        m_ds[tape::EventType::RaceVehicleData] = host.sym<PFN_Void_DS>("RaceVehicleData");
        m_void[tape::EventType::RunDeinit] = host.sym<PFN_Shutdown>("RunDeinit");
        m_void[tape::EventType::RunStart]  = host.sym<PFN_Shutdown>("RunStart");
        m_void[tape::EventType::RunStop]   = host.sym<PFN_Shutdown>("RunStop");
        m_classify = host.sym<PFN_Class>("RaceClassification");
        m_trackPos = host.sym<PFN_CountArray>("RaceTrackPosition");
        m_center   = host.sym<PFN_TrackCenter>("TrackCenterline");
        m_telem    = host.sym<PFN_Telemetry>("RunTelemetry");
    }
    bool ok() const { return m_draw && m_allocCount && m_allocStats; }
    void setOverlayOpen(bool on) { m_overlay = on && m_markClient; }
    // Rounded up to a whole second, so the replay's frame grid does not move
    // with Startup's length. What Startup schedules can still jitter by the few
    // microseconds of its worker waits (each iteration reads the clock); that
    // matters only if it straddles a frame, and the repeat check catches it.
    void setOrigin(uint64_t us) { m_origin = (us / 1000000 + 1) * 1000000; }

    // Load the whole tape up front: no file I/O inside the measured loop.
    bool load(const char* path) {
        FILE* f = fopen(path, "rb");
        if (!f) return false;
        tape::FileHeader fh{};
        if (fread(&fh, sizeof(fh), 1, f) != 1 || std::memcmp(fh.magic, "MXBHREC", 7) != 0) { fclose(f); return false; }
        tape::EventHeader eh{};
        while (fread(&eh, sizeof(eh), 1, f) == 1) {
            Event e{ static_cast<tape::EventType>(eh.eventType), eh.timestampUs, std::vector<uint8_t>(eh.dataSize) };
            if (eh.dataSize && fread(e.data.data(), 1, eh.dataSize, f) != eh.dataSize) break;
            m_events.push_back(std::move(e));
        }
        fclose(f);
        return !m_events.empty();
    }

    void run() {
        const double period = 1e6 / kFrameHz;
        const uint64_t t0 = m_events.front().us;
        double nextFrame = static_cast<double>(t0);
        for (Event& e : m_events) e.us += m_origin;   // onto the simulated timeline
        nextFrame += m_origin;
        m_allocCount(1);                          // arm on THIS (the game) thread
        if (m_sitesPath && m_sites) m_sites(1, 0, nullptr);
        for (Event& e : m_events) {
            while (nextFrame <= static_cast<double>(e.us)) {
                frame(static_cast<uint64_t>(nextFrame), t0 + m_origin, m_replay);
                nextFrame += period;
            }
            if (e.type == tape::EventType::Draw || e.type == tape::EventType::Startup ||
                e.type == tape::EventType::Shutdown || e.type == tape::EventType::DrawInit) continue;
            vclock::setUs(e.us);
            callback(e, e.us - t0 - m_origin >= kWarmupS * 1e6);
        }
        const double end = nextFrame + kIdleTailS * 1e6;
        while (nextFrame < end) {
            frame(static_cast<uint64_t>(nextFrame), t0 + m_origin, m_idle);
            nextFrame += period;
        }
        m_allocCount(0);
        if (m_sitesPath && m_sites) {
            m_sites(0, 0, nullptr);
            fprintf(stderr, "allocation sites: %d\n", m_sites(3, 0, m_sitesPath));
        }
    }

    void print(const char* profile) {
        printf("LEDGER profile=%s events=%zu frame_hz=%.0f warmup_s=%.0f idle_tail_s=%.0f\n",
               profile, m_events.size(), kFrameHz, kWarmupS, kIdleTailS);
        m_replay.print("replay");
        m_idle.print("idle");
        for (auto& kv : m_cb) {
            printf("COUNT cb.%s.calls %llu\n", kv.first.c_str(), kv.second.calls);
            printf("COUNT cb.%s.allocs %llu\n", kv.first.c_str(), kv.second.allocs);
            printf("COUNT cb.%s.alloc_bytes %llu\n", kv.first.c_str(), kv.second.bytes);
        }
        unsigned long long n = 0, b = 0; long long live = 0;
        m_allocStats(&n, &b, &live);
        printf("COUNT heap.net_live_bytes %lld\n", live);
        if (m_host.hasSnapshotSeq()) printf("COUNT http.snapshot_builds %llu\n", m_host.snapshotSeq());
        printf("COUNT frame_hash %016llx\n", m_hash);
        m_replay.draw.print("draw.replay");
        m_idle.draw.print("draw.idle");
        for (auto& kv : m_cb) kv.second.time.print(("cb." + kv.first).c_str());
    }

private:
    struct Cb { unsigned long long calls = 0, allocs = 0, bytes = 0; Timing time; };

    void callback(Event& e, bool steady) {
        unsigned long long n0, b0, n1, b1;
        if (m_sitesPath && m_sites) m_sites(2, static_cast<int>(e.type), nullptr);
        m_allocStats(&n0, &b0, nullptr);
        const double t = realUs();
        const bool known = dispatch(e);
        const double dt = realUs() - t;
        m_allocStats(&n1, &b1, nullptr);
        if (!known || !steady) return;
        const char* name = typeName(e.type);
        if (!name) return;
        Cb& c = m_cb[name];
        ++c.calls; c.allocs += n1 - n0; c.bytes += b1 - b0;
        c.time.add(dt);
    }

    void frame(uint64_t simUs, uint64_t t0, FrameTotals& tot) {
        vclock::setUs(simUs);
        // An open overlay polls /api/state about once a second.
        if (m_overlay && simUs >= m_nextPollUs) { m_markClient(); m_nextPollUs = simUs + 1000000; }
        int nq = 0, ns = 0; void* q = nullptr; void* s = nullptr;
        unsigned long long n0, b0, n1, b1;
        if (m_sitesPath && m_sites) m_sites(2, kDrawTag, nullptr);
        m_allocStats(&n0, &b0, nullptr);
        const double t = realUs();
        m_draw(m_onTrack ? 0 : 1, &nq, &q, &ns, &s);
        const double dt = realUs() - t;
        m_allocStats(&n1, &b1, nullptr);
        ++m_frameIndex;
        if (simUs - t0 < kWarmupS * 1e6) return;
        ++tot.frames;
        tot.draw.add(dt);
        const unsigned long long a = n1 - n0;
        tot.allocs += a; tot.allocBytes += b1 - b0;
        if (a) ++tot.allocFrames;
        tot.allocMax = std::max(tot.allocMax, a);
        unsigned long long glyphs = 0, sw = 0;
        const auto* quads = static_cast<const SPluginQuad_t*>(q);
        const auto* strs = static_cast<const SPluginString_t*>(s);
        for (int i = 1; i < nq; ++i) if (quads[i].m_iSprite != quads[i - 1].m_iSprite) ++sw;
        for (int i = 0; i < ns; ++i) glyphs += strnlen(strs[i].m_szString, sizeof(strs[i].m_szString));
        tot.quads += nq; tot.strings += ns; tot.glyphs += glyphs; tot.switches += sw;
        tot.quadsMax = std::max<unsigned long long>(tot.quadsMax, nq);
        tot.stringsMax = std::max<unsigned long long>(tot.stringsMax, ns);
        tot.engineUs += nq * kQuadUs + sw * kSwitchUs + glyphs * kGlyphUs;
        if (m_trace) {
            fprintf(m_trace, "%llu %d %d %llu %llu %llu %016llx\n", static_cast<unsigned long long>(simUs),
                    nq, ns, glyphs, sw, a, m_frameHash ? m_frameHash() : 0ULL);
            if (m_dumpFrame >= 0 && simUs >= static_cast<unsigned long long>(m_dumpFrame)) {
                m_dumpFrame = -1;   // the first frame at or after it, once
                char name[32];
                int vis, hq, a0, deg, hs, empty;
                for (int i = 0; m_audit && m_audit(i, name, sizeof(name), &vis, &hq, &a0, &deg, &hs, &empty); ++i)
                    if (vis) fprintf(m_trace, "  A %s quads=%d alpha0=%d degenerate=%d strings=%d empty=%d\n",
                                     name, hq, a0, deg, hs, empty);
                for (int i = 0; i < nq; ++i) {
                    const auto& p = quads[i].m_aafPos;
                    fprintf(m_trace, "  Q %d %08lx %.6f %.6f %.6f %.6f %.6f %.6f %.6f %.6f\n",
                        quads[i].m_iSprite, quads[i].m_ulColor,
                        p[0][0], p[0][1], p[1][0], p[1][1], p[2][0], p[2][1], p[3][0], p[3][1]);
                }
                for (int i = 0; i < ns; ++i) fprintf(m_trace, "  S %08lx %.6f %.6f %.*s\n", strs[i].m_ulColor,
                    strs[i].m_afPos[0], strs[i].m_afPos[1],
                    static_cast<int>(sizeof(strs[i].m_szString)), strs[i].m_szString);
            }
        }
        if (m_frameHash && m_frameIndex % kHashEvery == 0) {
            m_hash = (m_hash ^ m_frameHash()) * 1099511628211ULL;
        }
    }

    bool dispatch(Event& e) {
        void* p = e.data.data();
        const int size = static_cast<int>(e.data.size());
        switch (e.type) {
            case tape::EventType::RunStart: m_onTrack = true; break;
            case tape::EventType::RunStop:  m_onTrack = false; break;
            default: break;
        }
        auto ds = m_ds.find(e.type);
        if (ds != m_ds.end()) { if (!ds->second) return false; ds->second(p, size); return true; }
        auto v = m_void.find(e.type);
        if (v != m_void.end()) { if (!v->second) return false; v->second(); return true; }
        switch (e.type) {
            case tape::EventType::RaceClassification: {
                if (!m_classify || e.data.size() < sizeof(tape::ClassificationPrefix)) return false;
                auto* pre = reinterpret_cast<tape::ClassificationPrefix*>(p);
                m_classify(&pre->header, static_cast<int>(sizeof(SPluginsRaceClassification_t)),
                           e.data.data() + sizeof(tape::ClassificationPrefix),
                           static_cast<int>(sizeof(SPluginsRaceClassificationEntry_t)));
                return true;
            }
            case tape::EventType::RaceTrackPosition: {
                if (!m_trackPos || e.data.size() < sizeof(tape::TrackPositionPrefix)) return false;
                auto* pre = reinterpret_cast<tape::TrackPositionPrefix*>(p);
                m_trackPos(pre->numVehicles, e.data.data() + sizeof(tape::TrackPositionPrefix),
                           static_cast<int>(sizeof(SPluginsRaceTrackPosition_t)));
                return true;
            }
            case tape::EventType::TrackCenterline: {
                if (!m_center || e.data.size() < sizeof(int)) return false;
                const int n = *reinterpret_cast<int*>(p);
                m_center(n, e.data.size() > sizeof(int) ? e.data.data() + sizeof(int) : nullptr, nullptr);
                return true;
            }
            case tape::EventType::RunTelemetry: {
                const size_t need = sizeof(SPluginsBikeData_t) + 2 * sizeof(float);
                if (!m_telem || e.data.size() < need) return false;
                float tm, pos;
                std::memcpy(&tm, e.data.data() + sizeof(SPluginsBikeData_t), sizeof(float));
                std::memcpy(&pos, e.data.data() + sizeof(SPluginsBikeData_t) + sizeof(float), sizeof(float));
                m_telem(p, static_cast<int>(sizeof(SPluginsBikeData_t)), tm, pos);
                return true;
            }
            default: return false;
        }
    }

    PluginHost& m_host;
    PFN_Draw m_draw = nullptr;
    PFN_AllocCount m_allocCount = nullptr;
    PFN_AllocStats m_allocStats = nullptr;
    PFN_AllocSites m_sites = nullptr;
    PFN_Audit m_audit = nullptr;
    unsigned long long (*m_frameHash)() = nullptr;
    void (*m_markClient)() = nullptr;
    std::map<tape::EventType, PFN_Void_DS> m_ds;
    std::map<tape::EventType, PFN_Shutdown> m_void;
    PFN_Class m_classify = nullptr;
    PFN_CountArray m_trackPos = nullptr;
    PFN_TrackCenter m_center = nullptr;
    PFN_Telemetry m_telem = nullptr;
    std::vector<Event> m_events;
    std::map<std::string, Cb> m_cb;
    FrameTotals m_replay, m_idle;
    unsigned long long m_hash = 1469598103934665603ULL;
    unsigned long long m_frameIndex = 0;
    uint64_t m_nextPollUs = 0;
    uint64_t m_origin = 0;
    bool m_onTrack = false;
    bool m_overlay = false;
public:
    long long m_dumpFrame = -1;  // MXB_LEDGER_DUMP=<sim us>: also write the first frame's at or after it
    FILE* m_trace = nullptr;     // MXB_LEDGER_TRACE: one line per frame, to bisect a counter that drifts
    const char* m_sitesPath = nullptr;  // MXB_LEDGER_SITES=<file>: write allocation sites there
};

const char* dllBaseName(const char* path) {
    const char* b = path;
    for (const char* c = path; *c; ++c) if (*c == '\\' || *c == '/') b = c + 1;
    return b;
}

}  // namespace

int main(int argc, char** argv) {
    QueryPerformanceFrequency(&g_freq);
    const char* dll = argc > 1 ? argv[1] : "mxbmrp3_test.dlo";
    const char* tapePath = argc > 2 ? argv[2] : "Z:\\tmp\\perf_busy.tape";
    const char* profile = argc > 3 ? argv[3] : "all";
    // `hudtime`: the per-HUD pass on the REAL clock, for the time each HUD
    // costs - under the virtual one every interval the plugin measures is 0.
    // Its counters are host-dependent, so perf_ledger.py reads only its times.
    const bool realClock = argc > 4 && std::strcmp(argv[4], "hudtime") == 0;
    const bool hudPass = realClock || (argc > 4 && std::strcmp(argv[4], "hud") == 0);
    const bool all = std::strcmp(profile, "all") == 0;
    if (!all && std::strcmp(profile, "defaults") != 0) { fprintf(stderr, "profile must be all|defaults\n"); return 2; }

    PluginHost host(dll);
    if (!host.loaded()) { fprintf(stderr, "failed to load %s\n", dll); return 1; }
    Ledger ledger(host);
    if (!ledger.ok()) { fprintf(stderr, "missing perf hooks (rebuild the DLL)\n"); return 2; }
    if (!ledger.load(tapePath)) { fprintf(stderr, "cannot read tape %s\n", tapePath); return 3; }

    // Patch the clock BEFORE Startup, stepping per read until frames are
    // pumped (see vclock::stepOnThisThread), so every timestamp the plugin
    // keeps lies on the simulated timeline.
    const int patched = realClock ? 3 : vclock::install(GetModuleHandleA(dllBaseName(dll)));
    if (patched < 3) { fprintf(stderr, "virtual clock: only %d imports patched\n", patched); return 4; }
    vclock::stepOnThisThread(true);

    host.startup("Z:\\tmp\\mxbledger\\");
    if (auto drop = host.sym<void(*)(int)>("MXBMRP3_Test_SetDropShadow")) drop(1);
    if (auto glow = host.sym<void(*)(int, float, float)>("MXBMRP3_Test_SetGlow")) glow(1, 1.0f, 1.0f);
    if (all) {
        if (auto theme = host.sym<void(*)(const char*, float, float, int, int, int, int)>("MXBMRP3_Test_InstallTheme"))
            theme("perf", 3.0f, 1.0f, 1, 1, 1, 1);
        if (auto show = host.sym<void(*)(int)>("MXBMRP3_Test_ShowAllHuds")) show(1);
        if (auto max = host.sym<void(*)()>("MXBMRP3_Test_MaxHudSettings")) max();
        if (host.startHttpNoClient()) ledger.setOverlayOpen(true);
    }
    auto bench = host.sym<void(*)(int)>("MXBMRP3_Test_BenchmarkWidget");
    if (hudPass && bench) bench(1);

    vclock::stepOnThisThread(false);
    // The replay's simulated time starts after Startup's: the tape's first
    // timestamp is 0, and setUs() never moves backwards.
    ledger.setOrigin(vclock::nowUs());
    if (const char* tr = getenv("MXB_LEDGER_TRACE")) ledger.m_trace = fopen(tr, "w");
    if (const char* d = getenv("MXB_LEDGER_DUMP")) ledger.m_dumpFrame = atoll(d);
    if (const char* sp = getenv("MXB_LEDGER_SITES")) ledger.m_sitesPath = *sp ? sp : nullptr;
    // MXB_LEDGER_PROFILE=<file>: sample where the replay spends CPU (perf_sampler.h).
    const char* prof = getenv("MXB_LEDGER_PROFILE");
    if (prof && *prof && !perf_sampler::start(GetModuleHandleA(dllBaseName(dll)))) prof = nullptr;
    ledger.run();
    if (prof && *prof) perf_sampler::stopAndWrite(prof);
    if (ledger.m_trace) fclose(ledger.m_trace);
    ledger.print(profile);

    if (hudPass) {
        auto row = host.sym<PFN_HudRow>("MXBMRP3_Test_BenchmarkHudRow");
        char name[32];
        int rebuilds = 0, quads = 0, strings = 0;
        auto times = host.sym<int(*)(int, long long*, long long*)>("MXBMRP3_Test_BenchmarkHudTime");
        for (int i = 0; row && row(i, name, sizeof(name), &rebuilds, &quads, &strings); ++i) {
            printf("HUD %s rebuilds=%d quads=%d strings=%d\n", name, rebuilds, quads, strings);
            long long rebuildUs = 0, updateUs = 0;
            if (times && times(i, &rebuildUs, &updateUs))
                printf("HUDTIME %s rebuild_us=%lld update_us=%lld\n", name, rebuildUs, updateUs);
        }
        if (bench) bench(0);
    }
    vclock::release();
    host.shutdown();
    return 0;
}
