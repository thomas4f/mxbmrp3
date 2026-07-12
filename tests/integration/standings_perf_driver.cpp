// ============================================================================
// tests/integration/standings_perf_driver.cpp
// Per-phase CPU probe for StandingsHud::rebuildRenderData(), the priciest single
// HUD rebuild at max settings (~1.5ms in the whole-plugin benchmark). Where the
// bench driver reports one number per HUD, this drives a standings rebuild every
// frame (RaceClassification each frame -> Standings dirty -> Draw rebuilds) and
// reads the internal per-phase timing (MXBMRP3_Test_StandingsProfile): setup
// (build display entries) / format (gap+laptime+penalty strings) / name+anim /
// layout / render (per-row quads + strings). Runs default settings vs max
// settings so the cost of "all columns x 50 rows x long names" is attributed.
//
//   x86_64-w64-mingw32-g++ -std=c++17 -O2 standings_perf_driver.cpp -o standings_perf_driver.exe
//   wine standings_perf_driver.exe mxbmrp3_test.dlo
// ============================================================================
#include <windows.h>
#include <cstdio>
#include <cstdint>
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

typedef int  (*PFN_Startup)(char*);
typedef void (*PFN_Shutdown)();
typedef void (*PFN_DS)(void*, int);
typedef void (*PFN_Class)(void*, int, void*, int);
typedef void (*PFN_TrackPos)(int, void*, int);
typedef void (*PFN_Draw)(int, int*, void**, int*, void**);
typedef void (*PFN_Void)();
typedef long long (*PFN_StProfile)(double*, double*, double*, double*, double*);
typedef double (*PFN_StTracked)();

static LARGE_INTEGER g_freq;
static uint64_t nowUs() { LARGE_INTEGER t; QueryPerformanceCounter(&t); return (uint64_t)(t.QuadPart * 1000000.0 / g_freq.QuadPart); }

static const int RIDERS = 40;

static PFN_Draw Draw;
static PFN_Class RaceClassification;
static PFN_StProfile StProfile;
static PFN_StTracked StTracked;

