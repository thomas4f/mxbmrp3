// ============================================================================
// tests/integration/harness/plugin_host.h
// PluginHost — the one place the headless integration tests touch the plugin.
// It loads the cross-compiled DLL, resolves the PiBoSo exports (and the
// MXBMRP3_Test_* hooks), drives the common callbacks with sensible defaults,
// starts the embedded web server via the test hook, and hands back the plugin's
// own /api/state as parsed JSON.
//
// This replaces the per-driver copy-pasted struct blocks, socket code, and
// file-shuttling: a test now reads like the scenario it describes
// (host.addEntry(...); host.classify(...); auto st = host.state();) and asserts
// with doctest + nlohmann::json. Runs under Wine; no game, no Windows.
// ============================================================================
#pragma once
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>
#include <sstream>

#include "plugin_api.h"
#include "tape.h"
#include "nlohmann/json.hpp"

// Unbuffered progress trace (stderr) so a Wine crash still shows how far we got.
#define HOST_TRACE(...) do { fprintf(stderr, "[host] " __VA_ARGS__); fputc('\n', stderr); fflush(stderr); } while (0)

class PluginHost {
public:
    using json = nlohmann::json;

    explicit PluginHost(const char* dll) {
        m_h = LoadLibraryA(dll);
        if (!m_h) { HOST_TRACE("LoadLibrary(%s) failed: %lu", dll, GetLastError()); return; }
        m_startup   = sym<PFN_Startup>("Startup");
        m_shutdown  = sym<PFN_Shutdown>("Shutdown");
        m_eventInit = sym<PFN_Void_DS>("EventInit");
        m_raceEvent = sym<PFN_Void_DS>("RaceEvent");
        m_session   = sym<PFN_Void_DS>("RaceSession");
        m_sessionState = sym<PFN_Void_DS>("RaceSessionState");
        m_addEntry  = sym<PFN_Void_DS>("RaceAddEntry");
        m_removeEntry = sym<PFN_Void_DS>("RaceRemoveEntry");
        m_classify  = sym<PFN_Class>("RaceClassification");
        m_comm      = sym<PFN_Void_DS>("RaceCommunication");
        m_raceLap   = sym<PFN_Void_DS>("RaceLap");
        m_raceSplit = sym<PFN_Void_DS>("RaceSplit");
        m_holeshot  = sym<PFN_Void_DS>("RaceHoleshot");
        m_trackPos  = sym<PFN_CountArray>("RaceTrackPosition");
        m_trackCenter = sym<PFN_TrackCenter>("TrackCenterline");
        m_spectate  = sym<PFN_Spectate>("SpectateVehicles");
        m_runInit   = sym<PFN_Void_DS>("RunInit");
        m_runDeinit = sym<PFN_Shutdown>("RunDeinit");
        m_runStart  = sym<PFN_Shutdown>("RunStart");
        m_runStop   = sym<PFN_Shutdown>("RunStop");
        m_telemetry = sym<PFN_Telemetry>("RunTelemetry");
        m_getRTG    = sym<int(*)(int)>("MXBMRP3_Test_GetRealTimeGap");
        m_hasATP    = sym<int(*)(int)>("MXBMRP3_Test_HasActiveTrackPos");
        m_hazCount  = sym<int(*)()>("MXBMRP3_Test_HazardRaceNumCount");
        m_takeATPB  = sym<int(*)()>("MXBMRP3_Test_TakeNewAllTimePB");
        m_spectatable = sym<int(*)(int)>("MXBMRP3_Test_IsRiderSpectatable");
        m_evtRegions  = sym<int(*)()>("MXBMRP3_Test_EventLogSpectateRegionCount");
        m_evtAutoHide = sym<void(*)(int,int)>("MXBMRP3_Test_EventLogSetAutoHide");
        m_chartSeries = sym<void(*)(int,int*)>("MXBMRP3_Test_ChartSeries");
        m_blueFlag  = sym<int(*)(int)>("MXBMRP3_Test_IsRiderBlueFlagged");
        m_lapping   = sym<int(*)(int)>("MXBMRP3_Test_IsRiderLapping");
        m_lapTarget = sym<int(*)(int)>("MXBMRP3_Test_RiderLappingTarget");
        m_draw      = sym<PFN_Draw>("Draw");
        m_startHttp = sym<void(*)()>("MXBMRP3_Test_StartHttp");
        m_snapshot  = sym<const char*(*)()>("MXBMRP3_Test_Snapshot");
        m_snapshotSeq = sym<unsigned long long(*)()>("MXBMRP3_Test_SnapshotSeq");
        m_ptEnable  = sym<void(*)()>("MXBMRP3_Test_PluginThreadEnable");
        m_ptEnabled = sym<int(*)()>("MXBMRP3_Test_PluginThreadEnabled");
        m_ptFlush   = sym<void(*)()>("MXBMRP3_Test_PluginThreadFlush");
        m_ptStop    = sym<void(*)()>("MXBMRP3_Test_PluginThreadStop");
        m_ptAbort   = sym<void(*)()>("MXBMRP3_Test_PluginThreadAbortWorker");
        m_ptSwallow = sym<void(*)(int)>("MXBMRP3_Test_PluginThreadSwallowBatches");
        m_setProduceDelay = sym<void(*)(int)>("MXBMRP3_Test_SetProduceDelayMs");
        m_getDebugMetrics = sym<void(*)(float*,float*,float*)>("MXBMRP3_Test_GetDebugMetrics");
        m_setPtFlag = sym<void(*)(int)>("MXBMRP3_Test_SetPluginThreadFlag");
        m_xiStopIo = sym<void(*)()>("MXBMRP3_Test_XInputStopIo");
        m_xiSetIndex = sym<void(*)(int)>("MXBMRP3_Test_XInputSetIndex");
        m_xiVibrate = sym<void(*)(float,float)>("MXBMRP3_Test_XInputVibrate");
        m_xiConsume = sym<int(*)(int*,int*,int*)>("MXBMRP3_Test_XInputConsumePending");
        m_ruSetPerBike  = sym<void(*)(int)>("MXBMRP3_Test_RumbleSetPerBike");
        m_ruSetEnabled  = sym<void(*)(int)>("MXBMRP3_Test_RumbleSetEnabled");
        m_ruLoad        = sym<void(*)(const char*)>("MXBMRP3_Test_RumbleLoadProfiles");
        m_ruHasProfile  = sym<int(*)()>("MXBMRP3_Test_RumbleHasProfile");
        m_ruChannels    = sym<void(*)(float*,float*,float*,float*,float*,float*,float*,float*,float*,float*,float*,float*)>("MXBMRP3_Test_RumbleChannels");
        m_startRec  = sym<int(*)(const char*)>("MXBMRP3_Test_StartRecording");
        m_stopRec   = sym<void(*)()>("MXBMRP3_Test_StopRecording");
        m_resetAll  = sym<void(*)()>("MXBMRP3_Test_ResetAll");
        m_resetActiveProfile = sym<void(*)()>("MXBMRP3_Test_ResetActiveProfile");
        m_resetHud  = sym<void(*)(const char*, int)>("MXBMRP3_Test_ResetHud");
        m_copyProfileToAll = sym<void(*)()>("MXBMRP3_Test_CopyProfileToAll");
        m_switchProfile = sym<void(*)(int)>("MXBMRP3_Test_SwitchProfile");
        m_setAutoSwitch = sym<void(*)(int)>("MXBMRP3_Test_SetAutoSwitch");
        m_getActiveProfile = sym<int(*)()>("MXBMRP3_Test_GetActiveProfile");
        m_dirSetEnabled = sym<void(*)(int)>("MXBMRP3_Test_DirectorSetEnabled");
        m_dirToggleLock = sym<void(*)()>("MXBMRP3_Test_DirectorToggleLock");
        m_dirIsLocked = sym<int(*)()>("MXBMRP3_Test_DirectorIsLocked");
        m_dirNextLockedCam = sym<int(*)(int)>("MXBMRP3_Test_DirectorNextLockedCamera");
        m_dirSetNowMs = sym<void(*)(long long)>("MXBMRP3_Test_DirectorSetNowMs");
        m_dirSetStories = sym<void(*)(int)>("MXBMRP3_Test_DirectorSetStories");
        m_dirSetShotSec = sym<void(*)(int, int)>("MXBMRP3_Test_DirectorSetShotSec");
        m_dirHomeSubject = sym<int(*)()>("MXBMRP3_Test_DirectorHomeSubject");
        m_dirSubject = sym<int(*)()>("MXBMRP3_Test_DirectorSubject");
        m_benchHudCount = sym<int(*)()>("MXBMRP3_Test_BenchmarkHudCount");
        m_benchCbCount  = sym<int(*)()>("MXBMRP3_Test_BenchmarkCallbackCount");
        m_eventLogEnableDirector = sym<void(*)(int)>("MXBMRP3_Test_EventLogEnableDirector");
        m_timingConfig = sym<void(*)(int,int,int)>("MXBMRP3_Test_TimingConfig");
        m_timingReferenceMs = sym<int(*)(int,int)>("MXBMRP3_Test_TimingReferenceMs");
        m_timingTargetSplit = sym<int(*)()>("MXBMRP3_Test_TimingTargetSplit");
        m_timingInvalidShown = sym<int(*)()>("MXBMRP3_Test_TimingInvalidShown");
        m_timingFrozen = sym<int(*)()>("MXBMRP3_Test_TimingFrozen");
        m_elapsedLapTime = sym<int(*)()>("MXBMRP3_Test_ElapsedLapTime");
        m_lapTimerFromRaceStart = sym<int(*)()>("MXBMRP3_Test_LapTimerFromRaceStart");
        m_inGridStartGrace = sym<int(*)()>("MXBMRP3_Test_InGridStartGrace");
        m_timingGeometry = sym<void(*)(int*,int*,int*,int*,int*,int*)>("MXBMRP3_Test_TimingGeometry");
        m_eventLogSetVisible = sym<void(*)(int)>("MXBMRP3_Test_EventLogSetVisible");
        m_eventLogIconColorSlot = sym<int(*)(const char*)>("MXBMRP3_Test_EventLogIconColorSlot");
        m_noticesSetVisible = sym<void(*)(int)>("MXBMRP3_Test_NoticesSetVisible");
        m_save      = sym<void(*)()>("MXBMRP3_Test_Save");
        m_markDirty = sym<void(*)()>("MXBMRP3_Test_MarkDirty");
        m_flushIfDirty = sym<void(*)()>("MXBMRP3_Test_FlushIfDirty");
        m_isDirty = sym<int(*)()>("MXBMRP3_Test_IsDirty");
        m_setAutoSave = sym<void(*)(int)>("MXBMRP3_Test_SetAutoSave");
        m_loadSettings = sym<void(*)(const char*)>("MXBMRP3_Test_LoadSettings");
        m_setActiveTab = sym<void(*)(const char*)>("MXBMRP3_Test_SetActiveTab");
        m_showSettings = sym<void(*)(int)>("MXBMRP3_Test_ShowSettings");
        m_stepCount    = sym<int(*)(int)>("MXBMRP3_Test_SettingsSteppedCount");
        m_stepClick    = sym<int(*)(int,int,int)>("MXBMRP3_Test_SettingsClickStepped");
        m_cycleCount   = sym<int(*)(int)>("MXBMRP3_Test_SettingsCycleCount");
        m_regionSig    = sym<void(*)(char*,int)>("MXBMRP3_Test_SettingsRegionSignature");
        m_cycleClick   = sym<int(*)(int,int)>("MXBMRP3_Test_SettingsClickCycle");
        m_ruBumpsLight = sym<float(*)()>("MXBMRP3_Test_RumbleActiveBumpsLight");
        m_companion = sym<void(*)(int)>("MXBMRP3_Test_CompanionWindow");
        m_getActiveTab = sym<void(*)(char*, int)>("MXBMRP3_Test_GetActiveTab");
        m_capturedSections = sym<void(*)(char*, int)>("MXBMRP3_Test_CapturedSections");
        m_anPrime      = sym<void(*)()>("MXBMRP3_Test_AnalyticsPrime");
        m_anSetFull    = sym<void(*)(int)>("MXBMRP3_Test_AnalyticsSetFullLaunch");
        m_anAppStarted = sym<void(*)(char*, int)>("MXBMRP3_Test_AnalyticsAppStarted");
        m_anSessionEnd = sym<void(*)()>("MXBMRP3_Test_AnalyticsQueueSessionEnd");
        m_anCustom     = sym<void(*)(const char*)>("MXBMRP3_Test_AnalyticsQueueCustom");
        m_anSeedCrash  = sym<void(*)(const char*, const char*, const char*)>("MXBMRP3_Test_AnalyticsSeedCrash");
        m_anDrain      = sym<int(*)(char*, int)>("MXBMRP3_Test_AnalyticsDrainPending");
        m_resolveFrame = sym<void(*)(unsigned long long, char*, int)>("MXBMRP3_Test_ResolveFrame");
        m_extractInstall = sym<int(*)(const char*, const char*, int, char*, int)>("MXBMRP3_Test_ExtractAndInstall");
        m_stSetVisible      = sym<void(*)(int)>("MXBMRP3_Test_StandingsSetVisible");
        m_stSetCompVisible  = sym<void(*)(int)>("MXBMRP3_Test_StandingsSetCompanionVisible");
        m_stClearCompanion  = sym<void(*)()>("MXBMRP3_Test_StandingsClearCompanion");
        m_stCompanionState  = sym<void(*)(int*, int*, int*)>("MXBMRP3_Test_StandingsCompanionState");
        m_stPlateInsetY     = sym<float(*)(int)>("MXBMRP3_Test_StandingsPlateInsetY");
        m_stSetOffset       = sym<void(*)(float, float)>("MXBMRP3_Test_StandingsSetOffset");
        m_tmSetVisible      = sym<void(*)(int)>("MXBMRP3_Test_TelemetrySetVisible");
        m_tmSetCompVisible  = sym<void(*)(int)>("MXBMRP3_Test_TelemetrySetCompanionVisible");
        m_tmClearCompanion  = sym<void(*)()>("MXBMRP3_Test_TelemetryClearCompanion");
        m_tmHistoryDepth    = sym<int(*)()>("MXBMRP3_Test_TelemetryHistoryDepth");
        m_tmClearHistory    = sym<void(*)()>("MXBMRP3_Test_TelemetryClearHistory");
        m_bmSetVisible      = sym<void(*)(int)>("MXBMRP3_Test_BenchmarkSetVisible");
        m_bmSetCompVisible  = sym<void(*)(int)>("MXBMRP3_Test_BenchmarkSetCompanionVisible");
        m_bmActive          = sym<int(*)()>("MXBMRP3_Test_BenchmarkMetricsActive");
        m_bmExists          = sym<int(*)()>("MXBMRP3_Test_BenchmarkExists");
        m_helmetSetVisible  = sym<void(*)(int)>("MXBMRP3_Test_HelmetSetVisible");
        m_helmetVisible     = sym<int(*)()>("MXBMRP3_Test_HelmetVisible");
        m_helmetSetCompVis  = sym<void(*)(int)>("MXBMRP3_Test_HelmetSetCompanionVisible");
        m_helmetAnySurface  = sym<int(*)()>("MXBMRP3_Test_HelmetVisibleAnySurface");
        m_clickDirectorVis  = sym<int(*)()>("MXBMRP3_Test_ClickDirectorHudVisible");
        m_dirWidgetVis      = sym<void(*)(int*, int*)>("MXBMRP3_Test_DirectorWidgetVisibility");
        m_setDisplayTarget  = sym<void(*)(int)>("MXBMRP3_Test_SetDisplayTarget");
        m_getDisplayTarget  = sym<int(*)()>("MXBMRP3_Test_GetDisplayTarget");
        m_surfaceFrameStats = sym<void(*)(int*, int*, double*, double*)>("MXBMRP3_Test_SurfaceFrameStats");
        m_stSetCompOffset   = sym<void(*)(float, float)>("MXBMRP3_Test_StandingsSetCompanionOffset");
        m_companionClose    = sym<void(*)()>("MXBMRP3_Test_CompanionSimulateUserClose");
        m_fakeGamepad       = sym<void(*)(int)>("MXBMRP3_Test_FakeGamepad");
        m_gamepadExtent     = sym<void(*)(float*, float*)>("MXBMRP3_Test_GamepadContentExtent");
        m_forceSurface      = sym<void(*)(int)>("MXBMRP3_Test_ForceActiveSurface");
        m_rcSetVisible      = sym<void(*)(int)>("MXBMRP3_Test_SessionChartsSetVisible");
        m_rcSetCharts       = sym<void(*)(int)>("MXBMRP3_Test_SessionChartsSetCharts");
        m_fmxSetNow         = sym<void(*)(long long)>("MXBMRP3_Test_FmxSetNowUs");
        m_fmxState          = sym<void(*)(int*,int*,int*,int*,int*,int*,int*,int*)>("MXBMRP3_Test_FmxState");
        m_statsSetNow       = sym<void(*)(long long)>("MXBMRP3_Test_StatsSetNowUs");
        m_statsOdoState     = sym<void(*)(double*,double*,double*,int*)>("MXBMRP3_Test_StatsOdometerState");
        m_statsSave         = sym<void(*)()>("MXBMRP3_Test_StatsSave");
        m_recParse          = sym<int(*)(int, const char*)>("MXBMRP3_Test_RecordsParse");
        m_recCount          = sym<int(*)()>("MXBMRP3_Test_RecordsCount");
        m_recGet            = sym<int(*)(int, char*, int, char*, int, int*, int*, int*, int*, char*, int)>("MXBMRP3_Test_RecordsGet");
        m_recSetStub        = sym<void(*)(int, const char*)>("MXBMRP3_Test_RecordsSetFetchStub");
        m_recStartFetch     = sym<int(*)()>("MXBMRP3_Test_RecordsStartFetch");
        m_recFetchState     = sym<int(*)()>("MXBMRP3_Test_RecordsFetchState");
        m_steamStartWorker  = sym<int(*)()>("MXBMRP3_Test_SteamStartWorker");
        m_steamWorkerAlive  = sym<int(*)()>("MXBMRP3_Test_SteamWorkerRunning");
        m_eventDeinit  = sym<PFN_Shutdown>("EventDeinit");
        m_raceDeinit   = sym<PFN_Shutdown>("RaceDeinit");
        m_raceVehicleData = sym<PFN_Void_DS>("RaceVehicleData");
        m_runSplit     = sym<PFN_Void_DS>("RunSplit");
        m_cameras      = sym<PFN_Cameras>("SpectateCameras");
        m_bikeTelemetry = sym<void(*)(float*,int*,int*,float*,float*,float*,int*)>("MXBMRP3_Test_BikeTelemetry");
        m_curSplits    = sym<int(*)(int,int*,int*,int*,int*)>("MXBMRP3_Test_CurrentLapSplits");
        m_anStartWorker = sym<int(*)()>("MXBMRP3_Test_AnalyticsStartEventWorker");
        m_anWorkerAlive = sym<int(*)()>("MXBMRP3_Test_AnalyticsEventWorkerRunning");
        m_anShutdown    = sym<void(*)()>("MXBMRP3_Test_AnalyticsShutdown");
        m_reqCamera    = sym<void(*)(int)>("MXBMRP3_Test_RequestSpectateCamera");
        m_manualCam    = sym<int(*)()>("MXBMRP3_Test_ManualCameraActive");
        m_resetCamTrack = sym<void(*)()>("MXBMRP3_Test_ResetCameraTracking");
    }
    // Shut the plugin down BEFORE unloading it, unless a test opted out.
    //
    // This used to be a bare FreeLibrary, which left it to each test to remember
    // host.shutdown() — four had not, and blueflag_test crashed 8/10 times under
    // CPU load because its three SUBCASEs each unmapped the DLL with the
    // plugin's background threads still running (the unload-without-Shutdown()
    // path CLAUDE.md flags as having cost two shipped crashes). Fixing the four
    // call sites left the same trap for the 65th test, and for the abort path:
    // an explicit shutdown() is the LAST statement in a test body, so a failing
    // REQUIRE above it skips teardown and restores exactly the crashy path.
    // Doing it here covers every caller, present and future, plus the abort.
    ~PluginHost() {
        if (!m_h) return;
        if (!m_skipShutdownOnDestroy) shutdown();
        FreeLibrary(m_h);
    }

