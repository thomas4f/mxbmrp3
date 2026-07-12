// ============================================================================
// tests/integration/perf_driver.cpp
// CPU performance baseline for the plugin's hot paths. The plugin is CPU-bound
// (it builds render primitives and processes callbacks on the CPU; the GPU only
// consumes the quads/strings it emits), so headless timing under Wine is
// representative of the cost the plugin actually controls — measured against the
// 240fps frame budget (4.17ms).
//
// Sets up a FULL 50-rider race (MAX_RACE_ENTRIES) with a populated track and
// positions, then times the hot callbacks: Draw (per-frame render build),
// RaceTrackPosition (high-frequency, full grid), RaceClassification (standings
// rebuild), RunTelemetry (100Hz player). Reports per-call avg/p50/p99/max and a
// projection to a full warmup+race session.
//
// Caveats (printed in the report): absolute numbers include Wine overhead and
// vary with host CPU; use for relative cost, hot-path ID, and regression, not as
// exact Windows figures.
//
//   x86_64-w64-mingw32-g++ perf_driver.cpp -o perf_driver.exe
//   wine perf_driver.exe mxbmrp3_test.dlo
// ============================================================================
#include <windows.h>
#include <cstdio>
#include <cstdint>
#include <cstring>
#include <cstdlib>

// --- plugin API structs (match vendor/piboso/mxb_api.h; default alignment) ---
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
struct SPluginsBikeData_t {
    int m_iRPM; float m_fEngineT, m_fWaterT; int m_iGear; float m_fFuel, m_fSpeed;
    float m_fPosX, m_fPosY, m_fPosZ, m_fVelX, m_fVelY, m_fVelZ, m_fAccX, m_fAccY, m_fAccZ;
    float m_aafRot[3][3]; float m_fYaw, m_fPitch, m_fRoll, m_fYawV, m_fPitchV, m_fRollV;
    float m_afSuspLen[2], m_afSuspVel[2]; int m_iCrashed; float m_fSteer, m_fThrottle, m_fFrontBrake, m_fRearBrake, m_fClutch;
    float m_afWheelSpeed[2]; int m_aiWheelMat[2]; float m_afBrakeP[2]; float m_fSteerTorque;
};

typedef int  (*PFN_Startup)(char*);
typedef void (*PFN_Shutdown)();
typedef void (*PFN_DS)(void*, int);
typedef void (*PFN_Telem)(void*, int, float, float);
typedef void (*PFN_Class)(void*, int, void*, int);
typedef void (*PFN_TrackPos)(int, void*, int);
typedef void (*PFN_Draw)(int, int*, void**, int*, void**);
typedef void (*PFN_TrackCenter)(int, void*, void*);

static LARGE_INTEGER g_freq;
static uint64_t nowUs() { LARGE_INTEGER t; QueryPerformanceCounter(&t); return (uint64_t)(t.QuadPart * 1000000.0 / g_freq.QuadPart); }

// Per-callback timing: keep all samples so we can report percentiles.
struct Stat {
    const char* name; double* us; int n, cap;
    void init(const char* nm, int c) { name = nm; cap = c; n = 0; us = (double*)malloc(sizeof(double)*c); }
    void add(double t) { if (n < cap) us[n++] = t; }
};
static int cmp(const void* a, const void* b) { double x=*(const double*)a,y=*(const double*)b; return x<y?-1:x>y?1:0; }
static double pct(Stat& s, double p) { if (!s.n) return 0; int i=(int)(p*(s.n-1)); return s.us[i]; }
static double avg(Stat& s) { double t=0; for (int i=0;i<s.n;++i) t+=s.us[i]; return s.n?t/s.n:0; }

static const int RIDERS = 50;
static const double BUDGET_US = 4170.0;  // 240 fps frame budget

