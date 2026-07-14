// ============================================================================
// core/plugin_manager.cpp
// Main entry point and coordinator for all plugin lifecycle events
// ============================================================================
#include "plugin_manager.h"
#include "plugin_constants.h"
#include "plugin_data.h"
#include "plugin_utils.h"
#include "../diagnostics/logger.h"
#include "../diagnostics/timer.h"
#include "hud_manager.h"
#include "input_manager.h"
#include "hotkey_manager.h"
#include "plugin_thread.h"
#include "asset_manager.h"
#include "../handlers/draw_handler.h"
#include "../handlers/event_handler.h"
#include "../handlers/race_event_handler.h"
#include "../handlers/race_session_handler.h"
#include "../handlers/race_entry_handler.h"
#include "../handlers/race_lap_handler.h"
#include "../handlers/race_split_handler.h"
#include "../handlers/race_classification_handler.h"
#include "../handlers/race_track_position_handler.h"
#include "../handlers/race_communication_handler.h"
#include "../handlers/run_handler.h"
#include "../handlers/run_lap_handler.h"
#include "../handlers/run_split_handler.h"
#include "../handlers/run_telemetry_handler.h"
#include "../handlers/track_centerline_handler.h"
#include "../handlers/race_vehicle_data_handler.h"
#include "../handlers/spectate_handler.h"
#include "tracked_riders_manager.h"
#include "rumble_profile_manager.h"
#include "xinput_reader.h"
#include "stats_manager.h"
#include "update_checker.h"
#include "update_downloader.h"
#if GAME_HAS_DISCORD
#include "discord_manager.h"
#endif
#if GAME_HAS_STEAM_FRIENDS
#include "steam_friends_manager.h"
#endif
#if GAME_HAS_HTTP_SERVER
#include "http_server.h"
#endif
#if GAME_HAS_ANALYTICS
#include "analytics_manager.h"
#endif
#if GAME_HAS_RECORDER
#include "event_recorder.h"
#endif
#include "crash_handler.h"
#include <cstring>
#include <vector>
#include <windows.h>

using namespace PluginConstants;

// RAII helper macro to automatically measure and accumulate callback execution time
// Also records per-callback timing for the benchmark widget when active
// Usage: Add ACCUMULATE_CALLBACK_TIME(name) at the start of any plugin callback
#define ACCUMULATE_CALLBACK_TIME_NAMED(callbackName) \
    static int _cbIdx = -1; \
    struct _ScopedCallbackTimer { \
        long long _start; \
        int _idx; \
        _ScopedCallbackTimer(int& idx) : _start(DrawHandler::getCurrentTimeUs()), _idx(idx) { \
            auto& bm = PluginData::getInstance().getBenchmarkMetrics(); \
            if (idx < 0 || idx >= bm.callbackCount) { idx = bm.registerCallback(callbackName); } \
            _idx = idx; \
        } \
        ~_ScopedCallbackTimer() { \
            long long elapsed = DrawHandler::getCurrentTimeUs() - _start; \
            DrawHandler::accumulateCallbackTime(elapsed); \
            auto& bm = PluginData::getInstance().getBenchmarkMetrics(); \
            if (bm.active && _idx >= 0) { bm.recordCallback(_idx, elapsed); } \
        } \
    } _cbtimer(_cbIdx)

// Backward-compatible version (no per-callback recording)
#define ACCUMULATE_CALLBACK_TIME() \
    struct _ScopedCallbackTimerSimple { \
        long long _start; \
        _ScopedCallbackTimerSimple() : _start(DrawHandler::getCurrentTimeUs()) {} \
        ~_ScopedCallbackTimerSimple() { DrawHandler::accumulateCallbackTime(DrawHandler::getCurrentTimeUs() - _start); } \
    } _cbtimer

PluginManager::PluginManager() {
    m_savePath[0] = '\0';
}

PluginManager& PluginManager::getInstance() {
    static PluginManager instance;
    return instance;
}