    // Opt OUT of the destructor's shutdown, for a test whose subject IS the
    // unload-without-Shutdown() path (teardown_test's second case). Without this
    // the safety above would silently turn that test into a duplicate of its
    // first case — and the deliberate omission it relies on would look like the
    // same oversight this destructor exists to fix. Saying it out loud makes it
    // a declaration rather than an absence.
    void skipShutdownOnDestroy() { m_skipShutdownOnDestroy = true; }

    PluginHost(const PluginHost&) = delete;
    PluginHost& operator=(const PluginHost&) = delete;

    bool loaded() const { return m_h != nullptr; }
    template <class T> T sym(const char* n) const { return reinterpret_cast<T>(GetProcAddress(m_h, n)); }

    // --- lifecycle -----------------------------------------------------------
    // savePath is a Wine Windows path (Z:\ maps to the unix root).
    // startup() re-arms the shutdown guard: several tests run start/shutdown
    // cycles (deinit, companion_decouple, director_*), and swallowing the second
    // shutdown would leave the plugin live across the next one.
    int startup(const char* savePath) {
        m_shutdownDone = false;
        m_started = true;
        return m_startup ? m_startup(const_cast<char*>(savePath)) : -1;
    }
    // Idempotent within one startup, so an explicit call followed by the
    // destructor's does not Shutdown twice.
    //
    // Also a no-op before the first startup(). Shutdown() constructs
    // PluginManager (and with it every singleton) in order to tear it down, so
    // calling it on a host that only LOADED the DLL would build the whole
    // plugin purely to destroy it — turning "the test never started the plugin"
    // into a full lifecycle. Every test today calls startup(), so this guards a
    // latent case rather than a live one; it exists because the destructor now
    // shuts down unconditionally, which is what made the case reachable.
    void shutdown() {
        if (!m_started) return;
        if (m_shutdown && !m_shutdownDone) { m_shutdownDone = true; m_shutdown(); }
    }

    // Start the web overlay server (default off) via the test hook, then poll
    // until it answers — this both waits out the bind race and registers the
    // first client so frequent-type (Standings) snapshot rebuilds aren't gated
    // out (hasActiveClients: a poll in the last 5s). Returns true once live.
    bool startHttp() {
        if (!m_startHttp) { HOST_TRACE("MXBMRP3_Test_StartHttp not exported"); return false; }
        m_startHttp();
        for (int i = 0; i < 100; ++i) {                 // ~2s worst case
            if (!rawState().empty()) return true;
            Sleep(20);
        }
        HOST_TRACE("http server did not answer /api/state");
        return false;
    }

    // Start the server WITHOUT touching /api/state. startHttp() above polls,
    // which counts as a client for the next 5s (hasActiveClients) and therefore
    // disables the frequent-change gating — exactly what a test of that gating
    // must not do. HttpServer::start() blocks until the port is bound and sets
    // m_running synchronously, so no polling is needed to know it is live.
    bool startHttpNoClient() {
        if (!m_startHttp) { HOST_TRACE("MXBMRP3_Test_StartHttp not exported"); return false; }
        m_startHttp();
        return true;
    }

    // Snapshot rebuild counter (the SSE sequence). See http_gating_test.cpp.
    bool hasSnapshotSeq() const { return m_snapshotSeq != nullptr; }
    unsigned long long snapshotSeq() { return m_snapshotSeq ? m_snapshotSeq() : 0ull; }

    // Callback-tape recorder (in-plugin, GAME_HAS_RECORDER). Open a tape at `path`,
    // drive callbacks (which record into it), then stopRecording() to finalize.
    bool startRecording(const std::string& path) {
        if (!m_startRec) { HOST_TRACE("MXBMRP3_Test_StartRecording not exported"); return false; }
        return m_startRec(path.c_str()) != 0;
    }
    void stopRecording() { if (m_stopRec) m_stopRec(); }