int main(int argc, char** argv) {
    const char* dll = (argc > 1) ? argv[1] : "mxbmrp3_test.dlo";
    QueryPerformanceFrequency(&g_freq);

    HMODULE h = LoadLibraryA(dll);
    if (!h) { printf("FAIL: LoadLibrary %lu\n", GetLastError()); return 2; }
    auto S = [&](const char* n){ return GetProcAddress(h, n); };
    auto Startup=(PFN_Startup)S("Startup"); auto Shutdown=(PFN_Shutdown)S("Shutdown");
    auto EventInit=(PFN_DS)S("EventInit"); auto RaceEvent=(PFN_DS)S("RaceEvent");
    auto RaceSession=(PFN_DS)S("RaceSession"); auto RaceAddEntry=(PFN_DS)S("RaceAddEntry");
    auto RaceClassification=(PFN_Class)S("RaceClassification"); auto RaceTrackPosition=(PFN_TrackPos)S("RaceTrackPosition");
    auto RunTelemetry=(PFN_Telem)S("RunTelemetry"); auto TrackCenterline=(PFN_TrackCenter)S("TrackCenterline");
    auto Draw=(PFN_Draw)S("Draw");
    if (!Startup || !Draw) { printf("FAIL: missing exports\n"); return 2; }

    char savePath[] = "Z:\\tmp\\mxbperf\\";
    Startup(savePath);

    // --- Populate a full 50-rider race on a real track ----------------------
    SPluginsBikeEvent_t ev{}; strcpy(ev.m_szRiderName,"Player"); strcpy(ev.m_szBikeName,"Test 450");
    strcpy(ev.m_szCategory,"MX1"); strcpy(ev.m_szTrackName,"PerfTrack"); ev.m_fTrackLength=1600.0f; ev.m_iType=2;
    EventInit(&ev,(int)sizeof(ev));
    SPluginsRaceEvent_t re{}; re.m_iType=2; strcpy(re.m_szName,"PerfTrack"); strcpy(re.m_szTrackName,"PerfTrack"); re.m_fTrackLength=1600.0f;
    if (RaceEvent) RaceEvent(&re,(int)sizeof(re));
    // Warmup session (5 min) then race is simulated by re-sending session; here race1.
    SPluginsRaceSession_t ss{}; ss.m_iSession=6; ss.m_iSessionState=16; ss.m_iSessionLength=480000; ss.m_iSessionNumLaps=2;
    if (RaceSession) RaceSession(&ss,(int)sizeof(ss));

    for (int i=0;i<RIDERS;++i){ SPluginsRaceAddEntry_t e{}; e.m_iRaceNum=i+1;
        snprintf(e.m_szName,100,"Rider %02d",i+1); strcpy(e.m_szBikeName,"Test 450"); strcpy(e.m_szBikeShortName,"T450");
        strcpy(e.m_szCategory,"MX1"); e.m_iNumberOfGears=5; e.m_iMaxRPM=13000; RaceAddEntry(&e,(int)sizeof(e)); }

    // A valid ~256-segment loop so the map HUD has real geometry to render.
    const int SEGS=256; SPluginsTrackSegment_t* segs=(SPluginsTrackSegment_t*)calloc(SEGS,sizeof(SPluginsTrackSegment_t));
    for (int i=0;i<SEGS;++i){ segs[i].m_iType=0; segs[i].m_fLength=1600.0f/SEGS; segs[i].m_fAngle=360.0f*i/SEGS; }
    float raceData[4]={800.0f,400.0f,1200.0f,0.0f};
    if (TrackCenterline) TrackCenterline(SEGS,segs,raceData);

    // Classification (standings) + positions (map) for all 50.
    struct { SPluginsRaceClassification_t hdr; SPluginsRaceClassificationEntry_t e[64]; } cls{};
    cls.hdr.m_iSession=6; cls.hdr.m_iSessionState=16; cls.hdr.m_iSessionTime=120000; cls.hdr.m_iNumEntries=RIDERS;
    for (int i=0;i<RIDERS;++i){ cls.e[i].m_iRaceNum=i+1; cls.e[i].m_iBestLap=90000+i*350; cls.e[i].m_iNumLaps=3; cls.e[i].m_iGap=i*450; }
    RaceClassification(&cls.hdr,(int)sizeof(cls.hdr),cls.e,(int)sizeof(cls.e[0]));

    SPluginsRaceTrackPosition_t* pos=(SPluginsRaceTrackPosition_t*)calloc(RIDERS,sizeof(SPluginsRaceTrackPosition_t));
    for (int i=0;i<RIDERS;++i){ pos[i].m_iRaceNum=i+1; pos[i].m_fTrackPos=(float)i/RIDERS; pos[i].m_fPosX=(float)i; pos[i].m_fYaw=(float)(i*7%360); }
    RaceTrackPosition(RIDERS,pos,(int)sizeof(pos[0]));

    SPluginsBikeData_t bd{}; bd.m_iRPM=9000; bd.m_iGear=4; bd.m_fSpeed=28.0f; bd.m_fFuel=3.5f; bd.m_fRoll=25.0f;
    bd.m_afSuspLen[0]=0.12f; bd.m_afSuspLen[1]=0.14f;

    // --- Measure hot paths --------------------------------------------------
    Stat draw, tpos, cla, telem;
    draw.init("Draw (frame build, 50 riders)", 30000);
    tpos.init("RaceTrackPosition (50)", 15000);
    cla.init("RaceClassification (50)", 4000);
    telem.init("RunTelemetry (100Hz)", 30000);

    for (int i=0;i<30000;++i){ int nq,ns; void*q; void*s; uint64_t t0=nowUs(); Draw(0,&nq,&q,&ns,&s); draw.add((double)(nowUs()-t0)); }
    for (int i=0;i<15000;++i){ for(int r=0;r<RIDERS;++r) pos[r].m_fTrackPos=(float)((i+r)%1000)/1000.0f;
        uint64_t t0=nowUs(); RaceTrackPosition(RIDERS,pos,(int)sizeof(pos[0])); tpos.add((double)(nowUs()-t0)); }
    for (int i=0;i<4000;++i){ for(int r=0;r<RIDERS;++r) cls.e[r].m_iGap=(r*450+i)%60000;
        uint64_t t0=nowUs(); RaceClassification(&cls.hdr,(int)sizeof(cls.hdr),cls.e,(int)sizeof(cls.e[0])); cla.add((double)(nowUs()-t0)); }
    if (RunTelemetry) for (int i=0;i<30000;++i){ bd.m_fRoll=(float)((i%90)-45);
        uint64_t t0=nowUs(); RunTelemetry(&bd,(int)sizeof(bd),(float)i*0.01f,(float)(i%1000)/1000.0f); telem.add((double)(nowUs()-t0)); }

    Stat* all[4]={&draw,&tpos,&cla,&telem};
    for (auto* s: all) qsort(s->us,s->n,sizeof(double),cmp);

    printf("\n=== MXBMRP3 CPU perf baseline (50 riders, headless/Wine) ===\n");
    printf("%-34s %8s %8s %8s %8s %9s\n","callback","n","avg us","p50 us","p99 us","max us");
    printf("%-34s %8s %8s %8s %8s %9s\n","--------","-","------","------","------","------");
    for (auto* s: all) printf("%-34s %8d %8.1f %8.1f %8.1f %9.1f\n",
        s->name,s->n,avg(*s),pct(*s,0.50),pct(*s,0.99),pct(*s,1.0));

    double dAvg=avg(draw), dP99=pct(draw,0.99);
    printf("\nDraw() vs 240fps budget (%.0f us/frame): avg %.1f%%  p99 %.1f%%\n",
        BUDGET_US, 100.0*dAvg/BUDGET_US, 100.0*dP99/BUDGET_US);

    // Projection to a session: 5min warmup + (8min + ~2 laps) race ~= 960s.
    // Realistic rates: Draw 240Hz, RaceTrackPosition 30Hz, RunTelemetry 100Hz,
    // RaceClassification 5Hz.
    double sessionS=960.0;
    double drawCpu = 240.0*sessionS*dAvg/1e6;
    double tposCpu = 30.0*sessionS*avg(tpos)/1e6;
    double telemCpu= 100.0*sessionS*avg(telem)/1e6;
    double claCpu  = 5.0*sessionS*avg(cla)/1e6;
    double total = drawCpu+tposCpu+telemCpu+claCpu;
    printf("\nProjected plugin CPU over a ~%.0fs session (warmup+race):\n", sessionS);
    printf("  Draw %.2fs  TrackPos %.2fs  Telemetry %.2fs  Classification %.2fs\n", drawCpu,tposCpu,telemCpu,claCpu);
    printf("  total %.2fs = %.2f%% of one CPU core over the session\n", total, 100.0*total/sessionS);
    printf("\nNOTE: includes Wine overhead and varies with host CPU. Use for relative\n");
    printf("cost, hot-path identification, and regression detection - not exact\n");
    printf("Windows figures. Baseline captured on the CI/dev host.\n");

    // Machine-readable line for the harness threshold check.
    printf("\nPERF draw_avg_us=%.1f draw_p99_us=%.1f tpos_avg_us=%.1f cla_avg_us=%.1f\n",
        dAvg, dP99, avg(tpos), avg(cla));
    fflush(stdout);
    if (Shutdown) Shutdown();
    return 0;
}