void PluginManager::initialize(const char* savePath) {
    // Initialize logger first (so we can log everything else)
    Logger::getInstance().initialize(savePath);

    // Install crash handler immediately after the logger so any unhandled
    // hardware fault (AV, stack overflow, divide-by-zero) writes a
    // minidump before the host dies. This doesn't prevent crashes — it
    // just gives us a .dmp to debug from.
    CrashHandler::install(savePath);

    // From here on we hold a process-wide unhandled-exception filter
    // pointing into this DLL. If init throws and the game unloads us
    // (Startup returns -1), that filter would be left dangling in
    // unmapped memory — and the next host-side fault anywhere in the
    // process would jump to garbage, which is exactly the failure mode
    // this PR is meant to prevent. Catch + uninstall + rethrow so init
    // is transactional w.r.t. the SEH filter.
    try {

    // Discover assets (syncs user overrides, then scans plugin data directory)
    // Must happen before HudManager::initialize() which sets up resources
    AssetManager::getInstance().discoverAssets(savePath);

    // Initialize components
    InputManager::getInstance().initialize();
    HotkeyManager::getInstance().initialize();
    HudManager::getInstance().initialize();   // loads settings (sets the [Recorder] enabled flag)

#if GAME_HAS_RECORDER
    // Hidden dev tool: if [Recorder] enabled=1, open a fresh session tape now —
    // before any EventInit/telemetry/race callbacks arrive — and record Startup.
    // No-op (and zero cost) when disabled, which is the default.
    EventRecorder::getInstance().beginSessionRecording(savePath);
#endif

#if GAME_HAS_DISCORD
    // Initialize Discord Rich Presence (runs in background thread)
    DiscordManager::getInstance().initialize();
#endif

#if GAME_HAS_STEAM_FRIENDS
    // Experimental Steam friends probe (game thread only, no background thread).
    // Hooks the game's already-loaded steam_api64.dll; safe no-op if absent.
    SteamFriendsManager::getInstance().initialize();
#endif

#if GAME_HAS_HTTP_SERVER
    // Initialize HTTP server (starts if enabled via settings)
    HttpServer::getInstance().initialize(savePath);
#endif

#if GAME_HAS_ANALYTICS
    // Fire the anonymous usage beacon (background thread, fire-and-forget).
    // Must run AFTER settings load (HudManager::initialize) so the enabled flag
    // and HUD visibility reflect the user's config.
    AnalyticsManager::getInstance().initialize(savePath);
#endif

    // Start the XInput I/O thread once settings (controller index / rumble config) are
    // loaded, so all XInputGetState/XInputSetState runs off the game/worker thread. A
    // degraded controller driver can then never stall whichever thread drives telemetry.
    XInputReader::getInstance().startIoThread();

    // Experimental: spawn the plugin worker thread LAST, once all singletons above
    // are initialized and settings (the [Advanced] pluginThread flag) are loaded.
    // A no-op unless the flag is on. From here, callbacks + the HUD render build run
    // off the game thread. See core/plugin_thread.{h,cpp}.
    PluginThread::getInstance().start();

    DEBUG_INFO("PluginManager initialized");

    } catch (...) {
        // Roll back anything that may have spawned background threads
        // before the throw. The C++ exception barrier at the DLL boundary
        // (API_GUARD_CATCH in Startup) returns -1 on rethrow, which makes
        // the game unload our DLL. If Discord's connection thread or
        // HttpServer's listen thread is still alive at that point, it
        // outlives the DLL's mapped memory and faults the next time it
        // executes an instruction. Both shutdown() methods early-exit
        // cleanly if their thread wasn't spawned yet.
        //
        // INVARIANT: this list must cover every background thread spawned
        // during initialize() above. Currently HttpServer + DiscordManager
        // + AnalyticsManager (its beacon + custom-event-worker threads, both
        // joined by AnalyticsManager::shutdown()).
        // UpdateChecker/UpdateDownloader/RecordsHud start their threads
        // later via user action, so they're not relevant here. If you add
        // another initialize() call that spawns a thread, add its
        // shutdown() here.
        //
        // Each rollback step runs in its own try/catch so a throw from one
        // shutdown() doesn't skip the rest. Leaving a thread alive past
        // DLL unload (the game does that on our -1 return) crashes the host
        // when the thread next executes any instruction — much worse than
        // a swallowed shutdown exception.
        try { PluginThread::getInstance().stop(); } catch (...) {}
        try { XInputReader::getInstance().stopIoThread(); } catch (...) {}
#if GAME_HAS_ANALYTICS
        try { AnalyticsManager::getInstance().shutdown(); } catch (...) {}
#endif
#if GAME_HAS_HTTP_SERVER
        try { HttpServer::getInstance().shutdown(); } catch (...) {}
#endif
#if GAME_HAS_DISCORD
        try { DiscordManager::getInstance().shutdown(); } catch (...) {}
#endif
#if GAME_HAS_STEAM_FRIENDS
        // No background thread, but initialize() may have published presence -
        // clear it so a failed init doesn't leave stale rich presence in Steam.
        try { SteamFriendsManager::getInstance().shutdown(); } catch (...) {}
#endif
        try { CrashHandler::uninstall(); } catch (...) {}
        throw;
    }
}