    // --- driving the game callbacks -----------------------------------------
    // category is the game's "Class" (MX1, MX2, ...) — the field the PiBoSo API spells
    // m_szCategory. It selects which stored PBs the default PBScope::CATEGORY scans, so
    // a test can put two different bikes in the same class (or deliberately not).
    //
    // trackId is m_szTrackID, which is NOT the track name: it is the other half of
    // StatsManager's "trackId|bikeName" PB key. It defaults to empty (what this harness
    // has always sent, so every existing test keeps its current keys); pass one when a
    // test reads the stats file by key and wants a realistic one.
    void eventInit(const char* trackName, const char* riderName,
                   float trackLength = 1600.0f, int type = 2,
                   const char* bikeName = "Test 450",
                   const char* category = "MX1",
                   const char* trackId = "") {
        SPluginsBikeEvent_t ev{};
        setStr(ev.m_szRiderName, riderName);
        setStr(ev.m_szBikeName, bikeName);
        setStr(ev.m_szCategory, category);
        setStr(ev.m_szTrackID, trackId);
        setStr(ev.m_szTrackName, trackName);
        ev.m_fTrackLength = trackLength; ev.m_iType = type;
        ev.m_iNumberOfGears = 6;   // a real bike has gears (so the gear widget shows a digit, not "D")
        if (m_eventInit) m_eventInit(&ev, (int)sizeof(ev));
    }
    void raceEvent(const char* trackName, int type = 2) {
        SPluginsRaceEvent_t re{};
        re.m_iType = type; setStr(re.m_szName, trackName);
        setStr(re.m_szTrackName, trackName); re.m_fTrackLength = 1600.0f;
        if (m_raceEvent) m_raceEvent(&re, (int)sizeof(re));
    }
    // session: 1=Practice .. 6/7=Race1/Race2 (PiBoSo session enum); state 16=running.
    void session(int session, int numLaps, int lengthMs = 480000, int state = 16) {
        SPluginsRaceSession_t ss{};
        ss.m_iSession = session; ss.m_iSessionState = state;
        ss.m_iSessionLength = lengthMs; ss.m_iSessionNumLaps = numLaps;
        if (m_session) m_session(&ss, (int)sizeof(ss));
    }
    // RaceSessionState: a session-state transition (256=pre-start, 16=in
    // progress/green, 512=race over, 2048=cancelled). The green-flag transition
    // snapshots the start grid; state changes log "started"/"ended" events.
    void raceSessionState(int session, int state, int lengthMs = 0) {
        SPluginsRaceSessionState_t ss{};
        ss.m_iSession = session; ss.m_iSessionState = state; ss.m_iSessionLength = lengthMs;
        if (m_sessionState) m_sessionState(&ss, (int)sizeof(ss));
        draw();
    }
    void addEntry(int num, const char* name) {
        SPluginsRaceAddEntry_t e{};
        e.m_iRaceNum = num; setStr(e.m_szName, name);
        setStr(e.m_szBikeName, "Test 450"); setStr(e.m_szBikeShortName, "T450");
        setStr(e.m_szCategory, "MX1"); e.m_iNumberOfGears = 5; e.m_iMaxRPM = 13000;
        if (m_addEntry) m_addEntry(&e, (int)sizeof(e));
    }
    void removeEntry(int num) {
        SPluginsRaceRemoveEntry_t rm{}; rm.m_iRaceNum = num;
        if (m_removeEntry) m_removeEntry(&rm, (int)sizeof(rm));
    }
    // Send a full classification; row order is the finishing order (index 0 = P1).
    void classify(int session, int sessionTimeMs, const std::vector<ClassRow>& rows,
                  int sessionState = 16) {
        std::vector<SPluginsRaceClassificationEntry_t> e(rows.size());
        for (size_t i = 0; i < rows.size(); ++i) {
            e[i] = SPluginsRaceClassificationEntry_t{};
            e[i].m_iRaceNum = rows[i].num; e[i].m_iState = rows[i].state;
            e[i].m_iBestLap = rows[i].best; e[i].m_iBestLapNum = rows[i].bestLapNum;
            e[i].m_iNumLaps = rows[i].laps; e[i].m_iGap = rows[i].gap;
            e[i].m_iGapLaps = rows[i].gapLaps; e[i].m_iPenalty = rows[i].penalty;
            e[i].m_iPit = rows[i].pit;
        }
        SPluginsRaceClassification_t hdr{};
        hdr.m_iSession = session; hdr.m_iSessionState = sessionState;
        hdr.m_iSessionTime = sessionTimeMs; hdr.m_iNumEntries = (int)rows.size();
        if (m_classify) m_classify(&hdr, (int)sizeof(hdr),
                                   e.data(), (int)sizeof(SPluginsRaceClassificationEntry_t));
        draw();  // let the change flush through to the snapshot
    }
    // RaceLap: a rider completed a lap. best: 0=neither, 1=personal best,
    // 2=overall best (fires the fastest-lap event when online). split0/split1 are
    // the ACCUMULATED split times (S1, then S1+S2); the plugin derives per-sector
    // times (sector1=split0, sector2=split1-split0, sector3=lapTime-split1) that
    // feed the best-sectors board. Default to even thirds.
    void raceLap(int session, int raceNum, int lapNum, int lapTimeMs, int best = 0,
                 int split0 = -1, int split1 = -1, bool invalid = false) {
        SPluginsRaceLap_t lap{};
        lap.m_iSession = session; lap.m_iRaceNum = raceNum; lap.m_iLapNum = lapNum;
        lap.m_iInvalid = invalid ? 1 : 0;
        lap.m_iLapTime = lapTimeMs; lap.m_iBest = best;
        lap.m_aiSplit[0] = (split0 >= 0) ? split0 : lapTimeMs / 3;
        lap.m_aiSplit[1] = (split1 >= 0) ? split1 : 2 * lapTimeMs / 3;
        if (m_raceLap) m_raceLap(&lap, (int)sizeof(lap));
        draw();
    }

    // RaceSplit: a rider crossed a split line on the current lap. In a race this
    // snapshots the rider's position as the rolling reference for posDeltaSplit
    // (positions gained since the last split). session must match the running
    // session; splitTime must be > 0; splitIndex is 0..2.
    void raceSplit(int session, int raceNum, int lapNum, int splitIndex, int splitTimeMs) {
        SPluginsRaceSplit_t s{};
        s.m_iSession = session; s.m_iRaceNum = raceNum; s.m_iLapNum = lapNum;
        s.m_iSplit = splitIndex; s.m_iSplitTime = splitTimeMs;
        if (m_raceSplit) m_raceSplit(&s, (int)sizeof(s));
        draw();
    }

    // RaceCommunication: change a rider's state (state 4=DSQ, 3=retired, 1=DNS)
    // or apply a penalty (communication 2). Defaults to a state change.
    void communication(int raceNum, int state, int communication = 1) {
        SPluginsRaceCommunication_t rc{};
        rc.m_iRaceNum = raceNum; rc.m_iCommunication = communication;
        rc.m_iState = state; rc.m_iReason = 2;
        if (m_comm) m_comm(&rc, (int)sizeof(rc));
        draw();
    }
    // RaceHoleshot: who won the holeshot and when. The game doesn't fire this today
    // and the plugin takes no action on it, but the recorder captures it — drive it
    // to exercise the record + replay path.
    void raceHoleshot(int raceNum, int timeMs, int session = 6) {
        SPluginsRaceHoleshot_t h{};
        h.m_iSession = session;
        h.m_iRaceNum = raceNum;
        h.m_iTime = timeMs;
        if (m_holeshot) m_holeshot(&h, (int)sizeof(h));
    }
    // RaceTrackPosition: each rider's centerline position (0..1). Drives the map
    // and, for a race in progress, the real-time-gap computation. The session
    // clock used for the gap comes from the preceding classify(sessionTimeMs).
    void raceTrackPosition(const std::vector<TrackRow>& rows) {
        std::vector<SPluginsRaceTrackPosition_t> a(rows.size());
        for (size_t i = 0; i < rows.size(); ++i) {
            a[i] = SPluginsRaceTrackPosition_t{};
            a[i].m_iRaceNum = rows[i].num;
            a[i].m_fTrackPos = rows[i].trackPos;
            a[i].m_iCrashed = rows[i].crashed;
            a[i].m_fPosX = rows[i].posX;
            a[i].m_fPosZ = rows[i].posZ;
            a[i].m_fYaw = rows[i].yaw;
        }
        if (m_trackPos) m_trackPos((int)rows.size(), a.data(),
                                   (int)sizeof(SPluginsRaceTrackPosition_t));
    }
    // TrackCenterline: the track shape (once per session). Feeds MapHud so the map
    // draws. TrackSegmentRow matches SPluginsTrackSegment_t; raceData is the optional
    // marker array (start/finish + splits in meters) — empty = none.
    void trackCenterline(const std::vector<TrackSegmentRow>& segs,
                         const std::vector<float>& raceData = {}) {
        if (!m_trackCenter || segs.empty()) return;
        m_trackCenter((int)segs.size(), (void*)segs.data(),
                      raceData.empty() ? nullptr : (void*)raceData.data());
        draw();
    }
    // Internal real-time gap (ms) for a rider — 0 for the leader, -1 unknown
    // (via the MXBMRP3_Test_GetRealTimeGap hook; not in the JSON snapshot).
    int realTimeGap(int raceNum) { return m_getRTG ? m_getRTG(raceNum) : -2; }
    // Internal "recently seen in a RaceTrackPosition batch" bit (feeds liveGapValid).
    int hasActiveTrackPos(int raceNum) { return m_hasATP ? m_hasATP(raceNum) : -1; }
    int hazardRaceNumCount() { return m_hazCount ? m_hazCount() : -1; }
    // Consume the "new all-time PB" notice flag (true once per fired notice).
    bool takeNewAllTimePB() { return m_takeATPB && m_takeATPB() != 0; }
    // The shared click-to-spectate gate (Standings/Map/Event Log/Session Charts).
    bool isRiderSpectatable(int raceNum) { return m_spectatable && m_spectatable(raceNum) != 0; }
    // Event Log rows currently offering click-to-spectate.
    int eventLogSpectateRegionCount() { return m_evtRegions ? m_evtRegions() : -1; }
    // Put the Event Log into AUTO_HIDE with `durationMs` (0 = default).
    void eventLogSetAutoHide(bool on, int durationMs = 0) { if (m_evtAutoHide) m_evtAutoHide(on ? 1 : 0, durationMs); }
    // Session Charts sample resolution + the display rider's series length. sectorPoints:
    // 1/0 to force ELEM_SECTOR_POINTS on/off first, -1 to leave it as configured.
    struct ChartSeries { int pointsPerLap = -1; int points = -1; };
    ChartSeries chartSeries(int sectorPoints = -1) {
        ChartSeries cs;
        if (!m_chartSeries) return cs;
        int out[2] = { -1, -1 };
        m_chartSeries(sectorPoints, out);
        cs.pointsPerLap = out[0]; cs.points = out[1];
        return cs;
    }
    // Blue-flag / lapping detection (lazy-rebuilt caches).
    bool isRiderBlueFlagged(int raceNum) { return m_blueFlag && m_blueFlag(raceNum) != 0; }
    bool isRiderLapping(int raceNum) { return m_lapping && m_lapping(raceNum) != 0; }
    int  riderLappingTarget(int raceNum) { return m_lapTarget ? m_lapTarget(raceNum) : -2; }

    // SpectateVehicles: the game's rider list + which index the camera is on.
    // curSelection's rider becomes the spectated/"camera" rider (gets the camera
    // chip when the view is SPECTATE — call draw() with state 1 first, as the
    // driving helpers do). Rebuilds the snapshot (SpectateTarget change).
    void spectateVehicles(const std::vector<std::pair<int, std::string>>& riders,
                          int curSelection) {
        std::vector<SPluginsSpectateVehicle_t> a(riders.size());
        for (size_t i = 0; i < riders.size(); ++i) {
            a[i] = SPluginsSpectateVehicle_t{};
            a[i].m_iRaceNum = riders[i].first;
            setStr(a[i].m_szName, riders[i].second.c_str());
        }
        int select = curSelection;
        if (m_spectate) m_spectate((int)riders.size(), a.data(), curSelection, &select);
        draw();
    }

