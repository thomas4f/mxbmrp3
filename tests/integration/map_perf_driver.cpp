// ============================================================================
// tests/integration/map_perf_driver.cpp
// Targeted CPU perf probe for the MAP HUD's per-frame rebuild.
//
// The general perf_driver.cpp calls Draw() in a tight loop with NO intervening
// callbacks, so any HUD that gates its rebuild on isDataDirty() is only rebuilt
// on the FIRST Draw and then serves cached primitives. That hides the map's real
// cost, because the map re-dirties itself on every rider-position update.
//
// This driver instead drives the REALISTIC hot loop: each "frame" it mutates the
// 50-rider positions, fires RaceTrackPosition (which marks the map dirty), then
// calls Draw (which rebuilds it). It sweeps map scenarios via the MXBMRP3_Test_Map*
// hooks and reports Draw cost for each, so the map's per-frame rebuild — and the
// effect of its ribbon-quad cache — is isolated as a delta over a map-off baseline.
//
//   x86_64-w64-mingw32-g++ -std=c++17 -O2 map_perf_driver.cpp -o map_perf_driver.exe
//   wine map_perf_driver.exe mxbmrp3_test.dlo
// ============================================================================
#include <windows.h>
#include <cstdio>
#include <cstdint>
#include <cstring>
#include <cstdlib>

// --- plugin API structs (match perf_driver.cpp / vendor/piboso/mxb_api.h) ---
struct SPluginsBikeEvent_t {
    char m_szRiderName[100]; char m_szBikeID[100]; char m_szBikeName[100];
    int a,b,c,d; float e; float f[2]; float g; float h[2]; float i;
    char m_szCategory[100]; char m_szTrackID[100]; char m_szTrackName[100];
    float m_fTrackLength; int m_iType; char m_szServerName[64]; int m_iServerType; char m_szGUID[100];
};
struct SPluginsRaceEvent_t { int m_iType; char m_szName[100]; char m_szTrackName[100]; float m_fTrackLength; };
struct SPluginsRaceSession_t { int m_iSession, m_iSessionState, m_iSessionLength, m_iSessionNumLaps, m_iConditions; float m_fAir; };
struct SPluginsRaceAddEntry_t {
    int m_iRaceNum; char m_szName[100], m_szBikeName[100], m_szBikeShortName[100], m_szCategory[100];
    int m_iUnactive, m_iNumberOfGears, m_iMaxRPM;
};
struct SPluginsRaceClassification_t { int m_iSession, m_iSessionState, m_iSessionTime, m_iNumEntries; };
struct SPluginsRaceClassificationEntry_t {
    int m_iRaceNum, m_iState, m_iBestLap, m_iBestLapNum, m_iNumLaps, m_iGap, m_iGapLaps, m_iPenalty, m_iPit;
};
struct SPluginsRaceTrackPosition_t { int m_iRaceNum; float m_fPosX, m_fPosY, m_fPosZ, m_fYaw, m_fTrackPos; int m_iCrashed; };
struct SPluginsTrackSegment_t { int m_iType; float m_fLength, m_fRadius, m_fAngle; float m_afStart[2]; float m_fHeight; };

typedef int  (*PFN_Startup)(char*);
typedef void (*PFN_Shutdown)();
typedef void (*PFN_DS)(void*, int);
typedef void (*PFN_Class)(void*, int, void*, int);
typedef void (*PFN_TrackPos)(int, void*, int);
typedef void (*PFN_Draw)(int, int*, void**, int*, void**);
typedef void (*PFN_TrackCenter)(int, void*, void*);
typedef void (*PFN_MapI)(int);
typedef long long (*PFN_MapProfile)(double*, double*, double*, double*, long long*, long long*);
static PFN_MapProfile MapProfile = nullptr;

static LARGE_INTEGER g_freq;
static uint64_t nowUs() { LARGE_INTEGER t; QueryPerformanceCounter(&t); return (uint64_t)(t.QuadPart * 1000000.0 / g_freq.QuadPart); }