int main(int argc, char** argv) {
    const char* dll = (argc > 1) ? argv[1] : "mxbmrp3_test.dlo";
    QueryPerformanceFrequency(&g_freq);
    HMODULE h = LoadLibraryA(dll);
    if (!h) { printf("FAIL: LoadLibrary %lu\n", GetLastError()); return 2; }
    auto S = [&](const char* n){ return GetProcAddress(h, n); };
    auto Startup=(PFN_Startup)S("Startup"); auto Shutdown=(PFN_Shutdown)S("Shutdown");
    auto EventInit=(PFN_DS)S("EventInit"); auto RaceEvent=(PFN_DS)S("RaceEvent");
    auto RaceSession=(PFN_DS)S("RaceSession"); auto RaceAddEntry=(PFN_DS)S("RaceAddEntry");
    RaceClassification=(PFN_Class)S("RaceClassification");
    auto RaceTrackPosition=(PFN_TrackPos)S("RaceTrackPosition");
    Draw=(PFN_Draw)S("Draw");
    auto MaxSettings=(PFN_Void)S("MXBMRP3_Test_MaxHudSettings");
    StProfile=(PFN_StProfile)S("MXBMRP3_Test_StandingsProfile");
    StTracked=(PFN_StTracked)S("MXBMRP3_Test_StandingsTrackedUs");
    if (!Startup || !Draw || !RaceClassification) { printf("FAIL: missing core exports\n"); return 2; }
    if (!MaxSettings || !StProfile) { printf("FAIL: missing standings hooks (rebuild the DLL)\n"); return 2; }

    char savePath[] = "Z:\\tmp\\mxbperf\\";
    Startup(savePath);

    SPluginsBikeEvent_t ev{}; strcpy(ev.m_szRiderName,"Player Longname Zero One"); strcpy(ev.m_szBikeName,"Test 450");
    strcpy(ev.m_szCategory,"MX1"); strcpy(ev.m_szTrackName,"StTrack"); ev.m_fTrackLength=1600.0f; ev.m_iType=2;
    EventInit(&ev,(int)sizeof(ev));
    SPluginsRaceEvent_t re{}; re.m_iType=2; strcpy(re.m_szName,"StTrack"); strcpy(re.m_szTrackName,"StTrack"); re.m_fTrackLength=1600.0f;
    if (RaceEvent) RaceEvent(&re,(int)sizeof(re));
    SPluginsRaceSession_t ss{}; ss.m_iSession=6; ss.m_iSessionState=16; ss.m_iSessionLength=480000; ss.m_iSessionNumLaps=6;
    if (RaceSession) RaceSession(&ss,(int)sizeof(ss));

    // Long rider names so LONG name-mode + fitText truncation is exercised at max.
    for (int i=0;i<RIDERS;++i){ SPluginsRaceAddEntry_t e{}; e.m_iRaceNum=i+1;
        snprintf(e.m_szName,100,"Rider Fullname Number %02d Extra",i+1); strcpy(e.m_szBikeName,"Test 450"); strcpy(e.m_szBikeShortName,"T450");
        strcpy(e.m_szCategory,"MX1"); e.m_iNumberOfGears=5; e.m_iMaxRPM=13000; RaceAddEntry(&e,(int)sizeof(e)); }

    struct Cls { SPluginsRaceClassification_t hdr; SPluginsRaceClassificationEntry_t e[64]; } cls{};
    cls.hdr.m_iSession=6; cls.hdr.m_iSessionState=16; cls.hdr.m_iSessionTime=120000; cls.hdr.m_iNumEntries=RIDERS;
    for (int i=0;i<RIDERS;++i){ cls.e[i].m_iRaceNum=i+1; cls.e[i].m_iBestLap=90000+i*350; cls.e[i].m_iNumLaps=3; cls.e[i].m_iGap=i*450; cls.e[i].m_iPenalty=(i%5)*1000; }
    RaceClassification(&cls.hdr,(int)sizeof(cls.hdr),cls.e,(int)sizeof(cls.e[0]));

    // Positions so live gaps / active-trackpos paths engage for all riders.
    SPluginsRaceTrackPosition_t* pos=(SPluginsRaceTrackPosition_t*)calloc(RIDERS,sizeof(SPluginsRaceTrackPosition_t));
    for (int i=0;i<RIDERS;++i){ pos[i].m_iRaceNum=i+1; pos[i].m_fTrackPos=(float)i/RIDERS; }
    if (RaceTrackPosition) RaceTrackPosition(RIDERS,pos,(int)sizeof(pos[0]));

    const int FRAMES = 8000;

    auto runScenario = [&](const char* label) {
        // Reset the phase profile, then drive a standings rebuild every frame.
        { double d; StProfile(&d,&d,&d,&d,&d); if (StTracked) StTracked(); }
        double sumDraw = 0; int nDraw = 0;
        for (int f = 0; f < FRAMES; ++f) {
            for (int r = 0; r < RIDERS; ++r) cls.e[r].m_iGap = (r*450 + f*7) % 60000;  // changing gaps -> re-format
            RaceClassification(&cls.hdr,(int)sizeof(cls.hdr),cls.e,(int)sizeof(cls.e[0]));
            int nq,ns; void*q; void*s;
            uint64_t t0 = nowUs();
            Draw(0,&nq,&q,&ns,&s);
            sumDraw += (double)(nowUs()-t0); ++nDraw;
        }
        double se=0,fo=0,na=0,la=0,re2=0;
        long long c = StProfile(&se,&fo,&na,&la,&re2);
        double trk = StTracked ? StTracked() : 0;
        double total = se+fo+na+la+re2;
        printf("%-16s  rebuilds=%lld   Draw avg=%.1f us\n", label, c, nDraw?sumDraw/nDraw:0);
        if (c > 0) {
            printf("   per-rebuild us:  setup %6.1f  format %6.1f  name+anim %6.1f  layout %6.1f  render %6.1f   TOTAL %6.1f\n",
                se/c, fo/c, na/c, la/c, re2/c, total/c);
            printf("      of which render's TRACKED-column status resolution: %6.1f us  (%.0f%% of render)\n",
                trk/c, re2 > 0 ? 100.0*trk/re2 : 0.0);
        }
    };

    printf("\n=== StandingsHud rebuild per-phase probe (40 riders, rebuild every frame, headless/Wine) ===\n");
    printf("%d rebuilds per scenario.\n\n", FRAMES);

    // Warmup (default settings).
    { double d; StProfile(&d,&d,&d,&d,&d); for (int f=0;f<1000;++f){ RaceClassification(&cls.hdr,(int)sizeof(cls.hdr),cls.e,(int)sizeof(cls.e[0])); int nq,ns; void*q; void*s; Draw(0,&nq,&q,&ns,&s);} }

    runScenario("default");
    MaxSettings();
    runScenario("max settings");

    fflush(stdout);
    if (Shutdown) Shutdown();
    return 0;
}