    // SpectateCameras: the game's per-track camera list. Unlike every other
    // callback this one is an OPAQUE blob with no element size — the names are
    // packed null-terminated strings — so the harness builds exactly that, padded
    // out so the plugin's bounded walk can never read past the allocation.
    // Returns the callback's own return (1 = "I changed the selection"); the
    // chosen index is written to *outSelect.
    int spectateCameras(const std::vector<std::string>& names, int curSelection,
                        int* outSelect = nullptr) {
        std::vector<unsigned char> blob;
        for (const auto& n : names) {
            for (char c : n) blob.push_back((unsigned char)c);
            blob.push_back(0);
        }
        blob.resize(4096, 0);   // matches Cameras::kMaxBytes, the plugin's walk cap
        int select = curSelection;
        int ret = m_cameras ? m_cameras((int)names.size(), blob.data(), curSelection, &select) : 0;
        if (outSelect) *outSelect = select;
        return ret;
    }

    // EventDeinit / RaceDeinit: the game left the event / the race session ended.
    // Both clear PluginData wholesale.
    void eventDeinit() { if (m_eventDeinit) m_eventDeinit(); }
    void raceDeinit()  { if (m_raceDeinit) m_raceDeinit(); }

    // RaceVehicleData: one rider's live vehicle state. This is the ONLY telemetry
    // source while spectating or in a replay, so it feeds the display rider's
    // telemetry; active=0 means the remaining fields are unset and ignored.
    void raceVehicleData(int raceNum, float speedMs, int gear, int rpm,
                         float throttle, float frontBrake, float leanDeg,
                         bool active = true) {
        SPluginsRaceVehicleData_t d{};
        d.m_iRaceNum = raceNum;
        d.m_iActive = active ? 1 : 0;
        d.m_iRPM = rpm;
        d.m_iGear = gear;
        d.m_fSpeedometer = speedMs;
        d.m_fThrottle = throttle;
        d.m_fFrontBrake = frontBrake;
        d.m_fLean = leanDeg;
        if (m_raceVehicleData) m_raceVehicleData(&d, (int)sizeof(d));
    }

    // RunSplit: the player's own split. Deliberately a no-op in the plugin —
    // RaceSplit (all riders) owns split handling. See run_split_test.cpp.
    void runSplit(int splitIndex, int splitTimeMs, int bestDiffMs = 0) {
        SPluginsBikeSplit_t s{};
        s.m_iSplit = splitIndex;
        s.m_iSplitTime = splitTimeMs;
        s.m_iBestDiff = bestDiffMs;
        if (m_runSplit) m_runSplit(&s, (int)sizeof(s));
    }

    // Live bike/input telemetry for the display rider (not in /api/state).
    struct BikeTelemetry {
        float speedometer = 0.0f;
        int gear = 0;
        int rpm = 0;
        float throttle = 0.0f;
        float frontBrake = 0.0f;
        float roll = 0.0f;
        bool valid = false;
    };
    BikeTelemetry bikeTelemetry() {
        BikeTelemetry t;
        int valid = 0;
        if (m_bikeTelemetry) {
            m_bikeTelemetry(&t.speedometer, &t.gear, &t.rpm, &t.throttle,
                            &t.frontBrake, &t.roll, &valid);
        }
        t.valid = (valid != 0);
        return t;
    }

    // A rider's live current-lap splits (-1 = not crossed yet). In-game display
    // only — never in /api/state — so this is the only way to assert them.
    struct CurrentLapSplits {
        bool present = false;
        int lapNum = -1;
        int s1 = -1, s2 = -1, s3 = -1;
    };
    CurrentLapSplits currentLapSplits(int raceNum) {
        CurrentLapSplits c;
        if (m_curSplits) {
            c.present = m_curSplits(raceNum, &c.lapNum, &c.s1, &c.s2, &c.s3) != 0;
        }
        return c;
    }

    // Start the analytics custom-event worker (capture mode: no network). Lets a
    // teardown test shut down with a live analytics thread to join.
    bool analyticsStartEventWorker() { return m_anStartWorker && m_anStartWorker() != 0; }
    bool analyticsEventWorkerRunning() { return m_anWorkerAlive && m_anWorkerAlive() != 0; }
    // AnalyticsManager::shutdown() — drain the queue and JOIN the worker. In a
    // shipping build the orchestrated Shutdown() does this; here nothing does.
    void analyticsShutdown() { if (m_anShutdown) m_anShutdown(); }

    // Post a director camera-role request (SpectateHandler::CameraRole as int).
    void requestSpectateCamera(int role) { if (m_reqCamera) m_reqCamera(role); }
    bool manualCameraActive() { return m_manualCam && m_manualCam() != 0; }
    void resetCameraTracking() { if (m_resetCamTrack) m_resetCamTrack(); }

    void draw() { drawWithState(1); }

    // Draw with an explicit view state (0 = ON_TRACK, 1 = SPECTATE, 2 = REPLAY).
    // Everything else here uses SPECTATE, which is what the driving helpers want;
    // reach for this when the behaviour under test is gated on the view itself
    // (e.g. RaceVehicleData standing down while the player is on track).
    void drawWithState(int state) {
        if (!m_draw) return;
        int nq = 0, ns = 0; void* q = nullptr; void* s = nullptr;
        m_draw(state, &nq, &q, &ns, &s);
        m_lastGameQuads = nq; m_lastGameStrings = ns;   // what draw() EMITTED to the game
    }
    // Quad/string counts the last draw() emitted to the game surface (0 when the
    // in-game HUD is suppressed in COMPANION mode). Distinct from the raw game
    // frame in getGameQuads() (which is always the full frame).
    int lastGameQuads() const { return m_lastGameQuads; }
    int lastGameStrings() const { return m_lastGameStrings; }

    // RunInit: player session start (feeds stats session timers). session matches
    // the RaceSession enum (6=Race1).
    void runInit(int session) {
        SPluginsBikeSession_t s{};
        s.m_iSession = session;
        if (m_runInit) m_runInit(&s, (int)sizeof(s));
    }
    void runDeinit() { if (m_runDeinit) m_runDeinit(); }
    // RunStart/RunStop: sim resume / pause (RunStart sets the isPlayerRunning
    // flag that gates FmxManager's telemetry processing; RunStop is the pit
    // leave-track flush).
    void runStart() { if (m_runStart) m_runStart(); }
    void runStop()  { if (m_runStop) m_runStop(); }
    // RunTelemetry: one telemetry frame. speedMs (m/s) feeds stats top-speed and,
    // integrated over the WALL-CLOCK gap between calls, the odometer/distance;
    // gear feeds the shift counter. time/pos are the extra RunTelemetry args
    // (session time, centerline position). Top speed is captured on a single call;
    // distance needs two calls with a time delta (real, or injected via
    // statsSetNowUs) and speed >= 0.1 m/s.
    void telemetry(float speedMs, int gear = 3, float time = 0.0f, float pos = 0.0f) {
        SPluginsBikeData_t d{};
        d.m_fSpeedometer = speedMs;
        d.m_iGear = gear;
        if (m_telemetry) m_telemetry(&d, (int)sizeof(d), time, pos);
    }
    // RunTelemetry with the orientation/contact/rumble fields the FMX trick
    // detection and the rumble effect engine read (see TelemetryRow). Wheel
    // speeds default to mirroring the vehicle speed so a grounded, rolling
    // frame carries no burnout slip / lockup.
    void telemetryFrame(const TelemetryRow& r) {
        SPluginsBikeData_t d{};
        d.m_fSpeedometer = r.speed;
        d.m_iGear = r.gear;
        d.m_fPosX = r.posX; d.m_fPosY = r.posY; d.m_fPosZ = r.posZ;
        d.m_fYaw = r.yaw; d.m_fPitch = r.pitch; d.m_fRoll = r.roll;
        d.m_fYawVelocity = r.yawVel; d.m_fPitchVelocity = r.pitchVel; d.m_fRollVelocity = r.rollVel;
        d.m_aiWheelMaterial[0] = r.frontMaterial;
        d.m_aiWheelMaterial[1] = r.rearMaterial;
        d.m_afWheelSpeed[0] = std::isnan(r.wheelSpeedFront) ? r.speed : r.wheelSpeedFront;
        d.m_afWheelSpeed[1] = std::isnan(r.wheelSpeedRear)  ? r.speed : r.wheelSpeedRear;
        d.m_afSuspVelocity[0] = r.suspVelFront;
        d.m_afSuspVelocity[1] = r.suspVelRear;
        d.m_iRPM = r.rpm;
        d.m_fThrottle = r.throttle;
        d.m_fSteerTorque = r.steerTorque;
        d.m_iCrashed = r.crashed;
        d.m_fClutch = r.clutch;
        if (m_telemetry) m_telemetry(&d, (int)sizeof(d), r.time, r.trackPos);
    }

    // --- replay a recorded callback tape ------------------------------------
    // Read a MXBHREC tape (the recorder format) and dispatch each recorded
    // callback into the plugin's real exports — the same events the game sent,
    // headless. Handles the snapshot-affecting inputs (event/session/entries/
    // classification/lap/track-position/communication/session-state/draw) plus
    // holeshot (recorded but no-op in the plugin); other recorded types (telemetry,
    // splits, etc.) are skipped. Call startup()
    // yourself first (Startup in the tape, if any, is ignored). Returns the count
    // of events applied, or -1 if the file is missing/not a tape.
    int replayTape(const std::string& path) {
        FILE* f = fopen(path.c_str(), "rb");
        if (!f) { HOST_TRACE("replayTape: cannot open %s", path.c_str()); return -1; }
        tape::FileHeader fh{};
        if (fread(&fh, sizeof(fh), 1, f) != 1 || std::memcmp(fh.magic, "MXBHREC", 7) != 0) {
            HOST_TRACE("replayTape: %s is not a MXBHREC tape", path.c_str());
            fclose(f); return -1;
        }
        int applied = 0;
        tape::EventHeader eh{};
        std::vector<uint8_t> buf;
        while (fread(&eh, sizeof(eh), 1, f) == 1) {
            buf.resize(eh.dataSize);
            if (eh.dataSize && fread(buf.data(), 1, eh.dataSize, f) != eh.dataSize) break;
            if (dispatch(static_cast<tape::EventType>(eh.eventType), buf)) ++applied;
        }
        fclose(f);
        return applied;
    }

    // Timestamp-driven replay for broadcast measurement. Same event dispatch as
    // replayTape(), but before each event it feeds the recorded timestamp into the
    // director's injectable clock (MXBMRP3_Test_DirectorSetNowMs) so the director's
    // wall-clock pacing (min/max shot, holds, variety cadence) plays out at the real
    // recorded cadence instead of collapsing into the few real milliseconds a naive
    // replay takes. The director must be enabled and the draw-state spectating first
    // (see the test). Each cut it makes is logged by cutTo(); parse the plugin log to
    // reconstruct the broadcast. Restores the real clock on exit. Returns events applied.
    // drawTickMs > 0 interleaves synthetic Draw() calls at that sim-time cadence
    // between recorded events. The real plugin pumps Draw every frame (which drives
    // the director's per-frame pacing pump, pollPacing), but a slimmed tape often
    // carries NO Draw events, so without this the director is only ticked by the
    // recorded data callbacks — under-driving the wall-clock pacing exactly where a
    // live game would keep enforcing the max-shot cap during a data lull. Pass e.g.
    // 100 (10 Hz) for a broadcast-faithful replay; 0 (default) keeps the original
    // data-only behavior the existing director_broadcast_test relies on.
    int replayTapeTimed(const std::string& path, long long drawTickMs = 0) {
        FILE* f = fopen(path.c_str(), "rb");
        if (!f) { HOST_TRACE("replayTapeTimed: cannot open %s", path.c_str()); return -1; }
        tape::FileHeader fh{};
        if (fread(&fh, sizeof(fh), 1, f) != 1 || std::memcmp(fh.magic, "MXBHREC", 7) != 0) {
            HOST_TRACE("replayTapeTimed: %s is not a MXBHREC tape", path.c_str());
            fclose(f); return -1;
        }
        int applied = 0;
        long long lastTickMs = -1;
        tape::EventHeader eh{};
        std::vector<uint8_t> buf;
        while (fread(&eh, sizeof(eh), 1, f) == 1) {
            buf.resize(eh.dataSize);
            if (eh.dataSize && fread(buf.data(), 1, eh.dataSize, f) != eh.dataSize) break;
            const long long evMs = static_cast<long long>(eh.timestampUs / 1000);
            // Pump Draw ticks up to this event's time so the director's wall-clock
            // pacing advances even across a stretch with no data events.
            if (drawTickMs > 0) {
                if (lastTickMs < 0) lastTickMs = evMs;
                for (long long t = lastTickMs + drawTickMs; t <= evMs; t += drawTickMs) {
                    if (m_dirSetNowMs) m_dirSetNowMs(t);
                    draw();
                    lastTickMs = t;
                }
            }
            m_lastReplayTimeMs = evMs;
            if (m_dirSetNowMs) m_dirSetNowMs(m_lastReplayTimeMs);
            if (dispatch(static_cast<tape::EventType>(eh.eventType), buf)) ++applied;
        }
        fclose(f);
        if (m_dirSetNowMs) m_dirSetNowMs(-1);   // restore the real clock
        return applied;
    }
    // Sim time (ms) of the last event fed by replayTapeTimed() — i.e. the tape's end,
    // used to attribute screen time to the final shot (which has no following cut).
    long long lastReplayTimeMs() const { return m_lastReplayTimeMs; }

