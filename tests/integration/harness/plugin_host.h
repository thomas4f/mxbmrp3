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
#include <fstream>
#include <sstream>
#include <stdexcept>

// Where the shipped spotter pack lives. run_tests.sh passes an absolute Wine
// path; everything ELSE that includes this header — companion_demo, the replay
// tool, the benchmark drivers — does not, and a header that only compiles under
// one build script is a header that breaks the others silently (companion_demo
// stopped compiling for exactly this reason, and no gate builds it). The
// fallback is the staged layout those tools already run from.
#ifndef MXB_SHIPPED_PACK_INI
#define MXB_SHIPPED_PACK_INI "plugins/mxbmrp3_data/spotters/default/spotter.ini"
#endif

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
        m_resetGlobals = sym<void(*)()>("MXBMRP3_Test_ResetGlobals");
        m_spotEnable = sym<void(*)(int)>("MXBMRP3_Test_SpotterEnable");
        m_spotSubs   = sym<void(*)(int)>("MXBMRP3_Test_SpotterSubtitles");
        m_spotMask   = sym<void(*)(unsigned)>("MXBMRP3_Test_SpotterCategoryMask");
        m_spotLog    = sym<void(*)(char*,int)>("MXBMRP3_Test_SpotterCueLog");
        m_spotAudio  = sym<void(*)(char*,int)>("MXBMRP3_Test_SpotterLastAudio");
        m_spotPack   = sym<void(*)(const char*)>("MXBMRP3_Test_SpotterInstallPack");
        m_spotPrev   = sym<void(*)(int)>("MXBMRP3_Test_SpotterPreview");
        m_spotPin    = sym<void(*)(int)>("MXBMRP3_Test_SpotterPinVariant");
        m_spotHotkey = sym<void(*)()>("MXBMRP3_Test_SpotterHotkey");
        m_spotParked = sym<int(*)()>("MXBMRP3_Test_SpotterWorkerParked");
        m_stTheme   = sym<void(*)(char*,int)>("MXBMRP3_Test_StandingsTheme");
        m_stSetTheme = sym<void(*)(const char*)>("MXBMRP3_Test_StandingsSetTheme");
        m_rdTheme   = sym<void(*)(char*,int)>("MXBMRP3_Test_RadarTheme");
        m_rdSetTheme = sym<void(*)(const char*)>("MXBMRP3_Test_RadarSetTheme");
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
        m_profilableHuds = sym<int(*)()>("MXBMRP3_Test_ProfilableHudCount");
        m_snapCap  = sym<int(*)()>("MXBMRP3_Test_BenchmarkSnapshotCapacity");
        m_snapCount= sym<int(*)()>("MXBMRP3_Test_BenchmarkSnapshotCount");
        m_hudRebuilds = sym<int(*)(const char*)>("MXBMRP3_Test_HudRebuildCount");
        m_setProbe = sym<void(*)(int,int,int,int)>("MXBMRP3_Test_SetRenderProbe");
        m_spriteSpan = sym<void(*)(int*,int*,int*)>("MXBMRP3_Test_FrameSpriteSpan");
        m_spriteCount = sym<int(*)()>("MXBMRP3_Test_RegisteredSpriteCount");
        m_installSprites = sym<void(*)(int)>("MXBMRP3_Test_InstallSpriteTable");
        m_getProbe = sym<void(*)(int*,int*,int*,int*)>("MXBMRP3_Test_GetRenderProbe");
        m_sweepStart = sym<void(*)()>("MXBMRP3_Test_ProbeSweepStart");
        m_sweepAbort = sym<void(*)()>("MXBMRP3_Test_ProbeSweepAbort");
        m_sweepRunning = sym<int(*)()>("MXBMRP3_Test_ProbeSweepRunning");
        m_setProbeChars = sym<void(*)(int)>("MXBMRP3_Test_SetRenderProbeTextChars");
        m_getProbeChars = sym<int(*)()>("MXBMRP3_Test_GetRenderProbeTextChars");
        m_setDropShadow = sym<void(*)(int)>("MXBMRP3_Test_SetDropShadow");
        m_setDevMode = sym<void(*)(int)>("MXBMRP3_Test_SetDeveloperMode");
        m_setHudOpacity = sym<int(*)(const char*,float)>("MXBMRP3_Test_SetHudOpacity");
        m_setHudOffset = sym<int(*)(const char*,float,float)>("MXBMRP3_Test_SetHudOffset");
        m_maxQuadArea = sym<int(*)(const char*)>("MXBMRP3_Test_HudMaxQuadArea");
        m_eventLogEnableDirector = sym<void(*)(int)>("MXBMRP3_Test_EventLogEnableDirector");
        m_timingConfig = sym<void(*)(int,int,int)>("MXBMRP3_Test_TimingConfig");
        m_timingReadouts = sym<void(*)(unsigned int)>("MXBMRP3_Test_TimingReadouts");
        m_timingTextBudget = sym<int(*)()>("MXBMRP3_Test_TimingTextBudget");
        m_timingReferenceMs = sym<int(*)(int,int)>("MXBMRP3_Test_TimingReferenceMs");
        m_timingTargetSplit = sym<int(*)()>("MXBMRP3_Test_TimingTargetSplit");
        m_timingInvalidShown = sym<int(*)()>("MXBMRP3_Test_TimingInvalidShown");
        m_timingFrozen = sym<int(*)()>("MXBMRP3_Test_TimingFrozen");
        m_elapsedLapTime = sym<int(*)()>("MXBMRP3_Test_ElapsedLapTime");
        m_lapTimerFromRaceStart = sym<int(*)()>("MXBMRP3_Test_LapTimerFromRaceStart");
        m_inGridStartGrace = sym<int(*)()>("MXBMRP3_Test_InGridStartGrace");
        m_timingGeometry = sym<void(*)(int*,int*,int*,int*,int*,int*,int*)>("MXBMRP3_Test_TimingGeometry");
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
        m_wnReset      = sym<void(*)()>("MXBMRP3_Test_WhatsNewReset");
        m_wnLiveCount  = sym<int(*)()>("MXBMRP3_Test_WhatsNewLiveCount");
        m_wnTabTagged  = sym<int(*)(const char*)>("MXBMRP3_Test_WhatsNewTabTagged");
        m_wnHoverRow   = sym<void(*)(const char*)>("MXBMRP3_Test_WhatsNewHoverRow");
        m_wnClickTab   = sym<int(*)(const char*)>("MXBMRP3_Test_WhatsNewClickTab");
        m_wnMarkerCount    = sym<int(*)()>("MXBMRP3_Test_WhatsNewMarkerCount");
        m_wnMarkerResolves = sym<int(*)(int)>("MXBMRP3_Test_WhatsNewMarkerResolves");
        m_wnMarkerName     = sym<void(*)(int, char*, int)>("MXBMRP3_Test_WhatsNewMarkerName");
        m_wnSerialize      = sym<void(*)(char*, int)>("MXBMRP3_Test_WhatsNewSerialize");
        m_clickAbout       = sym<void(*)()>("MXBMRP3_Test_ClickAbout");
        m_updateTagLive    = sym<int(*)()>("MXBMRP3_Test_UpdateTagLive");
        m_updateTagReset   = sym<void(*)()>("MXBMRP3_Test_UpdateTagReset");
        m_aboutRect        = sym<int(*)(int*,int*,int*,int*)>("MXBMRP3_Test_AboutButtonRect");
        m_settingsTabName = sym<int(*)(int,char*,int)>("MXBMRP3_Test_SettingsTabName");
        m_settingsAnyTabName = sym<int(*)(int,char*,int)>("MXBMRP3_Test_SettingsAnyTabName");
        m_settingsOverflow = sym<int(*)()>("MXBMRP3_Test_SettingsOverflowRows");
        m_standingsRowBand = sym<int(*)(int*,int*)>("MXBMRP3_Test_StandingsRowBand");
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
        m_installTheme      = sym<void(*)(const char*,float,float,int,int,int,int)>("MXBMRP3_Test_InstallTheme");
        m_setThemeTitleBorder  = sym<int(*)(float)>("MXBMRP3_Test_SetThemeTitleBorder");
        m_setThemePanelPad  = sym<int(*)(float,float)>("MXBMRP3_Test_SetThemePanelPadding");
        m_layoutCells       = sym<void(*)(double*)>("MXBMRP3_Test_LayoutCells");
        m_setHudScale       = sym<int(*)(const char*, float)>("MXBMRP3_Test_SetHudScale");
        m_hudBgSprite       = sym<int(*)(const char*)>("MXBMRP3_Test_HudBackgroundTextureOn");
        m_gapBarWidth       = sym<void(*)(int)>("MXBMRP3_Test_GapBarWidth");
        m_setThemeContentBorder =
            sym<int(*)(float,float,float,float)>("MXBMRP3_Test_SetThemeContentBorder");
        m_setThemeContentMargin =
            sym<int(*)(float,float,float,float)>("MXBMRP3_Test_SetThemeContentMargin");
        m_setThemeTitleMargin =
            sym<int(*)(float,float,float,float)>("MXBMRP3_Test_SetThemeTitleMargin");
        m_hudCardRect = sym<int(*)(const char*,int*)>("MXBMRP3_Test_HudCardRect");
        m_gapBarForceGap = sym<void(*)(int,int)>("MXBMRP3_Test_GapBarForceGap");
        m_setThemeIcon      = sym<int(*)(const char*,int,int)>("MXBMRP3_Test_SetThemeIconOverride");
        m_iconForName       = sym<int(*)(const char*)>("MXBMRP3_Test_IconSpriteForName");
        m_iconForShape      = sym<int(*)(int)>("MXBMRP3_Test_IconSpriteForShape");
        m_shapeForIcon      = sym<int(*)(int)>("MXBMRP3_Test_ShapeForIconSprite");
        m_clearTheme        = sym<void(*)()>("MXBMRP3_Test_ClearTheme");
        m_hudPanelRect      = sym<void(*)(const char*,int*,int*,int*)>("MXBMRP3_Test_HudPanelRect");
        m_sectionCards      = sym<int(*)(const char*,int*,int)>("MXBMRP3_Test_SectionCards");
        m_panelCells        = sym<int(*)(int*,int)>("MXBMRP3_Test_PanelCells");
        m_panelPadY         = sym<int(*)(int*,int)>("MXBMRP3_Test_PanelPadY");
        m_setPerfElements   = sym<void(*)(unsigned)>("MXBMRP3_Test_SetPerformanceElements");
        m_quadRects         = sym<int(*)(const char*,int*,int)>("MXBMRP3_Test_HudQuadRects");
        m_stringColor       = sym<unsigned long(*)(const char*,int)>("MXBMRP3_Test_HudStringColor");
        m_quadColor         = sym<unsigned long(*)(const char*,int)>("MXBMRP3_Test_HudQuadColor");
        m_minLumaGap        = sym<int(*)()>("MXBMRP3_Test_MinGlyphLumaGap");
        m_luma601           = sym<int(*)(unsigned long)>("MXBMRP3_Test_Luma601");
        m_stringRows        = sym<int(*)(const char*,int,int*,int*,char*,int)>("MXBMRP3_Test_HudStringRows");
        m_fillCut           = sym<int(*)(const char*,int*,int)>("MXBMRP3_Test_HudFillCut");
        m_panelName         = sym<void(*)(int,char*,int)>("MXBMRP3_Test_PanelName");
        m_hudScreenEdges    = sym<void(*)(const char*,int*,int*,int*,int*)>("MXBMRP3_Test_HudScreenEdges");
        m_setScreenClamping = sym<void(*)(int)>("MXBMRP3_Test_SetScreenClamping");
        m_settingsMarginsX  = sym<void(*)(int*,int*,int*,int*)>("MXBMRP3_Test_SettingsMarginsX");
        m_setThemeGap       = sym<int(*)(float)>("MXBMRP3_Test_SetThemeGap");
        m_settingsGutter    = sym<void(*)(int*,int*,int*)>("MXBMRP3_Test_SettingsGutter");
        m_settingsContentX  = sym<void(*)(int*,int*,int*)>("MXBMRP3_Test_SettingsContentX");
        m_setHudTitle       = sym<int(*)(const char*,int)>("MXBMRP3_Test_SetHudTitle");
        m_updateSetAvailable = sym<void(*)(const char*)>("MXBMRP3_Test_UpdateSetAvailable");
        m_versionRowTerms   = sym<void(*)(int*,int*)>("MXBMRP3_Test_VersionRowTerms");
        m_showAllHuds       = sym<void(*)(int)>("MXBMRP3_Test_ShowAllHuds");
        m_effColor          = sym<unsigned long(*)(int)>("MXBMRP3_Test_EffectiveColor");
        m_colorOverridden   = sym<int(*)(int)>("MXBMRP3_Test_ColorOverridden");
        m_themeOrDefColor   = sym<unsigned long(*)(int)>("MXBMRP3_Test_ThemeOrDefaultColor");
        m_themeInfo         = sym<void(*)(const char*, char*, int)>("MXBMRP3_Test_ThemeInfo");
        m_gaugesInfo        = sym<void(*)(const char*, char*, int)>("MXBMRP3_Test_GaugesInfo");
        m_cycleColor        = sym<void(*)(int,int)>("MXBMRP3_Test_CycleColor");
        m_clearColorOv      = sym<void(*)(int)>("MXBMRP3_Test_ClearColorOverride");
        m_setThemeColor     = sym<void(*)(int,unsigned long)>("MXBMRP3_Test_SetThemeColor");
        m_effFont           = sym<void(*)(int,char*,int)>("MXBMRP3_Test_EffectiveFont");
        m_fontOverridden    = sym<int(*)(int)>("MXBMRP3_Test_FontOverridden");
        m_cycleFont         = sym<void(*)(int,int)>("MXBMRP3_Test_CycleFont");
        m_clearFontOv       = sym<void(*)(int)>("MXBMRP3_Test_ClearFontOverride");
        m_setFont           = sym<void(*)(int,const char*)>("MXBMRP3_Test_SetFont");
        m_fontCount         = sym<int(*)()>("MXBMRP3_Test_FontCount");
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
        m_installGamepad    = sym<void(*)(const char*, float)>("MXBMRP3_Test_InstallGamepad");
        m_clearGamepads     = sym<void(*)()>("MXBMRP3_Test_ClearGamepads");
        m_installPitboard   = sym<void(*)(const char*, float, float)>("MXBMRP3_Test_InstallPitboard");
        m_clearPitboards    = sym<void(*)()>("MXBMRP3_Test_ClearPitboards");
        m_cyclePack         = sym<void(*)(int, int)>("MXBMRP3_Test_CyclePack");
        m_packShowBg        = sym<int(*)(int)>("MXBMRP3_Test_PackShowBg");
        m_gamepadStemSrc    = sym<int(*)(const char*, int)>("MXBMRP3_Test_GamepadStemSource");
        m_gamepadGeomWidth  = sym<float(*)(const char*)>("MXBMRP3_Test_GamepadGeomWidth");
        m_pitboardStemSrc   = sym<int(*)(const char*, int)>("MXBMRP3_Test_PitboardStemSource");
        m_pitboardPackArtW  = sym<float(*)(const char*)>("MXBMRP3_Test_PitboardPackArtWidth");
        m_pitboardTextColor = sym<unsigned(*)(const char*)>("MXBMRP3_Test_PitboardTextColor");
        m_padStemCount      = sym<int(*)()>("MXBMRP3_Test_GamepadStemCount");
        m_padStemName       = sym<void(*)(int, char*, int)>("MXBMRP3_Test_GamepadStemName");
        m_spriteOrderMism   = sym<int(*)()>("MXBMRP3_Test_SpriteOrderMismatches");
        m_spriteOrderSwap   = sym<int(*)(int, int)>("MXBMRP3_Test_SpriteOrderWithSwap");
        m_setPackShowBg     = sym<void(*)(int, int)>("MXBMRP3_Test_SetPackShowBg");
        m_setPitboardPack   = sym<void(*)(const char*)>("MXBMRP3_Test_SetPitboardPack");
        m_pitboardStored    = sym<void(*)(char*, int)>("MXBMRP3_Test_PitboardPackStored");
        m_pitboardActive    = sym<void(*)(char*, int)>("MXBMRP3_Test_PitboardPackActive");
        m_setGamepadPack    = sym<void(*)(const char*)>("MXBMRP3_Test_SetGamepadPack");
        m_gamepadStored     = sym<void(*)(char*, int)>("MXBMRP3_Test_GamepadPackStored");
        m_gamepadActive     = sym<void(*)(char*, int)>("MXBMRP3_Test_GamepadPackActive");
        m_reloadAssetLayouts = sym<void(*)()>("MXBMRP3_Test_ReloadAssetLayouts");
        m_pitboardArtWidth  = sym<float(*)()>("MXBMRP3_Test_PitboardArtWidth");
        m_gamepadArtWidth   = sym<float(*)()>("MXBMRP3_Test_GamepadArtWidth");
        m_setUiFontSize     = sym<void(*)(float)>("MXBMRP3_Test_SetUiFontSize");
        m_setBoxTerm        = sym<void(*)(int,const char*)>("MXBMRP3_Test_SetBoxTerm");
        m_forceSurface      = sym<void(*)(int)>("MXBMRP3_Test_ForceActiveSurface");
        m_rcSetVisible      = sym<void(*)(int)>("MXBMRP3_Test_SessionChartsSetVisible");
        m_rcSetCharts       = sym<void(*)(int)>("MXBMRP3_Test_SessionChartsSetCharts");
        m_fmxSetNow         = sym<void(*)(long long)>("MXBMRP3_Test_FmxSetNowUs");
        m_fmxState          = sym<void(*)(int*,int*,int*,int*,int*,int*,int*,int*)>("MXBMRP3_Test_FmxState");
        m_statsSetNow       = sym<void(*)(long long)>("MXBMRP3_Test_StatsSetNowUs");
        m_statsOdoState     = sym<void(*)(double*,double*,double*,int*)>("MXBMRP3_Test_StatsOdometerState");
        m_statsSave         = sym<void(*)()>("MXBMRP3_Test_StatsSave");
        m_crashTally        = sym<int(*)(int)>("MXBMRP3_Test_CrashTally");
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
                   const char* trackId = "",
                   // 0 = offline (the default, and what the game sends in
                   // single player), 1 = online race, 2 = online practice day.
                   // It decides isOnline(), which the lap-quality ladder reads:
                   // OFFLINE a session-leading lap speaks as a session best,
                   // because fastest-of-one means nothing; ONLINE it speaks as
                   // the fastest lap. A case pinning either wording has to say
                   // which world it is in.
                   int serverType = 0) {
        SPluginsBikeEvent_t ev{};
        ev.m_iServerType = serverType;
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
    // bikeName defaults to a name deliberately OUTSIDE PluginUtils' brand map, so
    // existing tests keep the brand-less entry they were written against; pass a
    // real one (e.g. "FACTORY CRF450R") to exercise the brand/brandColor fields.
    void addEntry(int num, const char* name, const char* bikeName = "Test 450") {
        SPluginsRaceAddEntry_t e{};
        e.m_iRaceNum = num; setStr(e.m_szName, name);
        setStr(e.m_szBikeName, bikeName); setStr(e.m_szBikeShortName, "T450");
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
    // penaltySeconds rides m_iTime (the game sends seconds; the adapter
    // converts to ms) and only means anything for communication 2.
    void communication(int raceNum, int state, int communication = 1,
                       int penaltySeconds = 0) {
        SPluginsRaceCommunication_t rc{};
        rc.m_iRaceNum = raceNum; rc.m_iCommunication = communication;
        rc.m_iState = state; rc.m_iReason = 2;
        rc.m_iTime = penaltySeconds;
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

    // Real-time replay: same dispatch as replayTape(), but SLEEPS out the
    // recorded inter-event gaps (capped so dead air can't stall a test).
    // Needed whenever the behavior under test runs on the WALL clock rather
    // than callback data — wrong-way/stationary hazard confirmation
    // (steady_clock timers in updateTrackPosition) never trips in a burst
    // replay no matter what the tape contains. Cost is the tape's real
    // duration; keep such tapes short.
    int replayTapePaced(const std::string& path, int maxGapMs = 250) {
        FILE* f = fopen(path.c_str(), "rb");
        if (!f) { HOST_TRACE("replayTapePaced: cannot open %s", path.c_str()); return -1; }
        tape::FileHeader fh{};
        if (fread(&fh, sizeof(fh), 1, f) != 1 || std::memcmp(fh.magic, "MXBHREC", 7) != 0) {
            HOST_TRACE("replayTapePaced: %s is not a MXBHREC tape", path.c_str());
            fclose(f); return -1;
        }
        int applied = 0;
        uint64_t prevUs = 0;
        bool first = true;
        tape::EventHeader eh{};
        std::vector<uint8_t> buf;
        while (fread(&eh, sizeof(eh), 1, f) == 1) {
            buf.resize(eh.dataSize);
            if (eh.dataSize && fread(buf.data(), 1, eh.dataSize, f) != eh.dataSize) break;
            if (!first && eh.timestampUs > prevUs) {
                uint64_t gapMs = (eh.timestampUs - prevUs) / 1000;
                if (gapMs > (uint64_t)maxGapMs) gapMs = (uint64_t)maxGapMs;
                if (gapMs) Sleep((DWORD)gapMs);
            }
            first = false;
            prevUs = eh.timestampUs;
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
    // The GLOBAL half of "Reset Everything" (see the hook). The menu button is both
    // halves, so a test modelling that button calls resetEverything() below.
    void resetGlobals() { if (m_resetGlobals) m_resetGlobals(); }
    bool hasResetGlobals() const { return m_resetGlobals != nullptr; }
    void resetEverything() { resetGlobals(); resetAll(); }
    // Spotter: enable/filter the cue pipeline and read back what it decided
    // to say (newline-joined, oldest first). See MXBMRP3_Test_SpotterCueLog.
    void spotterEnable(bool on) { if (m_spotEnable) m_spotEnable(on ? 1 : 0); }
    void spotterSubtitles(bool on) { if (m_spotSubs) m_spotSubs(on ? 1 : 0); }
    void spotterCategoryMask(unsigned mask) { if (m_spotMask) m_spotMask(mask); }
    void spotterInstallPack(const char* iniText) { if (m_spotPack) m_spotPack(iniText); }
    // Load the SHIPPED pack's real text. The plugin carries no built-in
    // wording, so this is what makes a cue speak at all — and asserting
    // against the file users actually get beats asserting against a stand-in
    // that can quietly drift from it.
    // THROWS rather than asserting: doctest turns an escaped exception into a
    // failed test case with this message, so the tests keep their hard stop
    // while the header stays usable by the non-doctest tools that also include
    // it. (It used to call REQUIRE_MESSAGE, which only exists under doctest.)
    static std::string readShippedPack() {
        std::ifstream in(MXB_SHIPPED_PACK_INI, std::ios::binary);
        if (!in.good()) {
            throw std::runtime_error(
                std::string("shipped spotter pack missing: ")
                + MXB_SHIPPED_PACK_INI);
        }
        std::ostringstream ss;
        ss << in.rdbuf();
        return ss.str();
    }
    // Which alternate a cue with `_2..` rows speaks: -1 rolls (what ships), 0
    // always the base row. See SpotterManager::testPinVariant.
    void spotterPinVariant(int idx) { if (m_spotPin) m_spotPin(idx); }
    // BOTH shipped-pack installers pin the base row, because the reason to
    // reach for the shipped file is to assert against the words users get —
    // and the shipped pack carries alternates, so an unpinned assertion on any
    // cue with a `_2` is a one-in-N flake that only shows up on some runs.
    // Pinning here rather than at each call site is deliberate: a case added
    // later gets it without knowing this exists, which is the failure mode
    // that matters (nothing about a flaky exact-match assertion says "you
    // forgot the pin"). A test that WANTS the roll calls spotterPinVariant(-1)
    // after installing, and the two in this suite that do say so.
    void pinBaseVariant() { spotterPinVariant(0); }
    void spotterInstallShippedPack() {
        m_shippedPackText = readShippedPack();
        spotterInstallPack(m_shippedPackText.c_str());
        pinBaseVariant();
    }
    // The shipped pack with `extra` laid over it — the same shape the plugin
    // builds when a user pack overrides some of the shipped rows. What a test
    // wants when it overrides one cue and still expects the rest to speak;
    // spotterInstallPack alone is the isolated case, where anything the text
    // does not define is silent.
    void spotterInstallPackOver(const char* extra) {
        m_shippedPackText = readShippedPack() + "\n" + extra;
        spotterInstallPack(m_shippedPackText.c_str());
        pinBaseVariant();
    }
    // The settings menu's voice preview; see MXBMRP3_Test_SpotterPreview.
    void spotterPreview(bool ttsOnly = false) { if (m_spotPrev) m_spotPrev(ttsOnly ? 1 : 0); }
    // The Spotter Cue hotkey. See MXBMRP3_Test_SpotterHotkey.
    void spotterHotkey() { if (m_spotHotkey) m_spotHotkey(); }
    // Block until the TTS worker is parked in its queue wait, or `timeoutMs`
    // elapses; returns whether it parked. Call it after a cue when the NEXT
    // thing the test does must not race the worker — the first speech cue
    // lazily CoCreateInstances the engine, which loads DLLs and so needs the
    // loader lock, and how long that takes is a cold/warm Wine question rather
    // than anything the test controls (see MXBMRP3_Test_SpotterWorkerParked).
    bool spotterWaitWorkerParked(int timeoutMs = 5000) {
        if (!m_spotParked) return false;
        for (int waited = 0; waited < timeoutMs; waited += 10) {
            if (m_spotParked()) return true;
            ::Sleep(10);
        }
        return m_spotParked() != 0;
    }
    // Sized for the whole 96-entry ring rather than a round number: the hook
    // keeps the NEWEST cues when it has to cut, but a test that scrolls its
    // own assertions out of the buffer still reads as a pass on every
    // "must NOT say X" check, so the buffer is what keeps that from mattering.
    std::string spotterCueLog() {
        std::vector<char> buf(64 * 1024, '\0');
        if (m_spotLog) m_spotLog(buf.data(), static_cast<int>(buf.size()));
        return std::string(buf.data());
    }
    // Which audio route the last cue took ("<key>|wav:x.wav", "|mix:a+b",
    // "|tts"). See MXBMRP3_Test_SpotterLastAudio: the cue log answers what was
    // SAID, this answers what would PLAY, and on Wine "tts" means nothing does.
    std::string spotterLastAudio() {
        char buf[512] = {0};
        if (m_spotAudio) m_spotAudio(buf, static_cast<int>(sizeof(buf)));
        return std::string(buf);
    }
    // Per-HUD panel-theme override on StandingsHud; see MXBMRP3_Test_StandingsTheme.
    std::string standingsTheme() {
        char buf[64] = {0};
        if (m_stTheme) m_stTheme(buf, static_cast<int>(sizeof(buf)));
        return std::string(buf);
    }
    void setStandingsTheme(const char* v) { if (m_stSetTheme) m_stSetTheme(v); }
    // RadarHud's, whose factory default is THEME_NONE -- see MXBMRP3_Test_RadarTheme
    // for why a non-empty default is the only way to test clearing one.
    std::string radarTheme() {
        char buf[128] = {0};
        if (m_rdTheme) m_rdTheme(buf, static_cast<int>(sizeof(buf)));
        return buf;
    }
    void setRadarTheme(const char* v) { if (m_rdSetTheme) m_rdSetTheme(v); }
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
    // Panels that should carry a slot -- compare against benchmarkHudCount().
    int profilableHudCount() { return m_profilableHuds ? m_profilableHuds() : -1; }
    // Snapshot array capacity vs the count stored in it -- count > capacity is
    // the out-of-bounds read that corrupts the exported report.
    int benchmarkSnapshotCapacity() { return m_snapCap ? m_snapCap() : -1; }
    int benchmarkSnapshotCount() { return m_snapCount ? m_snapCount() : -1; }
    // A named panel's stint rebuild count (-1 if unknown). Only moves while the
    // profiler is collecting, so switch the benchmark widget on first with
    // benchmarkSetVisible(true). This is how a test asks whether a panel actually
    // rebuilt on a given change -- a dirty-flag subscription is a claim about
    // exactly that, and snapshot()/the HTTP state cannot see it: a panel that stops
    // updating leaves the plugin's computed numbers entirely correct.
    int hudRebuildCount(const char* name) { return m_hudRebuilds ? m_hudRebuilds(name) : -1; }
    // Render probe ([Advanced] renderProbe*): synthetic quads appended to the frame
    // for the ENGINE to draw. type 0=fill 1=sprite 2=text; sprite 0=cycle all, k=pin k.
    void setRenderProbe(int quads, int type, bool fullscreen, int sprite) {
        if (m_setProbe) m_setProbe(quads, type, fullscreen ? 1 : 0, sprite);
    }
    // Lowest/highest TEXTURED sprite in the last drawn frame, and how many quads were
    // sprite 0 (untextured). All zero if the hook is absent.
    void frameSpriteSpan(int& lo, int& hi, int& untextured) {
        lo = hi = untextured = 0;
        if (m_spriteSpan) m_spriteSpan(&lo, &hi, &untextured);
    }
    int registeredSpriteCount() { return m_spriteCount ? m_spriteCount() : 0; }
    // Install a synthetic sprite table. The real one is built in DrawInit from files
    // on disk and this harness stages no assets, so a test about sprite INDEXING has
    // to supply its own table or assert nothing.
    void installSpriteTable(int count) { if (m_installSprites) m_installSprites(count); }
    // The live probe settings, to assert the automatic sweep restored them.
    void getRenderProbe(int& n, int& type, int& fs, int& sprite) {
        n = type = fs = sprite = -1;
        if (m_getProbe) m_getProbe(&n, &type, &fs, &sprite);
    }
    void probeSweepStart() { if (m_sweepStart) m_sweepStart(); }
    void probeSweepAbort() { if (m_sweepAbort) m_sweepAbort(); }
    bool probeSweepRunning() { return m_sweepRunning && m_sweepRunning() != 0; }
    // The sweep's report text for a SYNTHETIC result set (supplied per-quad
    // costs, no wall-clock sweep) — see MXBMRP3_Test_ProbeSweepReport.
    std::string probeSweepReport(double fillUs, double alpha0Us, double degenUs) {
        auto fn = sym<void (*)(double, double, double, char*, int)>(
            "MXBMRP3_Test_ProbeSweepReport");
        if (!fn) return {};
        std::vector<char> buf(16384);
        fn(fillUs, alpha0Us, degenUs, buf.data(), static_cast<int>(buf.size()));
        return std::string(buf.data());
    }
    // Glyphs per probe string. The engine bills per glyph, so the sweep steps this
    // too -- and must therefore restore it like every other probe setting.
    void setRenderProbeTextChars(int n) { if (m_setProbeChars) m_setProbeChars(n); }
    int getRenderProbeTextChars() { return m_getProbeChars ? m_getProbeChars() : -1; }
    // Global drop shadow. On, every non-skipped string gets a second copy emitted
    // underneath it -- so the handed-over string count is what this is observed by.
    void setDropShadow(bool on) { if (m_setDropShadow) m_setDropShadow(on ? 1 : 0); }
    // Developer mode: reveals the Performance tab's Developer section, which the
    // settings panel measures like any other content.
    void setDeveloperMode(bool on) { if (m_setDevMode) m_setDevMode(on ? 1 : 0); }
    // A named panel's background opacity; 0 means its background draws nothing, and
    // the plugin skips emitting it rather than emitting invisible quads.
    bool setHudOpacity(const char* name, float opacity) {
        return m_setHudOpacity && m_setHudOpacity(name, opacity) != 0;
    }
    // Move a panel. LAYOUT-dirty only, so the next frame takes the reposition fast
    // path rather than a full rebuild -- the path that rewrites the background span.
    bool setHudOffset(const char* name, float x, float y) {
        return m_setHudOffset && m_setHudOffset(name, x, y) != 0;
    }
    // Largest quad a named panel emits (area x 1e6). A move is a pure translation, so
    // this is invariant across one -- which is what makes it catch a quad that got
    // stretched to the panel rect instead of translated.
    int hudMaxQuadArea(const char* name) { return m_maxQuadArea ? m_maxQuadArea(name) : -1; }
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
    // The Timing HUD's optional readout rows (ReadoutFlags bitmask); all off by default.
    // The live character budget the panel truncates its text readouts to.
    int timingTextBudget() { return m_timingTextBudget ? m_timingTextBudget() : 0; }
    bool timingReadouts(unsigned int mask) {
        if (!m_timingReadouts) return false;
        m_timingReadouts(mask);
        return true;
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
    struct TimingGeom { int height, contentTop, contentBot, fontLarge, fontNormal, lineLarge, lineNormal; };
    TimingGeom timingGeometry() {
        TimingGeom g{};
        if (m_timingGeometry) m_timingGeometry(&g.height, &g.contentTop, &g.contentBot,
                                               &g.fontLarge, &g.fontNormal,
                                               &g.lineLarge, &g.lineNormal);
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

    // --- What's-new markers (hud/settings/whats_new.h) -----------------------
    // The "New" tags on settings tabs and the bands on their rows: how many are
    // still live, whether a named tab is tagged, and the two dismissal paths.
    bool hasWhatsNew() const {
        return m_wnReset && m_wnLiveCount && m_wnTabTagged && m_wnHoverRow && m_wnClickTab;
    }
    void whatsNewReset() { if (m_wnReset) m_wnReset(); }
    int  whatsNewLiveCount() { return m_wnLiveCount ? m_wnLiveCount() : 0; }
    bool whatsNewTabTagged(const char* tab) {
        return m_wnTabTagged && m_wnTabTagged(tab) != 0;
    }
    // Opening a tab goes through handleTabClick, the path the sidebar click takes
    // and the one that dismisses the tag. Deliberately NOT setActiveTabByName: that
    // is also the persisted-tab restore, which must not dismiss anything.
    bool openSettingsTab(const char* tab) {
        return m_wnClickTab && m_wnClickTab(tab) != 0;
    }
    void hoverSettingsRow(const char* rowTooltipId) {
        if (m_wnHoverRow) m_wnHoverRow(rowTooltipId);
    }
    // Does marker `i` point at a row that this build actually draws? Opens its
    // tab and looks for a click region carrying its tooltip id. A marker naming
    // an id no row registers is silent -- no band, no complaint, and the tab
    // still tagged -- so this is the only way to see it.
    int  whatsNewMarkerCount() { return m_wnMarkerCount ? m_wnMarkerCount() : 0; }
    bool whatsNewMarkerResolves(int index) {
        return m_wnMarkerResolves && m_wnMarkerResolves(index) == 1;
    }
    // The dismissed set exactly as the INI would carry it. Reading it directly is
    // the only way to see a dismissal of a key no marker names.
    std::string whatsNewSerialize() {
        char buf[512] = {0};
        if (m_wnSerialize) m_wnSerialize(buf, static_cast<int>(sizeof(buf)));
        return std::string(buf);
    }
    // The footer's About button, one click, through the real dispatch path.
    bool hasAbout() const { return m_clickAbout && m_updateTagLive; }
    void clickAbout() { if (m_clickAbout) m_clickAbout(); }
    // Whether the Updates tab is wearing its "Update" tag.
    bool updateTagLive() { return m_updateTagLive && m_updateTagLive() != 0; }
    void updateTagReset() { if (m_updateTagReset) m_updateTagReset(); }
    // The About button's footer rect, from its own click region, in fixed point
    // (x * 1e6). Out-params rather than a ScreenEdges return: that type is declared
    // further down this header, and reordering it to suit one accessor is a worse
    // trade than four ints.
    bool hasAboutRect() const { return m_aboutRect != nullptr; }
    bool aboutButtonRect(int& l, int& t, int& r, int& b) {
        return m_aboutRect && m_aboutRect(&l, &t, &r, &b) != 0;
    }
    std::string whatsNewMarkerName(int index) {
        char buf[128] = {0};
        if (m_wnMarkerName) m_wnMarkerName(index, buf, static_cast<int>(sizeof(buf)));
        return std::string(buf);
    }
    // Every SELECTABLE settings tab, in tab-list order. Enumerated from the
    // registry rather than listed here: the panel sizes itself to the tab it is
    // showing, so a screen-fit case has to drive all of them, and a list in a
    // test is a list a new tab does not get added to.
    // How far the last-built settings tab overran its row budget, in rows;
    // negative is slack. Drive a draw() first.
    // StandingsHud's player-row band, as (x, width). `ok` is false when the HUD
    // emitted no band this rebuild.
    struct RowBand { double x = 0, w = 0; bool ok = false; };
    bool hasRowBand() const { return m_standingsRowBand != nullptr; }
    RowBand standingsRowBand() {
        RowBand b;
        if (!m_standingsRowBand) return b;
        int x = 0, w = 0;
        b.ok = m_standingsRowBand(&x, &w) != 0;
        b.x = x / 1e6; b.w = w / 1e6;
        return b;
    }
    bool hasSettingsOverflow() const { return m_settingsOverflow != nullptr; }
    double settingsOverflowRows() {
        return m_settingsOverflow ? m_settingsOverflow() / 1000.0 : 0.0;
    }
    std::vector<std::string> settingsTabNames() {
        std::vector<std::string> out;
        if (!m_settingsTabName) return out;
        char buf[64];
        for (int i = 0; m_settingsTabName(i, buf, static_cast<int>(sizeof(buf))); ++i)
            out.push_back(buf);
        return out;
    }
    // Every selectable tab, INCLUDING those hidden from the sidebar (About). Use
    // this when the question is "which tabs can set the panel's height"; use
    // settingsTabNames() when it is "what does the sidebar show". They differ, and
    // a height sweep run against the list silently stops finding the tallest tab.
    std::vector<std::string> settingsAllTabNames() {
        std::vector<std::string> out;
        if (!m_settingsAnyTabName) return out;
        char buf[64];
        for (int i = 0; m_settingsAnyTabName(i, buf, static_cast<int>(sizeof(buf))); ++i)
            out.push_back(buf);
        return out;
    }
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
    // Gamepad packs. installGamepad() adds one with no files on disk; the two
    // readers return the STORED name and the RESOLVED one, which differ exactly when
    // the stored name names no installed pack.
    void clearGamepads() { if (m_clearGamepads) m_clearGamepads(); }
    // Pit board packs, same contract as the gamepad calls below.
    void clearPitboards() { if (m_clearPitboards) m_clearPitboards(); }
    // The settings UI's pack cycle, and the flag its Off entry owns. `pitboard`
    // selects which of the two pack-driven HUDs.
    void cyclePack(bool pitboard, bool forward) {
        if (m_cyclePack) m_cyclePack(pitboard ? 1 : 0, forward ? 1 : 0);
    }
    bool packShowBg(bool pitboard) {
        return m_packShowBg && m_packShowBg(pitboard ? 1 : 0) != 0;
    }
    void setPackShowBg(bool pitboard, bool on) {
        if (m_setPackShowBg) m_setPackShowBg(pitboard ? 1 : 0, on ? 1 : 0);
    }
    void installPitboard(const char* name, float artW = 1920.0f, float artH = 1080.0f) {
        if (m_installPitboard) m_installPitboard(name, artW, artH);
    }
    // The RELOAD_CONFIG hotkey's asset half. See MXBMRP3_Test_ReloadAssetLayouts.
    void reloadAssetLayouts() { if (m_reloadAssetLayouts) m_reloadAssetLayouts(); }
    // The ACTIVE pack's current art width, for watching a reload act on it.
    float pitboardArtWidth() { return m_pitboardArtWidth ? m_pitboardArtWidth() : 0.0f; }
    float gamepadArtWidth() { return m_gamepadArtWidth ? m_gamepadArtWidth() : 0.0f; }
    void setPitboardPack(const char* name) { if (m_setPitboardPack) m_setPitboardPack(name); }
    std::string pitboardPackStored() {
        char buf[128] = {0};
        if (m_pitboardStored) m_pitboardStored(buf, static_cast<int>(sizeof(buf)));
        return buf;
    }
    std::string pitboardPackActive() {
        char buf[128] = {0};
        if (m_pitboardActive) m_pitboardActive(buf, static_cast<int>(sizeof(buf)));
        return buf;
    }
    // Pack skins (the `base` key): where a stem's file resolved from (-1 pack or
    // stem unknown, 0 own folder, 1 base's), and the resolved geometry numbers
    // that prove inheritance/override. See pack_skin_test.cpp.
    int gamepadStemSource(const char* pack, int stem) {
        return m_gamepadStemSrc ? m_gamepadStemSrc(pack, stem) : -1;
    }
    float gamepadGeomWidth(const char* pack) {
        return m_gamepadGeomWidth ? m_gamepadGeomWidth(pack) : 0.0f;
    }
    int pitboardStemSource(const char* pack, int stem) {
        return m_pitboardStemSrc ? m_pitboardStemSrc(pack, stem) : -1;
    }
    float pitboardPackArtWidth(const char* pack) {
        return m_pitboardPackArtW ? m_pitboardPackArtW(pack) : 0.0f;
    }
    unsigned pitboardTextColor(const char* pack) {
        return m_pitboardTextColor ? m_pitboardTextColor(pack) : 0u;
    }

    // The sprite-order self-check's verdict (0 = discovery indices and the
    // registered sprite table agree; -1 = hook missing). Meaningful after
    // Startup+DrawInit have run setupDefaultResources.
    int spriteOrderMismatches() { return m_spriteOrderMism ? m_spriteOrderMism() : -1; }
    // Re-run the checker with sprite table entries a and b (1-based) swapped,
    // restoring the table; non-zero = the checker catches skew (-1 = unavailable).
    int spriteOrderWithSwap(int a, int b) {
        return m_spriteOrderSwap ? m_spriteOrderSwap(a, b) : -1;
    }

    // GamepadSprite::kStems, read from the DLL so a fixture staging a real pack
    // tree cannot drift from the table discovery actually walks.
    int gamepadStemCount() { return m_padStemCount ? m_padStemCount() : 0; }
    std::string gamepadStemName(int i) {
        char buf[64] = {0};
        if (m_padStemName) m_padStemName(i, buf, static_cast<int>(sizeof(buf)));
        return buf;
    }

    void installGamepad(const char* name, float geometryWidth = 750.0f) {
        if (m_installGamepad) m_installGamepad(name, geometryWidth);
    }
    void setGamepadPack(const char* name) { if (m_setGamepadPack) m_setGamepadPack(name); }
    std::string gamepadPackStored() {
        char buf[128] = {0};
        if (m_gamepadStored) m_gamepadStored(buf, static_cast<int>(sizeof(buf)));
        return buf;
    }
    std::string gamepadPackActive() {
        char buf[128] = {0};
        if (m_gamepadActive) m_gamepadActive(buf, static_cast<int>(sizeof(buf)));
        return buf;
    }
    // [Advanced] uiFontSize at runtime — the one setting that resizes every panel.
    // Marks all HUDs dirty, so draw() afterwards is a real rebuild at the new size.
    bool hasUiFontSize() const { return m_setUiFontSize != nullptr; }
    void setUiFontSize(float v) { if (m_setUiFontSize) m_setUiFontSize(v); }
    // The box model's eight air terms, in the [Advanced] shorthand. Order as
    // settings_manager_global.cpp writes them; see MXBMRP3_Test_SetBoxTerm.
    enum BoxTermId { BOX_PANEL_PADDING = 0, BOX_TITLE_MARGIN, BOX_TITLE_PADDING,
                     BOX_CONTENT_MARGIN, BOX_CONTENT_PADDING, BOX_BUTTON_MARGIN,
                     BOX_BUTTON_PADDING, BOX_PANEL_GAP };
    bool hasBoxTerms() const { return m_setBoxTerm != nullptr; }
    void setBoxTerm(BoxTermId which, const char* shorthand) {
        if (m_setBoxTerm) m_setBoxTerm(static_cast<int>(which), shorthand);
    }
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

    // The crash widget's streaming tally. crashTallyReset() goes through the
    // widget's own resetCounter(), the entry point the button and hotkey share.
    bool hasCrashTally() const { return m_crashTally != nullptr; }
    int crashTally() { return m_crashTally ? m_crashTally(0) : -1; }
    int crashTallyReset() { return m_crashTally ? m_crashTally(1) : -1; }

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
    // --- Themed panel geometry (theme_geometry_test) ------------------------
    // A theme installed here has NO files behind it: the geometry is arithmetic over
    // the insets, so the integration suite needs no asset staging. See
    // AssetManager::installSyntheticTheme.
    // READABLE SUGAR FOR A PANEL'S REGISTRATION NAME. The geometry hooks take the
    // NAME now (testHudByName in core/test_hooks.cpp), so this enum is a convenience
    // for the panels tests name often, not the addressing mechanism -- hudName() below
    // is the only place it is translated, and every hook wrapper also takes a bare
    // const char*, so all 44 registered elements are reachable whether or not they
    // appear here. Adding an element to a sweep needs no plugin-side edit.
    //
    // WHAT THIS REPLACED, because the shape is worth not rebuilding: a 21-case switch
    // in the plugin, mirrored here, whose `default:` arm returned the G-FORCE WIDGET --
    // so an unmapped id silently handed back a real panel and the assertion passed
    // against the wrong one. The two copies had already diverged (1 meant the Gap Bar
    // to one hook and G-force to another), latent only because no case asked the same
    // id both ways. An unknown NAME cannot do that: it resolves to nullptr and the hook
    // returns its empty answer.
    enum HudId { HUD_STANDINGS = 0, HUD_GFORCE = 1, HUD_TIMING = 2,
                 HUD_PERFORMANCE = 3, HUD_SESSION_CHARTS = 4, HUD_SETTINGS = 5,
                 HUD_GAPBAR = 6, HUD_NOTICES = 7,
                 // Reference panels, named here but with no test asking for them today.
                 HUD_RECORDS = 8, HUD_LAP = 9, HUD_POSITION = 10,
                 HUD_MAP = 11,
                 // The plainest table panel WITH a caption, which is what the caption
                 // cases drive: three caption-geometry bugs shipped in that blind spot,
                 // each "fixed" against the settings panel, whose cards lay out in
                 // COLUMNS and speak for nothing else.
                 HUD_SESSION = 12,
                 // The only panel laying out its own button row.
                 HUD_VERSION = 13,
                 // The third rider-marker HUD, beside Map and the gap bar -- the set
                 // whose labels have to agree.
                 HUD_RADAR = 14,
                 // The three widgets sharing one content box, so it can be checked
                 // that they still do.
                 HUD_SPEED = 15, HUD_GEAR = 16, HUD_CRASH = 17,
                 // The gauge widgets, so the card-anchor sweep can cover every panel
                 // that centres on its card.
                 HUD_BARS = 18, HUD_COMPASS = 19, HUD_LEAN = 20 };

    // THE ONE TRANSLATION. Strings are HudManager::initialize()'s registration names;
    // an id outside the enum yields "", which resolves to no panel rather than to
    // somebody else's.
    static const char* hudName(HudId which) {
        switch (which) {
            case HUD_STANDINGS:      return "standings_hud";
            case HUD_GFORCE:         return "gforce_widget";
            case HUD_TIMING:         return "timing_hud";
            case HUD_PERFORMANCE:    return "performance_hud";
            case HUD_SESSION_CHARTS: return "session_charts_hud";
            case HUD_SETTINGS:       return "settings_hud";
            case HUD_GAPBAR:         return "gap_bar_hud";
            case HUD_NOTICES:        return "notices_hud";
            case HUD_RECORDS:        return "records_hud";
            case HUD_LAP:            return "lap_widget";
            case HUD_POSITION:       return "position_widget";
            case HUD_MAP:            return "map_hud";
            case HUD_SESSION:        return "session_widget";
            case HUD_VERSION:        return "version_widget";
            case HUD_RADAR:          return "radar_hud";
            case HUD_SPEED:          return "speed_widget";
            case HUD_GEAR:           return "gear_widget";
            case HUD_CRASH:          return "crash_widget";
            case HUD_BARS:           return "bars_widget";
            case HUD_COMPASS:        return "compass_widget";
            case HUD_LEAN:           return "lean_widget";
        }
        return "";
    }
    struct PanelRect { int w = 0, h = 0, quads = 0; };

    bool hasThemeGeometry() const {
        return m_installTheme && m_clearTheme && m_hudPanelRect && m_setHudTitle;
    }
    // cardSprites = false models a theme folder that ships frame slices only, so a
    // test can ask what happens when a theme requests a band or a card it has no art
    // for. Defaulted, because every existing caller wants the full set.
    // buttonSprites = true adds the third (BUTTON) slice set, which is what makes
    // addButtonQuad() draw a nine-slice instead of a flat rectangle. Off by default:
    // the geometry cases below count exact quad totals for panels with no buttons.
    void installTheme(const char* name, float inset, float cardBorder,
                      int titleBand, int card, bool cardSprites = true,
                      bool buttonSprites = false) {
        if (m_installTheme) m_installTheme(name, inset, cardBorder, titleBand, card,
                                           cardSprites ? 1 : 0, buttonSprites ? 1 : 0);
    }
    void clearTheme()                  { if (m_clearTheme) m_clearTheme(); }
    // `[card] band-size` on the theme just installed; negative restores the default
    // (the band follows `[card] border`). See MXBMRP3_Test_SetThemeTitleBorder.
    bool hasTitleBorder() const { return m_setThemeTitleBorder != nullptr; }
    // `[panel] padding-x/-y` on the theme just installed; negative restores the
    // built-in for that axis. See MXBMRP3_Test_SetThemePanelPadding.
    bool hasPanelPaddingKey() const { return m_setThemePanelPad != nullptr; }
    bool setThemePanelPadding(float xCells, float yCells) {
        return m_setThemePanelPad && m_setThemePanelPad(xCells, yCells) != 0;
    }
    bool setThemeTitleBorder(float cells) {
        return m_setThemeTitleBorder && m_setThemeTitleBorder(cells) != 0;
    }

    // The plugin's LIVE lattice (MXBMRP3_Test_LayoutCells): cellW/cellH are the
    // snap grid, artV is a theme border cell's vertical extent (cellW * aspect --
    // font-derived, so it does NOT move with uiLineHeight), lineH is one normal
    // text row. Geometry tests derive expectations from these rather than freeze
    // the shipped values into literals that rot when a metric root is retuned.
    struct LayoutCells { double cellW, cellH, artV, lineH; };
    bool hasLayoutCells() const { return m_layoutCells != nullptr; }
    LayoutCells layoutCells() {
        double v[4] = {};
        if (m_layoutCells) m_layoutCells(v);
        return { v[0], v[1], v[2], v[3] };
    }

    // Theme icon overrides, injected into the last installed synthetic theme (see
    // MXBMRP3_Test_SetThemeIconOverride).
    bool hasIconTheming() const {
        return m_setThemeIcon && m_iconForName && m_iconForShape && m_shapeForIcon;
    }
    bool setThemeIcon(const char* icon, int sprite, int shape) {
        return m_setThemeIcon && m_setThemeIcon(icon, sprite, shape) != 0;
    }
    int iconSpriteForName(const char* name) { return m_iconForName ? m_iconForName(name) : 0; }
    int iconSpriteForShape(int shape)       { return m_iconForShape ? m_iconForShape(shape) : 0; }
    int shapeForIconSprite(int sprite)      { return m_shapeForIcon ? m_shapeForIcon(sprite) : 0; }
    // Publish an UPDATE_AVAILABLE result with no network, so the update-available UI
    // (the settings footer chip, the Version widget's notification panel) can be
    // rendered and asserted headlessly.
    void updateSetAvailable(const char* latest) {
        if (m_updateSetAvailable) m_updateSetAvailable(latest);
    }

    // The two terms the Version widget's notification button row is laid out from:
    // one text row and the [panel] junction gap, in screen units (the hook's fixed
    // point is undone here, so they compare directly with StringRow::y).
    struct VersionRowTerms { double rowH = 0, junction = 0; };
    VersionRowTerms versionRowTerms() {
        VersionRowTerms t;
        if (!m_versionRowTerms) return t;
        int rh = 0, j = 0;
        m_versionRowTerms(&rh, &j);
        t = { rh / 1e6, j / 1e6 };
        return t;
    }

    // An asymmetric [content] border on the installed synthetic theme -- the shape a
    // uniform border cannot make, and the one that tells a panel centring in the
    // content band apart from one centring in the card. See testSetThemeContentBorder.
    bool setThemeContentBorder(float t, float r, float b, float l) {
        return m_setThemeContentBorder && m_setThemeContentBorder(t, r, b, l) != 0;
    }
    // ...and `[content] margin`, which moves the whole card inside the panel --
    // the term that separates the card's centre from the panel's.
    bool setThemeContentMargin(float t, float r, float b, float l) {
        return m_setThemeContentMargin && m_setThemeContentMargin(t, r, b, l) != 0;
    }
    bool setThemeTitleMargin(float t, float r, float b, float l) {
        return m_setThemeTitleMargin && m_setThemeTitleMargin(t, r, b, l) != 0;
    }

    // Is a HUD's background texture switched on? 1 = on, 0 = off, -1 = no such
    // HUD (or the hook is missing). The flag rather than the sprite: see
    // MXBMRP3_Test_HudBackgroundTextureOn for why the sprite cannot answer here.
    int hudBackgroundTextureOn(const char* name) {
        return m_hudBgSprite ? m_hudBgSprite(name) : -1;
    }

    // Runtime scale change by panel label (MXBMRP3_Test_SetHudScale) -- the
    // settings-click path, distinct from a scale loaded out of the INI.
    bool setHudScale(const char* name, float scale) {
        return m_setHudScale && m_setHudScale(name, scale) != 0;
    }
    // The Gap Bar's width setting, in percent (MXBMRP3_Test_GapBarWidth).
    bool gapBarWidth(int percent) {
        if (!m_gapBarWidth) return false;
        m_gapBarWidth(percent);
        return true;
    }
    // Any HUD's caption, by its REGISTRATION NAME (HudManager::initialize; the same
    // string MXBMRP3_Test_PanelName reports and panelCells() reads a panel back
    // under). Returns false if no HUD carries that name, so a caller can REQUIRE it
    // rather than silently toggling nothing; this header is included by non-doctest
    // drivers too, so it cannot assert itself.
    bool setHudTitle(const char* name, bool on) {
        return m_setHudTitle && m_setHudTitle(name, on ? 1 : 0) != 0;
    }
    void setGForceTitle(bool on)       { setHudTitle("gforce_widget", on); }
    void showAllHuds(bool on)          { if (m_showAllHuds) m_showAllHuds(on ? 1 : 0); }
    PanelRect hudPanelRect(HudId which) { return hudPanelRect(hudName(which)); }
    PanelRect hudPanelRect(const char* name) {
        PanelRect r;
        if (m_hudPanelRect) m_hudPanelRect(name, &r.w, &r.h, &r.quads);
        return r;
    }

    // The section cards a HUD emitted, in order, as (top, bottom) in normalized Y.
    // Empty when the HUD draws one body card (or none). See the hook for why the
    // no-overlap invariant is unmeasurable from a screenshot.
    // No SectionHudId enum: it was { SEC_TIMING = 0, SEC_PERFORMANCE = 1 } over a
    // two-case mapping in the hook that answered "Performance" to every value it did
    // not recognise. sectionCards() takes a registration name like everything else, so
    // any panel with section cards can be asked rather than those two.
    struct CardSpan { double top = 0.0, bottom = 0.0; };
    bool hasSectionCards() const { return m_sectionCards != nullptr; }
    std::vector<CardSpan> sectionCards(const char* name) {
        std::vector<CardSpan> out;
        if (!m_sectionCards) return out;
        int raw[32] = {};
        const int n = m_sectionCards(name, raw,
                                     static_cast<int>(sizeof(raw) / sizeof(raw[0])));
        for (int i = 0; i < n && (i * 2 + 1) < 32; i++) {
            out.push_back({ raw[i * 2] / 1e6, raw[i * 2 + 1] / 1e6 });
        }
        return out;
    }

    // EVERY per-HUD sweep hook has the same shape: fill an int array with one PAIR per
    // registered HUD, in registration order, and label index i through
    // MXBMRP3_Test_PanelName. Written twice before this existed, and the copy carried
    // the 256-int cap and the (i*2+1) bound with it -- so the day the HUD count outgrows
    // the buffer, one copy silently keeps truncating. `scale` is the hook's own
    // quantisation (x1000 cells, x1e6 screen height).
    template <typename T>
    std::vector<T> namedPairSweep(int (*fn)(int*, int), double scale) {
        std::vector<T> out;
        if (!fn) return out;
        int raw[256] = {};
        const int n = fn(raw, 256);
        for (int i = 0; i < n && (i * 2 + 1) < 256; i++) {
            char nm[64] = {};
            if (m_panelName) m_panelName(i, nm, static_cast<int>(sizeof(nm)));
            out.push_back({ nm, raw[i * 2] / scale, raw[i * 2 + 1] / scale });
        }
        return out;
    }

    // EVERY registered HUD's panel size in cells, with its label. The grid sweep --
    // see the hook for why a per-HUD id could not answer this.
    struct PanelCells { std::string name; double w = 0.0, h = 0.0; };
    bool hasPanelSweep() const { return m_panelCells && m_panelName; }
    // PerformanceHud's element mask; 3 = FPS | CPU, which is what gives it TWO sections.
    bool hasPerfElements() const { return m_setPerfElements != nullptr; }
    void setPerformanceElements(unsigned mask) { if (m_setPerfElements) m_setPerfElements(mask); }
    std::vector<PanelCells> panelCells() {
        return namedPairSweep<PanelCells>(m_panelCells, 1000.0);
    }

    // EVERY registered HUD's vertical frame margin and padding, with its label. See
    // MXBMRP3_Test_PanelPadY for why the invariant is paddingV >= frameMarginY.
    struct PanelPadY { std::string name; double frameMarginY = 0.0, paddingV = 0.0; };
    bool hasPanelPadY() const { return m_panelPadY && m_panelName; }
    std::vector<PanelPadY> panelPadY() {
        return namedPairSweep<PanelPadY>(m_panelPadY, 1e6);
    }

    // A HUD's quads as rects, in draw order. See the hook.
    struct QuadRect { double l = 0, t = 0, r = 0, b = 0; };
    bool hasQuadRects() const { return m_quadRects != nullptr; }

    // Plant a live gap in the Gap Bar so its fill draws (see the hook).
    bool gapBarForceGap(int ms, bool valid) {
        if (!m_gapBarForceGap) return false;
        m_gapBarForceGap(ms, valid ? 1 : 0);
        return true;
    }

    // The placed card rect of a HUD's memoized plan, pre-offset (see the hook).
    // `panel`, when asked for, is the panel's own rect in the SAME space -- the
    // pair a side-air measurement needs, since drawn quads carry the HUD offset
    // and this plan does not.
    bool hudCardRect(HudId which, QuadRect* out, QuadRect* panel = nullptr) {
        return hudCardRect(hudName(which), out, panel);
    }
    bool hudCardRect(const char* name, QuadRect* out, QuadRect* panel = nullptr) {
        if (!m_hudCardRect) return false;
        int v[8] = {0, 0, 0, 0, 0, 0, 0, 0};
        if (m_hudCardRect(name, v) == 0) return false;
        if (out) {
            out->l = v[0] / 1e6; out->t = v[1] / 1e6;
            out->r = (v[0] + v[2]) / 1e6; out->b = (v[1] + v[3]) / 1e6;
        }
        if (panel) {
            panel->l = v[4] / 1e6; panel->t = v[5] / 1e6;
            panel->r = (v[4] + v[6]) / 1e6; panel->b = (v[5] + v[7]) / 1e6;
        }
        return true;
    }

    // A HUD's drawn strings: what text sits on what line. See MXBMRP3_Test_HudStringRows
    // for why the quad hooks cannot answer that.
    struct StringRow { double x = 0.0, y = 0.0; std::string text; };
    bool hasStringRows() const { return m_stringRows != nullptr; }
    std::vector<StringRow> hudStringRows(HudId which) { return hudStringRows(hudName(which)); }
    std::vector<StringRow> hudStringRows(const char* name) {
        std::vector<StringRow> rows;
        if (!m_stringRows) return rows;
        const int count = m_stringRows(name, -1, nullptr, nullptr, nullptr, 0);
        for (int i = 0; i < count; ++i) {
            int x = 0, y = 0; char buf[128] = {0};
            m_stringRows(name, i, &x, &y, buf, static_cast<int>(sizeof(buf)));
            rows.push_back({ x / 1e6, y / 1e6, std::string(buf) });
        }
        return rows;
    }
    // COLOUR READERS, for asserting that a caption is legible on what is behind it --
    // a relationship the rect and text hooks cannot express. Index is emission order,
    // matching hudStringRows() / hudQuadRects().
    bool hasInkHooks() const {
        return m_stringColor && m_quadColor && m_minLumaGap && m_luma601;
    }
    unsigned long stringColor(const char* name, int i) {
        return m_stringColor ? m_stringColor(name, i) : 0;
    }
    unsigned long quadColor(const char* name, int i) {
        return m_quadColor ? m_quadColor(name, i) : 0;
    }
    // The plugin's own threshold and its own luma formula, read live rather than
    // re-spelled here -- a second copy of either is a test that can agree with itself
    // while disagreeing with the code.
    int minGlyphLumaGap() { return m_minLumaGap ? m_minLumaGap() : 0; }
    int luma601(unsigned long c) { return m_luma601 ? m_luma601(c) : 0; }

    std::vector<QuadRect> hudQuadRects(HudId which) { return hudQuadRects(hudName(which)); }
    std::vector<QuadRect> hudQuadRects(const char* name) {
        std::vector<QuadRect> out;
        if (!m_quadRects) return out;
        static int raw[4096];
        const int n = m_quadRects(name, raw, 4096);
        for (int i = 0; i < n && (i * 4 + 3) < 4096; i++) {
            out.push_back({ raw[i*4]/1e6, raw[i*4+1]/1e6, raw[i*4+2]/1e6, raw[i*4+3]/1e6 });
        }
        return out;
    }

    // How finalizeThemedFill cut this panel's fill: the centre it cut from, the
    // strips it wrote, and the cards/band it cut around. See the hook.
    struct FillCut {
        QuadRect centre;
        std::vector<QuadRect> strips;
        std::vector<QuadRect> covers;
        bool themed = false;
    };
    bool hasFillCut() const { return m_fillCut != nullptr; }
    FillCut hudFillCut(HudId which) { return hudFillCut(hudName(which)); }
    FillCut hudFillCut(const char* name) {
        FillCut out;
        if (!m_fillCut) return out;
        static int raw[4096];
        const int need = m_fillCut(name, raw, 4096);
        if (need < 6 || need > 4096) return out;
        const int nStrip = raw[0], nCover = raw[1];
        out.centre = { raw[2]/1e6, raw[3]/1e6, raw[4]/1e6, raw[5]/1e6 };
        out.themed = (out.centre.r > out.centre.l) && (out.centre.b > out.centre.t);
        int i = 6;
        for (int k = 0; k < nStrip; k++, i += 4)
            out.strips.push_back({ raw[i]/1e6, raw[i+1]/1e6, raw[i+2]/1e6, raw[i+3]/1e6 });
        for (int k = 0; k < nCover; k++, i += 4)
            out.covers.push_back({ raw[i]/1e6, raw[i+1]/1e6, raw[i+2]/1e6, raw[i+3]/1e6 });
        return out;
    }

    // A HUD's panel edges ON SCREEN (bounds + the live offset), quantised x1e6.
    // hudPanelRect() reports the panel's own box and cannot answer "did this panel
    // grow off the display" -- a widened panel is wider either way.
    struct ScreenEdges { int l = 0, t = 0, r = 0, b = 0; };
    bool hasScreenEdges() const { return m_hudScreenEdges && m_setScreenClamping; }
    // [Display] screenClamping. Off by default; see the hook.
    void setScreenClamping(bool on) { if (m_setScreenClamping) m_setScreenClamping(on ? 1 : 0); }
    ScreenEdges hudScreenEdges(HudId which) { return hudScreenEdges(hudName(which)); }
    ScreenEdges hudScreenEdges(const char* name) {
        ScreenEdges e;
        if (m_hudScreenEdges) m_hudScreenEdges(name, &e.l, &e.t, &e.r, &e.b);
        return e;
    }

    // The settings panel's horizontal margins: its background's two edges, and the
    // outer edges of its two COLUMNS. Fixed point (x * 1e6).
    struct SettingsMargins { int bgL = 0, bgR = 0, colL = 0, colR = 0; };
    bool hasSettingsMargins() const { return m_settingsMarginsX != nullptr; }
    // [panel] gap on the synthetic theme; false if the hook is absent.
    bool setThemeGap(float cells) { return m_setThemeGap && m_setThemeGap(cells); }
    // The settings gutter's bounding card edges and the vertical seam read, in
    // normalized units (converted back from the hook's 1e6 fixed point).
    struct SettingsGutter { float sidebarCardRight, contentCardLeft, seam; };
    SettingsGutter settingsGutter() {
        int sr = 0, cl = 0, sv = 0;
        if (m_settingsGutter) m_settingsGutter(&sr, &cl, &sv);
        return { sr / 1e6f, cl / 1e6f, sv / 1e6f };
    }
    SettingsMargins settingsMarginsX() {
        SettingsMargins m;
        if (m_settingsMarginsX) m_settingsMarginsX(&m.bgL, &m.bgR, &m.colL, &m.colR);
        return m;
    }

    // The settings panel's CONTENT anchors (label column, control column, and a
    // full-width row's right edge), fixed point. Theme-invariant by construction; see
    // MXBMRP3_Test_SettingsContentX.
    struct SettingsContentX { int labelX = 0, controlX = 0, rowRight = 0; };
    bool hasSettingsContentX() const { return m_settingsContentX != nullptr; }
    SettingsContentX settingsContentX() {
        SettingsContentX c;
        if (m_settingsContentX) m_settingsContentX(&c.labelX, &c.controlX, &c.rowRight);
        return c;
    }

    // --- Appearance palette / font precedence (theme_palette_test) ----------
    bool hasPaletteHooks() const {
        return m_effColor && m_colorOverridden && m_themeOrDefColor && m_cycleColor
            && m_clearColorOv && m_setThemeColor && m_effFont && m_fontOverridden
            && m_cycleFont && m_clearFontOv;
    }
    unsigned long effectiveColor(int slot)      { return m_effColor ? m_effColor(slot) : 0; }
    int  colorOverridden(int slot)              { return m_colorOverridden ? m_colorOverridden(slot) : -1; }
    unsigned long themeOrDefaultColor(int slot) { return m_themeOrDefColor ? m_themeOrDefColor(slot) : 0; }
    // One named theme as "own=<n>;center=<sprite>;base=<name>;primary=<hex>;primarySet=<0|1>",
    // empty when no such theme is installed. See MXBMRP3_Test_ThemeInfo for why
    // these four travel together.
    std::string themeInfo(const char* name) {
        char buf[256] = {0};
        if (m_themeInfo) m_themeInfo(name, buf, static_cast<int>(sizeof(buf)));
        return std::string(buf);
    }
    // One named gauges pack as "base=<name>;tachoMax=<f>;speedoMax=<f>", empty
    // when it is not installed. See MXBMRP3_Test_GaugesInfo.
    std::string gaugesInfo(const char* name) {
        char buf[256] = {0};
        if (m_gaugesInfo) m_gaugesInfo(name, buf, static_cast<int>(sizeof(buf)));
        return std::string(buf);
    }
    void cycleColor(int slot, bool fwd)         { if (m_cycleColor) m_cycleColor(slot, fwd ? 1 : 0); }
    void clearColorOverride(int slot)           { if (m_clearColorOv) m_clearColorOv(slot); }
    void setThemeColor(int slot, unsigned long c) { if (m_setThemeColor) m_setThemeColor(slot, c); }
    std::string effectiveFont(int cat) {
        char buf[128] = {};
        if (m_effFont) m_effFont(cat, buf, static_cast<int>(sizeof(buf)));
        return std::string(buf);
    }
    int  fontOverridden(int cat)                { return m_fontOverridden ? m_fontOverridden(cat) : -1; }
    void cycleFont(int cat, bool fwd)           { if (m_cycleFont) m_cycleFont(cat, fwd ? 1 : 0); }
    void clearFontOverride(int cat)             { if (m_clearFontOv) m_clearFontOv(cat); }
    void setFont(int cat, const char* n)        { if (m_setFont) m_setFont(cat, n); }
    int  fontCount()                            { return m_fontCount ? m_fontCount() : -1; }

    bool hasCompanionDecouple() const {
        return m_stSetVisible && m_stSetCompVisible && m_stClearCompanion && m_stCompanionState;
    }
    void stSetVisible(bool v)          { if (m_stSetVisible) m_stSetVisible(v ? 1 : 0); }
    bool hasPlateHooks() const         { return m_stPlateInsetY && m_stSetOffset; }
    // How far the race number sits from its plate's CENTRE, as a signed fraction of
    // plate height: 0 is centred, positive is low. Signed, so kPlateInsetNotFound is
    // far out of band -- it is the "no plate on that row" reading (and what a missing
    // hook returns).
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

    // Write a settings INI by hand under `savePath`, creating the mxbmrp3\ subfolder.
    //
    // FOR TESTING WHAT IS *NOT* IN THE FILE. Every other route into the settings goes
    // through a capture, and a capture can only ever emit keys -- so the one shape it
    // cannot produce is a key's ABSENCE, which is exactly what the sparse [Colors] /
    // [Fonts] sections make meaningful ("absent" = follow the theme). Hand-writing the
    // file is also the user-facing workflow those sections have to survive.
    //
    // Uses the Win32 API rather than std::ofstream because the harness runs under Wine
    // and `savePath` is a Windows path (Z:\tmp\...) the C runtime here resolves natively.
    void writeSettingsFile(const char* savePath, const std::string& contents) {
        std::string dir(savePath);
        if (!dir.empty() && dir.back() != '\\') dir += '\\';
        dir += "mxbmrp3";
        CreateDirectoryA(std::string(savePath).c_str(), nullptr);
        CreateDirectoryA(dir.c_str(), nullptr);
        const std::string path = dir + "\\mxbmrp3_settings.ini";
        HANDLE h = CreateFileA(path.c_str(), GENERIC_WRITE, 0, nullptr,
                               CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (h == INVALID_HANDLE_VALUE) return;
        DWORD written = 0;
        WriteFile(h, contents.data(), static_cast<DWORD>(contents.size()), &written, nullptr);
        CloseHandle(h);
    }

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
    void        (*m_resetGlobals)() = nullptr;
    void        (*m_spotEnable)(int) = nullptr;
    void        (*m_spotSubs)(int) = nullptr;
    void        (*m_spotMask)(unsigned) = nullptr;
    void        (*m_spotLog)(char*, int) = nullptr;
    void        (*m_spotAudio)(char*, int) = nullptr;
    void        (*m_spotPack)(const char*) = nullptr;
    void        (*m_spotPrev)(int) = nullptr;
    void        (*m_spotPin)(int) = nullptr;
    void        (*m_spotHotkey)() = nullptr;
    int         (*m_spotParked)() = nullptr;
    void        (*m_stTheme)(char*, int) = nullptr;
    void        (*m_stSetTheme)(const char*) = nullptr;
    void        (*m_rdTheme)(char*, int) = nullptr;
    void        (*m_rdSetTheme)(const char*) = nullptr;
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
    void        (*m_timingReadouts)(unsigned int) = nullptr;
    int         (*m_timingTextBudget)() = nullptr;
    int         (*m_timingReferenceMs)(int,int) = nullptr;
    int         (*m_timingTargetSplit)() = nullptr;
    int         (*m_timingInvalidShown)() = nullptr;
    int         (*m_timingFrozen)() = nullptr;
    int         (*m_elapsedLapTime)() = nullptr;
    std::string m_shippedPackText;   // outlives the const char* handed to the DLL
    int         (*m_lapTimerFromRaceStart)() = nullptr;
    int         (*m_inGridStartGrace)() = nullptr;
    void        (*m_timingGeometry)(int*,int*,int*,int*,int*,int*,int*) = nullptr;
    void        (*m_eventLogSetVisible)(int) = nullptr;
    int         (*m_eventLogIconColorSlot)(const char*) = nullptr;
    void        (*m_noticesSetVisible)(int) = nullptr;
    void        (*m_dirSetStories)(int) = nullptr;
    void        (*m_dirSetShotSec)(int, int) = nullptr;
    int         (*m_dirHomeSubject)() = nullptr;
    int         (*m_dirSubject)() = nullptr;
    int         (*m_benchHudCount)() = nullptr;
    int         (*m_benchCbCount)() = nullptr;
    int         (*m_profilableHuds)() = nullptr;
    int         (*m_snapCap)() = nullptr;
    int         (*m_snapCount)() = nullptr;
    int         (*m_hudRebuilds)(const char*) = nullptr;
    void        (*m_setProbe)(int,int,int,int) = nullptr;
    void        (*m_spriteSpan)(int*,int*,int*) = nullptr;
    int         (*m_spriteCount)() = nullptr;
    void        (*m_installSprites)(int) = nullptr;
    void        (*m_getProbe)(int*,int*,int*,int*) = nullptr;
    void        (*m_sweepStart)() = nullptr;
    void        (*m_sweepAbort)() = nullptr;
    int         (*m_sweepRunning)() = nullptr;
    void        (*m_setProbeChars)(int) = nullptr;
    int         (*m_getProbeChars)() = nullptr;
    void        (*m_setDropShadow)(int) = nullptr;
    void        (*m_setDevMode)(int) = nullptr;
    int         (*m_setHudOpacity)(const char*,float) = nullptr;
    int         (*m_setHudOffset)(const char*,float,float) = nullptr;
    int         (*m_maxQuadArea)(const char*) = nullptr;
    long long   m_lastReplayTimeMs = 0;
    void        (*m_save)() = nullptr;
    void        (*m_markDirty)() = nullptr;
    void        (*m_flushIfDirty)() = nullptr;
    int         (*m_isDirty)() = nullptr;
    void        (*m_setAutoSave)(int) = nullptr;
    void        (*m_loadSettings)(const char*) = nullptr;
    void        (*m_setActiveTab)(const char*) = nullptr;
    void        (*m_wnReset)() = nullptr;
    int         (*m_wnLiveCount)() = nullptr;
    int         (*m_wnTabTagged)(const char*) = nullptr;
    void        (*m_wnHoverRow)(const char*) = nullptr;
    int         (*m_wnClickTab)(const char*) = nullptr;
    int  (*m_wnMarkerCount)() = nullptr;
    int  (*m_wnMarkerResolves)(int) = nullptr;
    void (*m_wnMarkerName)(int, char*, int) = nullptr;
    void (*m_wnSerialize)(char*, int) = nullptr;
    void (*m_clickAbout)() = nullptr;
    int  (*m_updateTagLive)() = nullptr;
    void (*m_updateTagReset)() = nullptr;
    int  (*m_aboutRect)(int*,int*,int*,int*) = nullptr;
    int         (*m_settingsTabName)(int,char*,int) = nullptr;
    int         (*m_settingsAnyTabName)(int,char*,int) = nullptr;
    int         (*m_settingsOverflow)() = nullptr;
    int         (*m_standingsRowBand)(int*,int*) = nullptr;
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
    int         (*m_setThemeGap)(float) = nullptr;
    void        (*m_settingsGutter)(int*,int*,int*) = nullptr;
    void        (*m_companion)(int) = nullptr;
    void        (*m_stSetVisible)(int) = nullptr;
    void        (*m_installTheme)(const char*,float,float,int,int,int,int) = nullptr;
    int         (*m_setThemeTitleBorder)(float) = nullptr;
    int         (*m_setThemePanelPad)(float,float) = nullptr;
    void        (*m_layoutCells)(double*) = nullptr;
    int         (*m_setHudScale)(const char*, float) = nullptr;
    int         (*m_hudBgSprite)(const char*) = nullptr;
    void        (*m_gapBarWidth)(int) = nullptr;
    void        (*m_clearTheme)() = nullptr;
    int         (*m_setThemeIcon)(const char*,int,int) = nullptr;
    int         (*m_iconForName)(const char*) = nullptr;
    int         (*m_iconForShape)(int) = nullptr;
    int         (*m_shapeForIcon)(int) = nullptr;
    void        (*m_hudPanelRect)(const char*,int*,int*,int*) = nullptr;
    int         (*m_sectionCards)(const char*,int*,int) = nullptr;
    int         (*m_panelCells)(int*,int) = nullptr;
    int         (*m_panelPadY)(int*,int) = nullptr;
    void        (*m_setPerfElements)(unsigned) = nullptr;
    int         (*m_quadRects)(const char*,int*,int) = nullptr;
    unsigned long (*m_stringColor)(const char*,int) = nullptr;
    unsigned long (*m_quadColor)(const char*,int) = nullptr;
    int         (*m_minLumaGap)() = nullptr;
    int         (*m_luma601)(unsigned long) = nullptr;
    int         (*m_stringRows)(const char*,int,int*,int*,char*,int) = nullptr;
    int         (*m_fillCut)(const char*,int*,int) = nullptr;
    void        (*m_panelName)(int,char*,int) = nullptr;
    void        (*m_hudScreenEdges)(const char*,int*,int*,int*,int*) = nullptr;
    void        (*m_setScreenClamping)(int) = nullptr;
    void        (*m_settingsMarginsX)(int*,int*,int*,int*) = nullptr;
    void        (*m_settingsContentX)(int*,int*,int*) = nullptr;
    int         (*m_setHudTitle)(const char*,int) = nullptr;
    int         (*m_setThemeContentBorder)(float,float,float,float) = nullptr;
    int         (*m_setThemeContentMargin)(float,float,float,float) = nullptr;
    int         (*m_setThemeTitleMargin)(float,float,float,float) = nullptr;
    int         (*m_hudCardRect)(const char*,int*) = nullptr;
    void        (*m_gapBarForceGap)(int,int) = nullptr;
    void        (*m_updateSetAvailable)(const char*) = nullptr;
    void        (*m_versionRowTerms)(int*,int*) = nullptr;
    void        (*m_showAllHuds)(int) = nullptr;
    unsigned long (*m_effColor)(int) = nullptr;
    int         (*m_colorOverridden)(int) = nullptr;
    unsigned long (*m_themeOrDefColor)(int) = nullptr;
    void          (*m_themeInfo)(const char*, char*, int) = nullptr;
    void          (*m_gaugesInfo)(const char*, char*, int) = nullptr;
    void        (*m_cycleColor)(int,int) = nullptr;
    void        (*m_clearColorOv)(int) = nullptr;
    void        (*m_setThemeColor)(int,unsigned long) = nullptr;
    void        (*m_effFont)(int,char*,int) = nullptr;
    int         (*m_fontOverridden)(int) = nullptr;
    void        (*m_cycleFont)(int,int) = nullptr;
    void        (*m_clearFontOv)(int) = nullptr;
    void        (*m_setFont)(int,const char*) = nullptr;
    int         (*m_fontCount)() = nullptr;
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
    void        (*m_installGamepad)(const char*, float) = nullptr;
    void        (*m_clearGamepads)() = nullptr;
    void        (*m_installPitboard)(const char*, float, float) = nullptr;
    void        (*m_reloadAssetLayouts)() = nullptr;
    float       (*m_pitboardArtWidth)() = nullptr;
    float       (*m_gamepadArtWidth)() = nullptr;
    void        (*m_clearPitboards)() = nullptr;
    void        (*m_cyclePack)(int, int) = nullptr;
    int         (*m_packShowBg)(int) = nullptr;
    int         (*m_gamepadStemSrc)(const char*, int) = nullptr;
    float       (*m_gamepadGeomWidth)(const char*) = nullptr;
    int         (*m_pitboardStemSrc)(const char*, int) = nullptr;
    float       (*m_pitboardPackArtW)(const char*) = nullptr;
    unsigned    (*m_pitboardTextColor)(const char*) = nullptr;
    int         (*m_padStemCount)() = nullptr;
    void        (*m_padStemName)(int, char*, int) = nullptr;
    int         (*m_spriteOrderMism)() = nullptr;
    int         (*m_spriteOrderSwap)(int, int) = nullptr;
    void        (*m_setPackShowBg)(int, int) = nullptr;
    void        (*m_setPitboardPack)(const char*) = nullptr;
    void        (*m_pitboardStored)(char*, int) = nullptr;
    void        (*m_pitboardActive)(char*, int) = nullptr;
    void        (*m_setGamepadPack)(const char*) = nullptr;
    void        (*m_gamepadStored)(char*, int) = nullptr;
    void        (*m_gamepadActive)(char*, int) = nullptr;
    void        (*m_setUiFontSize)(float) = nullptr;
    void        (*m_setBoxTerm)(int, const char*) = nullptr;
    void        (*m_forceSurface)(int) = nullptr;
    void        (*m_rcSetVisible)(int) = nullptr;
    void        (*m_rcSetCharts)(int) = nullptr;
    void        (*m_fmxSetNow)(long long) = nullptr;
    void        (*m_fmxState)(int*,int*,int*,int*,int*,int*,int*,int*) = nullptr;
    void        (*m_statsSetNow)(long long) = nullptr;
    void        (*m_statsOdoState)(double*,double*,double*,int*) = nullptr;
    void        (*m_statsSave)() = nullptr;
    int         (*m_crashTally)(int) = nullptr;
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