void PluginManager::shutdown() {
    if (m_bShutdown) return;
    m_bShutdown = true;
    DEBUG_INFO("PluginManager shutdown");

    // Stop the plugin worker thread FIRST (before any singleton it touches is torn
    // down). stop() joins it and then drains any still-queued callbacks inline, so
    // PluginData is consistent for the stats/settings saves below. No-op if off.
    PluginThread::getInstance().stop();

    // Then stop the XInput I/O thread — AFTER the plugin worker, since that worker is
    // what posts rumble / reads the controller snapshot in threaded mode. stopIoThread
    // joins it and sends a final motors-off so the pad doesn't keep buzzing.
    XInputReader::getInstance().stopIoThread();

#if GAME_HAS_RECORDER
    // Finalize the callback tape on the clean shutdown path (records the Shutdown
    // event and updates the header). Self-contained (own FILE* only). If the game
    // exits without calling Shutdown(), ~EventRecorder still finalizes the tape.
    EventRecorder::getInstance().recordShutdown();
    EventRecorder::getInstance().stopRecording();
#endif

#if GAME_HAS_ANALYTICS
    // Join the one-shot usage beacon thread (cancels any in-flight POST).
    AnalyticsManager::getInstance().shutdown();
#endif
#if GAME_HAS_HTTP_SERVER
    // Shutdown network threads first (these may have blocking I/O)
    HttpServer::getInstance().shutdown();
#endif
    UpdateChecker::getInstance().shutdown();
    UpdateDownloader::getInstance().shutdown();

#if GAME_HAS_DISCORD
    // Shutdown Discord Rich Presence (clears presence from Discord)
    DiscordManager::getInstance().shutdown();
#endif

#if GAME_HAS_STEAM_FRIENDS
    // Clear our Steam rich presence
    SteamFriendsManager::getInstance().shutdown();
#endif

    // Save rumble profiles before shutdown
    RumbleProfileManager::getInstance().save();

    // Record race finish if player quit mid-race (ALT-F4, etc.) and save stats
    StatsManager::getInstance().tryRecordRaceFinish(PluginData::getInstance());
    StatsManager::getInstance().recordSessionEnd();
    StatsManager::getInstance().save();

    // Shutdown HUD manager (its own settings save on the way out is synchronous) — the
    // backstop that persists any deferred settings changes not yet flushed on leave-track.
    HudManager::getInstance().shutdown();

    // Shutdown input manager
    InputManager::getInstance().shutdown();

    // Clear plugin data store
    PluginData::getInstance().clear();

    // Restore the previous unhandled exception filter — if our DLL is
    // about to unload, leaving a dangling filter pointing into freed
    // memory would itself become a crash bomb.
    CrashHandler::uninstall();

    // Shutdown logger last (so we can log everything else)
    Logger::getInstance().shutdown();
}