    // --- settings actions (test hooks) --------------------------------------
    void resetAll() { if (m_resetAll) m_resetAll(); }
    // Reset the ACTIVE profile / one HUD to factory defaults (do NOT persist on
    // their own — follow with save()). switchProfile/copyProfileToAll persist.
    void resetActiveProfile() { if (m_resetActiveProfile) m_resetActiveProfile(); }
    void resetHud(const char* name, bool keepVisibility) { if (m_resetHud) m_resetHud(name, keepVisibility ? 1 : 0); }
    void copyProfileToAll() { if (m_copyProfileToAll) m_copyProfileToAll(); }
    void switchProfile(int idx) { if (m_switchProfile) m_switchProfile(idx); }
    // Auto-by-session profile switching: arm the flag, then read which profile the
    // game state resolved to (0=Practice,1=Qualify,2=Race,3=Spectate).
    void setAutoSwitch(bool on) { if (m_setAutoSwitch) m_setAutoSwitch(on ? 1 : 0); }
    int activeProfile() { return m_getActiveProfile ? m_getActiveProfile() : -1; }
    // Auto-director rider lock: enable the director, toggle the lock, read it.
    void directorSetEnabled(bool on) { if (m_dirSetEnabled) m_dirSetEnabled(on ? 1 : 0); }
    void directorToggleLock() { if (m_dirToggleLock) m_dirToggleLock(); }
    bool directorIsLocked() { return m_dirIsLocked && m_dirIsLocked() != 0; }
    int directorNextLockedCamera(int cur) { return m_dirNextLockedCam ? m_dirNextLockedCam(cur) : -1; }
    // Set the director's story-follow toggles (bitmask: 1=battles 2=incidents
    // 4=fastestLap 8=pace 16=lappers 32=drops) so one tape can be replayed under
    // different story configs for broadcast comparison.
    void directorSetStories(int mask) { if (m_dirSetStories) m_dirSetStories(mask); }
    // Shot pacing in seconds. maxSec = 0 is "Max shot = Off" — forced rotation disabled,
    // so the director cuts only for stories and returns to the broadcaster's own rider.
    void directorSetShotSec(int minSec, int maxSec) { if (m_dirSetShotSec) m_dirSetShotSec(minSec, maxSec); }
    // The broadcaster's "home" rider (-1 until adopted), and the director's raw current
    // subject (unlike /api/state's advisory, not blanked while paused/held).
    int directorHomeSubject() { return m_dirHomeSubject ? m_dirHomeSubject() : -2; }
    int directorSubject() { return m_dirSubject ? m_dirSubject() : -2; }
    // Benchmark profiler registry sizes. Registration is process-lifetime (HUDs are
    // registered once in HudManager::initialize()), so these must survive a session
    // teardown — a drop to 0 silently empties the profiler's per-HUD table.
    int benchmarkHudCount() { return m_benchHudCount ? m_benchHudCount() : -1; }
    int benchmarkCallbackCount() { return m_benchCbCount ? m_benchCbCount() : -1; }
    // Inject the director's simulated wall-clock (ms); -1 restores the real clock. Lets a
    // hand-built scenario drive the director's pacing at chosen times. (replayTapeTimed
    // feeds this from tape timestamps.)
    void directorSetNowMs(long long ms) { if (m_dirSetNowMs) m_dirSetNowMs(ms); }
    // Toggle the "Director" event-log type (the emission gate for director-cut events).
    void eventLogEnableDirector(bool on) { if (m_eventLogEnableDirector) m_eventLogEnableDirector(on ? 1 : 0); }
    void eventLogSetVisible(bool on) { if (m_eventLogSetVisible) m_eventLogSetVisible(on ? 1 : 0); }
    // Icon-color-slot override of the newest event-log entry matching `messageSubstr`:
    // a ColorSlot (>=0) if overridden, -1 if using the type default, -2 if not found.
    int eventLogIconColorSlot(const char* messageSubstr) {
        return m_eventLogIconColorSlot ? m_eventLogIconColorSlot(messageSubstr) : -2;
    }
    void noticesSetVisible(bool on) { if (m_noticesSetVisible) m_noticesSetVisible(on ? 1 : 0); }
    // Configure the Timing HUD (primary gap + secondary-chip bitmask) for a demo screenshot.
    void timingConfig(bool gapEnabled, int primaryGap, int secondaryMask) {
        if (m_timingConfig) m_timingConfig(gapEnabled ? 1 : 0, primaryGap, secondaryMask);
    }
    // Timing HUD reference (ms) for a gap type at a split boundary (-1 = full lap; -999 = live
    // sector). -1 if the reference is unavailable. See MXBMRP3_Test_TimingReferenceMs.
    int timingReferenceMs(int gapFlag, int targetSplit) {
        return m_timingReferenceMs ? m_timingReferenceMs(gapFlag, targetSplit) : -1;
    }
    // The split boundary the live Timing HUD reference is tracking (currentTargetSplit()).
    int timingTargetSplit() { return m_timingTargetSplit ? m_timingTargetSplit() : -2; }
    // Whether the Timing HUD time cell currently shows "INVALID". See MXBMRP3_Test_TimingInvalidShown.
    bool timingInvalidShown() { return m_timingInvalidShown && m_timingInvalidShown() != 0; }
    // Whether the Timing HUD is currently holding a frozen official split/lap time. See MXBMRP3_Test_TimingFrozen.
    bool timingFrozen() { return m_timingFrozen && m_timingFrozen() != 0; }
    // Display rider's live elapsed lap time (ms); -1 = placeholder (no anchor). See
    // MXBMRP3_Test_ElapsedLapTime.
    int elapsedLapTime() { return m_elapsedLapTime ? m_elapsedLapTime() : -2; }
    // Whether the lap timer is in the grid-start grace window (anchored at the green flag).
    // See MXBMRP3_Test_LapTimerFromRaceStart.
    bool lapTimerFromRaceStart() { return m_lapTimerFromRaceStart && m_lapTimerFromRaceStart() != 0; }
    // Whether the display rider is in the grid-start grace (green flag -> first split). See
    // MXBMRP3_Test_InGridStartGrace. Drives the wrong-way + grid-hazard suppression.
    bool inGridStartGrace() { return m_inGridStartGrace && m_inGridStartGrace() != 0; }
    // Rendered Timing panel geometry (each value ×1e6). See MXBMRP3_Test_TimingGeometry.
    struct TimingGeom { int height, paddingV, fontLarge, fontNormal, lineLarge, lineNormal; };
    TimingGeom timingGeometry() {
        TimingGeom g{};
        if (m_timingGeometry) m_timingGeometry(&g.height, &g.paddingV, &g.fontLarge,
                                               &g.fontNormal, &g.lineLarge, &g.lineNormal);
        return g;
    }
    void save()     { if (m_save) m_save(); }
    // Mark settings dirty without writing (deferred auto-save); flush with flushIfDirty().
    bool hasMarkDirty() const { return m_markDirty && m_flushIfDirty && m_isDirty && m_setAutoSave; }
    void markDirty() { if (m_markDirty) m_markDirty(); }
    // Persist pending settings if dirty (models the leave-track flush). No-op when clean/manual.
    void flushIfDirty() { if (m_flushIfDirty) m_flushIfDirty(); }
    // Unsaved-changes state (drives the Save button); toggle Auto-Save for manual-mode tests.
    bool isDirty() { return m_isDirty && m_isDirty() != 0; }
    void setAutoSave(bool on) { if (m_setAutoSave) m_setAutoSave(on ? 1 : 0); }
    // Active settings tab (by display name) — for the persisted-tab restore test.
    void setActiveTab(const char* name) { if (m_setActiveTab) m_setActiveTab(name); }
    void showSettings(bool v = true) { if (m_showSettings) m_showSettings(v ? 1 : 0); }
    // Stepped-control click seam: count/click STEPPED_UP (up=true) / STEPPED_DOWN
    // regions on the active tab, in layout order. holdRepeats drives the accel tier.
    int steppedCount(bool up) { return m_stepCount ? m_stepCount(up ? 1 : 0) : -1; }
    bool clickStepped(int index, bool up, int holdRepeats = 0) {
        return m_stepClick && m_stepClick(index, up ? 1 : 0, holdRepeats) != 0;
    }
    // Cycle-control click seam: count/click CYCLE_UP (up=true) / CYCLE_DOWN
    // regions on the active tab, in layout order (no hold tier - cycles never
    // accelerate).
    int cycleCount(bool up) { return m_cycleCount ? m_cycleCount(up ? 1 : 0) : -1; }
    // Signature of the active settings tab's emitted click regions (see the hook).
    std::string regionSignature() {
        if (!m_regionSig) return {};
        std::vector<char> buf(16384, '\0');
        m_regionSig(buf.data(), static_cast<int>(buf.size()));
        return std::string(buf.data());
    }
    bool clickCycle(int index, bool up) {
        return m_cycleClick && m_cycleClick(index, up ? 1 : 0) != 0;
    }
    void companionWindow(bool v = true) { if (m_companion) m_companion(v ? 1 : 0); }
    // Force a fake connected controller and show the gamepad widget (preview/tests).
    void fakeGamepad(bool on = true) { if (m_fakeGamepad) m_fakeGamepad(on ? 1 : 0); }
    // Pin the active input surface: 0=Game, 1=Companion, -1=off (for previewing
    // surface-scoped rendering like the settings menu on the companion window).
    void forceActiveSurface(int surface) { if (m_forceSurface) m_forceSurface(surface); }
    void sessionChartsSetVisible(bool v) { if (m_rcSetVisible) m_rcSetVisible(v ? 1 : 0); }
    void sessionChartsSetCharts(int mask) { if (m_rcSetCharts) m_rcSetCharts(mask); }

    // --- FMX trick detection (GAME_HAS_FMX builds only) ----------------------
    // The FMX state machine runs on the wall clock; inject simulated time (µs,
    // -1 restores the real clock) before each telemetryFrame() so debounces,
    // grace and the chain window play out deterministically.
    bool hasFmx() const { return m_fmxSetNow && m_fmxState; }
    void fmxSetNowUs(long long us) { if (m_fmxSetNow) m_fmxSetNow(us); }
    // Score/chain/active-trick state (Fmx::TrickType / TrickState as ints);
    // lastTrickType = newest chained trick, or the completed/failed chain's
    // final type once the chain has been banked or lost.
    struct FmxState {
        int sessionScore = -1, tricksCompleted = -1, tricksFailed = -1;
        int chainCount = -1, chainScore = -1;
        int activeState = -1, activeType = -1, lastTrickType = -1;
    };
    FmxState fmxState() {
        FmxState s;
        if (m_fmxState) m_fmxState(&s.sessionScore, &s.tricksCompleted, &s.tricksFailed,
                                   &s.chainCount, &s.chainScore,
                                   &s.activeState, &s.activeType, &s.lastTrickType);
        return s;
    }