static const int RIDERS = 50;
static const double BUDGET_US = 4170.0;

struct Stat {
    double* us; int n, cap;
    void init(int c) { cap = c; n = 0; us = (double*)malloc(sizeof(double)*c); }
    void add(double t) { if (n < cap) us[n++] = t; }
};
static int cmp(const void* a, const void* b) { double x=*(const double*)a,y=*(const double*)b; return x<y?-1:x>y?1:0; }
static double pct(Stat& s, double p) { if (!s.n) return 0; int i=(int)(p*(s.n-1)); return s.us[i]; }
static double avg(Stat& s) { double t=0; for (int i=0;i<s.n;++i) t+=s.us[i]; return s.n?t/s.n:0; }

static PFN_Draw Draw;
static PFN_TrackPos RaceTrackPosition;
static SPluginsRaceTrackPosition_t* g_pos;

// Run the realistic frame loop: mutate positions -> RaceTrackPosition (dirties
// the map) -> Draw (rebuilds). Time only the Draw. Returns sorted stats.
static void runScenario(Stat& out, int frames) {
    out.init(frames);
    for (int i = 0; i < frames; ++i) {
        for (int r = 0; r < RIDERS; ++r) {
            g_pos[r].m_fTrackPos = (float)((i + r) % 1000) / 1000.0f;
            g_pos[r].m_fPosX = (float)((i * 3 + r * 7) % 1600);
            g_pos[r].m_fPosZ = (float)((i * 5 + r * 11) % 1600);
            g_pos[r].m_fYaw = (float)((i + r * 7) % 360);
        }
        RaceTrackPosition(RIDERS, g_pos, (int)sizeof(g_pos[0]));
        int nq, ns; void *q, *s;
        uint64_t t0 = nowUs();
        Draw(0, &nq, &q, &ns, &s);
        out.add((double)(nowUs() - t0));
    }
    qsort(out.us, out.n, sizeof(double), cmp);
}

static void report(const char* name, Stat& s, double baseAvg) {
    double a = avg(s);
    printf("%-30s %8.1f %8.1f %8.1f %9.1f", name, a, pct(s,0.50), pct(s,0.99), pct(s,1.0));
    if (baseAvg >= 0) printf("  %+8.1f  (%.1f%% budget)", a - baseAvg, 100.0*(a-baseAvg)/BUDGET_US);
    printf("\n");
}

// Read the accumulated per-phase profile (µs total) and print per-rebuild avg.
static void reportProfile(const char* name) {
    if (!MapProfile) return;
    double b=0,r=0,m=0,ri=0; long long hits=0,miss=0;
    long long c = MapProfile(&b,&r,&m,&ri,&hits,&miss);
    if (c <= 0) return;
    printf("    %-26s bounds %6.1f  ribbon %6.1f  markers %6.1f  riders %6.1f   ribbon-cache: %lld hit / %lld miss\n",
        name, b/c, r/c, m/c, ri/c, hits, miss);
}