int PluginManager::handleStartup(const char* savePath) {
    // Safety: Check for null pointer from API
    if (savePath != nullptr) {
        strncpy_s(m_savePath, sizeof(m_savePath), savePath, sizeof(m_savePath) - 1);
        m_savePath[sizeof(m_savePath) - 1] = '\0';
    } else {
        m_savePath[0] = '\0';
    }

    // Initialize with savePath (Logger is initialized first and will log startup info)
    initialize(m_savePath);

    // Load tracked riders from disk
    TrackedRidersManager::getInstance().load(m_savePath);

    // Load rumble profiles from disk
    RumbleProfileManager::getInstance().load(m_savePath);

    // Load unified stats from disk (includes PB, odometer, and track/bike stats)
    StatsManager::getInstance().load(m_savePath);

    if (savePath != nullptr) {
        DEBUG_INFO_F("Startup called with save path: %s", savePath);
    } else {
        DEBUG_WARN("Startup called with NULL save path");
    }

    // NOTE: API docs say -1 = disable is valid, but game rejects it and unloads plugin
    // CHANGE TELEMETRY RATE HERE: Options are TELEMETRY_RATE_10HZ, _20HZ, _50HZ, _100HZ
    return TELEMETRY_RATE_100HZ;
}

void PluginManager::handleShutdown() {
    SCOPED_TIMER_THRESHOLD("Plugin::handleShutdown", 1000);
    DEBUG_INFO("=== Shutdown ===");
    shutdown();
    m_savePath[0] = '\0';
}

void PluginManager::handleEventInit(Unified::VehicleEventData* psEventData) {
    if (psEventData && PluginThread::getInstance().offload(this, &PluginManager::handleEventInit, *psEventData)) return;
    ACCUMULATE_CALLBACK_TIME_NAMED("EventInit");
    SCOPED_TIMER_THRESHOLD("Plugin::handleEventInit", 100);
    DEBUG_INFO("=== Event Init ===");

    EventHandler::getInstance().handleEventInit(psEventData);
}

void PluginManager::handleEventDeinit() {
    if (PluginThread::getInstance().offload(this, &PluginManager::handleEventDeinit)) return;
    ACCUMULATE_CALLBACK_TIME_NAMED("EventDeinit");
    SCOPED_TIMER_THRESHOLD("Plugin::handleEventDeinit", 100);
    DEBUG_INFO("=== Event Deinit ===");

    EventHandler::getInstance().handleEventDeinit();
}

void PluginManager::handleRunInit(Unified::SessionData* psSessionData) {
    if (psSessionData && PluginThread::getInstance().offload(this, &PluginManager::handleRunInit, *psSessionData)) return;
    ACCUMULATE_CALLBACK_TIME_NAMED("RunInit");
    SCOPED_TIMER_THRESHOLD("Plugin::handleRunInit", 100);
    DEBUG_INFO("=== Run Init ===");

    RunHandler::getInstance().handleRunInit(psSessionData);
}

void PluginManager::handleRunDeinit() {
    if (PluginThread::getInstance().offload(this, &PluginManager::handleRunDeinit)) return;
    ACCUMULATE_CALLBACK_TIME_NAMED("RunDeinit");
    SCOPED_TIMER_THRESHOLD("Plugin::handleRunDeinit", 100);
    DEBUG_INFO("=== Run Deinit ===");

    RunHandler::getInstance().handleRunDeinit();
}

void PluginManager::handleRunStart() {
    if (PluginThread::getInstance().offload(this, &PluginManager::handleRunStart)) return;
    ACCUMULATE_CALLBACK_TIME_NAMED("RunStart");
    SCOPED_TIMER_THRESHOLD("Plugin::handleRunStart", 100);
    DEBUG_INFO("=== Run Start ===");

    RunHandler::getInstance().handleRunStart();
}