    // --- Stats odometer seam --------------------------------------------------
    // Inject the odometer's wall clock (µs, -1 restores the real clock) so each
    // telemetry tick's dt — and the expected integrated distance — is exact.
    bool hasStatsOdometer() const { return m_statsSetNow && m_statsOdoState && m_statsSave; }
    void statsSetNowUs(long long us) { if (m_statsSetNow) m_statsSetNow(us); }
    // Live odometer state: current bike's odometer + session trip (meters), and
    // the ~100m dirty-coalescing internals (unsaved distance, dirty flag).
    struct OdometerState { double bikeOdometer = -1, sessionTrip = -1, unsaved = -1; int dirty = -1; };
    OdometerState statsOdometerState() {
        OdometerState s;
        if (m_statsOdoState) m_statsOdoState(&s.bikeOdometer, &s.sessionTrip, &s.unsaved, &s.dirty);
        return s;
    }
    // Force a stats save (same save() as the leave-track flush; no-op when clean).
    void statsSave() { if (m_statsSave) m_statsSave(); }

    // --- Records fetch/parse seam (GAME_HAS_RECORDS_PROVIDER builds only) -----
    // See the MXBMRP3_Test_Records* hooks: canned-response parsing through the
    // REAL parse path, plus a stubbed fetch worker for the shutdown-during-fetch
    // join-contract test.
    bool hasRecords() const {
        return m_recParse && m_recCount && m_recGet && m_recSetStub
            && m_recStartFetch && m_recFetchState;
    }
    // Parse a canned response as provider (0=CBR, 1=MXB_RANKED); returns the
    // parsed record count, or -1 on a parse error (-2 = hook missing).
    // Steam friend-scan worker: lifecycle only (no Steam in a test build).
    bool steamStartWorker() { return m_steamStartWorker && m_steamStartWorker() != 0; }
    bool steamWorkerRunning() const { return m_steamWorkerAlive && m_steamWorkerAlive() != 0; }

    int recordsParse(int provider, const std::string& json) {
        return m_recParse ? m_recParse(provider, json.c_str()) : -2;
    }
    int recordsCount() { return m_recCount ? m_recCount() : -1; }
    struct RecordRow {
        bool ok = false;
        std::string rider, bike, date;
        int laptime = -1, s1 = -1, s2 = -1, s3 = -1;
    };
    RecordRow recordsGet(int index) {
        RecordRow r;
        if (!m_recGet) return r;
        char rider[128] = {0}, bike[128] = {0}, date[64] = {0};
        r.ok = m_recGet(index, rider, (int)sizeof(rider), bike, (int)sizeof(bike),
                        &r.laptime, &r.s1, &r.s2, &r.s3, date, (int)sizeof(date)) != 0;
        if (r.ok) { r.rider = rider; r.bike = bike; r.date = date; }
        return r;
    }
    // Arm the fetch-worker stub (delayMs < 0 disarms): sleep + canned response
    // through the normal completion path, no network.
    void recordsSetFetchStub(int delayMs, const std::string& response) {
        if (m_recSetStub) m_recSetStub(delayMs, response.c_str());
    }
    // Start a real fetch; true if a worker is now in flight.
    bool recordsStartFetch() { return m_recStartFetch && m_recStartFetch() != 0; }
    // Fetch state (0=IDLE, 1=FETCHING, 2=SUCCESS, 3=FETCH_ERROR; -1 = hook missing).
    int recordsFetchState() { return m_recFetchState ? m_recFetchState() : -1; }
    // Gamepad content extent inside its frame: {bottom-most Y, right-most X} as
    // fractions of the box. {-1,-1} if not rendered. A golden signature of the layout.
    struct GamepadExtent { float bottom = -1.0f, right = -1.0f; };
    GamepadExtent gamepadContentExtent() {
        GamepadExtent e;
        if (m_gamepadExtent) m_gamepadExtent(&e.bottom, &e.right);
        return e;
    }

    // Per-surface HUD decoupling: drive/read the live StandingsHud's in-game and
    // companion-surface visibility (see MXBMRP3_Test_Standings* hooks).
    bool hasCompanionDecouple() const {
        return m_stSetVisible && m_stSetCompVisible && m_stClearCompanion && m_stCompanionState;
    }
    void stSetVisible(bool v)          { if (m_stSetVisible) m_stSetVisible(v ? 1 : 0); }
    bool hasPlateHooks() const         { return m_stPlateInsetY && m_stSetOffset; }
    // Fraction of the plate height at which the race-number string sits. A POSITION,
    // so it can legitimately be slightly negative; kPlateInsetNotFound is the
    // out-of-band "no plate on that row" reading (also what a missing hook returns).
    static constexpr float kPlateInsetNotFound = -1000.0f;
    float stPlateInsetY(int row)       { return m_stPlateInsetY ? m_stPlateInsetY(row)
                                                               : kPlateInsetNotFound; }
    void stSetOffset(float x, float y) { if (m_stSetOffset) m_stSetOffset(x, y); }
    void stSetCompanionVisible(bool v) { if (m_stSetCompVisible) m_stSetCompVisible(v ? 1 : 0); }
    void stClearCompanion()            { if (m_stClearCompanion) m_stClearCompanion(); }

    // TelemetryHud surfaces + the history buffers its graphs are drawn from. The
    // depth is how a test sees whether PluginData is still ACCUMULATING while the
    // HUD is visible only on the companion window (see telemetry_companion_test).
    bool hasTelemetrySurfaces() const {
        return m_tmSetVisible && m_tmSetCompVisible && m_tmClearCompanion
            && m_tmHistoryDepth && m_tmClearHistory;
    }
    void tmSetVisible(bool v)          { if (m_tmSetVisible) m_tmSetVisible(v ? 1 : 0); }
    void tmSetCompanionVisible(bool v) { if (m_tmSetCompVisible) m_tmSetCompVisible(v ? 1 : 0); }
    void tmClearCompanion()            { if (m_tmClearCompanion) m_tmClearCompanion(); }
    int  tmHistoryDepth()              { return m_tmHistoryDepth ? m_tmHistoryDepth() : -1; }
    void tmClearHistory()              { if (m_tmClearHistory) m_tmClearHistory(); }

    // BenchmarkWidget: visibility per surface, plus whether the instrumentation
    // is actually COLLECTING. The second is the point — the widget renders on
    // either surface regardless, so only `active` distinguishes real data from an
    // empty frame (see benchmark_companion_test).
    bool hasBenchmarkSurfaces() const {
        return m_bmSetVisible && m_bmSetCompVisible && m_bmActive && m_bmExists;
    }
    bool bmExists()                    { return m_bmExists && m_bmExists() != 0; }
    void bmSetVisible(bool v)          { if (m_bmSetVisible) m_bmSetVisible(v ? 1 : 0); }
    void bmSetCompanionVisible(bool v) { if (m_bmSetCompVisible) m_bmSetCompVisible(v ? 1 : 0); }
    bool bmMetricsActive()             { return m_bmActive && m_bmActive() != 0; }

    // HelmetOverlayHud: on/off plus the state read-back. It never renders on the
    // companion, so a test asserts the companion frame is unchanged by it.
    bool hasHelmetHooks() const        { return m_helmetSetVisible && m_helmetVisible && m_helmetSetCompVis; }
    void helmetSetVisible(bool v)      { if (m_helmetSetVisible) m_helmetSetVisible(v ? 1 : 0); }
    bool helmetVisible()               { return m_helmetVisible && m_helmetVisible() != 0; }
    void helmetSetCompanionVisible(bool v) { if (m_helmetSetCompVis) m_helmetSetCompVis(v ? 1 : 0); }
    bool helmetVisibleAnySurface()     { return m_helmetAnySurface && m_helmetAnySurface() != 0; }

    // Director "Visible" row: click it through the real path, read the widget's
    // per-surface state, and choose which surface the click counts as.
    bool hasDirectorSurfaceHooks() const {
        return m_clickDirectorVis && m_dirWidgetVis && m_forceSurface;
    }
    bool clickDirectorHudVisible()     { return m_clickDirectorVis && m_clickDirectorVis() != 0; }
    struct SurfaceVis { int game = -1, companion = -1; };
    SurfaceVis directorWidgetVisibility() {
        SurfaceVis v;
        if (m_dirWidgetVis) m_dirWidgetVis(&v.game, &v.companion);
        return v;
    }
    struct SurfaceState { int configured = -1, companionVisible = -1, gameVisible = -1; };
    SurfaceState stCompanionState() {
        SurfaceState s;
        if (m_stCompanionState) m_stCompanionState(&s.configured, &s.companionVisible, &s.gameVisible);
        return s;
    }

    // Surface render routing (game vs companion frame).
    bool hasSurfaceRouting() const {
        return m_setDisplayTarget && m_getDisplayTarget && m_surfaceFrameStats
            && m_stSetCompOffset && m_companionClose;
    }
    // DisplayTarget: 0=IN_GAME, 1=COMPANION, 2=BOTH.
    void setDisplayTarget(int t) { if (m_setDisplayTarget) m_setDisplayTarget(t); }
    int  displayTarget()         { return m_getDisplayTarget ? m_getDisplayTarget() : -1; }
    void stSetCompanionOffset(float x, float y) { if (m_stSetCompOffset) m_stSetCompOffset(x, y); }
    void companionSimulateUserClose() { if (m_companionClose) m_companionClose(); }
    struct FrameStats { int gameQuads = -1, companionQuads = -1; double gameSumX = 0, companionSumX = 0; };
    FrameStats surfaceFrameStats() {
        FrameStats f;
        if (m_surfaceFrameStats)
            m_surfaceFrameStats(&f.gameQuads, &f.companionQuads, &f.gameSumX, &f.companionSumX);
        return f;
    }
    std::string activeTab() {
        if (!m_getActiveTab) return {};
        char buf[32] = {0};
        m_getActiveTab(buf, (int)sizeof(buf));
        return buf;
    }
    // Sorted section names captureToCache() produces for the current live HUDs.
    std::vector<std::string> capturedSections() {
        std::vector<std::string> out;
        if (!m_capturedSections) return out;
        char buf[2048] = {0};
        m_capturedSections(buf, (int)sizeof(buf));
        std::string s(buf), cur;
        std::istringstream in(s);
        while (std::getline(in, cur, ',')) if (!cur.empty()) out.push_back(cur);
        return out;
    }
    // --- Analytics dry-run capture seam (drives the payload build + sampling gate; no net). ---
    bool hasAnalytics() const { return m_anPrime && m_anAppStarted && m_anDrain; }
    void analyticsPrime() { if (m_anPrime) m_anPrime(); }
    void analyticsSetFullLaunch(bool full) { if (m_anSetFull) m_anSetFull(full ? 1 : 0); }
    std::string analyticsAppStarted() {
        if (!m_anAppStarted) return {};
        std::vector<char> buf(8192, 0);
        m_anAppStarted(buf.data(), (int)buf.size());
        return buf.data();
    }
    void analyticsQueueSessionEnd() { if (m_anSessionEnd) m_anSessionEnd(); }
    void analyticsQueueCustom(const char* name) { if (m_anCustom) m_anCustom(name); }
    void analyticsSeedCrash(const char* path, const char* fault, const char* code) {
        if (m_anSeedCrash) m_anSeedCrash(path, fault, code);
    }
    // Drain the pending event bodies: returns the count, and fills `text` with them joined.
    int analyticsDrainPending(std::string& text) {
        text.clear();
        if (!m_anDrain) return 0;
        std::vector<char> buf(8192, 0);
        int n = m_anDrain(buf.data(), (int)buf.size());
        text = buf.data();
        return n;
    }
    // Crash-backtrace frame resolver, formatted "module+0xoffset" (the dashboard
    // stack-frame format). Empty string if the hook isn't exported.
    std::string resolveFrame(unsigned long long addr) {
        if (!m_resolveFrame) return {};
        char buf[128] = {0};
        m_resolveFrame(addr, buf, (int)sizeof(buf));
        return buf;
    }
    // Run the update extract/install pipeline against destDir with an in-memory
    // zip (bypasses the network download). 1=success, 0=failed, -1=hook missing;
    // err receives the plugin's error message.
    int extractAndInstall(const char* destDir, const std::string& zip, std::string& err) {
        if (!m_extractInstall) return -1;
        char buf[256] = {0};
        int r = m_extractInstall(destDir, zip.data(), (int)zip.size(), buf, (int)sizeof(buf));
        err = buf;
        return r;
    }
    // (Re)load settings from savePath into live state (reads <savePath>mxbmrp3\...ini).
    void loadSettings(const char* savePath) { if (m_loadSettings) m_loadSettings(savePath); }