int main(int argc, char** argv) {
    const char* dll = (argc > 1) ? argv[1] : "mxbmrp3_test.dlo";
    QueryPerformanceFrequency(&g_freq);
    HMODULE h = LoadLibraryA(dll);
    if (!h) { printf("FAIL: LoadLibrary %lu\n", GetLastError()); return 2; }
    auto S = [&](const char* n){ return GetProcAddress(h, n); };
    auto Startup=(PFN_Startup)S("Startup"); auto Shutdown=(PFN_Shutdown)S("Shutdown");
    auto EventInit=(PFN_DS)S("EventInit"); auto RaceEvent=(PFN_DS)S("RaceEvent");
    auto RaceSession=(PFN_DS)S("RaceSession"); auto RaceAddEntry=(PFN_DS)S("RaceAddEntry");
    auto RaceClassification=(PFN_Class)S("RaceClassification");
    RaceTrackPosition=(PFN_TrackPos)S("RaceTrackPosition");
    auto TrackCenterline=(PFN_TrackCenter)S("TrackCenterline");
    Draw=(PFN_Draw)S("Draw");
    auto MapVisible=(PFN_MapI)S("MXBMRP3_Test_MapSetVisible");
    auto MapRotate=(PFN_MapI)S("MXBMRP3_Test_MapSetRotate");
    auto MapZoom=(PFN_MapI)S("MXBMRP3_Test_MapSetZoom");
    auto MapDetail=(PFN_MapI)S("MXBMRP3_Test_MapSetDetail");
    MapProfile=(PFN_MapProfile)S("MXBMRP3_Test_MapProfile");
    auto resetProf=[&](){ if(MapProfile){ double d; long long l; MapProfile(&d,&d,&d,&d,&l,&l);} };
    if (!Startup || !Draw || !RaceTrackPosition) { printf("FAIL: missing core exports\n"); return 2; }
    if (!MapVisible || !MapRotate || !MapZoom || !MapDetail) { printf("FAIL: missing map hooks (rebuild the DLL)\n"); return 2; }

    char savePath[] = "Z:\\tmp\\mxbperf\\";
    Startup(savePath);

    SPluginsBikeEvent_t ev{}; strcpy(ev.m_szRiderName,"Player"); strcpy(ev.m_szBikeName,"Test 450");
    strcpy(ev.m_szCategory,"MX1"); strcpy(ev.m_szTrackName,"PerfTrack"); ev.m_fTrackLength=1600.0f; ev.m_iType=2;
    EventInit(&ev,(int)sizeof(ev));
    SPluginsRaceEvent_t re{}; re.m_iType=2; strcpy(re.m_szName,"PerfTrack"); strcpy(re.m_szTrackName,"PerfTrack"); re.m_fTrackLength=1600.0f;
    if (RaceEvent) RaceEvent(&re,(int)sizeof(re));
    SPluginsRaceSession_t ss{}; ss.m_iSession=6; ss.m_iSessionState=16; ss.m_iSessionLength=480000; ss.m_iSessionNumLaps=2;
    if (RaceSession) RaceSession(&ss,(int)sizeof(ss));

    for (int i=0;i<RIDERS;++i){ SPluginsRaceAddEntry_t e{}; e.m_iRaceNum=i+1;
        snprintf(e.m_szName,100,"Rider %02d",i+1); strcpy(e.m_szBikeName,"Test 450"); strcpy(e.m_szBikeShortName,"T450");
        strcpy(e.m_szCategory,"MX1"); e.m_iNumberOfGears=5; e.m_iMaxRPM=13000; RaceAddEntry(&e,(int)sizeof(e)); }

    // A real 2D loop: a ~254m-radius circle of 256 CURVE segments (type != 0), so
    // heading actually changes and the track has non-degenerate width AND height.
    // (The old version used straight segments with a varying m_fAngle, but bounds
    // integration only turns on curve segments, so it collapsed to a 1D line ->
    // zero X-width -> divide-by-zero in worldToScreen -> NaN offset -> the ribbon
    // cache could never compare equal. That was a synthetic-track artifact.)
    const int SEGS=256; SPluginsTrackSegment_t* segs=(SPluginsTrackSegment_t*)calloc(SEGS,sizeof(SPluginsTrackSegment_t));
    const float TRACK_LEN=1600.0f, RADIUS=TRACK_LEN/(2.0f*3.14159265f);
    for (int i=0;i<SEGS;++i){ segs[i].m_iType=1; segs[i].m_fLength=TRACK_LEN/SEGS; segs[i].m_fRadius=RADIUS; segs[i].m_fAngle=0.0f; }
    float raceData[4]={800.0f,400.0f,1200.0f,0.0f};
    if (TrackCenterline) TrackCenterline(SEGS,segs,raceData);

    struct { SPluginsRaceClassification_t hdr; SPluginsRaceClassificationEntry_t e[64]; } cls{};
    cls.hdr.m_iSession=6; cls.hdr.m_iSessionState=16; cls.hdr.m_iSessionTime=120000; cls.hdr.m_iNumEntries=RIDERS;
    for (int i=0;i<RIDERS;++i){ cls.e[i].m_iRaceNum=i+1; cls.e[i].m_iBestLap=90000+i*350; cls.e[i].m_iNumLaps=3; cls.e[i].m_iGap=i*450; }
    RaceClassification(&cls.hdr,(int)sizeof(cls.hdr),cls.e,(int)sizeof(cls.e[0]));

    g_pos=(SPluginsRaceTrackPosition_t*)calloc(RIDERS,sizeof(SPluginsRaceTrackPosition_t));
    for (int i=0;i<RIDERS;++i){ g_pos[i].m_iRaceNum=i+1; }

    const int FRAMES = 12000;

    printf("\n=== MXBMRP3 MAP HUD perf probe (50 riders, interleaved TrackPos+Draw, headless/Wine) ===\n");
    printf("Realistic hot loop: each frame mutates positions, fires RaceTrackPosition (dirties map), then Draw.\n");
    printf("%d frames per scenario. Delta column = Draw avg over the map-off baseline.\n\n", FRAMES);
    printf("%-30s %8s %8s %8s %9s  %9s\n","scenario (Draw us)","avg","p50","p99","max","vs map-off");
    printf("%-30s %8s %8s %8s %9s  %9s\n","------------------","---","---","---","---","----------");

    // Warmup so lazy first-frame allocations don't skew the first scenario.
    { Stat warm; runScenario(warm, 2000); free(warm.us); }

    // Baseline: map OFF (all other default-on HUDs still rebuild each frame).
    MapVisible(0);
    Stat off; runScenario(off, FRAMES);
    double baseAvg = avg(off);
    report("map OFF (baseline)", off, -1);

    // Map ON, default view (no rotate/zoom): ribbon-quad cache SHOULD hit every
    // frame; the residual cost is the per-rebuild bounds traversals + riders.
    MapVisible(1); MapRotate(0); MapZoom(0); MapDetail(0);
    resetProf(); Stat def; runScenario(def, FRAMES);
    report("map ON, default (AUTO)", def, baseAvg); reportProfile("default");

    // Same, forced HIGH detail (1.0m subdivision) and LOW (4.0m) — only affects
    // the ribbon tessellation, which is cached in default view, so should ~match.
    MapDetail(1); resetProf(); Stat hi; runScenario(hi, FRAMES); report("map ON, default HIGH detail", hi, baseAvg); reportProfile("HIGH detail");
    MapDetail(2); resetProf(); Stat lo; runScenario(lo, FRAMES); report("map ON, default LOW detail", lo, baseAvg); reportProfile("LOW detail");
    MapDetail(0);

    // Rotate-to-player: the ribbon cache degrades to a pass-through (the key
    // changes every frame), so the whole centerline re-tessellates each rebuild.
    MapRotate(1); MapZoom(0);
    resetProf(); Stat rot; runScenario(rot, FRAMES);
    report("map ON, rotate-to-player", rot, baseAvg); reportProfile("rotate");
    MapRotate(0);

    // Zoom: same cache defeat, plus per-frame zoom-bounds recompute.
    MapZoom(1); MapRotate(0);
    resetProf(); Stat zoom; runScenario(zoom, FRAMES);
    report("map ON, zoom", zoom, baseAvg); reportProfile("zoom");

    // Rotate + zoom together (worst case).
    MapRotate(1);
    resetProf(); Stat both; runScenario(both, FRAMES);
    report("map ON, rotate+zoom", both, baseAvg); reportProfile("rotate+zoom");
    MapRotate(0); MapZoom(0);

    printf("\nBudget = %.0f us/frame (240fps). Baseline map-off Draw avg = %.1f us.\n", BUDGET_US, baseAvg);
    printf("Machine-readable:\n");
    printf("MAPPERF off=%.1f default=%.1f rotate=%.1f zoom=%.1f both=%.1f\n",
        baseAvg, avg(def), avg(rot), avg(zoom), avg(both));
    fflush(stdout);
    if (Shutdown) Shutdown();
    return 0;
}