void PluginManager::handleRunStop() {
    if (PluginThread::getInstance().offload(this, &PluginManager::handleRunStop)) return;
    ACCUMULATE_CALLBACK_TIME_NAMED("RunStop");
    SCOPED_TIMER_THRESHOLD("Plugin::handleRunStop", 100);
    DEBUG_INFO("=== Run Stop ===");

    RunHandler::getInstance().handleRunStop();

    // Window refresh and HUD validation now happens automatically
    // when cursor is re-enabled (see InputManager::updateFrame)
}

void PluginManager::handleRunLap(Unified::PlayerLapData* psLapData) {
    if (psLapData && PluginThread::getInstance().offload(this, &PluginManager::handleRunLap, *psLapData)) return;
    ACCUMULATE_CALLBACK_TIME_NAMED("RunLap");
    SCOPED_TIMER_THRESHOLD("Plugin::handleRunLap", 500);
    DEBUG_INFO("=== Run Lap ===");

    RunLapHandler::getInstance().handleRunLap(psLapData);
}

void PluginManager::handleRunSplit(Unified::PlayerSplitData* psSplitData) {
    if (psSplitData && PluginThread::getInstance().offload(this, &PluginManager::handleRunSplit, *psSplitData)) return;
    ACCUMULATE_CALLBACK_TIME_NAMED("RunSplit");
    SCOPED_TIMER_THRESHOLD("Plugin::handleRunSplit", 500);
    DEBUG_INFO("=== Run Split ===");

    RunSplitHandler::getInstance().handleRunSplit(psSplitData);
}

void PluginManager::handleRunTelemetry(Unified::TelemetryData* psTelemetryData) {
    if (psTelemetryData && PluginThread::getInstance().offload(this, &PluginManager::handleRunTelemetry, *psTelemetryData)) return;
    ACCUMULATE_CALLBACK_TIME_NAMED("RunTelemetry");
    SCOPED_TIMER_THRESHOLD("Plugin::handleRunTelemetry", 100);
    // Skip logging (high-frequency event - runs at telemetry rate)

    // Delegate to handler
    RunTelemetryHandler::getInstance().handleRunTelemetry(psTelemetryData);
}

int PluginManager::handleDrawInit(int* piNumSprites, char** pszSpriteName, int* piNumFonts, char** pszFontName) {
    SCOPED_TIMER_THRESHOLD("Plugin::handleDrawInit", 1000);
    DEBUG_INFO("=== Draw Init ===");

    // Safety: Check for null pointers from API
    if (piNumSprites == nullptr || pszSpriteName == nullptr ||
        piNumFonts == nullptr || pszFontName == nullptr) {
        DEBUG_WARN("handleDrawInit called with NULL pointer(s)");
        return 0;
    }

    // Delegate resource initialization to HudManager
    return HudManager::getInstance().initializeResources(piNumSprites, pszSpriteName, piNumFonts, pszFontName);
}

void PluginManager::handleDraw(int iState, int* piNumQuads, void** ppQuad, int* piNumString, void** ppString) {
    // Apply any live mode switch first (game thread). A RELOAD_CONFIG hotkey that
    // flipped [Advanced] pluginThread starts/stops the worker here, so legacy<->threaded
    // can change without a game restart. No-op when the flag already matches.
    PluginThread& pt = PluginThread::getInstance();
    pt.reconcileEnabled();

    // Threaded mode: the game thread does NO HUD work here. Ask the worker to build
    // this frame (non-blocking) and hand back the most recently finished, triple-
    // buffered frame. A hiccup on our side can never stall the game's Draw.
    if (pt.enabled()) {
        if (piNumQuads == nullptr || ppQuad == nullptr ||
            piNumString == nullptr || ppString == nullptr) {
            return;
        }
        pt.requestFrame(iState);
        const SPluginQuad_t* q = nullptr;
        const SPluginString_t* s = nullptr;
        int nq = 0, ns = 0;
        if (pt.takeFrame(q, nq, s, ns)) {
            *piNumQuads = nq;
            *ppQuad = const_cast<SPluginQuad_t*>(q);
            *piNumString = ns;
            *ppString = const_cast<SPluginString_t*>(s);
        }
        // else: the export wrapper already zeroed the outputs (no frame built yet).
        return;
    }

    ACCUMULATE_CALLBACK_TIME_NAMED("Draw");

    // Delegate to DrawHandler for performance tracking and rendering
    DrawHandler::getInstance().handleDraw(iState, piNumQuads, ppQuad, piNumString, ppString);
}