    // --- experimental plugin worker thread -----------------------------------
    // Turn on the off-thread callback/render path (see core/plugin_thread.*),
    // check it, block until the worker has drained all queued callbacks, or stop it.
    // A threaded-mode test drives callbacks as usual, then flush()es before reading
    // snapshot() so the assertion sees fully-applied state.
    void pluginThreadEnable() { if (m_ptEnable) m_ptEnable(); }
    bool pluginThreadEnabled() { return m_ptEnabled && m_ptEnabled() != 0; }
    void pluginThreadFlush() { if (m_ptFlush) m_ptFlush(); }
    void pluginThreadStop() { if (m_ptStop) m_ptStop(); }
    // Flip ONLY the [Advanced] flag, as a live INI reload would; the next draw()'s
    // reconcileEnabled() starts/stops the worker to match (the RELOAD_CONFIG path).
    void setPluginThreadFlag(bool on) { if (m_setPtFlag) m_setPtFlag(on ? 1 : 0); }
    // Fault injection: kill the worker with an escaping exception (see the abort
    // self-heal test). Returns false if the hook isn't exported.
    bool pluginThreadAbortWorker() {
        if (!m_ptAbort) return false;
        m_ptAbort();
        return true;
    }
    // Fault injection: while on, the worker discards each batch unrun (see the
    // flush-bound test). Returns false if the hook isn't exported.
    bool pluginThreadSwallowBatches(bool on) {
        if (!m_ptSwallow) return false;
        m_ptSwallow(on ? 1 : 0);
        return true;
    }

    // XInput I/O-thread test seam: stop the worker, then drive/inspect the rumble
    // command setVibration posts (proves the send policy survives off-threading).
    void xinputStopIo() { if (m_xiStopIo) m_xiStopIo(); }
    void xinputSetIndex(int idx) { if (m_xiSetIndex) m_xiSetIndex(idx); }
    void xinputVibrate(float l, float r) { if (m_xiVibrate) m_xiVibrate(l, r); }
    struct RumblePost { bool posted = false; int left8 = 0, right8 = 0, idx = 0; };
    RumblePost xinputConsumePending() {
        RumblePost p;
        if (m_xiConsume) p.posted = m_xiConsume(&p.left8, &p.right8, &p.idx) != 0;
        return p;
    }

    // --- Rumble effect math seam (see MXBMRP3_Test_Rumble* hooks) -------------
    // The per-channel effect contributions + combined motor values that
    // updateRumbleFromTelemetry() computed for the LAST telemetry frame —
    // in-game-only state (the rumble graph / motor feed), never in /api/state.
    bool hasRumbleMath() const {
        return m_ruSetPerBike && m_ruSetEnabled && m_ruLoad && m_ruHasProfile && m_ruChannels;
    }
    void rumbleSetPerBike(bool on) { if (m_ruSetPerBike) m_ruSetPerBike(on ? 1 : 0); }
    // The ACTIVE rumble config's Bumps light strength (global, or the current
    // bike's profile in per-bike mode) - see MXBMRP3_Test_RumbleActiveBumpsLight.
    float rumbleActiveBumpsLight() { return m_ruBumpsLight ? m_ruBumpsLight() : -1.0f; }
    // Master enable: off (default) still computes every channel but feeds the
    // motors 0 — turn on before asserting the combined heavy/light values.
    void rumbleSetEnabled(bool on) { if (m_ruSetEnabled) m_ruSetEnabled(on ? 1 : 0); }
    // (Re)load the per-bike profile JSON from savePath — same parse as startup,
    // so a test can rewrite the file and reload without a plugin restart.
    void rumbleLoadProfiles(const char* savePath) { if (m_ruLoad) m_ruLoad(savePath); }
    bool rumbleHasProfile() { return m_ruHasProfile && m_ruHasProfile() != 0; }
    struct RumbleChannels {
        float heavy = -1, light = -1;              // combined motors (pre-quantization)
        float susp = -1, suspRear = -1, spin = -1, lock = -1, lockRear = -1,
              wheelie = -1, rpm = -1, slide = -1, surface = -1, steer = -1;
    };
    RumbleChannels rumbleChannels() {
        RumbleChannels c;
        if (m_ruChannels)
            m_ruChannels(&c.heavy, &c.light, &c.susp, &c.suspRear, &c.spin, &c.lock,
                         &c.lockRear, &c.wheelie, &c.rpm, &c.slide, &c.surface, &c.steer);
        return c;
    }
    // Inject an artificial per-frame render-build stall (ms), simulating a slow HUD.
    void setProduceDelayMs(int ms) { if (m_setProduceDelay) m_setProduceDelay(ms); }
    // Read the live PerformanceHud metrics (fps / plugin ms / plugin %).
    struct DebugMetrics { float fps = 0, pluginMs = 0, pct = 0; };
    DebugMetrics debugMetrics() {
        DebugMetrics d;
        if (m_getDebugMetrics) m_getDebugMetrics(&d.fps, &d.pluginMs, &d.pct);
        return d;
    }

    // --- reading the plugin's own state --------------------------------------
    // Preferred for plugin-logic tests: build the snapshot directly (no HTTP
    // server, no socket, no rebuild-gating). Isolates the plugin from the
    // serving layer — see the note in TESTING.md. No startHttp() needed.
    json snapshot() {
        if (!m_snapshot) return json();
        const char* s = m_snapshot();
        return (s && *s) ? json::parse(s, nullptr, /*allow_exceptions=*/false) : json();
    }
    // The same snapshot as raw text. Use when the assertion is "nothing at all
    // changed" — comparing the strings catches a field a parsed comparison would
    // need to know about in advance.
    std::string rawSnapshot() {
        if (!m_snapshot) return std::string();
        const char* s = m_snapshot();
        return s ? std::string(s) : std::string();
    }

    // Via the real HTTP server + socket (needs startHttp()). Reserve for the
    // contract test that the server actually serves what the plugin builds.
    std::string rawState() { return httpGet("127.0.0.1", 8080, "/api/state"); }
    // Any path, body only / full response (status line + headers + body). The
    // full variant exists for header assertions (e.g. Cache-Control on the
    // custom-served /sw.js and /custom.css).
    std::string rawGet(const char* path) { return httpGet("127.0.0.1", 8080, path); }
    std::string rawGetFull(const char* path) { return httpGetFull("127.0.0.1", 8080, path); }
    json state() {
        std::string body = rawState();
        return body.empty() ? json() : json::parse(body, nullptr, /*allow_exceptions=*/false);
    }

    // --- raw socket, for HTTP robustness (slow-loris / malformed) tests -------
    // Connect to the server and send `payload` (a partial or malformed request),
    // returning the still-open socket so the caller can hold it — simulating a
    // client that never finishes. Returns INVALID_SOCKET on failure. rawClose()
    // frees it. WSAStartup is refcounted, so pairing connect/close is safe.
    uintptr_t rawConnectSend(const char* payload, int len) {
        WSADATA w; if (WSAStartup(MAKEWORD(2, 2), &w) != 0) return (uintptr_t)INVALID_SOCKET;
        SOCKET s = socket(AF_INET, SOCK_STREAM, 0);
        sockaddr_in a{}; a.sin_family = AF_INET; a.sin_port = htons(8080);
        inet_pton(AF_INET, "127.0.0.1", &a.sin_addr);
        if (connect(s, (sockaddr*)&a, sizeof(a)) != 0) { closesocket(s); WSACleanup(); return (uintptr_t)INVALID_SOCKET; }
        if (len > 0) send(s, payload, len, 0);
        return (uintptr_t)s;
    }
    void rawClose(uintptr_t s) {
        if ((SOCKET)s != INVALID_SOCKET) { closesocket((SOCKET)s); WSACleanup(); }
    }
    static bool rawValid(uintptr_t s) { return (SOCKET)s != INVALID_SOCKET; }

private:
    // Dispatch one recorded tape event into the matching real export. Returns
    // true if applied, false if the type is skipped (unhandled).
    bool dispatch(tape::EventType t, std::vector<uint8_t>& buf) {
        using ET = tape::EventType;
        void* p = buf.data();
        int size = (int)buf.size();
        switch (t) {
            case ET::EventInit:       if (m_eventInit) { m_eventInit(p, size); return true; } break;
            case ET::RaceEvent:       if (m_raceEvent) { m_raceEvent(p, size); return true; } break;
            case ET::RaceSession:     if (m_session)   { m_session(p, size);   return true; } break;
            case ET::RaceSessionState:if (m_sessionState){ m_sessionState(p, size); return true; } break;
            case ET::RaceAddEntry:    if (m_addEntry)  { m_addEntry(p, size);  return true; } break;
            case ET::RaceRemoveEntry: if (m_removeEntry){ m_removeEntry(p, size); return true; } break;
            case ET::RaceLap:         if (m_raceLap)   { m_raceLap(p, size);   return true; } break;
            case ET::RaceHoleshot:    if (m_holeshot)  { m_holeshot(p, size);  return true; } break;
            case ET::RaceCommunication:if (m_comm)     { m_comm(p, size);      return true; } break;
            case ET::Draw:            draw(); return true;
            case ET::RaceClassification: {
                if (!m_classify || buf.size() < sizeof(tape::ClassificationPrefix)) break;
                auto* pre = reinterpret_cast<tape::ClassificationPrefix*>(p);
                void* entries = buf.data() + sizeof(tape::ClassificationPrefix);
                m_classify(&pre->header, (int)sizeof(SPluginsRaceClassification_t),
                           entries, (int)sizeof(SPluginsRaceClassificationEntry_t));
                return true;
            }
            case ET::RaceTrackPosition: {
                if (!m_trackPos || buf.size() < sizeof(tape::TrackPositionPrefix)) break;
                auto* pre = reinterpret_cast<tape::TrackPositionPrefix*>(p);
                void* positions = buf.data() + sizeof(tape::TrackPositionPrefix);
                m_trackPos(pre->numVehicles, positions, (int)sizeof(SPluginsRaceTrackPosition_t));
                return true;
            }
            // Recorder format (event_recorder.cpp): [int numSegments][SPluginsTrackSegment_t...];
            // raceData is intentionally not recorded, so replay passes null. Feeds MapHud
            // the track geometry — without it a replayed tape's map is empty even with positions.
            case ET::TrackCenterline: {
                if (!m_trackCenter || buf.size() < sizeof(int)) break;
                int numSegments = *reinterpret_cast<int*>(p);
                void* segs = (buf.size() > sizeof(int)) ? (buf.data() + sizeof(int)) : nullptr;
                m_trackCenter(numSegments, segs, nullptr);
                return true;
            }
            // Recorder format: [SPluginsBikeData_t][float time][float pos]. Feeds the
            // telemetry-driven HUDs (speedo/tacho/gear/lean/g-force/fuel/stats).
            case ET::RunTelemetry: {
                const size_t need = sizeof(SPluginsBikeData_t) + 2 * sizeof(float);
                if (!m_telemetry || buf.size() < need) break;
                float time = *reinterpret_cast<float*>(buf.data() + sizeof(SPluginsBikeData_t));
                float pos  = *reinterpret_cast<float*>(buf.data() + sizeof(SPluginsBikeData_t) + sizeof(float));
                m_telemetry(p, (int)sizeof(SPluginsBikeData_t), time, pos);
                return true;
            }
            default: break;   // Startup/Shutdown/Run*/splits/etc. — skipped
        }
        return false;
    }

