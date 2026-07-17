// ============================================================================
// core/hud_manager.cpp
// Manages all HUD display elements and coordinates their rendering and updates
// ============================================================================
#include "hud_manager.h"
#include "../diagnostics/logger.h"
#include "../diagnostics/timer.h"
#include "asset_manager.h"
#include "companion_window.h"
#include "input_manager.h"
#include "xinput_reader.h"
#include "plugin_data.h"
#include "plugin_manager.h"
#include "settings_manager.h"
#include "director_manager.h"
#include "profile_manager.h"
#include "ui_config.h"
#include "../hud/base_hud.h"
#include "../hud/standings_hud.h"
#include "../hud/performance_hud.h"
#include "../hud/telemetry_hud.h"
#include "../hud/ideal_lap_hud.h"
#include "../hud/lap_log_hud.h"
#include "../hud/friends_hud.h"
#include "../hud/time_widget.h"
#include "../hud/position_widget.h"
#include "../hud/lap_widget.h"
#include "../hud/session_hud.h"
#include "../hud/speed_widget.h"
#include "../hud/gear_widget.h"
#include "../hud/speedo_widget.h"
#include "../hud/tacho_widget.h"
#include "../hud/timing_hud.h"
#include "../hud/bars_widget.h"
#include "../hud/version_widget.h"
#include "../hud/notices_hud.h"
#include "../hud/settings_hud.h"
#include "../hud/settings_button_widget.h"
#include "../hud/map_hud.h"
#include "../hud/radar_hud.h"
#include "../hud/pitboard_hud.h"
#include "../hud/fuel_widget.h"
#if GAME_HAS_RECORDS_PROVIDER
#include "../hud/records_hud.h"
#endif
#include "../hud/gap_bar_hud.h"
#include "../hud/pointer_widget.h"
#include "../hud/rumble_hud.h"
#include "../hud/director_widget.h"
#include "../hud/gamepad_widget.h"
#include "../hud/lean_widget.h"
#include "../hud/gforce_widget.h"
#include "../hud/compass_widget.h"
#include "../hud/clock_widget.h"
#if GAME_HAS_TYRE_TEMP
#include "../hud/tyre_temp_widget.h"
#endif
#if GAME_HAS_ECU
#include "../hud/ecu_widget.h"
#endif
#include "../hud/session_charts_hud.h"
#include "../hud/helmet_overlay_hud.h"
#include "../hud/fmx_hud.h"
#include "../hud/stats_hud.h"
#include "../hud/event_log_hud.h"
#include "../hud/benchmark_widget.h"
#include "hotkey_manager.h"
#if GAME_HAS_HTTP_SERVER
#include "http_server.h"
#endif
#include "../handlers/draw_handler.h"
#include "color_config.h"
#include <windows.h>
#include <algorithm>
#include <memory>
#include <cstring>
#if defined(MXBMRP3_TEST_BUILD)
#include <atomic>
#endif

HudManager& HudManager::getInstance() {
    static HudManager instance;
    return instance;
}

HudManager::~HudManager() {
    // A Meyers singleton's destructor only runs during static (DLL-detach) teardown.
    // At that point the singletons this teardown reaches — SettingsManager (the
    // settings auto-save), UiConfig, PluginManager — may ALREADY be destroyed:
    // statics are torn down in reverse construction order, and SettingsManager is
    // constructed lazily from inside HudManager::initialize(), so it is constructed
    // AFTER us and therefore destroyed BEFORE us. Running the auto-save from here
    // then walks a freed container and faults the host on exit — observed as an
    // access violation in SettingsManager::serializeSettings() -> m_hudDefaults.find()
    // reading a freed unordered_map bucket array, when the game unloaded the DLL
    // WITHOUT calling the Shutdown() export.
    //
    // The real teardown + auto-save runs from Shutdown() -> PluginManager::shutdown()
    // -> HudManager::shutdown() while every singleton is still alive; leave-track
    // flushIfDirty() already persists in-session edits. If Shutdown() was never called
    // the process is exiting anyway, so skipping this last auto-save is the correct
    // trade-off versus crashing the host.
    //
    // NOTE: shutdownInternal() still emits DEBUG_INFO here, which reaches the Logger
    // singleton — but Logger is the ONE cross-singleton reach that is safe during
    // static teardown: it is constructed first (PluginManager::initialize() inits it
    // before anything touches HudManager), so it is destroyed LAST and is guaranteed
    // alive in every other singleton's destructor. Unlike SettingsManager/CompanionWindow
    // (constructed later, torn down first), Logger cannot be a use-after-free here.
    shutdownInternal(/*allowSave=*/false);
}