void PluginManager::handleTrackCenterline(int iNumSegments, Unified::TrackSegment* pasSegment, void* pRaceData) {
    {
        PluginThread& pt = PluginThread::getInstance();
        if (pt.enabled() && !pt.onWorkerThread()) {
            int cnt = (pasSegment && iNumSegments > 0) ? iNumSegments : 0;
            std::vector<Unified::TrackSegment> segs(pasSegment, pasSegment + cnt);
            // pRaceData is [S/F, split1, split2, holeshot] (4 floats) or null.
            std::vector<float> race;
            if (pRaceData) {
                const float* rd = static_cast<const float*>(pRaceData);
                race.assign(rd, rd + 4);
            }
            pt.enqueue([this, segs, race]() mutable {
                handleTrackCenterline(static_cast<int>(segs.size()),
                                      segs.empty() ? nullptr : segs.data(),
                                      race.empty() ? nullptr : race.data());
            });
            return;
        }
    }
    ACCUMULATE_CALLBACK_TIME_NAMED("TrackCenterline");
    SCOPED_TIMER_THRESHOLD("Plugin::handleTrackCenterline", 100);
    DEBUG_INFO("=== Track Centerline ===");

    TrackCenterlineHandler::getInstance().handleTrackCenterline(iNumSegments, pasSegment, pRaceData);
}

void PluginManager::handleRaceEvent(Unified::RaceEventData* psRaceEvent) {
    if (psRaceEvent && PluginThread::getInstance().offload(this, &PluginManager::handleRaceEvent, *psRaceEvent)) return;
    ACCUMULATE_CALLBACK_TIME_NAMED("RaceEvent");
    SCOPED_TIMER_THRESHOLD("Plugin::handleRaceEvent", 100);
    DEBUG_INFO("=== Race Event ===");

    RaceEventHandler::getInstance().handleRaceEvent(psRaceEvent);
}

void PluginManager::handleRaceDeinit() {
    if (PluginThread::getInstance().offload(this, &PluginManager::handleRaceDeinit)) return;
    ACCUMULATE_CALLBACK_TIME_NAMED("RaceDeinit");
    SCOPED_TIMER_THRESHOLD("Plugin::handleRaceDeinit", 100);
    DEBUG_INFO("=== Race Deinit ===");

    // Delegate to handler
    RaceEventHandler::getInstance().handleRaceDeinit();
}

void PluginManager::handleRaceAddEntry(Unified::RaceEntryData* psRaceAddEntry) {
    if (psRaceAddEntry && PluginThread::getInstance().offload(this, &PluginManager::handleRaceAddEntry, *psRaceAddEntry)) return;
    ACCUMULATE_CALLBACK_TIME_NAMED("RaceAddEntry");
    SCOPED_TIMER_THRESHOLD("Plugin::handleRaceAddEntry", 500);
    DEBUG_INFO("=== Race Add Entry ===");

    RaceEntryHandler::getInstance().handleRaceAddEntry(psRaceAddEntry);
}

