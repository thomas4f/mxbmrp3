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
#include "spotter_manager.h"
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
#include "../hud/spotter_widget.h"
#include "../hud/position_widget.h"
#include "../hud/lap_widget.h"
#include "../hud/session_hud.h"
#include "../hud/speed_widget.h"
#include "../hud/gear_widget.h"
#include "../hud/crash_widget.h"
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
    createHud(m_pHelmetOverlay, "helmet_overlay");
    createHud(m_pStandings, "standings_hud");
    createHud(m_pMapHud, "map_hud");
    createHud(m_pRadarHud, "radar_hud");
    createHud(m_pLapLog, "lap_log_hud");
#if GAME_HAS_STEAM_FRIENDS
    createHud(m_pFriends, "friends_hud");
#endif
    createHud(m_pIdealLap, "ideal_lap_hud");
    createHud(m_pTelemetry, "telemetry_hud");
    createHud(m_pPerformance, "performance_hud");
    // Pitboard and Gamepad declare no texture stem in their constructors: their
    // background is a PACK (art + the geometry that places content on it), resolved
    // by name through AssetManager rather than through the texture-variant machinery.
    createHud(m_pPitboard, "pitboard_hud");
#if GAME_HAS_RECORDS_PROVIDER
    createHud(m_pRecords, "records_hud");
#endif
    createHud(m_pSessionCharts, "session_charts_hud");
#if GAME_HAS_FMX
    createHud(m_pFmxHud, "fmx_hud");
#endif
    createHud(m_pStatsHud, "stats_hud");
    createHud(m_pEventLog, "event_log_hud");

    // Benchmark Widget (always created, but only accessible via settings when developer mode is on)
    // Note: Must be created unconditionally because isDeveloperMode() returns false here -
    // settings are loaded later in initialize(). The settings tab gates access at render time.
    createHud(m_pBenchmark, "benchmark_widget");

    // Widgets
    createHud(m_pLap, "lap_widget");
    createHud(m_pPosition, "position_widget");
    createHud(m_pTime, "time_widget");
    createHud(m_pSession, "session_widget");  // Keep same texture for backwards compatibility
    createHud(m_pSpeed, "speed_widget");
    createHud(m_pGear, "gear_widget");
    createHud(m_pCrash, "crash_widget");
    createHud(m_pSpeedo, "speedo_widget");
    createHud(m_pTacho, "tacho_widget");
    createHud(m_pTiming, "timing_hud");
    createHud(m_pGapBar, "gap_bar_hud");
    createHud(m_pBars, "bars_widget");
    createHud(m_pVersion, "version_widget");
    createHud(m_pNotices, "notices_hud");
    createHud(m_pSpotter, "spotter_widget");  // spotter subtitle; content-gated on [Spotter] settings
    createHud(m_pFuel, "fuel_widget");
    createHud(m_pRumble, "rumble_hud");
    createHud(m_pDirector, "director_widget");
    createHud(m_pGamepad, "gamepad_widget");
    createHud(m_pLean, "lean_widget");
    createHud(m_pGforce, "gforce_widget");
    createHud(m_pCompass, "compass_widget");
    createHud(m_pClock, "clock_widget");
#if GAME_HAS_TYRE_TEMP
    createHud(m_pTyreTemp, "tyre_temp_widget");
#endif
#if GAME_HAS_ECU
    createHud(m_pEcu, "ecu_widget");