void HudManager::initialize() {
    if (m_bInitialized) return;

    DEBUG_INFO("HudManager initializing");

    // Note: AssetManager::discoverAssets() is called by PluginManager before this

    // Pre-allocate render data vectors for optimal performance
    m_quads.reserve(INITIAL_QUAD_CAPACITY);
    m_strings.reserve(INITIAL_STRING_CAPACITY);

    // Setup default resources (this prepares the resource lists)
    setupDefaultResources();

    // Register HUDs
    // Capture pointers to HUDs for SettingsHud and settings persistence
    // Note: Registration order = draw order (first registered = drawn first = behind)
    // Note: Texture base names match files in mxbmrp3_data/textures/ (e.g., "standings_hud" for "standings_hud_1.tga")

    // Helmet overlay registered FIRST so it draws behind all other HUDs/widgets.
    // This way HUD elements (speed, gear, lap, etc.) are always readable on top
    // of the helmet frame and visor tint, rather than being obscured by them.
    auto helmetOverlayPtr = std::make_unique<HelmetOverlayHud>();
    m_pHelmetOverlay = helmetOverlayPtr.get();
    registerHud(std::move(helmetOverlayPtr));

    auto standingsPtr = std::make_unique<StandingsHud>();
    m_pStandings = standingsPtr.get();
    m_pStandings->setTextureBaseName("standings_hud");
    registerHud(std::move(standingsPtr));

    auto mapPtr = std::make_unique<MapHud>();
    m_pMapHud = mapPtr.get();
    m_pMapHud->setTextureBaseName("map_hud");
    registerHud(std::move(mapPtr));

    auto radarPtr = std::make_unique<RadarHud>();
    m_pRadarHud = radarPtr.get();
    m_pRadarHud->setTextureBaseName("radar_hud");
    registerHud(std::move(radarPtr));

    auto lapLogPtr = std::make_unique<LapLogHud>();
    m_pLapLog = lapLogPtr.get();
    m_pLapLog->setTextureBaseName("lap_log_hud");
    registerHud(std::move(lapLogPtr));

#if GAME_HAS_STEAM_FRIENDS
    auto friendsPtr = std::make_unique<FriendsHud>();
    m_pFriends = friendsPtr.get();
    m_pFriends->setTextureBaseName("friends_hud");
    registerHud(std::move(friendsPtr));
#endif

    auto idealLapPtr = std::make_unique<IdealLapHud>();
    m_pIdealLap = idealLapPtr.get();
    m_pIdealLap->setTextureBaseName("ideal_lap_hud");
    registerHud(std::move(idealLapPtr));

    auto telemetryPtr = std::make_unique<TelemetryHud>();
    m_pTelemetry = telemetryPtr.get();
    m_pTelemetry->setTextureBaseName("telemetry_hud");
    registerHud(std::move(telemetryPtr));

    auto performancePtr = std::make_unique<PerformanceHud>();
    m_pPerformance = performancePtr.get();
    m_pPerformance->setTextureBaseName("performance_hud");
    registerHud(std::move(performancePtr));

    auto pitboardPtr = std::make_unique<PitboardHud>();
    m_pPitboard = pitboardPtr.get();
    m_pPitboard->setTextureBaseName("pitboard_hud");
    registerHud(std::move(pitboardPtr));

#if GAME_HAS_RECORDS_PROVIDER
    auto recordsPtr = std::make_unique<RecordsHud>();
    m_pRecords = recordsPtr.get();
    m_pRecords->setTextureBaseName("records_hud");
    registerHud(std::move(recordsPtr));
#endif

    auto sessionChartsPtr = std::make_unique<SessionChartsHud>();
    m_pSessionCharts = sessionChartsPtr.get();
    m_pSessionCharts->setTextureBaseName("session_charts_hud");
    registerHud(std::move(sessionChartsPtr));

#if GAME_HAS_FMX
    auto fmxPtr = std::make_unique<FmxHud>();
    m_pFmxHud = fmxPtr.get();
    m_pFmxHud->setTextureBaseName("fmx_hud");
    registerHud(std::move(fmxPtr));
#endif

    auto statsPtr = std::make_unique<StatsHud>();
    m_pStatsHud = statsPtr.get();
    m_pStatsHud->setTextureBaseName("stats_hud");
    registerHud(std::move(statsPtr));

    auto eventLogPtr = std::make_unique<EventLogHud>();
    m_pEventLog = eventLogPtr.get();
    registerHud(std::move(eventLogPtr));

    // Benchmark Widget (always created, but only accessible via settings when developer mode is on)
    // Note: Must be created unconditionally because isDeveloperMode() returns false here -
    // settings are loaded later in initialize(). The settings tab gates access at render time.
    {
        auto benchmarkPtr = std::make_unique<BenchmarkWidget>();
        m_pBenchmark = benchmarkPtr.get();
        registerHud(std::move(benchmarkPtr));
    }

    // Widgets
    auto lapPtr = std::make_unique<LapWidget>();
    m_pLap = lapPtr.get();
    m_pLap->setTextureBaseName("lap_widget");
    registerHud(std::move(lapPtr));

    auto positionPtr = std::make_unique<PositionWidget>();
    m_pPosition = positionPtr.get();
    m_pPosition->setTextureBaseName("position_widget");
    registerHud(std::move(positionPtr));

    auto timePtr = std::make_unique<TimeWidget>();
    m_pTime = timePtr.get();
    m_pTime->setTextureBaseName("time_widget");
    registerHud(std::move(timePtr));

    auto sessionPtr = std::make_unique<SessionHud>();
    m_pSession = sessionPtr.get();
    m_pSession->setTextureBaseName("session_widget");  // Keep same texture for backwards compatibility
    registerHud(std::move(sessionPtr));

    auto speedPtr = std::make_unique<SpeedWidget>();
    m_pSpeed = speedPtr.get();
    m_pSpeed->setTextureBaseName("speed_widget");
    registerHud(std::move(speedPtr));

    auto gearPtr = std::make_unique<GearWidget>();
    m_pGear = gearPtr.get();
    m_pGear->setTextureBaseName("gear_widget");
    registerHud(std::move(gearPtr));

    auto speedoPtr = std::make_unique<SpeedoWidget>();
    m_pSpeedo = speedoPtr.get();
    m_pSpeedo->setTextureBaseName("speedo_widget");
    registerHud(std::move(speedoPtr));

    auto tachoPtr = std::make_unique<TachoWidget>();
    m_pTacho = tachoPtr.get();
    m_pTacho->setTextureBaseName("tacho_widget");
    registerHud(std::move(tachoPtr));

    auto timingPtr = std::make_unique<TimingHud>();
    m_pTiming = timingPtr.get();
    m_pTiming->setTextureBaseName("timing_hud");
    registerHud(std::move(timingPtr));

    auto gapBarPtr = std::make_unique<GapBarHud>();
    m_pGapBar = gapBarPtr.get();
    m_pGapBar->setTextureBaseName("gap_bar_hud");
    registerHud(std::move(gapBarPtr));

    auto barsPtr = std::make_unique<BarsWidget>();
    m_pBars = barsPtr.get();
    m_pBars->setTextureBaseName("bars_widget");
    registerHud(std::move(barsPtr));

    auto versionPtr = std::make_unique<VersionWidget>();
    m_pVersion = versionPtr.get();
    registerHud(std::move(versionPtr));

    auto noticesPtr = std::make_unique<NoticesHud>();
    m_pNotices = noticesPtr.get();
    registerHud(std::move(noticesPtr));

    auto fuelPtr = std::make_unique<FuelWidget>();
    m_pFuel = fuelPtr.get();
    m_pFuel->setTextureBaseName("fuel_widget");
    registerHud(std::move(fuelPtr));

    auto rumblePtr = std::make_unique<RumbleHud>();
    m_pRumble = rumblePtr.get();
    m_pRumble->setTextureBaseName("rumble_hud");
    registerHud(std::move(rumblePtr));

    auto directorWidgetPtr = std::make_unique<DirectorWidget>();
    m_pDirector = directorWidgetPtr.get();
    registerHud(std::move(directorWidgetPtr));

    auto gamepadPtr = std::make_unique<GamepadWidget>();
    m_pGamepad = gamepadPtr.get();
    m_pGamepad->setTextureBaseName("gamepad_widget");
    registerHud(std::move(gamepadPtr));

    auto leanPtr = std::make_unique<LeanWidget>();
    m_pLean = leanPtr.get();
    m_pLean->setTextureBaseName("lean_widget");
    registerHud(std::move(leanPtr));

    auto gforcePtr = std::make_unique<GForceWidget>();
    m_pGforce = gforcePtr.get();
    m_pGforce->setTextureBaseName("gforce_widget");
    registerHud(std::move(gforcePtr));

    auto compassPtr = std::make_unique<CompassWidget>();
    m_pCompass = compassPtr.get();
    m_pCompass->setTextureBaseName("compass_widget");
    registerHud(std::move(compassPtr));

    auto clockPtr = std::make_unique<ClockWidget>();
    m_pClock = clockPtr.get();
    m_pClock->setTextureBaseName("clock_widget");
    registerHud(std::move(clockPtr));

#if GAME_HAS_TYRE_TEMP
    auto tyreTempPtr = std::make_unique<TyreTempWidget>();
    m_pTyreTemp = tyreTempPtr.get();
    m_pTyreTemp->setTextureBaseName("tyre_temp_widget");
    registerHud(std::move(tyreTempPtr));
#endif

#if GAME_HAS_ECU
    auto ecuPtr = std::make_unique<EcuWidget>();
    m_pEcu = ecuPtr.get();
    m_pEcu->setTextureBaseName("ecu_widget");
    registerHud(std::move(ecuPtr));
#endif

    // Create PointerWidget early so it can be passed to SettingsHud
    // (will be registered last to render on top)
    auto pointerPtr = std::make_unique<PointerWidget>();
    m_pPointer = pointerPtr.get();

    // Register SettingsHud with pointers to all configurable HUDs and widgets
#if GAME_HAS_RECORDS_PROVIDER
    RecordsHud* recordsHudPtr = m_pRecords;
#else
    RecordsHud* recordsHudPtr = nullptr;
#endif
    // Create the settings button up front so SettingsHud can reference it for the
    // Widgets tab (visibility/opacity/scale/texture). It is registered later so it
    // keeps its existing draw order (above SettingsHud, below the pointer).
    auto settingsButtonPtr = std::make_unique<SettingsButtonWidget>();
    m_pSettingsButton = settingsButtonPtr.get();

    auto settingsPtr = std::make_unique<SettingsHud>(m_pIdealLap, m_pLapLog, m_pFriends, m_pSessionCharts, m_pStandings,
                                                       m_pPerformance, m_pTelemetry, m_pTime, m_pPosition, m_pLap, m_pSession, m_pMapHud, m_pRadarHud, m_pSpeed, m_pGear, m_pSpeedo, m_pTacho, m_pTiming, m_pGapBar, m_pBars, m_pVersion, m_pNotices, m_pPitboard, recordsHudPtr, m_pFuel, m_pPointer, m_pRumble, m_pGamepad, m_pLean, m_pGforce, m_pCompass,
                                                       m_pFmxHud,
                                                       m_pStatsHud,
                                                       m_pEventLog,
                                                       m_pClock,
                                                       m_pHelmetOverlay,
                                                       m_pSettingsButton
#if GAME_HAS_TYRE_TEMP
                                                       , m_pTyreTemp
#endif
#if GAME_HAS_ECU
                                                       , m_pEcu
#endif
                                                       );
    m_pSettingsHud = settingsPtr.get();
    registerHud(std::move(settingsPtr));

    // Register SettingsButtonWidget - draggable button to toggle settings
    // (created earlier so SettingsHud could reference it; registered here for draw order)
    registerHud(std::move(settingsButtonPtr));

    // Register PointerWidget last so it renders on top of everything
    registerHud(std::move(pointerPtr));

    // Register all HUDs for benchmark profiling (developer mode only)
    if (m_pBenchmark) {
        auto& bm = PluginData::getInstance().getBenchmarkMetrics();
        for (auto& hud : m_huds) {
            if (hud && hud.get() != m_pBenchmark) {
                const std::string& baseName = hud->getTextureBaseName();
                const char* name;
                if (!baseName.empty()) {
                    name = baseName.c_str();
                } else if (hud.get() == m_pVersion) {
                    name = "version_widget";
                } else if (hud.get() == m_pSettingsHud) {
                    name = "settings_hud";
                } else if (hud.get() == m_pSettingsButton) {
                    name = "settings_button";
                } else if (hud.get() == m_pPointer) {
                    name = "pointer_widget";
                } else if (hud.get() == m_pHelmetOverlay) {
                    name = "helmet_overlay";
                } else {
                    name = "unknown";
                }
                int idx = bm.registerHud(name);
                hud->setBenchmarkIndex(idx);
            }
        }
    }

    // Load settings from disk (must happen after HUD registration)
    SettingsManager::getInstance().loadSettings(*this, PluginManager::getInstance().getSavePath());

    // NOTE: Individual HUD scaling is available via setScale() method.
    // For grid-aligned edges, use scales where (WIDTH_CHARS × scale) = integer:
    //   - StandingsHud (49 chars): 1.0, 2.0, 3.0 only
    //   - PerformanceHud (41 chars): 1.0, 2.0, 3.0 only
    // Non-aligned scales work but edges won't snap to grid perfectly.

    // No observer registration needed - PluginData calls us directly

    m_bInitialized = true;
    DEBUG_INFO("HudManager initialized");
}

