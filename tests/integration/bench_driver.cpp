// ============================================================================
// tests/integration/bench_driver.cpp
// Whole-plugin timing breakdown using the plugin's OWN developer profiler
// (BenchmarkWidget / PluginData::BenchmarkMetrics). Where map_perf_driver.cpp
// isolates one HUD, this drives a realistic full-grid race with the built-in
// benchmark active and lets the plugin export its per-callback + per-HUD-rebuild
// report — the same report a developer gets in-game with developerMode=1, but
// headless. It answers "what else costs time besides the map?".
//
// Each frame drives the realistic hot loop (positions -> RaceTrackPosition,
// periodic RunTelemetry + RaceClassification, then Draw) so the standings, map
// and default widgets actually re-dirty and rebuild and get timed. On exit it
// toggles the benchmark off, which writes <save>/mxbmrp3/benchmarks/benchmark_*.txt.
//
//   x86_64-w64-mingw32-g++ -std=c++17 -O2 bench_driver.cpp -o bench_driver.exe
//   wine bench_driver.exe mxbmrp3_test.dlo
//   cat <save>/mxbmrp3/benchmarks/benchmark_*.txt
// ============================================================================
#include <windows.h>
#include <cstdio>
#include <cstring>
#include <cstdlib>

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
typedef void (*PFN_BenchI)(int);
typedef void (*PFN_ShowAll)(int);
typedef void (*PFN_MaxSettings)();

static const int RIDERS = 40;