    // dst is always a fixed char[N] field; copy up to N-1 and NUL-terminate.
    template <size_t N>
    static void setStr(char (&dst)[N], const char* src) {
        std::strncpy(dst, src ? src : "", N - 1); dst[N - 1] = '\0';
    }

    // Minimal blocking HTTP GET; returns the FULL response (status line +
    // headers + body), or "".
    static std::string httpGetFull(const char* host, int port, const char* path) {
        WSADATA w; if (WSAStartup(MAKEWORD(2, 2), &w) != 0) return "";
        SOCKET s = socket(AF_INET, SOCK_STREAM, 0);
        sockaddr_in a{}; a.sin_family = AF_INET; a.sin_port = htons((u_short)port);
        inet_pton(AF_INET, host, &a.sin_addr);
        if (connect(s, (sockaddr*)&a, sizeof(a)) != 0) { closesocket(s); WSACleanup(); return ""; }
        char req[256];
        int n = snprintf(req, sizeof(req),
                         "GET %s HTTP/1.0\r\nHost: %s\r\nConnection: close\r\n\r\n", path, host);
        send(s, req, n, 0);
        std::string resp; char buf[8192]; int r;
        while ((r = recv(s, buf, sizeof(buf), 0)) > 0) resp.append(buf, r);
        closesocket(s); WSACleanup();
        return resp;
    }

    // As above, but with headers stripped: body only, or "".
    static std::string httpGet(const char* host, int port, const char* path) {
        std::string resp = httpGetFull(host, port, path);
        size_t hdr = resp.find("\r\n\r\n");
        return hdr == std::string::npos ? resp : resp.substr(hdr + 4);
    }

    HMODULE      m_h = nullptr;
    PFN_Startup  m_startup = nullptr;
    PFN_Shutdown m_shutdown = nullptr;
    PFN_Void_DS  m_eventInit = nullptr, m_raceEvent = nullptr, m_session = nullptr,
                 m_sessionState = nullptr, m_addEntry = nullptr, m_removeEntry = nullptr,
                 m_comm = nullptr, m_raceLap = nullptr, m_raceSplit = nullptr,
                 m_holeshot = nullptr;
    PFN_Class    m_classify = nullptr;
    PFN_CountArray m_trackPos = nullptr;
    PFN_TrackCenter m_trackCenter = nullptr;
    PFN_Spectate m_spectate = nullptr;
    PFN_Cameras  m_cameras = nullptr;
    PFN_Shutdown m_eventDeinit = nullptr, m_raceDeinit = nullptr;
    PFN_Void_DS  m_raceVehicleData = nullptr, m_runSplit = nullptr;
    void (*m_bikeTelemetry)(float*,int*,int*,float*,float*,float*,int*) = nullptr;
    int  (*m_curSplits)(int,int*,int*,int*,int*) = nullptr;
    int  (*m_anStartWorker)() = nullptr;
    int  (*m_anWorkerAlive)() = nullptr;
    void (*m_anShutdown)() = nullptr;
    void (*m_reqCamera)(int) = nullptr;
    int  (*m_manualCam)() = nullptr;
    void (*m_resetCamTrack)() = nullptr;
    PFN_Void_DS  m_runInit = nullptr;
    PFN_Shutdown m_runDeinit = nullptr;
    PFN_Shutdown m_runStart = nullptr, m_runStop = nullptr;
    PFN_Telemetry m_telemetry = nullptr;
    int         (*m_getRTG)(int) = nullptr;
    int         (*m_hasATP)(int) = nullptr;
    int         (*m_hazCount)() = nullptr;
    int         (*m_takeATPB)() = nullptr;
    int         (*m_spectatable)(int) = nullptr;
    int         (*m_evtRegions)() = nullptr;
    void        (*m_evtAutoHide)(int,int) = nullptr;
    void        (*m_chartSeries)(int,int*) = nullptr;
    int         (*m_blueFlag)(int) = nullptr;
    int         (*m_lapping)(int) = nullptr;
    int         (*m_lapTarget)(int) = nullptr;
    PFN_Draw     m_draw = nullptr;
    void        (*m_startHttp)() = nullptr;
    const char* (*m_snapshot)() = nullptr;
    unsigned long long (*m_snapshotSeq)() = nullptr;
    void        (*m_ptEnable)() = nullptr;
    int         (*m_ptEnabled)() = nullptr;
    void        (*m_ptFlush)() = nullptr;
    void        (*m_ptStop)() = nullptr;
    void        (*m_ptAbort)() = nullptr;
    void        (*m_ptSwallow)(int) = nullptr;
    void        (*m_setProduceDelay)(int) = nullptr;
    void        (*m_getDebugMetrics)(float*,float*,float*) = nullptr;
    void        (*m_setPtFlag)(int) = nullptr;
    void        (*m_xiStopIo)() = nullptr;
    void        (*m_xiSetIndex)(int) = nullptr;
    void        (*m_xiVibrate)(float,float) = nullptr;
    int         (*m_xiConsume)(int*,int*,int*) = nullptr;
    void        (*m_ruSetPerBike)(int) = nullptr;
    void        (*m_ruSetEnabled)(int) = nullptr;
    void        (*m_ruLoad)(const char*) = nullptr;
    int         (*m_ruHasProfile)() = nullptr;
    void        (*m_ruChannels)(float*,float*,float*,float*,float*,float*,float*,float*,float*,float*,float*,float*) = nullptr;
    int         (*m_startRec)(const char*) = nullptr;
    void        (*m_stopRec)() = nullptr;
    void        (*m_resetAll)() = nullptr;
    void        (*m_resetActiveProfile)() = nullptr;
    void        (*m_resetHud)(const char*, int) = nullptr;
    void        (*m_copyProfileToAll)() = nullptr;
    void        (*m_switchProfile)(int) = nullptr;
    void        (*m_setAutoSwitch)(int) = nullptr;
    int         (*m_getActiveProfile)() = nullptr;
    void        (*m_dirSetEnabled)(int) = nullptr;
    void        (*m_dirToggleLock)() = nullptr;
    int         (*m_dirIsLocked)() = nullptr;
    int         (*m_dirNextLockedCam)(int) = nullptr;
    void        (*m_dirSetNowMs)(long long) = nullptr;
    void        (*m_eventLogEnableDirector)(int) = nullptr;
    void        (*m_timingConfig)(int,int,int) = nullptr;
    int         (*m_timingReferenceMs)(int,int) = nullptr;
    int         (*m_timingTargetSplit)() = nullptr;
    int         (*m_timingInvalidShown)() = nullptr;
    int         (*m_timingFrozen)() = nullptr;
    int         (*m_elapsedLapTime)() = nullptr;
    int         (*m_lapTimerFromRaceStart)() = nullptr;
    int         (*m_inGridStartGrace)() = nullptr;
    void        (*m_timingGeometry)(int*,int*,int*,int*,int*,int*) = nullptr;
    void        (*m_eventLogSetVisible)(int) = nullptr;
    int         (*m_eventLogIconColorSlot)(const char*) = nullptr;
    void        (*m_noticesSetVisible)(int) = nullptr;
    void        (*m_dirSetStories)(int) = nullptr;
    void        (*m_dirSetShotSec)(int, int) = nullptr;
    int         (*m_dirHomeSubject)() = nullptr;
    int         (*m_dirSubject)() = nullptr;
    int         (*m_benchHudCount)() = nullptr;
    int         (*m_benchCbCount)() = nullptr;
    long long   m_lastReplayTimeMs = 0;
    void        (*m_save)() = nullptr;
    void        (*m_markDirty)() = nullptr;
    void        (*m_flushIfDirty)() = nullptr;
    int         (*m_isDirty)() = nullptr;
    void        (*m_setAutoSave)(int) = nullptr;
    void        (*m_loadSettings)(const char*) = nullptr;
    void        (*m_setActiveTab)(const char*) = nullptr;
    int         (*m_stepCount)(int) = nullptr;
    int         (*m_stepClick)(int,int,int) = nullptr;
    bool        m_shutdownDone = false;
    bool        m_started = false;   // startup() was called at least once
    bool        m_skipShutdownOnDestroy = false;
    int         (*m_cycleCount)(int) = nullptr;
    void        (*m_regionSig)(char*,int) = nullptr;
    int         (*m_cycleClick)(int,int) = nullptr;
    float       (*m_ruBumpsLight)() = nullptr;
    void        (*m_showSettings)(int) = nullptr;
    void        (*m_companion)(int) = nullptr;
    void        (*m_stSetVisible)(int) = nullptr;
    void        (*m_stSetCompVisible)(int) = nullptr;
    float       (*m_stPlateInsetY)(int) = nullptr;
    void        (*m_stSetOffset)(float, float) = nullptr;
    void        (*m_stClearCompanion)() = nullptr;
    void        (*m_stCompanionState)(int*, int*, int*) = nullptr;
    void        (*m_tmSetVisible)(int) = nullptr;
    void        (*m_tmSetCompVisible)(int) = nullptr;
    void        (*m_bmSetVisible)(int) = nullptr;
    void        (*m_bmSetCompVisible)(int) = nullptr;
    int         (*m_bmActive)() = nullptr;
    int         (*m_bmExists)() = nullptr;
    void        (*m_helmetSetVisible)(int) = nullptr;
    int         (*m_helmetVisible)() = nullptr;
    void        (*m_helmetSetCompVis)(int) = nullptr;
    int         (*m_helmetAnySurface)() = nullptr;
    int         (*m_clickDirectorVis)() = nullptr;
    void        (*m_dirWidgetVis)(int*, int*) = nullptr;
    void        (*m_tmClearCompanion)() = nullptr;
    int         (*m_tmHistoryDepth)() = nullptr;
    void        (*m_tmClearHistory)() = nullptr;
    void        (*m_setDisplayTarget)(int) = nullptr;
    int         (*m_getDisplayTarget)() = nullptr;
    void        (*m_surfaceFrameStats)(int*, int*, double*, double*) = nullptr;
    void        (*m_stSetCompOffset)(float, float) = nullptr;
    void        (*m_companionClose)() = nullptr;
    void        (*m_fakeGamepad)(int) = nullptr;
    void        (*m_gamepadExtent)(float*, float*) = nullptr;
    void        (*m_forceSurface)(int) = nullptr;
    void        (*m_rcSetVisible)(int) = nullptr;
    void        (*m_rcSetCharts)(int) = nullptr;
    void        (*m_fmxSetNow)(long long) = nullptr;
    void        (*m_fmxState)(int*,int*,int*,int*,int*,int*,int*,int*) = nullptr;
    void        (*m_statsSetNow)(long long) = nullptr;
    void        (*m_statsOdoState)(double*,double*,double*,int*) = nullptr;
    void        (*m_statsSave)() = nullptr;
    int         (*m_steamStartWorker)() = nullptr;
    int         (*m_steamWorkerAlive)() = nullptr;
    int         (*m_recParse)(int, const char*) = nullptr;
    int         (*m_recCount)() = nullptr;
    int         (*m_recGet)(int, char*, int, char*, int, int*, int*, int*, int*, char*, int) = nullptr;
    void        (*m_recSetStub)(int, const char*) = nullptr;
    int         (*m_recStartFetch)() = nullptr;
    int         (*m_recFetchState)() = nullptr;
    int         m_lastGameQuads = 0;
    int         m_lastGameStrings = 0;
    void        (*m_getActiveTab)(char*, int) = nullptr;
    void        (*m_capturedSections)(char*, int) = nullptr;
    void        (*m_anPrime)() = nullptr;
    void        (*m_anSetFull)(int) = nullptr;
    void        (*m_anAppStarted)(char*, int) = nullptr;
    void        (*m_anSessionEnd)() = nullptr;
    void        (*m_anCustom)(const char*) = nullptr;
    void        (*m_anSeedCrash)(const char*, const char*, const char*) = nullptr;
    int         (*m_anDrain)(char*, int) = nullptr;
    void        (*m_resolveFrame)(unsigned long long, char*, int) = nullptr;
    int         (*m_extractInstall)(const char*, const char*, int, char*, int) = nullptr;
};