void HudManager::shutdown() {
    // Orchestrated path (game's Shutdown() export): every singleton is still alive,
    // so the settings auto-save is safe.
    shutdownInternal(/*allowSave=*/true);
}

void HudManager::shutdownInternal(bool allowSave) {
    if (!m_bInitialized) return;

    DEBUG_INFO("HudManager shutting down");

    // Save settings before clearing HUDs (if auto-save enabled). Only on the
    // orchestrated Shutdown() path — NEVER from the destructor, where the
    // SettingsManager singleton this reaches may already be torn down (see the
    // note in ~HudManager). The `allowSave &&` short-circuit also avoids touching
    // the UiConfig / PluginManager singletons on the destructor path.
    if (allowSave && UiConfig::getInstance().getAutoSave()) {
        SettingsManager::getInstance().saveSettings(*this, PluginManager::getInstance().getSavePath());
    }

    // allowSave doubles as "this is the orchestrated path, every singleton alive":
    // on the destructor path clear() must not reach the CompanionWindow singleton.
    clear(/*allowCrossSingleton=*/allowSave);

    m_bInitialized = false;
    m_bResourcesInitialized = false;
    DEBUG_INFO("HudManager shutdown complete");
}

void HudManager::clear(bool allowCrossSingleton) {
    // Join the companion window thread before tearing anything down — it snapshots
    // primitives under its own lock and touches no HUD state, so this is safe first.
    //
    // Skip this on the static-teardown (destructor) path: CompanionWindow is a
    // separate Meyers singleton that may already be destroyed (reverse construction
    // order), so reaching it via getInstance() would touch freed storage — the same
    // fiasco the settings auto-save hit. We don't NEED to stop it here anyway:
    // ~CompanionWindow() joins its own window thread self-containedly when that
    // singleton is torn down. On the orchestrated Shutdown() path everything is
    // alive, so we still stop it here (deterministic, joins before we continue).
    if (allowCrossSingleton) {
        CompanionWindow::getInstance().stop();
    }

#if GAME_HAS_RECORDS_PROVIDER
    // Join the records fetch thread BEFORE nulling the cached HUD pointers:
    // the worker calls getTimingHud().setDataDirty() on completion, which
    // would deref a null m_pTiming if it fired inside the window below.
    if (m_pRecords) {
        m_pRecords->joinFetchThread();
    }
#endif

    // Reset cached HUD pointers BEFORE destroying the objects
    // This prevents any dangling pointer window (defensive programming)
    m_pIdealLap = nullptr;
    m_pLapLog = nullptr;
    m_pFriends = nullptr;
    m_pStandings = nullptr;
    m_pPerformance = nullptr;
    m_pTelemetry = nullptr;
    m_pTime = nullptr;
    m_pPosition = nullptr;
    m_pLap = nullptr;
    m_pSession = nullptr;
    m_pMapHud = nullptr;
    m_pRadarHud = nullptr;
    m_pSpeed = nullptr;
    m_pGear = nullptr;
    m_pSpeedo = nullptr;
    m_pTacho = nullptr;
    m_pTiming = nullptr;
    m_pGapBar = nullptr;
    m_pBars = nullptr;
    m_pVersion = nullptr;
    m_pNotices = nullptr;
    m_pPitboard = nullptr;
#if GAME_HAS_RECORDS_PROVIDER
    m_pRecords = nullptr;
#endif
    m_pFuel = nullptr;
    m_pRumble = nullptr;
    m_pDirector = nullptr;
    m_pGamepad = nullptr;
    m_pLean = nullptr;
    m_pGforce = nullptr;
    m_pCompass = nullptr;
    m_pClock = nullptr;
#if GAME_HAS_TYRE_TEMP
    m_pTyreTemp = nullptr;
#endif
#if GAME_HAS_ECU
    m_pEcu = nullptr;
#endif
    m_pSessionCharts = nullptr;
    m_pHelmetOverlay = nullptr;
    m_pStatsHud = nullptr;
    m_pFmxHud = nullptr;
    m_pEventLog = nullptr;
    m_pBenchmark = nullptr;
    m_pSettingsHud = nullptr;
    m_pSettingsButton = nullptr;
    m_pPointer = nullptr;
    m_pDraggingHud = nullptr;

    // Now safe to destroy HUD objects
    m_huds.clear();
    m_quads.clear();
    m_strings.clear();

    // Clean up resource name storage
    m_spriteNames.clear();
    m_fontNames.clear();
    m_spriteBuffer.clear();
    m_fontBuffer.clear();

    DEBUG_INFO("HudManager data cleared");
}