int main(int argc, char** argv) {
    const char* dll = (argc > 1) ? argv[1] : "mxbmrp3_test.dlo";
    HMODULE h = LoadLibraryA(dll);
    if (!h) { printf("FAIL: LoadLibrary %lu\n", GetLastError()); return 2; }
    auto S = [&](const char* n){ return GetProcAddress(h, n); };
    auto Startup=(PFN_Startup)S("Startup"); auto Shutdown=(PFN_Shutdown)S("Shutdown");
    auto EventInit=(PFN_DS)S("EventInit"); auto RaceEvent=(PFN_DS)S("RaceEvent");
    auto RaceSession=(PFN_DS)S("RaceSession"); auto RaceAddEntry=(PFN_DS)S("RaceAddEntry");
    auto RaceClassification=(PFN_Class)S("RaceClassification");
    auto RaceTrackPosition=(PFN_TrackPos)S("RaceTrackPosition");
    auto RunTelemetry=(PFN_Telem)S("RunTelemetry"); auto TrackCenterline=(PFN_TrackCenter)S("TrackCenterline");
    auto Draw=(PFN_Draw)S("Draw");
    auto Benchmark=(PFN_BenchI)S("MXBMRP3_Test_BenchmarkWidget");
    auto ShowAll=(PFN_ShowAll)S("MXBMRP3_Test_ShowAllHuds");
    auto MaxSettings=(PFN_MaxSettings)S("MXBMRP3_Test_MaxHudSettings");
    if (!Startup || !Draw || !RaceTrackPosition) { printf("FAIL: missing core exports\n"); return 2; }
    if (!Benchmark) { printf("FAIL: missing MXBMRP3_Test_BenchmarkWidget (rebuild the DLL)\n"); return 2; }

    // arg2: "all"  -> every HUD/widget visible (default settings)
    //       "max"  -> every HUD visible AND its individual settings cranked to max
    const bool showAll = (argc > 2 && (strcmp(argv[2], "all") == 0 || strcmp(argv[2], "max") == 0));
    const bool maxSettings = (argc > 2 && strcmp(argv[2], "max") == 0);

    char savePath[] = "Z:\\tmp\\mxbperf\\";
    Startup(savePath);

    SPluginsBikeEvent_t ev{}; strcpy(ev.m_szRiderName,"Player"); strcpy(ev.m_szBikeName,"Test 450");
    strcpy(ev.m_szCategory,"MX1"); strcpy(ev.m_szTrackName,"BenchTrack"); ev.m_fTrackLength=1600.0f; ev.m_iType=2;
    EventInit(&ev,(int)sizeof(ev));
    SPluginsRaceEvent_t re{}; re.m_iType=2; strcpy(re.m_szName,"BenchTrack"); strcpy(re.m_szTrackName,"BenchTrack"); re.m_fTrackLength=1600.0f;
    if (RaceEvent) RaceEvent(&re,(int)sizeof(re));
    SPluginsRaceSession_t ss{}; ss.m_iSession=6; ss.m_iSessionState=16; ss.m_iSessionLength=480000; ss.m_iSessionNumLaps=6;
    if (RaceSession) RaceSession(&ss,(int)sizeof(ss));

    for (int i=0;i<RIDERS;++i){ SPluginsRaceAddEntry_t e{}; e.m_iRaceNum=i+1;
        snprintf(e.m_szName,100,"Rider %02d",i+1); strcpy(e.m_szBikeName,"Test 450"); strcpy(e.m_szBikeShortName,"T450");
        strcpy(e.m_szCategory,"MX1"); e.m_iNumberOfGears=5; e.m_iMaxRPM=13000; RaceAddEntry(&e,(int)sizeof(e)); }

    // Real 2D loop (circle of curve segments) so the map has genuine geometry.
    const int SEGS=256; SPluginsTrackSegment_t* segs=(SPluginsTrackSegment_t*)calloc(SEGS,sizeof(SPluginsTrackSegment_t));
    const float TRACK_LEN=1600.0f, RADIUS=TRACK_LEN/(2.0f*3.14159265f);
    for (int i=0;i<SEGS;++i){ segs[i].m_iType=1; segs[i].m_fLength=TRACK_LEN/SEGS; segs[i].m_fRadius=RADIUS; segs[i].m_fAngle=0.0f; }
    float raceData[4]={800.0f,400.0f,1200.0f,0.0f};
    if (TrackCenterline) TrackCenterline(SEGS,segs,raceData);

    struct { SPluginsRaceClassification_t hdr; SPluginsRaceClassificationEntry_t e[64]; } cls{};
    cls.hdr.m_iSession=6; cls.hdr.m_iSessionState=16; cls.hdr.m_iSessionTime=120000; cls.hdr.m_iNumEntries=RIDERS;
    for (int i=0;i<RIDERS;++i){ cls.e[i].m_iRaceNum=i+1; cls.e[i].m_iBestLap=90000+i*350; cls.e[i].m_iNumLaps=3; cls.e[i].m_iGap=i*450; }
    RaceClassification(&cls.hdr,(int)sizeof(cls.hdr),cls.e,(int)sizeof(cls.e[0]));

    SPluginsRaceTrackPosition_t* pos=(SPluginsRaceTrackPosition_t*)calloc(RIDERS,sizeof(SPluginsRaceTrackPosition_t));
    for (int i=0;i<RIDERS;++i){ pos[i].m_iRaceNum=i+1; }

    SPluginsBikeData_t bd{}; bd.m_iRPM=9000; bd.m_iGear=4; bd.m_fSpeed=28.0f; bd.m_fFuel=3.5f; bd.m_fRoll=25.0f;
    bd.m_afSuspLen[0]=0.12f; bd.m_afSuspLen[1]=0.14f;

    // --- Drive a realistic race with the built-in benchmark active -----------
    if (showAll && ShowAll) { ShowAll(1); printf("ALL HUDs/widgets forced visible.\n"); }
    if (maxSettings && MaxSettings) { MaxSettings(); printf("Heavy HUD settings cranked to MAX.\n"); }
    printf("Driving %d-rider race with benchmark profiler active (%s)...\n",
           RIDERS, maxSettings ? "everything + max settings" : (showAll ? "everything enabled" : "default HUDs"));
    Benchmark(1);  // activate + reset the profiler
    const int FRAMES = 900;  // > 30 (snapshot interval) so the export reflects a full window
    for (int f = 0; f < FRAMES; ++f) {
        for (int r = 0; r < RIDERS; ++r) {
            pos[r].m_fTrackPos = (float)((f + r * 25) % 1000) / 1000.0f;
            pos[r].m_fPosX = (float)((f * 3 + r * 7) % 1600);
            pos[r].m_fPosZ = (float)((f * 5 + r * 11) % 1600);
            pos[r].m_fYaw = (float)((f + r * 7) % 360);
        }
        RaceTrackPosition(RIDERS, pos, (int)sizeof(pos[0]));
        if (RunTelemetry) { bd.m_fRoll = (float)((f % 90) - 45); RunTelemetry(&bd, (int)sizeof(bd), f * 0.01f, (float)(f % 1000) / 1000.0f); }
        if ((f % 48) == 0) {
            for (int r = 0; r < RIDERS; ++r) cls.e[r].m_iGap = (r * 450 + f) % 60000;
            RaceClassification(&cls.hdr, (int)sizeof(cls.hdr), cls.e, (int)sizeof(cls.e[0]));
        }
        int nq, ns; void *q, *s;
        Draw(0, &nq, &q, &ns, &s);
    }
    Benchmark(0);  // deactivate -> exports report to <save>/mxbmrp3/benchmarks/
    printf("Done. Report written to <save>/mxbmrp3/benchmarks/benchmark_*.txt\n");

    if (Shutdown) Shutdown();
    return 0;
}