#endif

    // Create PointerWidget early so it can be passed to SettingsHud
    // (will be registered last to render on top)
    auto pointerPtr = std::make_unique<PointerWidget>();
    m_pPointer = pointerPtr.get();
    bindHudSlot(m_pPointer);

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
    bindHudSlot(m_pSettingsButton);

    auto settingsPtr = std::make_unique<SettingsHud>(m_pIdealLap, m_pLapLog, m_pFriends, m_pSessionCharts, m_pStandings,
                                                       m_pPerformance, m_pTelemetry, m_pTime, m_pPosition, m_pLap, m_pSession, m_pMapHud, m_pRadarHud, m_pSpeed, m_pGear, m_pCrash, m_pSpeedo, m_pTacho, m_pTiming, m_pGapBar, m_pBars, m_pVersion, m_pNotices, m_pPitboard, recordsHudPtr, m_pFuel, m_pPointer, m_pRumble, m_pGamepad, m_pLean, m_pGforce, m_pCompass,
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
    bindHudSlot(m_pSettingsHud);
    registerHud(std::move(settingsPtr), "settings_hud");

    // Register SettingsButtonWidget - draggable button to toggle settings
    // (created earlier so SettingsHud could reference it; registered here for draw order)
    registerHud(std::move(settingsButtonPtr), "settings_button");

    // Register PointerWidget last so it renders on top of everything
    registerHud(std::move(pointerPtr), "pointer_widget");

    // Register all HUDs for benchmark profiling (developer mode only)
    if (m_pBenchmark) {
        auto& bm = PluginData::getInstance().getBenchmarkMetrics();
        for (auto& hud : m_huds) {
            if (hud && hud.get() != m_pBenchmark) {
                // The registration name, which every element has by construction.
                // This was an if-else ladder: texture base name, else one of eight
                // `hud.get() == m_pX` special cases, else the literal "unknown".
                // Every element it could reach had a name, so the "unknown" arm was
                // in fact unreachable and one special case (pointer) was redundant
                // with the base name it already had -- the ladder was dead weight and
                // a drift risk rather than a source of wrong rows. It mattered that
                // it drift: bm.registerHud does not dedupe, and
                // tools/benchmark_report.py keys its table on this string, so two
                // elements answering to one name would silently merge.
                const char* name = hud->getHarnessId();
                int idx = bm.registerHud(name);
                // -1 means the registry is FULL, not that this HUD opted out -- and
                // the two are indistinguishable downstream, which is how ten panels
                // went unprofiled without a word. Say so.
                if (idx < 0) {
                    DEBUG_ERROR_F("Benchmark registry full (MAX_HUDS=%d); '%s' will not be profiled",
                                  BenchmarkMetrics::MAX_HUDS, name);
                }
                hud->setBenchmarkIndex(idx);
            }
        }
    }

    // Load settings from disk (must happen after HUD registration)
    SettingsManager::getInstance().loadSettings(*this, PluginManager::getInstance().getSavePath());

    // Load the spotter's cue pack for whatever name survived the settings
    // load. A `pack=` INI line already loaded it (this re-read is a cheap
    // no-op then), but a FRESH INSTALL has no [Spotter] section, and the
    // default pack name must still resolve to its files — the shipped packs
    // are what keep the spotter audible where SAPI TTS doesn't exist
    // (Wine/Proton players).
    SpotterManager::getInstance().reloadCuePack();

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

    // Reset cached HUD pointers BEFORE destroying the objects, to prevent any
    // dangling pointer window. Every typed cache pointer was enrolled here by
    // createHud()/bindHudSlot() at registration, so a HUD added tomorrow is
    // covered without touching this function — the hand-maintained null list
    // this replaces had to be kept in sync by review alone.
    for (auto& clearSlot : m_hudSlotClearers) {
        clearSlot();
    }
    m_hudSlotClearers.clear();
    m_pDraggingHud = nullptr;  // transient drag state, not a registered HUD slot

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

int HudManager::profilableHudCount() const {
    int n = 0;
    for (const auto& h : m_huds) {
        if (h && h.get() != static_cast<const BaseHud*>(m_pBenchmark)) ++n;
    }
    return n;
}

void HudManager::registerHud(std::unique_ptr<BaseHud> hud, const char* harnessId) {
    if (hud) {
        hud->setHarnessId(harnessId);
        m_huds.push_back(std::move(hud));
        DEBUG_INFO_F("HUD '%s' registered, total HUDs: %zu", harnessId, m_huds.size());
    }
}

#if defined(MXBMRP3_TEST_BUILD)
void HudManager::testInstallSpriteTable(int count) {
    m_spriteNames.clear();
    for (int i = 1; i <= count; ++i) m_spriteNames.push_back("test_sprite_" + std::to_string(i));
}

void HudManager::testLastFrameSpriteSpan(int& minSprite, int& maxSprite,
                                         int& untexturedCount) const {
    minSprite = 0;
    maxSprite = 0;
    untexturedCount = 0;
    for (const SPluginQuad_t& q : m_quads) {
        if (q.m_iSprite <= 0) { ++untexturedCount; continue; }
        if (minSprite == 0 || q.m_iSprite < minSprite) minSprite = q.m_iSprite;
        if (q.m_iSprite > maxSprite) maxSprite = q.m_iSprite;
    }
}

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

    // Pre-allocate based on expected counts (3-11 sprites per theme, see ThemeAsset)
    size_t expectedThemeSprites = 0;
    for (const auto& theme : assetMgr.getThemes()) expectedThemeSprites += theme.spriteFiles.size();
    size_t expectedSprites = assetMgr.getTotalTextureSprites() + assetMgr.getIconCount()
                           + expectedThemeSprites
                           + assetMgr.getGamepadCount() * GamepadSprite::COUNT
                           + assetMgr.getPitboardCount() * PitboardSprite::COUNT;
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

    // Add theme sprites LAST, in the same corner/edge/center order discoverThemes()
    // assigned indices in -- the ThemeAsset indices are absolute positions in this
    // array, so the two orders must not diverge.
    size_t themeSprites = 0;
    for (const auto& theme : assetMgr.getThemes()) {
        for (const std::string& stem : theme.spriteFiles) {
            m_spriteNames.push_back(assetMgr.getThemePath(theme.name, stem.c_str()));
            ++themeSprites;
        }
    }

    DEBUG_INFO_F("Added %zu theme sprites from %zu themes",
        themeSprites, assetMgr.getThemeCount());

    // Gamepad packs LAST, walking GamepadSprite::kStems in the same order
    // discoverGamepads() handed out indices in. Unlike the theme block above there
    // is no per-pack file list to keep in step: both sides iterate the one table,
    // so "this pack draws another pack's sprites" is not expressible here.
    for (const GamepadAsset& pad : assetMgr.getGamepads()) {
        for (int i = 0; i < GamepadSprite::COUNT; ++i) {
            // A skin's missing stems resolve to its base pack's files -- the
            // per-stem decision discovery recorded, replayed here so the file
            // list matches exactly what was accepted.
            m_spriteNames.push_back(assetMgr.getGamepadPath(
                pad.spriteFromBase[i] ? pad.baseName : pad.name,
                GamepadSprite::kStems[i]));
        }
    }

    DEBUG_INFO_F("Added %zu gamepad sprites from %zu packs",
        assetMgr.getGamepadCount() * GamepadSprite::COUNT, assetMgr.getGamepadCount());

    // Pit board packs after the pads, walking PitboardSprite::kStems in the order
    // discoverPitboards() assigned indices in -- same one-table rule.
    for (const PitboardAsset& board : assetMgr.getPitboards()) {
        for (int i = 0; i < PitboardSprite::COUNT; ++i) {
            m_spriteNames.push_back(assetMgr.getPitboardPath(
                board.spriteFromBase[i] ? board.baseName : board.name,
                PitboardSprite::kStems[i]));
        }
    }

    DEBUG_INFO_F("Added %zu pitboard sprites from %zu packs",
        assetMgr.getPitboardCount() * PitboardSprite::COUNT, assetMgr.getPitboardCount());

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
    // Lets PluginData skip history-buffer accumulation when nothing consumes it.
    // MUST use isVisibleAnySurface(), not isVisible(): TelemetryHud::update()
    // rebuilds whenever it is visible on EITHER surface, so gating production on
    // the game flag alone leaves a companion-only telemetry HUD rebuilding from
    // buffers nobody fills — empty graphs. This is the visibility-gate invariant
    // applied to the DATA side: the consumer's gate and the producer's gate have
    // to ask the same question.
    return m_pTelemetry && m_pTelemetry->isVisibleAnySurface();
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