int HudManager::initializeResources(int* piNumSprites, char** pszSpriteName, int* piNumFonts, char** pszFontName) {
    if (m_bResourcesInitialized) {
        DEBUG_WARN("HudManager resources already initialized");
        return 0;
    }

    DEBUG_INFO("HudManager initializing resources");

    // Calculate total buffer size needed for sprites
    size_t spriteBufferSize = 0;
    for (const auto& name : m_spriteNames) {
        spriteBufferSize += name.size() + 1;  // +1 for null terminator
    }

    // Build null-separated sprite names buffer
    m_spriteBuffer.resize(spriteBufferSize);
    char* bufferPos = m_spriteBuffer.data();
    for (const auto& name : m_spriteNames) {
        memcpy(bufferPos, name.c_str(), name.size() + 1);
        bufferPos += name.size() + 1;
    }

    // Calculate total buffer size needed for fonts
    size_t fontBufferSize = 0;
    for (const auto& name : m_fontNames) {
        fontBufferSize += name.size() + 1;
    }

    // Build null-separated font names buffer
    m_fontBuffer.resize(fontBufferSize);
    bufferPos = m_fontBuffer.data();
    for (const auto& name : m_fontNames) {
        memcpy(bufferPos, name.c_str(), name.size() + 1);
        bufferPos += name.size() + 1;
    }

    // Set output parameters
    int numSprites = static_cast<int>(m_spriteNames.size());
    int numFonts = static_cast<int>(m_fontNames.size());

    *piNumSprites = numSprites;
    *pszSpriteName = (numSprites > 0) ? m_spriteBuffer.data() : nullptr;

    *piNumFonts = numFonts;
    *pszFontName = (numFonts > 0) ? m_fontBuffer.data() : nullptr;

    m_bResourcesInitialized = true;

    DEBUG_INFO_F("Resources initialized: %d sprites, %d fonts", numSprites, numFonts);

    for (const auto& name : m_spriteNames) {
        DEBUG_INFO_F("Sprite: %s", name.c_str());
    }

    for (const auto& name : m_fontNames) {
        DEBUG_INFO_F("Font: %s", name.c_str());
    }

    return 0;
}