void PluginManager::handleRaceRemoveEntry(int raceNum) {
    if (PluginThread::getInstance().offloadValue(this, &PluginManager::handleRaceRemoveEntry, raceNum)) return;
    ACCUMULATE_CALLBACK_TIME_NAMED("RaceRemoveEntry");
    SCOPED_TIMER_THRESHOLD("Plugin::handleRaceRemoveEntry", 100);
    DEBUG_INFO("=== Race Remove Entry ===");

    RaceEntryHandler::getInstance().handleRaceRemoveEntry(raceNum);
}

void PluginManager::handleRaceSession(Unified::RaceSessionData* psRaceSession) {
    if (psRaceSession && PluginThread::getInstance().offload(this, &PluginManager::handleRaceSession, *psRaceSession)) return;
    ACCUMULATE_CALLBACK_TIME_NAMED("RaceSession");
    SCOPED_TIMER_THRESHOLD("Plugin::handleRaceSession", 100);
    DEBUG_INFO("=== Race Session ===");

    RaceSessionHandler::getInstance().handleRaceSession(psRaceSession);
}

void PluginManager::handleRaceSessionState(Unified::RaceSessionStateData* psRaceSessionState) {
    if (psRaceSessionState && PluginThread::getInstance().offload(this, &PluginManager::handleRaceSessionState, *psRaceSessionState)) return;
    ACCUMULATE_CALLBACK_TIME_NAMED("RaceSessionState");
    SCOPED_TIMER_THRESHOLD("Plugin::handleRaceSessionState", 100);
    DEBUG_INFO("=== Race Session State ===");

    RaceSessionHandler::getInstance().handleRaceSessionState(psRaceSessionState);
}

void PluginManager::handleRaceLap(Unified::RaceLapData* psRaceLap) {
    if (psRaceLap && PluginThread::getInstance().offload(this, &PluginManager::handleRaceLap, *psRaceLap)) return;
    ACCUMULATE_CALLBACK_TIME_NAMED("RaceLap");
    SCOPED_TIMER_THRESHOLD("Plugin::handleRaceLap", 500);
    DEBUG_INFO("=== Race Lap ===");

    RaceLapHandler::getInstance().handleRaceLap(psRaceLap);
}

void PluginManager::handleRaceSplit(Unified::RaceSplitData* psRaceSplit) {
    if (psRaceSplit && PluginThread::getInstance().offload(this, &PluginManager::handleRaceSplit, *psRaceSplit)) return;
    ACCUMULATE_CALLBACK_TIME_NAMED("RaceSplit");
    SCOPED_TIMER_THRESHOLD("Plugin::handleRaceSplit", 500);
    DEBUG_INFO("=== Race Split ===");
    RaceSplitHandler::getInstance().handleRaceSplit(psRaceSplit);
}

void PluginManager::handleRaceSpeed(Unified::RaceSpeedData* psRaceSpeed) {
    ACCUMULATE_CALLBACK_TIME_NAMED("RaceSpeed");
    SCOPED_TIMER_THRESHOLD("Plugin::handleRaceSpeed", 100);
    DEBUG_INFO("=== Race Speed ===");
    // TODO: Implement race speed handling if needed (GP Bikes, WRS, KRP only)
}

// NOTE: RaceHoleshot callback (MX Bikes API)
// The API defines SPluginsRaceHoleshot_t and a RaceHoleshot export, but in practice
// the game never fires this callback. The plugin takes NO gameplay action on it, so
// there is no handleRaceHoleshot here. mxb_api.cpp DOES export RaceHoleshot, but only
// to RECORD it (so a captured tape stays complete if PiBoSo ever starts firing it);
// if real holeshot handling is ever added, it goes in that export next to the tap.

void PluginManager::handleRaceCommunication(Unified::RaceCommunicationData* psRaceCommunication) {
    if (psRaceCommunication && PluginThread::getInstance().offload(this, &PluginManager::handleRaceCommunication, *psRaceCommunication)) return;
    ACCUMULATE_CALLBACK_TIME_NAMED("RaceComm");
    SCOPED_TIMER_THRESHOLD("Plugin::handleRaceCommunication", 500);
    DEBUG_INFO("=== Race Communication ===");

    RaceCommunicationHandler::getInstance().handleRaceCommunication(psRaceCommunication);
}