void HudManager::registerHud(std::unique_ptr<BaseHud> hud) {
    if (hud) {
        m_huds.push_back(std::move(hud));
        DEBUG_INFO_F("HUD registered, total HUDs: %zu", m_huds.size());
    }
}

#if defined(MXBMRP3_TEST_BUILD)
void HudManager::testSetAllHudsVisible(bool visible) {
    for (auto& hud : m_huds) {
        if (!hud) continue;
        BaseHud* p = hud.get();
        // Skip UI chrome and dev overlays — they aren't user "features" and
        // force-showing the settings menu/pointer would distort the profile.
        if (p == m_pSettingsHud || p == m_pSettingsButton ||
            p == m_pPointer || p == m_pBenchmark) {
            continue;
        }
        p->setVisible(visible);
    }
}
#endif

void HudManager::onDataChanged(DataChangeType changeType) {

    // Called when PluginData notifies that data has changed
    // Mark relevant HUDs as dirty based on data type
    for (auto& hud : m_huds) {
        if (hud && hud->handlesDataType(changeType)) {
            hud->setDataDirty();
        }
    }

    // Feed the auto-director. It gates internally (disabled / not spectating a race /
    // coalesced), so this is cheap on the frequent Standings change path.
    DirectorManager::getInstance().onDataChanged(static_cast<int>(changeType));

    // Check for auto profile switching when session or view state changes
    if (changeType == DataChangeType::SessionData || changeType == DataChangeType::SpectateTarget) {
        ProfileManager& profileMgr = ProfileManager::getInstance();
        if (profileMgr.isAutoSwitchEnabled()) {
            const PluginData& pluginData = PluginData::getInstance();
            int drawState = pluginData.getDrawState();

            // Determine target profile based on view state and session type
            ProfileType targetProfile;
            if (drawState == PluginConstants::ViewState::SPECTATE ||
                drawState == PluginConstants::ViewState::REPLAY) {
                targetProfile = ProfileType::SPECTATE;
            } else if (pluginData.isRaceSession()) {
                targetProfile = ProfileType::RACE;
            } else if (pluginData.isQualifySession()) {
                targetProfile = ProfileType::QUALIFY;
            } else {
                targetProfile = ProfileType::PRACTICE;
            }

            // Only switch when the game state resolves to a different profile
            // bucket than last time. This prevents overriding manual profile
            // changes when the session type hasn't actually changed.
            if (targetProfile != profileMgr.getLastAutoSwitchTarget()) {
                profileMgr.setLastAutoSwitchTarget(targetProfile);
                SettingsManager::getInstance().switchProfile(*this, targetProfile);
                // Notify SettingsHud to refresh if visible (shows current profile name)
                if (m_pSettingsHud) {
                    m_pSettingsHud->setDataDirty();
                }
            }
        }
    }
}

void HudManager::validateAllHudPositions() {
    DEBUG_INFO("Validating all HUD positions");

    for (auto& hud : m_huds) {
        if (hud) {
            hud->validatePosition();
        }
    }
}

void HudManager::markAllHudsDirty() {
    for (auto& hud : m_huds) {
        if (hud) {
            hud->setDataDirty();
        }
    }
}

void HudManager::rebuildAllIfDirty() {
    for (auto& hud : m_huds) {
        if (hud) {
            hud->rebuildIfDirty();
        }
    }
}

void HudManager::setupDefaultResources() {
    // Clear any existing resources
    m_spriteNames.clear();
    m_fontNames.clear();

    const AssetManager& assetMgr = AssetManager::getInstance();

    // Pre-allocate based on expected counts
    size_t expectedSprites = assetMgr.getTotalTextureSprites() + assetMgr.getIconCount();
    m_spriteNames.reserve(expectedSprites);
    m_fontNames.reserve(assetMgr.getFontCount());

    // Add texture sprites from AssetManager (discovered dynamically)
    // Textures are sorted alphabetically by base name, each with variants
    const auto& textures = assetMgr.getTextures();
    for (const auto& texture : textures) {
        for (int variant : texture.variants) {
            m_spriteNames.push_back(assetMgr.getTexturePath(texture.baseName, variant));
        }
    }

    DEBUG_INFO_F("Added %zu texture sprites from %zu texture bases",
        assetMgr.getTotalTextureSprites(), textures.size());

    // Add icon sprites from AssetManager (discovered dynamically)
    // Icons are sorted alphabetically
    size_t iconCount = assetMgr.getIconCount();
    for (size_t i = 0; i < iconCount; ++i) {
        m_spriteNames.push_back(assetMgr.getIconPath(i));
    }

    DEBUG_INFO_F("Added %zu icon sprites", iconCount);

    // Add fonts from AssetManager (discovered dynamically)
    size_t fontCount = assetMgr.getFontCount();
    for (size_t i = 0; i < fontCount; ++i) {
        m_fontNames.push_back(assetMgr.getFontPath(i));
    }

    DEBUG_INFO_F("Added %zu fonts", fontCount);

    DEBUG_INFO_F("Default HUD resources configured: %zu sprites, %zu fonts",
        m_spriteNames.size(), m_fontNames.size());
}