void PluginManager::handleRaceClassification(Unified::RaceClassificationData* psRaceClassification, Unified::RaceClassificationEntry* pasRaceClassificationEntry, int iNumEntries) {
    {
        PluginThread& pt = PluginThread::getInstance();
        if (pt.enabled() && !pt.onWorkerThread() && psRaceClassification) {
            Unified::RaceClassificationData cls = *psRaceClassification;
            int cnt = (pasRaceClassificationEntry && iNumEntries > 0) ? iNumEntries : 0;
            std::vector<Unified::RaceClassificationEntry> entries(
                pasRaceClassificationEntry, pasRaceClassificationEntry + cnt);
            pt.enqueue([this, cls, entries, cnt]() mutable {
                handleRaceClassification(&cls, entries.empty() ? nullptr : entries.data(), cnt);
            });
            return;
        }
    }
    ACCUMULATE_CALLBACK_TIME_NAMED("Classification");
    SCOPED_TIMER_THRESHOLD("Plugin::handleRaceClassification", 100);
    // Skip logging (high-frequency event)

    RaceClassificationHandler::getInstance().handleRaceClassification(
        psRaceClassification,
        pasRaceClassificationEntry,
        iNumEntries
    );
}

void PluginManager::handleRaceTrackPosition(int iNumVehicles, Unified::TrackPositionData* pasRaceTrackPosition) {
    {
        PluginThread& pt = PluginThread::getInstance();
        if (pt.enabled() && !pt.onWorkerThread()) {
            int cnt = (pasRaceTrackPosition && iNumVehicles > 0) ? iNumVehicles : 0;
            std::vector<Unified::TrackPositionData> v(
                pasRaceTrackPosition, pasRaceTrackPosition + cnt);
            pt.enqueue([this, v]() mutable {
                handleRaceTrackPosition(static_cast<int>(v.size()), v.empty() ? nullptr : v.data());
            });
            return;
        }
    }
    ACCUMULATE_CALLBACK_TIME_NAMED("TrackPosition");
    // SCOPED_TIMER_THRESHOLD("Plugin::handleRaceTrackPosition", 500);  // Commented out - too noisy for debugging
    // Skip logging (high-frequency event - runs at vehicle update rate)

    // Null check and bounds validation moved to handler
    RaceTrackPositionHandler::getInstance().handleRaceTrackPosition(iNumVehicles, pasRaceTrackPosition);
}

void PluginManager::handleRaceVehicleData(Unified::RaceVehicleData* psRaceVehicleData) {
    if (psRaceVehicleData && PluginThread::getInstance().offload(this, &PluginManager::handleRaceVehicleData, *psRaceVehicleData)) return;
    ACCUMULATE_CALLBACK_TIME_NAMED("RaceVehicleData");
    SCOPED_TIMER_THRESHOLD("Plugin::handleRaceVehicleData", 500);
    // Skip logging (high-frequency event)

    RaceVehicleDataHandler::getInstance().handleRaceVehicleData(psRaceVehicleData);
}

int PluginManager::handleSpectateVehicles(int iNumVehicles, Unified::SpectateVehicle* pasVehicleData, int iCurSelection, int* piSelect) {
    return SpectateHandler::getInstance().handleSpectateVehicles(iNumVehicles, pasVehicleData, iCurSelection, piSelect);
}

int PluginManager::handleSpectateCameras(int iNumCameras, void* pCameraData, int iCurSelection, int* piSelect) {
    return SpectateHandler::getInstance().handleSpectateCameras(iNumCameras, pCameraData, iCurSelection, piSelect);
}

void PluginManager::requestSpectateRider(int raceNum) {
    SpectateHandler::getInstance().requestSpectateRider(raceNum);
}