bool HudManager::isTelemetryHistoryNeeded() const {
    // Returns true if TelemetryHud is visible and showing graphs
    // This allows PluginData to skip history buffer updates when not needed
    return m_pTelemetry && m_pTelemetry->isVisible();
}


void HudManager::updateTrackCenterline(int numSegments, Unified::TrackSegment* segments, const float* raceData) {
    if (!m_bInitialized || !m_pMapHud) {
        DEBUG_WARN("HudManager: Cannot update track centerline - not initialized or MapHud not available");
        return;
    }

    DEBUG_INFO_F("HudManager: Updating track centerline with %d segments", numSegments);
    m_pMapHud->updateTrackData(numSegments, segments, raceData);
}

void HudManager::updateRiderPositions(int numVehicles, Unified::TrackPositionData* positions) {
    // Skip logging - this is a high-frequency event
    if (!m_bInitialized) {
        return;
    }

    // Update MapHud
    if (m_pMapHud) {
        m_pMapHud->updateRiderPositions(numVehicles, positions);
    }

    // Update RadarHud
    if (m_pRadarHud) {
        m_pRadarHud->updateRiderPositions(numVehicles, positions);
    }

    // Update GapBarHud (for flat map mode)
    if (m_pGapBar) {
        m_pGapBar->updateRiderPositions(numVehicles, positions);
    }

    // Update CompassWidget (heading from the displayed rider's yaw)
    if (m_pCompass) {
        m_pCompass->updateRiderPositions(numVehicles, positions);
    }

    // Update centralized lap timer and HUDs with track position for S/F detection
    PluginData& pluginData = PluginData::getInstance();
    int displayRaceNum = pluginData.getDisplayRaceNum();

    // Find the display rider's position data
    for (int i = 0; i < numVehicles; ++i) {
        if (positions[i].raceNum == displayRaceNum) {
            // Get lap number from standings
            const StandingsData* standing = pluginData.getStanding(displayRaceNum);
            int lapNum = standing ? standing->numLaps : 0;

            // Update centralized lap timer (used by TimingHud, IdealLapHud, and others)
            pluginData.updateLapTimerTrackPosition(
                displayRaceNum,
                positions[i].trackPos,
                lapNum
            );

            // Update GapBarHud
            if (m_pGapBar) {
                m_pGapBar->updateTrackPosition(
                    displayRaceNum,
                    positions[i].trackPos,
                    lapNum
                );
            }
            break;
        }
    }
}
